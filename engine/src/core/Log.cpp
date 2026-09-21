#include "aurea/core/Log.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdarg>

namespace aurea {
namespace {

std::atomic<LogSink>  g_sink{nullptr};
std::atomic<void*>    g_sinkUser{nullptr};
std::atomic<LogLevel> g_minLevel{LogLevel::Info};

constexpr const char* level_tag(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Trace: return "T";
        case LogLevel::Debug: return "D";
        case LogLevel::Info:  return "I";
        case LogLevel::Warn:  return "W";
        case LogLevel::Error: return "E";
        case LogLevel::Fatal: return "F";
    }
    return "?";
}

/// Nome base do arquivo. O caminho completo no log vaza a árvore de
/// diretórios de quem compilou e não ajuda a achar nada.
const char* base_name(const char* path) noexcept {
    const char* last = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') last = p + 1;
    }
    return last;
}

/// Sink padrão: stderr. Em Android/iOS a plataforma instala o seu.
void default_sink(LogLevel level, const char* message, void* user) noexcept {
    (void)user;
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    if (level == LogLevel::Fatal) {
        std::fflush(stderr);
    }
}

} // namespace

void set_log_sink(LogSink sink, void* user) noexcept {
    g_sinkUser.store(user, std::memory_order_release);
    g_sink.store(sink, std::memory_order_release);
}

void set_min_log_level(LogLevel level) noexcept {
    g_minLevel.store(level, std::memory_order_relaxed);
}

void log_write(LogLevel level, const char* file, int line,
               const char* fmt, ...) noexcept {
    if (static_cast<u8>(level) < static_cast<u8>(g_minLevel.load(std::memory_order_relaxed))) {
        return;
    }

    // Buffer de tamanho fixo na pilha: o log NUNCA aloca. Uma chamada de log
    // não pode ser a razão de um frame estourar o orçamento.
    char body[512];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    (void)n;

    char line_buf[640];
    std::snprintf(line_buf, sizeof(line_buf), "[%s] %s:%d %s",
                  level_tag(level), base_name(file), line, body);

    LogSink sink = g_sink.load(std::memory_order_acquire);
    void*  user  = g_sinkUser.load(std::memory_order_acquire);
    if (sink) {
        sink(level, line_buf, user);
    } else {
        default_sink(level, line_buf, nullptr);
    }
}

} // namespace aurea
