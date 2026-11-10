// Included by test_render.cpp (RenderFixture, MockBackend).
// =============================================================================
//  0.5.2 (E1b): "textura transitoria indisponivel" no mesmo quadro em todo
//  degrau da escada, inclusive 480p (Galaxy A23, 2 clipes com transição e
//  shake). Reprodução: duas camadas do tamanho de uma fonte 4K (enquadradas
//  numa composição 1080p com 5% de zoom contra as bordas do shake) e o shake
//  em cada uma. Na densidade da composição, o pico do pool NÃO muda com o
//  tamanho da saída — é o que os degraus mudavam. Com a densidade da saída
//  (render_export_frame nos degraus com shortSideCap), o pico cai e o mesmo
//  limite passa.
// =============================================================================
namespace export_density_test {
struct Outcome {
    Status status;
    u64 transientBytes = 0;
};
Outcome render_two_source_sized_clips(u32 outW, u32 outH, u32 numerator, u32 denominator, u64 limit) {
    RenderFixture fixture;   // composição 1920 × 1080
    for (u32 i = 0; i < 2; ++i) {
        const LayerId id = fixture.solid(i ? "clipe B (fonte 4K)" : "clipe A (fonte 4K)",
                                         Vec4{.2f + .4f * static_cast<f32>(i), .5f, .7f, 1.f});
        Layer* layer = fixture.comp->layer(id);
        layer->shape.bounds = Rect{0, 0, 3840, 2160};
        layer->transform.anchor = Vec3{1920, 1080, 0};
        layer->transform.scale = Vec3{.525f, .525f, 1};
        layer->effects.push_back(make_effect(fixture.effects, effect_keys::kShake, 0));
    }
    TextureDesc td;
    td.width = outW; td.height = outH; td.format = SurfaceFormat::RGBA16F;
    td.sampled = true; td.renderTarget = true; td.debugName = "saida-teste";
    auto texture = fixture.backend.create_texture(td);
    Outcome out;
    if (!texture.ok()) { out.status = texture.status(); return out; }
    OffscreenTarget target;
    target.texture = *texture; target.width = outW; target.height = outH;
    RenderSettings settings;
    settings.finalQuality = true;
    settings.gpuTimers = false;
    settings.previewNumerator = numerator;
    settings.previewDenominator = denominator;
    if (limit) fixture.renderer.set_transient_allocation_limit(limit);
    FrameSnapshot snapshot;
    fixture.renderer.prepare(*fixture.comp, fixture.project, FrameIndex{0}, nullptr, nullptr, nullptr,
                             settings, 1, 0, DecodeMode::Still, 1, snapshot);
    FrameStats stats; RenderTimings timings;
    out.status = fixture.renderer.render(snapshot, settings, &target, stats, timings);
    out.transientBytes = fixture.renderer.graph_stats().transientBytes;
    return out;
}
} // namespace export_density_test

AUREA_TEST(MemoryPressure, SourceSizedLayersIgnoreTheOutputSizeUntilDensityFollowsIt) {
    using namespace export_density_test;
    // Sem teto: o pico de cada caso.
    const Outcome full1080 = render_two_source_sized_clips(1920, 1080, 1, 1, 0);
    const Outcome full480 = render_two_source_sized_clips(854, 480, 1, 1, 0);
    const Outcome dense480 = render_two_source_sized_clips(854, 480, 854, 1920, 0);
    std::printf("\n    pico do pool: 1080p %llu MB | 480p %llu MB | 480p na densidade da saida %llu MB",
                static_cast<unsigned long long>(full1080.transientBytes >> 20),
                static_cast<unsigned long long>(full480.transientBytes >> 20),
                static_cast<unsigned long long>(dense480.transientBytes >> 20));
    AUREA_CHECK(full1080.status.ok() && full480.status.ok() && dense480.status.ok());
    // A saída menor quase não muda o pico (só o alvo final): os degraus de
    // tamanho não ajudavam. Com a densidade da saída ele cai a menos da metade.
    AUREA_CHECK(full480.transientBytes * 10 >= full1080.transientBytes * 8);
    AUREA_CHECK(dense480.transientBytes * 2 < full480.transientBytes);
    // O teto do aparelho entre os dois: a densidade da composição falha em
    // 480p como em 1080p (o "mesmo quadro em todo degrau"); a da saída passa.
    const u64 limit = (dense480.transientBytes + full480.transientBytes) / 2;
    const Outcome refused = render_two_source_sized_clips(854, 480, 1, 1, limit);
    const Outcome fits = render_two_source_sized_clips(854, 480, 854, 1920, limit);
    std::printf("\n    teto %llu MB: densidade da composicao -> %d | da saida -> %d",
                static_cast<unsigned long long>(limit >> 20), refused.status.raw(), fits.status.raw());
    AUREA_CHECK_EQ(refused.status.code(), Errc::OutOfDeviceMemory);
    AUREA_CHECK(fits.status.ok());
}
