// =============================================================================
//  Aurea / hyper / neural / NeuralCommon.hpp (privado das redes)
//
//  O que as etapas Lite/Max das redes dividem:
//    * os modelos embutidos (engine/assets/hyper, gerados pelo CMake) e os sob
//      demanda (arquivo + SHA-256 do manifesto);
//    * o orçamento de memória (medido do sistema em tempo de execução; em 32
//      bits também o espaço de endereços livre), nunca um número fixo;
//    * o alocador limitado do ncnn (como o do ai/Upscaler), um por worker;
//    * a escolha de backend (Vulkan fp16 com volta para a CPU; int8 na CPU);
//    * os workers nos núcleos grandes (onde a plataforma deixa);
//    * o motor de tiles: tile/margem do DeviceProfile (8–12 px, não o halo de
//      20 px antigo), pular tile parado (SAD contra a entrada que gerou a
//      saída guardada), mistura temporal guiada por movimento.
// =============================================================================
#pragma once

#include "aurea/core/Result.hpp"
#include "aurea/hyper/HyperNeural.hpp"
#include "aurea/hyper/HyperStage.hpp"

#include <allocator.h>
#include <net.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace aurea::hyper::neural {

// --- Modelos -------------------------------------------------------------------
struct Blob {
    const unsigned char* data = nullptr;
    usize size = 0;
    [[nodiscard]] const char* text() const noexcept { return reinterpret_cast<const char*>(data); }
    [[nodiscard]] explicit operator bool() const noexcept { return data != nullptr; }
};
/// Arquivo embutido por id (o nome do arquivo em engine/assets/hyper/<modelo>/).
/// Texto (.param) termina em NUL. Nulo = ausente neste build.
[[nodiscard]] Blob embedded(const char* id) noexcept;

/// Um arquivo de modelo carregado (sob demanda) com terminação NUL.
struct OwnedBlob {
    std::vector<unsigned char> bytes;
    [[nodiscard]] Blob blob() const noexcept { return {bytes.data(), bytes.empty() ? 0 : bytes.size() - 1}; }
};
/// Confere <pasta de modelos>/<id>/<file> (tamanho e SHA-256 do manifesto,
/// lido em pedaços; uma vez por processo) e devolve o caminho.
[[nodiscard]] Status verify_ondemand(const char* id, const char* file, std::string& path);
/// Lê um arquivo sob demanda pequeno (o .param) inteiro, já conferido.
[[nodiscard]] Status load_ondemand(const char* id, const char* file, OwnedBlob& out);
[[nodiscard]] std::string sha256_hex(const unsigned char* data, usize size);

// --- Memória e threads -----------------------------------------------------------
/// Memória disponível que o sistema informa agora (0 = desconhecida).
[[nodiscard]] u64 system_available_bytes() noexcept;
/// 32 bits: o maior bloco de endereços reservável agora (sonda com reserva sem
/// acesso, devolvida na hora). 64 bits: ~infinito.
[[nodiscard]] u64 address_space_headroom() noexcept;
/// Orçamento de trabalho da inferência (ativações + buffers de tile), do que
/// o sistema informa e do perfil. `fixedBytes` = já gastos (pesos, quadros).
[[nodiscard]] u64 inference_budget(const DeviceProfile& profile, u64 fixedBytes) noexcept;
/// Workers de CPU: só os núcleos grandes; 32 bits no máximo 2, 64 bits 4.
[[nodiscard]] u32 cpu_workers(const DeviceProfile& profile) noexcept;
/// Prende a thread atual aos núcleos grandes (Android); QoS alta (Apple).
void pin_to_big_cores() noexcept;

class BoundedAllocator final : public ncnn::Allocator {
public:
    explicit BoundedAllocator(u64 budget = 64ull << 20) noexcept : budget_(budget) {}
    void set_budget(u64 budget) noexcept { budget_ = budget; }
    void* fastMalloc(size_t size) override;
    void fastFree(void* ptr) override;
    [[nodiscard]] u64 peak() const noexcept { return peak_.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 live() const noexcept { return live_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool exceeded() const noexcept { return exceeded_.load(std::memory_order_relaxed); }
    void reset_peak() noexcept { peak_.store(live(), std::memory_order_relaxed); exceeded_ = false; }
private:
    u64 budget_;
    std::atomic<u64> live_{0}, peak_{0};
    std::atomic<bool> exceeded_{false};
};

/// Workers fixos (criados no prepare, nunca por quadro). `run(n, fn)` chama
/// fn(job, worker) para job em [0, n), a thread que chama também trabalha.
class WorkerPool {
public:
    WorkerPool() = default;
    ~WorkerPool() { stop(); }
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    [[nodiscard]] Status start(u32 workers) noexcept;   ///< total, contando a thread que chama
    void stop() noexcept;
    [[nodiscard]] u32 size() const noexcept { return static_cast<u32>(threads_.size()) + 1; }
    void run(u32 jobs, const std::function<void(u32 job, u32 worker)>& fn) noexcept;
private:
    void loop(u32 worker) noexcept;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    const std::function<void(u32, u32)>* fn_ = nullptr;
    u32 jobs_ = 0, next_ = 0, busy_ = 0;
    u64 generation_ = 0;
    bool stopping_ = false;
};

// --- Backend -------------------------------------------------------------------
enum class Backend : u8 { Cpu = 0, Vulkan = 1 };
[[nodiscard]] bool vulkan_device_available() noexcept;

struct NetSource {
    Blob param;                       ///< texto NUL-terminado
    Blob weights;                     ///< .bin do ncnn
    const char* input = "in0";
    const char* output = "out0";
    bool rifeWarp = false;            ///< registra a camada rife.Warp (RIFE)
    const char* cpuOnlyLayer = nullptr; ///< camada sem kernel Vulkan confiável
    bool lightmode = true;
    /// Pesos lidos de arquivo (modelo sob demanda): o ncnn copia, nada fica
    /// apontando para um buffer nosso. Vazio = `weights`.
    std::string weightsFile;
    /// CPU: pesos/ativações em fp16 onde a CPU tem fp16 (ARMv8.2) — metade
    /// da memória dos modelos grandes.
    bool cpuFp16 = false;
    /// CPU: também a conta em fp16 (só onde a CPU tem; conferido pelo medidor).
    bool cpuFp16Arithmetic = false;
};

/// Uma rede carregada num backend, com volta para a CPU.
class NetRunner {
public:
    NetRunner() = default;
    NetRunner(const NetRunner&) = delete;
    NetRunner& operator=(const NetRunner&) = delete;
    ~NetRunner() { release(); }
    /// `fp16Arithmetic` só vale no Vulkan. Falha no Vulkan com `allowFallback`
    /// = carrega na CPU.
    [[nodiscard]] Status load(const NetSource& source, Backend backend, bool fp16Arithmetic, bool allowFallback) noexcept;
    [[nodiscard]] Status fallback_to_cpu() noexcept;
    void release() noexcept;
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] Backend backend() const noexcept { return backend_; }
    [[nodiscard]] bool can_fallback() const noexcept { return backend_ == Backend::Vulkan && fallback_; }
    [[nodiscard]] ncnn::Net& net() noexcept { return net_; }
    [[nodiscard]] const NetSource& source() const noexcept { return source_; }
    /// Uma inferência entrada → saída (blobs da fonte).
    [[nodiscard]] bool infer(const ncnn::Mat& in, ncnn::Mat& out, ncnn::Allocator* allocator) noexcept;
private:
    ncnn::Net net_;
    NetSource source_{};
    Backend backend_ = Backend::Cpu;
    bool fp16_ = false, fallback_ = false, loaded_ = false, warpRegistered_ = false;
};

// --- Tiles ---------------------------------------------------------------------
struct TileConfig {
    u32 tile = 96;          ///< lado do miolo (entrada)
    u32 margin = 12;        ///< borda calculada e descartada
    u32 netScale = 4;       ///< escala da rede
    u32 outDiv = 1;         ///< média em caixa da saída da rede (x4→x2: 2; Clean: 4)
    bool skipStatic = false;
    bool temporalBlend = false;
    f32 skipMean = 0.75f;   ///< SAD médio (níveis, médias 2×2) para pular
    u32 skipMax = 6;        ///< diferença máxima (médias 2×2)
};

/// Roda uma rede imagem→imagem sobre um quadro RGB8 em tiles, em paralelo nos
/// workers (cada um com o próprio alocador). A saída é PERSISTENTE (o buffer
/// do chamador entre quadros): tile pulado = a saída anterior fica.
class TiledRunner {
public:
    [[nodiscard]] Status prepare(const TileConfig& config, u32 width, u32 height, u32 workers, u64 budgetPerWorker) noexcept;
    void reset_history() noexcept { history_ = false; }
    /// `out` w·netScale/outDiv × h·netScale/outDiv RGB8 (stride = largura·3),
    /// o mesmo buffer a cada quadro.
    [[nodiscard]] Status run(NetRunner& net, WorkerPool& pool, const u8* rgb, u8* out, const StageContext& ctx) noexcept;
    [[nodiscard]] u32 out_width() const noexcept { return outW_; }
    [[nodiscard]] u32 out_height() const noexcept { return outH_; }
    [[nodiscard]] u32 tiles_total() const noexcept { return static_cast<u32>(tiles_.size()); }
    [[nodiscard]] u32 tiles_skipped() const noexcept { return skipped_; }
    [[nodiscard]] u64 peak_bytes() const noexcept;
    [[nodiscard]] u64 history_bytes() const noexcept;
    [[nodiscard]] const TileConfig& config() const noexcept { return cfg_; }
private:
    struct Tile { u32 x, y, w, h; u32 ex0, ey0, ex1, ey1; usize sigOffset; };
    bool is_static_tile(const Tile& t, const u8* rgb) const noexcept;
    void store_signature(const Tile& t, const u8* rgb) noexcept;
    bool run_tile(NetRunner& net, const Tile& t, const u8* rgb, u8* out, u32 worker, bool blend) noexcept;
    TileConfig cfg_{};
    u32 w_ = 0, h_ = 0, outW_ = 0, outH_ = 0;
    std::vector<Tile> tiles_;
    std::vector<u8> signatures_;          ///< médias 2×2 da entrada que gerou cada tile
    std::vector<u8> previous_;            ///< a entrada do quadro anterior (mistura temporal)
    std::vector<u8> stableMask_;          ///< pixels parados (erosão 3×3), da mistura temporal
    std::vector<std::unique_ptr<BoundedAllocator>> allocators_;
    std::vector<std::vector<u8>> scratch_;   ///< entrada de um tile, por worker
    std::vector<std::vector<u8>> fresh_;     ///< saída nova de um tile (mistura), por worker
    std::vector<u8> tileStatus_;
    bool history_ = false, blendFrame_ = false;
    u32 skipped_ = 0;
};

/// Converte um Mat (3 canais, 0..1) de um tile para RGB8 com média em caixa
/// `div`×`div`, escrevendo o retângulo [sx, sx+w)×[sy, sy+h) do Mat (em pixels
/// da SAÍDA) em `dst`.
void mat_to_rgb(const ncnn::Mat& m, u32 div, u32 sx, u32 sy, u32 w, u32 h, u8* dst, u32 dstStride) noexcept;

/// O padrão de referência do medidor (igual a tools/hyper_models.py).
void reference_pattern(u32 w, u32 h, u8* rgb) noexcept;
/// PSNR (dB) entre dois RGB8 do mesmo tamanho; 99 = iguais.
[[nodiscard]] f64 psnr_rgb(const u8* a, const u8* b, usize bytes) noexcept;

/// Catmull-Rom ×2 separável (os 2× finais do 4× em aparelho fraco).
void upscale2_catmull_rom(const u8* src, u32 w, u32 h, u8* dst) noexcept;

} // namespace aurea::hyper::neural
