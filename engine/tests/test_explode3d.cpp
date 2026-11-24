// Explosão (aurea.3d.explode, scene3d/Explode3D.cpp): a fratura respeita o
// número de peças e é determinística; o voo é fechado (progresso 0 = nada
// muda); no motor com GPU o objeto (forma 3D, texto 3D, glTF importado) muda
// com o progresso, progresso 0 desenha IGUAL ao objeto sem o efeito, o mesmo
// quadro sai igual duas vezes e depois de ir e voltar no tempo, e o projeto
// salvo/reaberto desenha igual. Com AUREA_FX_DUMP, grava as sequências de
// demonstração em build/agents/explosao/.
#include "TestFramework.hpp"
#include "ImageIO.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/platform/DevicePolicy.hpp"
#include "aurea/scene3d/Explode3D.hpp"
#include "aurea/scene3d/Importer.hpp"
#include "aurea/scene3d/SceneAsset.hpp"
#include "aurea/scene3d/Shape3D.hpp"
#include "aurea/scene3d/Text3D.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;
namespace ex = aurea::scene3d::explode3d;

namespace {

std::string gltf_path(const char* rel) { return std::string(AUREA_TEST_DATA_DIR) + "/gltf/" + rel; }

bool have_gltf(const char* rel) {
    std::FILE* f = std::fopen(gltf_path(rel).c_str(), "rb");
    if (!f) {
        std::printf("\n    (modelo de amostra ausente: %s — parte pulada)\n", rel);
        return false;
    }
    std::fclose(f);
    return true;
}

std::shared_ptr<const SceneAsset> load_gltf(const char* rel) {
    ImportOptions o;
    ImportResult r = import_gltf_file(gltf_path(rel), o);
    if (!r.ok()) return nullptr;
    return std::shared_ptr<const SceneAsset>(std::move(r.asset));
}

u64 geometry_hash(const SceneAsset& a) {
    u64 h = 1469598103934665603ull;
    auto add = [&](const void* p, usize n) {
        const u8* b = static_cast<const u8*>(p);
        for (usize i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    };
    for (const Node& n : a.nodes) { add(&n.translation, sizeof(Vec3)); add(&n.parent, sizeof(i32)); add(&n.mesh, sizeof(i32)); }
    for (const Mesh& m : a.meshes)
        for (const Primitive& p : m.primitives) {
            add(p.positions.data(), p.positions.size() * sizeof(Vec3));
            add(p.indices.data(), p.indices.size() * sizeof(u32));
            add(&p.material, sizeof(i32));
        }
    return h;
}

u32 surface_triangles(const SceneAsset& a, const ex::Fractured& f, bool caps) {
    u32 n = 0;
    for (u32 i = 0; i < f.fragments; ++i) {
        const Mesh& m = a.meshes[static_cast<usize>(a.nodes[f.firstFragment + i].mesh)];
        for (const Primitive& p : m.primitives) {
            const bool cap = p.material >= static_cast<i32>(f.baseMaterials);
            if (cap == caps) n += p.triangle_count();
        }
    }
    return n;
}

} // namespace

AUREA_TEST(Explode3D, FractureRespectsPieceCountAndIsDeterministic) {
    AUREA_CHECK(ex::piece_cap_for(DeviceTier::Low) == 250);
    AUREA_CHECK(ex::piece_cap_for(DeviceTier::Mid) == 600);
    AUREA_CHECK(ex::piece_cap_for(DeviceTier::High) == 1200);
    AUREA_CHECK(ex::piece_cap_for(DeviceTier::Ultra) == ex::kMaxPieces);

    // Uma forma 3D gerada (esfera) — não depende dos modelos de amostra.
    ImportResult sphere = build_shape3d(default_shape3d(Shape3DKind::Sphere));
    AUREA_CHECK(sphere.ok());
    if (!sphere.ok()) return;
    const std::shared_ptr<const SceneAsset> src(std::move(sphere.asset));
    for (const u32 pieces : {8u, 60u, 300u}) {
        ex::clear_cache();
        const auto t0 = std::chrono::steady_clock::now();
        const auto f = ex::fracture(src, 42, ex::FractureSpec{pieces, 7, 0.6f});
        const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
        AUREA_CHECK(f != nullptr);
        if (!f) return;
        const SceneAsset& a = *f->asset;
        std::printf("\n    esfera: %u peças pedidas → %u peças, %u tri de superfície, %u tri de tampa (%.1f ms)", pieces,
                    f->fragments, surface_triangles(a, *f, false), surface_triangles(a, *f, true), ms);
        AUREA_CHECK(f->fragments <= pieces);
        AUREA_CHECK(f->fragments * 10 >= pieces * 8);   // ≥ 80% (célula vazia some)
        AUREA_CHECK(f->firstFragment == src->nodes.size());
        AUREA_CHECK(a.nodes.size() == f->firstFragment + f->fragments);
        AUREA_CHECK(ex::fractured_of(a) == f.get());
        AUREA_CHECK(ex::fractured_of(*src) == nullptr);
        AUREA_CHECK(!f->interiorOf.empty());
        AUREA_CHECK(surface_triangles(a, *f, true) > 0);
        // Os nós originais ficam (hierarquia/animação) sem geometria.
        for (u32 n = 0; n < f->firstFragment; ++n) {
            const i32 m = a.nodes[n].mesh;
            if (m >= 0) AUREA_CHECK(a.meshes[static_cast<usize>(m)].primitives.empty());
        }
        // Mesma entrada → mesma geometria (sem cache).
        const u64 h1 = geometry_hash(a);
        ex::clear_cache();
        const auto g = ex::fracture(src, 42, ex::FractureSpec{pieces, 7, 0.6f});
        AUREA_CHECK(g && geometry_hash(*g->asset) == h1);
        AUREA_CHECK(g && g->key == f->key);
        // Outra semente → outras peças.
        const auto other = ex::fracture(src, 42, ex::FractureSpec{pieces, 8, 0.6f});
        AUREA_CHECK(other && geometry_hash(*other->asset) != h1);
    }
    // Sem espessura: casca oca, sem tampa nem material de interior.
    const auto hollow = ex::fracture(src, 42, ex::FractureSpec{60, 0, 0.0f});
    AUREA_CHECK(hollow && hollow->interiorOf.empty() && surface_triangles(*hollow->asset, *hollow, true) == 0);
    // A superfície das peças cobre a área original (nada perdido na fratura).
    {
        auto area_of = [](const SceneAsset& a, i32 maxMaterial) {
            f64 s = 0.0;
            for (const Mesh& m : a.meshes) for (const Primitive& p : m.primitives) {
                if (maxMaterial >= 0 && p.material >= maxMaterial) continue;
                for (usize t = 0; t + 2 < p.indices.size(); t += 3) {
                    const Vec3 x = p.positions[p.indices[t]], y = p.positions[p.indices[t + 1]], z = p.positions[p.indices[t + 2]];
                    s += 0.5 * static_cast<f64>((y - x).cross(z - x).length());
                }
            }
            return s;
        };
        const f64 before = area_of(*src, -1);
        const f64 after = area_of(*hollow->asset, -1);
        std::printf("\n    área: original %.5f, peças %.5f", before, after);
        AUREA_CHECK(std::fabs(after - before) <= before * 1e-3);
    }

    if (have_gltf("Box.glb")) {
        const auto box = load_gltf("Box.glb");
        AUREA_CHECK(box != nullptr);
        if (box) {
            const auto f = ex::fracture(box, 7, ex::FractureSpec{60, 1, 0.5f});
            AUREA_CHECK(f && f->fragments <= 60 && f->fragments >= 48);
            if (f) std::printf("\n    Box.glb: 60 → %u peças", f->fragments);
        }
    }
    if (have_gltf("DamagedHelmet.glb")) {
        const auto helmet = load_gltf("DamagedHelmet.glb");
        if (helmet) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto f = ex::fracture(helmet, 9, ex::FractureSpec{2000, 3, 0.6f});
            const f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
            AUREA_CHECK(f && f->fragments <= 2000 && f->fragments >= 1600);
            // As texturas não são copiadas: os pixels são os do original.
            if (f && !helmet->images.empty() && !helmet->images[0].pixels().empty())
                AUREA_CHECK(f->asset->images[0].pixels().data() == helmet->images[0].pixels().data());
            if (f) std::printf("\n    DamagedHelmet: 2000 → %u peças em %.0f ms\n", f->fragments, ms);
        }
    }
    ex::clear_cache();
}

AUREA_TEST(Explode3D, MotionIsClosedFormAndStartsAtRest) {
    ex::Values v;
    v.spec.seed = 5;
    const Vec3 c{0.3f, -0.2f, 0.1f};
    // Progresso 0: identidade exata, opacidade e escala 1.
    v.progress = 0.0f;
    ex::PieceMotion m = ex::piece_motion(v, 3, c, Vec3{}, 1.0f, 1.0f, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    for (u32 col = 0; col < 4; ++col) {
        const Vec4 a = m.delta.col[col], b = Mat4::identity().col[col];
        AUREA_CHECK(a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w);
    }
    AUREA_CHECK(m.opacity == 1.0f && m.scale == 1.0f);
    // Mesma entrada → mesma matriz; o voo cresce com o progresso.
    f32 last = 0.0f;
    for (const f32 p : {0.25f, 0.5f, 1.0f}) {
        v.progress = p;
        const ex::PieceMotion a = ex::piece_motion(v, 3, c, Vec3{}, 1.0f, 1.0f, Vec3{0, 0, -1}, Vec3{0, 1, 0});
        const ex::PieceMotion b = ex::piece_motion(v, 3, c, Vec3{}, 1.0f, 1.0f, Vec3{0, 0, -1}, Vec3{0, 1, 0});
        AUREA_CHECK(std::memcmp(&a.delta, &b.delta, sizeof(Mat4)) == 0);
        const Vec3 moved = a.delta.transform_point(c);
        const f32 dist = (moved - c).length();
        AUREA_CHECK(dist > last);
        last = dist;
    }
    // Onda: a peça longe do impacto sai depois.
    v.progress = 0.2f;
    v.wave = 0.6f;
    const ex::PieceMotion nearP = ex::piece_motion(v, 1, Vec3{0.05f, 0, 0}, Vec3{}, 1.0f, 1.0f, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    const ex::PieceMotion farP = ex::piece_motion(v, 1, Vec3{1.0f, 0, 0}, Vec3{}, 1.0f, 1.0f, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    AUREA_CHECK((nearP.delta.transform_point(Vec3{0.05f, 0, 0}) - Vec3{0.05f, 0, 0}).length() > 1e-3f);
    AUREA_CHECK((farP.delta.transform_point(Vec3{1.0f, 0, 0}) - Vec3{1.0f, 0, 0}).length() < 1e-6f);
    // Escala no fim 0 e esmaecer 100%: some no fim.
    v.wave = 0.0f; v.progress = 1.0f; v.endScale = 0.0f; v.fade = 1.0f;
    const ex::PieceMotion gone = ex::piece_motion(v, 2, c, Vec3{}, 1.0f, 1.0f, Vec3{0, 0, -1}, Vec3{0, 1, 0});
    AUREA_CHECK(gone.scale < 1e-3f && gone.opacity < 1e-3f);
}

#if defined(AUREA_TEST_VULKAN)

#include "VulkanBackend.hpp"
#include "aurea/Engine.hpp"

namespace {

struct ExplodeRig {
    Engine e;
    bool ok = false;
    explicit ExplodeRig(u32 w = 320, u32 h = 180) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ok = e.initialize(ec).ok() && e.new_project(w, h, 30.0, nullptr).ok();
    }
    ~ExplodeRig() { if (ok) e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    test::Image8 capture(u32 maxDim = 320) {
        test::Image8 img;
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        if (!e.capture_frame_rgba(maxDim, rgba, w, h).ok()) return img;
        img.width = w; img.height = h; img.rgba = std::move(rgba);
        return img;
    }
    void seek(i64 frame) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    }
    /// Põe a Explosão na camada; devolve o id do efeito.
    EffectId add_explode(u64 layer) {
        Command c;
        c.type = CommandType::EffectAdd;
        c.effect_add.layer = LayerId::unpack(layer);
        c.effect_add.effectType = effect_type_id(effect_keys::kExplode3D);
        c.effect_add.index = kInvalidIndex;
        AUREA_CHECK(e.apply_command(c).ok());
        const Layer* l = comp()->layer(LayerId::unpack(layer));
        return l && !l->effects.empty() ? EffectId{l->effects.back().id, 0} : EffectId{};
    }
    void set(u64 layer, EffectId fx, u32 param, f32 value) {
        Command c;
        c.type = CommandType::EffectSetParam;
        c.effect_param.layer = LayerId::unpack(layer);
        c.effect_param.effect = fx;
        c.effect_param.paramIndex = param;
        c.effect_param.value = value;
        AUREA_CHECK(e.apply_command(c).ok());
    }
};

u64 diff_pixels(const test::Image8& a, const test::Image8& b, u32 tol = 0) {
    if (a.rgba.size() != b.rgba.size() || a.rgba.empty()) return ~0ull;
    u64 n = 0;
    for (usize i = 0; i < a.rgba.size(); i += 4) {
        u32 d = 0;
        for (u32 c = 0; c < 4; ++c) d = std::max<u32>(d, static_cast<u32>(std::abs(static_cast<i32>(a.rgba[i + c]) - static_cast<i32>(b.rgba[i + c]))));
        if (d > tol) ++n;
    }
    return n;
}

u64 lit(const test::Image8& a) {
    u64 n = 0;
    for (usize i = 0; i < a.rgba.size(); i += 4) if (a.rgba[i] > 10 || a.rgba[i + 1] > 10 || a.rgba[i + 2] > 10) ++n;
    return n;
}

/// Um objeto de cada tipo: 0 forma 3D, 1 texto 3D, 2 glTF importado.
u64 add_object(ExplodeRig& rig, u32 kind) {
    Result<u64> id = Status{Errc::InvalidState};
    if (kind == 0) id = rig.e.add_shape3d(static_cast<u32>(Shape3DKind::Cube), "Cubo");
    else if (kind == 1) {
        Text3DSpec spec;
        spec.content = "AUREA";
        spec.color = Vec4{1.0f, 0.75f, 0.2f, 1.0f};
        id = rig.e.add_text3d(spec);
    } else {
        if (!have_gltf("DamagedHelmet.glb")) return 0;
        ModelImport mi;
        mi.path = gltf_path("DamagedHelmet.glb");
        id = rig.e.import_model(mi);
    }
    AUREA_CHECK(id.ok());
    if (!id.ok()) return 0;
    if (Layer* l = rig.comp()->layer(LayerId::unpack(*id))) {
        l->transform.rotation = Vec3{18.0f, kind == 2 ? 20.0f : 32.0f, 0.0f};
        l->end = FrameIndex{300};
    }
    return *id;
}

const char* kObjectName[] = {"cubo", "texto3d", "gltf"};

} // namespace

AUREA_TEST(Explode3DGpu, ProgressZeroIsIdenticalAndExplosionChangesTheFrame) {
    for (u32 kind = 0; kind < 3; ++kind) {
        ExplodeRig rig;
        if (!rig.ok) { std::printf("(sem GPU Vulkan: pulado) "); return; }
        const u64 id = add_object(rig, kind);
        if (!id) continue;
        rig.seek(10);
        const test::Image8 original = rig.capture();
        if (lit(original) == 0) { std::printf("(sem GPU: quadro vazio, pulado) "); return; }
        const EffectId fx = rig.add_explode(id);
        const test::Image8 atZero = rig.capture();
        rig.set(id, fx, ex::kProgress, 20.0f);
        const test::Image8 at20 = rig.capture();
        rig.set(id, fx, ex::kProgress, 60.0f);
        const test::Image8 at60 = rig.capture();
        const test::Image8 at60again = rig.capture();
        const u64 d0 = diff_pixels(original, atZero), d20 = diff_pixels(original, at20, 8), d60 = diff_pixels(original, at60, 8);
        const u64 d2060 = diff_pixels(at20, at60, 8), again = diff_pixels(at60, at60again);
        std::printf("\n    %-8s acesos %llu | p0 difere %llu | p20 %llu | p60 %llu | p20→p60 %llu | repetido %llu",
                    kObjectName[kind], static_cast<unsigned long long>(lit(original)), static_cast<unsigned long long>(d0),
                    static_cast<unsigned long long>(d20), static_cast<unsigned long long>(d60),
                    static_cast<unsigned long long>(d2060), static_cast<unsigned long long>(again));
        AUREA_CHECK_MSG(d0 == 0, "progresso 0 tem de desenhar igual ao objeto sem o efeito");
        AUREA_CHECK(d20 > lit(original) / 50);
        AUREA_CHECK(d60 > d20);
        AUREA_CHECK(d2060 > 0);
        AUREA_CHECK_MSG(again == 0, "o mesmo quadro tem de sair igual");
    }
}

AUREA_TEST(Explode3DGpu, AutomaticTimingSeeksDeterministicallyAndSurvivesSaveReopen) {
    ExplodeRig rig;
    if (!rig.ok) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const u64 id = add_object(rig, 0);
    if (!id) return;
    const EffectId fx = rig.add_explode(id);
    rig.set(id, fx, ex::kTiming, static_cast<f32>(ex::kTimingAuto));
    rig.set(id, fx, ex::kStart, 0.2f);
    rig.set(id, fx, ex::kDuration, 1.0f);
    rig.set(id, fx, ex::kPieces, 90.0f);
    rig.seek(3);   // antes do começo: intacto
    const test::Image8 before = rig.capture();
    rig.seek(18);
    const test::Image8 a = rig.capture();
    rig.seek(28);
    const test::Image8 b = rig.capture();
    rig.seek(18);
    const test::Image8 a2 = rig.capture();
    if (lit(a) == 0) { std::printf("(sem GPU: quadro vazio, pulado) "); return; }
    std::printf("\n    auto: 18≠3 %llu, 28≠18 %llu, volta 18 %llu",
                static_cast<unsigned long long>(diff_pixels(before, a, 8)),
                static_cast<unsigned long long>(diff_pixels(a, b, 8)), static_cast<unsigned long long>(diff_pixels(a, a2)));
    AUREA_CHECK(diff_pixels(before, a, 8) > 100);
    AUREA_CHECK(diff_pixels(a, b, 8) > 100);
    AUREA_CHECK_MSG(diff_pixels(a, a2) == 0, "voltar ao quadro tem de dar o mesmo quadro");

    // Montar (ao contrário): o fim volta a ser o objeto inteiro.
    rig.set(id, fx, ex::kReassemble, 1.0f);
    rig.seek(60);
    const test::Image8 assembled = rig.capture();
    rig.set(id, fx, ex::kReassemble, 0.0f);
    rig.seek(3);
    const test::Image8 intact = rig.capture();
    AUREA_CHECK_MSG(diff_pixels(assembled, intact) == 0, "Montar termina no objeto inteiro");

    // Salvar e reabrir: o efeito e os valores voltam, o quadro é o mesmo.
    rig.seek(18);
    const test::Image8 saved = rig.capture();
    const char* tmp = std::getenv("TEMP");
    const std::string path = std::string(tmp ? tmp : ".") + "/aurea_teste_explosao.aurea";
    AUREA_CHECK(rig.e.save_project(path.c_str()).ok());
    AUREA_CHECK(rig.e.load_project(path.c_str()).ok());
    u64 found = 0;
    const Layer* reopened = nullptr;
    rig.comp()->layers().for_each([&](LayerId lid, const Layer& l) {
        for (const auto& eff : l.effects)
            if (eff.type == effect_type_id(effect_keys::kExplode3D)) { found = lid.pack(); reopened = &l; }
    });
    AUREA_CHECK(found != 0 && reopened != nullptr);
    if (reopened) {
        const EffectInstance* inst = ex::find(*reopened);
        AUREA_CHECK(inst && inst->params.size() >= ex::kParamCount);
        if (inst) {
            AUREA_CHECK(inst->params[ex::kTiming].constant.as_enum() == ex::kTimingAuto);
            AUREA_CHECK(inst->params[ex::kPieces].constant.as_int() == 90);
            AUREA_CHECK(std::fabs(inst->params[ex::kDuration].constant.as_float() - 1.0f) < 1e-5f);
        }
    }
    rig.seek(18);
    const test::Image8 again = rig.capture();
    std::printf("\n    reaberto: difere %llu px", static_cast<unsigned long long>(diff_pixels(saved, again)));
    AUREA_CHECK_MSG(diff_pixels(saved, again) == 0, "o projeto reaberto desenha a mesma explosão");
    std::remove(path.c_str());
}

AUREA_TEST(Explode3DGpu, PresetsAndMotionBlurRender) {
    ExplodeRig rig;
    if (!rig.ok) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const u64 id = add_object(rig, 0);
    if (!id) return;
    const EffectId fx = rig.add_explode(id);
    // Os 5 presets aplicam (Impacto lateral põe o impacto no lado −X).
    for (u32 p = 0; p < 5; ++p) AUREA_CHECK(rig.e.apply_effect_preset(id, fx.index, p));
    const Layer* l = rig.comp()->layer(LayerId::unpack(id));
    const EffectInstance* inst = l ? ex::find(*l) : nullptr;
    AUREA_CHECK(inst && std::fabs(inst->params[ex::kImpact].constant.v[0] + 100.0f) < 1e-4f);
    AUREA_CHECK(inst && std::fabs(inst->params[ex::kDirection].constant.v[0] - 1.0f) < 1e-4f);
    rig.set(id, fx, ex::kProgress, 40.0f);
    rig.seek(10);
    const test::Image8 sharp = rig.capture();
    // Desfoque de movimento: as peças andam por quadro, então a camada vai
    // para a acumulação (não pode quebrar nem sumir).
    rig.comp()->motion_blur().enabled = true;
    if (Layer* m = rig.comp()->layer(LayerId::unpack(id))) m->motionBlur = true;
    Layer* ml = rig.comp()->layer(LayerId::unpack(id));
    const EffectInstance* live = ml ? ex::find(*ml) : nullptr;
    AUREA_CHECK(live != nullptr);
    if (!ml || !live) return;
    Track& tr = ml->tracks.get_or_create(TrackProperty::EffectParam, live->id, param_track_key(ex::kProgress, 0));
    tr.set(FrameIndex{0}, 0.0f);
    tr.set(FrameIndex{30}, 100.0f);
    const test::Image8 blurred = rig.capture();
    std::printf("\n    preset lateral: acesos %llu; com motion blur %llu (difere %llu)",
                static_cast<unsigned long long>(lit(sharp)), static_cast<unsigned long long>(lit(blurred)),
                static_cast<unsigned long long>(diff_pixels(sharp, blurred, 8)));
    AUREA_CHECK(lit(blurred) > 0);
    AUREA_CHECK(diff_pixels(sharp, blurred, 8) > 0);
}

AUREA_TEST(Explode3DGpu, ShowcaseFrames) {
    if (!std::getenv("AUREA_FX_DUMP")) { std::printf("(AUREA_FX_DUMP não definido: sem PNG) "); return; }
    const std::string dir = std::string(AUREA_TEST_DATA_DIR) + "/../../../build/agents/explosao";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const char* presetNames[] = {"explosao", "vidro", "desintegrar", "montar", "lateral"};
    struct Shot { u32 kind; u32 preset; };
    const Shot shots[] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 2}, {2, 3}, {1, 4}};
    u32 written = 0;
    for (const Shot& s : shots) {
        ExplodeRig rig(640, 360);
        if (!rig.ok) return;
        const u64 id = add_object(rig, s.kind);
        if (!id) continue;
        const EffectId fx = rig.add_explode(id);
        AUREA_CHECK(rig.e.apply_effect_preset(id, fx.index, s.preset));
        for (u32 i = 0; i < 12; ++i) {
            rig.set(id, fx, ex::kProgress, 100.0f * static_cast<f32>(i) / 11.0f);
            rig.seek(10);
            const test::Image8 img = rig.capture(640);
            char name[256];
            std::snprintf(name, sizeof name, "%s/%s_%s_%02u.png", dir.c_str(), kObjectName[s.kind], presetNames[s.preset], i);
            if (test::write_png(name, img)) ++written;
        }
    }
    std::printf("\n    %u PNG em %s\n", written, dir.c_str());
    AUREA_CHECK(written > 0);
}

#endif   // AUREA_TEST_VULKAN
