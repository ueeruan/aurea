// =============================================================================
//  Datamosh (Glitch) — o visual de compressão quebrada: os pixels de um quadro
//  ANTIGO continuam sendo arrastados pelo movimento do quadro ATUAL, em blocos
//  (macroblocos), como quando um vídeo perde os quadros-chave (I-frames) e os
//  quadros de previsão (P-frames) passam a mover a imagem errada.
//
//  Comportamento (especificado a partir da descrição pública da técnica de
//  "datamosh" — remoção de I-frame / sangria de P-frame; nenhum código,
//  shader ou asset de terceiros):
//
//   - REFERÊNCIA: a fonte da camada num instante anterior, buscada pelo
//     renderer como no Detectar movimento (`Effect::wants_history`). Ela fica
//     PRESA por "Quadros segurados": no quadro local n o renderer pede a fonte
//     em n − ((n mod Q) + 1), então a referência é a mesma durante Q quadros e
//     depois renova (o "quadro-chave" volta e a imagem limpa de novo). Tudo é
//     função do quadro: o mesmo quadro dá a mesma imagem no preview, no scrub
//     e no export.
//
//   - VETORES POR BLOCO (passe 1, uma textura do tamanho da grade): para cada
//     bloco do quadro atual, a busca acha o deslocamento que melhor o
//     reconstrói a partir da referência (soma das diferenças de luminância em
//     16 pontos do bloco; busca grossa ±2 blocos e fina ±¼ de bloco; o vetor
//     zero ganha um desconto, como o "skip" dos codecs). É o movimento
//     ACUMULADO desde a referência.
//
//   - RECONSTRUÇÃO SEM RESÍDUO (passe 2): cada pixel percorre o campo de
//     vetores em 6 passos (o P-frame repetido) e pega a cor da REFERÊNCIA lá.
//     Onde nada se mexe a imagem velha fica; onde se mexe ela é arrastada e
//     derrete nas bordas dos blocos. "Arraste" > 1 exagera o vetor (bloom).
//
//   - Corrupção: blocos sorteados (hash de bloco, semente e par de quadros)
//     viram cor chapada (só o DC do bloco), um bloco de outro lugar, ou canais
//     trocados e posterizados.
//   - Sangria de cor: a crominância vem de mais adiante no caminho do vetor
//     que a luminância (a cor escorre atrás do movimento).
//   - Intensidade: mistura com a camada original.
//
//  Sem passado (imagem, texto, forma, pré-composição, ou vídeo sem quadro
//  anterior ainda): o campo de vetores vem de RUÍDO coerente por bloco, que
//  cresce ao longo dos Q quadros segurados e muda a cada renovação — a camada
//  parada também "derrete" em blocos, em vez de o efeito não fazer nada.
//
//  Custo: a busca roda numa textura do tamanho da GRADE (no máximo ~16 mil
//  blocos; blocos pequenos demais crescem para caber), 16 amostras × ~106
//  candidatos por bloco; o passe final lê 6 vetores e 2–3 texels por pixel.
//  Um único quadro de histórico por camada.
// =============================================================================
#include "BuiltinEffects.hpp"

#include <algorithm>
#include <cmath>

namespace aurea::builtin {
namespace {

f32 fin(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? v : fallback; }
f32 clampf(f32 v, f32 lo, f32 hi, f32 fallback) noexcept { return std::clamp(fin(v, fallback), lo, hi); }

/// Teto de blocos da grade (custo da busca nos celulares).
constexpr f32 kMaxBlocks = 16384.0f;

class Datamosh final : public Effect {
public:
    enum : u32 { kAmount = 0, kBlockSize, kHoldFrames, kDrag, kCorruption, kColorBleed, kSeed };

    const EffectInfo& info() const noexcept override {
        // Domain, não Temporal: o quadro solto (prévia do catálogo, imagem parada)
        // também mostra o efeito, pelo campo de ruído.
        static const EffectInfo i{effect_keys::kDatamosh, "Datamosh", "Glitch", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("amount", "Intensidade", 100.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("block_size", "Tamanho do bloco", 16.0f, 4.0f, 64.0f, kParamAnimatable | kParamPixels, "px");
        p.typed_range(2.0f, 256.0f);
        // O renderer lê este parâmetro pelo id ("hold_frames") para buscar a referência.
        p.add_int("hold_frames", "Quadros segurados", 12, 1, kDatamoshMaxHold);
        p.add_float("drag", "Arraste", 1.0f, 0.0f, 4.0f, kParamAnimatable, "x");
        p.typed_range(0.0f, 20.0f);
        p.add_float("corruption", "Corrupção", 10.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_float("color_bleed", "Sangria de cor", 30.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.add_int("seed", "Semente", 1, 0, 9999);
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kBlockSize] = ParamValue::scalar(24.0f);
        v[kDrag] = ParamValue::scalar(2.0f);
        v[kCorruption] = ParamValue::scalar(35.0f);
        v[kColorBleed] = ParamValue::scalar(60.0f);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat work) const override {
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_datamosh_vectors_frag, work));
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_datamosh_frag, work));
    }
    bool wants_history() const noexcept override { return true; }
    bool is_identity(const EffectEval& e) const noexcept override { return !(fin(e.f(kAmount), 100.0f) > 0.01f); }
    f32 input_margin(const EffectEval& e) const noexcept override {
        // O arraste lê até ~2 blocos × arraste além do pixel; a corrupção, ±8 blocos.
        const f32 block = clampf(e.f(kBlockSize), 2.0f, 256.0f, 16.0f);
        const f32 drag = clampf(e.f(kDrag), 0.0f, 20.0f, 1.0f);
        return std::min(512.0f, block * (2.5f * drag + 1.0f));
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        if (!input.valid()) return Errc::InvalidArgument;
        const LayerImage& past = ctx.history();
        const bool hasPast = past.valid();

        // Grade em texels de trabalho; bloco pequeno demais cresce até caber no teto.
        const f32 W = static_cast<f32>(std::max<u32>(input.width, 1));
        const f32 H = static_cast<f32>(std::max<u32>(input.height, 1));
        f32 block = clampf(e.f(kBlockSize), 2.0f, 256.0f, 16.0f) * std::max(input.texel_scale_x(), 1e-3f);
        block = std::max(block, 2.0f);
        block = std::max(block, std::sqrt(W * H / kMaxBlocks));
        const u32 bw = std::max<u32>(1, static_cast<u32>(std::ceil(W / block)));
        const u32 bh = std::max<u32>(1, static_cast<u32>(std::ceil(H / block)));
        const Vec4 grid{static_cast<f32>(bw), static_cast<f32>(bh), block / W, block / H};

        const i64 frame = e.localTime.value;
        const i32 hold = std::clamp<i32>(static_cast<i32>(std::lround(fin(e.f(kHoldFrames), 12.0f))), 1, kDatamoshMaxHold);
        const i64 phase = ((frame % hold) + hold) % hold;           // 0..hold-1
        const i64 epoch = (frame - phase) / hold;                    // renovação da referência
        const f32 progress = static_cast<f32>(phase + 1) / static_cast<f32>(hold);
        const u32 seed = std::min<u32>(e.e(kSeed), 9999u);
        const Vec4 uvPast = hasPast ? EffectBuildContext::uv_map(input.region, past.region) : Vec4{1, 1, 0, 0};

        // --- Passe 1: um vetor por bloco -------------------------------------
        struct {
            Vec4 uvMap;   // uv da entrada → uv da referência
            Vec4 grid;    // xy blocos, zw tamanho do bloco em uv
            Vec4 a;       // x 1 = há passado, y desconto do vetor zero, z magnitude sem passado (blocos), w semente
            Vec4 b;       // x época (renovação), yzw livres
        } vu{};
        vu.uvMap = uvPast;
        vu.grid = grid;
        vu.a = Vec4{hasPast ? 1.0f : 0.0f, 0.85f, 0.6f + 1.6f * progress, static_cast<f32>(seed)};
        vu.b = Vec4{static_cast<f32>(epoch & 0xFFFF), 0, 0, 0};
        const FGTexture vectors = ctx.texture("datamosh-vetores", bw, bh);
        const FGTexture refTex = hasPast ? past.texture : input.texture;
        if (ctx.fullscreen_pass("datamosh-vetores", PassStage::Effects, vectors, ShaderId::effects_datamosh_vectors_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
                                 PassTexture{refTex, {}, CommonSampler::LinearClamp}},
                                &vu, sizeof(vu)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }

        // --- Passe 2: reconstrução pela referência ---------------------------
        struct {
            Vec4 uvMap;   // uv da entrada → uv da referência (identidade sem passado)
            Vec4 grid;
            Vec4 a;       // x intensidade, y arraste, z corrupção, w sangria de cor
            Vec4 b;       // x semente, y época da corrupção (par de quadros)
        } fu{};
        fu.uvMap = uvPast;
        fu.grid = grid;
        fu.a = Vec4{clampf(e.f(kAmount), 0.0f, 100.0f, 100.0f) / 100.0f, clampf(e.f(kDrag), 0.0f, 20.0f, 1.0f),
                    clampf(e.f(kCorruption), 0.0f, 100.0f, 10.0f) / 100.0f,
                    clampf(e.f(kColorBleed), 0.0f, 100.0f, 30.0f) / 100.0f};
        fu.b = Vec4{static_cast<f32>(seed), static_cast<f32>((((frame % 131072) + 131072) % 131072) / 2), 0, 0};
        out = LayerImage{ctx.texture("datamosh", input.width, input.height), input.region, input.width, input.height};
        if (ctx.fullscreen_pass("datamosh", PassStage::Effects, out.texture, ShaderId::effects_datamosh_frag,
                                {PassTexture{input.texture, {}, CommonSampler::LinearClamp},
                                 PassTexture{refTex, {}, CommonSampler::LinearClamp},
                                 PassTexture{vectors, {}, CommonSampler::NearestClamp}},
                                &fu, sizeof(fu)) == kInvalidIndex) {
            return Errc::PipelineCompileFailed;
        }
        return OkStatus;
    }
};

} // namespace

void register_datamosh_effect(EffectRegistry& r) { (void)r.add(std::make_unique<Datamosh>()); }

} // namespace aurea::builtin
