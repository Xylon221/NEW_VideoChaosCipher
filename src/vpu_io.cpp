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
#include <string>
#include <vector>

struct VPUDecoderImpl {
    AVFormatContext *fmtCtx = nullptr;
    AVCodecContext  *codecCtx = nullptr;
    AVFrame         *frame = nullptr;
    AVFrame         *swFrame = nullptr;
    AVFrame         *bgrFrame = nullptr;
    SwsContext      *swsCtx = nullptr;
    AVPacket        *packet = nullptr;

    int videoStreamIdx = -1;
    double fps = 30.0;
    int width = 0;
    int height = 0;
    int frameCount = 0;
    bool opened = false;
    bool isHW = false;
    bool needsHwTransfer = false;
    bool decoderDraining = false;
    int outputPixFmt = AV_PIX_FMT_YUV420P;
    std::string codecName;
};

struct VPUEncoderImpl {
    AVFormatContext *fmtCtx = nullptr;
    AVCodecContext  *codecCtx = nullptr;
    AVFrame         *yuvFrame = nullptr;
    SwsContext      *swsCtx = nullptr;
    AVPacket        *packet = nullptr;
    AVStream        *stream = nullptr;

    int width = 0;
    int height = 0;
    int64_t pts = 0;
    bool opened = false;
    bool isHW = false;
    std::string codecName;
};

static const char *pixFmtName(int fmt) {
    const char *name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(fmt));
    return name ? name : "unknown";
}

static std::string avErr2Str(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}

static bool isHardwareCodecName(const char *name) {
    if (!name) return false;
    std::string n(name);
    return n.find("rkmpp") != std::string::npos ||
           n.find("v4l2m2m") != std::string::npos;
}

static std::vector<const char *> decoderCandidates(AVCodecID codecId) {
    switch (codecId) {
    case AV_CODEC_ID_H264:
        return {"h264_rkmpp", "h264_v4l2m2m"};
    case AV_CODEC_ID_HEVC:
        return {"hevc_rkmpp", "hevc_v4l2m2m"};
    case AV_CODEC_ID_MPEG4:
        return {"mpeg4_v4l2m2m"};
    case AV_CODEC_ID_VP8:
        return {"vp8_rkmpp", "vp8_v4l2m2m"};
    case AV_CODEC_ID_VP9:
        return {"vp9_rkmpp", "vp9_v4l2m2m"};
    default:
        return {};
    }
}

static bool copyDecoderParams(VPUDecoderImpl *impl,
                              const AVCodec *codec,
                              AVCodecParameters *par) {
    impl->codecCtx = avcodec_alloc_context3(codec);
    if (!impl->codecCtx) return false;

    int ret = avcodec_parameters_to_context(impl->codecCtx, par);
    if (ret < 0) {
        std::fprintf(stderr, "[VPUDecoder] parameters_to_context failed for %s: %s\n",
                     codec->name, avErr2Str(ret).c_str());
        avcodec_free_context(&impl->codecCtx);
        return false;
    }

    impl->codecCtx->thread_count = 0;
    ret = avcodec_open2(impl->codecCtx, codec, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "[VPUDecoder] Cannot open decoder %s: %s\n",
                     codec->name, avErr2Str(ret).c_str());
        avcodec_free_context(&impl->codecCtx);
        return false;
    }

    impl->codecName = codec->name ? codec->name : "unknown";
    impl->isHW = isHardwareCodecName(codec->name);
    return true;
}

static bool configureEncoderContext(AVCodecContext *ctx,
                                    AVStream *stream,
                                    int w,
                                    int h,
                                    double fps,
                                    AVFormatContext *fmtCtx) {
    ctx->width = w;
    ctx->height = h;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    ctx->time_base = stream->time_base;
    ctx->framerate = stream->avg_frame_rate;
    ctx->bit_rate = w * h * 4;
    ctx->gop_size = 30;
    ctx->max_b_frames = 0;
    ctx->thread_count = 0;
    (void)fps;

    if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    return true;
}

VPUDecoder::VPUDecoder() { impl = new VPUDecoderImpl; }
VPUDecoder::~VPUDecoder() { release(); delete impl; }

bool VPUDecoder::open(const std::string &path) {
    release();

    impl->fmtCtx = avformat_alloc_context();
    AVInputFormat *inputFmt = nullptr;
    AVDictionary *inputOpts = nullptr;
    if (path.find("/dev/video") == 0) {
        inputFmt = av_find_input_format("v4l2");
        av_dict_set(&inputOpts, "framerate", "30", 0);
        std::fprintf(stdout, "[VPUDecoder] Opening camera via v4l2: %s\n", path.c_str());
    }
    int ret = avformat_open_input(&impl->fmtCtx, path.c_str(), inputFmt, &inputOpts);
    av_dict_free(&inputOpts);
    if (ret < 0) {
        std::fprintf(stderr, "[VPUDecoder] Cannot open %s: %s\n",
                     path.c_str(), avErr2Str(ret).c_str());
        release();
        return false;
    }

    ret = avformat_find_stream_info(impl->fmtCtx, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "[VPUDecoder] Cannot find stream info: %s\n",
                     avErr2Str(ret).c_str());
        release();
        return false;
    }

    for (unsigned i = 0; i < impl->fmtCtx->nb_streams; ++i) {
        if (impl->fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            impl->videoStreamIdx = static_cast<int>(i);
            break;
        }
    }
    if (impl->videoStreamIdx < 0) {
        std::fprintf(stderr, "[VPUDecoder] No video stream found\n");
        release();
        return false;
    }

    AVStream *stream = impl->fmtCtx->streams[impl->videoStreamIdx];
    AVCodecParameters *par = stream->codecpar;
    impl->width = par->width;
    impl->height = par->height;
    impl->fps = av_q2d(stream->avg_frame_rate);
    if (impl->fps <= 0) impl->fps = 30.0;
    impl->frameCount = static_cast<int>(stream->nb_frames);
    if (impl->frameCount <= 0 && impl->fmtCtx->duration > 0) {
        impl->frameCount = static_cast<int>(impl->fmtCtx->duration * impl->fps / AV_TIME_BASE);
    }

    bool openedDecoder = false;
    for (const char *name : decoderCandidates(par->codec_id)) {
        const AVCodec *candidate = avcodec_find_decoder_by_name(name);
        if (!candidate) {
            std::fprintf(stdout, "[VPUDecoder] Candidate not found: %s\n", name);
            continue;
        }
        std::fprintf(stdout, "[VPUDecoder] Trying: %s (HW=yes)\n", name);
        if (copyDecoderParams(impl, candidate, par)) {
            openedDecoder = true;
            break;
        }
    }

    if (!openedDecoder) {
        const AVCodec *codec = avcodec_find_decoder(par->codec_id);
        if (!codec) {
            std::fprintf(stderr, "[VPUDecoder] No decoder found\n");
            release();
            return false;
        }
        std::fprintf(stdout, "[VPUDecoder] Trying: %s (HW=no)\n", codec->name);
        openedDecoder = copyDecoderParams(impl, codec, par);
    }
    if (!openedDecoder) {
        release();
        return false;
    }

    impl->outputPixFmt = impl->codecCtx->pix_fmt;
    impl->needsHwTransfer = (impl->outputPixFmt == AV_PIX_FMT_DRM_PRIME);
    if (impl->needsHwTransfer) {
        impl->swFrame = av_frame_alloc();
        impl->swFrame->format = AV_PIX_FMT_NV12;
        impl->swFrame->width = impl->width;
        impl->swFrame->height = impl->height;
        impl->outputPixFmt = AV_PIX_FMT_NV12;
    }

    impl->frame = av_frame_alloc();
    impl->bgrFrame = av_frame_alloc();
    impl->packet = av_packet_alloc();
    if (!impl->frame || !impl->bgrFrame || !impl->packet) {
        std::fprintf(stderr, "[VPUDecoder] Cannot allocate frame buffers\n");
        release();
        return false;
    }

    impl->bgrFrame->format = AV_PIX_FMT_BGR24;
    impl->bgrFrame->width = impl->width;
    impl->bgrFrame->height = impl->height;
    ret = av_frame_get_buffer(impl->bgrFrame, 32);
    if (ret < 0) {
        std::fprintf(stderr, "[VPUDecoder] Cannot allocate BGR frame: %s\n",
                     avErr2Str(ret).c_str());
        release();
        return false;
    }

    impl->swsCtx = sws_getContext(
        impl->width, impl->height, static_cast<AVPixelFormat>(impl->outputPixFmt),
        impl->width, impl->height, AV_PIX_FMT_BGR24,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!impl->swsCtx) {
        std::fprintf(stderr, "[VPUDecoder] sws_getContext failed for fmt=%s\n",
                     pixFmtName(impl->outputPixFmt));
        release();
        return false;
    }

    impl->opened = true;
    std::fprintf(stdout, "[VPUDecoder] Opened: %s (HW=%s, fmt=%s, threads=%d)\n",
                 impl->codecName.c_str(), impl->isHW ? "yes" : "no",
                 pixFmtName(impl->outputPixFmt), impl->codecCtx->thread_count);
    return true;
}

bool VPUDecoder::read(cv::Mat &frame) {
    if (!impl->opened) return false;

    while (true) {
        if (!impl->decoderDraining) {
            int ret = av_read_frame(impl->fmtCtx, impl->packet);
            if (ret >= 0) {
                if (impl->packet->stream_index != impl->videoStreamIdx) {
                    av_packet_unref(impl->packet);
                    continue;
                }

                ret = avcodec_send_packet(impl->codecCtx, impl->packet);
                av_packet_unref(impl->packet);
                if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    std::fprintf(stderr, "[VPUDecoder] send_packet failed: %s\n", avErr2Str(ret).c_str());
                    continue;
                }
            } else {
                avcodec_send_packet(impl->codecCtx, nullptr);
                impl->decoderDraining = true;
            }
        }

        int ret = avcodec_receive_frame(impl->codecCtx, impl->frame);
        if (ret == AVERROR(EAGAIN)) {
            if (impl->decoderDraining) return false;
            continue;
        }
        if (ret == AVERROR_EOF) return false;
        if (ret < 0) return false;

        AVFrame *srcFrame = impl->frame;
        if (impl->needsHwTransfer) {
            av_frame_unref(impl->swFrame);
            impl->swFrame->format = AV_PIX_FMT_NV12;
            ret = av_hwframe_transfer_data(impl->swFrame, impl->frame, 0);
            if (ret < 0) {
                std::fprintf(stderr, "[VPUDecoder] HW frame transfer failed: %s\n",
                             avErr2Str(ret).c_str());
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

double VPUDecoder::getFPS() const { return impl->fps; }
int VPUDecoder::getWidth() const { return impl->width; }
int VPUDecoder::getHeight() const { return impl->height; }
int VPUDecoder::getFrameCount() const { return impl->frameCount; }
bool VPUDecoder::isOpened() const { return impl->opened; }
bool VPUDecoder::isHardwareAccelerated() const { return impl->isHW; }
std::string VPUDecoder::getCodecName() const { return impl->codecName; }

void VPUDecoder::release() {
    if (impl->swsCtx) { sws_freeContext(impl->swsCtx); impl->swsCtx = nullptr; }
    if (impl->bgrFrame) av_frame_free(&impl->bgrFrame);
    if (impl->swFrame) av_frame_free(&impl->swFrame);
    if (impl->frame) av_frame_free(&impl->frame);
    if (impl->packet) av_packet_free(&impl->packet);
    if (impl->codecCtx) avcodec_free_context(&impl->codecCtx);
    if (impl->fmtCtx) avformat_close_input(&impl->fmtCtx);
    impl->videoStreamIdx = -1;
    impl->opened = false;
    impl->isHW = false;
    impl->needsHwTransfer = false;
    impl->decoderDraining = false;
    impl->codecName.clear();
}

VPUEncoder::VPUEncoder() { impl = new VPUEncoderImpl; }
VPUEncoder::~VPUEncoder() { release(); delete impl; }

bool VPUEncoder::open(const std::string &path, int w, int h, double fps) {
    release();
    impl->width = w;
    impl->height = h;
    impl->pts = 0;

    int ret = avformat_alloc_output_context2(&impl->fmtCtx, nullptr, "mp4", path.c_str());
    if (ret < 0 || !impl->fmtCtx) {
        std::fprintf(stderr, "[VPUEncoder] Cannot create output %s: %s\n",
                     path.c_str(), avErr2Str(ret).c_str());
        release();
        return false;
    }

    impl->stream = avformat_new_stream(impl->fmtCtx, nullptr);
    if (!impl->stream) {
        std::fprintf(stderr, "[VPUEncoder] Cannot create output stream\n");
        release();
        return false;
    }
    int fpsTicks = static_cast<int>(fps * 1000 + 0.5);
    if (fpsTicks <= 0) fpsTicks = 30000;
    impl->stream->time_base = (AVRational){1000, fpsTicks};
    impl->stream->avg_frame_rate = (AVRational){fpsTicks, 1000};

    const AVCodec *codec = nullptr;
    for (const char *name : std::vector<const char *>{"h264_rkmpp", "h264_v4l2m2m"}) {
        codec = avcodec_find_encoder_by_name(name);
        if (codec) break;
        std::fprintf(stdout, "[VPUEncoder] Candidate not found: %s\n", name);
    }

    if (!codec) {
        codec = avcodec_find_encoder_by_name("libx264");
        if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!codec) {
        std::fprintf(stderr, "[VPUEncoder] No encoder found\n");
        release();
        return false;
    }

    bool requestedHW = isHardwareCodecName(codec->name);
    std::fprintf(stdout, "[VPUEncoder] Trying: %s (HW=%s)\n",
                 codec->name, requestedHW ? "yes" : "no");

    impl->codecCtx = avcodec_alloc_context3(codec);
    if (!impl->codecCtx) {
        release();
        return false;
    }
    configureEncoderContext(impl->codecCtx, impl->stream, w, h, fps, impl->fmtCtx);
    ret = avcodec_open2(impl->codecCtx, codec, nullptr);

    if (ret < 0 && requestedHW) {
        std::fprintf(stderr, "[VPUEncoder] HW encoder %s failed: %s; fallback to software H.264\n",
                     codec->name, avErr2Str(ret).c_str());
        avcodec_free_context(&impl->codecCtx);
        codec = avcodec_find_encoder_by_name("libx264");
        if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            release();
            return false;
        }
        impl->codecCtx = avcodec_alloc_context3(codec);
        configureEncoderContext(impl->codecCtx, impl->stream, w, h, fps, impl->fmtCtx);
        ret = avcodec_open2(impl->codecCtx, codec, nullptr);
    }
    if (ret < 0) {
        std::fprintf(stderr, "[VPUEncoder] Cannot open encoder %s: %s\n",
                     codec->name, avErr2Str(ret).c_str());
        release();
        return false;
    }

    impl->codecName = codec->name ? codec->name : "unknown";
    impl->isHW = isHardwareCodecName(codec->name);
    std::fprintf(stdout, "[VPUEncoder] Opened: %s (HW=%s)\n",
                 impl->codecName.c_str(), impl->isHW ? "yes" : "no");

    avcodec_parameters_from_context(impl->stream->codecpar, impl->codecCtx);

    if (!(impl->fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&impl->fmtCtx->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::fprintf(stderr, "[VPUEncoder] Cannot open output IO: %s\n",
                         avErr2Str(ret).c_str());
            release();
            return false;
        }
    }

    ret = avformat_write_header(impl->fmtCtx, nullptr);
    if (ret < 0) {
        std::fprintf(stderr, "[VPUEncoder] Cannot write header: %s\n", avErr2Str(ret).c_str());
        release();
        return false;
    }

    impl->yuvFrame = av_frame_alloc();
    impl->packet = av_packet_alloc();
    if (!impl->yuvFrame || !impl->packet) {
        release();
        return false;
    }
    impl->yuvFrame->format = AV_PIX_FMT_YUV420P;
    impl->yuvFrame->width = w;
    impl->yuvFrame->height = h;
    ret = av_frame_get_buffer(impl->yuvFrame, 32);
    if (ret < 0) {
        std::fprintf(stderr, "[VPUEncoder] Cannot allocate YUV frame: %s\n",
                     avErr2Str(ret).c_str());
        release();
        return false;
    }

    impl->swsCtx = sws_getContext(w, h, AV_PIX_FMT_BGR24,
                                  w, h, AV_PIX_FMT_YUV420P,
                                  SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!impl->swsCtx) {
        std::fprintf(stderr, "[VPUEncoder] sws_getContext failed\n");
        release();
        return false;
    }

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
    if (ret < 0) {
        std::fprintf(stderr, "[VPUEncoder] send_frame failed: %s\n", avErr2Str(ret).c_str());
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(impl->codecCtx, impl->packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            std::fprintf(stderr, "[VPUEncoder] receive_packet failed: %s\n", avErr2Str(ret).c_str());
            return false;
        }

        av_packet_rescale_ts(impl->packet, impl->codecCtx->time_base, impl->stream->time_base);
        impl->packet->stream_index = impl->stream->index;
        ret = av_interleaved_write_frame(impl->fmtCtx, impl->packet);
        av_packet_unref(impl->packet);
        if (ret < 0) {
            std::fprintf(stderr, "[VPUEncoder] write_frame failed: %s\n", avErr2Str(ret).c_str());
            return false;
        }
    }
    return true;
}

void VPUEncoder::release() {
    bool shouldFlush = impl->opened && impl->codecCtx && impl->packet && impl->fmtCtx;
    if (shouldFlush) {
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
    }

    if (impl->swsCtx) { sws_freeContext(impl->swsCtx); impl->swsCtx = nullptr; }
    if (impl->yuvFrame) av_frame_free(&impl->yuvFrame);
    if (impl->packet) av_packet_free(&impl->packet);
    if (impl->codecCtx) avcodec_free_context(&impl->codecCtx);
    if (impl->fmtCtx) {
        if (!(impl->fmtCtx->oformat->flags & AVFMT_NOFILE) && impl->fmtCtx->pb) {
            avio_closep(&impl->fmtCtx->pb);
        }
        avformat_free_context(impl->fmtCtx);
        impl->fmtCtx = nullptr;
    }

    impl->stream = nullptr;
    impl->opened = false;
    impl->isHW = false;
    impl->codecName.clear();
}

bool VPUEncoder::isOpened() const { return impl->opened; }
bool VPUEncoder::isHardwareAccelerated() const { return impl->isHW; }
std::string VPUEncoder::getCodecName() const { return impl->codecName; }
