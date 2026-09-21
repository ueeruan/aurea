#include "aurea/render/EffectGraph.hpp"
#include "aurea/core/Log.hpp"

#include <algorithm>

namespace aurea {
namespace {

/// Amostras de textura que um efeito de vizinhança custa. O valor vem da
/// descrição do efeito no registro: efeitos que usam separabilidade (H+V)
/// declaram o número de taps por passada, não o total.
u32 neighborhood_samples(const EffectDesc& desc, bool preview, f32 radius) noexcept {
    u32 base = desc.previewSamples;
    if (!preview || base == 0) base = desc.exportSamples;
    if (base == 0) base = 9;   // kernel 3x3 é o padrão conservador

    // Raio maior exige mais taps: um gaussiano de raio 40 não é bem aproximado
    // com 9 amostras. O crescimento é linear com um piso, porque o gaussiano
    // separável escala com o raio, não com o quadrado dele.
    if (radius > 8.0f) {
        const u32 byRadius = static_cast<u32>(radius) * 2u + 1u;
        const u32 ceiling = preview ? (base / 2u + 1u) : base;
        return byRadius < ceiling ? byRadius : ceiling;
    }
    return base;
}

} // namespace

// -----------------------------------------------------------------------------
// EffectRegistry
// -----------------------------------------------------------------------------
Status EffectRegistry::register_effect(const EffectDesc* description,
                                       EffectClass cls, u32 typeId) noexcept {
    if (!description) return Errc::InvalidArgument;
    if (typeId >= kMaxEffects) return Errc::OutOfRange;
    if (descriptions_[typeId]) return Errc::AlreadyExists;

    descriptions_[typeId] = description;
    classes_[typeId] = cls;
    if (typeId >= count_) count_ = typeId + 1;
    return OkStatus;
}

const EffectDesc* EffectRegistry::description(u32 typeId) const noexcept {
    if (typeId >= kMaxEffects) return nullptr;
    return descriptions_[typeId];
}

EffectClass EffectRegistry::classification(u32 typeId) const noexcept {
    if (typeId >= kMaxEffects) return EffectClass::PerPixel;
    return classes_[typeId];
}

u32 EffectRegistry::find(const char* name) const noexcept {
    if (!name) return kInvalidIndex;
    for (u32 i = 0; i < kMaxEffects; ++i) {
        if (!descriptions_[i]) continue;
        const char* a = descriptions_[i]->name;
        const char* b = name;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') return i;
    }
    return kInvalidIndex;
}

// -----------------------------------------------------------------------------
// EffectCompiler
// -----------------------------------------------------------------------------
bool EffectCompiler::is_neutral(const Effect& effect, u32 effectIndex,
                                const EffectDesc& desc,
                                const TrackSet& tracks, FrameIndex time) noexcept {
    // Um efeito que não muda nada é um passe inteiro de GPU jogado fora. Em
    // 4K, isso é 66 MB de leitura e 66 MB de escrita por frame — por efeito.
    //
    // Regra de segurança: só classifica como neutro quando o parâmetro que
    // controla o efeito está EXATAMENTE no valor neutro, usando o primeiro
    // parâmetro da descrição que tenha neutro conhecido. Se a descrição não
    // declara um neutro, o efeito NÃO é considerado neutro — na dúvida, aplica.
    if (!effect.enabled) return true;

    // Um efeito cujo PRIMEIRO parâmetro é um multiplicador de intensidade em
    // zero não contribui. É o caso do blur com raio 0, do glow com intensidade
    // 0, da vinheta com quantidade 0.
    //
    // Só o primeiro parâmetro é considerado, e só quando a descrição o declara
    // como Float. Sem essa restrição, um efeito cujo primeiro parâmetro é um
    // ângulo ou um enum seria removido ao chegar em 0 — e o frame sairia errado
    // sem ninguém perceber.
    if (desc.paramCount == 0) return false;
    if (desc.params[0].type != EffectParamType::Float) return false;

    const f32 first = tracks.sample_or(
        TrackProperty::EffectParam, time, effect.floats[0], effectIndex, 0);
    return first == 0.0f;
}

Status EffectCompiler::compile(const std::vector<Effect>& effects,
                               const TrackSet& tracks, FrameIndex time,
                               const EffectRegistry& registry,
                               u32 targetWidth, u32 targetHeight,
                               bool preview,
                               EffectPlan& out) noexcept {
    out.stages.clear();
    out.droppedEffects.clear();
    out.fusionBlockers.clear();

    const u64 pixelCount = static_cast<u64>(targetWidth) * static_cast<u64>(targetHeight);

    // -------------------------------------------------------------------------
    // Passo 1: filtrar. Efeito desabilitado ou neutro sai da cadeia inteira.
    // -------------------------------------------------------------------------
    struct LiveEffect {
        u32  effectIndex;
        const EffectDesc* desc;
        EffectClass cls;
        f32  radius;
    };
    std::vector<LiveEffect> live;
    live.reserve(effects.size());

    for (u32 i = 0; i < effects.size(); ++i) {
        const Effect& e = effects[i];
        const EffectDesc* d = registry.description(e.type);
        if (!d) {
            // Efeito desconhecido: o projeto veio de uma versão que tinha um
            // efeito que esta não tem. Registrar como bloqueado é o certo —
            // o renderer pula e a UI mostra "efeito indisponível". Aplicar um
            // passe vazio seria pior: o usuário veria o frame sem o efeito e
            // não saberia por quê.
            out.fusionBlockers.emplace_back(i, "efeito desconhecido nesta versao");
            out.droppedEffects.push_back(i);
            continue;
        }
        if (!e.enabled) { out.droppedEffects.push_back(i); continue; }
        if (is_neutral(e, i, *d, tracks, time)) { out.droppedEffects.push_back(i); continue; }

        // Raio: primeiro parâmetro numérico do efeito. É a convenção do
        // registro — efeitos de vizinhança declaram o raio primeiro.
        f32 radius = 0.0f;
        if (d->paramCount > 0) {
            radius = tracks.sample_or(TrackProperty::EffectParam, time,
                                      e.floats[0], i, 0);
        }

        live.push_back(LiveEffect{i, d, registry.classification(e.type), radius});
    }

    if (live.empty()) {
        return OkStatus;   // nada a fazer: a layer é desenhada direto
    }

    // -------------------------------------------------------------------------
    // Passo 2: agrupar.
    //
    // A regra de fusão, em uma frase: efeitos PerPixel consecutivos viram UM
    // passe. Qualquer outra classe quebra o grupo, porque muda o que é possível
    // afirmar sobre o pixel de saída.
    // -------------------------------------------------------------------------
    usize i = 0;
    while (i < live.size()) {
        const LiveEffect& first = live[i];

        if (first.cls == EffectClass::PerPixel) {
            // Absorve todos os PerPixel consecutivos.
            usize j = i;
            while (j < live.size() && live[j].cls == EffectClass::PerPixel) ++j;

            CompiledEffectStage stage;
            stage.cls = EffectClass::PerPixel;
            stage.stage = PassStage::Effects;
            stage.sampleCount = 1;   // lê o pixel de entrada uma vez
            stage.resolutionScale = 1.0f;

            // Rótulo: mostra o que foi fundido, para o painel de debug poder
            // provar que a fusão aconteceu em vez de só afirmar.
            stage.name = "cor-fundida(";
            stage.name += std::to_string(j - i);
            stage.name += ")";
            for (usize k = i; k < j; ++k) stage.effectIndices.push_back(live[k].effectIndex);

            stage.estimatedCost = pixelCount * stage.sampleCount;
            out.stages.push_back(std::move(stage));
            i = j;
            continue;
        }

        if (first.cls == EffectClass::Neighborhood) {
            // Blur e companhia: separável quando o efeito declara
            // exportSamples, o que significa que ele mesmo implementa a
            // separação. Dois passes (H e V), cada um com metade dos taps de
            // um kernel 2D — 2*N em vez de N².
            //
            // Efeitos de vizinhança CONSECUTIVOS com o mesmo raio fundem: dois
            // blurs de raio igual são um blur de raio dobrado com o mesmo
            // número de taps, não o dobro de passes.
            usize j = i;
            f32 maxRadius = first.radius;
            while (j < live.size() && live[j].cls == EffectClass::Neighborhood
                   && live[j].radius == first.radius) {
                ++j;
            }
            if (j == i + 1) {
                for (usize k = i + 1; k < live.size(); ++k) {
                    if (live[k].cls != EffectClass::Neighborhood) break;
                    if (live[k].radius != maxRadius) break;
                    ++j;
                }
            }

            const bool separable = first.desc->exportSamples >= 8;
            const u32 taps = neighborhood_samples(*first.desc, preview, maxRadius);

            auto make_directional = [&](const char* suffix) {
                CompiledEffectStage stage;
                stage.cls = EffectClass::Neighborhood;
                stage.stage = PassStage::Effects;
                stage.sampleCount = taps;
                // Blur grande roda em resolução menor: visualmente idêntico e
                // 4x mais barato. É o truque padrão de qualquer engine — e o
                // único que faz um blur de raio 100 caber no orçamento do
                // preview num aparelho médio.
                stage.resolutionScale = maxRadius > 16.0f ? 0.5f : 1.0f;
                stage.requiresFloatTarget = true;
                stage.name = first.desc->name;
                stage.name += suffix;
                for (usize k = i; k < j; ++k) stage.effectIndices.push_back(live[k].effectIndex);
                stage.previewSampleCount = preview ? taps : 0;
                stage.estimatedCost =
                    static_cast<u64>(static_cast<f64>(pixelCount)
                                     * static_cast<f64>(stage.resolutionScale * stage.resolutionScale)
                                     * static_cast<f64>(stage.sampleCount));
                out.stages.push_back(std::move(stage));
            };

            if (separable) {
                make_directional("-h");
                make_directional("-v");
            } else {
                make_directional("");
            }

            if (j > i + 1) {
                // Fundidos: registra quantos entraram, para a telemetria.
                out.fusionBlockers.emplace_back(
                    live[i].effectIndex,
                    "varios efeitos de vizinhanca com o mesmo raio: fundidos em 2 passes");
            }
            i = j;
            continue;
        }

        // Temporal, Global, Domain e MatteGenerator exigem passe próprio.
        // Isto é EXPLÍCITO: o compilador nunca finge que fundiu algo que não
        // pode — se pudesse, o export e o preview divergiriam.
        {
            CompiledEffectStage stage;
            stage.cls = first.cls;
            stage.stage = (first.cls == EffectClass::Temporal) ? PassStage::Effects
                        : (first.cls == EffectClass::Domain)   ? PassStage::Transform
                                                               : PassStage::Effects;
            stage.sampleCount = (first.cls == EffectClass::Temporal)
                              ? (preview ? first.desc->previewSamples : first.desc->exportSamples)
                              : 1;
            if (stage.sampleCount == 0) stage.sampleCount = 1;
            stage.resolutionScale = 1.0f;
            stage.requiresHistory = (first.cls == EffectClass::Temporal);
            stage.requiresFloatTarget = (first.cls != EffectClass::Domain);
            stage.previewSampleCount = preview ? stage.sampleCount : 0;
            stage.name = first.desc->name;
            stage.effectIndices.push_back(first.effectIndex);
            stage.estimatedCost = pixelCount * stage.sampleCount;

            const char* reason = nullptr;
            switch (first.cls) {
                case EffectClass::Temporal:
                    reason = "efeito temporal: precisa do frame anterior";
                    break;
                case EffectClass::Global:
                    reason = "efeito global: precisa do frame inteiro";
                    break;
                case EffectClass::Domain:
                    reason = "efeito de dominio: muda a geometria da imagem";
                    break;
                case EffectClass::MatteGenerator:
                    reason = "gerador de matte: alimenta outro efeito";
                    break;
                default:
                    reason = nullptr;
                    break;
            }
            if (reason) out.fusionBlockers.emplace_back(first.effectIndex, reason);

            out.stages.push_back(std::move(stage));
            ++i;
            continue;
        }
    }

    for (const auto& stage : out.stages) {
        if (stage.sampleCount == 0) {
            AUREA_LOG_ERROR("EffectCompiler: etapa '%s' com zero amostras", stage.name.c_str());
            return Errc::InvalidState;
        }
    }

    return OkStatus;
}

} // namespace aurea
