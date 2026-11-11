// =============================================================================
//  0.5.2 "Prender a um osso" no EXPORT. O export desenha uma cópia congelada do
//  documento num renderer próprio (OfflineFrameRenderer) com os modelos
//  congelados: a camada presa à mão tem de ir para o osso igual à prévia,
//  quadro a quadro, enquanto o braço gira (trilha BonePose do modelo).
//  Incluído no fim de test_export.cpp (usa Rig, run_export e gpu_ok de lá).
// =============================================================================

std::string aurea_test_write_symmetric_rig();   // test_rig_skeleton.cpp (o mesmo boneco em T)

namespace bone_export_test {

struct Centroid {
    f64 x = 0.0, y = 0.0;
    u32 n = 0;
};

/// Centro dos pixels claros (o fundo é preto; só a camada presa aparece).
template <class Luma>
Centroid bright_centroid(u32 w, u32 h, Luma luma, u32 threshold) {
    Centroid c;
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x)
            if (luma(x, y) > threshold) { c.x += x; c.y += y; ++c.n; }
    if (c.n) { c.x /= c.n; c.y /= c.n; }
    return c;
}

} // namespace bone_export_test

AUREA_TEST(Export, ALayerAttachedToABoneFollowsItInTheExportLikeInThePreview) {
    if (!gpu_ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    using namespace bone_export_test;
    SyntheticConfig cfg; cfg.width = 320; cfg.height = 180;
    Rig r(cfg, 30, 12, 2); AUREA_CHECK(r.ok); if (!r.ok) return;
    r.comp()->layer(r.video_layer())->visible = false;
    ModelImport req;
    req.path = aurea_test_write_symmetric_rig();
    const auto model = r.e.import_model(req);
    AUREA_CHECK(model.ok());
    if (!model.ok()) { std::remove(req.path.c_str()); return; }
    const auto shape = r.e.add_shape(0);
    AUREA_CHECK(shape.ok());
    if (!shape.ok()) { std::remove(req.path.c_str()); return; }
    constexpr i32 kLArm = 3, kLHand = 5;
    AUREA_CHECK(r.e.set_layer_parent_bone(*shape, *model, kLHand).ok());
    // Só a camada presa aparece (o boneco fica escondido, mas segue sendo o pai).
    Layer* m = r.comp()->layer(LayerId::unpack(*model));
    Layer* s = r.comp()->layer(LayerId::unpack(*shape));
    AUREA_CHECK(m && s);
    if (!m || !s) { std::remove(req.path.c_str()); return; }
    m->visible = false;
    s->transform.scale = Vec3{0.12f, 0.12f, 0.12f};
    for (TrackProperty p : {TrackProperty::ScaleX, TrackProperty::ScaleY, TrackProperty::ScaleZ})
        if (Track* t = s->tracks.find(p)) t->staticValue = 0.12f;
    // O braço esquerdo gira 90° no Z local ao longo do clipe: a mão (e a
    // camada) sai do lado e sobe.
    auto& arm = m->tracks.get_or_create(TrackProperty::BonePose, static_cast<u32>(kLArm), 2u);
    (void)arm.set(FrameIndex{0}, 0.0f, Interpolation::Linear);
    (void)arm.set(FrameIndex{11}, 90.0f, Interpolation::Linear);

    // Prévia: o renderer do editor nos quadros 0 e 11.
    auto preview_at = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(r.e.apply_command(c).ok());
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        AUREA_CHECK(r.e.capture_frame_rgba(320, rgba, w, h).ok());
        Centroid c0 = bright_centroid(w, h, [&](u32 x, u32 y) {
            const u8* p = rgba.data() + (static_cast<usize>(y) * w + x) * 4;
            return static_cast<u32>(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]);
        }, 40u);
        // Na escala do export (320×180).
        if (w) { c0.x *= 320.0 / w; c0.y *= 180.0 / h; }
        return c0;
    };
    const Centroid p0 = preview_at(0), p11 = preview_at(11);

    r.cap.keepFrames = true;
    const auto result = run_export(r, 180, 30, false, 60);
    AUREA_CHECK(result.finished && result.p.result == Errc::Ok);
    AUREA_CHECK_EQ(r.cap.frames.size(), usize{12});
    if (r.cap.frames.size() != 12) { std::remove(req.path.c_str()); return; }
    const u32 w = r.cap.video.width, h = r.cap.video.height;
    auto export_at = [&](usize f) {
        const std::vector<u8>& yuv = r.cap.frames[f];
        Centroid c0 = bright_centroid(w, h, [&](u32 x, u32 y) { return static_cast<u32>(yuv[static_cast<usize>(y) * w + x]); }, 60u);
        if (w) { c0.x *= 320.0 / w; c0.y *= 180.0 / h; }
        return c0;
    };
    const Centroid e0 = export_at(0), e11 = export_at(11);
    std::printf("\n    previa q0 (%.1f, %.1f) q11 (%.1f, %.1f) | export q0 (%.1f, %.1f) q11 (%.1f, %.1f) | px %u/%u/%u/%u",
                p0.x, p0.y, p11.x, p11.y, e0.x, e0.y, e11.x, e11.y, p0.n, p11.n, e0.n, e11.n);
    AUREA_CHECK(p0.n > 20 && p11.n > 20 && e0.n > 20 && e11.n > 20);
    // O osso move a camada (o modelo pai não se mexe)...
    const f64 moved = std::hypot(e11.x - e0.x, e11.y - e0.y);
    AUREA_CHECK_MSG(moved > 20.0, "no export a camada anda com a mao enquanto o braco gira");
    AUREA_CHECK_MSG(std::hypot(p11.x - p0.x, p11.y - p0.y) > 20.0, "na previa tambem");
    // ...e o export põe a camada onde a prévia põe.
    AUREA_CHECK_MSG(std::hypot(e0.x - p0.x, e0.y - p0.y) < 3.0, "quadro 0: export igual a previa");
    AUREA_CHECK_MSG(std::hypot(e11.x - p11.x, e11.y - p11.y) < 3.0, "quadro 11: export igual a previa");
    std::remove(req.path.c_str());
}
