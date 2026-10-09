// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define USE_IMGUI 1

#if	USE_IMGUI
#	include "imgui.h"
#	include "imgui_impl_glfw.h"
#	include "imgui_impl_opengl3.h"

#	if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#		pragma comment(lib, "legacy_stdio_definitions")
#	endif
#endif

#include <net/tcp/Client.hpp>
#include <net/Socket.hpp>
#include <H264Encoder.h>
#include <H264Decoder.h>
#include <audio/Capture.hpp>
#include <audio/Player.hpp>
#include <audio/User.hpp>
#include <ScreenCapture.h>
#include <st_signals.h>
#include <Room.h>

#include <plugin/Loader.hpp>
#include <plugin/Asr.hpp>

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <vector>
#include <chrono>
#include <semaphore>
using std::literals::chrono_literals::operator""ms;

#include <boost/lockfree/spsc_queue.hpp>

#include <gl/Window.h>
#include <gl/Shader.h>
#include <gl/VertexArray.h>
#include <gl/VertexBuffer.h>
#include <gl/IndexBuffer.h>
#include <gl/Render.h>
#include <gl/Texture.h>
#include <gl/util.h>

#ifndef NDEBUG
#	define print(x) std::cout<< x
#else
#	define print(x)
#endif

#define println(x) print(x << "\n")
#define PL println(__LINE__)
#define loop for(;;)

#undef min
#undef max

using ST::H264Encoder, ST::H264Decoder, ST::ScreenCapture;

inline constexpr int SLEEP_TIME = 0;
inline constexpr size_t MAX_VIDEO_FRAMES = 10;
inline constexpr size_t KEEP_FRAMES = 4;
inline constexpr double MAX_MAX_FPS = 150.0;
inline constexpr double MIN_MAX_FPS = 5.0;
namespace video {
	inline constexpr int min_bitrate = 2'000'000;
	inline constexpr int max_bitrate = 20'000'000;
}

namespace audio {
	inline constexpr uint32_t sampleRate = 16'000;
	inline constexpr uint32_t channels = 1;
	inline constexpr uint32_t periodSizeInFrames = 1000;
	static_assert(sampleRate% periodSizeInFrames == 0);
	inline constexpr double periodSec = static_cast<double>(periodSizeInFrames) / sampleRate;
	inline constexpr int mixPeriods = 5;
	inline constexpr int bufSec = 1;
}

template<typename Fn>
struct Defer {
	Fn m_fn;
	Defer(Fn fn): m_fn(std::move(fn)){}
	Defer(const Defer&) = delete;
	Defer(Defer&&) = delete;
	~Defer() { m_fn(); }
};

#define XCONCAT(x, y) x##y
#define CONCAT(x, y) XCONCAT(x, y) 
#define NAME(name) CONCAT(CONCAT(name##_, __LINE__), __)
#define DEFER Defer NAME(defer)


class App {
private:
	static std::unique_ptr<gl::Window> makeWindow();
	template<bool lock>
	[[nodiscard]] bool isHost();
	[[nodiscard]] bool clientClosed();
private:
	/* recv: signals, id (, name, is_host_from_server, is_host_from_client(, wanna_kick_off), audio_frames(, pk_size, pk[n])) */
	void Receive();
	/* send: id, name, is_host_from_client(, wanna_kick_off), audio_frames, pk_size, pk[n] */
	void Send();
	void audioMix();
	void asrRecv();
private:
	void pageRenderBegin(const char* title, int sleepTime);
	void pageRenderEnd();
	struct PageRenderGuard {
		App* p;
		PageRenderGuard(App* self, const char* title, int sleepTime) : p{ self } { p->pageRenderBegin(title, sleepTime); }
		~PageRenderGuard() { p->pageRenderEnd(); }
	};
	friend struct PageRenderGuard;
	enum class Page {
		connectToServer,
		chooseMode,
		makeRoom,
		enterRoom,
		Show,
		//serverStatusError,
		Tmp,
		//count,
	};
	struct P1 {
		static inline constexpr size_t buf_size = 256;
		char ipv4[buf_size]{};
		static inline constexpr uint16_t invalid_port{ 0 };
		uint16_t port{ invalid_port };
		char name[buf_size]{};
	}p1;
	Page connectToServer();

	Page chooseMode();

	struct P2 {
		uint32_t passwd{ Room::invalid_passwd };
	}p2;
	Page makeRoom();

	struct P3 {
		uint32_t room_id{ Room::invalid_id };
		uint32_t passwd{ Room::invalid_passwd };
	}p3;
	Page enterRoom();

	//Page serverStatusError();

	struct ShowPara{
	public:
		enum class End {
			clientClosed = 0,
			kickOff,
			Default = clientClosed,
		};
		ShowPara() { restore(); }
		void setEnd(bool ended, End showEnd);
		void restore();
		bool ended() { return m_ended.load(std::memory_order_acquire); }
		End showEnd() { return m_showEnd; }
	private:		
		std::atomic<bool> m_ended{ false };
		End m_showEnd{ End::Default };
	public:
		std::counting_semaphore<> wait_threads{0};
	}p_show;
	Page Show();
	void restoreAfterShow();

	struct {
		std::string title{};
		std::string content{};
		Page back{Page::Tmp};
	}p_tmp;
	Page Tmp();
	Page makeTmp(const std::string& title, std::string content, Page back);
	Page makeTmpServerStatusError() {
		return makeTmp("server_status_error", "server status error. please reconnect or close the window", Page::connectToServer);
	}

public:
	net::InitGuard initGuard{};
	gl::GlfwInitGuard glfwInitGuard;
	std::unique_ptr<gl::Window> window{ makeWindow() };

#if USE_IMGUI
	float main_scale{};
	ImGuiIO* io{};
	ImGuiStyle* style{};
#endif

	struct {
		std::mutex mtx_name;
		std::string name{};
		uint32_t room_id{ Room::invalid_id };
		uint32_t passwd{ Room::invalid_passwd };
	} logger;
	Page page{ Page::connectToServer };

	std::mutex mtx_video_frames;
	std::mutex mtx_close;
	std::mutex mtx_users;
	std::mutex mtx_audioFramesLong;
	std::mutex mtx_speechWords;


	std::queue<ST::DecodedFrame> total_video_frames;
	std::atomic<bool> close_signal = false;
	std::atomic<net::socket_t> chosen_user; // init with self
	struct User {
		std::atomic<bool> is_host{ false };
		std::string name;
		boost::lockfree::spsc_queue<float> audioBuf{ audio::sampleRate * audio::channels * audio::bufSec };
	};
	std::unordered_map < net::socket_t, User > users; // init with self
	std::vector<float> audioFramesLong{};
	std::string speechWords;


	audio::User audioUser{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, audio::bufSec };
	audio::Player audioPlayer{ audio::sampleRate, audio::channels, audio::periodSizeInFrames, &audioUser, audio::User::callback };
	net::tcp::Client client{ net::tcp::Ip::v4 };

	struct {
		struct {
			std::atomic<bool> share_audio{ false };
			std::atomic<bool> share_video{ false };
			std::atomic<double> fps = 20;
		} send;
		struct {
			std::atomic<double> fps = 100;
			struct {
				std::mutex mtx;
				std::atomic<int> fps = 30;
				std::atomic<int> bitrate = 4'000'000;
				std::atomic<int> width{0};
				std::atomic<int> height{0};
				std::counting_semaphore<> wait_wh{0};
			} encode;
		} video;
		struct {
			std::atomic<bool> is_kicked_off{ false };
			std::atomic<net::socket_t> wanna_kick_off{ net::invalid_socket };
		} account;
	} ui;

	void* any_usage{nullptr}; // now only for renderFunc in draw function
	
	struct {
		struct {
			plugin::Loader loader{};
			plugin::fn::AsrInit init{};
			plugin::fn::AsrTranscribe transcribe{};
			plugin::fn::AsrFree free{};
		} asr;
	} plug;
public:
	App();
	~App();
	void run();
};