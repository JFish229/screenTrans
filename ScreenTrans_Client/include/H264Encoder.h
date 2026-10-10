// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "ST_API.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace ST {

    class ST_API H264Encoder {
    public:
        // 源尺寸 = 编码尺寸
        H264Encoder(int width, int height, int fps, int bitrate);

        // 源尺寸与编码尺寸分离
        H264Encoder(int srcWidth, int srcHeight,
            int encWidth, int encHeight,
            int fps, int bitrate);

        ~H264Encoder() { Destroy(); }

        // 源尺寸 = 编码尺寸
        bool Reset(int width, int height, int fps, int bitrate);

        // 源尺寸与编码尺寸分离
        bool Reset(int srcWidth, int srcHeight,
            int encWidth, int encHeight,
            int fps, int bitrate);

        // 只修改输入源尺寸，不重建编码器
        bool SetSourceSize(int srcWidth, int srcHeight);

        std::vector<std::vector<uint8_t>> EncodeFrame(const std::vector<uint8_t>& rgba);
        std::vector<std::vector<uint8_t>> Flush();

        // 访问器
        int Width() const { return m_width; }              // 编码宽度
        int Height() const { return m_height; }            // 编码高度
        int SourceWidth() const { return m_src_width; }    // 输入源宽度
        int SourceHeight() const { return m_src_height; }  // 输入源高度
        int Fps() const { return m_fps; }
        int Bitrate() const { return m_bitrate; }

    private:
        bool Initialize(int srcWidth, int srcHeight,
            int encWidth, int encHeight,
            int fps, int bitrate);

        void Destroy();

        const AVCodec* SelectEncoder(std::string& out_name, bool& out_hw,
            int width, int height,
            int fps, int bitrate);

    private:
        AVCodecContext* m_ctx = nullptr;
        AVFrame* m_frame = nullptr;
        AVPacket* m_pkt = nullptr;
        SwsContext* m_sws = nullptr;

        int m_src_width = 0;    // 输入 RGBA 宽度
        int m_src_height = 0;   // 输入 RGBA 高度
        int m_width = 0;        // 编码输出宽度
        int m_height = 0;       // 编码输出高度

        int m_fps = 0;
        int m_bitrate = 0;
        int64_t m_pts = 0;

        std::string m_encoder_name;
        bool m_is_hw = false;
    };

}