// =============================================================================
//  Aurea / effects / HeadbopMotion.hpp
//
//  Motor de movimento do efeito "Headbop": a pessoa da camada balança a cabeça
//  na batida da música. Só números (sem GPU): o rig/render consome a `Pose`.
//
//  Modelo: três corpos acoplados — QUADRO (solavanco vertical), TRONCO (balanço
//  lateral + segue o solavanco) e CABEÇA (aceno + inclinação). Cada um segue o
//  anterior por uma mola amortecida, então a cabeça atrasa em relação ao corpo e
//  as extremidades atrasam mais ainda.
//
//  DETERMINÍSTICO: a pose em t depende só de t e dos parâmetros, nunca do
//  histórico de scrub. É a SOMA de respostas fechadas (analíticas) de osciladores
//  amortecidos a cada batida; só as batidas numa janela finita antes de t
//  contam (respostas abaixo de 1e-4 são ignoradas). Nada é integrado quadro a
//  quadro.
//
//  Unidades da Pose: posições em fração da LARGURA do quadro (0.05 = 5% da
//  largura; +y para baixo, +x para a direita), ângulos em graus
//  (pitch + = queixo para baixo; roll + = horário na tela; yaw + = olha para a
//  direita), zoom em fração (0.02 = +2% de escala).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurea::headbop {

/// Grade de batidas. `beats` em segundos, crescente. `strength` (mesmo tamanho
/// que `beats`, ou vazio = tudo 1) é a força relativa de cada batida: mediana 1,
/// faixa [0.25, 2] (a amplitude usa a raiz, então 2 → ×1.41).
/// Se `beats` estiver vazio mas `bpm > 0`, a grade é implícita e infinita:
/// batida k em `phaseSec + k*60/bpm` (k inteiro qualquer, inclusive negativo).
struct BeatGrid {
    std::vector<double> beats;
    std::vector<float> strength;
    double bpm = 0;        ///< tempo médio (BPM); 0 = desconhecido
    double phaseSec = 0;   ///< tempo (s) de uma batida de referência (fase da grade)
};

enum class Preset : uint8_t {
    Bop,     ///< padrão: solavanco + balanço do tronco fora da grade + aceno da cabeça
    Nod,     ///< só a cabeça acena (quadro/tronco parados)
    Sway,    ///< só balanço lateral (tronco + contra-balanço da cabeça)
    Bounce,  ///< só o solavanco diagonal do quadro (tronco/extremidades acompanhando)
    Groove,  ///< ciclo de 2 batidas com giro (yaw) e roll alternando
    Count
};

/// Canais das tabelas de preset (`preset_value(preset, channel)`).
enum Channel : int {
    ChJolt = 0,          ///< componente VERTICAL do solavanco do quadro (fração da largura)
    ChTorsoJolt,         ///< tronco: queda extra relativa ao quadro (fração da largura)
    ChTorsoSway,         ///< tronco: balanço lateral (fração da largura)
    ChSwayPeriod,        ///< período do balanço, em batidas
    ChTorsoRoll,         ///< roll do tronco acompanhando o balanço (graus)
    ChHeadPitch,         ///< aceno da cabeça (graus, pico médio)
    ChHeadRoll,          ///< inclinação (orelha → ombro) no fundo de cada batida, UM sentido só (graus; Groove alterna)
    ChHeadCounter,       ///< deslocamento da cabeça contra o tronco (fração de torsoDx; 1 = parada no mundo)
    ChHeadYaw,           ///< giro (graus), usado pelo Groove
    ChCycleBeats,        ///< ciclo do padrão alternado (1 ou 2 batidas)
    ChAttack,            ///< fração do aceno para descer (0.333 = 1/3); o quadro é um oscilador com ζ = cos(π·attack)
    ChZeta,              ///< amortecimento das molas seguidoras (tronco/cabeça/extremidades)
    ChHeadLagBeats,      ///< atraso da cabeça em relação ao corpo (batidas, a 117.5 BPM ≈ 2 quadros a 30 fps)
    ChExtremityLagBeats, ///< atraso das extremidades (batidas, 0.12)
    // --- 0.5.2 (vídeo de referência); novos canais só no fim ---
    ChJoltAngle,         ///< direção do solavanco (graus a partir de "para baixo"; + = desce para a DIREITA). frameDx = frameDy·tan
    ChHeadDrop,          ///< cabeça: queda extra relativa ao tronco no fundo da batida (fração da largura)
    ChHeadRollSway,      ///< inclinação da cabeça acompanhando o balanço (graus; Sway)
    ChCount
};

/// Todos keyframeáveis no efeito.
struct Params {
    Preset preset = Preset::Bop;
    float intensity = 1;   ///< multiplicador global de amplitude (0..2+)
    float head = 1;        ///< escala dos canais da cabeça (pitch/roll/yaw/headDx/headDy)
    float body = 1;        ///< escala do tronco (torsoDx/torsoDy/torsoRoll)
    float jolt = 1;        ///< escala do solavanco do quadro (frameDy)
    float sway = 1;        ///< escala do balanço lateral (torsoDx/torsoRoll e o contra-balanço da cabeça)
    float turn3d = 0;      ///< quantidade de giro 3D extra (headYaw), 0..1 (Groove já tem giro próprio)
    float delay = 1;       ///< escala dos atrasos (cabeça vs corpo, extremidades); 0 = tudo junto
    float smoothness = 1;  ///< escala da suavidade: >1 = mais amortecido/macio, <1 = mais elástico
    float variation = 1;   ///< escala da aleatoriedade por batida (0 = todas iguais, só força do ataque)
    double phaseBeats = 0; ///< desloca o movimento em batidas (+ = atrasa)
    float subdivision = 1; /*0.5,1,2 beats per nod*/ ///< batidas por aceno: 0.5 (dois acenos por batida), 1, 2
    float beatZoom = 0;    ///< pulso de zoom na batida (fração: 0.03 = +3% no pico)
    double manualBpm = 0;  /*0 = use grid*/ ///< >0 ignora a grade e usa BPM fixo (fase = grid.phaseSec)
    uint32_t seed = 1;     ///< semente do ruído por batida
    // --- 0.5.2 (vídeo de referência); novos campos só no fim ---
    float joltAngle = 0;   ///< graus somados à direção do solavanco do preset (Bop: 23.6° → dx ≈ 0.44·dy); ±80 no total
    float tiltSign = 1;    ///< sentido da inclinação da cabeça: +1 = referência (horário na tela), −1 = espelhado
    // --- 0.5.2: movimento secundário (cabelo/roupa); 0 = a pose de antes, bit a bit ---
    float hair = 0;        ///< quanto o cabelo segue a cabeça com atraso/overshoot (0..2)
    float cloth = 0;       ///< quanto a roupa segue o tronco com atraso/overshoot e ondula (0..2)
    float physics = 1;     ///< 0..2: < 1 mais firme (mola rápida, amortecida), > 1 mais solto (lento, balança mais)
};

/// Pose: posições relativas à LARGURA do quadro, ângulos em graus.
/// Hierarquia: quadro (frameDx/Dy, aplicado à imagem inteira) → tronco
/// (torso*, RELATIVO ao quadro) → cabeça (head*, RELATIVA ao tronco).
struct Pose {
    float frameDy = 0;      ///< deslocamento vertical do quadro inteiro (+ = desce)
    float frameZoom = 0;    ///< zoom extra (fração; escala = 1 + frameZoom)
    float torsoDx = 0;      ///< tronco: lateral relativa ao quadro (+ = direita)
    float torsoDy = 0;      ///< tronco: queda extra relativa ao quadro (+ = desce); menor que a da cabeça
    float torsoRoll = 0;    ///< tronco: rotação na tela (graus, + = horário)
    float headDx = 0;       ///< cabeça: lateral RELATIVA ao tronco (oposta ao balanço)
    float headDy = 0;       ///< cabeça: queda relativa ao tronco (+ = desce)
    float headPitch = 0;    ///< aceno (graus, + = queixo para baixo); pequeno no Bop (~5°)
    float headRoll = 0;     ///< inclinação no plano (orelha → ombro), graus, + = horário na tela; Bop: um sentido só
    float headYaw = 0;      ///< giro (graus)
    float extremityLag = 0; ///< deslocamento vertical das extremidades (mãos/cabelo), mesma unidade de frameDy, atrasado
    float frameDx = 0;      ///< (0.5.2, no fim) deslocamento horizontal do quadro inteiro (+ = direita); o solavanco é diagonal
    // --- 0.5.2: movimento secundário — DIFERENÇAS em relação à cabeça/tronco (0 com hair/cloth = 0) ---
    // O cabelo é uma mola (~3,5 Hz, ζ 0,25) presa à cabeça: atrasa, passa do ponto e balança 1–2 vezes.
    float hairDx = 0;       ///< cabelo − cabeça, lateral (fração da largura)
    float hairDy = 0;       ///< cabelo − cabeça, vertical (fração da largura; inclui o atraso ao solavanco do quadro)
    float hairRoll = 0;     ///< cabelo − cabeça, inclinação (graus)
    float hairPitch = 0;    ///< cabelo − cabeça, aceno (graus)
    // A roupa é uma mola (~2,5 Hz, ζ 0,35) presa ao tronco, mais o assentamento depois do solavanco.
    float clothDx = 0;      ///< roupa − tronco, lateral (fração da largura)
    float clothDy = 0;      ///< roupa − tronco, vertical (fração da largura)
    float clothRoll = 0;    ///< roupa − tronco, rotação (graus)
    float flutterX = 0;     ///< tremulação do cabelo (ruído determinístico em t, −1..1, já × hair)
    float flutterY = 0;
    float wavePhase = 0;    ///< fase da onda de dobra que corre pela camisa (rad)
    float waveAmp = 0;      ///< amplitude da onda (fração da largura, já × cloth)
};

/// Pose no tempo `tSeconds` (tempo do efeito/camada). Pura, forma fechada.
/// Nunca devolve NaN/inf (grade vazia, BPM 0, t não finito → pose neutra).
[[nodiscard]] Pose evaluate(const Params&, const BeatGrid&, double tSeconds);

/// Grade de BPM fixo a partir de `phaseSec` (batidas também antes de phaseSec, a
/// partir de 0) até `durationSec`. Força = 1.
[[nodiscard]] BeatGrid grid_from_bpm(double bpm, double phaseSec, double durationSec);
/// BPM e fase a partir de toques (tap tempo): regressão linear robusta dos toques.
/// Menos de 2 toques válidos → grade vazia (bpm 0).
[[nodiscard]] BeatGrid grid_from_taps(const std::vector<double>& tapTimes, double durationSec);
/// Detecção automática (ataques + tempo) via audio::detect_beats_ex. `mono48k` a 48 kHz.
[[nodiscard]] BeatGrid analyze_audio(const float* mono48k, size_t count);

/// Tabela de presets. `channel` em [0, ChCount). Fora da faixa → 0.
[[nodiscard]] float preset_value(Preset, int channel);

// --- Cache de análise ---------------------------------------------------------

/// Hash de conteúdo (FNV-1a 64 sobre as amostras + contagem). Use como chave do cache.
[[nodiscard]] uint64_t content_hash(const float* mono48k, size_t count);

/// Cache em memória (thread-safe) de análises por hash de conteúdo do áudio.
/// `get_or_analyze` só roda a análise na primeira vez por hash.
class AnalysisCache {
public:
    static AnalysisCache& shared();
    [[nodiscard]] bool get(uint64_t hash, BeatGrid& out) const;
    void put(uint64_t hash, const BeatGrid&);
    [[nodiscard]] BeatGrid get_or_analyze(const float* mono48k, size_t count);
    void clear();
    [[nodiscard]] size_t size() const;
private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, BeatGrid> map_;
};

/// Blob serializável (binário, little-endian, versionado) para persistir a análise.
/// Formato: "HBG1" | u32 versão | u64 hash | f64 bpm | f64 phaseSec | u32 n |
/// n×f64 beats | n×f32 strength.
[[nodiscard]] std::string serialize(const BeatGrid&, uint64_t hash = 0);
/// false se o blob for inválido/truncado/de outra versão (out fica intacto).
[[nodiscard]] bool deserialize(const std::string& blob, BeatGrid& out, uint64_t* hash = nullptr);

// --- Ponto de extensão --------------------------------------------------------

/// Fonte alternativa de pose ("PoseSource" da especificação; o nome PoseSource
/// é o enum do parâmetro em Headbop.hpp). Implementação futura: driver neural de expressão
/// (ex.: pose estimada de um vídeo de referência ou de um modelo de áudio→pose).
/// O render pode aceitar um `PoseDriver*`; se nulo, usa `evaluate()`. Requisito:
/// `at(t)` deve ser determinística em t (scrub/exportação pedem qualquer t em
/// qualquer ordem). Não implementado nesta fase.
struct PoseDriver {
    virtual ~PoseDriver() = default;
    virtual Pose at(double t) = 0;
};

/// Fonte padrão: embrulha evaluate() com params/grade fixos.
struct ProceduralPoseDriver final : PoseDriver {
    Params params;
    BeatGrid grid;
    Pose at(double t) override { return evaluate(params, grid, t); }
};

} // namespace aurea::headbop
