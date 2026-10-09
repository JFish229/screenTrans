// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <Room.h>
#include <iostream>
#include <st_signals.h>
#include <cassert>
#include <random>
#include <execution>
#undef max
#undef min
#define println(x) std::cout<< x << "\n"
#define print(x) std::cout<< x
#ifndef NDEBUG
#	define PL println(__LINE__)
#else
#	define PL
#endif

using net::tcp::Client;

//#define pv(x) std::cout<< #x << ": " << x << "\n"

/*
recv: signals, id(, name, audio_frames(, pk_size, pk[n]))
send: signals, id(, name, is_host, audio_frames(, pk_size, pk[n]))

if closed
	send: signals, id

if choiceNotMatch
	send: signals, id, name, ...

*/
void Room::threadSendMsg(ClientMtx* clientMtx) {
	{
		std::lock_guard lock(m_mtx_client_sockets);
		m_host_skt.store(m_client_sockets.empty() ? net::invalid_socket : m_client_sockets[0]->client.Id(), std::memory_order_release);
	}

	net::socket_t save_id = net::invalid_socket;
	Client* client = &clientMtx->client;

	using Flag0 = uint8_t;
	enum : Flag0 {
		flag_clientChoiceRecorded = (Flag0)1 << 0,
	};
	Flag0 flags = 0;
	std::atomic<bool> closedSignalSent{false};
	net::socket_t last_chosen_socket = net::invalid_socket;
	struct {
		net::socket_t id;
		std::string name;
		bool is_host_from_client;
		net::socket_t wanna_kick_off;
		std::vector<float> audio_frames;
		net::socket_t choose_socket;
		int32_t pk_size;
		std::vector<std::vector<uint8_t>> packets;
	}rv;
	for (;;) {
		if (client->Closed() && closedSignalSent.load(std::memory_order_acquire)) { break; }
		client->ReceiveBy(rv.id);
		client->ReceiveBy(rv.name);
		client->ReceiveBy(rv.is_host_from_client);
		if (rv.is_host_from_client) {
			client->ReceiveBy(rv.wanna_kick_off);
		}
		client->ReceiveBy(rv.audio_frames);
		client->ReceiveBy(rv.choose_socket);
		client->ReceiveBy(rv.pk_size);
		int32_t packet_count{};
		//std::vector<std::vector<uint8_t>> packets;
		if (!client->Closed()) {
			if (save_id == net::invalid_socket) { // since there always be at least one loop, this "=" will always be done
				save_id = rv.id;
				println("connected to client socket: " << save_id);
			}

			if (!(flags & flag_clientChoiceRecorded) || (rv.choose_socket != last_chosen_socket)) {
				{
					std::lock_guard lock(m_mutex_choices);
					m_choices_of[client->Id()] = rv.choose_socket;
				}
				last_chosen_socket = rv.choose_socket;
				flags |= flag_clientChoiceRecorded;
			}

			packet_count = client->Closed() ? 0 : rv.pk_size;
			rv.packets.resize(std::max(rv.packets.size(), (size_t)packet_count));
			for (int32_t i = 0;i < packet_count;++i) {
				client->ReceiveBy(rv.packets[i]);
			}
		}
		{
			std::shared_lock lock(m_mtx_client_sockets);
			std::for_each(std::execution::par, m_client_sockets.begin(), m_client_sockets.end(), 
			[&, packet_count](std::unique_ptr<ClientMtx>& cm) {
				std::lock_guard lock(cm->mtx);
				Client* c = &cm->client;

				if (client->Closed()) {
					c->Send(signal_closed);
					c->Send(save_id);
					closedSignalSent.store(true, std::memory_order_release);
					return;
				}

				bool choice_isnt_me;
				{
					std::shared_lock lock_choices(m_mutex_choices);
					auto choice = m_choices_of.find(c->Id());
					choice_isnt_me = choice == m_choices_of.end() || choice->second != rv.id;
				}
				if (choice_isnt_me) { c->Send(signal_choiceNotMatch); }
				else				{ c->Send(signal_none); }

				bool is_host_from_server = m_host_skt.load(std::memory_order_acquire) == client->Id();
				c->Send(rv.id);
				c->Send(rv.name);
				c->Send(is_host_from_server);
				c->Send(rv.is_host_from_client);
				if (rv.is_host_from_client) {
					c->Send(rv.wanna_kick_off);
				}
				c->Send(c->Id() == client->Id() ? std::vector<float>{} : rv.audio_frames);
				if (choice_isnt_me) {
					return;
				}
				c->Send(rv.pk_size);
				for (int i = 0;i < packet_count;++i) {
					c->Send(rv.packets[i]);
				}
			});
		}
	}

	println("closed client socket: " << save_id);
	m_cs_dead_threads.release();
}

void Room::threadDeleteThread() {
	for (;;) {
		m_cs_dead_threads.acquire();
		if (id() == invalid_id) { break; }
		{
			std::lock_guard lock_cs(m_mtx_client_sockets);
			for (size_t i = 0; i < m_client_sockets.size(); ) {
				if (m_client_sockets[i]->client.Closed()) {
					m_clients_threads.erase(m_clients_threads.begin() + i);
					m_client_sockets.erase(m_client_sockets.begin() + i);
					m_host_skt.store(m_client_sockets.empty() ? net::invalid_socket : m_client_sockets[0]->client.Id(), std::memory_order_release);
				}
				else {
					++i;
				}
			}
		}
	}
}

Room::Room(const std::optional<uint32_t>& passwd) :
	m_passwd(passwd),
	m_delete_thread_thread(&Room::threadDeleteThread, this)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint32_t> dist(1, 0xFFFFFFFF);
	while (id() == invalid_id) {
		m_id.store(dist(gen), std::memory_order_release);
		std::lock_guard lock(mtx_ids);
		if (Room::ids.end() != std::lower_bound(Room::ids.begin(), Room::ids.end(), id())) {
			m_id.store(invalid_id, std::memory_order_release);
		}
		else {
			ids.push_back(id());
		}
	}
	{
		std::lock_guard lock(mtx_ids);
		std::sort(Room::ids.begin(), Room::ids.end());
	}
}

Room::~Room() {
	assert(m_client_sockets.size() == 0);
	//end and join m_delete_thread_thread
	m_id.store(Room::invalid_id, std::memory_order_release);
	m_cs_dead_threads.release();
}

void Room::pushClient(Client c) {
	{
		std::lock_guard lock(m_mtx_used);
		m_flag |= f_used;
	}
	{
		std::lock_guard lock(m_mtx_client_sockets);
		std::unique_ptr<ClientMtx> client = std::make_unique<ClientMtx>(std::move(c));
		auto ptr = client.get();
		m_client_sockets.emplace_back(std::move(client));
		m_clients_threads.emplace_back(std::jthread(&Room::threadSendMsg, this, ptr));
	}
}