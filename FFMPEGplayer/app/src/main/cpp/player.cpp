#include <jni.h>
#include <string>
#include <pthread.h>
#include <unistd.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include "ffmpegplayer/queue.h"

// FFMPEG headers
extern "C" {
#include "libavutil/frame.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/imgutils.h"
#include "libavutil/avutil.h"
}

#define LOGE(FORMAT,...) __android_log_print(ANDROID_LOG_ERROR, "player", FORMAT, ##__VA_ARGS__);

/**
 * @param filename
 * @param err
 */
void print_error(int err) {
    char err_buf[128];
    const char *err_buf_ptr = err_buf;
    if (av_strerror(err, err_buf, sizeof(err_buf_ptr)) < 0) {
        err_buf_ptr = strerror(AVUNERROR(err));
    }
    LOGE("ffmpeg error descript : %s", err_buf_ptr);
}
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
    // av_register_all();  // not necessary after FFMPEG version 4.0
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

/**
 * play audio stream
 * R# means request, release or close memory
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_charles_ffmpegplayer_FFMpegPlayer_playAudio(JNIEnv *env, jobject instance, jstring path_) {
    int result;
    // R1 Java String -> C String
    const char *path = env->GetStringUTFChars(path_, 0);
    // register component
    // av_register_all();  // not necessary after FFMPEG version 4.0
    avformat_network_init();
    // R2 create AVFormatContext
    AVFormatContext *format_context = avformat_alloc_context();
    // R3 open video
    avformat_open_input(&format_context, path, nullptr, nullptr);
    // look up video file stream
    result = avformat_find_stream_info(format_context, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not find video file stream info");
        return;
    }
    // look up audio codec
    int audio_stream_index = -1;
    for (int i = 0; i < format_context->nb_streams; i++) {
        // match audio stream
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
        }
    }
    // return error if no audio stream found
    if (audio_stream_index == -1) {
        LOGE("Player Error : Can not find audio stream");
        return;
    }
    // initialize audio codec context
    AVCodecContext *audio_codec_context = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(audio_codec_context, format_context->streams[audio_stream_index]->codecpar);
    // initialize codec
    const AVCodec *audio_codec = avcodec_find_decoder(audio_codec_context->codec_id);
    if (audio_codec == nullptr) {
        LOGE("Player Error : Can not find audio codec");
        return;
    }
    // R4 open avcodec
    result  = avcodec_open2(audio_codec_context, audio_codec, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not open audio codec");
        return;
    }
    // prepare to audio resampling
    // R5 resample context
    struct SwrContext *swr_context = swr_alloc();
    // buffer
    int out_nb_samples = audio_codec_context->frame_size;
    const int OUT_BUFFER_LEN = 2;
    auto *out_buffer = (uint8_t *) av_malloc(out_nb_samples  * OUT_BUFFER_LEN);
    // output channel layout (dual stereo)   AV_CHANNEL_LAYOUT_MONO AV_CHANNEL_LAYOUT_STEREO
    AVChannelLayout outChannelLayout = AV_CHANNEL_LAYOUT_MONO;
    // AV_SAMPLE_FMT_S16
    enum AVSampleFormat out_format = AV_SAMPLE_FMT_S16;
    // out_sample_rate should be the same with input
    int out_sample_rate = audio_codec_context->sample_rate;

    //swr_alloc_set_opts transform the PCM source sample format to that you want
    if (swr_alloc_set_opts2(&swr_context, &outChannelLayout, out_format, out_sample_rate,
                            &audio_codec_context->ch_layout, audio_codec_context->sample_fmt,
                            audio_codec_context->sample_rate, 0, nullptr)
        != 0) {
        LOGE("swr_alloc_set_opts2 fail.");
        return;
    }
    swr_init(swr_context);
    // call Java layer to create AudioTrack
    int out_channels = outChannelLayout.nb_channels;
    jclass player_class = env->GetObjectClass(instance);
    jmethodID create_audio_track_method_id = env->GetMethodID(player_class, "createAudioTrack", "(II)V");
    env->CallVoidMethod(instance, create_audio_track_method_id, 44100, out_channels);
    // prepare to play audio
    jmethodID play_audio_track_method_id = env->GetMethodID(player_class, "playAudioTrack", "([BI)V");
    //  declare 2 data container
    // R6 the data container before decode, Packet codec data
    AVPacket *packet = av_packet_alloc();
    // R7 after decoding data container, still cannot play MPC data, need to re-sample first.
    AVFrame *frame = av_frame_alloc();
    // begin to read frame
    while (av_read_frame(format_context, packet) >= 0) {
        // match audio stream
        if (packet->stream_index == audio_stream_index) {
            // decode
            result = avcodec_send_packet(audio_codec_context, packet);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
                LOGE("Player Error : codec step 1 fail");
                return;
            }
            result = avcodec_receive_frame(audio_codec_context, frame);
            if (result < 0 && result != AVERROR_EOF) {
                LOGE("Player Error : codec step 2 fail");
                return;
            }
            // re-sample
            swr_convert(swr_context, &out_buffer, out_nb_samples * OUT_BUFFER_LEN, (const uint8_t **) frame->data, frame->nb_samples);
            // play audio
            // call Java layer to play AudioTrack
            int size = av_samples_get_buffer_size(nullptr, out_channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
            jbyteArray audio_sample_array = env->NewByteArray(size);
            env->SetByteArrayRegion(audio_sample_array, 0, size, (const jbyte *) out_buffer);
            env->CallVoidMethod(instance, play_audio_track_method_id, audio_sample_array, size);
            env->DeleteLocalRef(audio_sample_array);
        }
        // release packet reference
        av_packet_unref(packet);
    }
    // call Java layer to release AudioTrack
    jmethodID release_audio_track_method_id = env->GetMethodID(player_class, "releaseAudioTrack", "()V");
    env->CallVoidMethod(instance, release_audio_track_method_id);
    // release R7
    av_frame_free(&frame);
    // release R6
    av_packet_free(&packet);
    // release R5
    swr_free(&swr_context);
    // close R4
    avcodec_free_context(&audio_codec_context);
    // close R3
    avformat_close_input(&format_context);
    // release R2
    avformat_free_context(format_context);
    // release R1
    env->ReleaseStringUTFChars(path_, path);
}

// status code
#define SUCCESS_CODE 1
#define FAIL_CODE -1
const int OUT_BUFFER_LEN = 8;

// player struct in C layer
typedef struct _Player {
    // Env
    JavaVM *java_vm;
    // Java instance
    jobject instance;
    jobject surface;
    jobject callback;
    // Context
    AVFormatContext *format_context;
    // video component
    int video_stream_index;
    AVCodecContext *video_codec_context;
    ANativeWindow *native_window;
    ANativeWindow_Buffer window_buffer;
    uint8_t *video_out_buffer;
    struct SwsContext *sws_context;
    AVFrame *rgba_frame;
    Queue *video_queue;
    // audio component
    int audio_stream_index;
    AVCodecContext *audio_codec_context;
    uint8_t *audio_out_buffer;
    struct SwrContext *swr_context;
    int out_channels;
    jmethodID play_audio_track_method_id;
    Queue *audio_queue;
    double audio_clock;
    //
    int out_nb_samples;
} Player;

// _Consumer
typedef struct _Consumer {
    Player* player;
    int stream_index;
} Consumer;

// player
Player *cplayer;

// thread related
pthread_t produce_id, video_consume_id, audio_consume_id;
// fast forward/ back forward
bool is_seek;
pthread_mutex_t seek_mutex;
pthread_cond_t seek_condition;

/**
 * @param player
 */
void player_init(Player **player, JNIEnv *env, jobject instance, jobject surface, jobject callback) {
    *player = (Player*) malloc(sizeof(Player));
    JavaVM* java_vm;
    env->GetJavaVM(&java_vm);
    (*player)->java_vm = java_vm;
    (*player)->instance = env->NewGlobalRef(instance);
    (*player)->surface = env->NewGlobalRef(surface);
    (*player)->callback = env->NewGlobalRef(callback);
}

/**
 * @return
 */
int format_init(Player *player, const char* path) {
    int result;
    //av_register_all();
    avformat_network_init();
    player->format_context = avformat_alloc_context();
    result = avformat_open_input(&(player->format_context), path, nullptr, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not open video file");
        return result;
    }
    result = avformat_find_stream_info(player->format_context, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not find video file stream info");
        return result;
    }
    return SUCCESS_CODE;
}

/**
 * @param player
 * @param type
 * @return
 */
int find_stream_index(Player *player, AVMediaType type) {
    AVFormatContext* format_context = player->format_context;
    for (int i = 0; i < format_context->nb_streams; i++) {
        if (format_context->streams[i]->codecpar->codec_type == type) {
            return i;
        }
    }
    return -1;
}

/**
 * @param player
 * @param type
 * @return
 */
int codec_init(Player *player, AVMediaType type) {
    int result;
    AVFormatContext *format_context = player->format_context;
    int index = find_stream_index(player, type);
    if (index == -1) {
        LOGE("Player Error : Can not find stream");
        return FAIL_CODE;
    }
    AVCodecContext *codec_context = avcodec_alloc_context3(nullptr);
    avcodec_parameters_to_context(codec_context, format_context->streams[index]->codecpar);
    const AVCodec *codec = avcodec_find_decoder(codec_context->codec_id);
    result = avcodec_open2(codec_context, codec, nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not open codec");
        return FAIL_CODE;
    }
    if (type == AVMEDIA_TYPE_VIDEO) {
        player->video_stream_index = index;
        player->video_codec_context = codec_context;
    } else if (type == AVMEDIA_TYPE_AUDIO) {
        player->audio_stream_index = index;
        player->audio_codec_context = codec_context;
    }
    return SUCCESS_CODE;
}

/**
 * @param player
 */
int video_prepare(Player *player, JNIEnv *env) {
    AVCodecContext *codec_context = player->video_codec_context;
    int videoWidth = codec_context->width;
    int videoHeight = codec_context->height;
    player->native_window = ANativeWindow_fromSurface(env, player->surface);
    if (player->native_window == nullptr) {
        LOGE("Player Error : Can not create native window");
        return FAIL_CODE;
    }
    int result = ANativeWindow_setBuffersGeometry(player->native_window, videoWidth, videoHeight,WINDOW_FORMAT_RGBA_8888);
    if (result < 0){
        LOGE("Player Error : Can not set native window buffer");
        ANativeWindow_release(player->native_window);
        return FAIL_CODE;
    }
    player->rgba_frame = av_frame_alloc();
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);
    player->video_out_buffer = (uint8_t *) av_malloc(buffer_size * sizeof(uint8_t));
    av_image_fill_arrays(player->rgba_frame->data, player->rgba_frame->linesize, player->video_out_buffer, AV_PIX_FMT_RGBA, videoWidth, videoHeight, 1);
    player->sws_context = sws_getContext(
            videoWidth, videoHeight, codec_context->pix_fmt,
            videoWidth, videoHeight, AV_PIX_FMT_RGBA,
            SWS_BICUBIC, nullptr, nullptr, nullptr);
    return SUCCESS_CODE;
}

/**
 * @param player
 * @return
 */
int audio_prepare(Player *player, JNIEnv* env) {
    AVCodecContext *codec_context = player->audio_codec_context;
    player->swr_context = swr_alloc();
    player->out_nb_samples = codec_context->frame_size;
    player->audio_out_buffer = (uint8_t *) av_malloc(player->out_nb_samples * OUT_BUFFER_LEN);
    enum AVSampleFormat out_format = AV_SAMPLE_FMT_S16;
    // output channel layout (dual stereo)   AV_CHANNEL_LAYOUT_MONO AV_CHANNEL_LAYOUT_STEREO
    AVChannelLayout outChannelLayout = AV_CHANNEL_LAYOUT_MONO;
    //swr_alloc_set_opts transform the PCM source sample format to that you want
    if (swr_alloc_set_opts2(&player->swr_context, &outChannelLayout, out_format, player->audio_codec_context->sample_rate,
                            &codec_context->ch_layout, codec_context->sample_fmt,
                            codec_context->sample_rate, 0, nullptr)
        != 0) {
        LOGE("swr_alloc_set_opts2 fail.");
        return FAIL_CODE;
    }
    swr_init(player->swr_context);
    player->out_channels = outChannelLayout.nb_channels;
    jclass player_class = env->GetObjectClass(player->instance);
    jmethodID create_audio_track_method_id = env->GetMethodID(player_class, "createAudioTrack", "(II)V");
    env->CallVoidMethod(player->instance, create_audio_track_method_id, 44100, player->out_channels);
    player->play_audio_track_method_id = env->GetMethodID(player_class, "playAudioTrack", "([BI)V");
    return SUCCESS_CODE;
}

/**
 * @param frame
 */
void video_play(Player* player, AVFrame *frame, JNIEnv *env) {
    int video_height = player->video_codec_context->height;
    int result = sws_scale(
            player->sws_context,
            (const uint8_t* const*) frame->data, frame->linesize,
            0, video_height,
            player->rgba_frame->data, player->rgba_frame->linesize);
    if (result <= 0) {
        LOGE("Player Error : video data convert fail");
        return;
    }
    result = ANativeWindow_lock(player->native_window, &(player->window_buffer), nullptr);
    if (result < 0) {
        LOGE("Player Error : Can not lock native window");
    } else {
        auto *bits = (uint8_t *) player->window_buffer.bits;
        for (int h = 0; h < video_height; h++) {
            memcpy(bits + h * player->window_buffer.stride * 4,
                   player->video_out_buffer + h * player->rgba_frame->linesize[0],
                   player->rgba_frame->linesize[0]);
        }
        ANativeWindow_unlockAndPost(player->native_window);
    }
}

/**
 * @param frame
 */
void audio_play(Player* player, AVFrame *frame, JNIEnv *env) {
    swr_convert(player->swr_context, &(player->audio_out_buffer), player->out_nb_samples * OUT_BUFFER_LEN, (const uint8_t **) frame->data, frame->nb_samples);
    int size = av_samples_get_buffer_size(nullptr, player->out_channels, frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
    jbyteArray audio_sample_array = env->NewByteArray(size);
    env->SetByteArrayRegion(audio_sample_array, 0, size, (const jbyte *) player->audio_out_buffer);
    env->CallVoidMethod(player->instance, player->play_audio_track_method_id, audio_sample_array, size);
    env->DeleteLocalRef(audio_sample_array);
}

/**
 * @param player
 */
void player_release(Player* player) {
    avformat_close_input(&(player->format_context));
    av_free(player->video_out_buffer);
    av_free(player->audio_out_buffer);
    avcodec_free_context(&player->video_codec_context);
    ANativeWindow_release(player->native_window);
    sws_freeContext(player->sws_context);
    av_frame_free(&(player->rgba_frame));
    avcodec_free_context(&player->audio_codec_context);
    swr_free(&(player->swr_context));
    queue_destroy(player->video_queue);
    queue_destroy(player->audio_queue);
    player->instance = nullptr;
    JNIEnv *env;
    int result = player->java_vm->AttachCurrentThread(&env, nullptr);
    if (result != JNI_OK) {
        return;
    }
    env->DeleteGlobalRef(player->instance);
    env->DeleteGlobalRef(player->surface);
    player->java_vm->DetachCurrentThread();
}

/**
 * @param player
 */
void call_on_start(Player *player, JNIEnv *env) {
    jclass callback_class = env->GetObjectClass(player->callback);
    jmethodID on_start_method_id = env->GetMethodID(callback_class, "onStart", "()V");
    env->CallVoidMethod(player->callback, on_start_method_id);
    env->DeleteLocalRef(callback_class);
}

/**
 * @param player
 */
void call_on_end(Player *player, JNIEnv *env) {
    jclass callback_class = env->GetObjectClass(player->callback);
    jmethodID on_end_method_id = env->GetMethodID(callback_class, "onEnd", "()V");
    env->CallVoidMethod(player->callback, on_end_method_id);
    env->DeleteLocalRef(callback_class);
}

/**
 * @param player
 * @param env
 * @param total
 * @param current
 */
void call_on_progress(Player *player, JNIEnv *env, double total, double current) {
    jclass callback_class = env->GetObjectClass(player->callback);
    jmethodID on_progress_method_id = env->GetMethodID(callback_class, "onProgress", "(II)V");
    env->CallVoidMethod(player->callback, on_progress_method_id, (int) total, (int) current);
    env->DeleteLocalRef(callback_class);
}

/**
 *
 * @param arg
 * @return
 */
void* produce(void* arg) {
    Player *player = (Player*) arg;
    AVPacket *packet = av_packet_alloc();
    for (;;) {
        pthread_mutex_lock(&seek_mutex);
        while (is_seek) {
            LOGE("Player Log : produce waiting seek");
            pthread_cond_wait(&seek_condition, &seek_mutex);
            LOGE("Player Log : produce wake up seek");
        }
        pthread_mutex_unlock(&seek_mutex);
        if (av_read_frame(player->format_context, packet) < 0) {
            break;
        }
        if (packet->stream_index == player->video_stream_index) {
            queue_in(player->video_queue, packet);
        } else if (packet->stream_index == player->audio_stream_index) {
            queue_in(player->audio_queue, packet);
        }
        packet = av_packet_alloc();
    }
    break_block(player->video_queue);
    break_block(player->audio_queue);
    for (;;) {
        if (queue_is_empty(player->video_queue) && queue_is_empty(player->audio_queue)) {
            break;
        }
//        sleep(1);
    }
    player_release(player);
    return nullptr;
}

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.03
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/**
 * @param arg
 * @return
 */
void* consume(void* arg) {
    Consumer *consumer = (Consumer*) arg;
    Player *player = consumer->player;
    int index = consumer->stream_index;
    JNIEnv *env;
    int result = player->java_vm->AttachCurrentThread(&env, nullptr);
    if (result != JNI_OK) {
        LOGE("Player Error : Can not get current thread env");
        pthread_exit(nullptr);
        return nullptr;
    }
    AVCodecContext *codec_context;
    AVStream *stream;
    Queue *queue;
    if (index == player->video_stream_index) {
        codec_context = player->video_codec_context;
        stream = player->format_context->streams[player->video_stream_index];
        queue = player->video_queue;
        video_prepare(player, env);
    } else if (index == player->audio_stream_index) {
        codec_context = player->audio_codec_context;
        stream = player->format_context->streams[player->audio_stream_index];
        queue = player->audio_queue;
        audio_prepare(player, env);
    }
    if (index == player->audio_stream_index) {
        call_on_start(player, env);
    }
    double total = stream->duration * av_q2d(stream->time_base);
    AVFrame *frame = av_frame_alloc();
    for (;;) {
        pthread_mutex_lock(&seek_mutex);
        while (is_seek) {
            pthread_cond_wait(&seek_condition, &seek_mutex);
        }
        pthread_mutex_unlock(&seek_mutex);
        AVPacket *packet = queue_out(queue);
        if (packet == nullptr) {
            LOGE("consume packet is null");
            break;
        }
        result = avcodec_send_packet(codec_context, packet);
        if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            print_error(result);
            LOGE("Player Error : %d codec step 1 fail", index);
            av_packet_free(&packet);
            continue;
        }
        result = avcodec_receive_frame(codec_context, frame);
        if (result < 0 && result != AVERROR_EOF) {
            print_error(result);
            LOGE("Player Error : %d codec step 2 fail", index);
            av_packet_free(&packet);
            continue;
        }
        if (index == player->video_stream_index) {
            double audio_clock = player->audio_clock;
            double timestamp;
            if(packet->pts == AV_NOPTS_VALUE) {
                timestamp = 0;
            } else {
                timestamp = frame->best_effort_timestamp;
            }
            double frame_rate = av_q2d(stream->avg_frame_rate);
            frame_rate += frame->repeat_pict * (frame_rate * 0.5);
            if (timestamp == 0.0) {
                usleep((unsigned long)(frame_rate * 1000));
            } else {
                if (fabs(timestamp - audio_clock) > AV_SYNC_THRESHOLD_MIN &&
                    fabs(timestamp - audio_clock) < AV_NOSYNC_THRESHOLD) {
                    if (timestamp > audio_clock) {
                        usleep((unsigned long)((timestamp - audio_clock)*1000000));
                    }
                }
            }
            video_play(player, frame, env);
        } else if (index == player->audio_stream_index) {
            player->audio_clock = packet->pts * av_q2d(stream->time_base);
            audio_play(player, frame, env);
            call_on_progress(player, env, total, player->audio_clock);
        }
        av_packet_free(&packet);
    }
    if (index == player->audio_stream_index) {
        call_on_end(player, env);
    }
    player->java_vm->DetachCurrentThread();
    return nullptr;
}

/**
 */
void thread_init(Player* player) {
    pthread_create(&produce_id, nullptr, produce, player);
    Consumer* video_consumer = (Consumer*) malloc(sizeof(Consumer));
    video_consumer->player = player;
    video_consumer->stream_index = player->video_stream_index;
    pthread_create(&video_consume_id, nullptr, consume, video_consumer);
    Consumer* audio_consumer = (Consumer*) malloc(sizeof(Consumer));
    audio_consumer->player = player;
    audio_consumer->stream_index = player->audio_stream_index;
    pthread_create(&audio_consume_id, nullptr, consume, audio_consumer);
    pthread_mutex_init(&seek_mutex, nullptr);
    pthread_cond_init(&seek_condition, nullptr);
}

/**
 * @param player
 */
void play_start(Player *player) {
    player->audio_clock = 0;
    player->video_queue = (Queue*) malloc(sizeof(Queue));
    player->audio_queue = (Queue*) malloc(sizeof(Queue));
    queue_init(player->video_queue);
    queue_init(player->audio_queue);
    thread_init(player);
}

/**
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_charles_ffmpegplayer_FFMpegPlayer_play(JNIEnv *env, jobject instance, jstring path_, jobject surface, jobject callback) {
    const char *path = env->GetStringUTFChars(path_, nullptr);
    int result = 1;
    Player* player;
    player_init(&player, env, instance, surface, callback);
    if (result > 0) {
        result = format_init(player, path);
    }
    if (result > 0) {
        result = codec_init(player, AVMEDIA_TYPE_VIDEO);
    }
    if (result > 0) {
        result = codec_init(player, AVMEDIA_TYPE_AUDIO);
    }
    if (result > 0) {
        play_start(player);
    }
    env->ReleaseStringUTFChars(path_, path);
    cplayer = player;
}

/**
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_charles_ffmpegplayer_FFMpegPlayer_seekTo(JNIEnv *env, jobject instance, jint progress) {
    is_seek = true;
    pthread_mutex_lock(&seek_mutex);
    queue_clear(cplayer->video_queue);
    queue_clear(cplayer->audio_queue);
    int result = av_seek_frame(cplayer->format_context, cplayer->video_stream_index, (int64_t) (progress / av_q2d(cplayer->format_context->streams[cplayer->video_stream_index]->time_base)), AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        LOGE("Player Error : Can not seek video to %d", progress);
        return;
    }
    result = av_seek_frame(cplayer->format_context, cplayer->audio_stream_index, (int64_t) (progress / av_q2d(cplayer->format_context->streams[cplayer->audio_stream_index]->time_base)), AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        LOGE("Player Error : Can not seek audio to %d", progress);
        return;
    }
    is_seek = false;
    pthread_cond_broadcast(&seek_condition);
    pthread_mutex_unlock(&seek_mutex);
}