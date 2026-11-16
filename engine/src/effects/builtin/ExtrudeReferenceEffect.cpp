#include "BuiltinEffects.hpp"
#include "aurea/effects/ExtrudeReference.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace aurea::extrude_reference {
namespace {
struct Uniforms {
    Vec4 region, inputRegion, frontColor, sideControls;
    Vec4 angles[2];
    Vec4 colors[5];
    Vec4 controls;
};
static_assert(sizeof(Uniforms) == 192);

class Extrude final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo info{kKey, "Extrude", "3D", EffectClass::Domain};
        return info;
    }
    bool needs_full_input() const noexcept override { return true; }
    void declare_parameters(ParameterRegistry& p) const override {
        constexpr u16 pixels = kParamAnimatable | kParamPixels;
        constexpr u16 percent = kParamAnimatable | kParamPercent;
        constexpr u16 relative = kParamAnimatable | kParamRelative;
        p.add_int("mask_path", "Caminho da máscara", 0, 0, 32, kParamNone);
        p.add_bool("all_paths", "Usar todos os caminhos", true, kParamNone);
        static const char* const modes[]{"Direcional", "Ponto de fuga", "Câmera 3D"};
        p.add_enum("mode", "Modo", modes, 3, 0);
        p.add_bool("inner", "Extrusão interna", false);
        p.add_angle("direction", "Direção da extrusão", 135);
        p.add_float("back_length", "Comprimento para trás", 100, -10000, 10000, pixels, "px");
        p.add_float("front_length", "Comprimento para frente", 0, -10000, 10000, pixels, "px");
        p.add_point2("vanishing", "Ponto de fuga", {.8f, .8f}, -100, 100, relative);
        p.add_float("back_amount", "Quantidade para trás", 20, -1000, 100, percent, "%");
        p.add_float("front_amount", "Quantidade para frente", 0, -1000, 100, percent, "%");
        p.add_float("stroke_width", "Largura do contorno", 2, 0, 10000, pixels, "px");
        p.add_color("stroke_color", "Cor do contorno", {0, 0, 0, 1});
        p.add_float("stroke_opacity", "Opacidade do contorno", 100, 0, 100, percent, "%");
        p.add_bool("inherit_color", "Herdar a cor da camada", true);
        p.add_color("front_color", "Cor da frente", {1, 0, 0, 1});
        static const char* const fillModes[]{"Sólido", "Gradiente"};
        p.add_enum("side_mode", "Preenchimento das laterais", fillModes, 2, 0);
        p.add_int("color_count", "Número de cores das laterais", 1, 1, 5);
        p.add_color("side_color", "Cor da lateral", {1, 1, 1, 1});
        p.add_color("color_2", "Cor da lateral 2", {0, 0, 1, 1});
        p.add_angle("angle_2", "Ângulo da lateral 2", 72);
        p.add_color("color_3", "Cor da lateral 3", {1, 0, 0, 1});
        p.add_angle("angle_3", "Ângulo da lateral 3", 144);
        p.add_color("color_4", "Cor da lateral 4", {1, 1, 0, 1});
        p.add_angle("angle_4", "Ângulo da lateral 4", 216);
        p.add_color("color_5", "Cor da lateral 5", {0, 1, 1, 1});
        p.add_angle("angle_5", "Ângulo da lateral 5", 288);
        p.add_angle("global_offset", "Deslocamento global", 0);
        p.add_bool("interpolate", "Interpolar o gradiente", true, kParamNone);
        p.add_float("fill_opacity", "Opacidade do preenchimento", 100, 0, 100, percent, "%");
        p.add_layer_ref("parent", "Camada pai");
        p.add_point2("anchor", "Ponto de ancoragem", {.5f, .5f}, -100, 100, relative);
        p.add_float("input_opacity", "Opacidade da entrada", 0, 0, 100, percent, "%");
        p.add_float("resolution", "Resolução geral", 1, 1, 8);
        p.add_float("corner_threshold", "Limite do ângulo dos cantos", 10, 0, 180);
        p.add_float("flatness", "Limite de planura", .25f, 0, 10);
        p.add_float("front_resolution", "Escala da resolução da frente", 1, 1, 10);
        static const char* const debugViews[]{"Frente", "Trás", "Laterais", "Contorno", "Composição"};
        p.add_enum("debug_view", "Vista de depuração", debugViews, 5, 4);
        p.add_float("debug_x", "Deslocamento X da vista", 0, -2000, 2000, pixels, "px");
        p.add_float("debug_y", "Deslocamento Y da vista", 0, -2000, 2000, pixels, "px");
        p.add_float("left", "Expansão à esquerda", 0, -5000, 5000, pixels, "px");
        p.add_float("top", "Expansão acima", 0, -5000, 5000, pixels, "px");
        p.add_float("right", "Expansão à direita", 0, -5000, 5000, pixels, "px");
        p.add_float("bottom", "Expansão abaixo", 0, -5000, 5000, pixels, "px");
        p.add_section_range("paths", "Caminhos", kMaskPath, kAllPaths);
        p.add_section_range("extrusion", "Extrusão", kMode, kFrontAmount);
        p.add_section_range("stroke", "Contorno", kStrokeWidth, kStrokeOpacity);
        p.add_section_range("fill", "Preenchimento", kInheritColor, kFillOpacity);
        p.add_section_range("placement", "Posicionamento", kParent, kAnchor);
        p.add_section_range("quality", "Qualidade", kResolution, kFrontResolution);
        p.add_section_range("buffers", "Margens", kDebugView, kBottom);
        p.set_enabled_by(kMaskPath, kAllPaths, 2);
        p.set_enabled_by(kDirection, kMode, 1);
        p.set_enabled_by(kVanishing, kMode, 2);
        p.set_enabled_by(kBackAmount, kMode, 2);
        p.set_enabled_by(kFrontAmount, kMode, 2);
        p.set_enabled_by(kParent, kMode, 4);
        p.set_enabled_by(kAnchor, kMode, 4);
        p.set_enabled_by(kFrontColor, kInheritColor, 2);
    }
    void resolve_resources(EffectEval& e) const noexcept override {
        e.aux = {};
        e.auxInfo = {};
        e.pathSamples.reset();
        std::vector<SourcePaths> paths;
        Status status = source_paths(e, paths);
        std::vector<Vec4> vertices;
        Rect bounds;
        if (status.ok()) status = make_geometry(e, paths, vertices, bounds);
        if (!status.ok()) {
            e.auxInfo.w = static_cast<f32>(status.code());
            return;
        }
        const usize vertexCount = vertices.size() / 4;
        constexpr u32 maxTexels = 1024 * 1024;
        if (vertices.size() > maxTexels) {
            e.auxInfo.w = static_cast<f32>(Errc::BudgetExceeded);
            return;
        }
        const u32 width = std::min(1024u, std::max(1u, static_cast<u32>(vertices.size())));
        const u32 height = std::max(1u, (static_cast<u32>(vertices.size()) + width - 1) / width);
        vertices.resize(static_cast<usize>(width) * height);
        u64 key = 0x4558545255444531ull;
        for (const Vec4& value : vertices) for (f32 component : {value.x, value.y, value.z, value.w})
            key = (key ^ std::bit_cast<u32>(component)) * 1099511628211ull;
        key = (key ^ width) * 1099511628211ull;
        if (e.resources) e.aux = e.resources->data_texture(key, vertices.data(), width, height);
        e.auxInfo = {static_cast<f32>(width), static_cast<f32>(height), static_cast<f32>(vertexCount), 0};
        e.pathSamples = std::make_shared<const std::vector<Vec4>>(
            std::initializer_list<Vec4>{{bounds.x, bounds.y, bounds.w, bounds.h}});
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override {
        auto mesh = PipelineKey::graphics(ShaderId::effects_extrude_reference_vert,
            ShaderId::effects_extrude_reference_frag, format, true, BlendMode::Normal);
        mesh.hasDepth = true; mesh.depthTest = true; mesh.depthWrite = true;
        mesh.depthCompare = CompareOp::GreaterOrEqual; mesh.depthFormat = SurfaceFormat::Depth32F;
        out.push_back(mesh);
        out.push_back(PipelineKey::fullscreen(ShaderId::effects_extrude_reference_composite_frag, format));
    }
    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input,
                 f32 margin, LayerImage& out) const override {
        (void)margin;
        if (e.auxInfo.w != 0) return static_cast<Errc>(static_cast<i32>(e.auxInfo.w));
        if (!e.pathSamples || !e.aux.valid()) return Errc::InvalidState;
        const Vec4 box = e.pathSamples->front();
        Rect region{box.x, box.y, box.z, box.w};
        if (e.f(kInputOpacity) > 0 || region.empty()) region = Rect::unite(region, input.region);
        const f32 left = e.f(kLeft), top = e.f(kTop);
        region = {region.x - left, region.y - top,
            std::max(1.f, region.w + left + e.f(kRight)),
            std::max(1.f, region.h + top + e.f(kBottom))};
        u32 width = 0, height = 0;
        ctx.region_size(region, input.texel_scale_x() / std::clamp(e.f(kResolution), 1.f, 8.f), width, height);
        Uniforms u;
        u.region = {region.x, region.y, region.w, region.h};
        u.inputRegion = {input.region.x, input.region.y, input.region.w, input.region.h};
        u.frontColor = e.color(kFrontColor);
        u.sideControls = {static_cast<f32>(e.e(kSideMode)), static_cast<f32>(e.e(kColorCount)),
            e.f(kGlobalOffset), e.b(kInterpolate) ? 1.f : 0.f};
        u.angles[0] = {0, e.f(kAngle2), e.f(kAngle3), e.f(kAngle4)};
        u.angles[1] = {e.f(kAngle5), 0, 0, 0};
        u.colors[0] = e.color(kSideColor); u.colors[1] = e.color(kColor2);
        u.colors[2] = e.color(kColor3); u.colors[3] = e.color(kColor4); u.colors[4] = e.color(kColor5);
        u.controls = {e.b(kInheritColor) ? 1.f : 0.f, std::clamp(e.f(kFillOpacity), 0.f, 100.f) * .01f,
            std::clamp(e.f(kFrontResolution), 1.f, 10.f), e.auxInfo.x};
        u32 geometryWidth = 0, geometryHeight = 0;
        const f32 geometryScale = input.texel_scale_x() * u.controls.z / std::clamp(e.f(kResolution), 1.f, 8.f);
        ctx.region_size(region, geometryScale, geometryWidth, geometryHeight);
        LayerImage geometry{ctx.texture("extrude-vector-geometry", geometryWidth, geometryHeight),
            region, geometryWidth, geometryHeight};
        const u32 mesh = ctx.geometry_pass("extrude-vector-geometry", PassStage::Effects, geometry.texture,
            ShaderId::effects_extrude_reference_vert, ShaderId::effects_extrude_reference_frag,
            {{input.texture, {}, CommonSampler::LinearBorder}, {{}, e.aux, CommonSampler::NearestClamp}},
            &u, sizeof(u), static_cast<u32>(e.auxInfo.z), true, true);
        if (mesh == kInvalidIndex) return Errc::PipelineCompileFailed;
        struct Composite { Vec4 sourceUv; Vec4 controls; } composite{
            EffectBuildContext::uv_map(region, input.region),
            {std::clamp(e.f(kInputOpacity), 0.f, 100.f) * .01f, u.controls.z,
             static_cast<f32>(width), static_cast<f32>(height)}};
        out = {ctx.texture("extrude-vector-composite", width, height), region, width, height};
        const u32 pass = ctx.fullscreen_pass("extrude-vector-composite", PassStage::Effects, out.texture,
            ShaderId::effects_extrude_reference_composite_frag,
            {{input.texture, {}, CommonSampler::LinearBorder}, {geometry.texture, {}, CommonSampler::LinearBorder}},
            &composite, sizeof(composite));
        return pass == kInvalidIndex ? Status{Errc::PipelineCompileFailed} : OkStatus;
    }
};
}

void register_effect(EffectRegistry& registry) { (void)registry.add(std::make_unique<Extrude>()); }

} // namespace aurea::extrude_reference
