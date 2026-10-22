// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "audio/Capture.hpp"
#include <iostream>
#include <thread>

namespace audio {

    Capture::Capture(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames,
        double bufSec
    ):
        m_buf(static_cast<size_t>(sampleRate* channels* bufSec))
    {
        Init(sampleRate, channels, periodSizeInFrames);
    }

    void Capture::Reset(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        bool is_running = m_running;
        Stop();
        Init(sampleRate, channels, periodSizeInFrames);
        if (is_running) {
            Start();
        }
    }

    Capture::~Capture()
    {
        Stop();
        if (m_initiated) {
            ma_device_uninit(&m_device);
        }
    }

    void Capture::Init(
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t periodSizeInFrames
    ) {
        if (m_initiated) {
            ma_device_uninit(&m_device);
        }

        m_channels = channels;

        ma_device_config config =
            ma_device_config_init(ma_device_type_capture);

        config.capture.format = ma_format_f32;
        config.capture.channels = channels;

        config.sampleRate = sampleRate;

        config.periodSizeInFrames = periodSizeInFrames;
        config.periods = 2;

        config.dataCallback = DataCallback;
        config.pUserData = this;

        ma_result result =
            ma_device_init(nullptr, &config, &m_device);

        if (result != MA_SUCCESS) {
            std::cerr << "Capture::init(...) error" << std::endl;
            system("pause");
            exit(1);
        }

        m_initiated = true;
    }

    bool Capture::Start() {
        auto result = ma_device_start(&m_device);
        if (result != MA_SUCCESS)
        {
            std::cerr << "Capture::Start() error" << std::endl;
            return false;
        }
        m_running = true;
        std::clog << "Microphone started: " << m_device.capture.name
                  << " | " << m_device.sampleRate << " Hz | channels=" << m_channels << '\n';
        return true;
    }

    void Capture::Stop()
    {
        if (m_running)
        {
            m_running = false;
            ma_device_stop(&m_device);
            // 等待设备状态变为已停止
            while (ma_device_is_started(&m_device)) {
                std::this_thread::yield();  // 出让时间片，避免忙等占用CPU
            }
            while (m_activeCallbacks.load(std::memory_order_acquire) > 0) {
                std::this_thread::yield();
            }
        }
    }

    std::vector<float>& Capture::giveFrames(std::vector<float>& out) {
        const size_t samples = m_buf.read_available();
        out.resize(samples);
        m_buf.pop(out.data(), samples);
        return out;
    }

    void Capture::DataCallback(
        ma_device* device,
        void* output,
        const void* input,
        ma_uint32 frameCount)
    {
        (void)output;
        auto* self = static_cast<Capture*>(device->pUserData);
        if (!self || !input || !self->m_running.load(std::memory_order_relaxed)) { return; }
        self->m_activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const float* pcm = static_cast<const float*>(input);
        const size_t requested = static_cast<size_t>(frameCount) * self->m_channels;
        const size_t accepted = self->m_buf.push(pcm, requested);
        self->m_droppedSamples.fetch_add(requested - accepted, std::memory_order_relaxed);
        self->m_activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

}
