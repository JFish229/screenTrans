// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#define _CRT_SECURE_NO_WARNINGS
#include <filesystem>
#include <deque>
#include <cstdlib>
#include <App.hpp>
#include <AsrAudio.hpp>

#include <util/Timer.hpp>

#ifndef NDEBUG
#	include <filesystem>
#endif//NDEBUG

#ifdef NDEBUG
	extern "C" {
#	include <libavutil/log.h>
	}
#endif//NDEBUG

#include <inttypes.h>
#include <cstdlib>

#include <atomic_imgui.hpp>

#ifdef _WIN32
#	include <timeapi.h>
#	pragma comment(lib, "winmm.lib")
#endif


#if defined(_WIN32)
#   define FONT_CJK_PATH "C:/Windows/Fonts/msyh.ttc"          // 微软雅黑
#elif defined(__APPLE__)
#   define FONT_CJK_PATH "/System/Library/Fonts/PingFang.ttc" // 苹方
#elif defined(__linux__)
#   define FONT_CJK_PATH "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
#else
	static_assert(false, "unsupported platform font");
#endif

template <typename CB>
[[nodiscard]] std::size_t cbPop(CB& cb, typename CB::pointer out, std::size_t count) {
    using T = typename CB::value_type;
    if (count == 0 || cb.empty()) return 0;
    const std::size_t got = std::min<std::size_t>(count, cb.size());

    if constexpr (std::is_trivially_copyable_v<T>) {
        // 按字节复制，最快
        auto a1 = cb.array_one();
        auto a2 = cb.array_two();
        const std::size_t n1 = std::min(got, a1.second);
        std::memcpy(out, a1.first, n1 * sizeof(T));
        if (n1 < got) {
            std::memcpy(out + n1, a2.first, (got - n1) * sizeof(T));
        }
    }
    else if constexpr (std::is_copy_assignable_v<T>) {
        // 非平凡但可赋值：std::copy_n 会调 operator=
        std::copy_n(cb.begin(), got, out);
    }
    else {
		assert(false);
	}

    cb.erase_begin(got);
    return got;
}

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

	void PushU64(uint64_t id) {
		ImGui::PushID(static_cast<int>( id & 0xFFFFFFFFu));
		ImGui::PushID(static_cast<int>((id >> 32) & 0xFFFFFFFFu));
	}

	void PopU64() {
		ImGui::PopID();
		ImGui::PopID();
	}

	struct PushU64Guard {
		PushU64Guard(uint64_t id) { PushU64(id); }
		~PushU64Guard() { PopU64(); }
	};

	struct PushIdGuard {
		PushIdGuard(int id) { ImGui::PushID(id); }
		~PushIdGuard() { ImGui::PopID(); }
	};
}
#endif

std::unique_ptr<gl::Window> App::makeWindow() {
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	auto res = std::make_unique<gl::Window>(1200, 600, glm::ivec2{ 200, 100 }, "FlexMeet-client");
	glfwMakeContextCurrent(res->m_get);
	GLASSERTK(gladLoadGLLoader((GLADloadproc)glfwGetProcAddress));
	glEnable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	return res;
}

void App::storeUiState() {
	std::FILE* const file = std::fopen("FlexMeet.ini", "wb");
	DEFER([file]() { std::fclose(file); });
	assert(file);
	Ui buf_ui;

	constexpr size_t BEGIN = __LINE__ + 1; // === BEGIN - 1 ===
	memcpy(	buf_ui.p1.ipv4,							p1.ipv4, sizeof(p1.ipv4));//
			buf_ui.p1.port							= net::host_to_network(p1.port);
	memcpy(	buf_ui.p1.name,							p1.name, sizeof(p1.ipv4));
			buf_ui.p2.passwd						= net::host_to_network(p2.passwd);//
			buf_ui.p3.room_id						= net::host_to_network(p3.room_id);//
			buf_ui.p3.passwd						= net::host_to_network(p3.passwd);//
			buf_ui.send.fps							= net::host_to_network(get(ui.send.fps));//
			buf_ui.send.share_audio					= net::host_to_network(get(ui.send.share_audio));
			buf_ui.send.share_video					= net::host_to_network(get(ui.send.share_video));
			buf_ui.video.fps						= net::host_to_network(get(ui.video.fps));//
			buf_ui.video.encode.fps					= net::host_to_network(get(ui.video.encode.fps));
			buf_ui.video.encode.bitrate				= net::host_to_network(get(ui.video.encode.bitrate));
			buf_ui.video.encode.width				= net::host_to_network(get(ui.video.encode.width));
			buf_ui.video.encode.height				= net::host_to_network(get(ui.video.encode.height));
			buf_ui.room_user.host.set_room_passwd	= net::host_to_network(get(ui.room_user.host.set_room_passwd));
			buf_ui.room_user.host.mute_all_keep		= net::host_to_network(get(ui.room_user.host.mute_all_keep));
	constexpr size_t END = __LINE__; // === END ===
	static_assert(END - BEGIN == count_basic_members<Ui>());

	std::fwrite(&buf_ui, sizeof(buf_ui), 1, file);
}

void App::loadUiState() {
	std::FILE* const file = std::fopen("FlexMeet.ini", "rb");
	DEFER([file]() { if (file) { std::fclose(file); }});
	if (!file) { return; }
	std::fseek(file, 0, SEEK_END);
	const auto file_size = std::ftell(file);
	if (file_size != sizeof(Ui)) { return; }
	std::rewind(file);
	Ui buf_ui;
	fread(&buf_ui, sizeof(Ui), 1, file);
	constexpr size_t BEGIN = __LINE__ + 1; // === BEGIN -1 ===
	memcpy( p1.ipv4,							buf_ui.p1.ipv4, sizeof(p1.ipv4));//
			p1.port	=							net::network_to_host(buf_ui.p1.port);
	memcpy( p1.name,							buf_ui.p1.name, sizeof(p1.ipv4));
			p2.passwd =							net::network_to_host(buf_ui.p2.passwd);//
			p3.room_id =						net::network_to_host(buf_ui.p3.room_id);//
			p3.passwd =							net::network_to_host(buf_ui.p3.passwd);//
	set(	ui.send.fps,						net::network_to_host(buf_ui.send.fps));//
	set(	ui.send.share_audio,				net::network_to_host(buf_ui.send.share_audio));
	set(	ui.send.share_video,				net::network_to_host(buf_ui.send.share_video));
	set(	ui.video.fps,						net::network_to_host(buf_ui.video.fps));//
	set(	ui.video.encode.fps,				net::network_to_host(buf_ui.video.encode.fps));
	set(	ui.video.encode.bitrate,			net::network_to_host(buf_ui.video.encode.bitrate));
	set(	ui.video.encode.width,				net::network_to_host(buf_ui.video.encode.width));
	set(	ui.video.encode.height,				net::network_to_host(buf_ui.video.encode.height));
	set(	ui.room_user.host.set_room_passwd,	net::network_to_host(buf_ui.room_user.host.set_room_passwd));
	set(	ui.room_user.host.mute_all_keep,	net::network_to_host(buf_ui.room_user.host.mute_all_keep));
	constexpr size_t END = __LINE__; // === END ===
	static_assert(END - BEGIN == count_basic_members<Ui>());
}

template<bool lock_users>
bool App::isHost() {
	if constexpr (lock_users) {
		std::lock_guard lock(mtx_users);
		return get(users[get(logger.id)].is_host);
	}
	else {
		return get(users[get(logger.id)].is_host);
	}
}

bool App::clientClosed() {
	std::lock_guard lock(mtx_close);
	return client.Closed();
}

void App::Receive() {
	DEFER{ [this]() {
		p_show.wait_threads.release();
	}};

	struct {
		Signal signals{};
		user::id_t id{ user::invalid_id };
		std::string other_name{};
		bool is_host_from_server{};
		uint32_t room_passwd{};
		bool is_host_from_client{};
		user::id_t wanna_kick_off{};
		//bool mute_all_once{};
		//bool mute_all_keep{};
		//bool share_audio{};
		//std::vector<float> audioFrames{};
		bool has_words_msg{};
		user::id_t words_msg_target{};
		char words_msg[words_msg_bytes];
		bool share_video{};
		int32_t pk_size{};
		std::vector<uint8_t> pk{};
		void restore() {
		}
	} rv;

	while (!p_show.ended() && !clientClosed()) {
		rv.restore();
		bool recv_ok{true};
		if (!client.ReceiveBy(rv.signals) ||  
			!client.ReceiveBy(rv.id)) [[unlikely]] 
		{
			break;
		}
		if (rv.signals & signal_closed) [[unlikely]] {
			{
				std::lock_guard lock(mtx_users);
				auto count = std::erase_if(users, [&](auto& user) { return user.first == rv.id;});
			}
			set(ui.room_user.chosen_user, get(logger.id));
			continue;
		}

		client.ReceiveBy(rv.other_name);
		if (!client.ReceiveBy(rv.is_host_from_server)) [[unlikely]] { break; }
		if (rv.is_host_from_server && get(logger.id) == rv.id) {
			if (!client.ReceiveBy(rv.room_passwd)) [[unlikely]] { break; }
			set(ui.room_user.host.room_passwd, rv.room_passwd);
		}
		if (!client.ReceiveBy(rv.is_host_from_client)) [[unlikely]] { break; }
		if (rv.is_host_from_client) {
			client.ReceiveBy(rv.wanna_kick_off);
			//client.ReceiveBy(rv.mute_all_once);
			//client.ReceiveBy(rv.mute_all_keep);
		}
		//if (!client.ReceiveBy(rv.share_audio)) [[unlikely]] { break; }
		//if (rv.share_audio) {
			//client.ReceiveBy(rv.audioFrames);
		//}
		//else {
			//rv.audioFrames.resize(0);
		//}

		if (!client.ReceiveBy(rv.has_words_msg)) [[unlikely]] { break; }
		if (rv.has_words_msg) [[unlikely]] {
			client.ReceiveBy(rv.words_msg_target);
			if (rv.words_msg_target == user::public_target) {
				client.ReceiveBy(rv.words_msg, sizeof(rv.words_msg));
				{
					std::lock_guard lock0(mtx_users);
					std::lock_guard lock1(ui.send.total_words_msg.mtx);
					ui.send.total_words_msg.buf.push_back(users[rv.id].name + ": " + rv.words_msg);
				}
			}
			else if (rv.words_msg_target == logger.id) {
				client.ReceiveBy(rv.words_msg, sizeof(rv.words_msg));
				{
					std::lock_guard lock(mtx_users);
					users[rv.id].total_words_msg.buf.push_back(users[rv.id].name + ": " + rv.words_msg);
				}
			}
		}

		if (!client.ReceiveBy(rv.share_video)) [[unlikely]] { break; }

		if (rv.is_host_from_client) {
			if (rv.wanna_kick_off == get(logger.id)) [[unlikely]] {
				p_show.setEnd(true, ShowPara::End::kickOff);
			}
			//if (rv.mute_all_once && rv.id != logger.id) [[unlikely]] {
			//	set(ui.send.share_audio, false);
			//}
			//if (rv.mute_all_keep && rv.id != logger.id) {
			//	set(ui.send.share_audio, false);
			//	set(ui.room_user.is_muted_keep, true);
			//}
			//else {
			//	set(ui.room_user.is_muted_keep, false);
			//}
		}
		{
			std::lock_guard lock(mtx_users);
			auto [itr, first] = users.try_emplace(rv.id);
			itr->second.name = rv.other_name;
			auto& user = itr->second;
			//user.audioBuf.push(rv.audioFrames.begin(), rv.audioFrames.end());
			//set(user.share_audio, rv.share_audio);
			set(user.is_host, rv.is_host_from_server);
			set(user.share_video, rv.share_video);
		}

		if (rv.signals & signal_choiceNotMatch) { continue; }

		// each packet
		if (rv.share_video) {
			if (!client.ReceiveBy(rv.pk_size)) [[unlikely]] { break; }
			for (int i = 0;i < rv.pk_size;++i) {
				if (!client.ReceiveBy(rv.pk)) [[unlikely]] { break; }
				recv_screen_packets.push(std::move(rv.pk));
			}
		}
	}
}

void App::Send() {
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};

	auto last_time = std::chrono::steady_clock::now();
	while(!p_show.ended() && !clientClosed()){
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / get(ui.send.fps))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

		bool share_video = get(ui.send.share_video);

		std::vector<std::vector<uint8_t>> packets(send_screen_packets_count);
		int32_t pk_size = static_cast<int32_t>(send_screen_packets.pop(packets.data(), send_screen_packets_count));

		bool is_host_from_client = isHost<true>();
		assert(get(logger.id) != user::invalid_id);
		client.Send(get(logger.id));
		{
			std::lock_guard lock(logger.mtx_name);
			client.Send(logger.name);
		}
		client.Send(is_host_from_client);
		if (is_host_from_client) {
			client.Send(get(ui.room_user.host.set_room_passwd));
			set(ui.room_user.host.set_room_passwd, Room::invalid_passwd);
			client.Send(get(ui.room_user.host.wanna_kick_off));
			set(ui.room_user.host.wanna_kick_off, user::invalid_id);
			client.Send(get(ui.room_user.host.transfer_host));
			set(ui.room_user.host.transfer_host, user::invalid_id);
			//client.Send(get(ui.room_user.host.mute_all_once));
			//set(ui.room_user.host.mute_all_once, false);
			//client.Send(get(ui.room_user.host.mute_all_keep));
		}
		bool has_words_msg;
		{
			std::lock_guard lock(ui.send.words_msg.mtx);
			has_words_msg = ui.send.words_msg.buf[0] != '\0';
		}
		client.Send(has_words_msg);
		if (has_words_msg) [[unlikely]] {
			user::id_t target = get(ui.send.words_msg_target);
			client.Send(target);
			char buf[sizeof(ui.send.words_msg.buf)];
			{
				std::lock_guard lock(ui.send.words_msg.mtx);
				memcpy(buf, ui.send.words_msg.buf, sizeof(buf));
				ui.send.words_msg.buf[0] = '\0';
			}
			client.Send(buf, sizeof(buf));
			if (target != user::public_target) {
				std::lock_guard lock(mtx_users);
				auto& user = users[target];
				user.total_words_msg.buf.push_back(logger.name + ": " + buf);
			}
		}
		client.Send(get(ui.room_user.chosen_user));
		client.Send(share_video);
		if (share_video) {
			client.Send(pk_size);
			for (size_t i = 0;i < pk_size;++i) {
				client.Send(packets[i]);
			}
		}
	}
}

void App::audioSend(){
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};
	audio::Capture ad_cpt{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	captureActive.store(ad_cpt.Start());
	if (!captureActive.load()) std::cerr << "ASR: microphone start failed; check Windows microphone access/default device\n";
    DEFER{ [this]() { captureActive.store(false); } };
	std::vector<float> buf_audio{};
    asr_audio::CaptureGate captureGate;

	auto last_time = std::chrono::steady_clock::now();
	while (!p_show.ended() && !clientClosed()) {
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / (audio::sampleRate / audio::periodSizeInFrames))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;

        // Always drain, even while muted, before any potentially blocking send.
        ad_cpt.giveFrames(buf_audio);
        captureSamples.fetch_add(buf_audio.size());
        capturePeak.store(std::max(asr_audio::peak(buf_audio), capturePeak.load() * .90f));
        if (const auto dropped = ad_cpt.TakeDroppedSamples()) {
            buf_audio.clear(); asrDiscontinuity.store(true);
            std::cerr << "Capture overrun: " << dropped << " samples dropped\n";
        }
        const bool sharing = get(ui.send.share_audio) && !get(ui.room_user.is_muted_keep);
        const bool changed = captureGate.filter(buf_audio, sharing);
        if (plug.asr.stream) {
            std::lock_guard lock(mtx_asrLocal);
            if (!sharing || changed) asrLocalAudio.clear();
            if (const auto dropped = asr_audio::append_audio(asrLocalAudio, buf_audio)) {
                asrDiscontinuity.store(true);
                std::cerr << "ASR local buffer overflow: " << dropped << " samples lost\n";
            }
        }
        client_audio.Send(get(logger.id));
		faudio_t faudio{ 0 };
		if (get(ui.send.share_audio)) { faudio |= faudio_share; }
		if (isHost<true>()) { faudio |= faudio_is_host; }
		if (get(ui.room_user.host.mute_all_once)) [[unlikely]] { faudio |= faudio_mute_all_once; }
		if (get(ui.room_user.host.mute_all_keep)) { faudio |= faudio_mute_all_keep; }
		set(ui.room_user.host.mute_all_once, false);

		client_audio.Send(faudio);
		if (faudio & faudio_share) {
			client_audio.Send(buf_audio);
		}
	}
}

void App::audioRecv(){
	DEFER{ [this]() {
		audioPlayer.stop();
		p_show.wait_threads.release();
	}};
	audioPlayer.start();


	struct {
		user::id_t id{};
		faudio_t faudio{};
		std::vector<float> audioFrames{};
		void restore() {
		
		}
	} rv{};

	while (!p_show.ended() && !clientClosed()) {
		client_audio.ReceiveBy(rv.id);
		client_audio.ReceiveBy(rv.faudio);
		if (rv.faudio & faudio_share) {
			client_audio.ReceiveBy(rv.audioFrames);
		}
		else {
			rv.audioFrames.resize(0);
		}

		if (rv.faudio & faudio_is_host) {
			if ((rv.faudio & faudio_mute_all_once) && rv.id != logger.id) [[unlikely]] {
				set(ui.send.share_audio, false);
			}
			if ((rv.faudio & faudio_mute_all_keep) && rv.id != logger.id) {
				set(ui.send.share_audio, false);
				set(ui.room_user.is_muted_keep, true);
			}
			else {
				set(ui.room_user.is_muted_keep, false);
			}
		}
		{
			std::lock_guard lock(mtx_users);
			auto [itr, first] = users.try_emplace(rv.id);
			auto& user = itr->second;
            if (const auto dropped = asr_audio::append_audio(user.audioBuf, rv.audioFrames)) {
                if (rv.id != get(logger.id)) {
                    asrDiscontinuity.store(true);
                    std::cerr << "ASR remote buffer overflow: " << dropped << " samples lost\n";
                }
            }
			//user.audioBuf.push(rv.audioFrames.begin(), rv.audioFrames.end());
			set(user.share_audio, rv.faudio & faudio_share);
		}
	}
}

void App::ScreenCaptureEncode() {
	DEFER{
		[this]() {p_show.wait_threads.release();}
	};
	ScreenCapture capture;
	if (!capture.Initialize()) {
		std::cerr << "capture.Initialize() error\n";
		exit(1);
	}

	while (!capture.CaptureFrame());
	const int w = capture.Width() & ~1;
	const int h = capture.Height() & ~1;
	set(ui.video.encode.width, w);
	set(ui.video.encode.height, h);
	ui.video.encode.wait_wh.release();
	ST::H264Encoder encoder(w, h, ui.video.encode.fps, ui.video.encode.bitrate);

	auto last_time = std::chrono::steady_clock::now();
	while (!p_show.ended() && !clientClosed()) {
		if (!get(ui.send.share_video)) {
			// Audio-only meetings must not continuously capture and encode the desktop.
			std::this_thread::sleep_for(20ms);
			last_time = std::chrono::steady_clock::now();
			continue;
		}
		if (!capture.CaptureFrame()) {
			continue;
		}
		auto& rgba = capture.GetBuffer();
		if (rgba.empty()) {
			continue;
		}

		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / get(ui.send.fps))
		);
		auto next_frame = last_time + target_interval;
		std::this_thread::sleep_until(next_frame);
		last_time = next_frame;


		bool share_audio = get(ui.send.share_audio);
		bool share_video = get(ui.send.share_video);

		encoder.Reset(w, h, ui.video.encode.width, ui.video.encode.height, ui.video.encode.fps, ui.video.encode.bitrate);
		set(ui.video.encode.width, encoder.Width());
		set(ui.video.encode.height, encoder.Height());

		auto pkts = encoder.EncodeFrame(rgba);
		for(auto& pkt: pkts){
			send_screen_packets.push(std::move(pkt));
		}
	}
}

void App::ScreenDecode() {
	DEFER{ [this]() {
		p_show.wait_threads.release();
	} };
	H264Decoder decoder;
	std::vector<std::vector<uint8_t>> pkts(recv_screen_packets_count);
	while (!p_show.ended() && !clientClosed()) {
		const size_t pkts_count = recv_screen_packets.pop(pkts.data(), recv_screen_packets_count);
		if (pkts_count == 0) { std::this_thread::sleep_for(2ms); continue; }
		for (size_t i = 0;i < pkts_count;++i) {
			auto frames = decoder.DecodePacket(pkts[i]);
			for (auto& frame : frames) {
				total_video_frames_v1.push(std::move(frame));
			}
		}
	}
}

void App::CameraCapture() {

}

void App::audioMix() {
    DEFER{ [this]() { p_show.wait_threads.release(); } };
    constexpr size_t samples = audio::sampleRate / 50; // fixed 20 ms timeline
    std::vector<float> playback(samples), recognition(samples), scratch(samples);
    auto next = std::chrono::steady_clock::now();
    while (!p_show.ended() && !clientClosed()) {
        next += 20ms;
        std::this_thread::sleep_until(next);
        if (std::chrono::steady_clock::now() - next > 200ms) {
            next = std::chrono::steady_clock::now();
            // Scheduling delay alone does not imply PCM loss. The bounded
            // producer queues report actual overruns, with a reason, instead.
        }
        std::fill(playback.begin(), playback.end(), 0.0f);
        {
            std::lock_guard lock(mtx_users);
            for (auto& [id, user] : users) {
                const auto n = asr_audio::pop_audio(user.audioBuf, std::span<float>(scratch));
                bool playing = false;
                for (size_t i = 0; i < n; ++i) {
                    playing |= std::abs(scratch[i]) > User::audio_playing_least;
                    // Server echoes self. Local capture already feeds ASR directly.
                }
                asr_audio::add_remote(playback, std::span<const float>(scratch.data(), n), id == get(logger.id));
                set(user.audio_playing, playing);
            }
        }
        recognition = playback;
        if (plug.asr.stream) {
            std::lock_guard lock(mtx_asrLocal);
            const auto n = asr_audio::pop_audio(asrLocalAudio, std::span<float>(scratch));
            asr_audio::add(recognition, std::span<const float>(scratch.data(), n));
        }
        asr_audio::normalize(playback); asr_audio::normalize(recognition);
        audioUser.pushFrames(playback.data(), playback.size());
        // Push silence too: VAD must see the pause even when packets stop arriving.
        if (plug.asr.stream) {
            asrInputSamples.fetch_add(recognition.size());
            asrInputPeak.store(std::max(asr_audio::peak(recognition), asrInputPeak.load() * .90f));
            const int status = plug.asr.push(plug.asr.stream, recognition.data(), static_cast<uint32_t>(recognition.size()));
            if (status < 0 && status != PLUGIN_ASR_CLOSED) {
                std::cerr << "ASR push failed: " << status << '\n';
                asrDiscontinuity.store(true);
            }
        }
    }
}

void App::asrRecv() {
    DEFER{ [this]() { p_show.wait_threads.release(); } };
    if (!plug.asr.stream) return;
    std::deque<std::string> confirmed;
    std::string draft;
    uint64_t draftId = 0;
    std::vector<char> text(4096);
    while (!p_show.ended() && !clientClosed()) {
        if (asrDiscontinuity.exchange(false)) {
            plug.asr.reset(plug.asr.stream);
            draft.clear();
            asrInterruptions.fetch_add(1);
            { std::lock_guard lock(mtx_speechWords); asrStatus = "Audio samples lost; waiting for new speech."; }
            std::cerr << "ASR: input discontinuity; stream reset\n";
        }
        for (;;) {
            plugin_asr_result result{}; result.struct_size = sizeof(result);
            int status = plug.asr.poll(plug.asr.stream, &result, text.data(), static_cast<uint32_t>(text.size()));
            if (status == PLUGIN_ASR_BUFFER_SMALL) { text.resize(result.text_bytes); continue; }
            if (status != PLUGIN_ASR_EVENT) break;
            std::string value(text.data());
            if (result.type == PLUGIN_ASR_PARTIAL) {
                draftId = result.utterance_id; draft = std::move(value);
            } else if (result.type == PLUGIN_ASR_FINAL) {
                if (draftId == result.utterance_id) draft.clear();
                if (!value.empty()) confirmed.push_back(std::move(value));
                while (confirmed.size() > 32) confirmed.pop_front();
            } else if (result.type == PLUGIN_ASR_GAP || result.type == PLUGIN_ASR_ERROR) {
                draft.clear();
                std::cerr << "ASR: " << value << '\n';
                std::lock_guard lock(mtx_speechWords);
                if (result.type == PLUGIN_ASR_GAP) asrInterruptions.fetch_add(1);
                asrStatus = result.type == PLUGIN_ASR_ERROR ? "Subtitles unavailable; rejoin to retry." : "ASR overloaded; waiting for new speech. See startup log.";
            }
            if (result.type == PLUGIN_ASR_PARTIAL || result.type == PLUGIN_ASR_FINAL) {
                std::clog << "ASR event=" << result.type << " id=" << result.utterance_id
                          << " revision=" << result.revision << " queue_ms=" << result.queue_ms
                          << " inference_ms=" << result.inference_ms << '\n';
                std::lock_guard lock(mtx_speechWords); asrStatus.clear();
            }
        }
        {
            std::lock_guard lock(mtx_speechWords);
            speechWords.clear();
            for (const auto& sentence : confirmed) { speechWords += sentence; speechWords += '\n'; }
            speechWords += draft;
        }
        std::this_thread::sleep_for(10ms);
    }
    // Meeting exit cancels, rather than publishing late results into another room.
    plug.asr.reset(plug.asr.stream);
}

void App::ShowPara::setEnd(bool ended, End showEnd) {
	set(m_ended, ended);
	m_showEnd = showEnd;
}

void App::ShowPara::restore() {
	set(m_ended, false);
	m_showEnd = End::Default;
}

void App::restoreAfterShow() {
	p_show.restore();
	users.clear();
	glfwSetWindowUserPointer(window->m_get, this);
	glfwSetWindowPosCallback(window->m_get, [](GLFWwindow* window, int xpos, int ypos) {
		auto user = static_cast<App*>(glfwGetWindowUserPointer(window));
		user->window->updatePos({ xpos, ypos });
		});
	glfwSetFramebufferSizeCallback(window->m_get, [](GLFWwindow* window, int width, int height) {
		auto user = static_cast<App*>(glfwGetWindowUserPointer(window));
		glViewport(0, 0, width, height);
		user->window->setViewport({ 0,0,width, height });
		});
}

App::Page App::Show() {

    if (plug.asr.stream) plug.asr.reset(plug.asr.stream);
    { std::lock_guard lock(mtx_asrLocal); asrLocalAudio.clear(); }
    { std::lock_guard lock(mtx_speechWords); speechWords.clear(); if (plug.asr.stream) asrStatus.clear(); }
    asrDiscontinuity.store(false);
    captureSamples.store(0); asrInputSamples.store(0); asrInterruptions.store(0);
    capturePeak.store(0); asrInputPeak.store(0);
	set(ui.room_user.chosen_user, get(logger.id));
	{
		std::lock_guard lock2{ mtx_users };
		auto [itr, first] = users.try_emplace(get(ui.room_user.chosen_user));
		itr->second.name = logger.name;
	}

	constexpr size_t thread_count = 8;


	std::jthread threads[] = {
		std::jthread{ &App::audioMix, this },
		std::jthread{ &App::Send, this },
		std::jthread{ &App::Receive, this },
		std::jthread{ &App::audioSend, this },
		std::jthread{ &App::audioRecv, this },
		std::jthread{ &App::asrRecv, this },
		std::jthread{ &App::ScreenCaptureEncode, this },
		std::jthread{ &App::ScreenDecode, this },
	};

	static_assert(thread_count == sizeof(threads) / sizeof(std::jthread));

    // Close sockets before jthread destructors try to join blocking receivers.
	DEFER{ [this, thread_count, &threads]() {
		p_show.setEnd(true, p_show.showEnd());
		if (plug.asr.stream) plug.asr.reset(plug.asr.stream);
		client.Close();
		client_audio.Close();
		for (auto& thread : threads) if (thread.joinable()) thread.join();
		for (size_t i = 0;i < thread_count;++i) {
			p_show.wait_threads.acquire();
		}
		restoreAfterShow();
	}};

	ui.video.encode.wait_wh.acquire();

	//assert(thread_count == threads.size());

	glfwSetWindowTitle(window->m_get, ("client: " + logger.name).c_str());
		
	struct imgStruct {
		float pos[3];
		float texCoords[2];
	};

	gl::Texture2D img(1, 1, { 0x33,0x33,0x33,0xFF }, gl::GpuTextureFmt::RGBA8, gl::TextureFmt::BGRA, 4);

#if USE_IMGUI
	const auto DEFAULT_BK_COLOR = style->Colors[ImGuiCol_WindowBg];
#endif

	int sc_width{};
	int sc_height{};
	{
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		assert(monitor);
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		assert(mode);
		sc_width = mode->width;
		sc_height = mode->height;
	}

	boost::float64_t d_time = 0;
	float fps = 0;
	bool show_settings = true;
	struct {
		char words_msg_buf[sizeof(ui.send.words_msg.buf)]{};
		char name_buf[2 * sizeof(p1.name)]{};
		char rename[sizeof(p1.name)]{};
		uint32_t room_passwd{ Room::invalid_passwd };
	} mid;
	ST::DecodedFrame decodedFrame;


	auto last_time = std::chrono::steady_clock::now();
	auto renderDealing = [&]() {
		const auto target_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<boost::float64_t>(1.0 / get(ui.video.fps))
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
		//shader.use();
		//vaoImg.bind();
		//gl::draw(eboImg, gl::DrawMode::TRIANGLES);

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
		if (ImGui::Button("<-")) {
			p_show.setEnd(true, ShowPara::End::Return);
		}
		if (ImGui::TreeNode("message")) {
			ImGui::PushIdGuard pushIdGuard(__LINE__);
			// fps
			COMP_ATOMIC_T(ImGui::SliderScalar, "fps", ImGuiDataType_Double, ui.send.fps, &MIN_MAX_FPS, &MAX_MAX_FPS);
			// share_video
			COMP_ATOMIC(ImGui::Checkbox, "share_video", ui.send.share_video);
			// share_audio
			COMP_ATOMIC(ImGui::Checkbox, "share_audio", ui.send.share_audio);
			// words_msg
			if (get(ui.send.words_msg_target) == user::public_target) {
				constexpr const char public_target[] = "public";
				memcpy(mid.name_buf, public_target, sizeof(public_target));
			}
			else {
				std::lock_guard lock(mtx_users);
				auto& name = users[get(ui.send.words_msg_target)].name;
				memcpy(mid.name_buf, name.c_str(), name.size() + 1);
			}
			ImGui::Text("message_target: ");
			if (ImGui::BeginCombo("##message_targets", mid.name_buf)) {
				while (true) {
					if (ImGui::Selectable("public")) {
						set(ui.send.words_msg_target, user::public_target);
						break;
					}
					{
						std::lock_guard lock(mtx_users);
						for (const auto& user : users) {
							if (user.first == logger.id) { continue; }
							ImGui::PushU64Guard pushU64Guard(user.first);
							if (ImGui::Selectable(user.second.name.c_str())) {
								set(ui.send.words_msg_target, user.first);
								break;
							}
						}
					}
					break;
				}
				ImGui::EndCombo();
			}// ##message_targets
			// total_words_msg
			ImGui::BeginChild("##speech", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::PushTextWrapPos(0.0f);   // 0 表示按窗口右边界换行
			if(ui.send.words_msg_target == user::public_target) {
				std::lock_guard lock{ ui.send.total_words_msg.mtx };
				for (const auto& msg : ui.send.total_words_msg.buf) {
					ImGui::TextUnformatted(msg.c_str());
				}
			}
			else {
				std::lock_guard lock(mtx_users);
				auto& msgs = users[get(ui.send.words_msg_target)].total_words_msg;
				for (const auto& msg : msgs.buf) {
					ImGui::TextUnformatted(msg.c_str());
				}
			}
			ImGui::PopTextWrapPos();
			ImGui::EndChild();
			// words_msg
			ImGui::InputText("##message", mid.words_msg_buf, sizeof(mid.words_msg_buf));
			ImGui::SameLine();
			if (ImGui::Button("send")) {
				std::lock_guard lock(ui.send.words_msg.mtx);
				memcpy(ui.send.words_msg.buf, mid.words_msg_buf, sizeof(mid.words_msg_buf));
				mid.words_msg_buf[0] = '\0';
			}

			ImGui::TreePop();
		}// message
		if (ImGui::TreeNode("vedio")) {
			ImGui::PushIdGuard pushIdGuard(__LINE__);
			// fps
			if (d_time > 0.4) {
				fps = ImGui::GetIO().Framerate;
				d_time = 0;
			}
			ImGui::Text("FPS: %.3f", fps);
			COMP_ATOMIC_T(ImGui::SliderScalar, "fps", ImGuiDataType_Double, ui.video.fps, &MIN_MAX_FPS, &MAX_MAX_FPS);
			if (ImGui::TreeNode("encode")) {
				// fps
				COMP_ATOMIC(ImGui::SliderInt, "fps", ui.video.encode.fps, static_cast<int>(MIN_MAX_FPS), static_cast<int>(MAX_MAX_FPS));
				// bitrate
				COMP_ATOMIC(ImGui::SliderInt, "bitrate", ui.video.encode.bitrate, video::min_bitrate, video::max_bitrate);
				// width
				COMP_ATOMIC_EX(ImGui::SliderInt, "width", ui.video.encode.width, [](int v) {return v & ~1;}, static_cast<int>(sc_width * 0.1), sc_width);
				// height
				COMP_ATOMIC_EX(ImGui::SliderInt, "height", ui.video.encode.height, [](int v) {return v & ~1;}, static_cast<int>(sc_height * 0.1), sc_height);
				ImGui::TreePop();
			}// encode
			ImGui::TreePop();
		}// vedio
		if (ImGui::TreeNode("room_user")) {
#			ifndef NDEBUG
				ImGui::Text("id: %zu", (size_t)(get(logger.id)));
				ImGui::Text("socket: %zu", (size_t)(client.Id()));
#			endif
			// name
			ImGui::Text("name: %s", logger.name.c_str());
			// room id
			uint32_t const_room_id = logger.room_id;
			ImGui::InputScalar("room_id", ImGuiDataType_U32, &const_room_id);
			// rename
			ImGui::InputText("##rename", mid.rename, sizeof(mid.rename), ImGuiInputTextFlags_CharsNoBlank);
			ImGui::SameLine();
			if (ImGui::Button("rename")) {
				{
					std::lock_guard lock(logger.mtx_name);
					logger.name = mid.rename;
				}
				glfwSetWindowTitle(window->m_get, ("client: " + logger.name).c_str());
			}
			// share_video
			COMP_ATOMIC(ImGui::Checkbox, "share_video", ui.send.share_video);
			// share_audio
			COMP_ATOMIC(ImGui::Checkbox, "share_audio", ui.send.share_audio);
			// is_muted_keep
			bool is_muted_keep = ui.room_user.is_muted_keep;
			ImGui::SameLine();
			ImGui::Checkbox("is_muted", &is_muted_keep);
			{
				ImGui::PushIdGuard pushIdGuard(__LINE__);
				std::lock_guard lock(mtx_users);
				auto chosen = users.find(get(ui.room_user.chosen_user));
				assert(chosen != users.end());
				//auto name_chosen = users[chosen->first].name + (get(users[chosen->first].is_host) ? "(host)" : "");
				auto& chosen_user = users[chosen->first];
				constexpr char host_signal[] = "(host)";
				memcpy(mid.name_buf, chosen_user.name.c_str(), chosen_user.name.size() + 1);
				if (get(chosen_user.is_host)) {
					memcpy(mid.name_buf + chosen_user.name.size(), &host_signal, sizeof(host_signal));
				}
				ImGui::Text("target:");
				if (ImGui::TreeNode("target", mid.name_buf)) {
					const bool is_host = isHost<false>();
					if (ImGui::BeginTable("##target_users", is_host ? 6 : 4, ImGuiTableFlags_SizingFixedFit)) {
						ImGui::TableSetupColumn("video");
						ImGui::TableSetupColumn("audio");
						if (is_host) {
							ImGui::TableSetupColumn("kick_off");
							ImGui::TableSetupColumn("transfer_host");
						}
						ImGui::TableSetupColumn("target");
						ImGui::TableSetupColumn("##audio_playing");
						ImGui::TableHeadersRow();
						for (auto& user : users) {
							ImGui::PushU64Guard pushU64Guard(user.first);

							const size_t name_len = user.second.name.size();
							memcpy(mid.name_buf, user.second.name.c_str(), name_len + 1);
							size_t name_buf_size = user.second.name.size();
							if (get(user.second.is_host)) {
								memcpy(mid.name_buf + name_len, &host_signal, sizeof(host_signal));
								name_buf_size += sizeof(host_signal);
							}
							// share_video
							bool v = get(user.second.share_video);
							ImGui::TableNextColumn();
							ImGui::Checkbox("##video", &v);
							// share_audio
							v = get(user.second.share_audio);
							ImGui::TableNextColumn();
							ImGui::Checkbox("##audio", &v);
							// kick off
							if (is_host) {
								ImGui::TableNextColumn();
								if (!user.second.is_host) {
									if (ImGui::Button("kick_off")) {
										set(ui.room_user.host.wanna_kick_off, user.first);
									}
								}
								else {
									ImGui::Button("(host)");
								}
								ImGui::TableNextColumn();
								if (ImGui::Button("transfer")) {
									set(ui.room_user.host.transfer_host, user.first);
								}
							}
							// select user
							ImGui::TableNextColumn();
							if (ImGui::Selectable(mid.name_buf)) {
								set(ui.room_user.chosen_user, user.first);
							}
							// audio_playing
							constexpr const char audio_playing[] = ">))";
							constexpr const char no_audio_playing[sizeof(audio_playing)] = "___";
							ImGui::TableNextColumn();
							if (get(user.second.audio_playing)) {
								ImGui::Text(audio_playing);
							}
							else {
								ImGui::Text(no_audio_playing);
							}
						}
						ImGui::EndTable();
					}
					ImGui::TreePop();
				}
			}

			ImGui::TreePop();
		}// room_user

		if (isHost<true>() && ImGui::TreeNode("host")) {
			uint32_t const_room_passwd = get(ui.room_user.host.room_passwd);
			// mute_all_once 
			bool mute_all_once = get(ui.room_user.host.mute_all_once);
			mute_all_once |= ImGui::Button("mute_all_once");
			set(ui.room_user.host.mute_all_once, mute_all_once);
			// mute_all_keep
			COMP_ATOMIC(ImGui::Checkbox, "mute_all_keep", ui.room_user.host.mute_all_keep);
			// room_id
			uint32_t const_room_id = logger.room_id;
			ImGui::InputScalar("room_id", ImGuiDataType_U32, &const_room_id);
			// room_passwd
			ImGui::InputScalar("room_passwd", ImGuiDataType_U32, &const_room_passwd);
			ImGui::InputScalar("##set_room_passwd", ImGuiDataType_U32, &mid.room_passwd);
			ImGui::SameLine();
			if (ImGui::Button("reset_passwd")) {
				set(ui.room_user.host.set_room_passwd, mid.room_passwd);
				set(ui.room_user.host.room_passwd, mid.room_passwd);
			}
			if (ImGui::TreeNode("kick_off", "kick_off_target")) {
				{
					std::lock_guard lock(mtx_users);
					for (const auto& user : users) {
						ImGui::PushU64Guard pushU64Guard(user.first);
						if (get(user.second.is_host)) {
							continue;
						}
						if (ImGui::Selectable(user.second.name.c_str())) {
							set(ui.room_user.host.wanna_kick_off, user.first);
							println("kick off user_id: " << user.first);
						}
					}
				}
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}// host

		if (ImGui::TreeNode("plugin")) {
            ImGui::TextUnformatted(get(ui.send.share_audio) && !get(ui.room_user.is_muted_keep)
                ? "Local microphone sharing: ON" : "Local microphone sharing: OFF (enable share_audio to transcribe yourself)");
            ImGui::Text("Capture: %s | samples: %llu", captureActive.load() ? "running" : "NOT RUNNING",
                static_cast<unsigned long long>(captureSamples.load()));
            ImGui::ProgressBar(capturePeak.load(), ImVec2(-FLT_MIN, 0), "Microphone level");
            ImGui::ProgressBar(asrInputPeak.load(), ImVec2(-FLT_MIN, 0), "ASR input level (local + remote)");
            ImGui::Text("ASR input: %.1fs | interruptions: %llu", asrInputSamples.load() / 16000.0,
                static_cast<unsigned long long>(asrInterruptions.load()));
			ImGui::BeginChild("##speech", ImVec2(-FLT_MIN, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::PushTextWrapPos(0.0f);   // 0 表示按窗口右边界换行
			{
				std::lock_guard lock(mtx_speechWords);
				ImGui::TextUnformatted(speechWords.c_str());
                if (!asrStatus.empty()) ImGui::TextWrapped("%s", asrStatus.c_str());
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
		{
			const ImVec2 winSize = ImGui::GetWindowSize();
			const ImVec2 content_origin = ImGui::GetCursorScreenPos();
			const auto imgSize = img.imgSize();
			ImVec2 show_img_size;

			float offsetX{};
			float offsetY{};
			const float img_w_y = imgSize.x / imgSize.y;
			const float win_w_y = winSize.x / winSize.y;
			if (img_w_y > win_w_y) {
				offsetX = 0.0f;
				show_img_size.x = winSize.x;
				show_img_size.y = show_img_size.x / img_w_y;
				offsetY = (winSize.y - show_img_size.y) / 2;
			}
			else {
				offsetY = 0.0f;
				show_img_size.y = winSize.y;
				show_img_size.x = show_img_size.y * img_w_y;
				offsetX = (winSize.x - show_img_size.x) / 2;
			}
			//ImVec2 img_pos = ImVec2(content_origin.x + offsetX, content_origin.y + offsetY);

			ImGui::SetCursorPos(ImVec2(offsetX, offsetY));
			ImGui::Image(img.id(), show_img_size);
		}
		ImGui::End();// main
		style->Colors[ImGuiCol_WindowBg] = DEFAULT_BK_COLOR;
		// ============ main window ============ 

		ImGui::End();//docking end

		// Rendering
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#if USE_IMGUI_VIEWPORT
		{
			GLFWwindow* backup = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup);   // 恢复主上下文
		}
#endif// USE_IMGUI_VIEWPORT

		d_time += 1 / (boost::float64_t)ImGui::GetIO().Framerate;
#endif

		std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_TIME));


		if (!total_video_frames_v1.empty()) {
			total_video_frames_v1.pop(decodedFrame);
		}
		//if (total_video_frames.empty()) { return; }
		//frame = std::move(total_video_frames.front());
		//total_video_frames.pop();

		int width = decodedFrame.width;
		int height = decodedFrame.height;
		auto& data = decodedFrame.rgba;

		assert(data.size() == width * height * 4);

		bool share_video;
		{
			std::lock_guard lock(mtx_users);
			share_video = users[get(ui.room_user.chosen_user)].share_video;
		}
		if (share_video) {
			img.resize(nullptr, width, height, 4);
			img.resetData(data, 4);
		}
		else {
			img.resize(nullptr, 1, 1, 4);
			img.resetData({ 0,0,0,0 }, 4);
		}
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
		if (window->shouldClose()) [[unlikely]] { 
			p_show.setEnd(true, ShowPara::End::Default); 
			set(close_signal, true);  
			return Page::Tmp; 
		}
		glfwPollEvents();
		renderDealing();
	}

	println("show ended");
	switch (p_show.showEnd()) {
	case ShowPara::End::clientClosed:
		return makeTmp("clientClosed", "your tcp socket closed in the middle.", Page::connectToServer);
		break;
	case ShowPara::End::kickOff:
		return makeTmp("kickOff", "you are kicked off by the room host.", Page::connectToServer);
		break;
	case ShowPara::End::Return:
		return Page::connectToServer;
		break;
	default:
		assert(false);
	}
	return Page::Tmp;
}

App::Page App::Tmp() {
	for (;;) {
		if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
		PageRenderGuard pageRenderGuard{ this, p_tmp.title.c_str(), 20};
		ImGui::Text(p_tmp.content.c_str());
		if (ImGui::Button("<-")) { break; }
	}
	return p_tmp.back;
}

App::Page App::makeTmp(std::string title, std::string content, Page back) {
	p_tmp.title = std::move(title);
	p_tmp.content = std::move(content);
	p_tmp.back = back;
	return Page::Tmp;
}

void App::pageRenderBegin(const char* title, int sleepTime) {
	std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
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
#if	USE_IMGUI_VIEWPORT
	GLFWwindow* backup = glfwGetCurrentContext();
	ImGui::UpdatePlatformWindows();
	ImGui::RenderPlatformWindowsDefault();
	glfwMakeContextCurrent(backup);   // 恢复主上下文
#endif// USE_IMGUI_VIEWPORT
#endif	
	window->swapBuffers();
}

App::Page App::chooseMode() {
	for (;;) {
		if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
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
	logger.room_id = room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	while (1) {
		while (1) {
			if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
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
		if (room_id0 == Room::invalid_passwd) {
			return makeTmp("audio connection error", "Audio connection error happened while making room. Please try reconnect", Page::connectToServer);
		}
		logger.room_id = room_id0;
		return Page::Show;
	}
}

App::Page App::enterRoom() {
	logger.room_id = room::invalid_id;
	logger.passwd = Room::invalid_passwd;
	bool room_id_not_exist{ false };
	bool passwd_wrong{ false };
	bool room_cleared{ false };
	for (;;) {
		if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
		PageRenderGuard pageRenderGuard(this, "enter room", 20);
		if (ImGui::Button("<-")) { client.Send(true); return Page::chooseMode; }
		ImGui::InputScalar("room_id", ImGuiDataType_U32, &p3.room_id);
		ImGui::InputScalar("passwd", ImGuiDataType_U32, &p3.passwd);
		ImGui::Text("both: 1~4294967295");
		bool want_continue{ false };
		if (p3.room_id == room::invalid_id)		{ ImGui::Text("room_id format invalid"); want_continue = true; }
		if (p3.passwd == Room::invalid_passwd)	{ ImGui::Text("passwd format invalid"); want_continue = true; }
		if (want_continue) { continue; }
		if (room_id_not_exist)	{ ImGui::Text("room id not exist"); } 
		if (passwd_wrong)		{ ImGui::Text("passwd wrong"); }
		if (room_cleared)		{ ImGui::Text("others in the room all left while you're entering"); }
		if (ImGui::Button("->")) {
			client.Send(false);
			client.Send(p3.room_id);
			client.Send(p3.passwd);
			bool room_id_ok{};
			if (!client.ReceiveBy(room_id_ok))	{ return makeTmpServerStatusError(); }
			if (!room_id_ok)					{ room_id_not_exist = true; continue; }
			else { room_id_not_exist = false; }
			bool passwd_ok{};
			if (!client.ReceiveBy(passwd_ok))	{ return makeTmpServerStatusError(); }
			if (!passwd_ok)						{ passwd_wrong = true; continue; }
			else { passwd_wrong = false; }
			bool sync_audio_connection;
			if(!client_audio.ReceiveBy(sync_audio_connection)) {  
				return makeTmp("audio connection error", "Audio connection error happened while entering room. Please try reconnect", Page::connectToServer);
			}
			bool enter_ok;
			if (!client.ReceiveBy(enter_ok))	{ return makeTmpServerStatusError(); }
			if (!enter_ok)						{ room_cleared = !enter_ok; continue; }
			else { room_cleared = false; }
			break;
		}
	}
	logger.passwd = p3.passwd;
	logger.room_id = p3.room_id;
	return Page::Show;
}

App::Page App::connectToServer() {
	logger.name = {};

	user::id_t get_id;
	bool first{ true };
	loop {
		client = net::tcp::Client{ net::tcp::Ip::v4 };
		client_audio = net::tcp::Client{ net::tcp::Ip::v4 };
		loop {
			if (window->shouldClose()) { set(close_signal, true); return Page::Tmp; }
			PageRenderGuard pageRenderGuard{this, "connect to server", 20 };
			ImGui::Text("your tcp socket: %zu", static_cast<size_t>(client.Id()));
			ImGui::InputText("target_ipv4", p1.ipv4, sizeof(p1.ipv4));
			ImGui::InputScalar("target_port", ImGuiDataType_U16, &p1.port);
			ImGui::InputText("your_name", p1.name, sizeof(p1.name), ImGuiInputTextFlags_CharsNoBlank);
			bool ok{ true };
			if (!first)						{ ImGui::Text("connect failed"); } 
			if (strlen(p1.name) == 0)		{ ImGui::Text("name should not be blank"); ok = false; }
			if (p1.port == p1.invalid_port)	{ ImGui::Text("%s: port invalid - should be a unsigned number not 0", p1.port); ok = false; }
			if (ok && ImGui::Button("connect")) { break; }
		}
		bool sync_connection;
		if (client.ConnectTo(net::tcp::Ip::v4, p1.ipv4, p1.port) &&
			client.Send(connect_user) &&
			client.ReceiveBy(get_id) &&

			client_audio.ConnectTo(net::tcp::Ip::v4, p1.ipv4, p1.port) &&
			client_audio.Send(connect_audio) &&
			client_audio.Send(get_id) &&
			client_audio.ReceiveBy(sync_connection)
		) {
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

	set(logger.id, get_id);
	println("id: " << logger.id);
	println("socket: " << client.Id());
	println("name: " << logger.name);
	return Page::chooseMode;
}

void App::run() {
#ifdef NDEBUG
	av_log_set_level(AV_LOG_ERROR); // ffmpeg log
#endif//NDEBUG

	while (!get(close_signal)) {
		switch (page) {
		case Page::connectToServer: page = connectToServer(); break;
		case Page::chooseMode: page = chooseMode(); break;
		case Page::makeRoom: page = makeRoom(); break;
		case Page::enterRoom: page = enterRoom(); break;
		case Page::Show: page = Show(); break;
		case Page::Tmp: page = Tmp(); break;
		}
	}

}

App::App() {

#ifdef _WIN32
	timeBeginPeriod(1);   // App::App() 里，配合 App::~App() 的 timeEndPeriod(1)
#endif


#if USE_IMGUI
	main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	//SetImGuiContext(ImGui::GetCurrentContext());
	io = &ImGui::GetIO();
	io->ConfigFlags =
#if USE_IMGUI_VIEWPORT
		ImGuiConfigFlags_ViewportsEnable |
#endif// USE_IMGUI_VIEWPORT
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
	//io->Fonts->AddFontFromFileTTF("font/MapleMonoNL-CN-Regular.ttf", 15.0f, &fontConfig, io->Fonts->GetGlyphRangesChineseFull());
	io->Fonts->AddFontFromFileTTF(FONT_CJK_PATH, 15.0f, &fontConfig, io->Fonts->GetGlyphRangesChineseFull());
	//io.Fonts->Build();
	//io.Fonts->AddFontFromFileTTF("font/msyh.ttc", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window->m_get, true);
	ImGui_ImplOpenGL3_Init("#version 330");
#endif

    // Resolve plugin assets relative to the executable, independent of VS cwd.
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto assets = std::filesystem::path(executable).parent_path() / "plugin";
    const auto utf8 = [](const std::filesystem::path& p) {
        auto bytes = p.u8string(); return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const auto setting = [](const char* key, const std::string& fallback) {
        char* value = nullptr; size_t n = 0;
        _dupenv_s(&value, &n, key);
        std::string result = value && *value ? value : fallback;
        free(value); return result;
    };
    const auto dll = utf8(assets / "plugin_ASR.dll");
    bool loaded = plug.asr.loader.load(dll.c_str());
    if (loaded) {
        loaded = plug.asr.loader.get(plug.asr.defaults, plugin::name::asrDefaults)
            && plug.asr.loader.get(plug.asr.create, plugin::name::asrCreate)
            && plug.asr.loader.get(plug.asr.push, plugin::name::asrPush)
            && plug.asr.loader.get(plug.asr.poll, plugin::name::asrPoll)
            && plug.asr.loader.get(plug.asr.flush, plugin::name::asrFlush)
            && plug.asr.loader.get(plug.asr.reset, plugin::name::asrReset)
            && plug.asr.loader.get(plug.asr.destroy, plugin::name::asrDestroy);
    }
    if (loaded) {
        plugin_asr_config config{}; plug.asr.defaults(&config);
        const auto model = setting("FLEXMEET_ASR_MODEL", utf8(assets / "ggml-base.bin"));
        const auto vad = setting("FLEXMEET_ASR_VAD", utf8(assets / "ggml-silero-v6.2.0.bin"));
        config.model_path = model.c_str(); config.vad_model_path = vad.c_str();
        config.n_threads = std::clamp(atoi(setting("FLEXMEET_ASR_THREADS", std::to_string(config.n_threads)).c_str()), 1, 64);
        config.use_gpu = setting("FLEXMEET_ASR_GPU", "1") != "0";
        char error[1024]{};
        if (plug.asr.create(&config, &plug.asr.stream, error, sizeof(error)) != PLUGIN_ASR_OK) {
            asrStatus = "Subtitles unavailable: check model files and ASR startup log.";
            std::cerr << "ASR initialization: " << error << "\nmodel: " << model << "\nVAD: " << vad << '\n';
        }
    } else {
        asrStatus = "Subtitles unavailable: ASR plugin missing or incompatible.";
        std::cerr << "ASR: cannot load streaming exports from " << dll << '\n';
    }


	loadUiState();
}

App::~App() {
	storeUiState();

    if (plug.asr.stream) plug.asr.destroy(plug.asr.stream);

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

#ifdef _WIN32
	timeEndPeriod(1);   // App::App() 里，配合 App::~App() 的 timeEndPeriod(1)
#endif
}

int main() {
	auto app = std::make_unique<App>();
	app->run();
}
