// =============================================================================
//  Aurea / tests / PuppetGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  Fantoche (aurea.distort.puppet): ARAP na CPU (identidade, pino puxado
//  deforma perto e deixa o longe quase rígido, ocupação segue o contorno),
//  pinos animados interpolam, render na GPU e salvar/reabrir.
// =============================================================================

#include "aurea/effects/Puppet.hpp"

namespace {

void puppet_set_pin(EffectInstance& e, u32 i, Vec2 rest, Vec2 pos) {
    e.params[puppet::pin_on(i)].constant.v[0] = 1.0f;
    e.params[puppet::pin_rest(i)].constant.v[0] = rest.x;
    e.params[puppet::pin_rest(i)].constant.v[1] = rest.y;
    e.params[puppet::pin_pos(i)].constant.v[0] = pos.x;
    e.params[puppet::pin_pos(i)].constant.v[1] = pos.y;
}

f32 puppet_len(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

} // namespace

AUREA_TEST(Puppet, PinsInPlaceLeaveTheMeshAlone) {
    puppet::Mesh m;
    puppet::build_mesh(200, 100, 300, 3, m);
    AUREA_CHECK(m.tris.size() >= 3 * 200);
    std::vector<Vec2> out;
    puppet::deform(m, {}, 0.0f, out);
    AUREA_CHECK(out.size() == m.rest.size());
    const std::vector<puppet::Pin> still = {{{20, 20}, {20, 20}}, {{180, 80}, {180, 80}}, {{100, 50}, {100, 50}}};
    puppet::deform(m, still, 0.0f, out);
    f32 worst = 0.0f;
    for (usize i = 0; i < out.size(); ++i) worst = std::max(worst, puppet_len(out[i], m.rest[i]));
    std::printf("    pior desvio com pinos parados %.5f px ", worst);
    AUREA_CHECK(worst < 0.05f);
}

AUREA_TEST(Puppet, MovingOnePinBendsNearbyAndKeepsTheFarSideRigid) {
    puppet::Mesh m;
    puppet::build_mesh(300, 100, 400, 0, m);
    // Dois pinos parados à esquerda; o da direita sobe 40 px.
    const std::vector<puppet::Pin> pins = {{{20, 30}, {20, 30}}, {{20, 70}, {20, 70}}, {{280, 50}, {280, 10}}};
    std::vector<Vec2> out;
    puppet::deform(m, pins, 0.0f, out);
    f32 nearMove = 0.0f, farMove = 0.0f;
    u32 nearN = 0, farN = 0;
    for (usize i = 0; i < out.size(); ++i) {
        const f32 d = puppet_len(out[i], m.rest[i]);
        if (m.rest[i].x >= 260) { nearMove += d; ++nearN; }
        if (m.rest[i].x <= 40) { farMove += d; ++farN; }
    }
    nearMove /= std::max(1u, nearN);
    farMove /= std::max(1u, farN);
    std::printf("    perto %.2f px, longe %.2f px ", nearMove, farMove);
    AUREA_CHECK(nearMove > 25.0f);
    AUREA_CHECK(farMove < 4.0f);
    // O pino chega onde foi pedido.
    Vec2 back;
    AUREA_CHECK(puppet::rest_point(m, out, Vec2{280, 10}, back));
    AUREA_CHECK(puppet_len(back, Vec2{280, 50}) < 1.5f);
    // Triângulos longe dos pinos continuam do mesmo tamanho (rígido, não esticado).
    f32 worstEdge = 0.0f;
    for (usize t = 0; t + 2 < m.tris.size(); t += 3) {
        const u32 a = m.tris[t], b = m.tris[t + 1];
        if (m.rest[a].x > 60 || m.rest[b].x > 60) continue;
        worstEdge = std::max(worstEdge, std::fabs(puppet_len(out[a], out[b]) - puppet_len(m.rest[a], m.rest[b])));
    }
    AUREA_CHECK(worstEdge < 1.5f);
    // Rigidez 100 %: a camada inteira só gira/anda (arestas intactas em todo lugar).
    puppet::deform(m, pins, 1.0f, out);
    for (usize t = 0; t + 2 < m.tris.size(); t += 3) {
        const u32 a = m.tris[t], b = m.tris[t + 1];
        AUREA_CHECK(std::fabs(puppet_len(out[a], out[b]) - puppet_len(m.rest[a], m.rest[b])) < 1e-2f);
    }
}

AUREA_TEST(Puppet, OccupancyFollowsTheOutline) {
    // Ocupação 4×4 com só a coluna da esquerda cheia: a malha não cobre a direita.
    const u8 occ[16] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    puppet::Mesh full, cut;
    puppet::build_mesh(400, 400, 400, 0, full);
    puppet::build_mesh(400, 400, 400, 0, cut, occ, 4, 4);
    // (uma célula de folga em volta do que é opaco)
    AUREA_CHECK(cut.tris.size() * 10 < full.tris.size() * 6);
    for (const Vec2& v : cut.rest) AUREA_CHECK(v.x <= 260.0f);
}

AUREA_TEST(Gpu, PuppetDeformsTheLayerAndInterpolatesPins) {
    AUREA_REQUIRE_GPU();
    const u32 W = 96, H = 64;
    ImagePixels px = uniform_image(W, H, 0, 0, 0);
    for (u32 y = 0; y < H; ++y)
        for (u32 x = 0; x < W; ++x) {
            const usize i = (static_cast<usize>(y) * W + x) * 4;
            const bool stripe = (x / 8) % 2 == 0;
            px.rgba[i] = stripe ? 230 : 20; px.rgba[i + 1] = static_cast<u8>(y * 3); px.rgba[i + 2] = 80; px.rgba[i + 3] = 255;
        }
    Scene base(W, H);
    base.image(px, W * 0.5f, H * 0.5f);
    const FloatImage plain = base.render();

    // Pinos parados: a imagem sai igual (o efeito vira identidade).
    Scene s(W, H);
    const LayerId id = s.image(px, W * 0.5f, H * 0.5f);
    EffectInstance& e = s.add_effect(id, puppet::kPuppetKey);
    AUREA_CHECK(e.params.size() >= puppet::kParamCount);
    // Dois pinos parados à esquerda (em cima e embaixo) seguram aquele lado.
    puppet_set_pin(e, 0, {0.1f, 0.2f}, {0.1f, 0.2f});
    puppet_set_pin(e, 1, {0.9f, 0.5f}, {0.9f, 0.5f});
    puppet_set_pin(e, 2, {0.1f, 0.8f}, {0.1f, 0.8f});
    const FloatImage still = s.render();
    f32 diff = 0.0f;
    for (usize i = 0; i < still.px.size(); ++i) diff = std::max(diff, std::fabs(still.px[i] - plain.px[i]));
    AUREA_CHECK(diff < 1e-3f);

    // O pino da direita desce 20 % da altura: o lado direito muda, o esquerdo quase não.
    Layer* l = s.comp->layer(id);
    EffectInstance& ef = l->effects.back();
    puppet_set_pin(ef, 1, {0.9f, 0.5f}, {0.9f, 0.7f});
    const FloatImage moved = s.render();
    // Média da diferença (as bordas duras das listras pesam demais num máximo).
    f32 left = 0.0f, right = 0.0f;
    u32 nl = 0, nr = 0;
    for (u32 y = 8; y < H - 8; ++y) {
        for (u32 x = 2; x < 14; ++x, ++nl) left += std::fabs(moved.v(x, y).x - plain.v(x, y).x);
        for (u32 x = 74; x < 90; ++x, ++nr) right += std::fabs(moved.v(x, y).y - plain.v(x, y).y);
    }
    left /= std::max(1u, nl);
    right /= std::max(1u, nr);
    std::printf("    lado do pino %.4f, lado parado %.4f (média) ", right, left);
    AUREA_CHECK(right > 0.02f);
    AUREA_CHECK(left < right * 0.5f);
    AUREA_CHECK(left < 0.03f);

    // Keyframes no pino: no meio do caminho o valor é o meio.
    const u32 pp = puppet::pin_pos(1);
    for (u32 c = 0; c < 2; ++c) {
        Track& t = l->tracks.get_or_create(TrackProperty::EffectParam, ef.id, param_track_key(pp, c));
        (void)t.set(FrameIndex{0}, c == 0 ? 0.9f : 0.5f);
        (void)t.set(FrameIndex{10}, c == 0 ? 0.9f : 0.7f);
    }
    const ParameterRegistry* specs = gpu().effects.params(ef.type);
    AUREA_CHECK(specs != nullptr);
    if (specs) {
        const ParamValue mid = evaluate_param(l->tracks, ef, pp, specs->at(pp), FrameIndex{5});
        AUREA_CHECK(std::fabs(mid.v[1] - 0.6f) < 1e-4f);
    }
    const FloatImage atStart = s.render(FrameIndex{0});
    f32 d0 = 0.0f;
    for (usize i = 0; i < atStart.px.size(); ++i) d0 = std::max(d0, std::fabs(atStart.px[i] - plain.px[i]));
    AUREA_CHECK(d0 < 1e-3f);   // no quadro 0 o pino está no repouso
    const FloatImage atEnd = s.render(FrameIndex{10});
    f32 d10 = 0.0f;
    for (usize i = 0; i < atEnd.px.size(); ++i) d10 = std::max(d10, std::fabs(atEnd.px[i] - moved.px[i]));
    AUREA_CHECK(d10 < 2e-3f);  // no 10, igual ao pino movido

    // Salvar e reabrir: pinos e keyframes voltam.
    const std::string path = "tests/build/prompt03/aurea_teste_fantoche.aurea";
    std::string error;
    AUREA_CHECK(ProjectSerializer::save(s.project, path, SaveOptions{}, &error).ok());
    Project loaded;
    AUREA_CHECK(ProjectSerializer::load(loaded, path, LoadOptions{}, nullptr, &error).ok());
    const Composition* lc = loaded.timeline().composition(loaded.timeline().root());
    const Layer* ll = lc ? lc->layer(id) : nullptr;
    AUREA_CHECK(ll && !ll->effects.empty());
    if (ll && !ll->effects.empty()) {
        const EffectInstance& le = ll->effects.back();
        AUREA_CHECK(le.type == effect_type_id(puppet::kPuppetKey));
        AUREA_CHECK(le.params.size() >= puppet::kParamCount);
        if (le.params.size() >= puppet::kParamCount) {
            AUREA_CHECK(le.params[puppet::pin_on(1)].constant.v[0] > 0.5f);
            AUREA_CHECK(std::fabs(le.params[puppet::pin_rest(1)].constant.v[0] - 0.9f) < 1e-6f);
        }
        const Track* t = ll->tracks.find(TrackProperty::EffectParam, le.id, param_track_key(pp, 1));
        AUREA_CHECK(t && t->keys.size() == 2);
    }
    std::remove(path.c_str());
}

AUREA_TEST(Puppet, OutlineRowsRoundTrip) {
    // Cobertura 48×48 com a metade esquerda cheia → 12 colunas ligadas por linha.
    std::vector<f32> cov(48 * 48, 0.0f);
    for (u32 y = 0; y < 48; ++y) for (u32 x = 0; x < 24; ++x) cov[y * 48 + x] = 1.0f;
    f32 rows[puppet::kOutline];
    puppet::outline_rows(cov.data(), 48, 48, 0.5f, rows);
    std::vector<u8> cells;
    AUREA_CHECK(puppet::outline_cells(rows, cells));
    u32 on = 0;
    for (u32 y = 0; y < puppet::kOutline; ++y)
        for (u32 x = 0; x < puppet::kOutline; ++x) {
            on += cells[y * puppet::kOutline + x];
            AUREA_CHECK((cells[y * puppet::kOutline + x] != 0) == (x < puppet::kOutline / 2));
        }
    AUREA_CHECK_EQ(on, puppet::kOutline * puppet::kOutline / 2);
    const f32 zero[puppet::kOutline] = {};
    AUREA_CHECK(!puppet::outline_cells(zero, cells));
}

AUREA_TEST(Puppet, EngineAddMoveKeyUndoRemoveAndAlphaOutline) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    // Imagem 200×100: metade esquerda opaca, direita transparente.
    std::vector<u8> rgba(200u * 100u * 4u, 255);
    for (u32 y = 0; y < 100; ++y) for (u32 x = 100; x < 200; ++x) rgba[(y * 200u + x) * 4u + 3] = 0;
    auto img = e.import_image(rgba.data(), 200, 100, "fantoche");
    AUREA_CHECK(img.ok());
    if (!img.ok()) { e.shutdown(); return; }
    const u64 layer = *img;
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add = EffectAddPayload{LayerId::unpack(layer), effect_type_id(puppet::kPuppetKey), kInvalidIndex};
    AUREA_CHECK(e.apply_command(add).ok());
    std::vector<bridge::LayerEffectRow> rows(4);
    std::vector<char> blob(4096);
    const u32 n = e.query_layer_effects(layer, rows.data(), 4, blob.data(), static_cast<u32>(blob.size()));
    AUREA_CHECK(n >= 1u);
    if (n < 1u) { e.shutdown(); return; }
    const u32 fx = rows[n - 1].effectId;
    AUREA_CHECK_EQ(e.query_puppet(layer, fx, nullptr, 0), 0u);
    const u32 fullMesh = e.query_puppet_mesh(layer, fx, nullptr, 0);
    // Primeiro pino: o contorno (alfa) entra — a malha fica só na metade opaca.
    AUREA_CHECK_EQ(e.puppet_add_pin(layer, fx, 0.1f, 0.5f), 0);
    AUREA_CHECK_EQ(e.puppet_add_pin(layer, fx, 0.4f, 0.5f), 1);
    std::vector<f32> mesh(e.query_puppet_mesh(layer, fx, nullptr, 0));
    std::printf("    malha: %u -> %u floats ", fullMesh, static_cast<u32>(mesh.size()));
    AUREA_CHECK(!mesh.empty() && mesh.size() * 10 < static_cast<usize>(fullMesh) * 7);
    e.query_puppet_mesh(layer, fx, mesh.data(), static_cast<u32>(mesh.size()));
    f32 maxU = 0.0f;
    for (usize i = 0; i < mesh.size(); i += 2) maxU = std::max(maxU, mesh[i]);
    AUREA_CHECK(maxU < 0.7f);
    std::vector<f32> pins(2 * Engine::kPuppetPinFloats);
    AUREA_CHECK_EQ(e.query_puppet(layer, fx, pins.data(), static_cast<u32>(pins.size())), 2u * Engine::kPuppetPinFloats);
    AUREA_CHECK_NEAR(pins[4 + 1], 0.4f, 1e-6f);
    // Arrasto sem auto-key (um gesto = um desfazer).
    AUREA_CHECK(e.puppet_move_pin(layer, fx, 1, 0.42f, 0.3f, false, false));
    AUREA_CHECK(e.puppet_move_pin(layer, fx, 1, 0.45f, 0.2f, false, true));
    e.query_puppet(layer, fx, pins.data(), static_cast<u32>(pins.size()));
    AUREA_CHECK_NEAR(pins[4 + 2], 0.2f, 1e-6f);
    AUREA_CHECK_NEAR(pins[4 + 3], 0.0f, 0.0f);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    e.query_puppet(layer, fx, pins.data(), static_cast<u32>(pins.size()));
    AUREA_CHECK_NEAR(pins[4 + 2], 0.5f, 1e-6f);
    // Auto-key: key do pino no cabeçote.
    AUREA_CHECK(e.puppet_move_pin(layer, fx, 1, 0.45f, 0.2f, true, false));
    e.query_puppet(layer, fx, pins.data(), static_cast<u32>(pins.size()));
    AUREA_CHECK_NEAR(pins[4 + 3], 1.0f, 0.0f);
    // Apagar o pino.
    AUREA_CHECK(e.puppet_remove_pin(layer, fx, 0));
    AUREA_CHECK_EQ(e.query_puppet(layer, fx, nullptr, 0), Engine::kPuppetPinFloats);
    AUREA_CHECK(!e.puppet_remove_pin(layer, fx, 0));
    AUREA_CHECK_EQ(e.puppet_add_pin(layer, fx + 99, 0.5f, 0.5f), -1);
    e.shutdown();
}
