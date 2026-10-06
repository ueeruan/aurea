// =============================================================================
//  Aurea / tests / Shapes2DGpu.inl  (incluído no fim de test_gpu.cpp)
//
//  Formas paramétricas 2D: tipos 16..24 (linha, losango, coração, selo, arco,
//  balão, raio, onda, blob), os parâmetros 7..14 e os ladrilhos novos.
//  - geometria na CPU (timeline/ShapeGeometry, o espelho de shape.frag):
//    fechada, simétrica onde deve, cada parâmetro muda a forma, e as formas
//    antigas continuam com a mesma conta de antes;
//  - motor: valores, keyframes, desfazer, salvar/reabrir;
//  - GPU: cada ladrilho novo desenha, bate com a conta da CPU e o parâmetro
//    muda os pixels.
// =============================================================================

#include "aurea/timeline/ShapeGeometry.hpp"

namespace {

/// Grade de amostras da forma (1 = dentro) numa caixa `half`, passo 1 px.
struct ShapeMaskCpu {
    i32 w = 0, h = 0;
    std::vector<u8> in;
    u32 count() const { u32 n = 0; for (u8 v : in) n += v; return n; }
    bool at(i32 x, i32 y) const { return in[static_cast<usize>(y) * w + x] != 0; }
};

ShapeMaskCpu shape_mask_cpu(const ShapeData& sh, Vec2 half) {
    ShapeMaskCpu m;
    m.w = static_cast<i32>(half.x * 2.0f);
    m.h = static_cast<i32>(half.y * 2.0f);
    m.in.assign(static_cast<usize>(m.w) * m.h, 0);
    for (i32 y = 0; y < m.h; ++y)
        for (i32 x = 0; x < m.w; ++x) {
            const Vec2 q{static_cast<f32>(x) + 0.5f - half.x, static_cast<f32>(y) + 0.5f - half.y};
            m.in[static_cast<usize>(y) * m.w + x] = shape::signed_distance(sh, q, half) < 0.0f ? 1 : 0;
        }
    return m;
}

/// Fração das amostras que mudaram de lado entre duas grades do mesmo tamanho.
f32 shape_mask_change(const ShapeMaskCpu& a, const ShapeMaskCpu& b) {
    u32 n = 0;
    for (usize i = 0; i < a.in.size(); ++i) n += a.in[i] != b.in[i] ? 1u : 0u;
    return static_cast<f32>(n) / static_cast<f32>(std::max<usize>(1, a.in.size()));
}

/// Fração das amostras sem par no espelho esquerda ↔ direita.
f32 shape_mask_mirror_error(const ShapeMaskCpu& m) {
    u32 bad = 0, total = 0;
    for (i32 y = 0; y < m.h; ++y)
        for (i32 x = 0; x < m.w / 2; ++x) {
            ++total;
            if (m.at(x, y) != m.at(m.w - 1 - x, y)) ++bad;
        }
    return static_cast<f32>(bad) / static_cast<f32>(std::max(1u, total));
}

/// Fração das amostras sem par na rotação de meia volta (simetria central).
f32 shape_mask_point_error(const ShapeMaskCpu& m) {
    u32 bad = 0;
    for (i32 y = 0; y < m.h; ++y)
        for (i32 x = 0; x < m.w; ++x)
            if (m.at(x, y) != m.at(m.w - 1 - x, m.h - 1 - y)) ++bad;
    return static_cast<f32>(bad) / static_cast<f32>(std::max<usize>(1, m.in.size()));
}

ShapeData shape_of(u32 type) {
    ShapeData sh;
    sh.shapeType = type;
    sh.bounds = Rect{0.0f, 0.0f, 200.0f, 160.0f};
    sh.points = type == shape::kSeal ? 14.0f : type == shape::kWave ? 3.0f : type == shape::kBlob ? 4.0f : 5.0f;
    if (type == shape::kLine) sh.thickness = 0.3f;   // a linha no padrão (1) enche a caixa
    return sh;
}

// --- As contas de ANTES (shape.frag até a v43), para provar que as formas
// antigas desenham igual com os parâmetros novos no padrão. -------------------
f32 old_box(Vec2 q, Vec2 b) {
    const Vec2 d{std::fabs(q.x) - b.x, std::fabs(q.y) - b.y};
    return Vec2{std::max(d.x, 0.0f), std::max(d.y, 0.0f)}.length() + std::min(std::max(d.x, d.y), 0.0f);
}
f32 old_tri(Vec2 q, Vec2 p0, Vec2 p1, Vec2 p2) {
    auto dot = [](Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; };
    const Vec2 e0 = p1 - p0, e1 = p2 - p1, e2 = p0 - p2;
    const Vec2 v0 = q - p0, v1 = q - p1, v2 = q - p2;
    const Vec2 pq0 = v0 - e0 * std::clamp(dot(v0, e0) / dot(e0, e0), 0.0f, 1.0f);
    const Vec2 pq1 = v1 - e1 * std::clamp(dot(v1, e1) / dot(e1, e1), 0.0f, 1.0f);
    const Vec2 pq2 = v2 - e2 * std::clamp(dot(v2, e2) / dot(e2, e2), 0.0f, 1.0f);
    const f32 s = (e0.x * e2.y - e0.y * e2.x) >= 0.0f ? 1.0f : -1.0f;
    f32 dx = dot(pq0, pq0), dy = s * (v0.x * e0.y - v0.y * e0.x);
    dx = std::min(dx, dot(pq1, pq1)); dy = std::min(dy, s * (v1.x * e1.y - v1.y * e1.x));
    dx = std::min(dx, dot(pq2, pq2)); dy = std::min(dy, s * (v2.x * e2.y - v2.y * e2.x));
    return -std::sqrt(dx) * (dy > 0.0f ? 1.0f : (dy < 0.0f ? -1.0f : 0.0f));
}
f32 old_shape(const ShapeData& sh, Vec2 q, Vec2 half) {
    const f32 m = std::min(half.x, half.y);
    const Vec2 st{half.x / m, half.y / m};
    const f32 mst = std::min(st.x, st.y);
    ShapeData ellipse = sh;
    ellipse.shapeType = shape::kEllipse;
    switch (sh.shapeType) {
        case shape::kPie: return std::max(shape::signed_distance(ellipse, q, half), -std::max(-q.x, q.y));
        case shape::kFlower: {
            const f32 n = std::max(3.0f, sh.points);
            const f32 r = m * (0.72f + 0.28f * std::cos(n * std::atan2(q.y, q.x)));
            return (Vec2{q.x / st.x, q.y / st.y}.length() - r) * mst * 0.8f;
        }
        case shape::kArrow:
            return std::min(old_box(q - Vec2{-half.x * 0.25f, 0.0f}, Vec2{half.x * 0.75f, half.y * 0.22f}),
                            old_tri(q, Vec2{half.x * 0.1f, -half.y}, Vec2{half.x, 0.0f}, Vec2{half.x * 0.1f, half.y}));
        case shape::kGear: {
            const Vec2 p{q.x / st.x, q.y / st.y};
            const f32 r = m, n = std::max(3.0f, sh.points), hub = std::clamp(sh.innerRadius, 0.05f, 0.95f);
            const f32 root = r * 0.78f, sector = 2.0f * 3.14159265358979f / n;
            const f32 x = std::atan2(p.x, -p.y) + sector * 0.5f;
            const f32 a = x - sector * std::floor(x / sector) - sector * 0.5f;
            const f32 L = p.length();
            const Vec2 f{L * std::sin(a), L * std::cos(a)};
            const f32 tw = root * std::sin(sector * 0.25f);
            const f32 tooth = old_box(f - Vec2{0.0f, (root + r) * 0.5f}, Vec2{tw, (r - root) * 0.5f + 1e-3f});
            return std::max(std::min(L - root, tooth), hub * root - L) * mst;
        }
        case shape::kDoubleArrow: {
            const f32 t = half.y * std::clamp(sh.innerRadius, 0.05f, 0.95f);
            const f32 shaft = old_box(q, Vec2{half.x * 0.6f, t});
            const f32 right = old_tri(q, Vec2{half.x * 0.45f, -half.y}, Vec2{half.x, 0.0f}, Vec2{half.x * 0.45f, half.y});
            const f32 left = old_tri(q, Vec2{-half.x * 0.45f, half.y}, Vec2{-half.x, 0.0f}, Vec2{-half.x * 0.45f, -half.y});
            return std::min(shaft, std::min(left, right));
        }
        default: return shape::signed_distance(sh, q, half);
    }
}

constexpr u32 kNewShapeTypes[] = {shape::kLine, shape::kDiamond, shape::kHeart, shape::kSeal, shape::kArc,
                                  shape::kBubble, shape::kBolt, shape::kWave, shape::kBlob};

} // namespace

AUREA_TEST(Shape2D, NewTypesAreClosedAndSymmetricWhereExpected) {
    const Vec2 half{100.0f, 80.0f};
    for (u32 type : kNewShapeTypes) {
        const ShapeData sh = shape_of(type);
        const ShapeMaskCpu m = shape_mask_cpu(sh, half);
        const f32 fill = static_cast<f32>(m.count()) / static_cast<f32>(m.in.size());
        // Borda da caixa sempre fora (forma fechada dentro da caixa).
        u32 edgeIn = 0;
        for (i32 x = 0; x < m.w; ++x) edgeIn += (m.at(x, 0) ? 1u : 0u) + (m.at(x, m.h - 1) ? 1u : 0u);
        for (i32 y = 0; y < m.h; ++y) edgeIn += (m.at(0, y) ? 1u : 0u) + (m.at(m.w - 1, y) ? 1u : 0u);
        std::printf("    tipo %u: %.3f da caixa, borda %u, espelho %.4f, centro %.4f\n", type, fill, edgeIn,
                    shape_mask_mirror_error(m), shape_mask_point_error(m));
        AUREA_CHECK_MSG(fill > 0.04f && fill < 0.97f, "forma nova vazia ou enchendo a caixa");
        // Linha e balão (o corpo é um retângulo) encostam na borda de cima por
        // definição; o resto só toca nela num ponto ou numa crista.
        u32 topIn = 0;
        for (i32 x = 0; x < m.w; ++x) topIn += m.at(x, 0) ? 1u : 0u;
        if (type != shape::kLine && type != shape::kBubble)
            AUREA_CHECK_MSG(topIn < static_cast<u32>(m.w) / 3, "forma nova cobrindo a borda de cima");
        const bool mirror = type == shape::kLine || type == shape::kDiamond || type == shape::kHeart || type == shape::kSeal;
        if (mirror) AUREA_CHECK_MSG(shape_mask_mirror_error(m) < 0.003f, "forma deveria ser simétrica esquerda/direita");
        if (type == shape::kWave || type == shape::kLine) AUREA_CHECK_MSG(shape_mask_point_error(m) < 0.003f, "onda/linha: simetria central");
        // O centro da caixa: dentro em todas, menos no arco (é um aro).
        const bool centre = shape::signed_distance(sh, Vec2{0.0f, 0.0f}, half) < 0.0f;
        AUREA_CHECK_MSG(type == shape::kArc ? !centre : centre, "centro da forma no lado errado");
    }
}

AUREA_TEST(Shape2D, EveryParameterChangesTheGeometry) {
    const Vec2 half{100.0f, 80.0f};
    struct Case { u32 type; u32 param; f32 a; f32 b; };
    const Case cases[] = {
        {shape::kHeart, shape::kParamDepth, 0.15f, 0.85f},
        {shape::kSeal, shape::kParamDepth, 0.1f, 1.0f},
        {shape::kSeal, shape::kParamCount, 6.0f, 20.0f},
        {shape::kGear, shape::kParamDepth, 0.1f, 0.4f},
        {shape::kFlower, shape::kParamDepth, 0.1f, 0.5f},
        {shape::kBlob, shape::kParamDepth, 0.1f, 0.9f},
        {shape::kBlob, shape::kParamSeed, 0.0f, 7.0f},
        {shape::kBlob, shape::kParamCount, 3.0f, 7.0f},
        {shape::kDiamond, shape::kParamTip, 0.2f, 0.8f},
        {shape::kBubble, shape::kParamTip, 0.1f, 0.9f},
        {shape::kBubble, shape::kParamCorner, 0.0f, 40.0f},
        {shape::kBolt, shape::kParamTip, 0.1f, 0.9f},
        {shape::kLine, shape::kParamThickness, 0.2f, 0.9f},
        {shape::kArc, shape::kParamThickness, 0.1f, 0.4f},
        {shape::kWave, shape::kParamThickness, 0.2f, 0.6f},
        {shape::kWave, shape::kParamCount, 1.0f, 5.0f},
        {shape::kWave, shape::kParamAmplitude, 0.0f, 1.0f},
        {shape::kArc, shape::kParamSweep, 90.0f, 300.0f},
        {shape::kPie, shape::kParamSweep, 45.0f, 200.0f},
        {shape::kArrow, shape::kParamHead, 0.2f, 0.7f},
        {shape::kArrow, shape::kParamShaft, 0.15f, 0.8f},
        {shape::kDoubleArrow, shape::kParamHead, 0.15f, 0.4f},
    };
    for (const Case& c : cases) {
        ShapeData a = shape_of(c.type), b = shape_of(c.type);
        *shape::param_field(a, c.param) = c.a;
        *shape::param_field(b, c.param) = c.b;
        const f32 change = shape_mask_change(shape_mask_cpu(a, half), shape_mask_cpu(b, half));
        std::printf("    tipo %u, parâmetro %u: %.3f da caixa mudou\n", c.type, c.param, change);
        AUREA_CHECK_MSG(change > 0.01f, "o parâmetro não mexeu na forma");
    }
    // Abertura: 270° é a fatia de sempre; mais aberta = mais área.
    ShapeData pie = shape_of(shape::kPie);
    pie.sweep = 90.0f;
    const u32 quarter = shape_mask_cpu(pie, half).count();
    pie.sweep = 360.0f;
    const u32 full = shape_mask_cpu(pie, half).count();
    AUREA_CHECK(std::fabs(static_cast<f32>(quarter) / static_cast<f32>(full) - 0.25f) < 0.02f);
}

AUREA_TEST(Shape2D, OldShapesKeepTheExactGeometryOfBefore) {
    // Projeto antigo (sem os campos novos) e forma nova no padrão: a conta é a
    // de antes — fatia de 3/4, seta, engrenagem, flor e seta dupla.
    const u32 types[] = {shape::kPie, shape::kArrow, shape::kGear, shape::kFlower, shape::kDoubleArrow};
    for (u32 type : types) {
        for (const Vec2 half : {Vec2{100.0f, 80.0f}, Vec2{60.0f, 60.0f}, Vec2{140.0f, 50.0f}}) {
            ShapeData sh = shape_of(type);
            sh.points = 12.0f;
            sh.innerRadius = 0.3f;
            f32 worst = 0.0f;
            for (f32 y = -half.y - 4.0f; y <= half.y + 4.0f; y += 1.7f)
                for (f32 x = -half.x - 4.0f; x <= half.x + 4.0f; x += 1.3f) {
                    const Vec2 q{x, y};
                    worst = std::max(worst, std::fabs(shape::signed_distance(sh, q, half) - old_shape(sh, q, half)));
                }
            std::printf("    tipo %u (%gx%g): maior diferença %.6f px\n", type, half.x, half.y, worst);
            AUREA_CHECK_MSG(worst < 2e-3f, "forma antiga mudou de geometria");
        }
    }
    // Os padrões "da forma" valem o que a conta antiga tinha fixo.
    ShapeData gear = shape_of(shape::kGear);
    AUREA_CHECK(std::fabs(shape::param_value(gear, shape::kParamDepth) - 0.22f) < 1e-6f);
    gear.shapeType = shape::kFlower;
    AUREA_CHECK(std::fabs(shape::param_value(gear, shape::kParamDepth) - 0.28f) < 1e-6f);
    gear.shapeType = shape::kDoubleArrow;
    AUREA_CHECK(std::fabs(shape::param_value(gear, shape::kParamHead) - 0.275f) < 1e-6f);
    gear.shapeType = shape::kArrow;
    AUREA_CHECK(std::fabs(shape::param_value(gear, shape::kParamHead) - 0.45f) < 1e-6f);
    AUREA_CHECK(std::fabs(shape::param_value(gear, shape::kParamSweep) - 270.0f) < 1e-6f);
}

AUREA_TEST(Shape2D, ParamsAnimateUndoSaveAndReopen) {
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    auto seek = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    };
    auto params = [&](u64 layer) {
        std::vector<f32> out(Engine::kShapeParamFloats);
        AUREA_CHECK(e.query_shape_params(layer, out.data(), Engine::kShapeParamFloats) == Engine::kShapeParamFloats);
        return out;
    };
    // Todo ladrilho novo cria a forma certa (23 = explosão, 24..32 = tipos 16..24).
    const u32 expected[] = {shape::kStar, shape::kLine, shape::kDiamond, shape::kHeart, shape::kSeal,
                            shape::kArc, shape::kBubble, shape::kBolt, shape::kWave, shape::kBlob};
    std::vector<u64> ids;
    for (u32 i = 0; i < std::size(expected); ++i) {
        auto id = e.add_shape(23 + i);
        AUREA_CHECK(id.ok());
        if (!id.ok()) return;
        ids.push_back(*id);
        const std::vector<f32> v = params(*id);
        AUREA_CHECK_EQ(static_cast<u32>(v[0]), expected[i]);
    }
    const u64 heart = ids[3], wave = ids[8], blob = ids[9];
    // Padrões resolvidos: coração 0,4, onda espessura 0,4 e amplitude 1.
    AUREA_CHECK(std::fabs(params(heart)[shape::kParamDepth] - 0.4f) < 1e-6f);
    AUREA_CHECK(std::fabs(params(wave)[shape::kParamThickness] - 0.4f) < 1e-6f);
    AUREA_CHECK(std::fabs(params(wave)[shape::kParamAmplitude] - 1.0f) < 1e-6f);
    AUREA_CHECK_EQ(static_cast<u32>(params(wave)[shape::kParamCount]), 3u);

    // Valor parado + desfazer.
    seek(0);
    AUREA_CHECK(e.set_shape_param(heart, shape::kParamDepth, 0.8f, false));
    AUREA_CHECK(std::fabs(params(heart)[shape::kParamDepth] - 0.8f) < 1e-6f);
    Command undo;
    undo.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(undo).ok());
    AUREA_CHECK(std::fabs(params(heart)[shape::kParamDepth] - 0.4f) < 1e-6f);
    Command redo;
    redo.type = CommandType::Redo;
    AUREA_CHECK(e.apply_command(redo).ok());
    AUREA_CHECK(std::fabs(params(heart)[shape::kParamDepth] - 0.8f) < 1e-6f);
    // Faixa: amplitude acima de 1 volta para 1; ondas aceitam 1.
    AUREA_CHECK(e.set_shape_param(wave, shape::kParamAmplitude, 3.0f, false));
    AUREA_CHECK(std::fabs(params(wave)[shape::kParamAmplitude] - 1.0f) < 1e-6f);
    AUREA_CHECK(e.set_shape_param(wave, shape::kParamCount, 1.0f, false));
    AUREA_CHECK_EQ(static_cast<u32>(params(wave)[shape::kParamCount]), 1u);

    // Keyframes: o primeiro copia o valor efetivo (o padrão da forma), o
    // segundo muda no quadro 30; no meio o valor fica entre os dois.
    AUREA_CHECK(e.ensure_shape_param_key(wave, shape::kParamThickness));
    std::vector<f32> v = params(wave);
    AUREA_CHECK(std::fabs(v[shape::kParamThickness] - 0.4f) < 1e-6f);
    AUREA_CHECK((static_cast<u32>(v[Engine::kShapeParamCount]) & (1u << shape::kParamThickness)) != 0);
    AUREA_CHECK((static_cast<u32>(v[Engine::kShapeParamCount + 1]) & (1u << shape::kParamThickness)) != 0);
    seek(30);
    AUREA_CHECK(e.set_shape_param(wave, shape::kParamThickness, 0.8f, false));
    seek(15);
    const f32 mid = params(wave)[shape::kParamThickness];
    AUREA_CHECK(mid > 0.45f && mid < 0.75f);
    AUREA_CHECK(e.set_shape_param(blob, shape::kParamSeed, 12.4f, false));
    AUREA_CHECK(std::fabs(params(blob)[shape::kParamSeed] - 12.0f) < 1e-6f);

    // O comando também chega aos parâmetros novos e aos tipos novos.
    Command c;
    c.type = CommandType::ShapeSetParam;
    c.shape_param = ShapeParamPayload{LayerId::unpack(ids[5]), shape::kParamSweep, 120.0f};
    AUREA_CHECK(e.apply_command(c).ok());
    AUREA_CHECK(std::fabs(params(ids[5])[shape::kParamSweep] - 120.0f) < 1e-6f);
    c.shape_param = ShapeParamPayload{LayerId::unpack(ids[0]), 0, static_cast<f32>(shape::kBlob)};
    AUREA_CHECK(e.apply_command(c).ok());
    AUREA_CHECK_EQ(static_cast<u32>(params(ids[0])[0]), static_cast<u32>(shape::kBlob));

    // Salvar e reabrir: todos os valores (parados e animados) voltam iguais.
    std::vector<std::vector<f32>> before;
    for (u64 id : ids) before.push_back(params(id));
    const char* path = "aurea_teste_formas_param.aurea";
    AUREA_CHECK(e.save_project(path).ok());
    AUREA_CHECK(e.load_project(path).ok());
    seek(15);
    for (usize i = 0; i < ids.size(); ++i) {
        const std::vector<f32> after = params(ids[i]);
        for (u32 p = 0; p < Engine::kShapeParamFloats; ++p)
            AUREA_CHECK_MSG(std::fabs(after[p] - before[i][p]) < 1e-5f, "parâmetro da forma mudou ao reabrir");
    }
    std::remove(path);
    e.shutdown();
}

AUREA_TEST(Gpu, ParametricShapesRenderMatchTheCpuAndReactToParams) {
    AUREA_REQUIRE_GPU();
    for (u32 preset = 23; preset <= 32; ++preset) {
        Scene3DRig rig(256, 256);
        auto id = rig.e.add_shape(preset);
        AUREA_CHECK(id.ok());
        if (!id.ok()) continue;
        const Image8 img = rig.capture(256);
        const f32 cov = coverage(img);
        char name[48];
        std::snprintf(name, sizeof(name), "forma_%02u.png", preset);
        (void)write_png(name, img);
        // A conta da CPU no centro de cada pixel (forma no centro do quadro, 1:1).
        Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
        const Layer* l = comp->layer(LayerId::unpack(*id));
        const Vec2 half{l->shape.bounds.w * 0.5f, l->shape.bounds.h * 0.5f};
        u32 checked = 0, wrong = 0;
        for (u32 y = 0; y < img.height; ++y)
            for (u32 x = 0; x < img.width; ++x) {
                const Vec2 q{static_cast<f32>(x) + 0.5f - 128.0f, static_cast<f32>(y) + 0.5f - 128.0f};
                if (std::fabs(q.x) > half.x + 2.0f || std::fabs(q.y) > half.y + 2.0f) continue;
                const f32 d = shape::signed_distance(l->shape, q, half);
                if (std::fabs(d) < 2.0f) continue;   // franja do antisserrilhado
                ++checked;
                const bool lit = img.at(x, y)[0] > 127;
                if (lit != (d < 0.0f)) ++wrong;
            }
        std::printf("    ladrilho %u (tipo %u): cobertura %.3f, %u de %u pixels fora da conta da CPU\n", preset,
                    l->shape.shapeType, cov, wrong, checked);
        AUREA_CHECK_MSG(cov > 0.004f && cov < 0.5f, "forma nova sem pixels ou enchendo o quadro");
        AUREA_CHECK_MSG(checked > 0 && wrong * 200 <= checked, "GPU e CPU discordam da forma");
    }
    // Um parâmetro novo muda os pixels; desfazer volta ao quadro de antes;
    // salvar e reabrir desenha igual.
    Scene3DRig rig(256, 256);
    auto id = rig.e.add_shape(26);   // coração
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    const Image8 before = rig.capture(256);
    AUREA_CHECK(rig.e.set_shape_param(*id, shape::kParamDepth, 0.9f, false));
    const Image8 deeper = rig.capture(256);
    u32 changed = 0;
    for (usize i = 0; i < before.rgba.size(); i += 4) changed += before.rgba[i] != deeper.rgba[i] ? 1u : 0u;
    std::printf("    coração mais fundo: %u pixels mudaram\n", changed);
    AUREA_CHECK(changed > 100);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_coracao.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    const Image8 reopened = rig.capture(256);
    AUREA_CHECK(reopened.rgba == deeper.rgba);
    std::remove(path.c_str());
    // Arco pela metade: continua desenhando (abertura animável sem sumir).
    auto arc = rig.e.add_shape(28);
    AUREA_CHECK(arc.ok());
    if (!arc.ok()) return;
    AUREA_CHECK(rig.e.set_shape_param(*arc, shape::kParamSweep, 180.0f, false));
    const Image8 arcImg = rig.capture(256);
    AUREA_CHECK(coverage(arcImg) > coverage(reopened));
}
