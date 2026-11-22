#include "aurea/text/TextGradient.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Palette.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Layer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace aurea::text {
namespace {
enum Param : u32 {
    Shape, Mapping, Target, Palette, Stops, ColorSpace, Blend, Amount,
    CenterX, CenterY, Angle, Scale, Aspect, Offset, Repeats, Extend,
    Gamma, Hue, Exposure, StaggerBy, Delay, OffsetStep, Order, Seed,
    SizeMode, SizePx, AlignBounds, RangeStart, RangeEnd, OutsideRange,
    RadialX, RadialY, OffsetMode, SizeStep, HueStep, AngleStep, CenterStepX, CenterStepY,
    ScaleStepX, ScaleStepY, OpacityStep, PathMode, PathDelay, PathX, PathY,
    PathRotation, PathScaleX, PathScaleY, StrokeWidth, SoloStroke,
    Original, OriginalOpacity, SourceMatte, InvertMatte, StopBlend,
    RangeUnits, RangeOffset, Wrap, PaletteMirror,
};
constexpr u32 kSamples = 128;
constexpr u32 kColorsPerRecord = 8;
constexpr u32 kPaletteCount = 6;
constexpr u32 kSpaceCount = 3;
constexpr usize kNoPalette = std::numeric_limits<usize>::max();

// The glyph fragment shader reads this record format from the same buffer as
// the SDF quads. All references are relative so nested snapshots can relocate.
struct Unit {
    Vec4 box{1e20f, 1e20f, -1e20f, -1e20f};
    u32 rank = 0;
    void include(Vec4 area) noexcept {
        box = {std::min(box.x, area.x), std::min(box.y, area.y),
               std::max(box.z, area.z), std::max(box.w, area.w)};
    }
};

Vec3 hsl(Vec3 c) noexcept {
    const f32 hi = std::max({c.x,c.y,c.z}), lo = std::min({c.x,c.y,c.z});
    const f32 light = (hi + lo) * .5f, chroma = hi - lo;
    if (chroma <= 1e-6f) return {0,0,light};
    f32 hue = hi == c.x ? (c.y - c.z) / chroma
            : hi == c.y ? 2 + (c.z - c.x) / chroma : 4 + (c.x - c.y) / chroma;
    hue /= 6;
    return {hue - std::floor(hue), chroma / std::max(1e-6f, 1 - std::abs(2 * light - 1)), light};
}

Vec3 from_hsl(Vec3 c) noexcept {
    const f32 hue = c.x - std::floor(c.x);
    const f32 chroma = (1 - std::abs(2 * c.z - 1)) * c.y;
    const auto lane = [&](f32 offset) {
        const f32 t = hue + offset;
        return c.z + chroma * (std::clamp(std::abs((t - std::floor(t)) * 6 - 3) - 1, 0.f, 1.f) - .5f);
    };
    return {lane(0), lane(2.f / 3), lane(1.f / 3)};
}

Vec3 decode(Vec3 c) noexcept {
    return {Color::srgb_to_linear(c.x), Color::srgb_to_linear(c.y), Color::srgb_to_linear(c.z)};
}

Vec3 from_oklab(Vec3 c) noexcept {
    const f32 l = c.x + .3963377774f * c.y + .2158037573f * c.z;
    const f32 m = c.x - .1055613458f * c.y - .0638541728f * c.z;
    const f32 s = c.x - .0894841775f * c.y - 1.2914855480f * c.z;
    const f32 ll = l*l*l, mm = m*m*m, ss = s*s*s;
    return {std::max(0.f, 4.0767416621f*ll - 3.3077115913f*mm + .2309699292f*ss),
            std::max(0.f, -1.2684380046f*ll + 2.6097574011f*mm - .3413193965f*ss),
            std::max(0.f, -.0041960863f*ll - .7034186147f*mm + 1.7076147010f*ss)};
}

std::vector<GradientStop> palette(const EffectInstance& effect, u32 choice) {
    if (choice == 0 && effect.params.size() > Stops) {
        const u64 ref = effect.params[Stops].constant.ref;
        if (ref < effect.gradients.size() && effect.gradients[ref].stops.size() >= 2)
            return effect.gradients[ref].stops;
    }
    constexpr u32 colors[][4] = {
        {0x392adfff, 0x7e50ffff, 0xff76ccff, 0xffc4a3ff},
        {0x101d69ff, 0x2859c8ff, 0x30b8f2ff, 0xafffeaff},
        {0x4b182aff, 0xd64c45ff, 0xff973fff, 0xffe58fff},
        {0x111722ff, 0x455165ff, 0xa8b6c7ff, 0xf9fbffff},
        {0xff0000ff, 0x00ff00ff, 0x0000ffff, 0xff0000ff},
    };
    const auto& picked = colors[choice == 0 ? 0 : choice - 1];
    std::vector<GradientStop> stops;
    for (u32 i = 0; i < 4; ++i) stops.push_back({i / 3.f, Color::from_hex(picked[i]).to_vec4()});
    return stops;
}

void append_palette(std::vector<GlyphInstance>& data, std::vector<GradientStop> stops, u32 space) {
    GlyphInstance positions;
    const usize size = std::min<usize>(16, stops.size());
    positions.misc.x = static_cast<f32>(size);
    for (usize i = 0; i < size; ++i) {
        Vec4& row = positions.xform.col[i / 4];
        f32* components[]{&row.x, &row.y, &row.z, &row.w};
        *components[i % 4] = stops[i].position;
    }
    data.push_back(positions);
    for (GradientStop& stop : stops) {
        const Vec3 display{std::clamp(stop.color.x, 0.f, 8.f), std::clamp(stop.color.y, 0.f, 8.f),
                           std::clamp(stop.color.z, 0.f, 8.f)};
        Vec3 c = space == 2 ? srgb_to_oklab(display) : decode(display);
        if (space == 1) c = hsl({std::clamp(c.x,0.f,1.f),std::clamp(c.y,0.f,1.f),std::clamp(c.z,0.f,1.f)});
        stop.color = {c.x, c.y, c.z, stop.color.w};
    }
    usize next = 1;
    for (u32 block = 0; block < kSamples / kColorsPerRecord; ++block) {
        GlyphInstance record;
        Vec4* lanes[]{&record.rect, &record.uv, &record.fill, &record.stroke,
                      &record.xform.col[0], &record.xform.col[1], &record.xform.col[2], &record.xform.col[3]};
        for (u32 lane = 0; lane < kColorsPerRecord; ++lane) {
            const f32 x = (block * kColorsPerRecord + lane) / static_cast<f32>(kSamples - 1);
            while (next + 1 < stops.size() && x >= stops[next].position) ++next;
            const GradientStop& a = stops[next - 1];
            const GradientStop& b = stops[next];
            const f32 p = std::clamp((x - a.position) / std::max(1e-6f, b.position - a.position), 0.f, 1.f);
            Vec3 start{a.color.x, a.color.y, a.color.z}, end{b.color.x, b.color.y, b.color.z};
            if (space == 1) {
                if (start.y < 1e-5f) start.x = end.x;
                if (end.y < 1e-5f) end.x = start.x;
                end.x = start.x + std::remainder(end.x - start.x, 1.f);
            }
            Vec3 rgb = Vec3::lerp(start, end, p);
            if (space == 1) rgb = from_hsl(rgb);
            if (space == 2) rgb = from_oklab(rgb);
            *lanes[lane] = {rgb.x, rgb.y, rgb.z, std::lerp(a.color.w, b.color.w, p)};
        }
        data.push_back(record);
    }
}
}

void declare_prism_parameters(ParameterRegistry& p) {
    static const char* shapes[]{"Linear", "Radial", "Conical"};
    static const char* units[]{"Character", "Word", "Line"};
    static const char* mappings[]{"Character", "Word", "Line", "Entire Text"};
    static const char* targets[]{"Fill", "Stroke", "Fill and Stroke"};
    static const char* palettes[]{"Custom", "Prism", "Glacier", "Ember", "Silver", "Spectrum"};
    static const char* spaces[]{"RGB", "HSL", "Oklab"};
    static const char* blends[]{"Normal", "Multiply", "Screen", "Overlay", "Alpha Matte"};
    static const char* extensions[]{"Clamp", "Repeat", "Mirror"};
    static const char* orders[]{"Forward", "Reverse", "Random", "Interleave"};
    static const char* sizes[]{"Auto", "Manual"};
    static const char* alignment[]{"None", "X", "Y", "XY"};
    static const char* range[]{"Original", "Hidden"};
    static const char* offsets[]{"Cascade", "Stack"};
    static const char* original[]{"None", "Behind", "Above"};
    static const char* matte[]{"None", "Alpha", "Luma"};
    static const char* rangeUnits[]{"Percentage", "Index"};
    const auto percent = [&](const char* id, const char* label, f32 def, f32 lo, f32 hi) {
        p.add_float(id, label, def, lo, hi, kParamAnimatable | kParamPercent, "%");
    };
    p.add_enum("shape", "Forma", shapes, 3, 0);
    p.add_enum("mapping", "Mapear por", mappings, 4, 0);
    p.add_enum("target", "Aplicar em", targets, 3, 0);
    p.add_enum("palette", "Paleta", palettes, kPaletteCount, 5);
    p.add_gradient("gradient", "Degradê RGBA");
    p.set_enabled_by(Stops, Palette, 1u);
    p.add_enum("color_space", "Espaço de cor", spaces, kSpaceCount, 1);
    p.add_enum("blend", "Mesclagem", blends, 5, 0);
    percent("mix", "Intensidade", 100, 0, 100);
    percent("center_x", "Centro X", 50, -200, 300);
    percent("center_y", "Centro Y", 50, -200, 300);
    p.add_angle("angle", "Ângulo", 90);
    percent("scale", "Escala", 100, 1, 500);
    percent("aspect", "Proporção", 100, 1, 500);
    percent("offset", "Deslocamento", 0, -1000, 1000);
    p.add_float("repeats", "Repetições", 1, 1, 32);
    p.add_enum("extend", "Extensão", extensions, 3, 0);
    p.add_float("gamma", "Gama", 1, .1f, 4);
    p.add_angle("hue", "Matiz", 0);
    p.add_float("exposure", "Exposição HDR", 0, -4, 8, kParamAnimatable, "EV");
    p.add_enum("stagger_by", "Defasar por", units, 3, 0);
    p.add_float("delay", "Atraso por unidade", 0, -30, 30, kParamAnimatable | kParamFrames, "frames");
    percent("offset_step", "Deslocamento por unidade", 0, -100, 100);
    p.add_enum("order", "Ordem", orders, 4, 0);
    p.add_int("seed", "Semente", 1, 0, 65535);
    p.add_enum("size_mode", "Modo de tamanho", sizes, 2, 0);
    p.add_float("size_px", "Tamanho manual", 100, 1, 4000, kParamAnimatable | kParamPixels, "px");
    p.set_enabled_by(SizePx, SizeMode, 2u);
    p.add_enum("align_bounds", "Alinhamento dos limites", alignment, 4, 0);
    p.add_float("range_start", "Início", 0, 0, 100);
    p.typed_range(0, 10000);
    p.add_float("range_end", "Fim", 100, 0, 100);
    p.typed_range(0, 10000);
    p.add_enum("outside_range", "Fora da faixa", range, 2, 0);
    percent("radial_x", "Origem radial X", 50, -200, 300);
    percent("radial_y", "Origem radial Y", 50, -200, 300);
    p.set_enabled_by(RadialX, Shape, 2u);
    p.set_enabled_by(RadialY, Shape, 2u);
    p.add_enum("offset_mode", "Modo da defasagem", offsets, 2, 0);
    percent("size_step", "Tamanho por unidade", 0, -100, 100);
    p.add_angle("hue_step", "Matiz por unidade", 0);
    p.add_angle("angle_step", "Ângulo por unidade", 0);
    percent("center_step_x", "Centro X por unidade", 0, -200, 200);
    percent("center_step_y", "Centro Y por unidade", 0, -200, 200);
    percent("scale_step_x", "Escala X por unidade", 0, -100, 100);
    percent("scale_step_y", "Escala Y por unidade", 0, -100, 100);
    percent("opacity_step", "Opacidade por unidade", 0, -100, 100);
    p.add_enum("path_mode", "Modo do movimento", offsets, 2, 0);
    p.add_float("path_delay", "Atraso do movimento", 0, -30, 30, kParamAnimatable | kParamFrames, "quadros");
    percent("path_x", "Mover letras X", 0, -300, 300);
    percent("path_y", "Mover letras Y", 0, -300, 300);
    p.add_angle("path_rotation", "Girar letras", 0);
    percent("path_scale_x", "Escala das letras X", 0, -100, 400);
    percent("path_scale_y", "Escala das letras Y", 0, -100, 400);
    p.add_float("stroke_width", "Espessura do contorno", 0, 0, 128, kParamAnimatable | kParamPixels, "px");
    p.add_bool("solo_stroke", "Só contorno", false);
    p.add_enum("original", "Compor original", original, 3, 0);
    percent("original_opacity", "Opacidade do original", 100, 0, 100);
    p.set_enabled_by(OriginalOpacity, Original, 6u);
    p.add_enum("source_matte", "Máscara do original", matte, 3, 0);
    p.add_bool("invert_matte", "Inverter máscara", false);
    p.set_enabled_by(InvertMatte, SourceMatte, 6u);
    percent("stop_blend", "Suavidade das cores", 100, 0, 100);
    p.add_enum("range_units", "Unidades", rangeUnits, 2, 0);
    p.add_float("range_offset", "Deslocamento da faixa", 0, -100, 100);
    p.typed_range(-10000, 10000);
    p.add_bool("wrap_cycle", "Repetir ciclo", false);
    p.add_bool("mirror_palette", "Espelhar paleta", false);
    p.add_section("style", "Estilo", {Shape, Mapping, Target, Palette, Stops, ColorSpace, StopBlend, PaletteMirror, Blend, Amount});
    p.add_section("geometry", "Geometria", {CenterX, CenterY, Angle, Scale, Aspect, Offset, Repeats, Extend, Wrap,
                                            SizeMode, SizePx, AlignBounds, RangeUnits, RangeStart, RangeEnd, RangeOffset, OutsideRange, RadialX, RadialY});
    p.add_section_range("color", "Cor", Gamma, Exposure);
    p.add_section("stagger", "Defasagem", {StaggerBy, OffsetMode, Delay, OffsetStep, Order, Seed,
                                           SizeStep, HueStep, AngleStep, CenterStepX, CenterStepY, ScaleStepX, ScaleStepY, OpacityStep},
                  kInvalidIndex, false);
    p.add_section_range("paths", "Movimento das letras", PathMode, PathScaleY, kInvalidIndex, false);
    p.add_section_range("stroke", "Contorno", StrokeWidth, SoloStroke, kInvalidIndex, false);
    p.add_section_range("composite", "Composição", Original, InvertMatte, kInvalidIndex, false);
}

PrismDrawData prepare_prism(const Layer& layer, const TextLayout& layout,
                            const EffectRegistry& registry, f64 localTime,
                            std::vector<GlyphInstance>& data) {
    const EffectTypeId type = effect_type_id(kTextPrismEffect);
    const auto matches = [=](const EffectInstance& e) { return e.enabled && e.type == type; };
    if (layout.quads.empty() || std::none_of(layer.effects.begin(), layer.effects.end(), matches)) return {};
    const ParameterRegistry* specs = registry.params(type);
    if (!specs) return {};

    std::array<std::vector<Unit>,3> groups;
    groups[0].resize(layout.chars);
    groups[1].resize(layout.words);
    groups[2].resize(layout.lines);
    for (const GlyphQuad& glyph : layout.quads) {
        const f32 px = kGlyphSpread * glyph.k;
        const Vec4 ink{std::min(glyph.x0 + px, (glyph.x0 + glyph.x1) * .5f - .5f),
                       std::min(glyph.y0 + px, (glyph.y0 + glyph.y1) * .5f - .5f),
                       std::max(glyph.x1 - px, (glyph.x0 + glyph.x1) * .5f + .5f),
                       std::max(glyph.y1 - px, (glyph.y0 + glyph.y1) * .5f + .5f)};
        const std::array ids{glyph.charIndex, glyph.wordIndex, glyph.lineIndex};
        for (u32 i = 0; i < groups.size(); ++i) {
            if (groups[i].size() <= ids[i]) groups[i].resize(static_cast<usize>(ids[i]) + 1);
            groups[i][ids[i]].include(ink);
        }
    }
    Unit textBounds;
    std::array<u32, 3> counts{};
    for (u32 i = 0; i < groups.size(); ++i)
        for (Unit& unit : groups[i]) if (unit.box.x <= unit.box.z) {
            unit.rank = counts[i]++;
            if (i == 0) textBounds.include(unit.box);
        }

    const usize firstDirectory = data.size();
    const u32 effectCount = static_cast<u32>(std::count_if(layer.effects.begin(), layer.effects.end(), matches));
    data.resize(firstDirectory + effectCount);
    usize directory = firstDirectory;
    PrismDrawData result;
    bool moves = false;
    for (const EffectInstance& effect : layer.effects) {
        if (!matches(effect)) continue;
        const auto sample = [&](u32 param, f64 time) {
            return evaluate_param_f(layer.tracks, effect, param, specs->at(param), time).as_float();
        };
        const u32 group = static_cast<u32>(sample(StaggerBy, localTime));
        const u32 count = std::max(1u, counts[group]);
        const f32 delay = sample(Delay, localTime);
        const f32 phaseStep = sample(OffsetStep, localTime) * .01f;
        const u32 order = static_cast<u32>(sample(Order, localTime));
        const bool stack = sample(OffsetMode, localTime) == 1;
        const bool pathStack = sample(PathMode, localTime) == 1;
        const f32 pathDelay = sample(PathDelay, localTime);
        const bool animated = layer.tracks.has_animation();
        const auto nonzero = [&](u32 first, u32 last) {
            for (u32 field = first; field <= last; ++field) if (sample(field, localTime) != 0) return true;
            return false;
        };
        const bool precisionCascade = delay != 0 && animated && !stack;
        const bool multipleStyles = precisionCascade || (stack && nonzero(SizeStep, OpacityStep))
            || (pathStack && nonzero(PathX, PathScaleY)) || (pathDelay != 0 && animated)
            || (order >= 2 && phaseStep != 0);
        const u32 styleCount = multipleStyles ? count : 1;
        std::vector<u32> ranks;
        if (order == 2 && multipleStyles) {
            ranks.resize(count);
            std::iota(ranks.begin(), ranks.end(), 0);
            std::mt19937 rng(static_cast<u32>(sample(Seed, localTime)));
            for (u32 i = count; i > 1; --i) std::swap(ranks[i - 1], ranks[rng() % i]);
        }
        const usize firstStyle = data.size();
        data.resize(firstStyle + styleCount);
        data[directory].rect = {static_cast<f32>(group), static_cast<f32>(styleCount - 1),
                                static_cast<f32>(firstStyle - directory), multipleStyles ? 0.f : phaseStep};
        data[directory].uv = {order == 1 ? -1.f : 1.f, static_cast<f32>(count - 1), 0, 0};
        ++directory;
        std::array<usize, kPaletteCount * kSpaceCount> palettes;
        palettes.fill(kNoPalette);
        for (u32 index = 0; index < styleCount; ++index) {
            u32 rank = index;
            if (order == 1) rank = count - 1 - index;
            if (order == 2 && !ranks.empty()) rank = ranks[index];
            if (order == 3) rank = index / 2 + (index % 2) * ((count + 1) / 2);
            if (!multipleStyles) rank = 0;
            const f64 t = localTime - rank * static_cast<f64>(delay);
            const auto p = [&](u32 field) { return sample(field, stack ? localTime : t); };
            const auto delta = [&](u32 field) { return p(field) * (stack ? static_cast<f32>(rank) : 1.f); };
            const auto path = [&](u32 field) {
                return sample(field, pathStack ? localTime : localTime - rank * static_cast<f64>(pathDelay))
                     * (pathStack ? static_cast<f32>(rank) : 1.f);
            };
            const u32 choice = static_cast<u32>(p(Palette)), space = static_cast<u32>(p(ColorSpace));
            usize& paletteIndex = palettes[choice * kSpaceCount + space];
            if (paletteIndex == kNoPalette) {
                paletteIndex = data.size();
                append_palette(data, palette(effect, choice), space);
            }
            const usize style = firstStyle + index;
            GlyphInstance& r = data[style];
            const f32 shape = p(Shape);
            const f32 angle = (p(Angle) + delta(AngleStep)) * kDeg2Rad;
            const f32 size = std::max(.01f, (p(Scale) + delta(SizeStep)) * .01f);
            const f32 cx = (p(shape == 1 ? RadialX : CenterX) + delta(CenterStepX)) * .01f;
            const f32 cy = (p(shape == 1 ? RadialY : CenterY) + delta(CenterStepY)) * .01f;
            r.rect = {shape, p(Mapping), p(Target), p(Blend)};
            r.uv = {cx, cy, size * std::max(.01f, 1.f + delta(ScaleStepX) * .01f),
                    size * p(Aspect) * .01f * std::max(.01f, 1.f + delta(ScaleStepY) * .01f)};
            r.fill = {std::cos(angle), std::sin(angle), p(Offset) * .01f + rank * phaseStep, p(Repeats)};
            r.stroke = {std::clamp((p(Amount) + delta(OpacityStep)) * .01f, 0.f, 1.f),
                        p(Extend), p(Gamma), std::exp2(p(Exposure))};
            r.xform.col[0] = {(p(Hue) + delta(HueStep)) / 360.f, static_cast<f32>(paletteIndex - style),
                              p(SizeMode) > .5f ? p(SizePx) : 0.f, p(AlignBounds)};
            const bool indexed = p(RangeUnits) != 0;
            const f32 rangeScale = indexed ? 1.f : .01f;
            r.xform.col[1] = {(p(RangeStart) + p(RangeOffset)) * rangeScale, (p(RangeEnd) + p(RangeOffset)) * rangeScale,
                               p(OutsideRange), p(StopBlend) * .01f};
            r.xform.col[2] = {p(StrokeWidth), p(SoloStroke), p(SourceMatte), p(InvertMatte)};
            r.xform.col[3] = {path(PathX) * .01f, path(PathY) * .01f, path(PathRotation) * kDeg2Rad, p(Original)};
            r.extra = {std::max(.001f, 1.f + path(PathScaleX) * .01f),
                       std::max(.001f, 1.f + path(PathScaleY) * .01f), p(OriginalOpacity) * .01f, 0};
            r.misc = {indexed ? 1.f : 0.f, p(Wrap), p(PaletteMirror), 0};
            result.stroke = std::max(result.stroke, r.xform.col[2].x);
            const Vec4 movement = r.xform.col[3];
            moves |= movement.x != 0 || movement.y != 0 || movement.z != 0 || r.extra.x != 1 || r.extra.y != 1;
        }
    }
    result.enabled = true;
    result.firstGlyph = data.size();
    if (moves) result.transforms.reserve(layout.quads.size());
    for (const GlyphQuad& glyph : layout.quads) {
        const Unit& character = groups[0][glyph.charIndex];
        const Unit& word = groups[1][glyph.wordIndex];
        const Unit& line = groups[2][glyph.lineIndex];
        GlyphInstance record;
        record.rect = character.box; record.uv = word.box; record.fill = line.box;
        record.stroke = {static_cast<f32>(character.rank), static_cast<f32>(word.rank), static_cast<f32>(line.rank), 0};
        record.misc = {-static_cast<f32>(data.size() - firstDirectory), static_cast<f32>(effectCount), 0, 0};
        record.xform.col[0] = textBounds.box;
        record.xform.col[1] = {static_cast<f32>(counts[0]), static_cast<f32>(counts[1]), static_cast<f32>(counts[2]), 1};
        data.push_back(record);
        if (!moves) continue;
        Mat4 transform = Mat4::identity();
        const std::array<const Unit*,4> all{&character, &word, &line, &textBounds};
        for (usize i = firstDirectory; i < firstDirectory + effectCount; ++i) {
            const Vec4& entry = data[i].rect;
            const Unit& timed = *all[static_cast<u32>(entry.x)];
            const auto& style = data[i + static_cast<usize>(entry.z) + std::min<usize>(timed.rank, static_cast<usize>(entry.y))];
            const u32 group = static_cast<u32>(style.rect.y);
            const Unit& unit = *all[group];
            const f32 rank = style.misc.x > .5f ? static_cast<f32>(unit.rank)
                : (unit.rank + .5f) / (group == 3 ? 1.f : static_cast<f32>(std::max(1u, counts[group])));
            const Vec4 range = style.xform.col[1];
            if (rank < range.x || rank > range.y) continue;
            const Vec4 movement = style.xform.col[3];
            if (movement.x != 0 || movement.y != 0 || movement.z != 0 || style.extra.x != 1 || style.extra.y != 1) {
                const Vec3 pivot{(timed.box.x + timed.box.z) * .5f, (timed.box.y + timed.box.w) * .5f, 0};
                const Vec3 delta{movement.x * (timed.box.z - timed.box.x), movement.y * (timed.box.w - timed.box.y), 0};
                transform = Mat4::translation(pivot + delta)
                    * Mat4::from_quat(Quat::from_axis_angle(Vec3{0,0,1}, movement.z))
                    * Mat4::scale(Vec3{style.extra.x, style.extra.y, 1})
                    * Mat4::translation(-pivot) * transform;
            }
        }
        result.transforms.push_back(transform);
    }
    return result;
}
}
