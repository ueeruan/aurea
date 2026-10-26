// =============================================================================
//  Aurea / hyper / HyperStage.hpp
//
//  A INTERFACE DAS ETAPAS do grafo Hyper (0.3.2). É o único ponto de contato
//  entre o motor (src/hyper/**, src/engine/EngineHyper.cpp) e as redes
//  (src/hyper/neural/**). Mudar algo aqui exige avisar os dois lados.
//
//  Onde cada caminho roda
//  ----------------------
//    Turbo — na GPU, DENTRO do render (efeitos de camada aurea.hyper.*: os
//            mesmos shaders da prévia). Não passa por esta interface.
//    Lite/Max — aqui: o grafo Hyper (src/hyper/HyperGraph.cpp) roda entre o
//            readback do export e o encoder, quadro a quadro, numa fila
//            limitada (3–5 quadros; nunca memória proporcional ao vídeo), com
//            uma thread de processamento própria. Uma etapa recebe o quadro
//            atual e, se for temporal, os vizinhos (±radius) e devolve UM
//            quadro: o grafo NÃO muda o número de quadros. Motion Turbo
//            interpola no render; Motion Lite/Max roda aqui sobre quadros de
//            saída em que o render REPETE o de origem (FrameView::
//            sourcePosition) e recebe o par de origem e a fase em StageInput
//            (motionA, motionB, motionPhase; o grafo espera B chegar, até 10
//            quadros à frente). Etapas antes do Motion não reprocessam um
//            quadro repetido: o grafo copia a saída anterior.
//
//  Formato dos quadros
//  -------------------
//    NV12 BT.709 faixa limitada (o que o export produz e o encoder recebe):
//    Y w×h e UV entrelaçado (w/2)×(h/2)×2, w e h PARES. `FrameView` aponta
//    para memória de outro dono (válida só durante `process`); a saída vai
//    num `FrameBuffer` do grafo (já dimensionado pelo `prepare`). As redes
//    trabalham em R'G'B' 8 bits: use `nv12_to_rgb24` / `rgb24_to_nv12` daqui.
//
//  Como as redes se registram
//  --------------------------
//    O agente das redes define, em src/hyper/neural/NeuralStages.cpp,
//        void aurea::hyper::register_neural_stages(StageRegistry& r);
//    e chama `r.add(...)` para cada implementação Lite/Max. Enquanto esse
//    arquivo não existir o CMake compila o padrão (src/hyper/HyperRegistry.cpp,
//    que não registra nada) — o build funciona antes das redes chegarem. O
//    motor já registra uma implementação embutida de Upscale Anime Lite/Max
//    (o Real-ESRGAN animevideov3 da 0.1.4, ai/Upscaler) com prioridade 0; a
//    das redes ganha com prioridade maior.
//
//  Regras de uma etapa
//  -------------------
//    * `prepare` aloca tudo (pesos, buffers) e diz o formato de saída. Falha =
//      o grafo tenta a próxima implementação (Max → Lite → Turbo no render).
//    * `process` não aloca por quadro além do que `prepare` previu, chama
//      `ctx.beat()` pelo menos a cada ~1 s (o watchdog do export conta), e
//      volta com Errc::Cancelled logo que `ctx.cancelled()`.
//    * Uma instância pertence a UMA thread (a do grafo). `release` devolve a
//      memória; o destrutor também.
//    * Determinismo: o mesmo quadro de entrada dá a mesma saída (retomada de
//      export por checkpoint depende disso).
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/hyper/Hyper.hpp"

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace aurea::hyper {

/// As etapas, na ordem fixa do grafo (Hyper.hpp).
enum class StageKind : u8 { Clean = 0, Stable, Upscale, Face, Motion, Count };
inline constexpr u32 kStageKindCount = static_cast<u32>(StageKind::Count);
[[nodiscard]] const char* stage_kind_name(StageKind kind) noexcept;

/// Um quadro NV12 de outro dono (só leitura).
struct FrameView {
    const u8* y = nullptr;
    const u8* uv = nullptr;
    u32 width = 0, height = 0;
    u32 yStride = 0, uvStride = 0;   ///< bytes por linha (≥ width)
    i64 ptsUs = 0;                   ///< tempo de apresentação na SAÍDA
    u32 index = 0;                   ///< quadro na saída (0..n-1)
    /// Posição deste quadro de saída em quadros da FONTE (−1 = não se sabe).
    /// Numa passagem com Motion Lite/Max o render repete o quadro de origem
    /// floor(sourcePosition) e a etapa de Motion monta o instante fracionário.
    f64 sourcePosition = -1.0;
    [[nodiscard]] bool valid() const noexcept { return y && uv && width && height; }
};

/// Um quadro NV12 do grafo (Y e UV contíguos, stride = largura).
struct FrameBuffer {
    std::vector<u8> nv12;
    u32 width = 0, height = 0;
    i64 ptsUs = 0;
    u32 index = 0;
    f64 sourcePosition = -1.0;   ///< FrameView::sourcePosition
    /// Dimensiona sem perder a capacidade (sem alocação por quadro depois do 1º).
    [[nodiscard]] Status resize(u32 w, u32 h) noexcept;
    [[nodiscard]] u8* y() noexcept { return nv12.data(); }
    [[nodiscard]] u8* uv() noexcept { return nv12.data() + static_cast<usize>(width) * height; }
    [[nodiscard]] FrameView view() const noexcept;
};

/// Formato que entra/sai de uma etapa.
struct StageFormat {
    u32 width = 0, height = 0;   ///< pares
    f64 fps = 0.0;
};

/// O que o grafo oferece a uma etapa enquanto ela processa.
struct StageContext {
    const std::atomic<bool>* cancel = nullptr;
    void (*beatFn)(void*) noexcept = nullptr;            ///< batida do watchdog do export
    void* beatContext = nullptr;
    /// Progresso DENTRO do quadro (0..1) para a mensagem da tela ("Upscale 3/120 · 40%").
    void (*progressFn)(void*, f32 fraction) noexcept = nullptr;
    void* progressContext = nullptr;
    [[nodiscard]] bool cancelled() const noexcept { return cancel && cancel->load(std::memory_order_relaxed); }
    void beat() const noexcept { if (beatFn) beatFn(beatContext); }
    void progress(f32 fraction) const noexcept { if (progressFn) progressFn(progressContext, fraction); beat(); }
};

/// A entrada de `process`: o quadro atual e os vizinhos que a etapa pediu
/// (`temporal_radius`), na ordem −r..−1, +1..+r. Vizinho fora do vídeo (antes
/// do 1º, depois do último) vem como a borda repetida — nunca inválido.
struct StageInput {
    FrameView current{};
    std::span<const FrameView> neighbors{};
    std::span<const i32> offsets{};      ///< deslocamento de cada vizinho (quadros)
    /// Só para etapas StageKind::Motion: os dois quadros de ORIGEM em volta do
    /// instante deste quadro de saída e a fase t em [0, 1) entre eles.
    /// motionPhase == 0 = quadro original (copie `current`). Sem o quadro B
    /// ainda (fim do vídeo, ou além da fila do grafo), motionB = motionA.
    FrameView motionA{}, motionB{};
    f32 motionPhase = 0.0f;
};

class HyperStage {
public:
    virtual ~HyperStage() = default;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
    [[nodiscard]] virtual StageKind kind() const noexcept = 0;
    [[nodiscard]] virtual Path path() const noexcept = 0;
    /// Vizinhos de cada lado que `process` precisa (0 = só o quadro). Teto 2:
    /// a fila do grafo guarda no máximo 2·radius+1 ≤ 5 quadros por etapa.
    [[nodiscard]] virtual u32 temporal_radius() const noexcept { return 0; }
    /// A saída depende de quadros ANTES da janela (estado entre quadros, ex.:
    /// suavização temporal acumulada). Verdadeiro = o export desta passagem não
    /// retoma no meio (recomeça do quadro 0; as passagens prontas continuam).
    [[nodiscard]] virtual bool stateful() const noexcept { return false; }
    /// Aloca e configura. `out` = formato de saída (o Upscale multiplica).
    [[nodiscard]] virtual Status prepare(const DeviceProfile& profile, const Recipe& recipe,
                                         const StageFormat& in, StageFormat& out) noexcept = 0;
    /// Um quadro. `out` já tem o tamanho de saída do `prepare`; a etapa só
    /// escreve os pixels (o grafo copia pts/index).
    [[nodiscard]] virtual Status process(const StageInput& in, FrameBuffer& out, const StageContext& ctx) noexcept = 0;
    virtual void release() noexcept {}
    /// Memória máxima (CPU + GPU estimada) depois do `prepare`, para a estimativa.
    [[nodiscard]] virtual u64 peak_bytes() const noexcept { return 0; }
};

/// Uma implementação registrada.
struct StageDescriptor {
    const char* id = "";                 ///< estável, para log/diagnóstico ("upscale.anime.esrgan")
    StageKind kind = StageKind::Upscale;
    Path path = Path::Lite;              ///< Lite ou Max (Turbo vive no render)
    /// Upscale: modo atendido (UpscaleMode) ou −1 = qualquer. Outras etapas: −1.
    i32 mode = -1;
    i32 priority = 0;                    ///< maior ganha entre as disponíveis
    std::unique_ptr<HyperStage> (*create)() = nullptr;
    /// Disponível neste aparelho? Nulo = sempre. (Ex.: Max pede 64 bits.)
    bool (*available)(const DeviceProfile&) noexcept = nullptr;
    /// Custo previsto em ms por megapixel de ENTRADA neste aparelho, para a
    /// estimativa (`Engine::hyper_estimate`). Nulo = o motor usa o tile medido.
    f64 (*msPerMegapixel)(const DeviceProfile&) noexcept = nullptr;
};

class StageRegistry {
public:
    void add(const StageDescriptor& descriptor);
    /// A melhor implementação disponível para (etapa, caminho, modo); nulo = nenhuma.
    [[nodiscard]] const StageDescriptor* find(StageKind kind, Path path, i32 mode,
                                              const DeviceProfile& profile) const noexcept;
    [[nodiscard]] std::span<const StageDescriptor> all() const noexcept { return entries_; }
private:
    std::vector<StageDescriptor> entries_;
};

/// O registro do processo (embutidos + `register_neural_stages`), criado na
/// primeira chamada. Thread-safe para leitura depois de criado.
[[nodiscard]] const StageRegistry& stage_registry();

/// Definida pelo agente das redes em src/hyper/neural/NeuralStages.cpp; sem
/// esse arquivo, o padrão (HyperRegistry.cpp) não registra nada.
void register_neural_stages(StageRegistry& registry);

// --- Conversões para as redes (BT.709 faixa limitada ↔ R'G'B' 0..255) -------
void nv12_to_rgb24(const FrameView& in, u8* rgb, u32 rgbStride) noexcept;
/// `rgb` w×h (pares). Escreve no FrameBuffer já dimensionado w×h.
[[nodiscard]] bool rgb24_to_nv12(const u8* rgb, u32 rgbStride, u32 width, u32 height, FrameBuffer& out) noexcept;

} // namespace aurea::hyper
