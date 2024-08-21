package com.charles.ffmpegplayer;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.TextView;

import com.charles.ffmpegplayer.databinding.ActivityMainBinding;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity {

    private ActivityMainBinding binding;
    private SurfaceView surfaceView;
    private SurfaceHolder surfaceHolder;
    private Button PlayVideoBtn;
    private Button PlayAudioBtn;
    private Button PlayBtn;
    private TextView currentTimeView, totalTimeView;
    String videoPath = "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";
    private FFMpegPlayer player;
//    String videoPath = "rtmp://ns8.indexforce.com/home/mystream";
    //
    ExecutorService executor = Executors.newSingleThreadExecutor();
    Handler handler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        surfaceView = findViewById(R.id.surface_view);
        surfaceHolder = surfaceView.getHolder();
        PlayVideoBtn = findViewById(R.id.btn_play_video);
        PlayAudioBtn = findViewById(R.id.btn_play_audio);
        PlayBtn = findViewById(R.id.btn_play);
        currentTimeView = findViewById(R.id.current_time_view);
        totalTimeView = findViewById(R.id.total_time_view);
        player = new FFMpegPlayer();
    }

    public void playVideo(View view) {
        PlayVideoBtn.setEnabled(false);

        executor.execute(() -> {
            //Background work here
            player.playVideo(videoPath, surfaceHolder.getSurface());
            handler.post(() -> {
                //UI Thread work here
                PlayVideoBtn.setEnabled(true);
            });
        });
    }

    public void playAudio(View view) {
        PlayAudioBtn.setEnabled(false);

        executor.execute(() -> {
            //Background work here
            player.playAudio(videoPath);
            handler.post(() -> {
                //UI Thread work here
                PlayAudioBtn.setEnabled(true);
            });
        });
    }

    public void play(View view) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        PlayBtn.setEnabled(false);
        executor.execute(() -> {
            //Background work here
            player.play(videoPath, surfaceHolder.getSurface(), new FFMpegPlayer.PlayerCallback() {
                @Override
                public void onStart() {

                }
                @Override
                public void onProgress(final int total, final int current) {
                    handler.post(() -> {
                        currentTimeView.setText(formatTime(current));
                        totalTimeView.setText(formatTime(total));
                    });
                }
                @Override
                public void onEnd() {

                    handler.post(() -> {
                        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        PlayBtn.setEnabled(true);
                    });
                }
            });
        });

    }

    public void seekTo(View view) {
        player.seekTo(590);
    }

    private String formatTime(int time) {
        int minute = time / 60;
        int second = time % 60;
        return (minute < 10 ? ("0" + minute) : minute) + ":" + (second < 10 ? ("0" + second) : second);
    }
}