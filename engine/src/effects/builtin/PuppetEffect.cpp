// =============================================================================
//  Aurea / effects / builtin / PuppetEffect.cpp
//
//  Fantoche (`aurea.distort.puppet`, Puppet Pin do AE). Os pinos são
//  parâmetros ocultos (effects/Puppet.hpp) em FRAÇÃO do tamanho da camada —
//  a mesma deformação em qualquer resolução de trabalho. No planejamento a
//  malha é resolvida (ARAP) e vai para a GPU como textura de dado: um texel
//  RGBA32F por vértice de triângulo, (x, y) deformado e (x, y) de repouso em
//  px da camada; o vértice lê com texelFetch e o fragmento amostra a camada.
// =============================================================================
#include "BuiltinEffects.hpp"
#include "aurea/effects/Puppet.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace aurea {
namespace builtin {
namespace {

struct PuppetUniforms {
    Vec4 region;        ///< saída (px da camada)
    Vec4 inputRegion;   ///< entrada (px da camada)
    Vec4 info;          ///< x = largura da textura de dado
};

/// Pinos ligados (px da camada) a partir dos valores avaliados.
std::vector<puppet::Pin> pins_of(const EffectEval& e, f32 w, f32 h) {
    std::vector<puppet::Pin> pins;
    if (e.count < puppet::kParamCount) return pins;
    for (u32 i = 0; i < puppet::kMaxPins; ++i) {
        if (!e.b(puppet::pin_on(i))) continue;
        const Vec2 r = e.p2(puppet::pin_rest(i)), p = e.p2(puppet::pin_pos(i));
        pins.push_back(puppet::Pin{Vec2{r.x * w, r.y * h}, Vec2{p.x * w, p.y * h}});
    }
    return pins;
}

class PuppetEffect final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{puppet::kPuppetKey, "Puppet", "Distort", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        p.add_int("triangles", "Triângulos", 300, 20, 1500, kParamNone);
        p.add_float("expansion", "Expansão", 3.0f, 0.0f, 100.0f, kParamNone, "px");
        p.add_float("rigidity", "Rigidez", 0.0f, 0.0f, 100.0f, kParamAnimatable, "%");
        for (u32 i = 0; i < puppet::kMaxPins; ++i) {
            // Os ids ficam na memória estática (o registro guarda o ponteiro).
            static char ids[puppet::kMaxPins][3][16];
            std::snprintf(ids[i][0], 16, "pin%u_on", i);
            std::snprintf(ids[i][1], 16, "pin%u_rest", i);
            std::snprintf(ids[i][2], 16, "pin%u", i);
            p.add_bool(ids[i][0], "Pino", false, kParamHidden);
            p.add_point2(ids[i][1], "Pino (repouso)", Vec2{0.5f, 0.5f}, -50.0f, 50.0f, kParamHidden);
            p.add_point2(ids[i][2], "Pino (posição)", Vec2{0.5f, 0.5f}, -50.0f, 50.0f, kParamAnimatable | kParamHidden);
        }
        // Contorno (Puppet.hpp): uma linha da grade por parâmetro, 24 bits.
        for (u32 r = 0; r < puppet::kOutline; ++r) {
            static char rowIds[puppet::kOutline][16];
            std::snprintf(rowIds[r], 16, "outline%u", r);
            p.add_float(rowIds[r], "Contorno", 0.0f, 0.0f, 16777215.0f, kParamHidden);
        }
    }

    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat f) const override {
        out.push_back(PipelineKey::graphics(ShaderId::effects_puppet_vert, ShaderId::effects_puppet_frag, f));
    }

    bool needs_full_input() const noexcept override { return true; }

    bool demo_values(EffectInstance& instance, std::vector<ParamValue>& values) const noexcept override {
        (void)instance;
        if (values.size() < puppet::kParamCount) return false;
        // Prévia do catálogo: dois pinos parados embaixo e um no alto puxado.
        const Vec2 rest[3] = {{0.25f, 0.85f}, {0.75f, 0.85f}, {0.5f, 0.2f}};
        const Vec2 pos[3] = {{0.25f, 0.85f}, {0.75f, 0.85f}, {0.78f, 0.12f}};
        for (u32 i = 0; i < 3; ++i) {
            values[puppet::pin_on(i)].v[0] = 1.0f;
            values[puppet::pin_rest(i)].v[0] = rest[i].x; values[puppet::pin_rest(i)].v[1] = rest[i].y;
            values[puppet::pin_pos(i)].v[0] = pos[i].x; values[puppet::pin_pos(i)].v[1] = pos[i].y;
        }
        return true;
    }

    bool is_identity(const EffectEval& e) const noexcept override {
        if (e.count < puppet::kParamCount) return true;
        for (u32 i = 0; i < puppet::kMaxPins; ++i) {
            if (!e.b(puppet::pin_on(i))) continue;
            const Vec2 r = e.p2(puppet::pin_rest(i)), p = e.p2(puppet::pin_pos(i));
            if (std::fabs(r.x - p.x) > 1e-6f || std::fabs(r.y - p.y) > 1e-6f) return false;
        }
        return true;   // nenhum pino saiu do lugar: a camada fica como está
    }

    void resolve_resources(EffectEval& e) const noexcept override {
        e.pathSamples.reset();
        e.aux = TextureHandle{};
        if (!e.placement || !e.resources) return;
        const f32 w = std::max(1.0f, static_cast<f32>(e.placement->layerWidth));
        const f32 h = std::max(1.0f, static_cast<f32>(e.placement->layerHeight));
        const std::vector<puppet::Pin> pins = pins_of(e, w, h);
        if (pins.empty()) return;
        // A malha segue o contorno gravado (alfa/Rotobrush no primeiro pino).
        f32 rows[puppet::kOutline];
        for (u32 r = 0; r < puppet::kOutline; ++r) rows[r] = e.f(puppet::kOutlineFirst + r);
        std::vector<u8> cells;
        const bool outlined = puppet::outline_cells(rows, cells);
        puppet::Mesh mesh;
        puppet::build_mesh(w, h, static_cast<u32>(std::max(0, e.value(puppet::kTriangles).as_int())),
                           e.f(puppet::kExpansion), mesh, outlined ? cells.data() : nullptr,
                           outlined ? puppet::kOutline : 0u, outlined ? puppet::kOutline : 0u);
        std::vector<Vec2> def;
        puppet::deform(mesh, pins, e.f(puppet::kRigidity) / 100.0f, def);
        const u32 count = static_cast<u32>(mesh.tris.size());
        if (!count) return;
        const u32 tw = std::min<u32>(1024u, count), th = (count + tw - 1) / tw;
        // [0] = cabeçalho (nº de vértices, largura, região de saída depois).
        auto samples = std::make_shared<std::vector<Vec4>>();
        samples->reserve(static_cast<usize>(tw) * th + 2);
        f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (const Vec2& v : def) {
            minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        }
        samples->push_back(Vec4{static_cast<f32>(count), static_cast<f32>(tw), 0.0f, 0.0f});
        samples->push_back(Vec4{minX, minY, maxX - minX, maxY - minY});
        u64 key = 1469598103934665603ull ^ 0x505550504554ull;
        for (u32 k = 0; k < static_cast<u32>(tw) * th; ++k) {
            Vec4 t{0, 0, 0, 0};
            if (k < count) {
                const u32 vi = mesh.tris[k];
                t = Vec4{def[vi].x, def[vi].y, mesh.rest[vi].x, mesh.rest[vi].y};
            }
            samples->push_back(t);
            u32 bits[4];
            std::memcpy(bits, &t, sizeof bits);
            for (u32 b : bits) key = (key ^ b) * 1099511628211ull;
        }
        key = (key ^ tw) * 1099511628211ull;
        e.aux = e.resources->data_texture(key, samples->data() + 2, tw, th);
        e.pathSamples = std::move(samples);
    }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& in, f32 margin,
                 LayerImage& out) const override {
        if (!e.pathSamples || e.pathSamples->size() < 3 || !e.aux.valid()) {
            out = in;
            return OkStatus;
        }
        const auto& s = *e.pathSamples;
        const u32 count = static_cast<u32>(s[0].x);
        if (!count || !(s[1].z > 0.0f) || !(s[1].w > 0.0f)) {
            out = in;
            return OkStatus;
        }
        const Rect region = spread_region({s[1].x, s[1].y, s[1].z, s[1].w}, 0.0f, 0.0f, e.placement, margin);
        if (region.w <= 0.0f || region.h <= 0.0f) {
            out = in;
            return OkStatus;
        }
        PuppetUniforms u;
        u.region = {region.x, region.y, region.w, region.h};
        u.inputRegion = {in.region.x, in.region.y, in.region.w, in.region.h};
        u.info = {s[0].y, 0.0f, 0.0f, 0.0f};
        u32 w = 0, h = 0;
        ctx.region_size(region, in.texel_scale_x(), w, h);
        if (!w || !h) {
            out = in;
            return OkStatus;
        }
        out = {ctx.texture(info().key, w, h), region, w, h};
        if (ctx.geometry_pass(info().key, PassStage::Effects, out.texture, ShaderId::effects_puppet_vert,
                              ShaderId::effects_puppet_frag,
                              {PassTexture{in.texture, {}, CommonSampler::LinearBorder},
                               PassTexture{{}, e.aux, CommonSampler::NearestClamp}},
                              &u, sizeof(u), count, false, false) == kInvalidIndex)
            return Errc::PipelineCompileFailed;
        return OkStatus;
    }
};

} // namespace

void register_puppet_effect(EffectRegistry& r) { (void)r.add(std::make_unique<PuppetEffect>()); }

} // namespace builtin
} // namespace aurea
