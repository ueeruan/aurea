#pragma once
#include "aurea/core/Types.hpp"

namespace aurea {
/// A private composition cache. Progress is independent of user exports.
struct PreRenderProgress {
    bool running = false;
    bool complete = false;
    u64 framesDone = 0;
    u64 framesTotal = 0;
    u32 width = 0;
    u32 height = 0;
    i32 direction = 1;
    i32 error = 0;
    u64 diskBytes = 0;
    u64 residentBytes = 0;
    // 1.0.2 Pre Render & Pre Export: o trecho do cache (quadros da composição),
    // a qualidade (1, 2, 4, 8; 0 = o PreRender de 540p do gesto) e quantos
    // trechos de 8 quadros ficaram desatualizados por edições (tocam ao vivo).
    i64 firstFrame = 0;
    i64 endFrame = 0;
    u32 divisor = 0;
    u32 dirtySegments = 0;
};

/// O trecho do painel Pre Render & Pre Export. `in`/`out` = o escolhido com
/// Início/Fim (-1 = nenhum); `effectiveIn`/`effectiveOut` = o que o PreRender e
/// o pré-export vão cobrir (o escolhido, senão a área de trabalho, senão até o
/// fim do conteúdo). Quadros da composição atual, [in, out).
struct PreRenderRangeInfo {
    i64 in = -1, out = -1;
    i64 effectiveIn = 0, effectiveOut = 0;
    bool loop = false;
    u32 divisor = 2;
    f64 fps = 30.0;
};

/// "Cabe ~Xs" (render/PreRenderPlan.hpp, plan_fit) para a qualidade e o modo.
struct PreRenderFitInfo {
    u32 width = 0, height = 0;
    u32 divisor = 2;
    u32 mode = 0;               ///< 0 PreRender, 1 pré-export
    f64 fps = 30.0;
    f64 fitsSeconds = 0.0;
    f64 rangeSeconds = 0.0;
    u64 bytesPerSecond = 0;
    u64 availableBytes = 0;
    u64 rangeBytes = 0;
    bool fits = false;
};

/// Estado do pré-export: um MP4 do trecho no cache privado. `stale` = uma
/// edição mudou algum quadro do trecho depois da partida (ou outro projeto/
/// composição está aberto): a reprodução não usa mais o vídeo.
struct PreExportStatus {
    bool running = false;
    bool complete = false;
    bool stale = false;
    u64 framesDone = 0;
    u64 framesTotal = 0;
    i64 in = 0, out = 0;
    u32 width = 0, height = 0;
    u32 divisor = 2;
    u32 dirtySegments = 0;
    f64 fps = 30.0;
    u64 bytes = 0;
    i32 error = 0;
    char path[1024]{};
};

/// Pedido interno do motor (EnginePreRenderRange.cpp → start_export_checked):
/// exporta só [in, out) da composição atual para o pré-export `id`.
struct PreExportRequest {
    i64 in = 0, out = 0;
    u64 id = 0;
};

/// Pedido interno do PreRender por trecho (start_prerender_range → export de imagem).
struct PreRenderRangeRequest {
    i64 in = 0, out = 0;
    u32 divisor = 2;
};
} // namespace aurea
