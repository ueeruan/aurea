#pragma once
// =============================================================================
//  Aurea / export / ExportBoundedCall.hpp
//
//  0.5.2 (E4): chamada da PLATAFORMA na partida do export com prazo e
//  cancelar. A partida pergunta à plataforma por cada fonte (Android:
//  ContentResolver.openFileDescriptor de cada content://, a identidade da
//  fonte para o checkpoint). Um provedor de nuvem baixa o arquivo inteiro
//  antes de devolver o descritor, e a chamada não tem CancellationSignal nem
//  prazo: o "Preparando" ficava parado sem fim e o Cancelar não fazia nada,
//  porque o motor só olhava o cancelar ANTES de chamar.
//
//  A chamada roda numa thread própria; quem chama espera com prazo e olhando
//  o cancelar. Desistindo, a thread fica solta até a plataforma voltar (ela
//  não pode ser interrompida de fora) e o resultado dela é descartado; o
//  cancelar que ela recebe é o do estado compartilhado (vive até ela sair),
//  nunca o do contexto do export, que pode morrer antes. Quem passa `fn` faz
//  ele mesmo a limpeza do que abriu (o validate_asset do Android já fecha o
//  descritor que abriu).
// =============================================================================
#include "aurea/core/Result.hpp"
#include "aurea/core/Time.hpp"
#include "aurea/core/Types.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace aurea {

/// Padrão do prazo de cada chamada (EngineConfig::exportSourceOpenWaitMs = 0).
inline constexpr u64 kExportSourceOpenWaitNs = 30'000'000'000ull;

/// Roda `fn(const std::atomic<bool>* cancel)` com prazo. Ok: `out` recebe o
/// valor. Cancelled: `cancel` ficou verdadeiro durante a espera. Timeout: o
/// prazo passou. Sem thread disponível, roda na thread de quem chama (como
/// antes desta proteção) em vez de recusar a partida.
template <class T, class Fn>
[[nodiscard]] Status run_bounded_platform_call(Fn fn, T& out, const std::atomic<bool>* cancel, u64 timeoutNs,
                                               u64 pollNs = 20'000'000ull) noexcept {
    struct Shared {
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<bool> cancel{false};
        bool done = false;
        std::optional<T> value;
    };
    if (cancel && cancel->load(std::memory_order_acquire)) return Errc::Cancelled;
    std::shared_ptr<Shared> shared;
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
        shared = std::make_shared<Shared>();
        std::thread([shared, fn]() mutable noexcept {
            T value = fn(&shared->cancel);
            { std::lock_guard lock(shared->mutex); shared->value.emplace(std::move(value)); shared->done = true; }
            shared->cv.notify_all();
        }).detach();
    } catch (...) {
        if (!shared || !shared->done) { out = fn(cancel); return OkStatus; }
    }
#else
    out = fn(cancel);
    return OkStatus;
#endif
    const u64 deadline = monotonic_ns() + timeoutNs;
    std::unique_lock lock(shared->mutex);
    for (;;) {
        if (shared->done) { out = std::move(*shared->value); return OkStatus; }
        if (cancel && cancel->load(std::memory_order_acquire)) {
            shared->cancel.store(true, std::memory_order_release);
            return Errc::Cancelled;
        }
        const u64 now = monotonic_ns();
        if (now >= deadline) {
            shared->cancel.store(true, std::memory_order_release);
            return Errc::Timeout;
        }
        shared->cv.wait_for(lock, std::chrono::nanoseconds(std::min(pollNs, deadline - now)));
    }
}

} // namespace aurea
