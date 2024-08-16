package com.charles.ffmpegplayer;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;

import com.charles.ffmpegplayer.databinding.ActivityMainBinding;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity {



    private ActivityMainBinding binding;
    private SurfaceView surfaceView;
    private SurfaceHolder surfaceHolder;
    private Button PlayBtn;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        surfaceView = findViewById(R.id.surface_view);
        surfaceHolder = surfaceView.getHolder();

        PlayBtn = findViewById(R.id.btn_play);

    }

    public void play(View view) {

        PlayBtn.setEnabled(false);
        ExecutorService executor = Executors.newSingleThreadExecutor();
        Handler handler = new Handler(Looper.getMainLooper());
        executor.execute(() -> {
            //Background work here
            String videoPath = "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4";
            FFMpegPlayer player = new FFMpegPlayer();
            player.playVideo(videoPath, surfaceHolder.getSurface());
            handler.post(() -> {
                //UI Thread work here
                PlayBtn.setEnabled(true);
            });
        });
    }

}