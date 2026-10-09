// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#define _CRT_SECURE_NO_WARNINGS
#include <App.hpp>

#ifndef NDEBUG
#	include <filesystem>
#endif//NDEBUG

#ifdef NDEBUG
extern "C" {
#	include <libavutil/log.h>
}
#endif//NDEBUG

#include <inttypes.h>

#if USE_IMGUI
namespace ImGui {
	static void DockingSpace() {
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		constexpr ImGuiWindowFlags dock_flags = ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("MainDockSpace", nullptr, dock_flags);
		ImGui::PopStyleVar(3);

		ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
	}
}
#endif

std::unique_ptr<gl::Window> App::makeWindow() {
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	auto res = std::make_unique<gl::Window>(800, 600, glm::ivec2{ 200, 100 }, "client");
	glfwMakeContextCurrent(res->m_get);
	GLASSERTK(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	return res;
}

template<bool lock>
bool App::isHost() {
	if constexpr (lock) {
		std::lock_guard lock(mtx_users);
		return users[client.Id()].is_host.load(std::memory_order_acquire);
	}
	else {
		return users[client.Id()].is_host.load(std::memory_order_acquire);
	}
}

bool App::clientClosed() {
	std::lock_guard lock(mtx_close);
	return client.Closed();
}

void App::Receive() {
	H264Decoder decoder;

	audioPlayer.start();
	DEFER{ [this]() {
		audioPlayer.stop();
		p_show.wait_threads.release();
	}};

	struct {
		Signal signals{};
		net::socket_t id{};
		std::string other_name{};
		bool is_host_from_server;
		bool is_host_from_client;
		net::socket_t wanna_kick_off;
		std::vector<float> audioFrames;
		int32_t pk_size{};
		std::vector<uint8_t> pk{};
	} rv;

	while (!p_show.ended() && !clientClosed()) {
		bool recv_ok{true};
		recv_ok &= client.ReceiveBy(rv.signals);
		recv_ok &= client.ReceiveBy(rv.id);
		if (!recv_ok) [[unlikely]] { break; }
		if (rv.signals & signal_closed) {
			{
				std::lock_guard lock(mtx_users);
				auto count = std::erase_if(users, [&](auto& user) { return user.first == rv.id;});
			}
			chosen_user.store(client.Id(), std::memory_order_release);
			continue;
		}

		recv_ok &= client.ReceiveBy(rv.other_name);
		recv_ok &= client.ReceiveBy(rv.is_host_from_server);
		recv_ok &= client.ReceiveBy(rv.is_host_from_client);
		if (!recv_ok) [[unlikely]] { break; }
		if (rv.is_host_from_client) {
			recv_ok &= client.ReceiveBy(rv.wanna_kick_off);
		}
		recv_ok &= client.ReceiveBy(rv.audioFrames);
		if (!recv_ok) [[unlikely]] { break; }

		if (rv.wanna_kick_off == client.Id()) {
			p_show.setEnd(true, ShowPara::End::kickOff);
		}
		{
			std::lock_guard lock(mtx_users);
			auto [itr, first] = users.try_emplace(rv.id);
			itr->second.name = rv.other_name;
			users[rv.id].audioBuf.push(rv.audioFrames.begin(), rv.audioFrames.end());
			users[rv.id].is_host.store(rv.is_host_from_server, std::memory_order_release);
		}

		if (rv.signals & signal_choiceNotMatch) { continue; }

		// each packet
		if (!client.ReceiveBy(rv.pk_size)) [[unlikely]] { break; }
		for (int i = 0;i < rv.pk_size;++i) {
			//auto pk = client.Receive();
			if (!client.ReceiveBy(rv.pk)) [[unlikely]] { break; }
			auto frames = decoder.DecodePacket(rv.pk);
			if (!frames.empty()) {
				std::lock_guard<std::mutex> lock(mtx_video_frames);
				size_t current_size = total_video_frames.size();
				size_t new_count = frames.size();

				if (current_size + new_count > MAX_VIDEO_FRAMES) {
					while (!total_video_frames.empty())
						total_video_frames.pop();

					size_t keep = std::min(KEEP_FRAMES, new_count);
					auto start = frames.end() - keep;
					for (auto it = start; it != frames.end(); ++it) {
						total_video_frames.push(std::move(*it));
					}
				}
				else {
					for (auto& frame : frames) {
						total_video_frames.push(std::move(frame));
					}
				}
			}
		}

	}
}

void App::Send() {
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};
	ScreenCapture capture;
	assert(capture.Initialize());

	while (!capture.CaptureFrame());
	const int w = capture.Width() & ~1;
	const int h = capture.Height() & ~1;
	ui.video.encode.width.store(w, std::memory_order_release);
	ui.video.encode.height.store(h, std::memory_order_release);
	ui.video.encode.wait_wh.release();
	ST::H264Encoder encoder(w, h, ui.video.encode.fps, ui.video.encode.bitrate);

	audio::Capture ad_cpt{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	ad_cpt.Start();

	auto last_time = std::chrono::steady_clock::now();
	while(!p_show.ended() && !clientClosed()){
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(1.0 / ui.send.fps.load(std::memory_order_acquire))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		if (!capture.CaptureFrame()) {
			continue;
		}
		auto& rgba = capture.GetBuffer();

		if (rgba.empty()) {
			continue;
		}

		std::vector<std::vector<uint8_t>> packets;
		{
			std::lock_guard lock(ui.video.encode.mtx);
			encoder.Reset(w, h, ui.video.encode.width, ui.video.encode.height, ui.video.encode.fps, ui.video.encode.bitrate);
			packets = encoder.EncodeFrame(rgba);
		}
		int32_t pk_size = (int32_t)packets.size();

		bool is_host_from_client = isHost<true>();
		client.Send(client.Id());
		{
			std::lock_guard lock(logger.mtx_name);
			client.Send(logger.name);
		}
		client.Send(is_host_from_client);
		if (is_host_from_client) {
			client.Send(ui.account.wanna_kick_off.load(std::memory_order_acquire));
			ui.account.wanna_kick_off.store(net::invalid_socket, std::memory_order_release);
		}
		client.Send(ad_cpt.Frames());//move
		client.Send(chosen_user.load(std::memory_order_acquire));
		client.Send(pk_size);
		for (auto& pk : packets) {
			client.Send(pk);
		}

	}
}

void App::audioMix()
{
	DEFER{
		[this]() { p_show.wait_threads.release(); }
	};

	const size_t samples =
		audio::mixPeriods *
		audio::periodSizeInFrames *
		audio::channels;

	std::vector<float> mixBuf(samples);
	std::vector<float> getBuf(samples);

	while(!p_show.ended() && !clientClosed()) {

		std::this_thread::sleep_for(5ms);

		std::fill( mixBuf.begin(), mixBuf.end(), 0.0f );

		size_t realSamples = 0;

		/*
		 * ========================================================
		 * 多用户混音
		 * ========================================================
		 */
		{
			std::lock_guard lock{ mtx_users };

			for (auto& user : users) {

				const size_t thisSamples =
					user.second.audioBuf.pop( getBuf.data(), samples );

				realSamples = std::max( realSamples, thisSamples );

				for (size_t i = 0; i < thisSamples; ++i)
				{
					mixBuf[i] += getBuf[i];
				}
			}
		}

		/*
		 * 没有任何音频。
		 */
		if (realSamples == 0) { continue; }

		/*
		 * ========================================================
		 * 防止混音溢出
		 * ========================================================
		 */
		auto max_it =
			std::ranges::max_element(
				mixBuf.begin(),
				mixBuf.begin() + realSamples,
				[](float v1, float v2) {
					return std::abs(v1) <
						std::abs(v2);
				}
			);

		if ( max_it != mixBuf.begin() + realSamples) {
			const float maxVal =
				std::abs(*max_it);

			if (maxVal > 1.0f) {

				std::ranges::for_each(
					mixBuf.begin(),
					mixBuf.begin() + realSamples,
					[maxVal](float& v) {
						v /= maxVal;
					}
				);
			}
		}
		{
			std::lock_guard lock(mtx_audioFramesLong);
			audioFramesLong.insert(audioFramesLong.end(), mixBuf.begin(), mixBuf.begin() + realSamples);
		}
		audioUser.pushFrames(
			mixBuf.data(),
			realSamples
		);
	}
}

void App::asrRecv()
{
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};
	std::vector<float> audioBuf;
	while(!p_show.ended() && !clientClosed()) {
		std::this_thread::sleep_for(200ms);

		{
			std::lock_guard lock(mtx_audioFramesLong);
			audioBuf.resize(audioFramesLong.size());
			std::copy(audioFramesLong.begin(), audioFramesLong.end(), audioBuf.begin());
		}
		if (audioBuf.empty()) { continue; }

		int out_bytes = 0;

		// println( "asr.transcribe begin, " << "samples = " << audioBuf.size() << ", seconds = " << static_cast<double>( audioBuf.size() ) / 16000.0 );

		const char* str = plug.asr.transcribe( audioBuf.data(), static_cast<int>( audioBuf.size() ), &out_bytes );
		/*
		 * ========================================================
		 * 保存识别结果
		 * ========================================================
		 */
		if ( str ) {
			printf("got it\n");
			{
				std::lock_guard lock(mtx_audioFramesLong);
				audioFramesLong.clear();
			}
			{
				std::lock_guard lock(mtx_speechWords);
				speechWords.assign(str, out_bytes);
				printf("get: %s\n", speechWords.c_str());
			}
		} else if(speechWords.size() > audio::sampleRate * audio::channels * 20){
			printf("not yet\n");
			speechWords.clear();
		}
	}
}

void App::ShowPara::setEnd(bool ended, End showEnd) {
	m_ended.store(ended, std::memory_order_release);
	m_showEnd = showEnd;
}

void App::ShowPara::restore() {
	m_ended.store(false, std::memory_order_release);
	m_showEnd = End::Default;
}

void App::restoreAfterShow() {
	p_show.restore();
	users.clear();
}

App::Page App::Show() {

	chosen_user.store(client.Id(), std::memory_order_release);
	{
		std::lock_guard lock2{ mtx_users };
		auto [itr, first] = users.try_emplace(chosen_user.load(std::memory_order_acquire));
		itr->second.name = logger.name;
	}

	constexpr size_t thread_count = 4;

	DEFER{ [this, thread_count]() {
		client.Close();
		for (size_t i = 0;i < thread_count;++i) {
			p_show.wait_threads.acquire();
		}
		restoreAfterShow();
	}};

	std::array<std::jthread, thread_count> threads{
		std::jthread{ &App::audioMix, this },
		std::jthread{ &App::Send, this },
		std::jthread{ &App::Receive, this },
		std::jthread{ &App::asrRecv, this },
	};

	ui.video.encode.wait_wh.acquire();

	//assert(thread_count == threads.size());

	glfwSetWindowTitle(window->m_get, ("client: " + logger.name).c_str());

	constexpr const char* vs = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoords;
out vec2 texCoords;
void main(){
	texCoords = aTexCoords;
	gl_Position = vec4(aPos.x, -aPos.y, aPos.z, 1.0f);
}
)";
	constexpr const char* fs = R"(
#version 330 core
in vec2 texCoords;
uniform sampler2D sampler;
out vec4 FragColor;
void main(){
	FragColor = texture(sampler, texCoords);
}
)";
	struct imgStruct {
		float pos[3];
		float texCoords[2];
	};

	gl::Shader shader{ vs, fs };
	shader.use();
	shader.setUniform("sampler", 0);
	gl::Texture2D img(1, 1, { 0xFF,0x77,0x44,0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);
	img.bindTo(0);
	gl::VertexBufferLayout layoutImg;
	layoutImg.push<float>(3);
	layoutImg.push<float>(2);
	gl::VertexArray vaoImg;
	std::vector<imgStruct> verticesImg{
		{{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
		{{ 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
		{{-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f}},
		{{ 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f}},
	};
	gl::VertexBuffer vboImg(layoutImg, gl::toVector<uint8_t>(verticesImg), gl::BufferUsage::Static);
	gl::IndexBuffer eboImg({ 0,1,2,1,2,3 }, gl::BufferUsage::Static);
	vaoImg.bindVboAllAttribs({ 0,1 }, vboImg);


#if USE_IMGUI
	const auto DEFAULT_BK_COLOR = style->Colors[ImGuiCol_WindowBg];
#endif

	double d_time = 0;
	float fps = 0;
	bool show_settings = true;
	char rename[256]{};

	auto last_time = std::chrono::steady_clock::now();
	auto renderDealing = [&]() {
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(1.0 / ui.video.fps.load(std::memory_order_acquire))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

#if USE_IMGUI
		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
#endif

		glClearColor(0.1f, 0.1f, 0.1f, 0.1f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		shader.use();
		vaoImg.bind();
		gl::draw(eboImg, gl::DrawMode::TRIANGLES);

		DEFER {
			[this]() {window->swapBuffers();}
		};

#if USE_IMGUI
		ImGui::DockingSpace();

		// ============ setting window ============ 
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			show_settings = !show_settings;

	if (show_settings) {
		ImGui::Begin((char*)u8"settings");
		if (ImGui::TreeNode("send")) {
			bool val;
			// share_video
			val = ui.send.share_video.load(std::memory_order_acquire);
			ImGui::Checkbox("share_video", &val);
			ui.send.share_video.store(val, std::memory_order_release);
			// share_audio
			val = ui.send.share_audio.load(std::memory_order_acquire);
			ImGui::Checkbox("share_audio", &val);
			ui.send.share_audio.store(val, std::memory_order_release);
			// fps
			auto fd = ui.send.fps.load(std::memory_order_acquire);
			ImGui::SliderScalar("fps", ImGuiDataType_Double, &fd, &MIN_MAX_FPS, &MAX_MAX_FPS);
			ui.send.fps.store(fd, std::memory_order_release);
			ImGui::TreePop();
		}// send
		if (ImGui::TreeNode("vedio")) {
			// fps
			if (d_time > 0.4) {
				fps = ImGui::GetIO().Framerate;
				d_time = 0;
			}
			ImGui::Text("FPS: %.3f", fps);
			auto fv = ui.video.fps.load(std::memory_order_acquire);
			ImGui::SliderScalar("fps", ImGuiDataType_Double, &fv, &MIN_MAX_FPS, &MAX_MAX_FPS);
			ui.video.fps.store(fv, std::memory_order_release);
			if (ImGui::TreeNode("encode")) {
				// fps
				int fps = ui.video.encode.fps.load(std::memory_order_acquire);
				ImGui::SliderInt("fps", &fps, static_cast<int>(MIN_MAX_FPS), static_cast<int>(MAX_MAX_FPS));
				ui.video.encode.fps.store(fps, std::memory_order_release);
				// bitrate
				int bitrate = ui.video.encode.bitrate.load(std::memory_order_acquire);
				ImGui::SliderInt("bitrate", &bitrate, video::min_bitrate, video::max_bitrate);
				ui.video.encode.bitrate.store(bitrate, std::memory_order_release);
				ImGui::TreePop();
				// width
				int w = ui.video.encode.width.load(std::memory_order_acquire);
				ImGui::SliderInt("width", &w, 400, 10000);
				ui.video.encode.width.store(w, std::memory_order_release);
				// height
				int h = ui.video.encode.height.load(std::memory_order_acquire);
				ImGui::SliderInt("height", &h, 400, 10000);
				ui.video.encode.height.store(h, std::memory_order_release);
			}// encode
			ImGui::TreePop();
		}// vedio
		if (ImGui::TreeNode("account")) {
			// room id
			uint32_t const_room_id = logger.room_id;
			ImGui::InputScalar("room_id", ImGuiDataType_U32, &const_room_id);
			// rename
			ImGui::InputText("##rename", rename, sizeof(rename), ImGuiInputTextFlags_CharsNoBlank);
			ImGui::SameLine();
			if (ImGui::Button("rename")) {
				{
					std::lock_guard lock(logger.mtx_name);
					logger.name = rename;
				}
				glfwSetWindowTitle(window->m_get, ("client: " + logger.name).c_str());
			}
			{
				std::lock_guard lock(mtx_users);
				auto chosen = users.find(chosen_user.load(std::memory_order_acquire));
				assert(chosen != users.end());
				auto name_chosen = users[chosen->first].name + (users[chosen->first].is_host.load(std::memory_order_acquire) ? "(host)" : "");
				if (ImGui::BeginCombo("target", name_chosen.c_str())) {
					for (const auto& user : users) {
						auto name = user.second.name + (user.second.is_host.load(std::memory_order_acquire) ? "(host)" : "");
						if (ImGui::Selectable(name.c_str())) {
							chosen_user.store(user.first, std::memory_order_release);
							println("chosen socket: " << user.first);
						}
					}
					ImGui::EndCombo();
				}
				if (isHost<false>() && ImGui::BeginCombo("kick_off", "invalid user")) {
					(void)ImGui::Selectable("invalid user");
					for (const auto& user : users) {
						if (user.second.is_host.load(std::memory_order_acquire)) {
							continue;
						}
						if (ImGui::Selectable(user.second.name.c_str())) {
							ui.account.wanna_kick_off.store(user.first, std::memory_order_release);
							println("kick off socket: " << user.first);
						}
					}
					ImGui::EndCombo();
				}
			}
			ImGui::TreePop();
		}// account
		if (ImGui::TreeNode("plugin")) {
			ImGui::BeginChild("##speech", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::PushTextWrapPos(0.0f);   // 0 表示按窗口右边界换行
			{
				std::lock_guard lock(mtx_speechWords);
				ImGui::TextUnformatted(speechWords.c_str());
			}
			ImGui::PopTextWrapPos();
			ImGui::EndChild();
			ImGui::TreePop();
		}// plugin
		ImGui::End();
	}// settings


		// ============ main window ============ 
		style->Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // Alpha = 0
		ImGui::Begin("main");
		ImGui::End();// main
		style->Colors[ImGuiCol_WindowBg] = DEFAULT_BK_COLOR;
		// ============ main window ============ 

		ImGui::End();//docking end

		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		d_time += 1 / (double)ImGui::GetIO().Framerate;
#endif

		std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_TIME));


		ST::DecodedFrame frame;
		{
			std::lock_guard<std::mutex> lock(mtx_video_frames);
			if (total_video_frames.empty()) {
				//PL;
				return;
			}
			else {
				//PL;
			}

			frame = std::move(total_video_frames.front());
			total_video_frames.pop();
		}

		int width = frame.width;
		int height = frame.height;
		auto& data = frame.rgba;

		if (data.size() != width * height * 4) {
			return;
		}

		img.resize(width, height, 4);
		img.resetData(data, 4);
	};
	any_usage = &renderDealing;
	using renderFuncType = decltype(renderDealing);
	glfwSetWindowUserPointer(window->m_get, this);
	glfwSetWindowPosCallback(window->m_get, [](GLFWwindow* window, int xpos, int ypos) {
		auto user = static_cast<App*>(glfwGetWindowUserPointer(window));
		user->window->updatePos({ xpos, ypos });
		auto renderDealing = *static_cast<renderFuncType*>(user->any_usage);
		renderDealing();
		});
	glfwSetFramebufferSizeCallback(window->m_get, [](GLFWwindow* window, int width, int height) {
		auto user = static_cast<App*>(glfwGetWindowUserPointer(window));
		glViewport(0, 0, width, height);
		user->window->setViewport({ 0,0,width, height });
		auto renderDealing = *static_cast<renderFuncType*>(user->any_usage);
		renderDealing();
		});
	while (!p_show.ended() && !clientClosed()) {
		if (window->shouldClose()) { exit(1); }
		glfwPollEvents();
		renderDealing();
	}

	println("show ended");
	switch (p_show.showEnd()) {
	case ShowPara::End::clientClosed:
		println("enter clientClosed");
		return makeTmp("clientClosed", "your tcp socket closed in the middle.", Page::connectToServer);
		break;
	case ShowPara::End::kickOff:
		println("enter kickOff");
		return makeTmp("kickOff", "you are kicked off by the room host.", Page::connectToServer);
		break;
	default:
		assert(false);
	}
	return Page::Tmp;
}

App::Page App::Tmp() {
	while (1) {
		PageRenderGuard pageRenderGuard{ this, p_tmp.title.c_str(), 20};
		ImGui::Text(p_tmp.content.c_str());
		if (ImGui::Button("<-")) { break; }
	}
	return p_tmp.back;
}

App::Page App::makeTmp(const std::string& title, std::string content, Page back) {
	p_tmp.title = title;
	p_tmp.content = content;
	p_tmp.back = back;
	return Page::Tmp;
}

void App::pageRenderBegin(const char* title, int sleepTime) {
	std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
	if (window->shouldClose()) { exit(1); }
	glfwPollEvents();
#if USE_IMGUI
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
#endif

	glClearColor(0.1f, 0.1f, 0.1f, 0.1f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#if USE_IMGUI
	ImGui::DockingSpace();
	ImGui::Begin(title);
#endif
}

void App::pageRenderEnd() {
#if USE_IMGUI
	ImGui::End();// settings
	ImGui::End();//docking end
	// Rendering
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif	
	window->swapBuffers();
}

App::Page App::chooseMode() {
	while(1) {
		PageRenderGuard pageRenderGuard{ this, "choose mode", 20 };
		if (ImGui::Button("<-")) { client.Send(true); return Page::connectToServer; }
		if (ImGui::Button("enter a room")) {
			client.Send(false);
			client.Send(choice_enter_room);
			return Page::enterRoom;
		}
		if (ImGui::Button("make a room")) {
			client.Send(false);
			client.Send(choice_make_room);
			return Page::makeRoom;
		}
	}
	return Page::Tmp;
}

App::Page App::makeRoom() {
	logger.room_id = Room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	while (1) {
		while (1) {
			PageRenderGuard PageRenderGuard{ this, "passwd", 20 };
			if (ImGui::Button("<-")) { client.Send(true); return Page::chooseMode; }
			ImGui::InputScalar("passwd", ImGuiDataType_U32, &p2.passwd);
			if (p2.passwd == Room::invalid_passwd) {
				ImGui::Text("invalid passwd: %d", (int)Room::invalid_passwd);
			}
			else if(ImGui::Button("->")) { break; }
		}
		logger.passwd = p2.passwd;
		client.Send(false);
		client.Send(logger.passwd);
		uint32_t room_id0{};
		if (!client.ReceiveBy(room_id0)) {
			return makeTmpServerStatusError();
		}
		logger.room_id = room_id0;
		return Page::Show;
	}
}

App::Page App::enterRoom() {
	logger.room_id = Room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	bool room_id_not_exist{ false };
	bool passwd_wrong{ false };
	for (;;) {
		PageRenderGuard pageRenderGuard(this, "enter room", 20);
		if (ImGui::Button("<-")) { client.Send(true); return Page::chooseMode; }
		ImGui::InputScalar("room_id", ImGuiDataType_U32, &p3.room_id);
		ImGui::InputScalar("passwd", ImGuiDataType_U32, &p3.passwd);
		ImGui::Text("both: 1~4294967295");
		bool want_continue{ false };
		if (p3.room_id == Room::invalid_id)		{ ImGui::Text("room_id format invalid"); want_continue = true; }
		if (p3.passwd == Room::invalid_passwd)	{ ImGui::Text("passwd format invalid"); want_continue = true; }
		if (want_continue) { continue; }
		if (room_id_not_exist)	{ ImGui::Text("room id not exist"); } 
		if (passwd_wrong)		{ ImGui::Text("passwd wrong"); }
		if (ImGui::Button("->")) {
			client.Send(false);
			client.Send(p3.room_id);
			client.Send(p3.passwd);
			bool room_id_ok{};
			if (!client.ReceiveBy(room_id_ok)) { return makeTmpServerStatusError(); }
			if (!room_id_ok)					{ room_id_not_exist = true; continue; }
			else { room_id_not_exist = false; }
			bool passwd_ok{};
			if (!client.ReceiveBy(passwd_ok))	{ return makeTmpServerStatusError(); }
			if (!passwd_ok)						{ passwd_wrong = true; continue; }
			else { passwd_wrong = false; }
			break;
		}
	}
	logger.passwd = p3.passwd;
	logger.room_id = p3.room_id;
	return Page::Show;
}

App::Page App::connectToServer() {
	logger.name = {};

	bool first{ true };
	loop {
		client = net::tcp::Client{ net::tcp::Ip::v4 };
		loop {
			PageRenderGuard pageRenderGuard{this, "connect to server", 20 };
			ImGui::Text("your socket: %zu", static_cast<size_t>(client.Id()));
			ImGui::InputText("target_ipv4", p1.ipv4, p1.buf_size);
			ImGui::InputScalar("target_port", ImGuiDataType_U16, &p1.port);
			ImGui::InputText("your_name", p1.name, p1.buf_size, ImGuiInputTextFlags_CharsNoBlank);
			bool ok{ true };
			if (!first)						{ ImGui::Text("connect failed"); } 
			if (strlen(p1.name) == 0)		{ ImGui::Text("name should not be blank"); ok = false; }
			if (p1.port == p1.invalid_port)	{ ImGui::Text("%s: port invalid - should be a unsigned number not 0", p1.port); ok = false; }
			if (ok && ImGui::Button("connect")) { break; }
		}
		if (client.ConnectTo(net::tcp::Ip::v4, p1.ipv4, p1.port)) {
			println(p1.ipv4);
			println(p1.port);
			println("connected to server successfully");
			break;
		}
		else {
			println("connected to server failed");
			first = false;
		}
	}
	logger.name = p1.name;
	println("socket: " << client.Id());
	println("name: " << logger.name);
	return Page::chooseMode;
}

void App::run() {
#ifdef NDEBUG
	av_log_set_level(AV_LOG_ERROR); // ffmpeg log
#endif//NDEBUG

	while (!close_signal.load(std::memory_order_acquire)) {
		switch (page) {
		case Page::connectToServer: page = connectToServer(); break;
		case Page::chooseMode: page = chooseMode(); break;
		case Page::makeRoom: page = makeRoom(); break;
		case Page::enterRoom: page = enterRoom(); break;
		case Page::Show: page = Show(); break;
		//case Page::serverStatusError: page = serverStatusError(); break;
		case Page::Tmp: page = Tmp(); break;
		}
	}

}

App::App() {
#if USE_IMGUI
	main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	//SetImGuiContext(ImGui::GetCurrentContext());
	io = &ImGui::GetIO();
	io->ConfigFlags =
		//ImGuiConfigFlags_ViewportsEnable |
		ImGuiConfigFlags_DockingEnable |
		ImGuiConfigFlags_NavEnableKeyboard |     // Enable Keyboard Controls
		ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls


	ImGui::StyleColorsDark();

	// Setup scaling
	style = &ImGui::GetStyle();
	style->ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style->FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

	ImFontConfig fontConfig{};

	fontConfig.OversampleH = 2;
	fontConfig.OversampleV = 2;

	//io.Fonts->Clear();
	io->Fonts->AddFontFromFileTTF("font/MapleMonoNL-CN-Regular.ttf", 15.0f, &fontConfig, io->Fonts->GetGlyphRangesChineseFull());
	//io->Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc", 15.0f, &fontConfig, io->Fonts->GetGlyphRangesChineseFull());
	//io.Fonts->Build();
	//io.Fonts->AddFontFromFileTTF("font/msyh.ttc", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window->m_get, true);
	ImGui_ImplOpenGL3_Init("#version 330");
#endif

	assert(plug.asr.loader.load("plugin/plugin_ASR.dll"));
	plug.asr.loader.get(plug.asr.init, plugin::name::asrInit);
	plug.asr.loader.get(plug.asr.transcribe, plugin::name::asrTranscribe);
	plug.asr.loader.get(plug.asr.free, plugin::name::asrFree);

	assert(plug.asr.init("plugin/ggml-tiny.bin") == 0);
}

App::~App() {
	plug.asr.free();

#if USE_IMGUI
	// Cleanup
	ImGui::DestroyPlatformWindows();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
#endif

#ifndef NDEBUG
	println("end ok(press enter to finish)");
	//(void)getchar();
#endif
}

int main() {
	std::unique_ptr<App> app = std::make_unique<App>();
	app->run();
}