// =============================================================================
//  Aurea / render / HeavyQuality.hpp
//
//  Os botões de qualidade dos SISTEMAS PESADOS (Fase 8E) num lugar só: o
//  preview adaptativo (8C, AUTO) e o calor (ThermalManager) mexem aqui; cada
//  sistema lê o seu. O export (finalQuality) usa SEMPRE `full()` — nada daqui
//  muda o arquivo final (SPEC §8).
//
//  Por que um bloco e não o `heavyScale` solto: cada sistema reduz de um jeito
//  (partícula = contagem, flow = resolução da pirâmide, sombra = tamanho do
//  mapa e filtro, efeito = amostras). Com um número só, o AUTO não consegue
//  derrubar a sombra sem derrubar as partículas; com o bloco, consegue — e o
//  `from_scale` continua dando o mesmo resultado de antes para quem só passa a
//  escala.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <algorithm>

namespace aurea {

struct HeavyQuality {
    /// Fração das partículas desenhadas (0,05..1). A distribuição não muda:
    /// os slots que sobram são os primeiros, então o emissor só fica "ralo".
    f32 particles = 1.0f;
    /// Escala da base da pirâmide do optical flow (0,25..1; 1 = 384 px no lado
    /// maior). A deformação continua na resolução do vídeo.
    f32 flow = 1.0f;
    /// Amostras do desfoque vetorial (pelo fluxo), 4..16.
    u32 flowBlurSamples = 16;
    /// Mapa de sombra do 3D (lado, 512..4096) e nível da sombra (cada amostra
    /// é um PCF 2×2 do sampler de comparação):
    ///   0 BAIXO   PCF 8, raio fixo              (mapa até 1024)
    ///   1 MÉDIO   PCF 16, raio fixo             (2048)
    ///   2 ALTO    PCSS: 12 bloqueadores + 24    (2048; preview cheio)
    ///   3 ULTRA   PCSS: 16 bloqueadores + 32    (4096 no export)
    u32 shadowMapSize = 2048;
    u32 shadowFilter = 2;
    /// Multiplica o tamanho projetado na escolha do LOD (< 1 = troca antes
    /// para a malha simplificada). O export usa 1.
    f32 lodBias = 1.0f;
    /// Fração das amostras dos efeitos caros (desfoque de lente, raios): 0,25..1.
    f32 effects = 1.0f;
    /// Quadro de export: nada depende do histórico (o LOD escolhe sem a faixa
    /// de histerese do preview — o mesmo quadro sai igual sozinho ou em série).
    bool exportFrame = false;
    /// AA e pós do 3D (AUTO): MSAA pedido (o aparelho corta), FXAA quando não
    /// há MSAA, e a cadeia do bloom (resolução inicial 1/`bloomStartDiv`,
    /// `bloomLevels` níveis). O padrão — o do export — é o nível ALTO.
    u32 msaaSamples = 4;
    bool fxaa = false;
    u32 bloomStartDiv = 2;
    u32 bloomLevels = 6;

    [[nodiscard]] static constexpr HeavyQuality full() noexcept { return HeavyQuality{}; }

    /// A escada do AUTO/calor a partir da escala única (1 = completo). Mantém o
    /// que já valia antes da 8E (partículas × escala, flow × escala) e desce o
    /// resto junto; `previewDen` (1, 2, 4, 8) reduz o mapa de sombra na mesma
    /// razão do preview — um mapa de 2048 num preview de 1/4 gasta memória e
    /// banda sem aparecer.
    [[nodiscard]] static HeavyQuality from_scale(f32 heavyScale, u32 previewDen = 1) noexcept {
        HeavyQuality q;
        const f32 s = std::clamp(heavyScale, 0.05f, 1.0f);
        q.particles = s;
        q.flow = std::clamp(s, 0.25f, 1.0f);
        q.flowBlurSamples = s >= 0.75f ? 16u : (s >= 0.4f ? 8u : 4u);
        u32 shadow = s >= 0.75f ? 2048u : (s >= 0.4f ? 1024u : 512u);
        const u32 den = std::max(1u, previewDen);
        shadow = std::max(512u, std::min(shadow, 2048u / std::min(den, 4u)));
        q.shadowMapSize = shadow;
        q.shadowFilter = s >= 0.75f ? 2u : (s >= 0.4f ? 1u : 0u);
        q.lodBias = s >= 0.75f ? 1.0f : (s >= 0.4f ? 0.75f : 0.5f);
        q.effects = std::clamp(s, 0.25f, 1.0f);
        // 3D: 4× com o preview cheio, 2× no meio; abaixo disso, 1 amostra +
        // FXAA (o custo do MSAA é banda de tile — o que falta quando o AUTO
        // já derrubou a escala) e um bloom mais curto.
        // O bloom NÃO muda com a escala: o halo tem de ter o mesmo tamanho e a
        // mesma força em qualquer degrau do AUTO (é aparência, não detalhe), e
        // a cadeia inteira custa menos que um passe de tela cheia.
        // No degrau mais baixo continua 2× (não FXAA): o MSAA preserva a
        // energia da imagem, o FXAA borra traço fino — o objeto não pode mudar
        // de aparência só porque o aparelho esquentou. FXAA = nível BAIXO
        // explícito ou GPU sem MSAA (GLES).
        q.msaaSamples = s >= 0.75f ? 4u : 2u;
        q.fxaa = false;
        return q;
    }

    [[nodiscard]] bool is_full() const noexcept {
        return particles >= 1.0f && flow >= 1.0f && flowBlurSamples >= 16 && shadowMapSize >= 2048 && shadowFilter >= 2
            && lodBias >= 1.0f && effects >= 1.0f && msaaSamples >= 4 && bloomLevels >= 6;
    }
};

/// Qualidade do 3D escolhida na composição (AA + pós). AUTO segue o preview
/// adaptativo (HeavyQuality::from_scale); o export é sempre ALTO ou ULTRA.
///
///   BAIXO  1 amostra + FXAA, bloom de 6 níveis a partir de 1/2
///   MÉDIO  MSAA 2×, bloom de 6 níveis a partir de 1/2
///   ALTO   MSAA 4×, bloom de 6 níveis a partir de 1/2       (export padrão)
///   ULTRA  MSAA 4× (8× no export, se o aparelho tiver), bloom de 7 níveis a
///          partir da resolução inteira
enum class Scene3DQuality : u8 { Auto = 0, Low, Medium, High, Ultra };
inline constexpr u32 kScene3DQualityCount = 5;

/// Aplica o nível da composição sobre o bloco (que já traz o AUTO do preview
/// ou o cheio do export).
inline void apply_scene3d_quality(HeavyQuality& q, Scene3DQuality tier) noexcept {
    if (q.exportFrame) {
        // Export nunca abaixo do ALTO: o arquivo final não herda o corte do preview.
        q.msaaSamples = tier == Scene3DQuality::Ultra ? 8u : 4u;
        q.fxaa = false;
        q.bloomStartDiv = tier == Scene3DQuality::Ultra ? 1u : 2u;
        q.bloomLevels = tier == Scene3DQuality::Ultra ? 7u : 6u;
        // Sombra do arquivo final: sempre ULTRA (mapa 4096 + PCSS 16+32).
        q.shadowMapSize = 4096;
        q.shadowFilter = 3;
        return;
    }
    // Sombra por nível (AUTO fica com o from_scale; o mapa do preview nunca
    // passa do AUTO — 4096 só no export).
    switch (tier) {
        case Scene3DQuality::Low: q.shadowMapSize = std::min(q.shadowMapSize, 1024u); q.shadowFilter = 0; break;
        case Scene3DQuality::Medium: q.shadowFilter = 1; break;
        case Scene3DQuality::High: q.shadowFilter = 2; break;
        case Scene3DQuality::Ultra: q.shadowFilter = 3; break;
        default: break;
    }
    switch (tier) {
        case Scene3DQuality::Auto: return;
        case Scene3DQuality::Low: q.msaaSamples = 1; q.fxaa = true; q.bloomStartDiv = 2; q.bloomLevels = 6; return;
        case Scene3DQuality::Medium: q.msaaSamples = 2; q.fxaa = false; q.bloomStartDiv = 2; q.bloomLevels = 6; return;
        case Scene3DQuality::High: q.msaaSamples = 4; q.fxaa = false; q.bloomStartDiv = 2; q.bloomLevels = 6; return;
        case Scene3DQuality::Ultra: q.msaaSamples = 4; q.fxaa = false; q.bloomStartDiv = 1; q.bloomLevels = 7; return;
    }
}

/// Contadores dos sistemas pesados (acumulados desde o último `reset`), para
/// testes, benchmark e o HUD (8A lê as diferenças entre leituras). Os números
/// do ÚLTIMO quadro ficam nos campos `last*`.
struct HeavyStats {
    // Texto: o atlas SDF só sobe quando ganha glifo (em regime = 0 por quadro).
    u32 glyphAtlasUploads = 0;
    u64 glyphAtlasUploadBytes = 0;
    u32 glyphsRasterized = 0;     ///< SDFs novos no atlas (text::glyph_atlas_stats)
    u32 glyphAtlasResets = 0;     ///< atlas cheio → refeito
    // Vetor: malha refeita só quando o conteúdo avaliado muda.
    u32 vectorTessellations = 0;
    u32 vectorCacheHits = 0;
    // Máscaras: cobertura rasterizada × reaproveitada.
    u32 maskRasterizations = 0;
    u32 maskCacheHits = 0;
    // Optical flow.
    u32 flowComputed = 0;
    u32 flowCacheHits = 0;
    u32 flowCacheEvictions = 0;
    u64 flowCacheBytes = 0;       ///< residente agora
    // Partículas (último quadro).
    u32 lastParticleSlots = 0;    ///< instâncias desenhadas
    u32 lastParticleLayers = 0;
    /// Aurea Particular 8.2 (render/ParticleExtras): bytes residentes dos dados
    /// extras (cache de pontos/malhas na CPU + buffers de GPU) e camadas com
    /// eles ligados no último quadro.
    u64 particleExtraCpuBytes = 0;
    u64 particleExtraGpuBytes = 0;
    u32 lastParticleExtraLayers = 0;
    // 3D (último quadro, todas as cenas e subquadros).
    u32 lastSceneDrawCalls = 0;
    u32 lastSceneShadowDrawCalls = 0;
    u32 lastSceneInstancedDraws = 0;  ///< desenhos que juntaram ≥ 2 instâncias
    u32 lastSceneVisible = 0;
    u32 lastSceneCulled = 0;
    u32 lastSceneTriangles = 0;
    u32 lastShadowMapSize = 0;
};

} // namespace aurea
