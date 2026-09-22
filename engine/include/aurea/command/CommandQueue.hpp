// =============================================================================
//  Aurea / command / CommandQueue.hpp
//
//  A fila entre a UI e o motor.
//
//  Fluxo:
//     Gesto na UI
//        ↓  (Compose / SwiftUI escreve comandos POD)
//     CommandQueue (SPSC: UI produz, thread de render consome)
//        ↓  (uma drenagem por frame, fora da thread da UI)
//     Engine aplica
//        ↓
//     Renderer
//
//  A fila é SPSC sem trava. A UI nunca espera o motor, e o motor nunca espera a
//  UI — se a UI sumir (app em background), o motor simplesmente não recebe mais
//  comandos e o último estado vale.
//
//  Capacidade é fixa e grande o bastante para o pior caso humano (arrastar 40
//  layers com o dedo): nenhuma submissão aloca.
// =============================================================================
#pragma once

#include "aurea/command/Command.hpp"
#include "aurea/core/Types.hpp"

#include <atomic>

namespace aurea {

class CommandQueue {
public:
    /// 16384 comandos * 128 B = 2 MiB. Um dedo humano em 120 Hz não chega
    /// perto; a folga existe para rajadas (colar 200 layers, importar projeto).
    static constexpr u32 kCapacity = 16384;
    static constexpr u32 kMask     = kCapacity - 1;

    /// Blob de strings do lote atual. Nomes de layer, conteúdo de texto e
    /// caminhos de asset vivem aqui, endereçados por offset — nada de
    /// ponteiro atravessando a bridge.
    static constexpr u32 kStringBlobSize = 256 * 1024;

    CommandQueue() noexcept { reset(); }

    CommandQueue(const CommandQueue&)            = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        stringWrite_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
    }

    // -------------------------------------------------------------------------
    // Lado da UI (produtor)
    // -------------------------------------------------------------------------

    /// Escreve um comando. Devolve o índice dele, ou kInvalidIndex se a fila
    /// encheu. Fila cheia NÃO bloqueia: a UI decide (no caso real, ela para de
    /// emitir porque o dedo parou antes disso).
    [[nodiscard]] u32 push(const Command& cmd) noexcept {
        const u32 tail = tail_.load(std::memory_order_relaxed);
        const u32 head = head_.load(std::memory_order_acquire);
        if (tail - head >= kCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return kInvalidIndex;
        }
        slots_[tail & kMask].cmd = cmd;
        tail_.store(tail + 1, std::memory_order_release);
        return tail & kMask;
    }

    /// Reserva espaço no blob de strings e devolve o offset. A UI escreve
    /// direto no buffer devolvido. Devolve nullptr se o blob encheu.
    ///
    /// ANEL: quando não cabe no fim, volta ao início. Cada string é lida no
    /// próximo quadro (a drenagem copia na hora); para uma ser sobrescrita
    /// antes de lida, a UI teria de escrever o blob inteiro num único quadro.
    [[nodiscard]] char* alloc_string(u32 bytes) noexcept {
        if (bytes == 0 || bytes >= kStringBlobSize) return nullptr;
        u32 off = stringWrite_.load(std::memory_order_relaxed);
        if (off + bytes >= kStringBlobSize) off = 0;
        stringWrite_.store(off + bytes, std::memory_order_relaxed);
        return stringBlob_ + off;
    }
    /// Deslocamento de um ponteiro devolvido por alloc_string.
    [[nodiscard]] u32 offset_of(const char* p) const noexcept { return static_cast<u32>(p - stringBlob_); }

    /// Conveniência: escreve uma string terminada em NUL e devolve offset e
    /// tamanho prontos para o Command.
    bool push_string(const char* text, u32 len, u32& outOffset, u32& outLength) noexcept {
        char* dst = alloc_string(len + 1);
        if (!dst) return false;
        for (u32 i = 0; i < len; ++i) dst[i] = text[i];
        dst[len] = '\0';
        outOffset = static_cast<u32>(dst - stringBlob_);
        outLength = len;
        return true;
    }

    /// Publica o lote: torna tudo o que foi escrito visível ao consumidor.
    /// Sem isto, os comandos ficam no buffer mas o motor não os lê.
    void commit() noexcept {
        // A barreira de release em tail_ (feita em cada push) já ordena as
        // escritas dos slots. O blob é lido só depois de o comando que o
        // referencia ser lido, e a leitura de tail_ com acquire no consumidor
        // garante que o blob também está visível.
    }

    // -------------------------------------------------------------------------
    // Lado do motor (consumidor) — só a thread de render chama
    // -------------------------------------------------------------------------

    [[nodiscard]] u32 available() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool pop(Command& out) noexcept {
        const u32 head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        out = slots_[head & kMask].cmd;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Drena até `maxCount` comandos para `sink`, na ordem em que a UI os
    /// escreveu. Uma única travessia por frame.
    template <typename Sink>
    u32 drain(Sink&& sink, u32 maxCount = kCapacity) noexcept {
        u32 n = 0;
        Command c;
        while (n < maxCount && pop(c)) {
            sink(c);
            ++n;
        }
        return n;
    }

    /// Resolve o offset de string de um comando. Vale só durante a drenagem
    /// do lote em que o comando foi escrito.
    [[nodiscard]] const char* string_at(u32 offset, u32 length) const noexcept {
        if (offset + length >= kStringBlobSize) return "";
        return stringBlob_ + offset;
    }

    [[nodiscard]] u64 dropped_count() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    // Cada slot alinhado a uma linha de cache: sem isso produtor e consumidor
    // escrevem na mesma linha e o falso compartilhamento custa mais do que o
    // trabalho real da fila. O alinhamento sozinho basta — Command já tem
    // tamanho múltiplo de 64 (ver static_assert abaixo).
    struct alignas(64) Slot {
        Command cmd{};
    };

    Slot slots_[kCapacity]{};
    char stringBlob_[kStringBlobSize]{};

    alignas(64) std::atomic<u32> head_{0};
    alignas(64) std::atomic<u32> tail_{0};
    alignas(64) std::atomic<u32> stringWrite_{0};
    alignas(64) std::atomic<u64> dropped_{0};
};

static_assert(sizeof(Command) % 64 == 0,
              "Command precisa preencher linhas de cache inteiras para o "
              "alinhamento dos slots nao desperdicar memoria");

// A fila passa de 1 MB, e isso é uma armadilha: a pilha padrão de uma thread
// no Windows é de exatamente 1 MB, e no Android costuma ser 1 MB também. Um
// `CommandQueue fila;` local compila, roda a primeira linha e derruba o
// processo por estouro de pilha — sem mensagem, sem stack trace útil, e com o
// sintoma longe da causa.
//
// O Engine já a cria com `std::make_unique`. Este static_assert existe para que
// qualquer tentativa futura de trazer a fila para a pilha (um membro por valor,
// um array de filas) pare de compilar.
static_assert(sizeof(CommandQueue) > 1024 * 1024,
              "CommandQueue tem mais de 1 MB: aloque no heap, nunca na pilha");

} // namespace aurea
