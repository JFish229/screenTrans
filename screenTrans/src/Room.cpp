// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include <Room.h>
#include <asr_trace.hpp>
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
void Room::threadSendMsg(User* clientMtx) {
	{
		std::lock_guard lock(m_mtx_client_sockets);
		if(m_host_source.load()==net::invalid_socket) m_host_source.store(m_client_sockets.empty() ? net::invalid_socket : m_client_sockets[0]->id, std::memory_order_release);
	}

	Client* client = &clientMtx->client;
	Client* client_audio = &clientMtx->client_audio;

	const user::id_t save_id = clientMtx->id;
	println("connected to client user_id: " << save_id);

	using Flag0 = uint8_t;
	enum : Flag0 {
		flag_clientChoiceRecorded = (Flag0)1 << 0,
	};
	Flag0 flags = 0;
	std::atomic<bool> closedSignalSent{false};

	std::jthread audioThread(&Room::threadSendAudio, this, clientMtx, std::ref(closedSignalSent));

	user::id_t last_chosen_user = user::invalid_id;
	struct {
		user::id_t id{};
		std::string name{};
		bool is_host_from_client{};
		uint32_t set_passwd{ Room::invalid_passwd };
		user::id_t wanna_kick_off{};
		user::id_t transfer_host{};
		//bool mute_all_once{};
		//bool mute_all_keep{};
		//bool share_audio{};
		//std::vector<float> audio_frames{};
		bool has_words_msg;
		user::id_t words_msg_target{};
		char words_msg[words_msg_bytes];
		user::id_t choose_user{};
		bool share_video{};
		int32_t pk_size{};
		std::vector<std::vector<uint8_t>> packets{};
		void restore() {

		}
	}rv;
	for (;;) {
		if (client->Closed() && closedSignalSent.load(std::memory_order_acquire)) [[unlikely]] { break; }
		
		rv.restore();

		int32_t packet_count{};

		if (!client->ReceiveBy(rv.id)) client->Close();
        rv.id=save_id;
		client->ReceiveBy(rv.name);
		client->ReceiveBy(rv.is_host_from_client);
		if (rv.is_host_from_client) {
			client->ReceiveBy(rv.set_passwd);
			client->ReceiveBy(rv.wanna_kick_off);
			client->ReceiveBy(rv.transfer_host);
			//client->ReceiveBy(rv.mute_all_once);
			//client->ReceiveBy(rv.mute_all_keep);
		}
		//client->ReceiveBy(rv.share_audio);
		//if (rv.share_audio) {
		//	client->ReceiveBy(rv.audio_frames);
		//}
		client->ReceiveBy(rv.has_words_msg);
		if (rv.has_words_msg) {
			client->ReceiveBy(rv.words_msg_target);
			client->ReceiveBy(rv.words_msg, sizeof(rv.words_msg));
		}
        std::vector<uint8_t> caption_wire; std::vector<captions::Event> incoming;
        if (!client->ReceiveBounded(caption_wire, captions::max_wire) || !captions::decode(caption_wire,incoming) || incoming.size()>64) { client->Close(); }
        if (!client->Closed()) {
            std::lock_guard caption_lock(m_caption_mutex);
            for(auto& event:incoming) { event.source=save_id; event.commit=0; m_captions.apply(std::move(event),true); }
        }
        client->ReceiveBy(rv.choose_user);
		client->ReceiveBy(rv.share_video);
		if (rv.share_video) {
			if (client->ReceiveBy(rv.pk_size)) [[likely]] {
				if (rv.pk_size<0 || rv.pk_size>128) { client->Close(); rv.pk_size=0; }
                packet_count = rv.pk_size;
				rv.packets.resize(std::max(rv.packets.size(), (size_t)packet_count));
				for (int32_t i = 0;i < packet_count;++i) {
					client->ReceiveBy(rv.packets[i]);
				}
			}
		}

		//std::vector<std::vector<uint8_t>> packets;
		if (!client->Closed()) [[likely]] {
			if (rv.is_host_from_client && m_host_source.load()==clientMtx->id){
				if (rv.set_passwd != Room::invalid_passwd && rv.set_passwd != this->passwd()) [[unlikely]] {
					this->setPasswd(rv.set_passwd);
				}
				if (rv.transfer_host != user::invalid_id) [[unlikely]] {
					this->setHost(rv.transfer_host);
				}
			}

			if (!(flags & flag_clientChoiceRecorded) || (rv.choose_user != last_chosen_user)) {
				{
					std::lock_guard lock(m_mutex_choices);
					m_choices_of[client->Id()] = rv.choose_user;
				}
				last_chosen_user = rv.choose_user;
				flags |= flag_clientChoiceRecorded;
			}

		}
        std::vector<uint8_t> caption_snapshot;
        { std::lock_guard caption_lock(m_caption_mutex); if(client->Closed()) m_captions.depart(save_id); caption_snapshot=captions::encode(m_captions.snapshot()); }
        {
            bool is_host_from_server = m_host_source.load(std::memory_order_acquire) == clientMtx->id;
			std::shared_lock lock(m_mtx_client_sockets);
			std::for_each(std::execution::par, m_client_sockets.begin(), m_client_sockets.end(), 
			[&, packet_count](std::unique_ptr<User>& cm) {
				std::lock_guard lock(cm->mtx_client);
				Client* c = &cm->client;

				if (client->Closed()) [[unlikely]] {
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

				c->Send(save_id);
				c->Send(rv.name);
				c->Send(is_host_from_server);
				if (is_host_from_server && c->Id() == client->Id()) {
					c->Send(this->passwd());
				}
				c->Send(rv.is_host_from_client);
				if (rv.is_host_from_client) {
					if (is_host_from_server) [[likely]] {
						c->Send(rv.wanna_kick_off);
						//c->Send(rv.mute_all_once);
						//c->Send(rv.mute_all_keep);
					}
					else {
						c->Send(user::invalid_id);
						//c->Send(false);
						//c->Send(false);
					}
				}
				//c->Send(rv.share_audio);
				//if (rv.share_audio) {
				//	c->Send(rv.audio_frames);
				//}
				c->Send(rv.has_words_msg);
				if (rv.has_words_msg) [[unlikely]] {
					c->Send(rv.words_msg_target);
					if (rv.words_msg_target == user::public_target || cm->id == rv.words_msg_target) {
						c->Send(rv.words_msg, sizeof(rv.words_msg));
					}
				}
				c->Send(caption_snapshot);
				c->Send(rv.share_video);
				if (choice_isnt_me) {
					return;
				}
				if (rv.share_video) {
					c->Send(rv.pk_size);
					for (int i = 0;i < packet_count;++i) {
						c->Send(rv.packets[i]);
					}
				}
			});
		}
	}

	client_audio->Close();
	println("closed client user_id: " << save_id);
	if(audioThread.joinable()) audioThread.join();
    clientMtx->finished.store(true);
    m_cs_dead_threads.release();
}

void Room::threadSendAudio(User* clientMtx, std::atomic<bool>& closedSignalSent) {
	Client* client = &clientMtx->client;
	Client* client_audio = &clientMtx->client_audio;
    asr_trace::Writer trace("server-audio");
    uint64_t current_epoch=0,expected_seq=0,expected_start=0;
    for (;;) {
        uint64_t claimed=0,epoch=0,seq=0,start=0; faudio_t flags=0; std::vector<float> pcm;
        if (!client_audio->ReceiveBy(claimed) || !client_audio->ReceiveBy(flags) ||
            !client_audio->ReceiveBy(epoch) || !client_audio->ReceiveBy(seq) || !client_audio->ReceiveBy(start) ||
            ((flags & faudio_share) && !client_audio->ReceiveBounded(pcm, 16000*4)) || !asr_trace::valid(pcm)) break;
        if(!epoch || (flags&~faudio_t(15))) break;
        if(epoch!=current_epoch) {
            if(epoch<current_epoch || seq || start) break;
            current_epoch=epoch; expected_seq=expected_start=0;
        }
        if(seq!=expected_seq || start!=expected_start) { trace.record("invalid-timeline",clientMtx->id,epoch,seq,start); break; }
        ++expected_seq; expected_start+=pcm.size();
        const auto source=clientMtx->id;
        if(m_host_source.load()!=clientMtx->id) flags &= ~(faudio_is_host|faudio_mute_all_once|faudio_mute_all_keep);
        trace.record("received",source,epoch,seq,start,pcm);
        std::shared_lock lock(m_mtx_client_sockets);
        for(auto& cm:m_client_sockets) {
            std::lock_guard audio_lock(cm->mtx_audio); auto& c=cm->client_audio;
            if (!c.Send(source)||!c.Send(flags)||!c.Send(epoch)||!c.Send(seq)||!c.Send(start)||((flags&faudio_share)&&!c.Send(pcm))) c.Close();
        }
    }
    client_audio->Close(); client->Close();
}

void Room::threadDeleteThread() {
	for (;;) {
		m_cs_dead_threads.acquire();
		if (id() == room::invalid_id) { break; }
		{
			std::lock_guard lock_cs(m_mtx_client_sockets);
			for (size_t i = 0; i < m_client_sockets.size(); ) {
				if (m_client_sockets[i]->finished.load()) {
					m_clients_threads.erase(m_clients_threads.begin() + i);
					const bool departed_host=m_host_source.load()==m_client_sockets[i]->id;
                    m_client_sockets.erase(m_client_sockets.begin() + i);
					if(departed_host) m_host_source.store(m_client_sockets.empty() ? net::invalid_socket : m_client_sockets[0]->id, std::memory_order_release);
				}
				else {
					++i;
				}
			}
		}
	}
}

Room::Room(uint32_t passwd) :
	m_passwd(passwd),
	m_delete_thread_thread(&Room::threadDeleteThread, this)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint32_t> dist(1, 0xFFFFFFFF);
	while (id() == room::invalid_id) {
		m_id.store(dist(gen), std::memory_order_release);
		std::lock_guard lock(mtx_ids);
		if (Room::ids.end() != std::lower_bound(Room::ids.begin(), Room::ids.end(), id())) {
			m_id.store(room::invalid_id, std::memory_order_release);
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
	m_id.store(room::invalid_id, std::memory_order_release);
	m_cs_dead_threads.release();
}

void Room::pushClient(Client c, Client c_audio, user::id_t source) {
	{
		std::lock_guard lock(m_mtx_used);
		//m_flag |= f_used;
		m_used.store(true, std::memory_order_release);
	}
	{
		std::lock_guard lock(m_mtx_client_sockets);
		std::unique_ptr<User> user = std::make_unique<User>(source, std::move(c), std::move(c_audio));
		auto ptr = user.get();
		m_client_sockets.emplace_back(std::move(user));
		m_clients_threads.emplace_back(std::jthread(&Room::threadSendMsg, this, ptr));
	}
}