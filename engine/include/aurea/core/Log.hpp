// =============================================================================
//  Aurea / core / Log.hpp
//
//  Log do motor. Sem alocação no caminho quente: a mensagem vai para um buffer
//  de tamanho fixo na pilha e sai por um sink instalado pela plataforma
//  (logcat no Android, os_log no iOS, stderr no host).
//
//  Nada de printf em release no caminho de frame: AUREA_LOG_TRACE compila para
//  nada quando NDEBUG está definido.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

namespace aurea {

enum class LogLevel : u8 { Trace = 0, Debug, Info, Warn, Error, Fatal };

/// Sink instalado pela plataforma. Recebe a linha já formatada.
using LogSink = void (*)(LogLevel, const char* message, void* user);

void set_log_sink(LogSink sink, void* user) noexcept;
void set_min_log_level(LogLevel level) noexcept;

/// Escreve uma linha. `file` deve ser só o nome base — o caminho completo
/// polui o logcat e vaza a árvore de diretórios do desenvolvedor.
void log_write(LogLevel level, const char* file, int line,
               const char* fmt, ...) noexcept;

} // namespace aurea

#if defined(NDEBUG)
    #define AUREA_LOG_TRACE(...) ((void)0)
    #define AUREA_LOG_DEBUG(...) ((void)0)
#else
    #define AUREA_LOG_TRACE(...) ::aurea::log_write(::aurea::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
    #define AUREA_LOG_DEBUG(...) ::aurea::log_write(::aurea::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#endif

#define AUREA_LOG_INFO(...)  ::aurea::log_write(::aurea::LogLevel::Info,  __FILE__, __LINE__, __VA_ARGS__)
#define AUREA_LOG_WARN(...)  ::aurea::log_write(::aurea::LogLevel::Warn,  __FILE__, __LINE__, __VA_ARGS__)
#define AUREA_LOG_ERROR(...) ::aurea::log_write(::aurea::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define AUREA_LOG_FATAL(...) ::aurea::log_write(::aurea::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)

/// Marca um caminho que não deveria ser alcançado. Em debug quebra no ponto
/// exato; em release registra e segue, porque derrubar o editor do usuário por
/// uma invariante violada é pior do que seguir com estado degradado.
#if defined(NDEBUG)
    #define AUREA_UNREACHABLE(msg) \
        do { AUREA_LOG_ERROR("inalcancavel: %s", (msg)); } while (0)
#else
    #define AUREA_UNREACHABLE(msg) \
        do { AUREA_LOG_FATAL("inalcancavel: %s (%s:%d)", (msg), __FILE__, __LINE__); } while (0)
#endif
