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
    /// Mapa de sombra do 3D (lado, 512..2048) e filtro: 2 = PCF 6×6 com peso
    /// bilinear (o do export), 1 = PCF 2×2 bilinear, 0 = uma amostra.
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
        q.shadowFilter = s >= 0.75f ? 2u : 1u;
        q.lodBias = s >= 0.75f ? 1.0f : (s >= 0.4f ? 0.75f : 0.5f);
        q.effects = std::clamp(s, 0.25f, 1.0f);
        return q;
    }

    [[nodiscard]] bool is_full() const noexcept {
        return particles >= 1.0f && flow >= 1.0f && flowBlurSamples >= 16 && shadowMapSize >= 2048 && shadowFilter >= 2
            && lodBias >= 1.0f && effects >= 1.0f;
    }
};

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
