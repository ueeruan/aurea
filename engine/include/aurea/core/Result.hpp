// =============================================================================
//  Aurea / core / Result.hpp
//
//  O motor é compilado com -fno-exceptions. Nenhuma função de fronteira pode
//  lançar: a bridge JNI e a ObjC++ precisam devolver um erro tratável para a
//  UI, não derrubar o processo. Este é o mecanismo único de erro.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include "aurea/core/Types.hpp"

#include <string_view>
#include <utility>
#include <type_traits>

namespace aurea {

enum class Errc : i32 {
    Ok = 0,

    // Geral
    InvalidArgument,
    OutOfRange,
    NotFound,
    AlreadyExists,
    InvalidState,
    NotSupported,
    NotImplemented,

    // Memória
    OutOfMemory,
    BudgetExceeded,

    // Arquivo / projeto
    IoError,
    CorruptData,
    UnsupportedVersion,
    ChecksumMismatch,

    // Mídia
    DecodeFailed,
    EncodeFailed,
    UnsupportedCodec,
    UnsupportedFormat,
    MediaSourceMissing,

    // GPU
    DeviceLost,
    OutOfDeviceMemory,
    PipelineCompileFailed,
    ShaderCompileFailed,
    SurfaceLost,
    UnsupportedFeature,

    // Scheduler
    Cancelled,
    Timeout,
    ShuttingDown,
};

[[nodiscard]] constexpr std::string_view to_string(Errc e) noexcept {
    switch (e) {
        case Errc::Ok:                    return "ok";
        case Errc::InvalidArgument:       return "argumento invalido";
        case Errc::OutOfRange:            return "fora de faixa";
        case Errc::NotFound:              return "nao encontrado";
        case Errc::AlreadyExists:         return "ja existe";
        case Errc::InvalidState:          return "estado invalido";
        case Errc::NotSupported:          return "nao suportado";
        case Errc::NotImplemented:        return "nao implementado";
        case Errc::OutOfMemory:           return "memoria esgotada";
        case Errc::BudgetExceeded:        return "orcamento excedido";
        case Errc::IoError:               return "erro de E/S";
        case Errc::CorruptData:           return "dados corrompidos";
        case Errc::UnsupportedVersion:    return "versao nao suportada";
        case Errc::ChecksumMismatch:      return "checksum divergente";
        case Errc::DecodeFailed:          return "falha ao decodificar";
        case Errc::EncodeFailed:          return "falha ao codificar";
        case Errc::UnsupportedCodec:      return "codec nao suportado";
        case Errc::UnsupportedFormat:     return "formato nao suportado";
        case Errc::MediaSourceMissing:    return "midia de origem ausente";
        case Errc::DeviceLost:            return "dispositivo perdido";
        case Errc::OutOfDeviceMemory:     return "memoria de GPU esgotada";
        case Errc::PipelineCompileFailed: return "falha ao compilar pipeline";
        case Errc::ShaderCompileFailed:   return "falha ao compilar shader";
        case Errc::SurfaceLost:           return "superficie perdida";
        case Errc::UnsupportedFeature:    return "recurso nao suportado";
        case Errc::Cancelled:             return "cancelado";
        case Errc::Timeout:               return "tempo esgotado";
        case Errc::ShuttingDown:          return "encerrando";
    }
    return "erro desconhecido";
}

[[nodiscard]] constexpr bool ok(Errc e) noexcept { return e == Errc::Ok; }

/// Resultado sem valor de retorno. Trivial o bastante para voltar de uma
/// chamada nativa por valor.
class Status {
public:
    constexpr Status() noexcept = default;
    constexpr Status(Errc e) noexcept : code_(e) {}   // NOLINT: conversão intencional
    constexpr Status(Errc e, const char* detail) noexcept : code_(e), detail_(detail) {}

    [[nodiscard]] constexpr Errc code() const noexcept { return code_; }
    [[nodiscard]] constexpr bool ok() const noexcept { return code_ == Errc::Ok; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] constexpr std::string_view detail() const noexcept {
        return detail_ ? std::string_view{detail_} : std::string_view{};
    }
    [[nodiscard]] constexpr std::string_view message() const noexcept { return to_string(code_); }

    /// Código numérico estável para atravessar a bridge (JNI/ObjC++ recebem
    /// i32, não enum class).
    [[nodiscard]] constexpr i32 raw() const noexcept { return static_cast<i32>(code_); }

private:
    Errc code_{Errc::Ok};
    const char* detail_{nullptr};
};

inline constexpr Status OkStatus{};

/// Resultado com valor. Mantém o valor sempre construído (sem união nem
/// std::variant) para que o retorno por valor seja barato e previsível.
template <typename T>
class Result {
public:
    Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_(std::move(value)) {}

    Result(Status s) noexcept : status_(s) {}

    [[nodiscard]] bool ok() const noexcept { return status_.ok(); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
    [[nodiscard]] Status status() const noexcept { return status_; }
    [[nodiscard]] Errc code() const noexcept { return status_.code(); }

    /// Pré-condição: ok(). Chamar em estado de erro é bug de programação, e o
    /// motor não mascara bugs — por isso o valor vem do que já existe, sem
    /// construir um T falso.
    [[nodiscard]] T& operator*() noexcept { return value_; }
    [[nodiscard]] const T& operator*() const noexcept { return value_; }
    [[nodiscard]] T* operator->() noexcept { return &value_; }
    [[nodiscard]] const T* operator->() const noexcept { return &value_; }

private:
    T value_{};
    Status status_{};
};

} // namespace aurea
