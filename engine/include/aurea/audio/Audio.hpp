// =============================================================================
//  Aurea / audio / Audio.hpp
//
//  O motor de áudio.
//
//  FORMATO INTERNO ÚNICO: 48 kHz, estéreo, float intercalado. Todo arquivo é
//  convertido para isso uma vez, na entrada (reamostragem sinc + mapa de
//  canais); o mixer, o relógio e o export só conhecem esse formato. A posição
//  de qualquer coisa no tempo é um índice de amostra a 48 kHz — inteiro,
//  exato, sem acumular erro de float em projetos longos.
//
//  Camadas, de baixo para cima:
//
//   AudioDecoderBackend   plataforma (MediaCodec / AudioToolbox / teste):
//                         entrega PCM float no formato nativo do arquivo.
//   AudioBlockCache       blocos de 0,5 s já em 48 kHz estéreo, por asset,
//                         decodificados numa thread própria (ou sob demanda,
//                         no export), com teto de memória e despejo do menos
//                         usado recentemente.
//   AudioMixer            função PURA: snapshot da timeline + blocos → PCM.
//                         O preview e o export chamam a MESMA função: o que se
//                         ouve é o que sai no arquivo, amostra por amostra.
//   AudioEngine           reprodução: thread do mixer → anel de blocos → saída
//                         da plataforma (AAudio / AVAudioEngine). É também o
//                         RELÓGIO MESTRE do playback (MasterClock): o vídeo
//                         segue a amostra que está saindo no alto-falante.
// =============================================================================
#pragma once

#include "aurea/animation/Curve.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/memory/MemoryManager.hpp"
#include "aurea/playback/Playback.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace aurea {

struct Asset;
class VideoSourceFactory;
class Composition;
class Project;

namespace audio {

inline constexpr u32 kMixRate = 48000;
inline constexpr u32 kMixChannels = 2;
/// Bloco do cache: 0,5 s. Pequeno o bastante para um seek decodificar rápido,
/// grande o bastante para o custo fixo de busca no arquivo ficar diluído.
inline constexpr u32 kBlockFrames = kMixRate / 2;

/// Instante (ns) → amostra a 48 kHz, arredondando para baixo. 48000/10⁹ =
/// 6/125000: a conta é exata em inteiros (sem estourar por ~48 anos).
[[nodiscard]] constexpr i64 ns_to_sample(i64 ns) noexcept {
    return ns >= 0 ? ns * 6 / 125000 : -((-ns * 6 + 124999) / 125000);
}
/// Amostra → ns, arredondando para CIMA: ns_to_sample(sample_to_ns(s)) == s.
[[nodiscard]] constexpr i64 sample_to_ns(i64 s) noexcept {
    return s >= 0 ? (s * 125000 + 5) / 6 : -((-s * 125000) / 6);
}
/// Frame da composição → primeira amostra dele. fps racional aproximado pelo
/// f64 da composição: 29,97 → 30000/1001 é recuperado e a conta fica exata.
[[nodiscard]] i64 frame_to_sample(i64 frame, f64 fps) noexcept;

// =============================================================================
// Decoder da plataforma
// =============================================================================
struct AudioStreamInfo {
    u32 sampleRate = 0;
    u32 channels = 0;
    i64 durationUs = 0;
};

class AudioDecoderBackend {
public:
    virtual ~AudioDecoderBackend() = default;
    [[nodiscard]] virtual const AudioStreamInfo& info() const noexcept = 0;
    /// Posiciona em ou antes de `us` (o próximo `read` começa ali).
    [[nodiscard]] virtual Status seek(i64 us) noexcept = 0;
    /// Próximo trecho decodificado: float intercalado nos canais do arquivo,
    /// `ptsUs` = instante da primeira amostra. Vazio + `eos` = fim.
    [[nodiscard]] virtual Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept = 0;
};

// =============================================================================
// Reamostragem (sinc com janela de Kaiser, tabela polifásica)
// =============================================================================
/// Converte `srcFrames` quadros ESTÉREO de `src` (taxa srcRate) em 48 kHz.
/// `srcPos0` é a posição (em quadros da fonte, fracionária) que corresponde à
/// primeira amostra de saída; `step` = srcRate/48000. Amostras fora de
/// [0, srcFrames) contam como silêncio — quem chama entrega margem de
/// `resample_half_width(step)` quadros dos dois lados.
inline constexpr i32 kResampleTaps = 32;   ///< meia janela subindo a taxa, em quadros da fonte
/// Meia janela para esta razão. Descendo a taxa (96 → 48 kHz) o filtro
/// precisa ser mais longo na régua da fonte para manter a mesma banda de
/// transição na régua de saída.
[[nodiscard]] i32 resample_half_width(f64 step) noexcept;
void resample_to_mix(const f32* srcStereo, i64 srcFrames, f64 srcPos0, f64 step, f32* outStereo,
                     u32 outFrames) noexcept;
/// Mapa de canais nativo → estéreo de UM quadro (mono duplica; 5.1 faz downmix
/// ITU com os coeficientes de −3 dB).
void to_stereo(const f32* frame, u32 channels, f32& l, f32& r) noexcept;

// =============================================================================
// Cache de blocos
// =============================================================================
struct AudioBlock {
    std::vector<f32> pcm;   ///< kBlockFrames × 2 (o último bloco pode vir menor, completado com zero)
};

/// Onde o cache acha o arquivo de um asset. O motor registra; o cache só abre.
struct AudioAssetRef {
    std::string path;       ///< já resolvido (absoluto / fd / content://)
    i64 durationSamples = 0;
};

class AudioBlockCache {
public:
    /// `async` = com thread de decode (preview). Sem ela (export), `fetch`
    /// decodifica na hora, na thread de quem chama.
    AudioBlockCache(VideoSourceFactory* factory, u64 budgetBytes, bool async);
    ~AudioBlockCache();

    AudioBlockCache(const AudioBlockCache&) = delete;
    AudioBlockCache& operator=(const AudioBlockCache&) = delete;

    void register_asset(u64 key, AudioAssetRef ref);
    [[nodiscard]] bool knows(u64 key) const;

    /// Bloco pronto, ou nulo (nunca bloqueia; o chamador pede com `want`).
    [[nodiscard]] std::shared_ptr<const AudioBlock> find(u64 key, i64 block) const;
    /// Pede o bloco à thread de decode. `urgency` menor = antes.
    void want(u64 key, i64 block, i64 urgency);
    /// Descarta pedidos pendentes (seek: o que foi pedido para o instante
    /// antigo não interessa mais).
    void clear_wants();
    /// Bloco, decodificando agora se preciso (export / testes). Nulo = asset
    /// ilegível.
    [[nodiscard]] std::shared_ptr<const AudioBlock> fetch(u64 key, i64 block);

    struct Stats {
        u32 blocks = 0;
        u64 bytes = 0;
        u64 decoded = 0;
        u64 seeks = 0;
        u64 failures = 0;
        f32 decodeMsPerSecond = 0.0f;   ///< custo de decode por segundo de áudio
    };
    [[nodiscard]] Stats stats() const;

private:
    struct Reader;
    struct Key {
        u64 asset;
        i64 block;
        bool operator==(const Key& o) const noexcept { return asset == o.asset && block == o.block; }
    };
    struct KeyHash {
        usize operator()(const Key& k) const noexcept { return static_cast<usize>(k.asset * 1000003u ^ static_cast<u64>(k.block)); }
    };
    struct Want {
        Key key;
        i64 urgency;
    };

    [[nodiscard]] std::shared_ptr<const AudioBlock> decode_block(u64 key, i64 block);
    void insert(const Key& k, std::shared_ptr<const AudioBlock> b);
    void thread_main();

    VideoSourceFactory* factory_;
    u64 budget_;
    struct Entry {
        std::shared_ptr<const AudioBlock> block;
        mutable u64 lastUse = 0;
    };
    mutable u64 useClock_ = 0;

    mutable std::mutex mutex_;          ///< blocos, assets, pedidos
    std::condition_variable wake_;
    std::unordered_map<Key, Entry, KeyHash> blocks_;
    std::unordered_map<u64, AudioAssetRef> assets_;
    std::vector<Want> wants_;
    Stats stats_{};
    f64 decodeMsTotal_ = 0.0, decodedSeconds_ = 0.0;

    std::mutex readerMutex_;            ///< decoders: um por asset, só uma thread decodifica por vez
    std::unordered_map<u64, std::unique_ptr<Reader>> readers_;

    bool async_ = false;
    bool running_ = false;
    std::thread thread_;
};

// =============================================================================
// Mixer
// =============================================================================
/// Uma fonte de som na timeline, já achatada (pré-comps resolvidas).
///
/// Duas réguas de tempo: a da timeline RAIZ (o que o mixer percorre) e a da
/// composição onde a layer mora (onde vivem os fades e os keyframes de
/// volume). `envShift` converte: amostra da raiz − envShift = amostra na
/// composição da layer.
struct AudioClip {
    u64 asset = 0;
    i64 start = 0;          ///< amostra da timeline raiz onde o clipe começa (inclusive)
    i64 end = 0;            ///< exclusive
    i64 sourceAt0 = 0;      ///< amostra da FONTE que toca em `start`
    /// Velocidade (amostras da fonte por amostra da timeline; negativa =
    /// reverso). ≠ 1: leitura fracionária (varispeed — o tom acompanha a
    /// velocidade; preservar o tom é time stretch, fase seguinte).
    f64 rate = 1.0;
    f64 sourceStartF = 0.0; ///< amostra (fracionária) da fonte em `start` quando rate ≠ 1
    i64 sourceLength = 0;   ///< fim da mídia (além disso é silêncio)
    f32 gain = 1.0f;        ///< ganho do clipe (linear) × ganhos das pré-comps
    f32 pan = 0.0f;         ///< balanço −1..1
    i64 envShift = 0;
    i64 fadeFrom = 0;       ///< início da layer, na régua da composição dela
    i64 fadeTo = 0;         ///< fim da layer, idem
    i64 fadeIn = 0;         ///< em amostras, igual potência
    i64 fadeOut = 0;
    /// Volume animável (linear), por frame da composição da layer; vazio =
    /// constante `volume`.
    f32 volume = 1.0f;
    std::vector<f32> volumeByFrame;
    i64 volumeFrame0 = 0;   ///< frame (composição da layer) de volumeByFrame[0]
    f64 fps = 30.0;         ///< fps da composição da layer
    /// Remapeamento de tempo: amostra (fracionária) da fonte em cada quadro
    /// da layer a partir de `srcFrame0` (inclusive o do fim). Vazio = `rate`.
    std::vector<f64> srcByFrame;
    i64 srcFrame0 = 0;
};

struct AudioMixSnapshot {
    std::vector<AudioClip> clips;
    i64 endSample = 0;      ///< fim da composição
    u64 revision = 0;
    [[nodiscard]] bool audible() const noexcept { return !clips.empty(); }
};

/// Constrói o snapshot da composição (sob o lock do modelo). `assetPath`
/// resolve o caminho guardado para o que o decoder abre. Registra os assets
/// no cache.
using AssetPathResolver = std::string (*)(void* ctx, const std::string& stored);
[[nodiscard]] std::shared_ptr<AudioMixSnapshot> build_snapshot(const Composition& comp, const Project& project,
                                                               AudioBlockCache* cache, AssetPathResolver resolve,
                                                               void* resolveCtx);

/// De onde o mixer tira os blocos.
class BlockSource {
public:
    virtual ~BlockSource() = default;
    /// Nulo = ainda não há (conta como silêncio e como "atraso" nas estatísticas).
    [[nodiscard]] virtual const AudioBlock* block(u64 asset, i64 block) = 0;
};

struct MixStats {
    u32 missingBlocks = 0;
    f32 peak = 0.0f;
};

/// Mixa `frames` amostras a partir de `start` (amostra da timeline) em
/// `outStereo`. Pura e determinística: mesma entrada, mesmos bits.
void mix(const AudioMixSnapshot& snap, i64 start, u32 frames, BlockSource& blocks, f32* outStereo,
         MixStats* stats = nullptr) noexcept;

/// Blocos que o trecho [start, start+frames) vai precisar (para pedir antes).
void blocks_needed(const AudioMixSnapshot& snap, i64 start, i64 frames,
                   std::vector<std::pair<u64, i64>>& out);

/// Float → PCM 16 bits com arredondamento e saturação.
void to_pcm16(const f32* in, usize samples, i16* out) noexcept;

// =============================================================================
// Saída da plataforma
// =============================================================================
/// Chamado na thread de áudio de tempo real: preencher `frames` quadros
/// estéreo. Nada de lock, alocação ou log aqui dentro.
using AudioRenderFn = void (*)(void* ctx, f32* outStereo, u32 frames);

class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    [[nodiscard]] virtual Status open(AudioRenderFn fn, void* ctx) noexcept = 0;
    [[nodiscard]] virtual Status start() noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void close() noexcept = 0;
    /// Quadros que JÁ SAÍRAM no alto-falante no instante `nowNs` (relógio
    /// monotônico). false = a plataforma ainda não sabe (logo depois do start).
    [[nodiscard]] virtual bool presented(u64 nowNs, i64& frames) noexcept = 0;
    /// Latência estimada (buffer da saída), em quadros — usada quando
    /// `presented` não está disponível.
    [[nodiscard]] virtual u32 latency_frames() const noexcept = 0;
};

// =============================================================================
// Waveform (picos multirresolução)
// =============================================================================
/// Picos de um asset, calculados UMA vez numa thread de fundo e guardados em
/// níveis (cada nível é o máximo de dois do anterior). O zoom da timeline só
/// escolhe o nível — pinça não recalcula nada.
///
/// Valor: pico absoluto de L/R no balde, em u8 com compansão raiz (255·√pico):
/// fala baixa, respiração e silêncio ficam visíveis sem o pico estourar a
/// altura. A UI desenha a altura direto do valor.
///
/// Fase 8 (§12, §14–15): orçamento em bytes (categoria Waveforms), LRU pela
/// última consulta, métricas e versão. "Waveform antiga" (segundo estágio da
/// pressão do sistema) = pronta e sem consulta há mais de 2 s — a que está
/// na tela é consultada a cada redesenho e fica.
class WaveformCache final : public IMemoryReclaimable {
public:
    static constexpr u32 kBaseSamples = 240;   ///< 200 baldes por segundo no nível 0
    /// Consulta mais recente que isto não é "antiga" (está na tela).
    static constexpr u64 kRecentNs = 2'000'000'000ull;
    /// Teto sem orçamento ligado (testes).
    static constexpr u64 kDefaultBudget = 8ull << 20;

    explicit WaveformCache(VideoSourceFactory* factory);
    ~WaveformCache() override;

    WaveformCache(const WaveformCache&) = delete;
    WaveformCache& operator=(const WaveformCache&) = delete;

    /// Liga ao orçamento (categoria Waveforms). Nulo desliga.
    void attach(MemoryManager* memory) noexcept;

    /// Enfileira o asset (nada acontece se já está pronto ou na fila).
    void request(u64 key, const AudioAssetRef& ref);
    /// `count` baldes de `samplesPerBucket` amostras (48 kHz) a partir de
    /// `srcStart` (amostra da FONTE). Baldes ainda não calculados ou fora da
    /// mídia saem 0. false = asset desconhecido/ilegível.
    [[nodiscard]] bool query(u64 key, f64 srcStart, f64 samplesPerBucket, u32 count, u8* out) const;
    /// Fração pronta (0..1); −1 = desconhecido.
    [[nodiscard]] f32 progress(u64 key) const;
    /// Muda quando chega pedaço novo (a UI redesenha).
    [[nodiscard]] u32 generation() const noexcept { return generation_.load(std::memory_order_acquire); }

    /// Projeto fechado: tudo sai (as chaves são ids do projeto).
    void clear();
    [[nodiscard]] u64 bytes() const;
    [[nodiscard]] u32 entry_count() const;

    // IMemoryReclaimable
    [[nodiscard]] MemoryClass memory_class() const noexcept override { return MemoryClass::Waveforms; }
    [[nodiscard]] usize reclaim(usize targetBytes) noexcept override;
    [[nodiscard]] const char* debug_name() const noexcept override { return "waveform"; }
    [[nodiscard]] bool accounts_itself() const noexcept override { return true; }
    [[nodiscard]] bool metrics(CacheMetrics& out) const noexcept override;

private:
    struct Entry {
        AudioAssetRef ref;
        std::vector<std::vector<u8>> levels;   ///< levels[0] tem `total` baldes
        i64 total = 0;
        i64 ready = 0;                          ///< baldes do nível 0 já calculados
        bool done = false;
        bool failed = false;
        u64 bytes = 0;                          ///< contado no orçamento
        mutable u64 lastQueryNs = 0;
    };
    void thread_main();
    void account_locked(Entry& e) noexcept;
    void erase_locked(std::unordered_map<u64, Entry>::iterator it) noexcept;
    /// Despeja pela consulta mais antiga até caber (nunca a que está sendo
    /// calculada nem uma consultada nos últimos `kRecentNs`).
    void enforce_budget_locked() noexcept;
    [[nodiscard]] u64 budget_locked() const noexcept;

    VideoSourceFactory* factory_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::unordered_map<u64, Entry> entries_;
    std::vector<u64> queue_;
    std::atomic<u32> generation_{1};
    MemoryManager* memory_ = nullptr;
    u64 bytes_ = 0;
    u64 activeKey_ = 0;
    bool activeValid_ = false;
    mutable u64 hits_ = 0, misses_ = 0;
    u64 evictions_ = 0;
    u32 version_ = 0;
    bool quit_ = false;
    std::thread thread_;
};

// =============================================================================
// Reprodução
// =============================================================================
class AudioEngine final : public MasterClock {
public:
    AudioEngine() = default;
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// `output` nulo = sem som (host/sem permissão): o relógio do sistema
    /// continua mandando. Não assume a posse.
    void initialize(VideoSourceFactory* factory, AudioOutput* output, u64 cacheBudgetBytes);
    void shutdown();

    [[nodiscard]] AudioBlockCache* cache() noexcept { return cache_.get(); }

    /// Troca o que está na timeline (depois de uma edição). Tocando, vale a
    /// partir do próximo bloco mixado (~10 ms).
    void set_snapshot(std::shared_ptr<const AudioMixSnapshot> snap);

    /// Começa a tocar do instante `ns` da timeline (play, seek tocando, loop).
    void play(i64 ns);
    void stop();
    [[nodiscard]] bool playing() const noexcept { return playing_.load(std::memory_order_acquire); }

    /// Com o motor parado: pede os blocos em volta do playhead (o play começa
    /// com o som já decodificado).
    void prefetch(i64 ns);

    // MasterClock
    [[nodiscard]] bool available() const noexcept override;
    [[nodiscard]] i64 position_ns() const noexcept override;

    struct Stats {
        bool outputOpen = false;
        bool playing = false;
        u64 underruns = 0;          ///< callbacks que acharam o anel vazio
        u64 missingBlocks = 0;      ///< trechos mixados sem o bloco pronto
        u32 queuedMs = 0;
        f32 peak = 0.0f;            ///< pico do último bloco mixado
        i32 lastSyncErrorUs = 0;
    };
    [[nodiscard]] Stats stats() const noexcept;

    /// Teste: roda a saída "na mão" (sem thread de áudio da plataforma).
    void debug_render(f32* out, u32 frames) noexcept { render(out, frames); }

private:
    static void render_cb(void* ctx, f32* out, u32 frames) { static_cast<AudioEngine*>(ctx)->render(out, frames); }
    void render(f32* out, u32 frames) noexcept;
    void mixer_main();

    // Anel SPSC de blocos mixados (produtor: thread do mixer; consumidor:
    // callback de áudio). Cada bloco carrega a geração: um seek troca a
    // geração e o callback descarta o que sobrou da anterior sem lock.
    static constexpr u32 kChunkFrames = 256;
    static constexpr u32 kRingChunks = 64;
    struct Chunk {
        u64 gen = 0;
        i64 start = 0;
        f32 pcm[kChunkFrames * kMixChannels];
    };
    std::unique_ptr<std::array<Chunk, kRingChunks>> ring_;
    std::atomic<u32> head_{0};    ///< próximo a escrever (produtor)
    std::atomic<u32> tail_{0};    ///< próximo a ler (consumidor)
    u32 readOffset_ = 0;          ///< consumidor: quadros já lidos do bloco em `tail_`
    bool needRebase_ = false;     ///< consumidor: houve buraco, re-ancorar o relógio

    AudioOutput* output_ = nullptr;
    bool outputOpen_ = false;
    std::unique_ptr<AudioBlockCache> cache_;

    std::mutex mutex_;            ///< snapshot, geração, estado do mixer
    std::condition_variable wake_;
    std::shared_ptr<const AudioMixSnapshot> snap_;
    std::atomic<u64> gen_{1};
    i64 mixPos_ = 0;              ///< próxima amostra a mixar (thread do mixer)
    u64 mixGen_ = 0;
    std::atomic<bool> playing_{false};
    bool quit_ = false;
    std::thread mixer_;

    // Relógio: a amostra `baseTimeline_` saiu (sai) no quadro `baseWritten_`
    // da saída. Escrito pelo callback, lido pelo render (seqlock).
    std::atomic<u64> clockSeq_{0};
    std::atomic<i64> baseWritten_{0};
    std::atomic<i64> baseTimeline_{0};
    std::atomic<u64> baseGen_{0};
    std::atomic<i64> written_{0};  ///< quadros entregues à saída desde o open
    std::atomic<i64> playStartNs_{0};

    std::atomic<u64> underruns_{0};
    std::atomic<u64> missing_{0};
    std::atomic<u32> peakBits_{0};
};

} // namespace audio
} // namespace aurea
