#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_com_charles_ffmpegplayer_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++ 002";
    return env->NewStringUTF(hello.c_str());
}