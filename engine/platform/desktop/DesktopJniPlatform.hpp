// =============================================================================
//  Aurea / platform / desktop / DesktopJniPlatform.hpp
//
//  Aurea PC: o que a ponte JNI (platform/android/aurea_jni.cpp, o MESMO
//  arquivo do Android) pede ao sistema, no Windows. Só entra quando o arquivo
//  é compilado fora do Android (`#if !defined(__ANDROID__)`); o Android segue
//  com os cabeçalhos dele, sem mudança alguma.
//
//    Android                       Aurea PC
//    __android_log_write           OutputDebugString + stderr
//    __system_property_get         variável de ambiente (debug.aurea.gpu →
//                                  AUREA_DEBUG_GPU; só os diagnósticos)
//    ANativeWindow (SurfaceView)   apresentação no host (vk::Backend), lida
//                                  pela UI em DesktopFrames.cpp
//    AAudio                        windows::make_audio_output (waveOut)
//    MediaCodec                    windows::MediaFactory (Media Foundation)
//    unistd (close/dup/fdopen)     nomes POSIX do CRT (<io.h>)
// =============================================================================
#pragma once

#include "WindowsMedia.hpp"
#include "aurea/core/Log.hpp"

#include <io.h>
#include <windows.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Os mesmos nomes que o <windows.h> sujaria no resto da ponte.
#undef near
#undef far
#undef DeviceCapabilities

namespace aurea::desktop {

inline constexpr int kPropertyValueMax = 92;   // PROP_VALUE_MAX do bionic

/// Propriedade de diagnóstico do Android lida do ambiente:
/// "debug.aurea.memory_budget_mb" → AUREA_DEBUG_MEMORY_BUDGET_MB. As
/// propriedades do sistema (ro.*) não existem no PC: vazias.
inline int property_get(const char* name, char* value) noexcept {
    value[0] = '\0';
    constexpr const char kPrefix[] = "debug.aurea.";
    if (std::strncmp(name, kPrefix, sizeof(kPrefix) - 1) != 0) return 0;
    char key[128] = "AUREA_DEBUG_";
    usize at = std::strlen(key);
    for (const char* p = name + sizeof(kPrefix) - 1; *p && at + 1 < sizeof(key); ++p) {
        const char ch = *p;
        key[at++] = ch == '.' ? '_' : (ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch);
    }
    key[at] = '\0';
    char buffer[kPropertyValueMax]{};
    const DWORD n = GetEnvironmentVariableA(key, buffer, sizeof(buffer));
    if (n == 0 || n >= sizeof(buffer)) return 0;
    std::memcpy(value, buffer, n + 1);
    return static_cast<int>(n);
}

inline void log_sink(LogLevel level, const char* message, void*) noexcept {
    const char* tag = "I";
    switch (level) {
        case LogLevel::Trace: tag = "V"; break;
        case LogLevel::Debug: tag = "D"; break;
        case LogLevel::Info:  tag = "I"; break;
        case LogLevel::Warn:  tag = "W"; break;
        case LogLevel::Error: tag = "E"; break;
        case LogLevel::Fatal: tag = "F"; break;
    }
    char line[2048];
    std::snprintf(line, sizeof(line), "%s/AureaEngine: %s\n", tag, message ? message : "");
    OutputDebugStringA(line);
    std::fputs(line, stderr);
}

} // namespace aurea::desktop

// Os nomes que a ponte usa nas leituras de diagnóstico (nativeInitialize).
#ifndef PROP_VALUE_MAX
#define PROP_VALUE_MAX 92
#endif
inline int __system_property_get(const char* name, char* value) noexcept {
    return aurea::desktop::property_get(name, value);
}
