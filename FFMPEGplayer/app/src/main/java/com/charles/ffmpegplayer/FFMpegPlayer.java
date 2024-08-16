package com.charles.ffmpegplayer;

import android.view.Surface;

public class FFMpegPlayer {
    // Used to load the 'ffmpegplayer' library on application startup.
    static {
        System.loadLibrary("ffmpegplayer");
    }

    public native void playVideo(String path, Surface surface);

    /**
     * A native method that is implemented by the 'ffmpegplayer' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
}
