// Remover fundo (0.4.5, aurea.key.background_remove) no render de verdade:
// a rede embutida recorta, os controles agem no shader, e o export lê os
// recortes analisados na prévia (o mesmo cache) sem recalcular.
#include "aurea/ai/BackgroundRemoval.hpp"

namespace {
f32 bg_alpha_sum(const FloatImage& img, u32 w, u32 h) {
    f32 sum = 0;
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x) sum += img.v(x, y).w;
    return sum;
}
} // namespace

AUREA_TEST(BgRemoveGpu, PhotoLosesItsBackgroundAndTheControlsActWithoutRecomputing) {
    AUREA_REQUIRE_GPU();
    Scene s(320, 320); s.comp->set_transparent_background(true);
    auto px = uniform_image(320, 320, 30, 120, 70);
    for (u32 y = 0; y < 320; ++y) for (u32 x = 0; x < 320; ++x)
        if ((int(x) - 160) * (int(x) - 160) + (int(y) - 160) * (int(y) - 160) < 6400) {
            auto* p = &px.rgba[(y * 320 + x) * 4]; p[0] = 220; p[1] = 40; p[2] = 30;
        }
    const auto id = s.image(std::move(px), 160, 160);
    s.project.asset(s.comp->layer(id)->source)->sourcePath = "bgremove-gpu-disk.png";
    auto store = ai::make_bg_matte_store();
    gpu().renderer.set_bg_matte_store(store);
    auto& fx = s.add_effect(id, effect_keys::kBackgroundRemove);
    const auto cut = s.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(cut.v(160, 160).w > .9f);
    AUREA_CHECK(cut.v(4, 4).w < .01f);
    AUREA_CHECK(cut.v(300, 160).w < .01f);
    AUREA_CHECK_EQ(store->frames(), usize{1});       // a foto: um recorte, no cache
    // Inverter (manter o fundo), mistura 0 (original) e a visualização da máscara:
    // tudo no shader, o recorte não é refeito.
    fx.params[3].constant = ParamValue::boolean(true);
    const auto inverted = s.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(inverted.v(160, 160).w < .01f);
    AUREA_CHECK(inverted.v(4, 4).w > .9f);
    fx.params[3].constant = ParamValue::boolean(false);
    fx.params[4].constant = ParamValue::scalar(0);
    AUREA_CHECK(s.render(FrameIndex{0}, 1, true).v(4, 4).w > .9f);
    fx.params[4].constant = ParamValue::scalar(100);
    fx.params[5].constant = ParamValue::scalar(1);   // Máscara
    const auto matte = s.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(matte.v(160, 160).x > .9f && matte.v(4, 4).x < .01f && matte.v(4, 4).w > .9f);
    fx.params[5].constant = ParamValue::scalar(0);
    // Contrair/expandir: expandir aumenta o recorte, contrair diminui.
    fx.params[2].constant = ParamValue::scalar(40);
    const f32 grown = bg_alpha_sum(s.render(FrameIndex{0}, 1, true), 320, 320);
    fx.params[2].constant = ParamValue::scalar(-40);
    const f32 shrunk = bg_alpha_sum(s.render(FrameIndex{0}, 1, true), 320, 320);
    fx.params[2].constant = ParamValue::scalar(0);
    AUREA_CHECK(grown > shrunk);
    // Sem segmentador nativo, "Pessoa" vale o automático (mesmo recorte).
    fx.params[10].constant = ParamValue::scalar(1);
    const auto person = s.render(FrameIndex{0}, 1, true);
    AUREA_CHECK(depth_max_diff(person, cut) < .002f);
    AUREA_CHECK_EQ(store->frames(), usize{1});
    (void)write_png("build/bgremove-photo-cut.png", cut.encoded());
    gpu().renderer.set_bg_matte_store(nullptr);
}

AUREA_TEST(BgRemoveGpu, ExportReadsTheAnalyzedClipAndMatchesThePreview) {
    AUREA_REQUIRE_GPU();
    SyntheticConfig cfg; cfg.width = 128; cfg.height = 96; cfg.frameCount = 8;
    cfg.pattern = SyntheticPattern::MovingSquare;
    Scene s(128, 96); s.comp->set_transparent_background(true);
    const auto id = s.video(cfg, 64, 48);
    Asset* asset = s.project.asset(s.comp->layer(id)->source);
    asset->video.frameCount = FrameIndex{cfg.frameCount};
    asset->sourcePath = "bgremove-gpu-moving.mp4";
    auto store = ai::make_bg_matte_store();
    gpu().renderer.set_bg_matte_store(store);
    auto& fx = s.add_effect(id, effect_keys::kBackgroundRemove);
    // "Analisar clipe" no worker, com progresso.
    AUREA_CHECK(gpu().renderer.bg_analyze(s.project, *s.comp, *s.comp->layer(id), fx, &s.media));
    auto* svc = gpu().renderer.bg_service();
    AUREA_CHECK(svc != nullptr); if (!svc) return;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (svc->progress().running && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    AUREA_CHECK(!svc->progress().running && !svc->progress().failed);
    i64 st[8]{};
    AUREA_CHECK(gpu().renderer.bg_status(s.project, *s.comp, *s.comp->layer(id), fx, 3, st));
    AUREA_CHECK_EQ(st[5], i64{cfg.frameCount});
    AUREA_CHECK_EQ(st[6], i64{cfg.frameCount});
    AUREA_CHECK_EQ(st[4], i64{1});
    // Export (espera o exato) e a prévia do mesmo quadro: o mesmo recorte.
    const auto exported = s.render(FrameIndex{3}, 1, true);
    const auto preview = s.render(FrameIndex{3}, 1, false);
    AUREA_CHECK(depth_max_diff(exported, preview) < .002f);
    const f32 coverage = bg_alpha_sum(exported, 128, 96);
    AUREA_CHECK(coverage > 1.0f && coverage < 128.0f * 96.0f * .8f);   // recortou algo, não tudo
    // Outro renderizador/serviço (o do export) com o MESMO cache: nada recalculado.
    gpu().renderer.release_project_resources();
    const u64 computedBefore = svc->computed_frames();
    const auto again = s.render(FrameIndex{3}, 1, true);
    AUREA_CHECK(depth_max_diff(exported, again) < .002f);
    AUREA_CHECK_EQ(gpu().renderer.bg_service()->computed_frames(), computedBefore);
    gpu().renderer.release_project_resources();
    gpu().renderer.set_bg_matte_store(nullptr);
}
