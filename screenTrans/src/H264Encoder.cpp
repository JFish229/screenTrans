// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Yuan Aowei
#include "H264Encoder.h"

#include <iostream>
#include <array>

namespace ST {

    // ------------------------------------------------------------------------
    // 构造函数
    // ------------------------------------------------------------------------
    H264Encoder::H264Encoder(int width, int height, int fps, int bitrate) {
        if (!Initialize(width, height, width, height, fps, bitrate)) {
            std::cerr << "H264Encoder init error" << std::endl;
        }
    }

    H264Encoder::H264Encoder(int srcWidth, int srcHeight,
        int encWidth, int encHeight,
        int fps, int bitrate) {
        if (!Initialize(srcWidth, srcHeight, encWidth, encHeight, fps, bitrate)) {
            std::cerr << "H264Encoder init error" << std::endl;
        }
    }

    // ------------------------------------------------------------------------
    // 选择编码器：使用实际编码参数进行探测
    // ------------------------------------------------------------------------
    const AVCodec* H264Encoder::SelectEncoder(std::string& out_name, bool& out_hw,
        int width, int height,
        int fps, int bitrate) {
        static const std::array<const char*, 4> encoders = {
            "libx264",
            "h264_nvenc",
            "h264_amf",
            "h264_qsv",
        };

        for (auto name : encoders) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name);
            if (!codec)
                continue;

            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            if (!ctx)
                continue;

            ctx->width = width;
            ctx->height = height;
            ctx->time_base = { 1, fps };
            ctx->framerate = { fps, 1 };
            ctx->gop_size = 12;
            ctx->max_b_frames = 0;
            ctx->bit_rate = bitrate;

            if (std::string(name) == "h264_qsv" ||
                std::string(name) == "h264_nvenc" ||
                std::string(name) == "h264_amf") {
                ctx->pix_fmt = AV_PIX_FMT_NV12;
            }
            else {
                ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            }

            AVDictionary* opts = nullptr;

            if (std::string(name) == "libx264") {
                av_dict_set(&opts, "preset", "ultrafast", 0);
                av_dict_set(&opts, "tune", "zerolatency", 0);
            }
            else if (std::string(name) == "h264_nvenc") {
                av_dict_set(&opts, "preset", "p1", 0);
                av_dict_set(&opts, "tune", "ll", 0);
            }

            int ret = avcodec_open2(ctx, codec, &opts);

            av_dict_free(&opts);
            avcodec_free_context(&ctx);

            if (ret >= 0) {
                out_name = name;
                out_hw = (std::string(name) != "libx264");

                std::cout << "Selected encoder: " << name
                    << " (hardware=" << out_hw << ")\n";

                return codec;
            }
        }

        return nullptr;
    }

    // ------------------------------------------------------------------------
    // Reset：源尺寸 = 编码尺寸
    // ------------------------------------------------------------------------
    bool H264Encoder::Reset(int width, int height, int fps, int bitrate) {
        return Reset(width, height, width, height, fps, bitrate);
    }

    // ------------------------------------------------------------------------
    // Reset：源尺寸与编码尺寸分离
    // ------------------------------------------------------------------------
    bool H264Encoder::Reset(int srcWidth, int srcHeight,
        int encWidth, int encHeight,
        int fps, int bitrate) {
        // 如果编码参数没变，只更新源尺寸，不重建编码器
        if (encWidth == m_width && encHeight == m_height &&
            fps == m_fps && bitrate == m_bitrate) {
            return SetSourceSize(srcWidth, srcHeight);
        }

        Destroy();
        return Initialize(srcWidth, srcHeight, encWidth, encHeight, fps, bitrate);
    }

    // ------------------------------------------------------------------------
    // 只修改输入源尺寸，重建 SwsContext
    // ------------------------------------------------------------------------
    bool H264Encoder::SetSourceSize(int srcWidth, int srcHeight) {
        if (srcWidth == m_src_width && srcHeight == m_src_height) {
            return true;
        }

        // 编码器还没初始化，只记录尺寸
        if (!m_ctx) {
            m_src_width = srcWidth;
            m_src_height = srcHeight;
            return true;
        }

        // 重建 SwsContext
        if (m_sws) {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }

        m_src_width = srcWidth;
        m_src_height = srcHeight;

        m_sws = sws_getContext(
            m_src_width, m_src_height, AV_PIX_FMT_RGBA,
            m_width, m_height, m_ctx->pix_fmt,
            SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr
        );

        if (!m_sws) {
            std::cerr << "SetSourceSize: sws_getContext failed" << std::endl;
            return false;
        }

        return true;
    }

    // ------------------------------------------------------------------------
    // 初始化
    // ------------------------------------------------------------------------
    bool H264Encoder::Initialize(int srcWidth, int srcHeight,
        int encWidth, int encHeight,
        int fps, int bitrate) {
        Destroy();

        m_src_width = srcWidth;
        m_src_height = srcHeight;
        m_width = encWidth;
        m_height = encHeight;
        m_fps = fps;
        m_bitrate = bitrate;

        const AVCodec* codec = SelectEncoder(m_encoder_name, m_is_hw,
            encWidth, encHeight,
            fps, bitrate);
        if (!codec)
            return false;

        m_ctx = avcodec_alloc_context3(codec);
        if (!m_ctx)
            return false;

        m_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
        m_ctx->codec_id = AV_CODEC_ID_H264;

        m_ctx->width = encWidth;
        m_ctx->height = encHeight;

        m_ctx->time_base = { 1, fps };
        m_ctx->framerate = { fps, 1 };

        m_ctx->gop_size = 12;
        m_ctx->max_b_frames = 0;
        m_ctx->bit_rate = bitrate;

        if (m_is_hw)
            m_ctx->pix_fmt = AV_PIX_FMT_NV12;
        else
            m_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

        AVDictionary* opts = nullptr;

        if (m_encoder_name == "libx264") {
            av_dict_set(&opts, "preset", "ultrafast", 0);
            av_dict_set(&opts, "tune", "zerolatency", 0);
        }
        else if (m_encoder_name == "h264_nvenc") {
            av_dict_set(&opts, "preset", "p1", 0);
            av_dict_set(&opts, "tune", "ll", 0);
        }

        av_dict_set(&opts, "repeat_headers", "1", 0);

        if (avcodec_open2(m_ctx, codec, &opts) < 0) {
            av_dict_free(&opts);
            Destroy();
            return false;
        }

        av_dict_free(&opts);

        // 分配输入帧，尺寸必须与编码器一致
        m_frame = av_frame_alloc();
        if (!m_frame) {
            Destroy();
            return false;
        }

        m_frame->format = m_ctx->pix_fmt;
        m_frame->width = encWidth;
        m_frame->height = encHeight;

        if (av_frame_get_buffer(m_frame, 32) < 0) {
            Destroy();
            return false;
        }

        m_pkt = av_packet_alloc();
        if (!m_pkt) {
            Destroy();
            return false;
        }

        // SwsContext：源 RGBA -> 编码像素格式，支持缩放
        m_sws = sws_getContext(
            m_src_width, m_src_height, AV_PIX_FMT_RGBA,
            m_width, m_height, m_ctx->pix_fmt,
            SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr
        );

        if (!m_sws) {
            Destroy();
            return false;
        }

        return true;
    }

    // ------------------------------------------------------------------------
    // 编码一帧
    // ------------------------------------------------------------------------
    std::vector<std::vector<uint8_t>>
        H264Encoder::EncodeFrame(const std::vector<uint8_t>& rgba) {
        std::vector<std::vector<uint8_t>> packets;

        if (!m_ctx)
            return packets;

        if (rgba.empty())
            return packets;

        // 校验输入 RGBA 大小是否与源尺寸匹配
        const size_t expected =
            static_cast<size_t>(m_src_width) * m_src_height * 4;

        if (rgba.size() != expected) {
            std::cerr << "EncodeFrame: RGBA size mismatch. Expected "
                << expected << ", got " << rgba.size() << std::endl;
            return packets;
        }

        if (av_frame_make_writable(m_frame) < 0)
            return packets;

        const uint8_t* src[1] = { rgba.data() };
        int stride[1] = { m_src_width * 4 };

        sws_scale(
            m_sws,
            src,
            stride,
            0,
            m_src_height,
            m_frame->data,
            m_frame->linesize
        );

        m_frame->pts = m_pts++;

        if (avcodec_send_frame(m_ctx, m_frame) < 0)
            return packets;

        while (true) {
            int ret = avcodec_receive_packet(m_ctx, m_pkt);

            if (ret == AVERROR(EAGAIN))
                break;

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
                break;

            packets.emplace_back(
                m_pkt->data,
                m_pkt->data + m_pkt->size
            );

            av_packet_unref(m_pkt);
        }

        return packets;
    }

    // ------------------------------------------------------------------------
    // 刷新编码器
    // ------------------------------------------------------------------------
    std::vector<std::vector<uint8_t>>
        H264Encoder::Flush() {
        std::vector<std::vector<uint8_t>> packets;

        if (!m_ctx)
            return packets;

        avcodec_send_frame(m_ctx, nullptr);

        while (true) {
            int ret = avcodec_receive_packet(m_ctx, m_pkt);

            if (ret == AVERROR(EAGAIN))
                break;

            if (ret == AVERROR_EOF)
                break;

            if (ret < 0)
                break;

            packets.emplace_back(
                m_pkt->data,
                m_pkt->data + m_pkt->size
            );

            av_packet_unref(m_pkt);
        }

        return packets;
    }

    // ------------------------------------------------------------------------
    // 销毁
    // ------------------------------------------------------------------------
    void H264Encoder::Destroy() {
        if (m_pkt) {
            av_packet_free(&m_pkt);
            m_pkt = nullptr;
        }

        if (m_frame) {
            av_frame_free(&m_frame);
            m_frame = nullptr;
        }

        if (m_sws) {
            sws_freeContext(m_sws);
            m_sws = nullptr;
        }

        if (m_ctx) {
            avcodec_free_context(&m_ctx);
            m_ctx = nullptr;
        }

        m_src_width = 0;
        m_src_height = 0;
        m_width = 0;
        m_height = 0;
        m_pts = 0;
    }

}