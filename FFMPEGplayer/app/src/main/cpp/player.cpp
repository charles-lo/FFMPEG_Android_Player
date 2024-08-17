#include <jni.h>
#include <string>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>


// FFMPEG headers
extern "C" {
#include "libavutil/frame.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libavutil/avutil.h"
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_charles_ffmpegplayer_FFMpegPlayer_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++ 003";
    return env->NewStringUTF(hello.c_str());
}


// Android 打印 Log
#define LOGE(FORMAT,...) __android_log_print(ANDROID_LOG_ERROR, "player", FORMAT, ##__VA_ARGS__);
/**
 * play video stream
 * R# rqquest release or close memory
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_charles_ffmpegplayer_FFMpegPlayer_playVideo(JNIEnv *env, jobject instance, jstring path_, jobject surface) {
    // save the result
    int result;
    // R1 Java String -> C String
    const char *path = env->GetStringUTFChars(path_, 0);


    // regiister FFmpeg component
    // av_register_all();  // not necessary after version 4.0
    avformat_network_init();
    // R2 initialize AVFormatContext
    AVFormatContext *format_context = avformat_alloc_context();
    // open video file
    result = avformat_open_input(&format_context, path, nullptr, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not open video file");
        return;
    }
    // look up video file information
    result = avformat_find_stream_info(format_context, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not find video file stream info");
        return;
    }
    // look up video codec
    int video_stream_index = -1;
    for (int i = 0; i < format_context->nb_streams; i++) {
        // match video stream
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
        }
    }
    // video stream  is not found
    if (video_stream_index == -1) {
        LOGE("Player Error : Can not find video stream");
        return;
    }
    // initialize video codec context
    AVCodecContext *video_codec_context = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(video_codec_context, format_context->streams[video_stream_index]->codecpar);
    // initialize video codec
    const AVCodec *video_codec = avcodec_find_decoder(video_codec_context->codec_id);
    if (video_codec == nullptr) {
        LOGE("Player Error : Can not find video codec");
        return;
    }
    // R3 open video codec
    result  = avcodec_open2(video_codec_context, video_codec, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not find video stream");
        return;
    }
    // acquire width and height of video
    int videoWidth = video_codec_context->width;
    int videoHeight = video_codec_context->height;
    // R4   initialize Native Window for video playing
    ANativeWindow *native_window = ANativeWindow_fromSurface(env, surface);
    if (native_window == nullptr) {
        LOGE("Player Error : Can not create native window");
        return;
    }
    // limit the number of buffer by setting width and height, instead of physical dimensions of screen
    // if the sizes between buffer and physical screen are different, it might be stretch or shrink image
    result = ANativeWindow_setBuffersGeometry(native_window, videoWidth, videoHeight,WINDOW_FORMAT_RGBA_8888);
    if (result < 0){
        LOGE("Player Error : Can not set native window buffer");
        ANativeWindow_release(native_window);
        return;
    }
    // define drawing buffer
    ANativeWindow_Buffer window_buffer;
    // declare 3 data container
    // R5 before decoding, it is Packet encode data in the data container
    AVPacket *packet = av_packet_alloc();
    // R6 after decoding, it is Frame pixel data in the data container,  the data cannot be used directly, need to be transformed first
    AVFrame *frame = av_frame_alloc();
    // R7 After data transform, the data in the container can be used.
    AVFrame *rgba_frame = av_frame_alloc();
    // data format transform preparation
    // output Buffer
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);
    // R8 request Buffer memory
    auto *out_buffer = (uint8_t *) av_malloc(buffer_size * sizeof(uint8_t));
    av_image_fill_arrays(rgba_frame->data, rgba_frame->linesize, out_buffer, AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);
    // R9 Data format context transform
    struct SwsContext *data_convert_context = sws_getContext(
            videoWidth, videoHeight, video_codec_context->pix_fmt,
            videoWidth, videoHeight, AV_PIX_FMT_RGBA,
            SWS_BICUBIC, nullptr, nullptr, nullptr);
    // start to read frame
    while (av_read_frame(format_context, packet) >= 0) {
        // match video stream
        if (packet->stream_index == video_stream_index) {
            // decode
            result = avcodec_send_packet(video_codec_context, packet);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
                LOGE("Player Error : codec step 1 fail");
                return;
            }
            result = avcodec_receive_frame(video_codec_context, frame);
            if (result < 0 && result != AVERROR_EOF) {
                LOGE("Player Error : codec step 2 fail");
                return;
            }
            // data format transform
            result = sws_scale(
                    data_convert_context,
                    (const uint8_t* const*) frame->data, frame->linesize,
                    0, videoHeight,
                    rgba_frame->data, rgba_frame->linesize);
            if (result <= 0) {
                LOGE("Player Error : data convert fail");
                return;
            }
            // play
            result = ANativeWindow_lock(native_window, &window_buffer, nullptr);
            if (result < 0) {
                LOGE("Player Error : Can not lock native window");
            } else {
                // render the image to the GUI
                // Tip: the single line pixel size of rgba_frame might be different from the counterpart of window_buffer
                // It needs to be transformed appropriately or it might become snow screen
                auto *bits = (uint8_t *) window_buffer.bits;
                for (int h = 0; h < videoHeight; h++) {
                    memcpy(bits + h * window_buffer.stride * 4,
                           out_buffer + h * rgba_frame->linesize[0],
                           rgba_frame->linesize[0]);
                }
                ANativeWindow_unlockAndPost(native_window);
            }
        }
        // release packet reference
        av_packet_unref(packet);
    }
    // release R9
    sws_freeContext(data_convert_context);
    // release R8
    av_free(out_buffer);
    // release R7
    av_frame_free(&rgba_frame);
    // release R6
    av_frame_free(&frame);
    // release R5
    av_packet_free(&packet);
    // release R4
    ANativeWindow_release(native_window);
    // release R3
    avcodec_free_context(&video_codec_context);
    // release R2
    avformat_close_input(&format_context);
    // release R1
    env->ReleaseStringUTFChars(path_, path);
}