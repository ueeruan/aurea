// A FILA ENTRE A THREAD DA UI E A THREAD DO RENDER.
//
// UM PRODUTOR, UM CONSUMIDOR, SEM TRAVA. A thread da UI empurra; a do
// render puxa. Nenhuma das duas espera pela outra, e nenhuma das duas
// aloca dentro do caminho: a fila nasce com capacidade fixa (potencia de
// dois) e guarda comandos triviais (POD, sem ponteiro para memoria viva).
//
// POR QUE SEM TRAVA: uma `std::mutex` aqui seria tomada 120 vezes por
// segundo em cada lado para transferir oito bytes. O custo nao e o
// `lock` — e o `futex` do sistema quando ha disputa, e a disputa e certa
// quando a UI toca a tela no meio do quadro. Com `acquire`/`release` nos
// dois indices nao ha espera: no maximo o produtor encontra a fila cheia
// e devolve `capacidade` em vez de bloquear.
//
// A MEMORIA DOS COMANDOS NAO ATRAVESSA. Os comandos carregam valores,
// nunca ponteiros para objetos Dart vivos. Quem precisa entregar uma cena
// inteira entrega um `std::shared_ptr<const Cena>` — que e copiavel e
// seguro por construcao — e o outro lado solta quando terminar.
#ifndef AUREA_RENDER_FILA_DE_COMANDOS_H
#define AUREA_RENDER_FILA_DE_COMANDOS_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>

namespace aurea::render {

/// A capacidade padrao. Mil e vinte e quatro comandos por quadro e mais
/// do que qualquer edicao produz (uma cena inteira cabe em UM comando),
/// e o custo em memoria e a capacidade vezes o tamanho do comando.
inline constexpr std::size_t kCapacidadeDaFila = 1024;

template <typename T>
class FilaCircular {
  static_assert(std::is_trivially_copyable_v<T>,
                "a fila entre threads so guarda valores triviais: sem "
                "destrutor, sem alocacao, sem ponteiro para memoria viva");

 public:
  explicit FilaCircular(std::size_t capacidade = kCapacidadeDaFila)
      : capacidade_(arredondar_para_potencia_de_dois(capacidade)),
        // `new T[]` e nao `std::vector`: o buffer nunca cresce nem
        // realoca, e um vetor convidaria a isso.
        buffer_(std::make_unique<T[]>(capacidade_)) {}

  FilaCircular(const FilaCircular&) = delete;
  FilaCircular& operator=(const FilaCircular&) = delete;

  /// Do produtor. `false` = a fila esta cheia; quem chamou decide o que
  /// fazer (a UI pode tentar no quadro seguinte, e nao travar o dedo).
  [[nodiscard]] bool empurrar(const T& v) noexcept {
    const std::size_t cabeca = cabeca_.load(std::memory_order_relaxed);
    const std::size_t proxima = (cabeca + 1) & mascara_;
    // A leitura da cauda e `acquire`: garante que o consumidor ja soltou
    // o slot antes de escrevermos nele.
    if (proxima == cauda_.load(std::memory_order_acquire)) return false;
    buffer_[cabeca] = v;
    cabeca_.store(proxima, std::memory_order_release);
    return true;
  }

  /// Do consumidor. `nullopt` = vazia.
  [[nodiscard]] std::optional<T> puxar() noexcept {
    const std::size_t cauda = cauda_.load(std::memory_order_relaxed);
    if (cauda == cabeca_.load(std::memory_order_acquire)) {
      return std::nullopt;
    }
    const T v = buffer_[cauda];
    cauda_.store((cauda + 1) & mascara_, std::memory_order_release);
    return v;
  }

  /// ESVAZIA SEM LER. Serve para o caso de troca de projeto: os comandos
  /// do projeto antigo nao podem chegar ao novo.
  void limpar() noexcept {
    cauda_.store(cabeca_.load(std::memory_order_acquire),
                 std::memory_order_release);
  }

  [[nodiscard]] bool vazia() const noexcept {
    return cauda_.load(std::memory_order_acquire) ==
           cabeca_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::size_t capacidade() const noexcept {
    return capacidade_ - 1;  // um slot fica sempre vazio
  }

  /// Decidido na construcao e nunca mais escrito: ler de outra thread e
  /// seguro sem atomico.
  [[nodiscard]] std::size_t uso() const noexcept {
    const std::size_t c = cabeca_.load(std::memory_order_acquire);
    const std::size_t t = cauda_.load(std::memory_order_acquire);
    return (c - t) & mascara_;
  }

 private:
  [[nodiscard]] static std::size_t arredondar_para_potencia_de_dois(
      std::size_t v) noexcept {
    std::size_t p = 2;
    while (p < v && p < (std::size_t{1} << 40)) p <<= 1;
    return p;
  }

  const std::size_t capacidade_;
  const std::size_t mascara_ = capacidade_ - 1;
  std::unique_ptr<T[]> buffer_;
  // `alignas` mantem cada indice na propria linha de cache: sem isso os
  // dois nucleos brigam pela mesma linha a cada comando.
  alignas(64) std::atomic<std::size_t> cabeca_{0};
  alignas(64) std::atomic<std::size_t> cauda_{0};
};

}  // namespace aurea::render

#endif  // AUREA_RENDER_FILA_DE_COMANDOS_H
