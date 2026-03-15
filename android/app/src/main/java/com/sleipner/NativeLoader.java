package com.sleipner;

import android.app.NativeActivity;

public class NativeLoader extends NativeActivity {
    static {
        System.loadLibrary("sleipner");
    }
}
