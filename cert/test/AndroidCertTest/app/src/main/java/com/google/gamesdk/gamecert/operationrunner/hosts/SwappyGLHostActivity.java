/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.gamesdk.gamecert.operationrunner.hosts;

import android.os.Build.VERSION;
import android.os.Build.VERSION_CODES;
import android.os.Bundle;
import android.os.Trace;
import android.view.Choreographer;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import androidx.annotation.Nullable;

import com.google.gamesdk.R;
import com.google.gamesdk.gamecert.operationrunner.util.NativeInvoker;

public class SwappyGLHostActivity extends BaseGLHostActivity
        implements Choreographer.FrameCallback, SurfaceHolder.Callback {

    public static final String ID = "SwappyGLHostActivity";
    private static final String TAG = "SwappyGLHostActivity";

    public static boolean isSupported() {
        return (VERSION.SDK_INT >= VERSION_CODES.N);
    }

    @Override
    protected int getContentViewResource() {
        return R.layout.activity_swappy_surface;
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        GLContextConfiguration configuration = getGLContextConfiguration();
        GLContextConfiguration fallbackConfiguration = getDefaultGLContextConfiguration();

        SurfaceView surfaceView = findViewById(R.id.surfaceView);
        surfaceView.getHolder().addCallback(this);

        NativeInvoker.swappyGLHost_Init(this, configuration, fallbackConfiguration);
        NativeInvoker.swappyGLHost_SetAutoSwapInterval(true);
    }

    @Override
    protected void onStart() {
        super.onStart();
        NativeInvoker.swappyGLHost_StartRenderer();
        Choreographer.getInstance().postFrameCallback(this);
    }

    @Override
    protected void onStop() {
        super.onStop();
        NativeInvoker.swappyGLHost_StopRenderer();
    }

    @Override
    public void doFrame(long frameTimeNanos) {
        Trace.beginSection("doFrame");

        Choreographer.getInstance().postFrameCallback(this);

        Trace.endSection();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Do nothing here, waiting for surfaceChanged instead
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder,
                               int format,
                               int width,
                               int height) {
        Surface surface = holder.getSurface();
        NativeInvoker.swappyGLHost_SetSurface(surface, width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        NativeInvoker.swappyGLHost_ClearSurface();
    }

}
