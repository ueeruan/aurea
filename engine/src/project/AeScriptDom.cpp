// =============================================================================
//  Aurea / project / AeScriptDom.cpp
//
//  O DOM do After Effects que o interpretador de .jsx enxerga (AeScript.hpp):
//
//    app (beginUndoGroup/endUndoGroup = um passo só), app.project.activeItem
//    (a composição atual), comp.layers.addText/addSolid/addShape/addNull/
//    addCamera/addLight, comp.layer(i | nome), comp.selectedLayers, tempo da
//    camada (startTime/inPoint/outPoint), layer.property("ADBE …" | nome)
//    até as folhas — transform, efeitos (Effects.addProperty), Text Animator
//    (Animators/Selectors/Properties.addProperty), Path Options, máscaras —
//    com setValue, setValueAtTime, setValuesAtTimes, addKey/keyTime/keyValue,
//    setTemporalEaseAtKey (KeyframeEase), setInterpolationTypeAtKey
//    (KeyframeInterpolationType), expression, e TextDocument (texto, tamanho,
//    cor, contorno, alinhamento, tracking, fonte).
//
//  As folhas usam os MESMOS endereços e conversões do leitor de .ffx
//  (AeImportBind). Os keyframes ficam também do lado AE (tempo da composição,
//  ease em velocidade/influência) e a trilha do Aurea é reescrita a cada
//  mudança — a mesma conversão para bézier do .ffx.
//
//  SANDBOX: File, Folder, Socket, system, Window, $.evalFile, app.open,
//  app.executeCommand, app.project.save/importFile... viram objetos
//  "bloqueados": a chamada é relatada com a linha e nunca executada.
// =============================================================================
#include "AeImportBind.hpp"
#include "AeScript.hpp"

#include "aurea/core/Math.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::ae {
using namespace detail;
using script::Interp;
using script::Object;
using script::OK;
using script::T;
using script::Value;

namespace {

// --- tipos de objeto host --------------------------------------------------------
enum HK : u32 {
    HApp = 1, HProject, HItems, HComp, HLayers, HLayer, HProp, HBlocked, HDollar, HTextDoc,
};

constexpr i32 kInterpLinear = 6612, kInterpBezier = 6613, kInterpHold = 6614;

KeyInterp interp_from(f64 v) noexcept {
    const i64 i = std::llround(v);
    return i == kInterpBezier ? KeyInterp::Bezier : i == kInterpHold ? KeyInterp::Hold : KeyInterp::Linear;
}
f64 interp_to(KeyInterp k) noexcept {
    return k == KeyInterp::Bezier ? kInterpBezier : k == KeyInterp::Hold ? kInterpHold : kInterpLinear;
}

// --- grupos e folhas ---------------------------------------------------------------
enum class G : u8 {
    LayerRoot, Transform, Effects, Effect, TextProps, Animators, Animator, Selectors, Selector, Advanced, AnimProps,
    PathOptions, More, Masks, Mask, Dead, Leaf,
};
enum class LK : u8 { None, Transform, EffectParam, TextProp, TextSel, TextPath, TextMore, MaskParam, MaskShape, SourceText, Dead };

struct Child { const char* match; const char* names; G g; LK leaf; };

/// Filhos de cada grupo: matchName, nomes exibidos ('|') e o tipo.
constexpr Child kLayerChildren[] = {
    {"ADBE Marker", "Marker|marker", G::Leaf, LK::Dead},
    {"ADBE Time Remapping", "Time Remap|timeRemap", G::Leaf, LK::Dead},
    {"ADBE Transform Group", "Transform|transform", G::Transform, LK::None},
    {"ADBE Effect Parade", "Effects|effect|effects", G::Effects, LK::None},
    {"ADBE Mask Parade", "Masks|mask|masks", G::Masks, LK::None},
    {"ADBE Text Properties", "Text|text", G::TextProps, LK::None},
    {"ADBE Root Vectors Group", "Contents|content|contents", G::Dead, LK::None},
    {"ADBE Audio Group", "Audio|audio", G::Dead, LK::None},
    {"ADBE Material Options Group", "Material Options|Geometry Options|materialOption", G::Dead, LK::None},
    {"ADBE Camera Options Group", "Camera Options|cameraOption", G::Dead, LK::None},
    {"ADBE Light Options Group", "Light Options|lightOption", G::Dead, LK::None},
    {"ADBE Layer Styles", "Layer Styles|layerStyle", G::Dead, LK::None},
};
constexpr Child kTransformChildren[] = {
    {"ADBE Anchor Point", "Anchor Point|anchorPoint", G::Leaf, LK::Transform},
    {"ADBE Position", "Position|position", G::Leaf, LK::Transform},
    {"ADBE Position_0", "X Position|xPosition", G::Leaf, LK::Transform},
    {"ADBE Position_1", "Y Position|yPosition", G::Leaf, LK::Transform},
    {"ADBE Position_2", "Z Position|zPosition", G::Leaf, LK::Transform},
    {"ADBE Scale", "Scale|scale", G::Leaf, LK::Transform},
    {"ADBE Orientation", "Orientation|orientation", G::Leaf, LK::Transform},
    {"ADBE Rotate X", "X Rotation|xRotation", G::Leaf, LK::Transform},
    {"ADBE Rotate Y", "Y Rotation|yRotation", G::Leaf, LK::Transform},
    {"ADBE Rotate Z", "Rotation|Z Rotation|rotation|zRotation", G::Leaf, LK::Transform},
    {"ADBE Opacity", "Opacity|opacity", G::Leaf, LK::Transform},
};
constexpr Child kTextChildren[] = {
    {"ADBE Text Document", "Source Text|sourceText", G::Leaf, LK::SourceText},
    {"ADBE Text Path Options", "Path Options|pathOption|pathOptions", G::PathOptions, LK::None},
    {"ADBE Text More Options", "More Options|moreOption|moreOptions", G::More, LK::None},
    {"ADBE Text Animators", "Animators|animator|animators", G::Animators, LK::None},
};
constexpr Child kPathChildren[] = {
    {"ADBE Text Path", "Path|path", G::Leaf, LK::TextPath},
    {"ADBE Text Reverse Path", "Reverse Path|reversePath", G::Leaf, LK::TextPath},
    {"ADBE Text Perpendicular To Path", "Perpendicular To Path|perpendicularToPath", G::Leaf, LK::TextPath},
    {"ADBE Text Force Align Path", "Force Alignment|forceAlignment", G::Leaf, LK::TextPath},
    {"ADBE Text First Margin", "First Margin|firstMargin", G::Leaf, LK::TextPath},
    {"ADBE Text Last Margin", "Last Margin|lastMargin", G::Leaf, LK::TextPath},
};
constexpr Child kMoreChildren[] = {
    {"ADBE Text Anchor Point Option", "Anchor Point Grouping|anchorPointGrouping", G::Leaf, LK::TextMore},
    {"ADBE Text Anchor Point Align", "Grouping Alignment|groupingAlignment", G::Leaf, LK::TextMore},
    {"ADBE Text Render Order", "Fill & Stroke|fillAndStroke", G::Leaf, LK::TextMore},
    {"ADBE Text Character Blend Mode", "Inter-Character Blending|interCharacterBlending", G::Leaf, LK::TextMore},
};
constexpr Child kAnimatorChildren[] = {
    {"ADBE Text Selectors", "Selectors|selector|selectors", G::Selectors, LK::None},
    {"ADBE Text Animator Properties", "Properties|property|properties", G::AnimProps, LK::None},
};
constexpr Child kRangeChildren[] = {
    {"ADBE Text Percent Start", "Start|start", G::Leaf, LK::TextSel},
    {"ADBE Text Percent End", "End|end", G::Leaf, LK::TextSel},
    {"ADBE Text Percent Offset", "Offset|offset", G::Leaf, LK::TextSel},
    {"ADBE Text Index Start", "", G::Leaf, LK::TextSel},
    {"ADBE Text Index End", "", G::Leaf, LK::TextSel},
    {"ADBE Text Index Offset", "", G::Leaf, LK::TextSel},
    {"ADBE Text Range Advanced", "Advanced|advanced", G::Advanced, LK::None},
};
constexpr Child kAdvancedChildren[] = {
    {"ADBE Text Range Units", "Units|units", G::Leaf, LK::TextSel},
    {"ADBE Text Range Type2", "Based On|basedOn", G::Leaf, LK::TextSel},
    {"ADBE Text Selector Mode", "Mode|mode", G::Leaf, LK::TextSel},
    {"ADBE Text Selector Max Amount", "Amount|amount", G::Leaf, LK::TextSel},
    {"ADBE Text Range Shape", "Shape|shape", G::Leaf, LK::TextSel},
    {"ADBE Text Selector Smoothness", "Smoothness|smoothness", G::Leaf, LK::TextSel},
    {"ADBE Text Levels Max Ease", "Ease High|easeHigh", G::Leaf, LK::TextSel},
    {"ADBE Text Levels Min Ease", "Ease Low|easeLow", G::Leaf, LK::TextSel},
    {"ADBE Text Randomize Order", "Randomize Order|randomizeOrder", G::Leaf, LK::TextSel},
    {"ADBE Text Random Seed", "Random Seed|randomSeed", G::Leaf, LK::TextSel},
};
constexpr Child kWigglyChildren[] = {
    {"ADBE Text Selector Mode", "Mode|mode", G::Leaf, LK::TextSel},
    {"ADBE Text Wiggly Max Amount", "Max Amount|maxAmount", G::Leaf, LK::TextSel},
    {"ADBE Text Wiggly Min Amount", "Min Amount|minAmount", G::Leaf, LK::TextSel},
    {"ADBE Text Range Type2", "Based On|basedOn", G::Leaf, LK::TextSel},
    {"ADBE Text Temporal Freq", "Wiggles/Second|wigglesSecond", G::Leaf, LK::TextSel},
    {"ADBE Text Character Correlation", "Correlation|correlation", G::Leaf, LK::TextSel},
    {"ADBE Text Temporal Phase", "Temporal Phase|temporalPhase", G::Leaf, LK::TextSel},
    {"ADBE Text Spatial Phase", "Spatial Phase|spatialPhase", G::Leaf, LK::TextSel},
    {"ADBE Text Wiggly Lock Dim", "Lock Dimensions|lockDimensions", G::Leaf, LK::TextSel},
    {"ADBE Text Wiggly Random Seed", "Random Seed|randomSeed", G::Leaf, LK::TextSel},
};
constexpr Child kExprSelChildren[] = {
    {"ADBE Text Range Type2", "Based On|basedOn", G::Leaf, LK::TextSel},
    {"ADBE Text Expressible Amount", "Amount|amount", G::Leaf, LK::TextSel},
};
struct AnimPropDef { const char* match; const char* names; f64 def[4]; u32 dims; };
constexpr AnimPropDef kAnimProps[] = {
    {"ADBE Text Anchor Point 3D", "Anchor Point|anchorPoint", {0, 0, 0, 0}, 3},
    {"ADBE Text Position 3D", "Position|position", {0, 0, 0, 0}, 3},
    {"ADBE Text Scale 3D", "Scale|scale", {100, 100, 100, 0}, 3},
    {"ADBE Text Skew", "Skew|skew", {0, 0, 0, 0}, 1},
    {"ADBE Text Skew Axis", "Skew Axis|skewAxis", {0, 0, 0, 0}, 1},
    {"ADBE Text Rotation X", "X Rotation|xRotation", {0, 0, 0, 0}, 1},
    {"ADBE Text Rotation Y", "Y Rotation|yRotation", {0, 0, 0, 0}, 1},
    {"ADBE Text Rotation", "Rotation|Z Rotation|rotation|zRotation", {0, 0, 0, 0}, 1},
    {"ADBE Text Opacity", "Opacity|opacity", {100, 0, 0, 0}, 1},
    {"ADBE Text Fill Color", "Fill Color|fillColor", {1, 0, 0, 1}, 4},
    {"ADBE Text Fill Opacity", "Fill Opacity|fillOpacity", {100, 0, 0, 0}, 1},
    {"ADBE Text Fill Hue", "Fill Hue|fillHue", {0, 0, 0, 0}, 1},
    {"ADBE Text Fill Saturation", "Fill Saturation|fillSaturation", {0, 0, 0, 0}, 1},
    {"ADBE Text Fill Brightness", "Fill Brightness|fillBrightness", {0, 0, 0, 0}, 1},
    {"ADBE Text Stroke Color", "Stroke Color|strokeColor", {1, 0, 0, 1}, 4},
    {"ADBE Text Stroke Opacity", "Stroke Opacity|strokeOpacity", {100, 0, 0, 0}, 1},
    {"ADBE Text Stroke Hue", "Stroke Hue|strokeHue", {0, 0, 0, 0}, 1},
    {"ADBE Text Stroke Saturation", "Stroke Saturation|strokeSaturation", {0, 0, 0, 0}, 1},
    {"ADBE Text Stroke Brightness", "Stroke Brightness|strokeBrightness", {0, 0, 0, 0}, 1},
    {"ADBE Text Stroke Width", "Stroke Width|strokeWidth", {0, 0, 0, 0}, 1},
    {"ADBE Text Line Anchor", "Line Anchor|lineAnchor", {0, 0, 0, 0}, 1},
    {"ADBE Text Line Spacing", "Line Spacing|lineSpacing", {0, 0, 0, 0}, 2},
    {"ADBE Text Track Type", "Tracking Type|trackingType", {1, 0, 0, 0}, 1},
    {"ADBE Text Tracking Amount", "Tracking Amount|trackingAmount", {0, 0, 0, 0}, 1},
    {"ADBE Text Character Change Type", "Character Alignment|characterAlignment", {1, 0, 0, 0}, 1},
    {"ADBE Text Character Range", "Character Range|characterRange", {1, 0, 0, 0}, 1},
    {"ADBE Text Character Replace", "Character Value|characterValue", {0, 0, 0, 0}, 1},
    {"ADBE Text Character Offset", "Character Offset|characterOffset", {0, 0, 0, 0}, 1},
    {"ADBE Text Blur", "Blur|blur", {0, 0, 0, 0}, 2},
};
constexpr Child kMaskChildren[] = {
    {"ADBE Mask Shape", "Mask Path|Mask Shape|maskShape|maskPath", G::Leaf, LK::MaskShape},
    {"ADBE Mask Feather", "Mask Feather|maskFeather", G::Leaf, LK::MaskParam},
    {"ADBE Mask Opacity", "Mask Opacity|maskOpacity", G::Leaf, LK::MaskParam},
    {"ADBE Mask Offset", "Mask Expansion|maskExpansion|maskOffset", G::Leaf, LK::MaskParam},
};

bool alias_match(const char* names, std::string_view want) {
    std::string_view list(names);
    while (!list.empty()) {
        const usize bar = list.find('|');
        const std::string_view a = list.substr(0, bar);
        if (iequals(a, want)) return true;
        if (bar == std::string_view::npos) break;
        list.remove_prefix(bar + 1);
    }
    return false;
}

template <usize N>
const Child* find_child(const Child (&table)[N], std::string_view want) {
    for (const Child& c : table) if (want == c.match || alias_match(c.names, want)) return &c;
    return nullptr;
}

const AnimPropDef* find_anim_prop(std::string_view want) {
    for (const AnimPropDef& d : kAnimProps) if (want == d.match || alias_match(d.names, want)) return &d;
    return nullptr;
}

/// Primeiro nome exibido de uma lista ('|').
std::string first_name(const char* names) {
    const char* bar = std::strchr(names, '|');
    return bar ? std::string(names, static_cast<usize>(bar - names)) : std::string(names);
}

// --- referências de propriedade (o que um objeto Property/PropertyGroup é) ---------
struct PRef {
    LayerId layer;
    G g = G::Dead;
    LK leaf = LK::None;
    std::string match, name;
    u32 animator = kInvalidIndex;
    u32 selector = 0;          ///< ordem do seletor no animador (lado AE)
    u32 selKind = 0;           ///< 0 intervalo, 1 wiggly, 2 expressão
    u32 effectId = kInvalidIndex;
    const ERule* rule = nullptr;
    u16 aeParam = 0;
    u32 maskId = kInvalidIndex;
    u32 index = 1;             ///< propertyIndex
    i32 parent = -1;
    // lado AE da folha
    std::vector<Key> keys;
    std::string expression;
    bool exprEnabled = true;
    bool exprSet = false;
    std::vector<f64> lastValue;
    bool reported = false;
    u32 line = 0;              ///< linha do script da expressão
    u32 obj = 0;               ///< objeto do script já criado para esta referência
};

struct EffectEntry { u32 id; const ERule* rule; std::string name; };
struct SelEntry { u32 animator; u32 kind; };

f64 clamp_time(f64 t) noexcept { return std::isfinite(t) ? std::clamp(t, -1.0e6, 1.0e6) : 0.0; }

class AeDom final : public script::Dom {
public:
    AeDom(Host& h, LayerId target) : host_(h), target_(target) {}

    u32 keyCount = 0;

    // =========================================================================
    // instalação
    // =========================================================================
    void install(Interp& in) override {
        Composition* comp = host_.composition();
        fps_ = comp && comp->fps() > 0 ? comp->fps() : 30.0;
        time_ = static_cast<f64>(host_.playhead()) / fps_;
        in.define_global("app", host(in, HApp, "Application"));
        in.define_global("$", host(in, HDollar, "$"));
        // Classes do DOM (new X(...) volta para construct()).
        static const char* const kClasses[] = {"KeyframeEase", "TextDocument", "Shape", "MarkerValue", "CompItem", "AVLayer",
                                               "TextLayer", "ShapeLayer", "CameraLayer", "LightLayer", "Layer", "Property",
                                               "PropertyGroup", "PropertyBase", "FootageItem", "FolderItem", "AVItem", "Item"};
        u32 k = 0;
        for (const char* c : kClasses) in.define_global(c, in.native_fn(Interp::kDomNative + k++, c));
        // Bloqueados pelo sandbox.
        for (const char* b : {"File", "Folder", "Socket", "system", "Window", "BridgeTalk", "XMLHttpRequest", "ExternalObject",
                              "ImportOptions", "ScriptUI", "XML", "RenderQueue"}) {
            in.define_global(b, blocked(in, b));
        }
        auto consts = [&](const char* name, std::initializer_list<std::pair<const char*, f64>> v) {
            const Value o = in.plain(name);
            for (const auto& x : v) in.own_set(o.r, x.first, Value::num(x.second));
            in.define_global(name, o);
        };
        consts("KeyframeInterpolationType", {{"LINEAR", kInterpLinear}, {"BEZIER", kInterpBezier}, {"HOLD", kInterpHold}});
        consts("ParagraphJustification", {{"LEFT_JUSTIFY", 7413}, {"RIGHT_JUSTIFY", 7414}, {"CENTER_JUSTIFY", 7415},
                                          {"FULL_JUSTIFY_LASTLINE_LEFT", 7416}, {"FULL_JUSTIFY_LASTLINE_RIGHT", 7417},
                                          {"FULL_JUSTIFY_LASTLINE_CENTER", 7418}, {"FULL_JUSTIFY_LASTLINE_FULL", 7419}});
        consts("MaskMode", {{"NONE", 6812}, {"ADD", 6813}, {"SUBTRACT", 6814}, {"INTERSECT", 6815}, {"LIGHTEN", 6816},
                            {"DARKEN", 6817}, {"DIFFERENCE", 6818}});
        consts("LightType", {{"PARALLEL", 4412}, {"SPOT", 4413}, {"POINT", 4414}, {"AMBIENT", 4415}});
        consts("BlendingMode", {{"NORMAL", 5212}, {"ADD", 5213}, {"SUBTRACT", 5214}, {"MULTIPLY", 5215}, {"SCREEN", 5216},
                                {"OVERLAY", 5217}, {"DARKEN", 5218}, {"LIGHTEN", 5219}, {"COLOR_DODGE", 5220},
                                {"COLOR_BURN", 5221}, {"HARD_LIGHT", 5222}, {"SOFT_LIGHT", 5223}, {"DIFFERENCE", 5224},
                                {"EXCLUSION", 5225}, {"HUE", 5226}, {"SATURATION", 5227}, {"COLOR", 5228},
                                {"LUMINOSITY", 5229}, {"DIVIDE", 5230}, {"VIVID_LIGHT", 5231}, {"LINEAR_DODGE", 5232},
                                {"LINEAR_BURN", 5233}});
        consts("PropertyType", {{"PROPERTY", 6212}, {"INDEXED_GROUP", 6213}, {"NAMED_GROUP", 6214}});
        consts("PropertyValueType", {{"NO_VALUE", 6412}, {"ThreeD_SPATIAL", 6413}, {"ThreeD", 6414}, {"TwoD_SPATIAL", 6415},
                                     {"TwoD", 6416}, {"OneD", 6417}, {"COLOR", 6418}, {"CUSTOM_VALUE", 6419},
                                     {"MARKER", 6420}, {"LAYER_INDEX", 6421}, {"MASK_INDEX", 6422}, {"SHAPE", 6423},
                                     {"TEXT_DOCUMENT", 6424}});
        consts("TrackMatteType", {{"NO_TRACK_MATTE", 5012}, {"ALPHA", 5013}, {"ALPHA_INVERTED", 5014}, {"LUMA", 5015},
                                  {"LUMA_INVERTED", 5016}});
        consts("AutoOrientType", {{"NO_AUTO_ORIENT", 4212}, {"ALONG_PATH", 4213}, {"CAMERA_OR_POINT_OF_INTEREST", 4214}});
    }

    // =========================================================================
    // objetos
    // =========================================================================
    Value host(Interp& in, u32 kind, const char* cls, u64 h0 = 0, u64 h1 = 0) {
        const u32 o = in.new_object(OK::Host, cls);
        in.o(o).hostKind = kind;
        in.o(o).h0 = h0;
        in.o(o).h1 = h1;
        return Interp::obj(o);
    }
    Value blocked(Interp& in, const std::string& name) {
        const Value v = host(in, HBlocked, "Blocked");
        in.o(v).method = name;
        return v;
    }
    Value method(Interp& in, u32 obj, const std::string& name) {
        const u32 o = in.new_object(OK::BoundHost, "Function");
        in.o(o).h0 = obj;
        in.o(o).method = name;
        return Interp::obj(o);
    }
    Value comp_obj(Interp& in) {
        if (!compObj_) compObj_ = host(in, HComp, "CompItem").r;
        return Interp::obj(compObj_);
    }
    Value layer_obj(Interp& in, LayerId id) {
        Composition* c = host_.composition();
        const Layer* l = c ? c->layer(id) : nullptr;
        if (!l) return Value::null();
        const char* cls = l->kind == LayerKind::Text ? "TextLayer" : l->kind == LayerKind::Camera ? "CameraLayer"
                        : l->kind == LayerKind::Light ? "LightLayer"
                        : (l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector) ? "ShapeLayer" : "AVLayer";
        for (const auto& cached : layerObjs_) if (cached.first == id.pack()) return Interp::obj(cached.second);
        const Value v = host(in, HLayer, cls, id.pack());
        layerObjs_.emplace_back(id.pack(), v.r);
        return v;
    }
    Value prop_obj(Interp& in, u32 ref) {
        if (refs_[ref].obj) return Interp::obj(refs_[ref].obj);
        const Value v = host(in, HProp, refs_[ref].g == G::Leaf ? "Property" : "PropertyGroup", ref);
        refs_[ref].obj = v.r;
        return v;
    }

    static Value vec_value(Interp& in, const std::vector<f64>& v) {
        if (v.size() == 1) return Value::num(v[0]);
        std::vector<Value> e;
        for (f64 x : v) e.push_back(Value::num(x));
        return in.array(e);
    }

    // =========================================================================
    // camadas
    // =========================================================================
    Composition* comp() { return host_.composition(); }
    Layer* layer(LayerId id) { Composition* c = comp(); return c ? c->layer(id) : nullptr; }

    LayerId layer_at(i64 aeIndex) {
        Composition* c = comp();
        if (!c) return LayerId{};
        const auto& order = c->order();
        if (aeIndex < 1 || aeIndex > static_cast<i64>(order.size())) return LayerId{};
        return order.at(order.size() - static_cast<u32>(aeIndex));
    }
    i64 ae_index(LayerId id) {
        Composition* c = comp();
        if (!c) return 0;
        const i32 z = c->z_index_of(id);
        return z < 0 ? 0 : static_cast<i64>(c->order().size()) - z;
    }
    LayerId layer_named(const std::string& name) {
        Composition* c = comp();
        if (!c) return LayerId{};
        const auto& order = c->order();
        for (u32 i = order.size(); i-- > 0;) {
            const Layer* l = c->layer(order.at(i));
            if (l && l->name == name) return order.at(i);
        }
        return LayerId{};
    }

    WriteCtx wctx(Interp& in, Layer* l) {
        WriteCtx w;
        w.layer = l;
        w.fps = fps_;
        w.report = &in.report();
        w.registry = &host_.registry();
        w.compTime = true;
        w.line = in.line();
        return w;
    }

    Value create_layer(Interp& in, NewLayer kind, const NewLayerSpec& spec) {
        const LayerId id = host_.create_layer(kind, spec);
        if (!id.valid()) {
            in.unsupported("addLayer", "o Aurea não criou a camada (limite ou sem fonte de texto)");
            return Value::null();
        }
        ++in.report().layers;
        ++in.report().changes;
        return layer_obj(in, id);
    }

    // =========================================================================
    // referências de propriedade
    // =========================================================================
    u32 ref(PRef r) {
        for (u32 i = 0; i < refs_.size(); ++i) {
            const PRef& x = refs_[i];
            if (x.layer == r.layer && x.g == r.g && x.leaf == r.leaf && x.match == r.match && x.animator == r.animator &&
                x.selector == r.selector && x.effectId == r.effectId && x.maskId == r.maskId && x.aeParam == r.aeParam)
                return i;
        }
        refs_.push_back(std::move(r));
        return static_cast<u32>(refs_.size() - 1);
    }

    u32 group_ref(LayerId l, G g, std::string match, std::string name, i32 parent) {
        PRef r;
        r.layer = l;
        r.g = g;
        r.match = std::move(match);
        r.name = std::move(name);
        r.parent = parent;
        return ref(std::move(r));
    }

    u32 dead_ref(LayerId l, std::string match, i32 parent) {
        PRef r;
        r.layer = l;
        r.g = G::Dead;
        r.match = std::move(match);
        r.name = r.match;
        r.parent = parent;
        return ref(std::move(r));
    }

    /// Filho `want` (nome, matchName ou índice 1..) do grupo `pi` (−1 = raiz da camada).
    Value child(Interp& in, LayerId lid, i32 pi, const Value& wantV) {
        Layer* l = layer(lid);
        if (!l) return Value::null();
        const G g = pi < 0 ? G::LayerRoot : refs_[static_cast<usize>(pi)].g;
        const bool byIndex = wantV.is_num();
        const std::string want = byIndex ? std::string() : in.to_str(wantV);
        const i64 idx = byIndex ? static_cast<i64>(std::llround(wantV.n)) : 0;
        auto pick = [&](const auto& table) -> const Child* {
            if (byIndex) return idx >= 1 && static_cast<usize>(idx) <= std::size(table) ? &table[idx - 1] : nullptr;
            return find_child(table, want);
        };
        auto leaf_ref = [&](const Child* c, PRef base) -> Value {
            base.layer = lid;
            base.g = c->g;
            base.leaf = c->leaf;
            base.match = c->match;
            base.name = first_name(c->names);
            base.parent = pi;
            if (c->g == G::Dead && c->leaf == LK::None) base.g = G::Dead;
            return prop_obj(in, ref(std::move(base)));
        };
        const PRef parent = pi >= 0 ? refs_[static_cast<usize>(pi)] : PRef{};
        switch (g) {
            case G::LayerRoot: {
                // Atalhos que o AE aceita direto na camada.
                if (!byIndex && (want == "Source Text" || want == "ADBE Text Document" || want == "sourceText") && l->kind == LayerKind::Text) {
                    PRef r; r.layer = lid; r.g = G::Leaf; r.leaf = LK::SourceText; r.match = "ADBE Text Document"; r.name = "Source Text";
                    return prop_obj(in, ref(std::move(r)));
                }
                if (const Child* c = pick(kLayerChildren)) {
                    if (c->g == G::TextProps && l->kind != LayerKind::Text) return Value::null();
                    if (c->g == G::Dead || c->leaf == LK::Dead) {
                        in.unsupported(std::string(c->match), "grupo do After Effects sem equivalente no Aurea");
                        return prop_obj(in, dead_ref(lid, c->match, pi));
                    }
                    return leaf_ref(c, PRef{});
                }
                if (const Child* c = byIndex ? nullptr : find_child(kTransformChildren, want)) return leaf_ref(c, PRef{});
                return Value::null();
            }
            case G::Transform: if (const Child* c = pick(kTransformChildren)) return leaf_ref(c, PRef{}); return Value::null();
            case G::TextProps: if (const Child* c = pick(kTextChildren)) return leaf_ref(c, PRef{}); return Value::null();
            case G::PathOptions: if (const Child* c = pick(kPathChildren)) return leaf_ref(c, PRef{}); return Value::null();
            case G::More: if (const Child* c = pick(kMoreChildren)) return leaf_ref(c, PRef{}); return Value::null();
            case G::Animators: {
                const i64 n = static_cast<i64>(l->text.animators.size());
                i64 a = -1;
                if (byIndex) a = idx - 1;
                else for (i64 i = 0; i < n; ++i) if (l->text.animators[static_cast<usize>(i)].name == want) a = i;
                if (a < 0 || a >= n) return Value::null();
                PRef r; r.layer = lid; r.g = G::Animator; r.match = "ADBE Text Animator"; r.animator = static_cast<u32>(a);
                r.name = l->text.animators[static_cast<usize>(a)].name; r.parent = pi; r.index = static_cast<u32>(a + 1);
                return prop_obj(in, ref(std::move(r)));
            }
            case G::Animator: {
                if (const Child* c = pick(kAnimatorChildren)) {
                    PRef b; b.animator = parent.animator;
                    return leaf_ref(c, b);
                }
                return Value::null();
            }
            case G::Selectors: {
                std::vector<u32> mine;
                for (u32 i = 0; i < sels_.size(); ++i) if (sels_[i].animator == parent.animator) mine.push_back(i);
                i64 s = -1;
                if (byIndex) s = idx - 1;
                else if (!mine.empty()) s = 0;   // por nome: o primeiro
                if (s < 0 || s >= static_cast<i64>(mine.size())) return Value::null();
                return selector_obj(in, lid, parent.animator, static_cast<u32>(s), sels_[mine[static_cast<usize>(s)]].kind, pi);
            }
            case G::Selector: {
                const Child* c = nullptr;
                if (parent.selKind == 0) c = pick(kRangeChildren);
                else if (parent.selKind == 1) c = pick(kWigglyChildren);
                else c = pick(kExprSelChildren);
                if (!c && parent.selKind == 0 && !byIndex) c = find_child(kAdvancedChildren, want);   // atalho
                if (!c) return Value::null();
                PRef b; b.animator = parent.animator; b.selector = parent.selector; b.selKind = parent.selKind;
                return leaf_ref(c, b);
            }
            case G::Advanced: {
                if (const Child* c = pick(kAdvancedChildren)) {
                    PRef b; b.animator = parent.animator; b.selector = parent.selector; b.selKind = parent.selKind;
                    return leaf_ref(c, b);
                }
                return Value::null();
            }
            case G::AnimProps: {
                const AnimPropDef* d = nullptr;
                if (byIndex) d = idx >= 1 && static_cast<usize>(idx) <= std::size(kAnimProps) ? &kAnimProps[idx - 1] : nullptr;
                else d = find_anim_prop(want);
                if (!d) return Value::null();
                PRef r; r.layer = lid; r.g = G::Leaf; r.leaf = LK::TextProp; r.match = d->match; r.name = first_name(d->names);
                r.animator = parent.animator; r.parent = pi;
                return prop_obj(in, ref(std::move(r)));
            }
            case G::Effects: {
                const auto list = effects_of(lid);
                const EffectEntry* e = nullptr;
                if (byIndex) e = idx >= 1 && static_cast<usize>(idx) <= list.size() ? &list[static_cast<usize>(idx - 1)] : nullptr;
                else for (const EffectEntry& x : list) {
                    if (iequals(x.name, want) || (x.rule && (iequals(x.rule->aeName, want) || want == x.rule->ae))) { e = &x; break; }
                }
                if (!e) return Value::null();
                return effect_obj(in, lid, *e, pi);
            }
            case G::Effect: {
                if (!parent.rule) return Value::null();
                const u16 ae = byIndex ? effect_param_by_position(*parent.rule, static_cast<u32>(idx)) : effect_param_number(*parent.rule, want);
                if (!ae) {
                    if (!byIndex && want == "Compositing Options") return prop_obj(in, dead_ref(lid, want, pi));
                    return Value::null();
                }
                PRef r; r.layer = lid; r.g = G::Leaf; r.leaf = LK::EffectParam; r.effectId = parent.effectId;
                r.rule = parent.rule; r.aeParam = ae; r.parent = pi;
                r.match = std::string(parent.rule->ae) + "-" + (ae < 10 ? "000" : ae < 100 ? "00" : ae < 1000 ? "0" : "") + std::to_string(ae);
                for (u32 i = 0; i < parent.rule->paramCount; ++i) if (parent.rule->params[i].ae == ae) { r.name = parent.rule->params[i].aeName; break; }
                return prop_obj(in, ref(std::move(r)));
            }
            case G::Masks: {
                i64 m = -1;
                if (byIndex) m = idx - 1;
                else for (usize i = 0; i < l->masks.size(); ++i) if (l->masks[i].name == want) m = static_cast<i64>(i);
                if (m < 0 || m >= static_cast<i64>(l->masks.size())) return Value::null();
                PRef r; r.layer = lid; r.g = G::Mask; r.match = "ADBE Mask Atom"; r.maskId = l->masks[static_cast<usize>(m)].id;
                r.name = l->masks[static_cast<usize>(m)].name; r.parent = pi; r.index = static_cast<u32>(m + 1);
                return prop_obj(in, ref(std::move(r)));
            }
            case G::Mask: {
                if (const Child* c = pick(kMaskChildren)) { PRef b; b.maskId = parent.maskId; return leaf_ref(c, b); }
                return Value::null();
            }
            case G::Dead: return prop_obj(in, dead_ref(lid, want.empty() ? "?" : want, pi));
            default: return Value::null();
        }
    }

    std::vector<EffectEntry> effects_of(LayerId lid) {
        std::vector<EffectEntry> out;
        for (const auto& e : effects_) if (e.first == lid) out.push_back(e.second);
        return out;
    }

    Value effect_obj(Interp& in, LayerId lid, const EffectEntry& e, i32 parent) {
        PRef r;
        r.layer = lid;
        r.g = G::Effect;
        r.match = e.rule ? e.rule->ae : "";
        r.name = e.name;
        r.effectId = e.id;
        r.rule = e.rule;
        r.parent = parent;
        return prop_obj(in, ref(std::move(r)));
    }

    Value selector_obj(Interp& in, LayerId lid, u32 animator, u32 ordinal, u32 kind, i32 parent) {
        PRef r;
        r.layer = lid;
        r.g = G::Selector;
        r.match = kind == 0 ? "ADBE Text Selector" : kind == 1 ? "ADBE Text Wiggly Selector" : "ADBE Text Expressible Selector";
        r.name = kind == 0 ? "Range Selector " + std::to_string(ordinal + 1) : kind == 1 ? "Wiggly Selector " + std::to_string(ordinal + 1)
                                                                                    : "Expression Selector " + std::to_string(ordinal + 1);
        r.animator = animator;
        r.selector = ordinal;
        r.selKind = kind;
        r.parent = parent;
        r.index = ordinal + 1;
        return prop_obj(in, ref(std::move(r)));
    }

    // --- addProperty ---------------------------------------------------------------
    Value add_property(Interp& in, u32 ri, const std::string& name) {
        PRef& r = refs_[ri];
        const LayerId lid = r.layer;
        const i32 pi = static_cast<i32>(ri);
        Layer* l = layer(lid);
        if (!l) return Value::null();
        switch (r.g) {
            case G::Effects: {
                const ERule* rule = find_effect_rule(name);
                if (!rule) rule = find_effect_rule_by_name(name);
                if (!rule || !rule->key) {
                    in.unsupported("addProperty(\"" + name + "\")",
                                   rule && rule->reason ? rule->reason : "efeito sem equivalente no Aurea (ignorado)");
                    return prop_obj(in, dead_ref(lid, name, pi));
                }
                const u32 id = host_.add_effect(lid, effect_type_id(rule->key));
                if (id == kInvalidIndex) {
                    in.unsupported("addProperty(\"" + name + "\")", "o Aurea recusou este efeito nesta camada");
                    return prop_obj(in, dead_ref(lid, name, pi));
                }
                ++in.report().effects;
                ++in.report().changes;
                if (rule->reason) in.approx(rule->aeName, rule->reason);
                // Instância "Gaussian Blur", "Gaussian Blur 2"...
                u32 same = 0;
                for (const auto& e : effects_) if (e.first == lid && e.second.rule == rule) ++same;
                EffectEntry e{id, rule, same == 0 ? std::string(rule->aeName) : std::string(rule->aeName) + " " + std::to_string(same + 1)};
                effects_.emplace_back(lid, e);
                return effect_obj(in, lid, e, pi);
            }
            case G::Animators: {
                if (name != "ADBE Text Animator" && !iequals(name, "Animator")) return Value::null();
                if (l->text.animators.size() >= 64) { in.unsupported("addProperty(\"ADBE Text Animator\")", "a camada já tem 64 animadores"); return Value::null(); }
                TextAnimator a;
                a.name = "Animator " + std::to_string(l->text.animators.size() + 1);
                a.selector.type = 2;
                l->text.animators.push_back(a);
                ++in.report().animators;
                ++in.report().changes;
                const u32 ai = static_cast<u32>(l->text.animators.size() - 1);
                PRef nr; nr.layer = lid; nr.g = G::Animator; nr.match = "ADBE Text Animator"; nr.animator = ai; nr.name = a.name;
                nr.parent = pi; nr.index = ai + 1;
                return prop_obj(in, ref(std::move(nr)));
            }
            case G::Selectors: {
                u32 kind = 3;
                if (name == "ADBE Text Selector" || iequals(name, "Range Selector")) kind = 0;
                else if (name == "ADBE Text Wiggly Selector" || iequals(name, "Wiggly Selector")) kind = 1;
                else if (name == "ADBE Text Expressible Selector" || iequals(name, "Expression Selector")) kind = 2;
                if (kind > 2) return Value::null();
                u32 count = 0;
                for (const SelEntry& s : sels_) if (s.animator == r.animator) ++count;
                sels_.push_back(SelEntry{r.animator, kind});
                if (count == 0) {
                    if (r.animator < l->text.animators.size()) l->text.animators[r.animator].selector.type = kind == 1 ? 1 : 2;
                } else {
                    in.approx("addProperty(\"" + name + "\")", "o Aurea usa um seletor por animador: este foi ignorado");
                }
                ++in.report().changes;
                return selector_obj(in, lid, r.animator, count, kind, pi);
            }
            case G::AnimProps: {
                const AnimPropDef* d = find_anim_prop(name);
                if (!d) return Value::null();
                PRef nr; nr.layer = lid; nr.g = G::Leaf; nr.leaf = LK::TextProp; nr.match = d->match; nr.name = first_name(d->names);
                nr.animator = r.animator; nr.parent = pi;
                const u32 leaf = ref(std::move(nr));
                // O AE põe a propriedade com o valor padrão dela.
                std::vector<f64> def(d->def, d->def + d->dims);
                set_value(in, leaf, def, true);
                return prop_obj(in, leaf);
            }
            case G::Masks: {
                if (name != "ADBE Mask Atom" && !iequals(name, "Mask")) return Value::null();
                if (l->masks.size() >= kMaxMaskCount) { in.unsupported("addProperty(\"Mask\")", "a camada já tem o máximo de máscaras"); return Value::null(); }
                Mask m;
                m.id = l->alloc_mask_id();
                m.name = "Mask " + std::to_string(l->masks.size() + 1);
                l->masks.push_back(m);
                ++in.report().changes;
                PRef nr; nr.layer = lid; nr.g = G::Mask; nr.match = "ADBE Mask Atom"; nr.maskId = m.id; nr.name = m.name; nr.parent = pi;
                nr.index = static_cast<u32>(l->masks.size());
                return prop_obj(in, ref(std::move(nr)));
            }
            case G::Dead: return prop_obj(in, dead_ref(lid, name, pi));
            default:
                in.unsupported("addProperty(\"" + name + "\")", "este grupo não aceita propriedades novas no Aurea");
                return Value::null();
        }
    }

    // =========================================================================
    // folhas: ligação, valor, keyframes
    // =========================================================================
    Binding bind(PRef& r) {
        Layer* l = layer(r.layer);
        Binding b;
        if (!l) { b.unsupported = "camada não existe mais"; return b; }
        switch (r.leaf) {
            case LK::Transform: b = bind_transform(*l, r.match); break;
            case LK::TextProp: b = bind_text_property(*l, r.animator, r.match); break;
            case LK::TextSel: {
                if (r.selector != 0) { b.unsupported = "~extra"; break; }
                u32 units = 0;
                if (r.match.find("Index") != std::string::npos && r.animator < l->text.animators.size()) {
                    static const u32 ae[] = {1, 3, 4};
                    units = text_units(l->text.content, ae[std::min<u32>(l->text.animators[r.animator].selector.basedOn, 2)]);
                }
                b = bind_text_selector(*l, r.animator, r.selKind, r.match, units);
                break;
            }
            case LK::TextPath: b = bind_text_path(*l, r.match); break;
            case LK::TextMore: b = bind_text_more(*l, r.match); break;
            case LK::MaskParam: b = bind_mask(*l, r.maskId, r.match); break;
            case LK::EffectParam: {
                if (!r.rule) { b.unsupported = "efeito sem equivalente"; break; }
                f64 w = 1, h = 1;
                Composition* c = comp();
                layer_size(*l, c ? c->width() : 1920, c ? c->height() : 1080, w, h);
                b = bind_effect_param(*r.rule, r.effectId, r.aeParam, 1, host_.registry(), w, h, fps_);
                break;
            }
            default: b.unsupported = "propriedade sem equivalente no Aurea"; break;
        }
        return b;
    }

    u32 dims_of(PRef& r, const Binding& b) {
        Layer* l = layer(r.layer);
        if (r.leaf == LK::Transform && l && !l->threeD &&
            (r.match == "ADBE Position" || r.match == "ADBE Anchor Point" || r.match == "ADBE Scale")) return 2;
        if (r.leaf == LK::TextProp) if (const AnimPropDef* d = find_anim_prop(r.match)) return d->dims;
        return std::max<u32>(1, b.dims);
    }

    void report_once(Interp& in, PRef& r, const Binding& b) {
        if (r.reported || b.unsupported.empty() || b.unsupported[0] == '~') {
            if (b.unsupported == "~extra" && !r.reported) {
                in.approx(r.match, "ajuste de um seletor extra: o Aurea usa só o primeiro seletor do animador");
                r.reported = true;
            }
            return;
        }
        in.unsupported(r.name.empty() ? r.match : r.name + " (" + r.match + ")", b.unsupported);
        r.reported = true;
    }

    i64 local_frame(Layer* l, f64 t) {
        const i64 f = static_cast<i64>(std::llround(clamp_time(t) * fps_));
        return l ? f - l->start.value + l->offset.value : f;
    }

    std::vector<f64> current(Interp& in, PRef& r, f64 t) {
        (void)in;
        if (r.leaf == LK::Dead || r.g == G::Dead) return r.lastValue.empty() ? std::vector<f64>{0} : r.lastValue;
        Layer* l = layer(r.layer);
        const Binding b = bind(r);
        if (!l || !b.ok()) return r.lastValue.empty() ? std::vector<f64>(dims_of(r, b), 0.0) : r.lastValue;
        std::vector<f64> v = read_value(*l, b, local_frame(l, t));
        v.resize(dims_of(r, b), 0.0);
        if (b.dims == 4 && v.size() == 4 && (r.leaf == LK::TextProp)) v[3] = 1.0;
        return v;
    }

    void flush_keys(Interp& in, u32 ri) {
        PRef& r = refs_[ri];
        Layer* l = layer(r.layer);
        const Binding b = bind(r);
        if (!l) return;
        if (!b.ok()) { report_once(in, r, b); return; }
        WriteCtx w = wctx(in, l);
        clear_keys(w, b);
        if (r.keys.size() == 1) write_static(w, b, r.keys.front().value, false);
        else if (r.keys.size() > 1) write_keys(w, b, r.keys, false);
        ++in.report().changes;
    }

    void set_value(Interp& in, u32 ri, const std::vector<f64>& v, bool silentDefault = false) {
        PRef& r = refs_[ri];
        r.lastValue = v;
        if (r.leaf == LK::Dead || r.g == G::Dead) return;
        Layer* l = layer(r.layer);
        const Binding b = bind(r);
        if (!l) return;
        if (!b.ok()) { if (!silentDefault) report_once(in, r, b); return; }
        WriteCtx w = wctx(in, l);
        write_static(w, b, v, false);
        ++in.report().changes;
    }

    void upsert_key(PRef& r, f64 t, const std::vector<f64>& v) {
        for (Key& k : r.keys)
            if (std::fabs(k.time - t) < 1e-6) { k.value = v; return; }
        Key k;
        k.time = t;
        k.value = v;
        r.keys.push_back(std::move(k));
        std::stable_sort(r.keys.begin(), r.keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
    }

    Key* key_at(Interp& in, PRef& r, const Value& iv) {
        const i64 i = static_cast<i64>(std::llround(in.to_num(iv)));
        if (i < 1 || i > static_cast<i64>(r.keys.size())) {
            in.throw_error("índice de keyframe " + std::to_string(i) + " não existe");
            return nullptr;
        }
        return &r.keys[static_cast<usize>(i - 1)];
    }

    std::vector<Ease> eases(Interp& in, const Value& arr) {
        std::vector<Ease> out;
        if (!arr.is_obj() || in.o(arr).kind != OK::Array) return out;
        for (const Value& e : in.o(arr).elems) {
            if (!e.is_obj()) continue;
            Ease x;
            x.speed = in.to_num(in.own(e.r, "speed"));
            x.influence = std::clamp(in.to_num(in.own(e.r, "influence")), 0.1, 100.0);
            if (!std::isfinite(x.speed)) x.speed = 0;
            out.push_back(x);
        }
        return out;
    }

    Value ease_array(Interp& in, const std::vector<Ease>& v, usize dims) {
        std::vector<Value> out;
        const usize n = v.empty() ? dims : v.size();
        for (usize i = 0; i < n; ++i) {
            const Ease e = i < v.size() ? v[i] : Ease{};
            const Value o = in.plain("KeyframeEase");
            in.own_set(o.r, "speed", Value::num(e.speed));
            in.own_set(o.r, "influence", Value::num(e.influence));
            out.push_back(o);
        }
        return in.array(out);
    }

    // --- texto (Source Text) -----------------------------------------------------------
    Value text_document_from(Interp& in, const Layer& l) {
        const Value td = host(in, HTextDoc, "TextDocument");
        const TextData& t = l.text;
        in.own_set(td.r, "text", in.str(t.content));
        in.own_set(td.r, "fontSize", Value::num(t.size));
        in.own_set(td.r, "fillColor", vec_value(in, {t.color.x, t.color.y, t.color.z}));
        in.own_set(td.r, "strokeColor", vec_value(in, {t.strokeColor.x, t.strokeColor.y, t.strokeColor.z}));
        in.own_set(td.r, "strokeWidth", Value::num(t.strokeWidth));
        in.own_set(td.r, "applyFill", Value::boolean(true));
        in.own_set(td.r, "applyStroke", Value::boolean(t.strokeWidth > 0));
        in.own_set(td.r, "font", in.str(t.fontFamily.empty() ? "Default" : t.fontFamily));
        in.own_set(td.r, "justification", Value::num(t.alignment == 1 ? 7415 : t.alignment == 2 ? 7414 : 7413));
        in.own_set(td.r, "tracking", Value::num(t.tracking));
        in.own_set(td.r, "leading", Value::num(t.lineHeight * t.size));
        return td;
    }

    Value new_text_document(Interp& in, const std::string& text) {
        const Value td = host(in, HTextDoc, "TextDocument");
        in.own_set(td.r, "text", in.str(text));
        return td;
    }

    void apply_text_document(Interp& in, LayerId lid, const Value& v) {
        Layer* l = layer(lid);
        if (!l || l->kind != LayerKind::Text) return;
        TextData& t = l->text;
        if (v.is_str() || v.is_num()) {
            t.content = in.to_str(v);
        } else if (v.is_obj()) {
            bool f = false;
            const u32 o = v.r;
            Value x = in.own(o, "text", &f);
            if (f) t.content = in.to_str(x);
            x = in.own(o, "fontSize", &f);
            if (f && std::isfinite(in.to_num(x)) && in.to_num(x) > 0) t.size = static_cast<f32>(std::clamp(in.to_num(x), 1.0, 2000.0));
            auto color = [&](const char* key, Vec4& out) {
                bool g = false;
                const Value c = in.own(o, key, &g);
                if (!g) return;
                const std::vector<f64> c3 = in.to_vec(c);
                if (c3.size() >= 3) out = Vec4{static_cast<f32>(std::clamp(c3[0], 0.0, 1.0)), static_cast<f32>(std::clamp(c3[1], 0.0, 1.0)),
                                               static_cast<f32>(std::clamp(c3[2], 0.0, 1.0)), out.w};
            };
            color("fillColor", t.color);
            color("strokeColor", t.strokeColor);
            x = in.own(o, "strokeWidth", &f);
            const bool applyStroke = in.truthy(in.own(o, "applyStroke"));
            if (f) t.strokeWidth = applyStroke || !in.own(o, "applyStroke").nullish() ? static_cast<f32>(std::clamp(in.to_num(x), 0.0, 500.0)) : t.strokeWidth;
            if (!in.own(o, "applyStroke").nullish() && !applyStroke) t.strokeWidth = 0;
            x = in.own(o, "justification", &f);
            if (f) {
                const i64 j = std::llround(in.to_num(x));
                t.alignment = j == 7415 || j == 7418 ? 1 : j == 7414 || j == 7417 ? 2 : 0;
            }
            x = in.own(o, "tracking", &f);
            if (f && std::isfinite(in.to_num(x))) t.tracking = static_cast<f32>(in.to_num(x));
            x = in.own(o, "leading", &f);
            if (f && in.to_num(x) > 0 && t.size > 0) t.lineHeight = static_cast<f32>(std::clamp(in.to_num(x) / t.size, 0.3, 5.0));
            x = in.own(o, "font", &f);
            if (f && x.is_str()) {
                // PostScript do AE ("Arial-BoldMT") → família + peso.
                std::string ps = in.s(x), fam = ps, style;
                const usize dash = ps.find('-');
                if (dash != std::string::npos) { fam = ps.substr(0, dash); style = ps.substr(dash + 1); }
                if (fam.size() > 2 && fam.compare(fam.size() - 2, 2, "MT") == 0) fam.resize(fam.size() - 2);
                t.fontFamily = fam;
                t.fontPath.clear();
                t.fontWeight = style.find("Black") != std::string::npos ? 900 : style.find("Bold") != std::string::npos ? 700
                             : style.find("Light") != std::string::npos ? 300 : 400;
                t.fontItalic = style.find("Italic") != std::string::npos || style.find("Oblique") != std::string::npos;
                in.approx("TextDocument.font", "fonte \"" + ps + "\" usada pela família; sem ela no aparelho, fica a padrão");
            }
            x = in.own(o, "boxTextSize", &f);
            if (f) {
                const std::vector<f64> bs = in.to_vec(x);
                if (bs.size() >= 2) { t.boxMode = 1; t.box = Rect{0, 0, static_cast<f32>(bs[0]), static_cast<f32>(bs[1])}; t.autoSize = false; }
            }
        }
        host_.text_changed(lid);
        ++in.report().changes;
    }

    // --- máscara (Shape) ---------------------------------------------------------------
    void apply_shape(Interp& in, PRef& r, const Value& v, f64 timeOrNan) {
        Layer* l = layer(r.layer);
        if (!l || !v.is_obj()) return;
        Mask* m = nullptr;
        for (Mask& x : l->masks) if (x.id == r.maskId) m = &x;
        if (!m) return;
        auto pts = [&](const char* key) {
            std::vector<std::vector<f64>> out;
            const Value a = in.own(v.r, key);
            if (a.is_obj() && in.o(a).kind == OK::Array)
                for (const Value& p : in.o(a).elems) out.push_back(in.to_vec(p));
            return out;
        };
        const auto vs = pts("vertices"), is = pts("inTangents"), os = pts("outTangents");
        const bool closed = in.own(v.r, "closed").nullish() || in.truthy(in.own(v.r, "closed"));
        // Camada de texto: o (0,0) do AE é a origem do texto; no Aurea, o centro da caixa.
        const f32 ox = l->kind == LayerKind::Text ? l->transform.anchor.x : 0.0f;
        const f32 oy = l->kind == LayerKind::Text ? l->transform.anchor.y : 0.0f;
        std::vector<MaskPoint> points;
        for (usize i = 0; i < vs.size(); ++i) {
            if (vs[i].size() < 2) continue;
            MaskPoint p;
            p.position = Vec2{static_cast<f32>(vs[i][0]) + ox, static_cast<f32>(vs[i][1]) + oy};
            if (i < is.size() && is[i].size() >= 2) p.inTangent = Vec2{static_cast<f32>(is[i][0]), static_cast<f32>(is[i][1])};
            if (i < os.size() && os[i].size() >= 2) p.outTangent = Vec2{static_cast<f32>(os[i][0]), static_cast<f32>(os[i][1])};
            points.push_back(p);
        }
        m->closed = closed;
        if (std::isnan(timeOrNan)) {
            m->points = points;
        } else {
            MaskPathKey k;
            k.frame = local_frame(l, timeOrNan);
            k.interp = 1;
            k.points = points;
            auto it = std::find_if(m->pathKeys.begin(), m->pathKeys.end(), [&](const MaskPathKey& x) { return x.frame == k.frame; });
            if (it != m->pathKeys.end()) *it = k; else m->pathKeys.push_back(k);
            std::sort(m->pathKeys.begin(), m->pathKeys.end(), [](const MaskPathKey& a, const MaskPathKey& b) { return a.frame < b.frame; });
            m->points = m->pathKeys.front().points;
            ++keyCount;
        }
        if (l->kind == LayerKind::Text) in.approx("maskShape", "máscara na camada de texto: pontos medidos a partir do centro da caixa do Aurea");
        ++in.report().changes;
    }

    // =========================================================================
    // Dom: get / set / call / index / construct
    // =========================================================================
    Value get(Interp& in, u32 obj, const std::string& name, bool& handled) override {
        handled = true;
        Object& o = in.o(obj);
        if (o.kind == OK::BoundHost) { handled = false; return Value::undef(); }
        switch (o.hostKind) {
            case HBlocked: return Value::undef();
            case HDollar:
                if (name == "global") return in.global_object();
                if (name == "os") return in.str("Aurea");
                if (name == "version") return in.str("4.5.5");
                if (name == "level" || name == "buildDate") return Value::num(0);
                if (name == "engineName") return in.str("aurea");
                if (name == "fileName") { in.blocked("$.fileName"); return in.str(""); }
                return method(in, obj, name);
            case HApp:
                if (name == "project") return host(in, HProject, "Project");
                if (name == "version") return in.str("26.0");
                if (name == "buildName") return in.str("Aurea");
                if (name == "language") return Value::num(0);
                if (name == "isRenderEngine" || name == "isWatchFolder") return Value::boolean(false);
                if (name == "activeViewer") return Value::null();
                if (name == "settings" || name == "preferences") return blocked(in, "app." + name);
                return method(in, obj, name);
            case HProject:
                if (name == "activeItem") return comp_obj(in);
                if (name == "numItems") return Value::num(1);
                if (name == "items") return host(in, HItems, "ItemCollection");
                if (name == "selection") return in.array({comp_obj(in)});
                if (name == "file" || name == "renderQueue") return blocked(in, "app.project." + name);
                if (name == "rootFolder") return Value::null();
                return method(in, obj, name);
            case HItems:
                if (name == "length") return Value::num(1);
                return method(in, obj, name);
            case HComp: return comp_get(in, obj, name);
            case HLayers: {
                Composition* c = comp();
                if (name == "length") return Value::num(c ? c->order().size() : 0);
                return method(in, obj, name);
            }
            case HLayer: return layer_get(in, obj, name);
            case HProp: return prop_get(in, obj, name);
            case HTextDoc: {
                bool f = false;
                const Value v = in.own(obj, name, &f);
                if (f) return v;
                if (name == "resetCharStyle" || name == "resetParagraphStyle") return method(in, obj, name);
                return Value::undef();
            }
            default: handled = false; return Value::undef();
        }
    }

    Value comp_get(Interp& in, u32 obj, const std::string& name) {
        Composition* c = comp();
        if (!c) return Value::undef();
        if (name == "width") return Value::num(c->width());
        if (name == "height") return Value::num(c->height());
        if (name == "duration") return Value::num(static_cast<f64>(c->duration().value) / fps_);
        if (name == "frameRate") return Value::num(fps_);
        if (name == "frameDuration") return Value::num(1.0 / fps_);
        if (name == "time") return Value::num(time_);
        if (name == "name") return in.str(c->name());
        if (name == "numLayers") return Value::num(c->order().size());
        if (name == "layers") return host(in, HLayers, "LayerCollection");
        if (name == "pixelAspect") return Value::num(1);
        if (name == "displayStartTime" || name == "workAreaStart") return Value::num(0);
        if (name == "workAreaDuration") return Value::num(static_cast<f64>(c->duration().value) / fps_);
        if (name == "typeName") return in.str("Composition");
        if (name == "id") return Value::num(1);
        if (name == "selectedLayers") {
            std::vector<Value> v;
            if (target_.valid() && layer(target_)) v.push_back(layer_obj(in, target_));
            return in.array(v);
        }
        if (name == "selectedProperties") return in.array({});
        if (name == "bgColor") {
            const Color bg = c->background();
            return vec_value(in, {bg.r, bg.g, bg.b});
        }
        if (name == "activeCamera") {
            const LayerId cam = c->active_camera();
            return cam.valid() ? layer_obj(in, cam) : Value::null();
        }
        if (name == "motionBlur") return Value::boolean(c->motion_blur().enabled);
        return method(in, obj, name);
    }

    Value layer_get(Interp& in, u32 obj, const std::string& name) {
        const LayerId lid = LayerId::unpack(in.o(obj).h0);
        Layer* l = layer(lid);
        if (!l) return in.throw_error("a camada não existe mais");
        if (name == "name") return in.str(l->name);
        if (name == "index") return Value::num(static_cast<f64>(ae_index(lid)));
        if (name == "enabled") return Value::boolean(l->visible);
        if (name == "locked") return Value::boolean(l->locked);
        if (name == "solo") return Value::boolean(l->solo);
        if (name == "shy") return Value::boolean(false);
        if (name == "selected") return Value::boolean(lid == target_);
        if (name == "startTime") return Value::num(static_cast<f64>(l->start.value - l->offset.value) / fps_);
        if (name == "inPoint") return Value::num(static_cast<f64>(l->start.value) / fps_);
        if (name == "outPoint") return Value::num(static_cast<f64>(l->end.value) / fps_);
        if (name == "stretch") return Value::num(l->speed != 0 ? 100.0 / l->speed : 100.0);
        if (name == "threeDLayer") return Value::boolean(l->threeD);
        if (name == "motionBlur") return Value::boolean(l->motionBlur);
        if (name == "adjustmentLayer") return Value::boolean(l->adjustment);
        if (name == "nullLayer") return Value::boolean(l->kind == LayerKind::Null);
        if (name == "guideLayer") return Value::boolean(l->guide);
        if (name == "hasVideo") return Value::boolean(l->kind != LayerKind::Audio && l->kind != LayerKind::Null);
        if (name == "hasAudio") return Value::boolean(l->kind == LayerKind::Audio || l->kind == LayerKind::Video);
        if (name == "isNameSet") return Value::boolean(true);
        if (name == "containingComp") return comp_obj(in);
        if (name == "matchName") {
            return in.str(l->kind == LayerKind::Text ? "ADBE Text Layer" : l->kind == LayerKind::Camera ? "ADBE Camera Layer"
                          : l->kind == LayerKind::Light ? "ADBE Light Layer" : "ADBE AV Layer");
        }
        if (name == "parent") return l->parent.valid() ? layer_obj(in, l->parent) : Value::null();
        if (name == "time") return Value::num(time_ - static_cast<f64>(l->start.value - l->offset.value) / fps_);
        if (name == "width" || name == "height") {
            f64 w = 1, h = 1;
            Composition* c = comp();
            layer_size(*l, c ? c->width() : 1920, c ? c->height() : 1080, w, h);
            return Value::num(name == "width" ? w : h);
        }
        if (name == "numProperties") return Value::num(std::size(kLayerChildren));
        if (name == "source") return Value::null();
        if (name == "comment") return in.str("");
        if (name == "label") return Value::num(l->label);
        if (name == "blendingMode") return Value::num(5212 + static_cast<f64>(blend_index(l->blendMode)));
        // Grupos como atributo: layer.transform, layer.Effects, layer.text...
        if (const Child* c = find_child(kLayerChildren, name)) {
            if (c->g == G::TextProps && l->kind != LayerKind::Text) return Value::undef();
            return child(in, lid, -1, in.str(c->match));
        }
        if (const Child* c = find_child(kTransformChildren, name)) return child(in, lid, -1, in.str(c->match));
        if (name == "sourceText" && l->kind == LayerKind::Text) return child(in, lid, -1, in.str("ADBE Text Document"));
        return method(in, obj, name);
    }

    static u32 blend_index(BlendMode m) noexcept {
        switch (m) {
            case BlendMode::Add: return 1; case BlendMode::Subtract: return 2; case BlendMode::Multiply: return 3;
            case BlendMode::Screen: return 4; case BlendMode::Overlay: return 5; case BlendMode::Darken: return 6;
            case BlendMode::Lighten: return 7; case BlendMode::ColorDodge: return 8; case BlendMode::ColorBurn: return 9;
            case BlendMode::HardLight: return 10; case BlendMode::SoftLight: return 11; case BlendMode::Difference: return 12;
            case BlendMode::Exclusion: return 13; case BlendMode::Hue: return 14; case BlendMode::Saturation: return 15;
            case BlendMode::Color: return 16; case BlendMode::Luminosity: return 17; case BlendMode::Divide: return 18;
            case BlendMode::VividLight: return 19; case BlendMode::LinearDodge: return 20; case BlendMode::LinearBurn: return 21;
            default: return 0;
        }
    }
    static BlendMode blend_from(i64 v) noexcept {
        static const BlendMode m[] = {BlendMode::Normal, BlendMode::Add, BlendMode::Subtract, BlendMode::Multiply, BlendMode::Screen,
                                      BlendMode::Overlay, BlendMode::Darken, BlendMode::Lighten, BlendMode::ColorDodge,
                                      BlendMode::ColorBurn, BlendMode::HardLight, BlendMode::SoftLight, BlendMode::Difference,
                                      BlendMode::Exclusion, BlendMode::Hue, BlendMode::Saturation, BlendMode::Color,
                                      BlendMode::Luminosity, BlendMode::Divide, BlendMode::VividLight, BlendMode::LinearDodge,
                                      BlendMode::LinearBurn};
        const i64 i = v - 5212;
        return i >= 0 && i < static_cast<i64>(std::size(m)) ? m[i] : BlendMode::Normal;
    }

    Value prop_get(Interp& in, u32 obj, const std::string& name) {
        const u32 ri = static_cast<u32>(in.o(obj).h0);
        if (ri >= refs_.size()) return Value::undef();
        PRef& r = refs_[ri];
        if (name == "name") return in.str(r.name.empty() ? r.match : r.name);
        if (name == "matchName") return in.str(r.match);
        if (name == "propertyIndex") return Value::num(r.index);
        if (name == "propertyDepth") { u32 d = 1; for (i32 p = r.parent; p >= 0; p = refs_[static_cast<usize>(p)].parent) ++d; return Value::num(d); }
        if (name == "parentProperty") return r.parent >= 0 ? prop_obj(in, static_cast<u32>(r.parent)) : layer_obj(in, r.layer);
        if (name == "propertyType") return Value::num(r.g == G::Leaf ? 6212 : (r.g == G::Effects || r.g == G::Animators || r.g == G::Selectors || r.g == G::Masks) ? 6213 : 6214);
        if (name == "isEffect") return Value::boolean(r.g == G::Effect);
        if (name == "isMask") return Value::boolean(r.g == G::Mask);
        if (name == "active" || name == "elided") return Value::boolean(name == "active");
        if (name == "canSetEnabled") return Value::boolean(r.g == G::Effect || r.g == G::Animator);
        if (name == "enabled") {
            Layer* l = layer(r.layer);
            if (l && r.g == G::Effect) if (EffectInstance* fx = l->find_effect(EffectId{r.effectId, 0})) return Value::boolean(fx->enabled);
            if (l && r.g == G::Animator && r.animator < l->text.animators.size()) return Value::boolean(l->text.animators[r.animator].enabled);
            return Value::boolean(true);
        }
        if (r.g != G::Leaf) {
            if (name == "numProperties") return Value::num(num_properties(r));
            if (r.g == G::Mask) {
                Layer* l = layer(r.layer);
                const Mask* m = nullptr;
                if (l) for (const Mask& x : l->masks) if (x.id == r.maskId) m = &x;
                if (name == "maskMode" && m) {
                    const MaskOperation op = m->operation;
                    return Value::num(op == MaskOperation::None ? 6812 : op == MaskOperation::Subtract ? 6814
                                    : op == MaskOperation::Intersect ? 6815 : op == MaskOperation::Difference ? 6818 : 6813);
                }
                if (name == "inverted" && m) return Value::boolean(m->inverted);
            }
            // filho como atributo (group.position, group.Selectors...)
            if (name != "property" && name != "addProperty" && name != "canAddProperty" && name != "remove" &&
                name != "moveTo" && name != "duplicate") {
                const Value c = child(in, r.layer, static_cast<i32>(ri), in.str(name));
                if (!c.nullish()) return c;
            }
            return method(in, obj, name);
        }
        // folha
        if (name == "value") {
            if (r.leaf == LK::SourceText) {
                Layer* l = layer(r.layer);
                return l ? text_document_from(in, *l) : Value::null();
            }
            return vec_value(in, current(in, r, time_));
        }
        if (name == "numKeys") return Value::num(static_cast<f64>(r.keys.size()));
        if (name == "expression") return in.str(r.expression);
        if (name == "expressionEnabled") return Value::boolean(r.exprEnabled);
        if (name == "expressionError") return in.str("");
        if (name == "canSetExpression") return Value::boolean(r.leaf != LK::SourceText && r.leaf != LK::MaskShape);
        if (name == "canVaryOverTime") return Value::boolean(true);
        if (name == "isTimeVarying") return Value::boolean(r.keys.size() > 1 || !r.expression.empty());
        if (name == "isSpatial") return Value::boolean(r.match == "ADBE Position" || r.match == "ADBE Anchor Point");
        if (name == "dimensionsSeparated") return Value::boolean(false);
        if (name == "isSeparationFollower" || name == "isSeparationLeader") return Value::boolean(name == "isSeparationLeader" && r.match == "ADBE Position");
        if (name == "hasMin" || name == "hasMax") return Value::boolean(false);
        if (name == "unitsText") return in.str("");
        if (name == "propertyValueType") {
            const Binding b = bind(r);
            const u32 d = dims_of(r, b);
            if (r.leaf == LK::SourceText) return Value::num(6424);
            if (r.leaf == LK::MaskShape) return Value::num(6423);
            return Value::num(d == 4 ? 6418 : d == 3 ? (b.spatial ? 6413 : 6414) : d == 2 ? (b.spatial ? 6415 : 6416) : 6417);
        }
        return method(in, obj, name);
    }

    u32 num_properties(const PRef& r) {
        Layer* l = layer(r.layer);
        if (!l) return 0;
        switch (r.g) {
            case G::Transform: return static_cast<u32>(std::size(kTransformChildren));
            case G::TextProps: return static_cast<u32>(std::size(kTextChildren));
            case G::PathOptions: return static_cast<u32>(std::size(kPathChildren));
            case G::More: return static_cast<u32>(std::size(kMoreChildren));
            case G::Animators: return static_cast<u32>(l->text.animators.size());
            case G::Animator: return 2;
            case G::Selectors: { u32 n = 0; for (const SelEntry& s : sels_) if (s.animator == r.animator) ++n; return n; }
            case G::Selector: return r.selKind == 0 ? static_cast<u32>(std::size(kRangeChildren)) : r.selKind == 1 ? static_cast<u32>(std::size(kWigglyChildren)) : 2;
            case G::Advanced: return static_cast<u32>(std::size(kAdvancedChildren));
            case G::AnimProps: return static_cast<u32>(std::size(kAnimProps));
            case G::Effects: return static_cast<u32>(effects_of(r.layer).size());
            case G::Effect: return r.rule ? r.rule->paramCount : 0;
            case G::Masks: return static_cast<u32>(l->masks.size());
            case G::Mask: return static_cast<u32>(std::size(kMaskChildren));
            default: return 0;
        }
    }

    bool set(Interp& in, u32 obj, const std::string& name, const Value& v) override {
        Object& o = in.o(obj);
        switch (o.hostKind) {
            case HBlocked: in.blocked(o.method + "." + name + " ="); return true;
            case HComp: {
                Composition* c = comp();
                if (!c) return true;
                if (name == "time") { time_ = clamp_time(in.to_num(v)); return true; }
                if (name == "name") { c->set_name(in.to_str(v)); ++in.report().changes; return true; }
                if (name == "duration") {
                    const i64 d = static_cast<i64>(std::llround(clamp_time(in.to_num(v)) * fps_));
                    if (d > 0) { c->set_duration(FrameIndex{d}); ++in.report().changes; }
                    return true;
                }
                if (name == "bgColor") {
                    const std::vector<f64> col = in.to_vec(v);
                    if (col.size() >= 3) {
                        c->set_background(Color{static_cast<f32>(col[0]), static_cast<f32>(col[1]), static_cast<f32>(col[2]), 1.0f});
                        ++in.report().changes;
                    }
                    return true;
                }
                if (name == "motionBlur") { c->motion_blur().enabled = in.truthy(v); ++in.report().changes; return true; }
                in.unsupported("comp." + name + " =", "ajuste da composição sem equivalente (tamanho e fps não mudam por script)");
                return true;
            }
            case HLayer: return layer_set(in, obj, name, v);
            case HProp: return prop_set(in, obj, name, v);
            case HTextDoc: in.own_set(obj, name, v); return true;
            case HApp: case HProject: case HDollar: case HItems: case HLayers:
                in.unsupported(name + " =", "ajuste do aplicativo/projeto não existe no Aurea");
                return true;
            default: return false;
        }
    }

    bool layer_set(Interp& in, u32 obj, const std::string& name, const Value& v) {
        const LayerId lid = LayerId::unpack(in.o(obj).h0);
        Layer* l = layer(lid);
        if (!l) { in.throw_error("a camada não existe mais"); return true; }
        auto frames = [&](const Value& x) { return static_cast<i64>(std::llround(clamp_time(in.to_num(x)) * fps_)); };
        ++in.report().changes;
        if (name == "name") { l->name = in.to_str(v); return true; }
        if (name == "enabled") { l->visible = in.truthy(v); return true; }
        if (name == "locked") { l->locked = in.truthy(v); return true; }
        if (name == "solo") { l->solo = in.truthy(v); return true; }
        if (name == "threeDLayer") { l->threeD = in.truthy(v); return true; }
        if (name == "motionBlur") { l->motionBlur = in.truthy(v); return true; }
        if (name == "adjustmentLayer") { l->adjustment = in.truthy(v); return true; }
        if (name == "guideLayer") { l->guide = in.truthy(v); return true; }
        if (name == "label") { l->label = static_cast<u8>(std::clamp(in.to_num(v), 0.0, 12.0)); return true; }
        if (name == "blendingMode") { l->blendMode = blend_from(std::llround(in.to_num(v))); return true; }
        if (name == "startTime") {
            const i64 delta = frames(v) - (l->start.value - l->offset.value);
            l->start = FrameIndex{l->start.value + delta};
            l->end = FrameIndex{l->end.value + delta};
            return true;
        }
        if (name == "inPoint") {
            const i64 s = std::min(frames(v), l->end.value - 1);
            l->offset = FrameIndex{l->offset.value + (s - l->start.value)};
            l->start = FrameIndex{s};
            return true;
        }
        if (name == "outPoint") {
            l->end = FrameIndex{std::max(frames(v), l->start.value + 1)};
            if (Composition* c = comp(); c && l->end.value > c->duration().value) c->set_duration(l->end);
            return true;
        }
        if (name == "parent") {
            LayerId p{};
            if (v.is_obj() && in.o(v).hostKind == HLayer) p = LayerId::unpack(in.o(v).h0);
            if (!host_.set_parent(lid, p, true)) in.unsupported("layer.parent =", "o Aurea recusou o vínculo");
            return true;
        }
        if (name == "selected" || name == "shy" || name == "comment" || name == "quality" || name == "autoOrient") return true;
        if (name == "stretch") { in.unsupported("layer.stretch =", "esticar o tempo da camada por script não é suportado"); return true; }
        if (name == "trackMatteType") { in.unsupported("layer.trackMatteType =", "use o recorte (track matte) do Aurea"); return true; }
        in.unsupported("layer." + name + " =", "atributo da camada sem equivalente no Aurea");
        return true;
    }

    bool prop_set(Interp& in, u32 obj, const std::string& name, const Value& v) {
        const u32 ri = static_cast<u32>(in.o(obj).h0);
        if (ri >= refs_.size()) return true;
        PRef& r = refs_[ri];
        Layer* l = layer(r.layer);
        if (name == "expression") {
            r.expression = in.to_str(v);
            r.exprSet = true;
            r.line = in.line();
            ++in.report().changes;
            return true;
        }
        if (name == "expressionEnabled") { r.exprEnabled = in.truthy(v); return true; }
        if (name == "enabled") {
            if (l && r.g == G::Effect) if (EffectInstance* fx = l->find_effect(EffectId{r.effectId, 0})) fx->enabled = in.truthy(v);
            if (l && r.g == G::Animator && r.animator < l->text.animators.size()) l->text.animators[r.animator].enabled = in.truthy(v);
            ++in.report().changes;
            return true;
        }
        if (name == "name") {
            const std::string nm = in.to_str(v);
            if (l && r.g == G::Animator && r.animator < l->text.animators.size()) l->text.animators[r.animator].name = nm;
            if (l && r.g == G::Mask) for (Mask& m : l->masks) if (m.id == r.maskId) m.name = nm;
            if (r.g == G::Effect)
                for (auto& e : effects_) if (e.first == r.layer && e.second.id == r.effectId) e.second.name = nm;
            r.name = nm;
            return true;
        }
        if (r.g == G::Mask && l) {
            for (Mask& m : l->masks) {
                if (m.id != r.maskId) continue;
                if (name == "maskMode") {
                    const i64 x = std::llround(in.to_num(v));
                    m.operation = x == 6812 ? MaskOperation::None : x == 6814 ? MaskOperation::Subtract : x == 6815 ? MaskOperation::Intersect
                                : x == 6818 ? MaskOperation::Difference : MaskOperation::Add;
                    if (x == 6816 || x == 6817) in.approx("maskMode", "clarear/escurecer viraram adicionar");
                } else if (name == "inverted") m.inverted = in.truthy(v);
            }
            ++in.report().changes;
            return true;
        }
        if (name == "dimensionsSeparated") {
            if (in.truthy(v)) in.approx("dimensionsSeparated", "o Aurea já guarda X, Y e Z separados");
            return true;
        }
        in.unsupported("property." + name + " =", "atributo da propriedade sem equivalente no Aurea");
        return true;
    }

    Value index(Interp& in, u32 obj, const Value& key, bool& handled) override {
        handled = true;
        Object& o = in.o(obj);
        if (o.hostKind == HLayers && key.is_num()) {
            const LayerId id = layer_at(std::llround(key.n));
            return id.valid() ? layer_obj(in, id) : Value::undef();
        }
        if (o.hostKind == HProp && key.is_num()) {
            PRef& r = refs_[static_cast<usize>(o.h0)];
            if (r.g != G::Leaf) return child(in, r.layer, static_cast<i32>(o.h0), key);
        }
        handled = false;
        return Value::undef();
    }

    Value construct(Interp& in, const std::string& cls, const std::vector<Value>& a, bool& handled) override {
        handled = true;
        if (cls == "KeyframeEase") {
            const Value e = in.plain("KeyframeEase");
            in.own_set(e.r, "speed", Value::num(a.empty() ? 0.0 : in.to_num(a[0])));
            in.own_set(e.r, "influence", Value::num(a.size() > 1 ? in.to_num(a[1]) : 100.0 / 6.0));
            return e;
        }
        if (cls == "TextDocument") return new_text_document(in, a.empty() ? std::string() : in.to_str(a[0]));
        if (cls == "Shape") {
            const Value s = in.plain("Shape");
            in.own_set(s.r, "vertices", in.array({}));
            in.own_set(s.r, "inTangents", in.array({}));
            in.own_set(s.r, "outTangents", in.array({}));
            in.own_set(s.r, "closed", Value::boolean(true));
            return s;
        }
        if (cls == "MarkerValue") {
            in.unsupported("new MarkerValue()", "marcadores não entram pelo script");
            const Value m = in.plain("MarkerValue");
            in.own_set(m.r, "comment", a.empty() ? in.str("") : a[0]);
            return m;
        }
        in.unsupported("new " + cls + "()", "classe do After Effects que o script não pode criar");
        return Value::null();
    }

    // --- chamadas -------------------------------------------------------------------
    Value call(Interp& in, u32 obj, const std::string& m, const std::vector<Value>& a, bool& handled) override {
        handled = true;
        Object& o = in.o(obj);
        if (o.kind == OK::Native) {   // classe do DOM chamada como função
            return construct(in, o.cls, a, handled);
        }
        switch (o.hostKind) {
            case HBlocked: {
                in.blocked(o.method + (m == "()" ? std::string("()") : "." + m + "()"));
                return blocked(in, o.method + "." + m);
            }
            case HDollar:
                if (m == "writeln" || m == "write" || m == "sleep" || m == "gc" || m == "bp" || m == "clearerr") return Value::undef();
                if (m == "evalFile" || m == "include" || m == "fileName" || m == "setenv" || m == "getenv") {
                    in.blocked("$." + m + "()");
                    return Value::undef();
                }
                in.unsupported("$." + m + "()", "função do ExtendScript sem equivalente no sandbox");
                return Value::undef();
            case HApp:
                if (m == "beginUndoGroup" || m == "endUndoGroup" || m == "beginSuppressDialogs" || m == "endSuppressDialogs" ||
                    m == "cancelTask" || m == "activate")
                    return Value::undef();
                if (m == "findMenuCommandId") return Value::num(0);
                if (m == "executeCommand" || m == "open" || m == "newProject" || m == "quit" || m == "purge" || m == "scheduleTask" ||
                    m == "setMemoryUsageLimits" || m == "watchFolder" || m == "openFast" || m == "parseSwatchFile") {
                    in.blocked("app." + m + "()");
                    return Value::undef();
                }
                in.unsupported("app." + m + "()", "função do aplicativo sem equivalente no Aurea");
                return Value::undef();
            case HProject:
                if (m == "item") {
                    const i64 i = a.empty() ? 0 : std::llround(in.to_num(a[0]));
                    return i == 1 ? comp_obj(in) : Value::null();
                }
                if (m == "save" || m == "saveAs" || m == "importFile" || m == "importFileWithDialog" || m == "close" ||
                    m == "autoFixExpressions" || m == "reduceProject" || m == "consolidateFootage" || m == "removeUnusedFootage") {
                    in.blocked("app.project." + m + "()");
                    return Value::undef();
                }
                in.unsupported("app.project." + m + "()", "função do projeto sem equivalente no Aurea");
                return Value::undef();
            case HItems:
                if (m == "addComp") {
                    in.unsupported("items.addComp()", "o script trabalha na composição aberta (ativa); composição nova não é criada");
                    return comp_obj(in);
                }
                if (m == "addFolder") { in.unsupported("items.addFolder()", "pastas do projeto não existem no Aurea"); return Value::null(); }
                in.unsupported("items." + m + "()", "sem equivalente no Aurea");
                return Value::undef();
            case HComp: return comp_call(in, m, a);
            case HLayers: return layers_call(in, m, a);
            case HLayer: return layer_call(in, obj, m, a);
            case HProp: return prop_call(in, obj, m, a);
            case HTextDoc:
                if (m == "resetCharStyle" || m == "resetParagraphStyle") return Value::undef();
                handled = false;
                return Value::undef();
            default: handled = false; return Value::undef();
        }
    }

    Value comp_call(Interp& in, const std::string& m, const std::vector<Value>& a) {
        if (m == "layer") {
            if (a.empty()) return Value::null();
            if (a[0].is_num()) { const LayerId id = layer_at(std::llround(a[0].n)); return id.valid() ? layer_obj(in, id) : Value::null(); }
            const LayerId id = layer_named(in.to_str(a[0]));
            return id.valid() ? layer_obj(in, id) : Value::null();
        }
        if (m == "openInViewer" || m == "setActive") return Value::undef();
        if (m == "duplicate" || m == "saveFrameToPng" || m == "exportAsMotionGraphicsTemplate" || m == "openInEssentialGraphics") {
            in.blocked("comp." + m + "()");
            return Value::undef();
        }
        in.unsupported("comp." + m + "()", "função da composição sem equivalente no Aurea");
        return Value::undef();
    }

    Value layers_call(Interp& in, const std::string& m, const std::vector<Value>& a) {
        NewLayerSpec s;
        auto dur = [&](usize i) { if (a.size() > i && a[i].is_num()) s.durationSec = in.to_num(a[i]); };
        if (m == "addText" || m == "addBoxText") {
            const usize ti = m == "addBoxText" ? 1 : 0;
            Value textArg = a.size() > ti ? a[ti] : Value::undef();
            if (textArg.is_obj() && in.o(textArg).hostKind == HTextDoc) s.text = in.to_str(in.own(textArg.r, "text"));
            else if (!textArg.nullish()) s.text = in.to_str(textArg);
            const Value lv = create_layer(in, NewLayer::Text, s);
            if (lv.is_obj()) {
                const LayerId id = LayerId::unpack(in.o(lv).h0);
                if (textArg.is_obj() && in.o(textArg).hostKind == HTextDoc) apply_text_document(in, id, textArg);
                if (m == "addBoxText" && !a.empty()) {
                    const std::vector<f64> bs = in.to_vec(a[0]);
                    if (Layer* l = layer(id); l && bs.size() >= 2) {
                        l->text.boxMode = 1;
                        l->text.autoSize = false;
                        l->text.box = Rect{0, 0, static_cast<f32>(bs[0]), static_cast<f32>(bs[1])};
                        host_.text_changed(id);
                    }
                }
            }
            return lv;
        }
        if (m == "addSolid") {
            const std::vector<f64> c = a.empty() ? std::vector<f64>{} : in.to_vec(a[0]);
            for (usize i = 0; i < 3 && i < c.size(); ++i) s.color[i] = static_cast<f32>(std::clamp(c[i], 0.0, 1.0));
            if (a.size() > 1) s.name = in.to_str(a[1]);
            if (a.size() > 2) s.width = static_cast<u32>(std::clamp(in.to_num(a[2]), 1.0, 16384.0));
            if (a.size() > 3) s.height = static_cast<u32>(std::clamp(in.to_num(a[3]), 1.0, 16384.0));
            dur(5);
            return create_layer(in, NewLayer::Solid, s);
        }
        if (m == "addShape") {
            in.approx("addShape()", "o conteúdo de forma do AE (Contents) não é importado: a camada nasce vazia");
            return create_layer(in, NewLayer::Shape, s);
        }
        if (m == "addNull") { dur(0); return create_layer(in, NewLayer::Null, s); }
        if (m == "addCamera") {
            if (!a.empty()) s.name = in.to_str(a[0]);
            return create_layer(in, NewLayer::Camera, s);
        }
        if (m == "addLight") {
            if (!a.empty()) s.name = in.to_str(a[0]);
            return create_layer(in, NewLayer::Light, s);
        }
        if (m == "byName") {
            const LayerId id = a.empty() ? LayerId{} : layer_named(in.to_str(a[0]));
            return id.valid() ? layer_obj(in, id) : Value::null();
        }
        if (m == "add") { in.unsupported("layers.add()", "camada de mídia do projeto do AE: importe a mídia no Aurea"); return Value::null(); }
        if (m == "precompose") { in.unsupported("layers.precompose()", "pré-compor por script não é suportado"); return Value::null(); }
        in.unsupported("layers." + m + "()", "sem equivalente no Aurea");
        return Value::undef();
    }

    Value layer_call(Interp& in, u32 obj, const std::string& m, const std::vector<Value>& a) {
        const LayerId lid = LayerId::unpack(in.o(obj).h0);
        Layer* l = layer(lid);
        if (!l) return in.throw_error("a camada não existe mais");
        if (m == "property") return a.empty() ? Value::null() : child(in, lid, -1, a[0]);
        if (m == "remove") { if (host_.remove_layer(lid)) ++in.report().changes; return Value::undef(); }
        if (m == "setParentWithJump") {
            LayerId p{};
            if (!a.empty() && a[0].is_obj() && in.o(a[0]).hostKind == HLayer) p = LayerId::unpack(in.o(a[0]).h0);
            (void)host_.set_parent(lid, p, false);
            return Value::undef();
        }
        Composition* c = comp();
        auto move_to = [&](u32 z) {
            if (c && c->reorder_layer(lid, z)) { c->rebuild_draw_order(); ++in.report().changes; }
        };
        if (m == "moveToBeginning") { if (c) move_to(c->order().size() - 1); return Value::undef(); }
        if (m == "moveToEnd") { move_to(0); return Value::undef(); }
        if ((m == "moveBefore" || m == "moveAfter") && !a.empty() && a[0].is_obj() && in.o(a[0]).hostKind == HLayer && c) {
            const i32 z = c->z_index_of(LayerId::unpack(in.o(a[0]).h0));
            if (z >= 0) move_to(static_cast<u32>(m == "moveBefore" ? z : std::max(0, z - 1)));
            return Value::undef();
        }
        if (m == "activeAtTime") {
            const i64 f = static_cast<i64>(std::llround(clamp_time(a.empty() ? 0 : in.to_num(a[0])) * fps_));
            return Value::boolean(l->visible && f >= l->start.value && f < l->end.value);
        }
        if (m == "applyPreset") { in.blocked("layer.applyPreset()"); return Value::undef(); }
        if (m == "duplicate" || m == "copyToComp" || m == "replaceSource" || m == "openInViewer" || m == "sourcePointToComp" ||
            m == "compPointToSource" || m == "addToMotionGraphicsTemplate" || m == "sourceRectAtTime") {
            if (m == "sourceRectAtTime") {
                f64 w = 1, h = 1;
                layer_size(*l, c ? c->width() : 1920, c ? c->height() : 1080, w, h);
                const Value r = in.plain();
                in.own_set(r.r, "top", Value::num(0)); in.own_set(r.r, "left", Value::num(0));
                in.own_set(r.r, "width", Value::num(w)); in.own_set(r.r, "height", Value::num(h));
                return r;
            }
            in.unsupported("layer." + m + "()", "função da camada sem equivalente no Aurea");
            return Value::undef();
        }
        in.unsupported("layer." + m + "()", "função da camada sem equivalente no Aurea");
        return Value::undef();
    }

    Value prop_call(Interp& in, u32 obj, const std::string& m, const std::vector<Value>& a) {
        const u32 ri = static_cast<u32>(in.o(obj).h0);
        if (ri >= refs_.size()) return Value::undef();
        if (refs_[ri].g != G::Leaf) {
            if (m == "property") return a.empty() ? Value::null() : child(in, refs_[ri].layer, static_cast<i32>(ri), a[0]);
            if (m == "addProperty") return a.empty() ? Value::null() : add_property(in, ri, in.to_str(a[0]));
            if (m == "canAddProperty") {
                const G g = refs_[ri].g;
                const std::string n = a.empty() ? std::string() : in.to_str(a[0]);
                if (g == G::Effects) { const ERule* r = find_effect_rule(n); if (!r) r = find_effect_rule_by_name(n); return Value::boolean(r && r->key); }
                return Value::boolean(g == G::Animators || g == G::Selectors || g == G::AnimProps || g == G::Masks);
            }
            if (m == "remove") {
                PRef& r = refs_[ri];
                Layer* l = layer(r.layer);
                if (l && r.g == G::Effect) {
                    const u32 idx = l->effect_index(EffectId{r.effectId, 0});
                    if (idx != kInvalidIndex) {
                        l->effects.erase(l->effects.begin() + idx);
                        for (u32 t = 0; t < l->tracks.size(); ++t) {
                            Track& tr = l->tracks.at(t);
                            if (tr.property == TrackProperty::EffectParam && tr.effectIndex == r.effectId) tr.clear();
                        }
                        effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [&](const auto& e) {
                            return e.first == r.layer && e.second.id == r.effectId; }), effects_.end());
                        --in.report().effects;
                        ++in.report().changes;
                    }
                    return Value::undef();
                }
                in.unsupported(r.match + ".remove()", "remover este grupo por script não é suportado");
                return Value::undef();
            }
            if (m == "moveTo" || m == "duplicate") { in.unsupported(refs_[ri].match + "." + m + "()", "sem equivalente no Aurea"); return Value::undef(); }
            in.unsupported(refs_[ri].match + "." + m + "()", "função do grupo sem equivalente no Aurea");
            return Value::undef();
        }
        return leaf_call(in, ri, m, a);
    }

    Value leaf_call(Interp& in, u32 ri, const std::string& m, const std::vector<Value>& a) {
        PRef& r = refs_[ri];
        auto t_of = [&](usize i) { return clamp_time(i < a.size() ? in.to_num(a[i]) : time_); };
        if (r.leaf == LK::SourceText) {
            if (m == "setValue" && !a.empty()) { apply_text_document(in, r.layer, a[0]); return Value::undef(); }
            if (m == "setValueAtTime" && a.size() >= 2) {
                in.approx("sourceText.setValueAtTime", "o texto de origem não anima no Aurea: valeu o último texto");
                apply_text_document(in, r.layer, a[1]);
                return Value::undef();
            }
            if (m == "valueAtTime") { Layer* l = layer(r.layer); return l ? text_document_from(in, *l) : Value::null(); }
            in.unsupported("sourceText." + m + "()", "animação do texto de origem não existe no Aurea");
            return Value::undef();
        }
        if (r.leaf == LK::MaskShape) {
            if (m == "setValue" && !a.empty()) { apply_shape(in, r, a[0], NAN); return Value::undef(); }
            if (m == "setValueAtTime" && a.size() >= 2) { apply_shape(in, r, a[1], t_of(0)); return Value::undef(); }
            if (m == "setValuesAtTimes" && a.size() >= 2 && a[0].is_obj() && a[1].is_obj()) {
                const auto ts = in.o(a[0]).elems, vs = in.o(a[1]).elems;
                for (usize i = 0; i < ts.size() && i < vs.size(); ++i) apply_shape(in, r, vs[i], clamp_time(in.to_num(ts[i])));
                return Value::undef();
            }
            if (m == "numKeys") return Value::num(0);
            in.unsupported("maskShape." + m + "()", "sem equivalente para a forma da máscara");
            return Value::undef();
        }
        if (m == "setValue") {
            if (a.empty()) return Value::undef();
            if (r.keys.size() > 1) return in.throw_error("setValue numa propriedade com keyframes (use setValueAtTime)");
            set_value(in, ri, in.to_vec(a[0]));
            return Value::undef();
        }
        if (m == "setValueAtTime") {
            if (a.size() < 2) return Value::undef();
            upsert_key(r, t_of(0), in.to_vec(a[1]));
            flush_keys(in, ri);
            return Value::undef();
        }
        if (m == "setValuesAtTimes") {
            if (a.size() < 2 || !a[0].is_obj() || !a[1].is_obj()) return in.throw_error("setValuesAtTimes(tempos[], valores[])");
            const std::vector<Value> ts = in.o(a[0]).elems, vs = in.o(a[1]).elems;
            if (ts.size() != vs.size()) return in.throw_error("setValuesAtTimes: listas de tamanhos diferentes");
            for (usize i = 0; i < ts.size(); ++i) upsert_key(r, clamp_time(in.to_num(ts[i])), in.to_vec(vs[i]));
            flush_keys(in, ri);
            return Value::undef();
        }
        if (m == "addKey") {
            const f64 t = t_of(0);
            const std::vector<f64> v = current(in, r, t);
            upsert_key(r, t, v);
            flush_keys(in, ri);
            for (usize i = 0; i < r.keys.size(); ++i) if (std::fabs(r.keys[i].time - t) < 1e-6) return Value::num(static_cast<f64>(i + 1));
            return Value::num(static_cast<f64>(r.keys.size()));
        }
        if (m == "removeKey") {
            const i64 i = a.empty() ? 0 : std::llround(in.to_num(a[0]));
            if (i < 1 || i > static_cast<i64>(r.keys.size())) return in.throw_error("removeKey: índice inválido");
            r.keys.erase(r.keys.begin() + (i - 1));
            flush_keys(in, ri);
            return Value::undef();
        }
        if (m == "keyTime") { const Key* k = key_at(in, r, a.empty() ? Value::num(0) : a[0]); return k ? Value::num(k->time) : Value::undef(); }
        if (m == "keyValue") { const Key* k = key_at(in, r, a.empty() ? Value::num(0) : a[0]); return k ? vec_value(in, k->value) : Value::undef(); }
        if (m == "nearestKeyIndex") {
            if (r.keys.empty()) return in.throw_error("a propriedade não tem keyframes");
            const f64 t = t_of(0);
            usize best = 0;
            for (usize i = 1; i < r.keys.size(); ++i) if (std::fabs(r.keys[i].time - t) < std::fabs(r.keys[best].time - t)) best = i;
            return Value::num(static_cast<f64>(best + 1));
        }
        if (m == "keyInInterpolationType" || m == "keyOutInterpolationType") {
            const Key* k = key_at(in, r, a.empty() ? Value::num(0) : a[0]);
            return k ? Value::num(interp_to(m == "keyInInterpolationType" ? k->in : k->out)) : Value::undef();
        }
        if (m == "setInterpolationTypeAtKey") {
            Key* k = key_at(in, r, a.empty() ? Value::num(0) : a[0]);
            if (!k) return Value::undef();
            const KeyInterp inT = interp_from(a.size() > 1 ? in.to_num(a[1]) : kInterpLinear);
            k->in = inT;
            k->out = a.size() > 2 ? interp_from(in.to_num(a[2])) : inT;
            flush_keys(in, ri);
            return Value::undef();
        }
        if (m == "setTemporalEaseAtKey") {
            Key* k = key_at(in, r, a.empty() ? Value::num(0) : a[0]);
            if (!k) return Value::undef();
            const std::vector<Ease> ie = a.size() > 1 ? eases(in, a[1]) : std::vector<Ease>{};
            const std::vector<Ease> oe = a.size() > 2 ? eases(in, a[2]) : ie;
            if (!ie.empty()) { k->inEase = ie; if (k->in == KeyInterp::Linear) k->in = KeyInterp::Bezier; }
            if (!oe.empty()) { k->outEase = oe; if (k->out == KeyInterp::Linear) k->out = KeyInterp::Bezier; }
            flush_keys(in, ri);
            return Value::undef();
        }
        if (m == "keyInTemporalEase" || m == "keyOutTemporalEase") {
            const Key* k = key_at(in, r, a.empty() ? Value::num(0) : a[0]);
            if (!k) return Value::undef();
            const Binding b = bind(r);
            return ease_array(in, m == "keyInTemporalEase" ? k->inEase : k->outEase, b.spatial ? 1 : dims_of(r, b));
        }
        if (m == "valueAtTime") return vec_value(in, current(in, r, t_of(0)));
        if (m == "setTemporalContinuousAtKey" || m == "setTemporalAutoBezierAtKey" || m == "setSpatialContinuousAtKey" ||
            m == "setSpatialAutoBezierAtKey" || m == "setRovingAtKey" || m == "setSelectedAtKey" || m == "setLabelAtKey") {
            return Value::undef();
        }
        if (m == "setSpatialTangentsAtKey") {
            in.approx("setSpatialTangentsAtKey", "caminho curvo de movimento vira trajeto reto entre os keyframes");
            return Value::undef();
        }
        if (m == "keyTemporalContinuous" || m == "keyTemporalAutoBezier" || m == "keySpatialContinuous" || m == "keySpatialAutoBezier" ||
            m == "keyRoving" || m == "keySelected")
            return Value::boolean(false);
        if (m == "getSeparationFollower") {
            const i64 dim = a.empty() ? 0 : std::llround(in.to_num(a[0]));
            static const char* kSep[] = {"ADBE Position_0", "ADBE Position_1", "ADBE Position_2"};
            if (dim < 0 || dim > 2) return Value::null();
            return child(in, r.layer, -1, in.str(kSep[dim]));
        }
        if (m == "addToMotionGraphicsTemplate" || m == "addToMotionGraphicsTemplateAs") {
            in.unsupported("property." + m + "()", "modelos de gráficos animados não existem no Aurea");
            return Value::undef();
        }
        in.unsupported("property." + m + "()", "função da propriedade sem equivalente no Aurea");
        return Value::undef();
    }

    // =========================================================================
    // fim do script
    // =========================================================================
    void finish(Interp& in) override {
        // Apelidos dos efeitos criados (para effect("Nome") das expressões).
        std::vector<std::pair<LayerId, std::vector<EffectAlias>>> perLayer;
        for (const auto& e : effects_) {
            Layer* l = layer(e.first);
            if (!l || !e.second.rule) continue;
            const EffectTypeId type = effect_type_id(e.second.rule->key);
            u32 ordinal = 0;
            for (const EffectInstance& fx : l->effects) {
                if (fx.type == type) ++ordinal;
                if (fx.id == e.second.id) break;
            }
            const std::string ref = ordinal <= 1 ? std::string(e.second.rule->key) : std::string(e.second.rule->key) + " " + std::to_string(ordinal);
            auto it = std::find_if(perLayer.begin(), perLayer.end(), [&](const auto& p) { return p.first == e.first; });
            if (it == perLayer.end()) { perLayer.emplace_back(e.first, std::vector<EffectAlias>{}); it = perLayer.end() - 1; }
            it->second.push_back(EffectAlias{e.second.name, ref, e.second.rule});
            if (ordinal <= 1) {
                it->second.push_back(EffectAlias{e.second.rule->aeName, ref, e.second.rule});
                it->second.push_back(EffectAlias{e.second.rule->ae, ref, e.second.rule});
            }
        }
        for (u32 i = 0; i < refs_.size(); ++i) {
            PRef& r = refs_[i];
            if (r.g != G::Leaf) continue;
            if (r.keys.size() > 1) {
                const Binding b = bind(r);
                u32 keyed = 0;
                for (const Channel& c : b.ch) keyed += c.prop != TrackProperty::_Count && c.home != Home::Field && c.src != kConstSrc ? 1 : 0;
                keyCount += static_cast<u32>(r.keys.size()) * keyed;
            }
            if (!r.exprSet) continue;
            Layer* l = layer(r.layer);
            if (!l) continue;
            const Binding b = bind(r);
            if (!b.ok()) { report_once(in, r, b); continue; }
            WriteCtx w = wctx(in, l);
            w.line = r.line;
            if (r.expression.find_first_not_of(" \t\r\n") == std::string::npos) { clear_expression(w, b); continue; }
            auto it = std::find_if(perLayer.begin(), perLayer.end(), [&](const auto& p) { return p.first == r.layer; });
            const std::string src = it == perLayer.end() ? r.expression : rewrite_expression(r.expression, it->second);
            write_expression(w, b, src);
            if (!r.exprEnabled) set_expression_enabled(w, b, false);
            ++in.report().expressions;
        }
        in.report().keyframes += keyCount;
    }

private:
    Host& host_;
    LayerId target_;
    f64 fps_ = 30.0;
    f64 time_ = 0.0;
    std::vector<PRef> refs_;
    std::vector<std::pair<LayerId, EffectEntry>> effects_;
    std::vector<SelEntry> sels_;
    u32 compObj_ = 0;
    std::vector<std::pair<u64, u32>> layerObjs_;
};

} // namespace

bool run_jsx(std::string_view source, Host& host, LayerId target, const ScriptLimits& limits, ImportReport& report) {
    report.kind = "jsx";
    if (!host.composition()) {
        report.message = "Nenhuma composição aberta para o script.";
        return false;
    }
    AeDom dom(host, target);
    script::Interp in(host, report, limits);
    const bool ran = in.run(source, dom);
    report.ok = ran;
    if (ran && report.changes == 0 && report.message.empty()) report.message = "O script rodou, mas não mudou nada no projeto.";
    return ran;
}

} // namespace aurea::ae
