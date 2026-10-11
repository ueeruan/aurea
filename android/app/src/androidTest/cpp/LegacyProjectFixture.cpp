#include <jni.h>
#include <cerrno>
#include <unistd.h>

// Instrumentation-only helper, built separately from the application. A
// Windows drive path is relative on POSIX, so an owned fixture directory can
// reproduce the original asset identity without rewriting the project.
extern "C" JNIEXPORT jint JNICALL
Java_com_aurea_aurea_editor_AstraExportDeviceTest_changeFixtureDirectory(
        JNIEnv* env, jobject, jstring directory) {
    if (!directory) return EINVAL;
    const char* path = env->GetStringUTFChars(directory, nullptr);
    if (!path) return ENOMEM;
    const int result = chdir(path);
    const int error = result == 0 ? 0 : errno;
    env->ReleaseStringUTFChars(directory, path);
    return error;
}
