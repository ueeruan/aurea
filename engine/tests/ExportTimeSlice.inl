// =============================================================================
//  0.5.3 — relato: "Timeslice cannot render export at 1080p".
//
//  O TimeSlice guarda uma textura por instante (faixa). Cada uma era do tamanho
//  da fonte em RGBA16F (1080p ≈ 16 MB; 12 faixas ≈ 190 MB, 32 ≈ 506 MB), fora
//  do orçamento do export. Num aparelho o pico passava do que a GPU dava, a
//  recuperação destruía todas as faixas e o mesmo pico voltava: o export
//  falhava em 1080p e só passava reduzido. Estes testes exportam com o
//  orçamento rastreado de um aparelho e exigem o vídeo inteiro.
// =============================================================================

namespace {
struct TimeSliceCase { u32 w, h, slices; bool interpolate; u64 budget; i64 frames; };

Outcome export_time_slice(const TimeSliceCase& c, Rig*& out, std::unique_ptr<Rig>& holder) {
    SyntheticConfig cfg;
    cfg.width = c.w; cfg.height = c.h;
    cfg.gop = 30;
    cfg.pattern = SyntheticPattern::FrameGray;
    holder = std::make_unique<Rig>(cfg, 30.0, c.frames, 3, nullptr, nullptr, 0, false, "sintetico",
        ExportExecutionProfile::Balanced, false, nullptr, nullptr, false, c.budget);
    out = holder.get();
    Outcome o;
    if (!out->ok) return o;
    auto* fx = out->add_effect(out->video_layer(), effect_keys::kTimeSlice);
    if (!fx) return o;
    out->cap.keepFrames = true;
    fx->params[1].constant.v[0] = static_cast<f32>(c.slices);
    fx->params[3].constant.v[0] = c.interpolate ? 1.f : 0.f;
    return run_export(*out, std::min(c.w, c.h), 30, false, 240);
}

/// Quantos níveis de cinza (instantes do FrameGray) aparecem na linha e na
/// coluna do meio do quadro `f`: com N faixas exatas, ~N; sem o efeito, 1.
u32 time_slice_levels(const BenchCapture& cap, u32 f, u32 w, u32 h) {
    if (f >= cap.frames.size() || cap.frames[f].size() < static_cast<usize>(w) * h) return 0;
    const u8* y = cap.frames[f].data();
    auto count = [&](bool row) {
        bool seen[256] = {};
        u32 n = 0;
        const u32 len = row ? w : h;
        for (u32 i = 4; i + 4 < len; ++i) {
            const u8 v = row ? y[static_cast<usize>(h / 2) * w + i] : y[static_cast<usize>(i) * w + w / 2];
            // Arredonda ao código do FrameGray (passo 7) para ignorar o filtro da borda.
            const u8 q = static_cast<u8>((v / 7) * 7);
            if (!seen[q]) { seen[q] = true; ++n; }
        }
        return n;
    };
    return std::max(count(true), count(false));
}
} // namespace

AUREA_TEST(ExportTimeSlice, Exports1080pWithAPhoneGpuBudget) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    // 12 faixas (o padrão) e 32 com interpolar (64 amostras) num orçamento de
    // aparelho de 1,5 GB (o export recebe uma parte; as faixas precisam caber).
    for (const TimeSliceCase c : {TimeSliceCase{1920, 1080, 12, false, 1536ull << 20, 45},
                                  TimeSliceCase{1920, 1080, 32, true, 1536ull << 20, 45}}) {
        Rig* r = nullptr; std::unique_ptr<Rig> holder;
        const Outcome o = export_time_slice(c, r, holder);
        AUREA_CHECK(r && r->ok);
        AUREA_CHECK_MSG(o.finished && o.p.result == Errc::Ok, "TimeSlice 1080p nao exportou");
        if (!r) continue;
        AUREA_CHECK(r->cap.finished && !r->cap.aborted);
        AUREA_CHECK_EQ(r->cap.hashes.size(), static_cast<usize>(c.frames));
        AUREA_CHECK(r->cap.ptsMonotonic);
        // As faixas são de verdade: o quadro do meio mostra vários instantes.
        const u32 levels = time_slice_levels(r->cap, 20, c.w, c.h);
        AUREA_CHECK_MSG(levels >= std::min<u32>(c.slices, 12) / 2, "TimeSlice sem faixas no export");
        std::printf("\n    niveis no quadro 20: %u", levels);
        std::printf("\n    TimeSlice %ux%u %u faixas%s: %.1f s, %u quadros",
            c.w, c.h, c.slices, c.interpolate ? " +interp" : "", o.seconds, o.p.framesDone);
    }
}

AUREA_TEST(ExportTimeSlice, Exports4kByDegradingSliceDensityInsteadOfFailing) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const TimeSliceCase c{3840, 2160, 16, false, 1536ull << 20, 20};
    Rig* r = nullptr; std::unique_ptr<Rig> holder;
    const Outcome o = export_time_slice(c, r, holder);
    AUREA_CHECK(r && r->ok);
    AUREA_CHECK_MSG(o.finished && o.p.result == Errc::Ok, "TimeSlice 4K nao exportou");
    if (r) AUREA_CHECK_EQ(r->cap.hashes.size(), static_cast<usize>(c.frames));
    std::printf("\n    TimeSlice 4K 16 faixas: %.1f s", o.seconds);
}
