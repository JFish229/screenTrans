// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <vector>
#include <thread>
#include <net/tcp/Client.hpp>
#include <optional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <semaphore>

class Room {
public:
	constexpr static inline uint32_t invalid_id = 0;
	constexpr static inline uint32_t invalid_passwd = 0;
private:
	static inline std::mutex mtx_ids;
	static inline std::vector<uint32_t> ids = {};
private:
	using Flag = uint8_t;
	enum : Flag{
		f_none = 0,
		f_used = (Flag)1 << 0, // used to have at least one client
	};
	Flag m_flag = f_none;
	std::atomic<uint32_t> m_id = invalid_id;
	std::atomic<net::socket_t> m_host_skt{ net::invalid_socket };
	std::optional<std::atomic<uint32_t>> m_passwd{};
	std::counting_semaphore<> m_cs_dead_threads{0};
	std::jthread m_delete_thread_thread;
	std::shared_mutex m_mtx_client_sockets;
	std::shared_mutex m_mutex_choices;
	struct ClientMtx { 
		net::tcp::Client client;
		std::mutex mtx;
	};
	std::vector<std::unique_ptr<ClientMtx>> m_client_sockets;
	std::vector<std::jthread> m_clients_threads;
	std::mutex m_mtx_used;
	std::mutex m_mtx_need_check;
	std::unordered_map<net::socket_t, net::socket_t> m_choices_of; // <socket of client(in server), client socket choice>
public:
	Room(const std::optional<uint32_t>& passwd = {});
	~Room();
	Room(const Room&) = delete;
	Room(Room&&) noexcept = delete;
	Room& operator=(const Room&) = delete;
	Room& operator=(Room&&) noexcept = delete;
	bool used() { std::lock_guard lock(m_mtx_used); return m_flag & f_used; }
	bool empty() { std::lock_guard lock(m_mtx_client_sockets); return m_client_sockets.size() == 0; }
	uint32_t id() { return m_id.load(std::memory_order_acquire); }
	uint32_t passwd() { assert(m_passwd.has_value()); return m_passwd->load(std::memory_order_acquire); }
	void setPasswd(uint32_t passwd) { m_passwd = passwd; }
	//std::unique_ptr<Client>& client(size_t i) { return m_client_sockets[i]; }
	std::jthread& thread(size_t i) { return m_clients_threads[i]; }
	void pushClient(net::tcp::Client c);
private:
	void threadSendMsg(ClientMtx* cm);
	void threadDeleteThread();
};