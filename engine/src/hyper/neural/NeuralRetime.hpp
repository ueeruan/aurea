// =============================================================================
//  Aurea / hyper / neural / NeuralRetime.hpp
//
//  Flow Retime "Neural (IA)" na timeline (1.0.2). A rede RIFE v4.6 (MIT, já
//  embutida para o Hyper Motion) calcula, numa grade reduzida, os dois fluxos
//  do instante t para os quadros de origem e a máscara de visibilidade; o
//  quadro em resolução cheia é montado na GPU (video/flow_neural_warp.frag).
//
//  Onde roda:
//    * EXPORT: o quadro espera a rede (síncrono) — é o arquivo final;
//    * prévia PARADA (pausa ou fim do arrasto): a rede roda numa thread
//      própria; enquanto não fica pronta a prévia mostra o fluxo da GPU
//      (o de sempre) e, quando fica, pede um novo quadro. Na reprodução a
//      prévia segue só com o fluxo da GPU (a rede não acompanha 30 qps no
//      celular).
//  Android e Windows: ncnn no Vulkan (com volta para a CPU). Apple: ncnn na
//  CPU por enquanto (Core ML é o passo seguinte, ver docs).
//
//  Entrada: dois quadros NV12 com planos na CPU (o decoder entrega, ou
//  `prepare_cpu_planes`). Outro formato ou rotação: sem rede (o chamador usa
//  o fluxo da GPU na qualidade Melhor).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/media/VideoTypes.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace aurea {

namespace hyper { class MotionInterpolator; }

class NeuralRetime {
public:
    /// Campo pronto: fluxos (RGBA = t→A, t→B, em uv da imagem) e máscara (peso
    /// de A) em half float, na grade da rede.
    struct Field {
        u64 key = 0;
        u32 width = 0, height = 0;
        f32 validU = 1.0f, validV = 1.0f;   ///< parte da grade que cobre a imagem
        std::vector<u16> flow;              ///< width·height·4
        std::vector<u16> mask;              ///< width·height·4 (R = máscara)
        bool copyA = false, copyB = false;  ///< quadros iguais / corte: sem rede
    };

    NeuralRetime();
    ~NeuralRetime();
    NeuralRetime(const NeuralRetime&) = delete;
    NeuralRetime& operator=(const NeuralRetime&) = delete;

    /// Chave do pedido (o par de quadros e o instante).
    [[nodiscard]] static u64 key_of(u64 pairKey, f32 t) noexcept;

    /// Os quadros servem para a rede (NV12 na CPU, sem rotação)?
    [[nodiscard]] static bool supported(DecodedFrame& a, DecodedFrame& b) noexcept;

    /// Síncrono (export). Falso = sem rede (o chamador usa o fluxo da GPU).
    [[nodiscard]] bool compute(u64 key, DecodedFrame& a, DecodedFrame& b, f32 t, Field& out) noexcept;

    /// Assíncrono (prévia parada): o campo pronto desta chave, ou agenda o
    /// cálculo (o pedido mais novo substitui o que ainda não começou) e
    /// devolve falso. `ready` é chamado na thread da rede quando terminar.
    [[nodiscard]] bool request(u64 key, DecodedFrame& a, DecodedFrame& b, f32 t, Field& out) noexcept;
    void set_ready_callback(void (*fn)(void*), void* ctx) noexcept;

    /// Projeto fechado / pressão de memória: rede e campos fora.
    void clear() noexcept;

private:
    struct Job {
        u64 key = 0;
        u32 width = 0, height = 0;
        f32 t = 0.5f;
        std::vector<u8> rgbA, rgbB;
    };
    bool run(Job& job, Field& out) noexcept;
    void worker() noexcept;
    static bool to_rgb(DecodedFrame& f, std::vector<u8>& rgb, u32& w, u32& h) noexcept;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    bool stop_ = false;
    std::unique_ptr<Job> pending_;
    u64 runningKey_ = 0;
    std::vector<Field> done_;              ///< os últimos campos prontos (poucos)
    std::unique_ptr<hyper::MotionInterpolator> net_;
    u32 netW_ = 0, netH_ = 0;
    std::mutex netMutex_;
    void (*readyFn_)(void*) = nullptr;
    void* readyCtx_ = nullptr;
};

} // namespace aurea
