package com.charles.ffmpegplayer;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.view.Surface;

public class FFMpegPlayer {
    // Used to load the 'ffmpegplayer' library on application startup.
    static {
        System.loadLibrary("ffmpegplayer");
    }

    /**
     * play video
     * @param path
     * @param surface
     */
    public native void playVideo(String path, Surface surface);

    /**
     * play audio
     * @param path
     */
    public native void playAudio(String path);

    private AudioTrack audioTrack;
    /**
     * create AudioTrack
     * called by C reflection
     * @param sampleRate
     * @param channels
     */
    public void createAudioTrack(int sampleRate, int channels) {
        int channelConfig;
        if (channels == 1) {
            channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        } else if (channels == 2) {
            channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        }else {
            channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        }
        int bufferSize = AudioTrack.getMinBufferSize(sampleRate, channelConfig, AudioFormat.ENCODING_PCM_16BIT);
        audioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRate, channelConfig,
                AudioFormat.ENCODING_PCM_16BIT, bufferSize, AudioTrack.MODE_STREAM);
        audioTrack.play();
    }

    /**
     * play AudioTrack
     * called by C reflection
     * @param data
     * @param length
     */
    public void playAudioTrack(byte[] data, int length) {
        if (audioTrack != null && audioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
            audioTrack.write(data, 0, length);
        }
    }

    /**
     * release AudioTrack
     * called by C reflection
     */
    public void releaseAudioTrack() {
        if (audioTrack != null) {
            if (audioTrack.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) {
                audioTrack.stop();
            }
            audioTrack.release();
            audioTrack = null;
        }
    }

    /**
     * @param path
     * @param surface
     * @param callback
     */
    public native void play(String path, Surface surface, PlayerCallback callback);

    /**
     * @param progress
     */
    public native void seekTo(int progress);

    /**
     */
    public interface PlayerCallback {
        /**
         */
        void onStart();
        /**
         * @param total
         * @param current
         */
        void onProgress(int total, int current);
        /**
         * 播放结束
         */
        void onEnd();
    }
}
