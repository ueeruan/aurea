// =============================================================================
//  Aurea / autoedit / AutoEdit.hpp
//
//  Edição automática (1.0.8). Regras PURAS do planejamento, testadas no host:
//  áudio da fala (envelope de 10 ms, detecção com histerese, respiração),
//  alinhamento das palavras ao som, cortes (pausas por intensidade, hesitações,
//  claquete, muletas, bastidor, gaguejo, falso começo, tomadas repetidas,
//  proteções), mapa do tempo, zoom "Reels", sons (papéis e choque de 0,5 s),
//  flash branco, luz, música e ducking, voz (LUFS, frases, respirações), cor e
//  posição da legenda fora dos rostos.
//
//  A especificação de comportamento (números) está em
//  docs/architecture/AUTOEDIT_108_SPEC.md. Nada aqui lê arquivo nem projeto:
//  Engine::auto_edit (EngineAutoEdit.cpp) junta as entradas, chama estas regras
//  e monta a composição nova. Mesmas entradas → mesma edição (sem relógio, sem
//  sorteio fora das sementes fixas).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aurea::autoedit {

inline constexpr u32 kAnalysisRate = 16000;   ///< fala analisada em 16 kHz mono
inline constexpr u32 kFrameSamples = 160;     ///< quadro de análise: 10 ms
inline constexpr f64 kFrameSec = 0.01;

enum class Intensity : u8 { Light = 0, Medium = 1, Strong = 2 };
enum class Recording : u8 { Retakes = 0, Continuous = 1 };
enum class Language : u8 { Pt = 0, En = 1, Es = 2 };
/// "pt", "pt-BR", "en", "es"… (o resto cai em pt, o padrão da especificação).
[[nodiscard]] Language language_of(std::string_view code) noexcept;

/// Palavra da transcrição, em segundos da MÍDIA (como text::CaptionWord).
struct Word {
    std::string text;
    f64 start = 0.0, end = 0.0;
};

struct Range {
    f64 start = 0.0, end = 0.0;
    [[nodiscard]] f64 length() const noexcept { return end - start; }
};

struct Options {
    Intensity intensity = Intensity::Medium;
    Recording recording = Recording::Retakes;
    Language language = Language::Pt;
    bool cuts = true, zoom = true, sounds = true, music = true, voice = true, color = true, captions = true;
    f64 fps = 30.0;   ///< bordas dos cortes caem no quadro de vídeo
};

// ---------------------------------------------------------------------------
// §1 Análise de áudio
// ---------------------------------------------------------------------------
struct Envelope {
    std::vector<f32> db;     ///< 10·log10(média x² + 1e-18)
    std::vector<f32> hfDb;   ///< o mesmo da diferença entre amostras vizinhas
    std::vector<f32> zcr;    ///< cruzamentos por amostra (16 kHz)
    std::vector<f32> peakDb; ///< pico de amostra do quadro (dBFS)
    [[nodiscard]] usize size() const noexcept { return db.size(); }
};
[[nodiscard]] Envelope analyze_envelope(const f32* mono16k, usize count);

/// Percentil `p` (0..100) por interpolação linear; vazio = -120.
[[nodiscard]] f32 percentile(std::vector<f32> values, f32 p);

/// Sensibilidade da borda 1..7 → dB acima do ruído do vão (padrão 4 = 8 dB).
[[nodiscard]] f32 edge_sensitivity_db(u32 sensitivity) noexcept;

struct Speech {
    f32 noiseDb = -120.0f, levelDb = -120.0f, thresholdDb = -60.0f;
    std::vector<Range> regions;   ///< fala (segundos)
    std::vector<Range> breaths;   ///< respirações
    std::vector<Range> clicks;    ///< ilhas < 60 ms (estalos)
    std::vector<u8> active;       ///< por quadro: 1 = fala
    std::vector<u8> voiced;       ///< por quadro: zcr < 0,1 e dB > max(ruído + 12, nível − 40)
};
[[nodiscard]] Speech detect_speech(const Envelope& env, u32 sensitivity = 4);

// ---------------------------------------------------------------------------
// §2 Alinhamento das palavras ao som
// ---------------------------------------------------------------------------
/// Bordas no ataque/queda do som, duração 80 ms..min(2 s, max(0,6 s, 0,12 s ×
/// letras)) e fantasmas (nenhum quadro acima do limiar) fora. Sem faixa
/// dinâmica (< 10 dB), só a duração máxima e os fantasmas valem.
[[nodiscard]] std::vector<Word> align_words(const std::vector<Word>& words, const Envelope& env);

// ---------------------------------------------------------------------------
// §3 Cortes
// ---------------------------------------------------------------------------
struct PauseRule { f64 gap = 0.30, before = 0.07, after = 0.12; };
[[nodiscard]] PauseRule pause_rule(Intensity i) noexcept;

enum class CutReason : u8 {
    None = 0, Silence, Breath, Hesitation, Slate, Filler, Backstage, Stutter, FalseStart, Retake, Cleanup,
};
[[nodiscard]] const char* cut_reason_name(CutReason r) noexcept;

/// Minúsculas, sem acento (latim) e sem pontuação. "Não," → "nao".
[[nodiscard]] std::string fold_token(std::string_view word);
/// `fold_token` + números por extenso → dígitos ("três" → "3").
[[nodiscard]] std::string normalize_token(std::string_view word, Language lang);
/// ah, ahn, ã, eh, ee, er, erm, hm, hmm, hum, uh, uhm, mm… (e variações).
[[nodiscard]] bool is_hesitation(std::string_view word, Language lang);
/// Igual, ou distância de edição ≤ 1 com 4+ letras.
[[nodiscard]] bool tokens_match(const std::string& a, const std::string& b) noexcept;
[[nodiscard]] u32 edit_distance(std::string_view a, std::string_view b, u32 cap = 3) noexcept;
/// Maior subsequência comum com `tokens_match`.
[[nodiscard]] u32 token_lcs(const std::vector<std::string>& a, const std::vector<std::string>& b);
/// Frases ≤ 10 palavras: negação ou números diferentes, 1–2 termos trocados ou
/// as mesmas palavras em outra ordem → NÃO é tomada repetida.
[[nodiscard]] bool contrast_veto(const std::vector<std::string>& a, const std::vector<std::string>& b, Language lang);

struct Sentence {
    u32 first = 0, count = 0;
    bool complete = false;   ///< termina com ponto final (. ! ? …)
};
/// Frases: ponto final ou vão > 0,7 s.
[[nodiscard]] std::vector<Sentence> split_sentences(const std::vector<Word>& words);

/// Por palavra, o motivo para sair (None = fica): vícios (§3.2), gaguejo e
/// falso começo (§3.3) e, em "retomadas", as tomadas repetidas (§3.4).
[[nodiscard]] std::vector<CutReason> mark_words(const std::vector<Word>& words, const Options& opt);

struct Cut {
    Range range;
    CutReason reason = CutReason::Silence;
};
struct CutPlan {
    std::vector<Range> keep;            ///< trechos mantidos (segundos da mídia, em ordem)
    std::vector<Cut> cuts;              ///< o que sai
    std::vector<CutReason> wordMarks;   ///< por palavra de entrada
    f64 duration = 0.0;
    f64 kept = 0.0;                     ///< segundos mantidos
    bool aborted = false;               ///< trava: sobraria < 8 % (keep = tudo)
};
/// `env`/`speech` podem ser nulos (sem áudio: bordas só pelas palavras).
[[nodiscard]] CutPlan plan_cuts(const std::vector<Word>& words, f64 duration, const Options& opt,
                                const Envelope* env = nullptr, const Speech* speech = nullptr);

/// Mídia ↔ cópia cortada: os trechos mantidos, concatenados.
class TimeMap {
public:
    TimeMap() = default;
    explicit TimeMap(std::vector<Range> keep);
    /// Segundo da mídia → segundo da cópia; < 0 = dentro de um corte.
    [[nodiscard]] f64 to_edit(f64 source) const noexcept;
    /// Segundo da cópia → segundo da mídia.
    [[nodiscard]] f64 to_source(f64 edit) const noexcept;
    [[nodiscard]] f64 duration() const noexcept { return total_; }
    [[nodiscard]] const std::vector<Range>& keep() const noexcept { return keep_; }
    /// Início de cada trecho na cópia (o primeiro é 0).
    [[nodiscard]] const std::vector<f64>& starts() const noexcept { return starts_; }
    /// As emendas (instantes da cópia onde um corte foi feito).
    [[nodiscard]] std::vector<f64> splices() const;
private:
    std::vector<Range> keep_;
    std::vector<f64> starts_;
    f64 total_ = 0.0;
};
/// Palavras no tempo da cópia; coberta ≥ 70 % por corte sai.
[[nodiscard]] std::vector<Word> map_words(const std::vector<Word>& words, const TimeMap& map);

// ---------------------------------------------------------------------------
// §4 Zoom "Reels"
// ---------------------------------------------------------------------------
/// mulberry32 (semente fixa → mesma sequência em toda plataforma).
class Mulberry32 {
public:
    explicit Mulberry32(u32 seed) noexcept : state_(seed) {}
    [[nodiscard]] u32 next_u32() noexcept;
    [[nodiscard]] f64 next() noexcept { return static_cast<f64>(next_u32()) / 4294967296.0; }
private:
    u32 state_;
};

/// Easy Ease 65/65 = cubic-bezier(0,65, 0, 0,35, 1).
inline constexpr f32 kEaseX1 = 0.65f, kEaseY1 = 0.0f, kEaseX2 = 0.35f, kEaseY2 = 1.0f;
[[nodiscard]] f32 easy_ease(f32 t) noexcept;

struct ZoomStrength { f32 punch = 1.12f, push = 1.18f, share = 0.6f; };
/// 0 sutil 106/110 % (0,4), 1 média 112/118 % (0,6), 2 forte 118/126 % (0,75).
[[nodiscard]] ZoomStrength zoom_strength(u32 level) noexcept;

/// Rosto em segundos da MÍDIA ou da cópia (quem chama escolhe); caixa 0..1.
struct Face { f64 time = 0.0; f32 x = 0, y = 0, w = 0, h = 0; };
struct Anchor { f32 x = 0.5f, y = 0.42f; bool face = false; };
/// Maior rosto entre [from, to] (sem amostra no trecho: a mais próxima a ≤ 1,5 s);
/// âncora = centro x, topo + 0,45 × altura. Sem rosto: (0,5, 0,42).
[[nodiscard]] Anchor face_anchor(const std::vector<Face>& faces, f64 from, f64 to);

enum class ZoomType : u8 { None = 0, Intro, Hold, InOut, Push, Track };
[[nodiscard]] const char* zoom_type_name(ZoomType t) noexcept;
struct ZoomKey {
    f64 time = 0.0;      ///< segundos da cópia
    f32 scale = 1.0f;    ///< 1 = 100 %
    f32 ax = 0.5f, ay = 0.42f;   ///< ponto do zoom (fração do quadro)
    bool ease = true;    ///< trecho que SAI deste key: Easy Ease (falso = segura)
};
struct ZoomPiece {
    Range range;          ///< segundos da cópia
    ZoomType type = ZoomType::None;
    f32 peak = 1.0f;
    f64 rampIn = 0.0;     ///< duração da rampa de entrada (0 = corte seco)
    std::vector<ZoomKey> keys;
};
/// Pedaços: clipes da cópia divididos nos inícios de legenda a > 1,5 s das
/// pontas; sem marca no clipe, a cada 2,5 s.
[[nodiscard]] std::vector<Range> zoom_pieces(const std::vector<Range>& clips, const std::vector<f64>& captionStarts);
/// Plano do zoom (semente 7, taxa 0,9, parte ≤ share). `words` no tempo da
/// cópia (acento do push); `faces` no tempo da cópia.
[[nodiscard]] std::vector<ZoomPiece> plan_zoom(const std::vector<Range>& clips, const std::vector<f64>& captionStarts,
                                               const std::vector<Word>& words, const std::vector<Face>& faces,
                                               u32 level = 1);
/// Posição (px) da camada com zoom `scale` no ponto (fx, fy) (px da
/// composição), presa para a borda da camada nunca aparecer. `pos`/`extent`:
/// posição parada e distâncias até as bordas esquerda, direita, cima e baixo
/// (px, em escala 100 %).
struct ZoomPose { f32 x = 0, y = 0; };
[[nodiscard]] ZoomPose zoom_position(f32 posX, f32 posY, const f32 extent[4], f32 scale, f32 fx, f32 fy,
                                     f32 compW, f32 compH) noexcept;

/// Destaque de uma palavra da legenda (viral.js): base 10 + 2·min(letras, 11),
/// número +30, ! ou ? +8, ≤ 3 letras −6, última +6, tudo maiúsculo +6.
[[nodiscard]] i32 highlight_score(std::string_view word, bool last);

// ---------------------------------------------------------------------------
// §5 Sons e flash
// ---------------------------------------------------------------------------
enum class SfxRole : u8 { Woosh = 0, Shutter, Riser, Sub, Flash, CutFlick, Flick, Count };
inline constexpr u32 kSfxRoleCount = static_cast<u32>(SfxRole::Count);
[[nodiscard]] const char* sfx_role_name(SfxRole r) noexcept;
[[nodiscard]] f32 sfx_target_peak(SfxRole r) noexcept;
/// clamp(alvo − pico do arquivo, −40, +12).
[[nodiscard]] f32 sfx_gain_db(SfxRole r, f32 filePeakDb) noexcept;
[[nodiscard]] u32 sfx_priority(SfxRole r) noexcept;

struct SfxEvent {
    f64 time = 0.0;        ///< onde o som COMEÇA (segundos da cópia)
    SfxRole role = SfxRole::CutFlick;
    f32 gainDb = 0.0f;     ///< extra (push: −2 dB) somado ao ganho do papel
    u32 variant = 0;       ///< qual arquivo do papel (sorteio determinístico)
};
/// Até 3 flashes nas emendas, ≥ 6 s entre si, o primeiro no gancho (0,6–4 s).
[[nodiscard]] std::vector<f64> plan_flashes(const std::vector<f64>& splices, f64 duration);
/// Luz: até 6, corte ≥ 3 s dos vizinhos, ≥ 8 s entre luzes.
[[nodiscard]] std::vector<f64> plan_light_leaks(const std::vector<f64>& splices, f64 duration);

struct SfxInputs {
    std::vector<ZoomPiece> zooms;
    std::vector<f64> flashes;
    std::vector<f64> splices;
    std::vector<std::vector<f64>> captionWordStarts;   ///< por legenda, início das palavras (cópia)
    u32 variants[kSfxRoleCount]{};                     ///< arquivos por papel (0 = papel sem som)
    f64 peakOffset[kSfxRoleCount]{};                   ///< onde fica o pico no arquivo (s)
};
/// Papéis, horários, sorteios (sementes 11 e 13) e o choque de 0,5 s.
[[nodiscard]] std::vector<SfxEvent> plan_sfx(const SfxInputs& in);
/// Sons a < 0,5 s mantêm o de maior prioridade (sub e riser isentos).
[[nodiscard]] std::vector<SfxEvent> resolve_sfx_clashes(std::vector<SfxEvent> events);

/// Biblioteca de sons do próprio Aurea: cada papel sintetizado aqui (ruído
/// filtrado, senos e envoltórias; nenhum arquivo de terceiros), 48 kHz mono,
/// determinístico. `variant` 0..kGeneratedVariants−1 muda a cor do som.
inline constexpr u32 kGeneratedVariants = 2;
[[nodiscard]] std::vector<f32> synth_sfx(SfxRole role, u32 variant, u32 rate = 48000);
/// Papel de um arquivo da biblioteca do usuário pelo nome da pasta e do
/// arquivo ("whoosh", "câmera", "clique"…). Count = nenhum papel.
[[nodiscard]] SfxRole sfx_role_of(std::string_view category, std::string_view name);
/// A pasta/arquivo parece música (trilha, música, bgm, beat…).
[[nodiscard]] bool music_like(std::string_view category, std::string_view name);

/// Flash branco: começa 0,13 s antes da emenda; 0 → 100 % (0,13 s) → 100 %
/// (0,17 s) → 0 (0,13 s): 0,43 s ao todo. Devolve os 4 (tempo, opacidade).
struct OpacityKey { f64 time = 0.0; f32 opacity = 0.0f; };
[[nodiscard]] std::vector<OpacityKey> flash_keys(f64 splice);

// ---------------------------------------------------------------------------
// §6 Música e ducking
// ---------------------------------------------------------------------------
struct MusicTrack {
    f32 bpm = 0.0f;          ///< 0 = sem metadado
    f64 duration = 0.0;      ///< segundos
    f32 energyDb = -20.0f;   ///< energia média medida
    bool recent = false;     ///< usada nas últimas 5 edições
    bool music = true;       ///< categoria de música (sem BPM: prefere estas)
};
/// clamp(85 + (palavras/s − 2)·15, 80, 120).
[[nodiscard]] f32 music_target_bpm(f64 wordsPerSecond) noexcept;
[[nodiscard]] f64 music_score(const MusicTrack& t, f64 wordsPerSecond, f64 videoDuration) noexcept;
/// Índice da escolhida (empate: a primeira); −1 = lista vazia.
[[nodiscard]] i32 choose_music(const std::vector<MusicTrack>& tracks, f64 wordsPerSecond, f64 videoDuration);
/// Entrada medida: o primeiro segundo seguido com energia ≥ média − 6 dB.
[[nodiscard]] f64 music_entry(const Envelope& env);
inline constexpr f32 kMusicBaseDb = -15.0f;
inline constexpr f64 kMusicFadeIn = 1.0, kMusicFadeOut = 3.0;
struct GainKey { f64 time = 0.0; f32 db = 0.0f; };
/// Ducking: falas a < 1,15 s viram uma região; −`reductionDb` com ataque
/// 0,25 s e soltura 0,6 s (rampas lineares em dB). Keys em segundos da cópia.
[[nodiscard]] std::vector<GainKey> ducking_keys(const std::vector<Range>& speech, f64 duration,
                                                f32 reductionDb = 8.0f);
/// dB da envoltória no instante `t` (linear entre keys; 0 fora).
[[nodiscard]] f32 gain_at(const std::vector<GainKey>& keys, f64 t) noexcept;
/// Segurança de pico: mistura voz (+ganho) e música (base + ducking); pico >
/// −1,5 dBFS → redução += ceil(pico + 2,5), até 3 vezes. Devolve a redução final.
[[nodiscard]] f32 duck_reduction_for_peak(const std::vector<f32>& voice16k, f32 voiceGainDb,
                                          const std::vector<f32>& music16k, const std::vector<Range>& speech,
                                          f64 duration, f32 reductionDb = 8.0f);

// ---------------------------------------------------------------------------
// §7 Voz
// ---------------------------------------------------------------------------
/// LUFS integrados (BS.1770: K-weighting, blocos de 400 ms, portões −70/−10).
[[nodiscard]] f64 integrated_lufs(const f32* mono, usize count, u32 rate);
/// clamp(−12,5 − LUFS, −6, +15) em passos de 0,5 dB (≈ −14 LUFS no export).
[[nodiscard]] f32 voice_gain_db(f64 lufs) noexcept;
/// Limiar do compressor: p95 da fala + ganho − 8 dB (−26 sem medida).
[[nodiscard]] f32 compressor_threshold_db(f32 speechP95Db, f32 gainDb, bool measured) noexcept;
/// Nível por frase: alvo = mediana; clamp(alvo − nível, −6, +6), 0 se |g| < 1 dB.
[[nodiscard]] std::vector<f32> phrase_gains(const std::vector<f32>& levelsDb);
/// Compressor (2,5:1; ataque 10 ms, soltura 150 ms; make-up automático = a
/// redução mediana na fala) e limitador (pico ≤ −3 dBFS, soltura 50 ms)
/// calculados na análise, por quadro de 10 ms: o ganho (dB) a somar ao
/// volume. `inputDb` (o ganho até o alvo de LUFS) já conta no nível. Sem
/// estágio novo no mixer: vira keyframes de volume (um quadro de vídeo de
/// resolução; o limitador suave do mixer acima de −1 dBFS segura o resto).
struct Dynamics { std::vector<f32> gainDb; f32 makeupDb = 0.0f; f32 maxReductionDb = 0.0f; };
[[nodiscard]] Dynamics voice_dynamics(const Envelope& env, const std::vector<u8>& speech, f32 inputDb, f32 thresholdDb,
                                      f32 ratio = 2.5f, f32 ceilingDb = -3.0f);
/// Ramer–Douglas–Peucker em dB: keys só onde a curva por quadro foge mais que
/// `toleranceDb` da reta entre os keys vizinhos (pontas sempre entram).
[[nodiscard]] std::vector<GainKey> simplify_gain(const std::vector<f32>& perFrameDb, f64 frameSec, f32 toleranceDb);
inline constexpr f32 kBreathDb = -15.0f;
inline constexpr f64 kBreathRamp = 0.03;
/// Voz: −15 dB nas respirações (rampas de 30 ms) e nível por frase, juntos
/// numa envoltória (segundos da mídia). Respiração < 120 ms fica.
[[nodiscard]] std::vector<GainKey> voice_keys(const std::vector<Range>& phrases, const std::vector<f32>& phraseGainsDb,
                                              const std::vector<Range>& breaths);

// ---------------------------------------------------------------------------
// §8 Cor
// ---------------------------------------------------------------------------
struct FrameColor { f32 p2 = 0, p98 = 1, saturation = 0; };
[[nodiscard]] FrameColor frame_color_stats(const u8* rgba, u32 width, u32 height, u32 stride);
/// Já tem cor: a média dos quadros (20/50/80 %) tem p2 ≤ 0,08 e (saturação ≥
/// 0,42 ou p98 − p2 ≥ 0,6). Sem quadro = não sabe (falso).
[[nodiscard]] bool already_graded(const std::vector<FrameColor>& frames) noexcept;
struct ColorGrade { f32 exposure = 0.0f, contrast = 8.0f, saturation = 106.0f; };

// ---------------------------------------------------------------------------
// Legenda fora dos rostos
// ---------------------------------------------------------------------------
struct CaptionBlock {
    Range time;          ///< segundos da cópia
    u32 shot = 0;        ///< trecho (clipe) da cópia: a posição fica estável nele
};
/// Centro vertical (fração da altura) de cada legenda: o padrão `defaultY`, e
/// abaixo/acima do rosto que a caixa tocaria; estável dentro do mesmo trecho;
/// no 9:16 fica na área segura do Reels (0,14..0,70). `boxHeight` = altura da
/// legenda (fração da altura do quadro).
[[nodiscard]] std::vector<f32> caption_positions(const std::vector<CaptionBlock>& blocks, const std::vector<Face>& faces,
                                                 f32 defaultY, f32 boxHeight, bool vertical916);

} // namespace aurea::autoedit
