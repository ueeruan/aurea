// Malha de deformação (aurea.distort.mesh_warp): dado, animação, projeto e o
// desenho na GPU. Incluído no fim de test_gpu.cpp.
#include "aurea/effects/MeshWarp.hpp"
#include "aurea/project/Serialization.hpp"
namespace {
f32 mesh_difference(const FloatImage& a, const FloatImage& b) {
    f32 r = 0;
    for (usize i = 0; i < std::min(a.px.size(), b.px.size()); ++i) r = std::max(r, std::fabs(a.px[i] - b.px[i]));
    return r;
}
f32 mesh_region_difference(const FloatImage& a, const FloatImage& b, u32 x0, u32 y0, u32 x1, u32 y1) {
    f32 r = 0;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x)
            for (u32 c = 0; c < 4; ++c) r = std::max(r, std::fabs(a.at(x, y)[c] - b.at(x, y)[c]));
    return r;
}
MeshWarpData moved_mesh(u32 rows, u32 cols, u32 vertex, Vec2 to) {
    MeshWarpData d;
    d.rows = rows;
    d.cols = cols;
    mesh_warp::identity(rows, cols, d.values);
    AUREA_CHECK(mesh_warp::move_handle(d.values, rows, cols, vertex, 0, to));
    return d;
}
} // namespace

AUREA_TEST(MeshWarp, IdentityTessellationAndKeyInterpolation) {
    std::vector<f32> id;
    mesh_warp::identity(3, 4, id);
    AUREA_CHECK_EQ(id.size(), static_cast<usize>(mesh_warp::value_count(3, 4)));
    AUREA_CHECK(mesh_warp::is_identity(id, 3, 4));
    // Malha de fábrica = grade regular (o retalho de Coons é bilinear).
    std::vector<Vec2> grid;
    u32 gx = 0, gy = 0;
    mesh_warp::tessellate(id, 3, 4, 5, 6, grid, gx, gy);
    AUREA_CHECK_EQ(gx, 21u);
    AUREA_CHECK_EQ(gy, 19u);
    f32 worst = 0;
    for (u32 j = 0; j < gy; ++j)
        for (u32 i = 0; i < gx; ++i) {
            const Vec2 p = grid[usize(j) * gx + i];
            worst = std::max(worst, std::fabs(p.x - f32(i) / f32(gx - 1)));
            worst = std::max(worst, std::fabs(p.y - f32(j) / f32(gy - 1)));
        }
    AUREA_CHECK(worst < 1e-5f);
    // Vértice movido: a grade passa por ele; a borda oposta não mexe.
    MeshWarpData d = moved_mesh(2, 2, 4, Vec2{0.7f, 0.6f});
    mesh_warp::tessellate(d.values, 2, 2, 4, 4, grid, gx, gy);
    AUREA_CHECK_NEAR(grid[usize(4) * gx + 4].x, 0.7f, 1e-6f);
    AUREA_CHECK_NEAR(grid[usize(4) * gx + 4].y, 0.6f, 1e-6f);
    AUREA_CHECK_NEAR(grid[0].x, 0.0f, 1e-6f);
    AUREA_CHECK_NEAR(grid[usize(gy - 1) * gx + gx - 1].x, 1.0f, 1e-6f);
    // Alça arrastada é relativa: mover o vértice depois leva a alça junto.
    AUREA_CHECK(mesh_warp::move_handle(d.values, 2, 2, 4, 2, Vec2{0.9f, 0.6f}));
    AUREA_CHECK_NEAR(d.values[4 * kMeshWarpVertexFloats + 4], 0.2f, 1e-6f);
    AUREA_CHECK(mesh_warp::move_handle(d.values, 2, 2, 4, 0, Vec2{0.5f, 0.5f}));
    AUREA_CHECK_NEAR(d.values[4 * kMeshWarpVertexFloats + 4], 0.2f, 1e-6f);
    AUREA_CHECK(!mesh_warp::move_handle(d.values, 2, 2, 9, 0, Vec2{0, 0}));
    // Keys: linear no meio, segura fora; grade trocada = de fábrica.
    MeshWarpData k;
    k.rows = k.cols = 2;
    MeshWarpKey a, b;
    a.frame = 0;
    mesh_warp::identity(2, 2, a.values);
    b.frame = 10;
    b.values = moved_mesh(2, 2, 4, Vec2{0.8f, 0.5f}).values;
    k.keys = {a, b};
    std::vector<f32> v;
    AUREA_CHECK(!mesh_warp::evaluate(&k, 2, 2, 0.0, v));
    AUREA_CHECK(mesh_warp::evaluate(&k, 2, 2, 5.0, v));
    AUREA_CHECK_NEAR(v[4 * kMeshWarpVertexFloats], 0.65f, 1e-6f);
    AUREA_CHECK(mesh_warp::evaluate(&k, 2, 2, 50.0, v));
    AUREA_CHECK_NEAR(v[4 * kMeshWarpVertexFloats], 0.8f, 1e-6f);
    k.keys[0].interp = 0;
    AUREA_CHECK(!mesh_warp::evaluate(&k, 2, 2, 9.0, v));
    AUREA_CHECK(!mesh_warp::evaluate(&k, 3, 2, 10.0, v));
    AUREA_CHECK(mesh_warp::is_identity(v, 3, 2));
    AUREA_CHECK_EQ(mesh_warp::key_at(k, 10), 1);
    AUREA_CHECK_EQ(mesh_warp::key_at(k, 3), -1);
}

AUREA_TEST(MeshWarp, ProjectRoundTripKeepsMeshAndKeys) {
    auto p = Project::create_new(320, 180, 30.0, "malha");
    AUREA_CHECK(p.ok());
    if (!p.ok()) return;
    Project project = std::move(*p);
    Composition* comp = project.timeline().composition(project.timeline().root());
    const LayerId id = comp->add_layer(LayerKind::Image, "img");
    Layer* l = comp->layer(id);
    EffectInstance e;
    e.id = l->alloc_effect_id();
    e.type = effect_type_id(kMeshWarpKey);
    e.params.resize(3);
    e.params[0].constant = ParamValue::scalar(2);
    e.params[1].constant = ParamValue::scalar(3);
    e.params[2].constant = ParamValue::scalar(8);
    MeshWarpData d = moved_mesh(2, 3, 5, Vec2{0.4f, 0.45f});
    MeshWarpKey k0, k1;
    k0.frame = 3; k0.interp = 2; k0.values = d.values;
    k1.frame = 20; k1.values = moved_mesh(2, 3, 6, Vec2{0.9f, 0.1f}).values;
    d.keys = {k0, k1};
    e.meshes.push_back(d);
    l->effects.push_back(e);
    // Outro efeito depois: o bloco da malha não pode desalinhar o resto.
    EffectInstance other;
    other.id = l->alloc_effect_id();
    other.type = effect_type_id("aurea.blur.gaussian");
    other.params.resize(1);
    other.params[0].constant = ParamValue::scalar(12.5f);
    l->effects.push_back(other);
    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(project, SaveOptions{}, bytes).ok());
    Project back;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load_bytes(back, bytes.data(), bytes.size(), LoadOptions{}, &report).ok());
    const Composition* bc = back.timeline().composition(back.timeline().root());
    const Layer* bl = bc ? bc->layer(id) : nullptr;
    AUREA_CHECK(bl != nullptr);
    if (!bl) return;
    AUREA_CHECK_EQ(bl->effects.size(), static_cast<usize>(2));
    if (bl->effects.size() != 2) return;
    const EffectInstance& be = bl->effects[0];
    AUREA_CHECK_EQ(be.meshes.size(), static_cast<usize>(1));
    if (be.meshes.size() != 1) return;
    const MeshWarpData& bd = be.meshes[0];
    AUREA_CHECK_EQ(bd.rows, 2u);
    AUREA_CHECK_EQ(bd.cols, 3u);
    AUREA_CHECK(bd.values == d.values);
    AUREA_CHECK_EQ(bd.keys.size(), static_cast<usize>(2));
    if (bd.keys.size() == 2) {
        AUREA_CHECK_EQ(bd.keys[0].frame, 3);
        AUREA_CHECK_EQ(static_cast<u32>(bd.keys[0].interp), 2u);
        AUREA_CHECK(bd.keys[1].values == k1.values);
    }
    AUREA_CHECK_NEAR(bl->effects[1].params[0].constant.v[0], 12.5f, 0.0f);
    AUREA_CHECK(bl->effects[1].meshes.empty());
}

AUREA_TEST(MeshWarp, EngineDragKeysAtPlayheadUndoAndReset) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    std::vector<u8> rgba(200u * 100u * 4u, 255);
    auto img = e.import_image(rgba.data(), 200, 100, "malha");
    AUREA_CHECK(img.ok());
    if (!img.ok()) { e.shutdown(); return; }
    const u64 layer = *img;
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add = EffectAddPayload{LayerId::unpack(layer), effect_type_id(kMeshWarpKey), kInvalidIndex};
    AUREA_CHECK(e.apply_command(add).ok());
    std::vector<bridge::LayerEffectRow> rows(4);
    std::vector<char> blob(4096);
    const u32 n = e.query_layer_effects(layer, rows.data(), 4, blob.data(), static_cast<u32>(blob.size()));
    AUREA_CHECK(n >= 1u);
    if (n < 1u) { e.shutdown(); return; }
    const u32 fx = rows[n - 1].effectId;
    std::vector<f32> m(e.query_mesh_warp(layer, fx, nullptr, 0));
    AUREA_CHECK_EQ(m.size(), static_cast<usize>(Engine::kMeshWarpHeaderFloats + mesh_warp::value_count(7, 7)));
    e.query_mesh_warp(layer, fx, m.data(), static_cast<u32>(m.size()));
    AUREA_CHECK_NEAR(m[0], 7.0f, 0.0f);
    AUREA_CHECK_NEAR(m[3], 0.0f, 0.0f);   // parada
    // Arrasto sem auto-key: malha parada; um gesto = um desfazer.
    AUREA_CHECK(e.mesh_warp_drag(layer, fx, 9, 0, 0.3f, 0.3f, false, false));
    AUREA_CHECK(e.mesh_warp_drag(layer, fx, 9, 0, 0.35f, 0.32f, false, true));
    e.query_mesh_warp(layer, fx, m.data(), static_cast<u32>(m.size()));
    const usize at = Engine::kMeshWarpHeaderFloats + 9 * kMeshWarpVertexFloats;
    AUREA_CHECK_NEAR(m[at], 0.35f, 1e-6f);
    AUREA_CHECK_NEAR(m[3], 0.0f, 0.0f);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    e.query_mesh_warp(layer, fx, m.data(), static_cast<u32>(m.size()));
    AUREA_CHECK_NEAR(m[at], 1.0f / 7.0f, 1e-6f);
    // Auto-key: key no cabeçote.
    AUREA_CHECK(e.mesh_warp_drag(layer, fx, 9, 0, 0.3f, 0.3f, true, false));
    e.query_mesh_warp(layer, fx, m.data(), static_cast<u32>(m.size()));
    AUREA_CHECK_NEAR(m[2], 1.0f, 0.0f);
    AUREA_CHECK_NEAR(m[3], 1.0f, 0.0f);
    // Redefinir: de fábrica, sem keys.
    AUREA_CHECK(e.mesh_warp_reset(layer, fx));
    e.query_mesh_warp(layer, fx, m.data(), static_cast<u32>(m.size()));
    AUREA_CHECK_NEAR(m[3], 0.0f, 0.0f);
    AUREA_CHECK_NEAR(m[at], 1.0f / 7.0f, 1e-6f);
    AUREA_CHECK(!e.mesh_warp_drag(layer, fx + 99, 0, 0, 0.0f, 0.0f, false, false));
    e.shutdown();
}

AUREA_TEST(MeshWarpGpu, IdentityKeepsLayerAndVertexMovesItsRegion) {
    AUREA_REQUIRE_GPU();
    Scene scene(192, 144);
    auto gradient = uniform_image(96, 64, 0, 0, 80);
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 96; ++x) {
            gradient.rgba[(usize(y) * 96 + x) * 4] = static_cast<u8>(x * 255 / 95);
            gradient.rgba[(usize(y) * 96 + x) * 4 + 1] = static_cast<u8>(y * 255 / 63);
        }
    const auto id = scene.image(std::move(gradient), 96, 72);   // camada em (48..144, 40..104)
    const auto original = scene.render();
    auto& fx = scene.add_effect(id, kMeshWarpKey);
    fx.params[0].constant = ParamValue::scalar(2);
    fx.params[1].constant = ParamValue::scalar(2);
    // Sem malha e com a malha de fábrica explícita: a camada sai igual.
    AUREA_CHECK_NEAR(mesh_difference(original, scene.render()), 0, 1.0f / 255.0f);
    MeshWarpData identity;
    identity.rows = identity.cols = 2;
    mesh_warp::identity(2, 2, identity.values);
    fx.meshes = {identity};
    AUREA_CHECK_NEAR(mesh_difference(original, scene.render()), 0, 1.0f / 255.0f);
    // O vértice do meio vai para a direita: o miolo muda, os cantos não.
    fx.meshes = {moved_mesh(2, 2, 4, Vec2{0.75f, 0.5f})};
    const auto moved = scene.render();
    AUREA_CHECK(mesh_region_difference(original, moved, 80, 60, 120, 84) > 0.05f);
    AUREA_CHECK(mesh_region_difference(original, moved, 50, 42, 58, 48) < 0.02f);
    AUREA_CHECK(mesh_region_difference(original, moved, 136, 96, 142, 102) < 0.02f);
    // A cor que estava no centro (x=96) agora aparece em 3/4 da camada (x=120).
    for (u32 c = 0; c < 2; ++c) AUREA_CHECK_NEAR(moved.at(120, 72)[c], original.at(96, 72)[c], 0.03f);
    // Linhas trocadas: a malha não vale mais (redefinida).
    fx.params[0].constant = ParamValue::scalar(3);
    AUREA_CHECK_NEAR(mesh_difference(original, scene.render()), 0, 1.0f / 255.0f);
    fx.params[0].constant = ParamValue::scalar(2);
    // Keys: quadro 0 = de fábrica, 10 = movida, 5 = no meio do caminho.
    MeshWarpData animated;
    animated.rows = animated.cols = 2;
    MeshWarpKey k0, k1;
    k0.frame = 0; k0.values = identity.values;
    k1.frame = 10; k1.values = moved_mesh(2, 2, 4, Vec2{0.75f, 0.5f}).values;
    animated.keys = {k0, k1};
    fx.meshes = {animated};
    AUREA_CHECK_NEAR(mesh_difference(original, scene.render(FrameIndex{0})), 0, 1.0f / 255.0f);
    AUREA_CHECK_NEAR(mesh_difference(moved, scene.render(FrameIndex{10})), 0, 1.0f / 255.0f);
    const auto half = scene.render(FrameIndex{5});
    // No meio (vértice em 0.625): a cor do centro fica em x = 48 + 0.625·96 = 108.
    for (u32 c = 0; c < 2; ++c) AUREA_CHECK_NEAR(half.at(108, 72)[c], original.at(96, 72)[c], 0.03f);
}

#if defined(AUREA_TEST_VULKAN)
// O caminho do APP: texto padrão, efeito pelo comando, arrasto pela API da
// malha com auto-key e a captura do motor (não o Scene do teste).
AUREA_TEST(MeshWarpGpu, EngineDragOnTextLayerWarpsTheRenderedFrame) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(320, 180);
    auto text = rig.e.add_text("Texto");
    AUREA_CHECK(text.ok());
    if (!text.ok()) return;
    const u64 layer = *text;
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add = EffectAddPayload{LayerId::unpack(layer), effect_type_id(kMeshWarpKey), kInvalidIndex};
    AUREA_CHECK(rig.e.apply_command(add).ok());
    std::vector<bridge::LayerEffectRow> rows(8);
    std::vector<char> blob(4096);
    const u32 n = rig.e.query_layer_effects(layer, rows.data(), 8, blob.data(), static_cast<u32>(blob.size()));
    AUREA_CHECK(n >= 1u);
    if (n < 1u) return;
    const u32 fx = rows[n - 1].effectId;
    const Image8 before = rig.capture(320);
    // Caminho do PREVIEW (cache de quadros compostos ligado): o quadro antes
    // da edição fica no cache; a edição pela API da malha tem de invalidá-lo.
    TextureDesc td;
    td.width = 320;
    td.height = 180;
    td.format = SurfaceFormat::RGBA16F;
    td.renderTarget = true;
    td.transferSrc = true;
    const auto target = rig.e.gpu()->create_texture(td);
    AUREA_CHECK(target.ok());
    if (!target.ok()) return;
    std::vector<u16> previewBefore(320 * 180 * 4), previewAfter(320 * 180 * 4);
    AUREA_CHECK(rig.e.render_offscreen(*target, 320, 180, true).ok());
    AUREA_CHECK(rig.e.render_offscreen(*target, 320, 180, true).ok());
    AUREA_CHECK(rig.e.gpu()->read_texture(*target, previewBefore.data(), 320 * 8).ok());
    // Vértice de cima à direita (linha 0, coluna 7) bem para cima e à direita.
    AUREA_CHECK(rig.e.mesh_warp_drag(layer, fx, 7, 0, 1.6f, -0.6f, true, false));
    AUREA_CHECK(rig.e.mesh_warp_drag(layer, fx, 7, 0, 1.9f, -0.9f, true, true));
    std::vector<f32> m(rig.e.query_mesh_warp(layer, fx, nullptr, 0));
    rig.e.query_mesh_warp(layer, fx, m.data(), static_cast<u32>(m.size()));
    AUREA_CHECK_NEAR(m[2], 1.0f, 0.0f);
    const Image8 after = rig.capture(320);
    u32 diff = 0, changed = 0;
    for (usize i = 0; i + 3 < before.rgba.size() && i + 3 < after.rgba.size(); i += 4) {
        const u32 d = static_cast<u32>(std::abs(int(before.rgba[i]) - int(after.rgba[i])));
        diff = std::max(diff, d);
        changed += d > 24;
    }
    std::printf("\n    texto com malha: dif %u, %u pixels mudaram", diff, changed);
    if (std::getenv("AUREA_MESH_DUMP")) { (void)write_png("build/reference/mesh_text_before.png", before); (void)write_png("build/reference/mesh_text_after.png", after); }
    // A célula do canto de um texto é a margem acima do "o" (vazia): mover
    // SÓ o vértice do canto não pode borrar nem mexer no resto do texto —
    // como no AE, o vértice só entorta as células que tocam nele.
    u32 restDiff = 0;
    for (u32 y = 70; y < 110; ++y)
        for (u32 x = 110; x < 166; ++x)
            restDiff = std::max(restDiff, static_cast<u32>(std::abs(int(before.at(x, y)[0]) - int(after.at(x, y)[0]))));
    std::printf("\n    resto do texto: dif %u", restDiff);
    AUREA_CHECK(restDiff <= 3u);
    // Vértice do meio (linha 3, coluna 3) para cima: o miolo do texto entorta.
    AUREA_CHECK(rig.e.mesh_warp_drag(layer, fx, 3 * 8 + 3, 0, 0.45f, -1.2f, true, false));
    const Image8 middle = rig.capture(320);
    u32 changedMid = 0;
    for (usize i = 0; i + 3 < after.rgba.size() && i + 3 < middle.rgba.size(); i += 4)
        changedMid += static_cast<u32>(std::abs(int(after.rgba[i]) - int(middle.rgba[i]))) > 24;
    std::printf("\n    vertice do meio: %u pixels mudaram", changedMid);
    if (std::getenv("AUREA_MESH_DUMP")) (void)write_png("build/reference/mesh_text_middle.png", middle);
    AUREA_CHECK(changedMid > 150u);
    AUREA_CHECK(rig.e.render_offscreen(*target, 320, 180, true).ok());
    AUREA_CHECK(rig.e.gpu()->read_texture(*target, previewAfter.data(), 320 * 8).ok());
    u32 changedPreview = 0;
    for (usize i = 0; i < previewBefore.size(); i += 4)
        changedPreview += std::fabs(half_to_float(previewBefore[i]) - half_to_float(previewAfter[i])) > 0.1f;
    std::printf("\n    preview (cache): %u pixels mudaram", changedPreview);
    AUREA_CHECK(changedPreview > 150u);
    rig.e.gpu()->destroy_texture(*target);
}
#endif
