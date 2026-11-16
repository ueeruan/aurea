// =============================================================================
//  Texto 3D — estilo CARTOON (0.5.4)
//
//  Receita (só grava com o estilo ligado: projeto antigo = mesmos bytes), a
//  lateral automática, o contorno de verdade (casco invertido + quadriláteros
//  dos vincos, primitiva própria com material toon 2), o contorno com
//  keyframe (TrackProperty::Text3DOutline: desfazer, salvar e reabrir), a
//  tradução dos shaders mexidos para GLES/Metal e, na GPU do host, os quadros
//  de amostra gravados em build/reference/text3d-cartoon/ para comparar com a
//  referência do dono (owner-reference.png).
// =============================================================================
#include "TestFramework.hpp"

#include "ShaderTranslate.hpp"
#include "aurea/Engine.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/render/RuntimeShaders.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/scene3d/Image3D.hpp"
#include "aurea/scene3d/MaterialPreview.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"

#if defined(AUREA_TEST_VULKAN)
#include "ImageIO.hpp"
#include "VulkanBackend.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace aurea;
using namespace aurea::scene3d;
namespace fs = std::filesystem;

namespace {

Text3DSpec cartoon(const char* content) {
    Text3DSpec s;
    s.content = content;
    AUREA_CHECK(apply_text3d_material_preset(s, kText3DPresetCartoon));
    return s;
}

const Primitive* outline_of(const SceneAsset& a) {
    for (const Mesh& m : a.meshes)
        for (const Primitive& p : m.primitives)
            if (p.material == static_cast<i32>(kText3DOutlineMaterial)) return &p;
    return nullptr;
}

f32 hue_of(Vec4 c) {
    const f32 mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z}), d = mx - mn;
    if (d < 1e-5f) return 0.0f;
    f32 h = mx == c.x ? std::fmod((c.y - c.z) / d + 6.0f, 6.0f) : mx == c.y ? (c.z - c.x) / d + 2.0f : (c.x - c.y) / d + 4.0f;
    return h * 60.0f;
}

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string expand_includes(const fs::path& path) {
    std::istringstream input(read_text(path));
    std::string result;
    for (std::string line; std::getline(input, line);) {
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 8, "#include") == 0) {
            const auto quote = line.find('"', first + 8);
            const auto end = quote == std::string::npos ? quote : line.find('"', quote + 1);
            if (quote != std::string::npos && end != std::string::npos) {
                result += expand_includes(path.parent_path() / line.substr(quote + 1, end - quote - 1));
                result += '\n';
                continue;
            }
        }
        result += line + '\n';
    }
    return result;
}

fs::path engine_dir() { return fs::path(AUREA_TEST_DATA_DIR).parent_path().parent_path(); }

void seek(Engine& e, i64 frame) {
    Command s{};
    s.type = CommandType::PlaybackSeek;
    s.seek.time = tick_at(FrameIndex{frame}, 30.0);
    AUREA_CHECK(e.apply_command(s).ok());
}

} // namespace

// -----------------------------------------------------------------------------
// Receita
// -----------------------------------------------------------------------------

AUREA_TEST(Text3DCartoon, StandardRecipeKeepsTheOldBytesAndReadsAsStandard) {
    Text3DSpec s;
    s.content = "AUREA";
    const std::string recipe = encode_text3d(s);
    AUREA_CHECK(recipe.find("toon=") == std::string::npos);
    // Receita gravada antes do Cartoon (v2, sem a chave): estilo Padrão.
    const std::string legacy = std::string(kText3DScheme)
        + "v2;d=0.2500;a=1;c=ffffffff;bv=00.0200/0.0200/1.000/3;m0=ffffff/0.000/0.350/1.000/1.000;m0e=000000/1.00;rg=0;font=;texture=;anim=0/2.0000/0.1200/0.3000;glyphs=0;finish=0;t=Ola";
    Text3DSpec old;
    AUREA_CHECK(decode_text3d(legacy, old));
    AUREA_CHECK_EQ(old.style, kText3DStyleStandard);
    AUREA_CHECK(old.content == "Ola");
    // Reencodar o antigo devolve o mesmo texto (a chave do cache de geometria e
    // o caminho de origem do asset não mudam ao abrir o projeto).
    AUREA_CHECK(encode_text3d(old) == legacy);
}

AUREA_TEST(Text3DCartoon, RecipeRoundTripsEveryCartoonField) {
    Text3DSpec s = cartoon("Pop!");
    s.toonSideAuto = false;
    s.toonSideColor = Vec4{0.2f, 0.4f, 0.6f, 1.0f};
    s.toonHighlight = 0.7f;
    s.outlineColor = Vec4{0.0f, 0.5f, 1.0f, 1.0f};
    s.outlineWidth = 12.5f;
    const std::string recipe = encode_text3d(s);
    AUREA_CHECK(recipe.find(";toon=1/0/") != std::string::npos);
    Text3DSpec back;
    AUREA_CHECK(decode_text3d(recipe, back));
    AUREA_CHECK_EQ(back.style, kText3DStyleCartoon);
    AUREA_CHECK(!back.toonSideAuto);
    AUREA_CHECK(std::fabs(back.toonSideColor.y - 0.4f) < 0.01f);
    AUREA_CHECK(std::fabs(back.toonHighlight - 0.7f) < 1e-3f);
    AUREA_CHECK(std::fabs(back.outlineColor.z - 1.0f) < 0.01f);
    AUREA_CHECK(std::fabs(back.outlineWidth - 12.5f) < 1e-3f);
    AUREA_CHECK(back.content == "Pop!");
    // Valores fora da faixa e NaN são saneados.
    Text3DSpec bad = s;
    bad.outlineWidth = std::numeric_limits<f32>::quiet_NaN();
    bad.toonHighlight = 7.0f;
    bad.style = 9;
    sanitize_text3d(bad);
    AUREA_CHECK(std::isfinite(bad.outlineWidth) && bad.outlineWidth <= kText3DOutlineMaxWidth);
    AUREA_CHECK(bad.toonHighlight <= 1.0f);
    AUREA_CHECK_EQ(bad.style, kText3DStyleStandard);
}

AUREA_TEST(Text3DCartoon, PresetTurnsCartoonOnAndThePbrPresetsTurnItOff) {
    Text3DSpec s;
    s.content = "Texto";
    AUREA_CHECK(apply_text3d_material_preset(s, kText3DPresetCartoon));
    AUREA_CHECK_EQ(s.style, kText3DStyleCartoon);
    AUREA_CHECK(s.content == "Texto");
    AUREA_CHECK(s.bevel && s.bevelDepth < s.bevelWidth);   // faixa quase plana: o brilho
    AUREA_CHECK(apply_text3d_material_preset(s, 0));
    AUREA_CHECK_EQ(s.style, kText3DStyleStandard);
    AUREA_CHECK(!apply_text3d_material_preset(s, kText3DMaterialPresetCount));
    // A Imagem 3D não tem o Cartoon.
    Image3DSpec image;
    AUREA_CHECK(!apply_image3d_material_preset(image, kText3DPresetCartoon));
    AUREA_CHECK(apply_image3d_material_preset(image, 3));
    // A bola do material pronto sai em cel com contorno (difere do Brilhante).
    MaterialBall toonBall, glossy;
    AUREA_CHECK(text3d_preset_ball(kText3DPresetCartoon, toonBall) && toonBall.toon);
    AUREA_CHECK(text3d_preset_ball(3, glossy) && !glossy.toon);
    const auto a = render_material_ball(toonBall, 64), b = render_material_ball(glossy, 64);
    AUREA_CHECK(!a.empty() && a != b);
}

AUREA_TEST(Text3DCartoon, AutomaticSideIsTheSameHueDarker) {
    const Vec4 pink{0.957f, 0.478f, 0.549f, 1.0f}, teal{0.5f, 0.84f, 0.91f, 1.0f}, grey{0.6f, 0.6f, 0.6f, 1.0f};
    for (const Vec4 c : {pink, teal, grey}) {
        const Vec4 side = text3d_cartoon_side_color(c);
        AUREA_CHECK(std::max({side.x, side.y, side.z}) < std::max({c.x, c.y, c.z}) - 0.15f);
        if (c.x != c.y) AUREA_CHECK(std::fabs(hue_of(side) - hue_of(c)) < 2.0f);
    }
    const Vec4 g = text3d_cartoon_side_color(grey);
    AUREA_CHECK(std::fabs(g.x - g.y) < 1e-4f && std::fabs(g.y - g.z) < 1e-4f);   // cinza continua cinza
}

// -----------------------------------------------------------------------------
// Malha do contorno
// -----------------------------------------------------------------------------

AUREA_TEST(Text3DCartoon, CubeOutlineHasAHullAndExactlyTheTwelveCreases) {
    // Cubo com vértices repetidos por face (como frente/lateral do texto).
    Primitive cube;
    cube.material = 0;
    const Vec3 c[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
    const u32 faces[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
    for (const auto& f : faces) {
        const u32 base = static_cast<u32>(cube.positions.size());
        for (u32 k = 0; k < 4; ++k) { cube.positions.push_back(c[f[k]]); cube.normals.push_back(Vec3{0, 0, 1}); }
        cube.indices.insert(cube.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    std::vector<Primitive> prims{cube};
    AUREA_CHECK(append_toon_outline(prims, 3));
    AUREA_CHECK_EQ(prims.size(), static_cast<usize>(2));
    if (prims.size() != 2) return;
    const Primitive& o = prims[1];
    AUREA_CHECK_EQ(o.material, 3);
    // 8 vértices soldados do casco + 12 vincos × 4.
    AUREA_CHECK_EQ(o.positions.size(), static_cast<usize>(8 + 12 * 4));
    // Casco: os 12 triângulos invertidos; vincos: 12 × 4 triângulos (as duas ordens).
    AUREA_CHECK_EQ(o.indices.size(), static_cast<usize>(12 * 3 + 12 * 12));
    u32 hull = 0, lines = 0;
    for (usize v = 0; v < o.positions.size(); ++v) {
        if (o.uv1[v].y > 0.5f) {
            ++lines;
            // A "normal" do vinco é a outra ponta: as duas são cantos do cubo.
            AUREA_CHECK(std::fabs(std::fabs(o.normals[v].x) - 1.0f) < 1e-5f);
            AUREA_CHECK((o.normals[v] - o.positions[v]).length() > 1.0f);
        } else {
            ++hull;
            // Normal suave do canto: a diagonal.
            const Vec3 n = o.normals[v], p = o.positions[v].normalized();
            AUREA_CHECK(n.dot(p) > 0.99f);
        }
    }
    AUREA_CHECK_EQ(hull, 8u);
    AUREA_CHECK_EQ(lines, 48u);
    // Ordem invertida: a normal do triângulo do casco aponta para DENTRO.
    const Vec3 a = o.positions[o.indices[0]], b = o.positions[o.indices[1]], d = o.positions[o.indices[2]];
    const Vec3 centroid = (a + b + d) * (1.0f / 3.0f);
    AUREA_CHECK((b - a).cross(d - a).dot(centroid) < 0.0f);
}

AUREA_TEST(Text3DCartoon, CartoonMeshAddsTheOutlinePrimitiveAndStandardStaysTheSame) {
    const auto font = text::default_font();
    AUREA_CHECK(font != nullptr);
    if (!font) return;
    Text3DSpec standard;
    standard.content = "AB";
    ImportResult plain = build_text3d(*font, standard);
    AUREA_CHECK(plain.ok());
    if (!plain.ok()) return;
    AUREA_CHECK_EQ(plain.asset->materials.size(), static_cast<usize>(1));
    AUREA_CHECK(outline_of(*plain.asset) == nullptr);
    for (const Material& m : plain.asset->materials) AUREA_CHECK_EQ(m.toon, static_cast<u8>(0));

    Text3DSpec toon = cartoon("AB");
    toon.outlineWidth = 6.0f;
    ImportResult res = build_text3d(*font, toon);
    AUREA_CHECK(res.ok());
    if (!res.ok()) return;
    const SceneAsset& a = *res.asset;
    AUREA_CHECK_EQ(a.materials.size(), static_cast<usize>(4));
    if (a.materials.size() != 4) return;
    AUREA_CHECK_EQ(a.materials[0].toon, static_cast<u8>(1));
    AUREA_CHECK_EQ(a.materials[1].toon, static_cast<u8>(1));
    AUREA_CHECK_EQ(a.materials[3].toon, static_cast<u8>(2));
    AUREA_CHECK(std::fabs(a.materials[3].toonParam - 6.0f) < 1e-4f);
    // Só o chanfro (a faixa clara) carrega o brilho; a lateral é mais escura que a frente.
    AUREA_CHECK(a.materials[2].toonParam > 0.0f && a.materials[1].toonParam == 0.0f);
    AUREA_CHECK(a.materials[1].baseColor.x + a.materials[1].baseColor.y + a.materials[1].baseColor.z
                < a.materials[0].baseColor.x + a.materials[0].baseColor.y + a.materials[0].baseColor.z);
    const Primitive* o = outline_of(a);
    AUREA_CHECK(o != nullptr);
    if (!o) return;
    usize surfaceTris = 0;
    for (const Mesh& m : a.meshes)
        for (const Primitive& p : m.primitives)
            if (p.material != static_cast<i32>(kText3DOutlineMaterial)) surfaceTris += p.indices.size() / 3;
    u32 creaseVerts = 0;
    for (const Vec2 uv : o->uv1) creaseVerts += uv.y > 0.5f ? 1u : 0u;
    // O casco cobre a malha inteira; os vincos (frente/lateral, fundo, cantos) existem.
    AUREA_CHECK(o->indices.size() / 3 >= surfaceTris);
    AUREA_CHECK(creaseVerts >= 4u * 8u);
    // A faixa do chanfro aponta no plano da letra (uv1 normalizado).
    bool upward = false;
    for (const Mesh& m : a.meshes)
        for (const Primitive& p : m.primitives)
            if (p.material == 2)
                for (const Vec2 uv : p.uv1) upward = upward || uv.y > 0.9f;
    AUREA_CHECK(upward);
    // Letras separadas: cada letra tem o próprio contorno.
    toon.separateGlyphs = true;
    ImportResult glyphs = build_text3d(*font, toon);
    AUREA_CHECK(glyphs.ok());
    if (glyphs.ok()) {
        AUREA_CHECK_EQ(glyphs.asset->meshes.size(), static_cast<usize>(2));
        for (const Mesh& m : glyphs.asset->meshes) {
            bool has = false;
            for (const Primitive& p : m.primitives) has = has || p.material == static_cast<i32>(kText3DOutlineMaterial);
            AUREA_CHECK(has);
        }
    }
    // A chave do cache de geometria do estilo Padrão não mudou com o Cartoon.
    AUREA_CHECK(text3d_geometry_key(*font, standard).find("|toon") == std::string::npos);
    AUREA_CHECK(text3d_geometry_key(*font, cartoon("AB")).find("|toon") != std::string::npos);
}

// -----------------------------------------------------------------------------
// Contorno com keyframe (motor)
// -----------------------------------------------------------------------------

AUREA_TEST(Text3DCartoon, OutlineKeysUndoSaveAndReopen) {
    if (!text::default_font()) return;
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    Text3DSpec plain;
    plain.content = "Ola";
    const Result<u64> id = e.add_text3d(plain);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return;
    f32 o[Engine::kText3DOutlineFloats]{};
    // Padrão: não há contorno para mexer.
    AUREA_CHECK(!e.query_text3d_outline(*id, o));
    AUREA_CHECK(!e.set_text3d_outline(*id, 0, 4.0f));
    AUREA_CHECK(e.apply_text3d_preset(*id, kText3DPresetCartoon));
    AUREA_CHECK(e.query_text3d_outline(*id, o));
    AUREA_CHECK(std::fabs(o[0] - 10.0f) < 1e-3f && o[4] == 0.0f && o[5] == 0.0f);
    // Sem keyframe: a receita.
    AUREA_CHECK(e.set_text3d_outline(*id, 0, 5.0f));
    Text3DSpec q;
    AUREA_CHECK(e.query_text3d(*id, q) && std::fabs(q.outlineWidth - 5.0f) < 1e-3f);
    // ◆ em 0 e em 30 com valores diferentes: a largura anima.
    seek(e, 0);
    AUREA_CHECK(e.toggle_text3d_outline_key(*id, 0));
    seek(e, 30);
    AUREA_CHECK(e.set_text3d_outline(*id, 0, 20.0f));   // sem key ali ainda: grava o key no cabeçote
    AUREA_CHECK(e.query_text3d_outline(*id, o));
    AUREA_CHECK(std::fabs(o[0] - 20.0f) < 1e-3f && o[4] == 2.0f);
    seek(e, 15);
    AUREA_CHECK(e.query_text3d_outline(*id, o));
    AUREA_CHECK(o[0] > 5.5f && o[0] < 19.5f && o[4] == 1.0f);
    // A cor: um ◆ cria as três trilhas de uma vez.
    AUREA_CHECK(e.toggle_text3d_outline_key(*id, 1));
    AUREA_CHECK(e.query_text3d_outline(*id, o) && o[5] == 2.0f);
    {
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        const Layer* l = comp->layer(LayerId::unpack(*id));
        u32 tracks = 0;
        for (u32 i = 0; i < l->tracks.size(); ++i) tracks += l->tracks.at(i).property == TrackProperty::Text3DOutline ? 1u : 0u;
        AUREA_CHECK_EQ(tracks, 4u);
    }
    // Desfazer tira o ◆ da cor.
    Command u{};
    u.type = CommandType::Undo;
    AUREA_CHECK(e.apply_command(u).ok());
    AUREA_CHECK(e.query_text3d_outline(*id, o) && o[5] == 0.0f);
    // Salvar e reabrir: a trilha da largura volta com os dois keyframes.
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_text3d_cartoon.aurea";
    AUREA_CHECK(e.save_project(path.c_str()).ok());
    AUREA_CHECK(e.load_project(path.c_str()).ok());
    seek(e, 30);
    AUREA_CHECK(e.query_text3d_outline(*id, o));
    AUREA_CHECK(std::fabs(o[0] - 20.0f) < 1e-3f && o[4] == 2.0f);
    AUREA_CHECK(e.query_text3d(*id, q) && q.style == kText3DStyleCartoon && std::fabs(q.outlineWidth - 5.0f) < 1e-3f);
    // Tirar os dois keyframes devolve a largura da receita.
    AUREA_CHECK(e.toggle_text3d_outline_key(*id, 0));
    seek(e, 0);
    AUREA_CHECK(e.toggle_text3d_outline_key(*id, 0));
    AUREA_CHECK(e.query_text3d_outline(*id, o) && std::fabs(o[0] - 5.0f) < 1e-3f && o[4] == 0.0f);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// Shaders mexidos: GLES (ESSL 3.10) e Metal
// -----------------------------------------------------------------------------

AUREA_TEST(ShaderTranslate, CartoonOutlineScene3DShadersTranslateForGlesAndMetal) {
    if (!runtime_shaders::compiler_available()) {
        std::printf("(runtime shader compiler unavailable) ");
        return;
    }
    const fs::path dir = engine_dir() / "shaders" / "scene3d" / "pbr";
    struct Case { const char* file; ShaderStage stage; const char* entry; };
    for (const Case c : {Case{"pbr.frag", ShaderStage::Fragment, "fs_main"}, Case{"mesh.vert", ShaderStage::Vertex, "vs_main"},
                         Case{"mesh_skinned.vert", ShaderStage::Vertex, "vs_main"}}) {
        const std::string source = expand_includes(dir / c.file);
        AUREA_CHECK(source.find("alpha.z < -3.5") != std::string::npos);
        const runtime_shaders::SourcePart parts[]{{c.file, source}};
        std::vector<u32> spirv;
        std::string error;
        const bool compiled = runtime_shaders::compile_glsl(c.stage, parts, spirv, error);
        AUREA_CHECK_MSG(compiled, error.c_str());
        if (!compiled) continue;
        std::string essl, msl;
        AUREA_CHECK_MSG(xlate::spirv_to_essl(spirv.data(), spirv.size(), essl, error), error.c_str());
        AUREA_CHECK(essl.find("#version 310 es") != std::string::npos);
        if (xlate::msl_available()) {
            u32 group[3]{};
            AUREA_CHECK_MSG(xlate::spirv_to_msl(spirv.data(), spirv.size(), c.entry, 0xffffffffu, group, msl, error),
                            error.c_str());
            AUREA_CHECK(!msl.empty());
        }
    }
}

// -----------------------------------------------------------------------------
// GPU: quadros de amostra (build/reference/text3d-cartoon/)
// -----------------------------------------------------------------------------
#if defined(AUREA_TEST_VULKAN)
using namespace aurea::test;
namespace {

struct CartoonRig {
    Engine e;
    bool ok = false;
    CartoonRig(u32 w, u32 h) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ok = e.initialize(ec).ok();
        // Fundo creme como o da referência.
        const f32 cream[4]{0.97f, 0.94f, 0.87f, 1.0f};
        ok = ok && e.new_project(w, h, 30.0, nullptr, cream).ok();
    }
    ~CartoonRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* layer(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    Image8 capture(u32 maxDim) {
        Image8 img;
        std::vector<u8> rgba;
        u32 w = 0, h = 0;
        AUREA_CHECK(e.capture_frame_rgba(maxDim, rgba, w, h).ok());
        img.width = w;
        img.height = h;
        img.rgba = std::move(rgba);
        return img;
    }
};

fs::path reference_dir() {
    const fs::path dir = engine_dir().parent_path() / "build" / "reference" / "text3d-cartoon";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

/// Pixels quase pretos/azul-escuros (a tinta do contorno sobre o creme e as cores).
f32 ink_fraction(const Image8& img) {
    u32 n = 0;
    for (usize i = 0; i + 3 < img.rgba.size(); i += 4)
        n += (img.rgba[i] < 70 && img.rgba[i + 1] < 70 && img.rgba[i + 2] < 90) ? 1u : 0u;
    return static_cast<f32>(n) / static_cast<f32>(std::max<usize>(1, img.rgba.size() / 4));
}

u32 count_near(const Image8& img, u8 r, u8 g, u8 b, int tol) {
    u32 n = 0;
    for (usize i = 0; i + 3 < img.rgba.size(); i += 4)
        n += (std::abs(img.rgba[i] - r) <= tol && std::abs(img.rgba[i + 1] - g) <= tol && std::abs(img.rgba[i + 2] - b) <= tol) ? 1u : 0u;
    return n;
}

u64 add_cartoon(CartoonRig& rig, const char* content, Vec4 color, Vec3 pos, f32 scale, Vec3 rot) {
    Text3DSpec s = cartoon(content);
    s.color = color;
    // Só nas amostras: uma fonte grossa do sistema (como a da referência), se o host tiver.
    for (const char* font : {"C:/Windows/Fonts/ariblk.ttf", "/System/Library/Fonts/Supplemental/Arial Black.ttf"})
        if (fs::exists(font)) { s.fontPath = font; break; }
    const Result<u64> id = rig.e.add_text3d(s);
    AUREA_CHECK(id.ok());
    if (!id.ok()) return 0;
    Layer* l = rig.layer(*id);
    l->transform.position = pos;
    l->transform.scale = Vec3{scale, scale, scale};
    l->transform.rotation = rot;
    return *id;
}

} // namespace

AUREA_TEST(Text3DCartoonGpu, SampleRendersMatchTheCartoonLookAndKeepTheirWeightAtAnyResolution) {
    if (!text::default_font()) return;
    CartoonRig rig(960, 540);
    if (!rig.ok) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const f32 w = 960.0f, h = 540.0f;
    // A composição da referência: "3D" rosa grande em cima, "TEXT" ciano embaixo,
    // girados de leve para a extrusão aparecer para baixo e para a direita.
    const u64 top = add_cartoon(rig, "3D", Vec4{0.96f, 0.45f, 0.55f, 1.0f}, Vec3{w * 0.47f, h * 0.36f, 0.0f}, 1.9f, Vec3{-14.0f, 16.0f, -4.0f});
    const u64 bottom = add_cartoon(rig, "TEXT", Vec4{0.50f, 0.84f, 0.91f, 1.0f}, Vec3{w * 0.52f, h * 0.70f, 0.0f}, 1.25f, Vec3{-14.0f, 16.0f, -4.0f});
    AUREA_CHECK(top != 0 && bottom != 0);
    if (!top || !bottom) return;
    const fs::path dir = reference_dir();
    const Image8 full = rig.capture(960);
    AUREA_CHECK(write_png((dir / "cartoon-3d-text.png").string(), full));
    const f32 inkFull = ink_fraction(full);
    std::printf("\n    tinta (contorno) no quadro 960: %.4f", inkFull);
    AUREA_CHECK(inkFull > 0.01f);

    // Meia resolução: o traço tem o mesmo peso relativo (px da composição).
    const Image8 half = rig.capture(480);
    AUREA_CHECK(write_png((dir / "cartoon-3d-text-480.png").string(), half));
    const f32 inkHalf = ink_fraction(half);
    std::printf("\n    tinta em 480: %.4f (razão %.2f)", inkHalf, inkHalf / std::max(inkFull, 1e-6f));
    AUREA_CHECK(inkHalf > inkFull * 0.7f && inkHalf < inkFull * 1.3f);

    // Sem contorno: a tinta some (o casco e os vincos são o contorno).
    Text3DSpec s;
    AUREA_CHECK(rig.e.query_text3d(top, s));
    s.outlineWidth = 0.0f;
    AUREA_CHECK(rig.e.set_text3d(top, s).ok());
    Text3DSpec s2;
    AUREA_CHECK(rig.e.query_text3d(bottom, s2));
    s2.outlineWidth = 0.0f;
    AUREA_CHECK(rig.e.set_text3d(bottom, s2).ok());
    const Image8 bare = rig.capture(960);
    AUREA_CHECK(write_png((dir / "cartoon-3d-text-no-outline.png").string(), bare));
    std::printf("\n    tinta sem contorno: %.4f", ink_fraction(bare));
    AUREA_CHECK(ink_fraction(bare) < inkFull * 0.25f);

    // Cor chapada: a frente é a cor escolhida, exata (cel, sem reflexo).
    const u32 frontPink = count_near(bare, 245, 115, 140, 6);
    std::printf("\n    pixels da frente rosa exata: %u", frontPink);
    AUREA_CHECK(frontPink > 2000u);

    // Contorno grosso (16 px) e outra cor: mais tinta.
    s.outlineWidth = 16.0f;
    s.outlineColor = Vec4{0.05f, 0.05f, 0.1f, 1.0f};
    AUREA_CHECK(rig.e.set_text3d(top, s).ok());
    s2.outlineWidth = 16.0f;
    AUREA_CHECK(rig.e.set_text3d(bottom, s2).ok());
    const Image8 thick = rig.capture(960);
    AUREA_CHECK(write_png((dir / "cartoon-3d-text-thick.png").string(), thick));
    AUREA_CHECK(ink_fraction(thick) > inkFull * 1.3f);

    // De lado (giro Y 50°): os vincos das laterais e o casco seguem a forma.
    s.outlineWidth = 8.0f;
    AUREA_CHECK(rig.e.set_text3d(top, s).ok());
    s2.outlineWidth = 8.0f;
    AUREA_CHECK(rig.e.set_text3d(bottom, s2).ok());
    rig.layer(top)->transform.rotation = Vec3{-10.0f, 50.0f, 0.0f};
    rig.layer(bottom)->transform.rotation = Vec3{-10.0f, 50.0f, 0.0f};
    const Image8 side = rig.capture(960);
    AUREA_CHECK(write_png((dir / "cartoon-3d-text-side.png").string(), side));
    AUREA_CHECK(ink_fraction(side) > 0.01f);
    std::printf("\n    amostras em %s\n", dir.string().c_str());
}

AUREA_TEST(Text3DCartoonGpu, KeyframedOutlineWidthChangesTheFrame) {
    if (!text::default_font()) return;
    CartoonRig rig(480, 270);
    if (!rig.ok) { std::printf("(sem GPU Vulkan: pulado) "); return; }
    const u64 id = add_cartoon(rig, "Pop", Vec4{1.0f, 0.8f, 0.2f, 1.0f}, Vec3{240.0f, 135.0f, 0.0f}, 1.4f, Vec3{-10.0f, 15.0f, 0.0f});
    if (!id) return;
    seek(rig.e, 0);
    AUREA_CHECK(rig.e.set_text3d_outline(id, 0, 2.0f));
    AUREA_CHECK(rig.e.toggle_text3d_outline_key(id, 0));
    seek(rig.e, 20);
    AUREA_CHECK(rig.e.set_text3d_outline(id, 0, 14.0f));   // com trilha: vira o keyframe do quadro 20
    seek(rig.e, 0);
    const f32 thin = ink_fraction(rig.capture(480));
    seek(rig.e, 20);
    const f32 thick = ink_fraction(rig.capture(480));
    std::printf("\n    contorno animado: quadro 0 %.4f, quadro 20 %.4f\n", thin, thick);
    AUREA_CHECK(thick > thin * 1.5f);
}
#endif
