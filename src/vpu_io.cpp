#include "vpu_io.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <cstring>

// ============================================================
// VPUDecoderImpl
// ============================================================
struct VPUDecoderImpl {
    AVFormatContext   *fmtCtx    = nullptr;
    AVCodecContext    *codecCtx  = nullptr;
    AVFrame           *frame     = nullptr;
    AVFrame           *swFrame   = nullptr;
    AVFrame           *bgrFrame  = nullptr;
    SwsContext        *swsCtx    = nullptr;
    AVPacket          *packet    = nullptr;
    int                videoStreamIdx = -1;
    double             fps       = 30.0;
    int                width     = 0;
    int                height    = 0;
    int                frameCount = 0;
    bool               opened    = false;
    bool               isHW      = false;
    int                outputPixFmt = AV_PIX_FMT_YUV420P;
};

VPUDecoder::VPUDecoder()  { impl = new VPUDecoderImpl; }
VPUDecoder::~VPUDecoder() { release(); delete impl; }

static const char *pixFmtName(int fmt) {
    const char *name = av_get_pix_fmt_name((AVPixelFormat)fmt);
    return name ? name : "unknown";
}

bool VPUDecoder::open(const std::string &path) {
    release();

    impl->fmtCtx = avformat_alloc_context();
    if (avformat_open_input(&impl->fmtCtx, path.c_str(), nullptr, nullptr) < 0) {
        std::fprintf(stderr, "[VPUDecoder] Cannot open: %s\n", path.c_str());
        return false;
    }
    if (avformat_find_stream_info(impl->fmtCtx, nullptr) < 0) {
        std::fprintf(stderr, "[VPUDecoder] Cannot find stream info\n");
        return false;
    }

    for (unsigned i = 0; i < impl->fmtCtx->nb_streams; ++i) {
        if (impl->fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            impl->videoStreamIdx = i;
            break;
        }
    }
    if (impl->videoStreamIdx < 0) {
        std::fprintf(stderr, "[VPUDecoder] No video stream found\n");
        return false;
    }

    AVStream *stream = impl->fmtCtx->streams[impl->videoStreamIdx];
    AVCodecParameters *par = stream->codecpar;

    impl->width  = par->width;
    impl->height = par->height;
    impl->fps    = av_q2d(stream->avg_frame_rate);
    if (impl->fps <= 0) impl->fps = 30.0;
    impl->frameCount = static_cast<int>(stream->nb_frames);
    if (impl->frameCount <= 0)
        impl->frameCount = static_cast<int>(impl->fmtCtx->duration * impl->fps / AV_TIME_BASE);

    // NOTE: h264_rkmpp 在 FFmpeg 4.4 上存在 bug (avcodec_open2 成功但不产出帧)
    // 使用 SW 解码器 (ARM NEON 优化, 多线程), 实测 448fps @ 1080p
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        std::fprintf(stderr, "[VPUDecoder] No decoder found\n");
        return false;
    }
    impl->codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(impl->codecCtx, par);
    impl->codecCtx->thread_count = 0;  // auto-detect, 多线程
    if (avcodec_open2(impl->codecCtx, codec, nullptr) < 0) {
        std::fprintf(stderr, "[VPUDecoder] Cannot open decoder\n");
        return false;
    }
    impl->outputPixFmt = impl->codecCtx->pix_fmt;
    std::fprintf(stdout, "[VPUDecoder] Decoder: %s, fmt=%s, threads=%d\n",
                 codec->name, pixFmtName(impl->outputPixFmt),
                 impl->codecCtx->thread_count);

    impl->frame    = av_frame_alloc();
    impl->bgrFrame = av_frame_alloc();
    impl->packet   = av_packet_alloc();

    // 处理 HW 帧格式 (DRM_PRIME → NV12 transfer)
    if (impl->outputPixFmt == AV_PIX_FMT_DRM_PRIME) {
        impl->swFrame = av_frame_alloc();
        impl->swFrame->format = AV_PIX_FMT_NV12;
        impl->swFrame->width  = impl->width;
        impl->swFrame->height = impl->height;
        av_frame_get_buffer(impl->swFrame, 32);
        impl->outputPixFmt = AV_PIX_FMT_NV12;
    }

    impl->bgrFrame->format = AV_PIX_FMT_BGR24;
    impl->bgrFrame->width  = impl->width;
    impl->bgrFrame->height = impl->height;
    av_frame_get_buffer(impl->bgrFrame, 32);

    impl->swsCtx = sws_getContext(
        impl->width, impl->height, (AVPixelFormat)impl->outputPixFmt,
        impl->width, impl->height, AV_PIX_FMT_BGR24,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    if (!impl->swsCtx) {
        std::fprintf(stderr, "[VPUDecoder] sws_getContext failed for fmt=%s\n",
                     pixFmtName(impl->outputPixFmt));
        return false;
    }

    impl->opened = true;
    return true;
}

bool VPUDecoder::read(cv::Mat &frame) {
    if (!impl->opened) return false;

    while (true) {
        int ret = av_read_frame(impl->fmtCtx, impl->packet);
        if (ret < 0) return false;

        if (impl->packet->stream_index != impl->videoStreamIdx) {
            av_packet_unref(impl->packet);
            continue;
        }

        ret = avcodec_send_packet(impl->codecCtx, impl->packet);
        av_packet_unref(impl->packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) continue;

        ret = avcodec_receive_frame(impl->codecCtx, impl->frame);
        if (ret == AVERROR(EAGAIN)) continue;
        if (ret < 0) return false;

        AVFrame *srcFrame = impl->frame;

        if (impl->swFrame) {
            ret = av_hwframe_transfer_data(impl->swFrame, impl->frame, 0);
            if (ret < 0) {
                av_frame_unref(impl->frame);
                continue;
            }
            srcFrame = impl->swFrame;
        }

        sws_scale(impl->swsCtx,
                  srcFrame->data, srcFrame->linesize, 0, impl->height,
                  impl->bgrFrame->data, impl->bgrFrame->linesize);

        frame = cv::Mat(impl->height, impl->width, CV_8UC3,
                        impl->bgrFrame->data[0],
                        impl->bgrFrame->linesize[0]).clone();

        av_frame_unref(impl->frame);
        return true;
    }
}

double VPUDecoder::getFPS()        const { return impl->fps; }
int    VPUDecoder::getWidth()      const { return impl->width; }
int    VPUDecoder::getHeight()     const { return impl->height; }
int    VPUDecoder::getFrameCount() const { return impl->frameCount; }
bool   VPUDecoder::isOpened()      const { return impl->opened; }

void VPUDecoder::release() {
    if (impl->swsCtx)   { sws_freeContext(impl->swsCtx); impl->swsCtx = nullptr; }
    if (impl->bgrFrame) { av_frame_free(&impl->bgrFrame); }
    if (impl->swFrame)  { av_frame_free(&impl->swFrame); }
    if (impl->frame)    { av_frame_free(&impl->frame); }
    if (impl->packet)   { av_packet_free(&impl->packet); }
    if (impl->codecCtx) { avcodec_free_context(&impl->codecCtx); }
    if (impl->fmtCtx)   { avformat_close_input(&impl->fmtCtx); }
    impl->opened = false;
}

// ============================================================
// VPUEncoderImpl
// ============================================================
struct VPUEncoderImpl {
    AVFormatContext *fmtCtx   = nullptr;
    AVCodecContext  *codecCtx = nullptr;
    AVFrame         *yuvFrame = nullptr;
    SwsContext      *swsCtx   = nullptr;
    AVPacket        *packet   = nullptr;
    AVStream        *stream   = nullptr;
    int              width    = 0;
    int              height   = 0;
    int64_t          pts      = 0;
    bool             opened   = false;
};

VPUEncoder::VPUEncoder()  { impl = new VPUEncoderImpl; }
VPUEncoder::~VPUEncoder() { release(); delete impl; }

bool VPUEncoder::open(const std::string &path, int w, int h, double fps) {
    release();
    impl->width  = w;
    impl->height = h;
    impl->pts    = 0;

    int ret = avformat_alloc_output_context2(&impl->fmtCtx, nullptr, "mp4", path.c_str());
    if (ret < 0 || !impl->fmtCtx) {
        std::fprintf(stderr, "[VPUEncoder] Cannot create output: %s\n", path.c_str());
        return false;
    }

    // 编码器: 优先 HW (rkmpp / v4l2m2m), 回退 libx264 SW
    const AVCodec *codec = avcodec_find_encoder_by_name("h264_rkmpp");
    if (!codec) codec = avcodec_find_encoder_by_name("h264_v4l2m2m");
    bool useHW = (codec != nullptr);

    if (!codec) {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        useHW = false;
    }

    if (!codec) {
        std::fprintf(stderr, "[VPUEncoder] No encoder found\n");
        return false;
    }

    std::fprintf(stdout, "[VPUEncoder] Trying: %s (HW=%s)\n", codec->name, useHW ? "yes":"no");

    impl->stream = avformat_new_stream(impl->fmtCtx, nullptr);
    impl->stream->time_base = (AVRational){1, static_cast<int>(fps * 1000)};
    impl->stream->avg_frame_rate = (AVRational){static_cast<int>(fps * 1000), 1000};

    impl->codecCtx = avcodec_alloc_context3(codec);
    impl->codecCtx->width     = w;
    impl->codecCtx->height    = h;
    impl->codecCtx->pix_fmt   = AV_PIX_FMT_YUV420P;
    impl->codecCtx->time_base = impl->stream->time_base;
    impl->codecCtx->framerate = impl->stream->avg_frame_rate;
    impl->codecCtx->bit_rate  = w * h * 4;
    impl->codecCtx->gop_size  = 30;
    impl->codecCtx->max_b_frames = 0;
    impl->codecCtx->thread_count = 0;

    if (impl->fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
        impl->codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(impl->codecCtx, codec, nullptr);
    if (ret < 0 && useHW) {
        std::fprintf(stderr, "[VPUEncoder] HW encoder failed, fallback to libx264\n");
        avcodec_free_context(&impl->codecCtx);
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) return false;
        impl->codecCtx = avcodec_alloc_context3(codec);
        impl->codecCtx->width     = w;
        impl->codecCtx->height    = h;
        impl->codecCtx->pix_fmt   = AV_PIX_FMT_YUV420P;
        impl->codecCtx->time_base = impl->stream->time_base;
        impl->codecCtx->framerate = impl->stream->avg_frame_rate;
        impl->codecCtx->bit_rate  = w * h * 4;
        impl->codecCtx->gop_size  = 30;
        impl->codecCtx->max_b_frames = 0;
        impl->codecCtx->thread_count = 0;
        if (avcodec_open2(impl->codecCtx, codec, nullptr) < 0) return false;
    } else if (ret < 0) {
        return false;
    }

    std::fprintf(stdout, "[VPUEncoder] Opened: %s\n", codec->name);

    avcodec_parameters_from_context(impl->stream->codecpar, impl->codecCtx);

    if (!(impl->fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&impl->fmtCtx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return false;
    }
    if (avformat_write_header(impl->fmtCtx, nullptr) < 0) return false;

    impl->yuvFrame = av_frame_alloc();
    impl->yuvFrame->format = AV_PIX_FMT_YUV420P;
    impl->yuvFrame->width  = w;
    impl->yuvFrame->height = h;
    av_frame_get_buffer(impl->yuvFrame, 32);

    impl->swsCtx = sws_getContext(w, h, AV_PIX_FMT_BGR24, w, h, AV_PIX_FMT_YUV420P,
                                  SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    impl->packet = av_packet_alloc();
    impl->opened = true;
    return true;
}

bool VPUEncoder::write(const cv::Mat &frame) {
    if (!impl->opened) return false;
    const uint8_t *srcData[1] = { frame.data };
    int srcLinesize[1] = { static_cast<int>(frame.step) };
    sws_scale(impl->swsCtx, srcData, srcLinesize, 0, impl->height,
              impl->yuvFrame->data, impl->yuvFrame->linesize);
    impl->yuvFrame->pts = impl->pts++;

    int ret = avcodec_send_frame(impl->codecCtx, impl->yuvFrame);
    if (ret < 0) return false;
    while (ret >= 0) {
        ret = avcodec_receive_packet(impl->codecCtx, impl->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;
        av_packet_rescale_ts(impl->packet, impl->codecCtx->time_base, impl->stream->time_base);
        impl->packet->stream_index = impl->stream->index;
        av_interleaved_write_frame(impl->fmtCtx, impl->packet);
        av_packet_unref(impl->packet);
    }
    return true;
}

void VPUEncoder::release() {
    if (!impl->opened) return;
    avcodec_send_frame(impl->codecCtx, nullptr);
    while (true) {
        int ret = avcodec_receive_packet(impl->codecCtx, impl->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret >= 0) {
            av_packet_rescale_ts(impl->packet, impl->codecCtx->time_base, impl->stream->time_base);
            impl->packet->stream_index = impl->stream->index;
            av_interleaved_write_frame(impl->fmtCtx, impl->packet);
            av_packet_unref(impl->packet);
        }
    }
    av_write_trailer(impl->fmtCtx);
    if (impl->swsCtx)   { sws_freeContext(impl->swsCtx); impl->swsCtx = nullptr; }
    if (impl->yuvFrame) { av_frame_free(&impl->yuvFrame); }
    if (impl->packet)   { av_packet_free(&impl->packet); }
    if (impl->codecCtx) { avcodec_free_context(&impl->codecCtx); }
    if (impl->fmtCtx) {
        if (!(impl->fmtCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&impl->fmtCtx->pb);
        avformat_free_context(impl->fmtCtx);
        impl->fmtCtx = nullptr;
    }
    impl->opened = false;
}

bool VPUEncoder::isOpened() const { return impl->opened; }
