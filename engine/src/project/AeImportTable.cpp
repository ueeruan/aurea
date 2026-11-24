// =============================================================================
//  Aurea / project / AeImportTable.cpp
//
//  A TABELA: efeito do After Effects (matchName) → efeito do Aurea, parâmetro
//  a parâmetro. Números e nomes dos parâmetros do AE vêm dos presets
//  instalados (build/reference/ae2026/ae_effect_params.json, ordem da
//  interface); os ids do Aurea foram conferidos no registro dos efeitos
//  (`mapping_problems` acusa qualquer divergência — teste AeImport).
//
//  Unidades do lado AE = o valor gravado no .ffx/devolvido pelo scripting:
//  opacidade da Sombra projetada e limiar do Brilho em 0..255, níveis em
//  0..1, opacidade do Preencher em fração; pontos em px da camada (o .ffx
//  guarda fração do tamanho — o leitor converte); cores RGBA 0..1 sRGB.
//
//  Menus do AE contam o separador "(-" como item: os mapas abaixo seguem a
//  lista de `pdnm` com os separadores (−1 = opção sem equivalente → relatada).
//
//  Só entra efeito que o Aurea tem de verdade; os outros ficam na tabela com
//  `key` nulo e o motivo, para o relatório dizer o que é.
// =============================================================================
#include "AeImportBind.hpp"

#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace aurea::ae {
namespace detail {
namespace {

constexpr PRule P(u16 ae, const char* name, const char* id, PConv c = PConv::Same, f64 k = 1.0, f64 b = 0.0,
                  u8 comp = 0, u8 tcomp = 0xFF) {
    PRule r;
    r.ae = ae; r.aeName = name; r.id = id; r.conv = c; r.k = k; r.b = b; r.comp = comp; r.tcomp = tcomp;
    return r;
}
template <usize N>
constexpr PRule E(u16 ae, const char* name, const char* id, const i8 (&map)[N]) {
    PRule r;
    r.ae = ae; r.aeName = name; r.id = id; r.conv = PConv::Enum; r.map = map; r.mapN = static_cast<u8>(N);
    return r;
}
/// Sem equivalente: relatado quando o valor ≠ `def` ou animado.
constexpr PRule U(u16 ae, const char* name, f64 def) {
    PRule r;
    r.ae = ae; r.aeName = name; r.conv = PConv::None; r.def = def; r.hasDef = true;
    return r;
}
/// Sem equivalente e sem efeito visual aqui (qualidade, opção de interface).
constexpr PRule I(u16 ae, const char* name) {
    PRule r;
    r.ae = ae; r.aeName = name; r.conv = PConv::None; r.silent = true;
    return r;
}

// --- mapas de menu (índice = valor do AE, 1..) --------------------------------
constexpr i8 kMinus1[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9};      // valor − 1
constexpr i8 kFractalType[] = {-1, 0, -1, 1, 1, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr i8 kNoiseType[] = {-1, 0, 1, 2, 3};
constexpr i8 kOverflow[] = {-1, 0, 1, 2, 0};
// None, Normal, (-, Add, Multiply, Screen, Overlay, Soft Light, Hard Light → Normal|Mult|Tela|Sobrepor|Adicionar
constexpr i8 kGenBlend[] = {-1, 0, 0, -1, 4, 1, 2, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr i8 kGlowComposite[] = {-1, 0, 0, 1};                  // On Top, Behind, None
constexpr i8 kGlowOperation[] = {-1, -1, 0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr i8 kInvertAlpha[] = {-1, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1};
constexpr i8 kMinimaxOp[] = {-1, 1, 0, 2, 3};                    // Min → Erodir, Max → Dilatar
constexpr i8 kMinimaxChannel[] = {-1, 2, 3, -1, -1, -1, 1};
constexpr i8 kWavePin[] = {-1, 0, 1, 2, -1, 3, 4, 5, 6, -1, 7, 8};
constexpr i8 kTurbDisplace[] = {-1, 0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8};
constexpr i8 kTurbPinning[] = {-1, 0, -1, 1, 2, 3, 4, 5, 6, 7, -1, 8, 9, 10, 11, 12, 13, 14};
constexpr i8 kRadialFastZoom[] = {-1, 0, 1, 2};
constexpr i8 kColoramaInput[] = {-1, 0, 2, 3, 4, 1, -1, -1, -1, -1, -1};
// AE 2026
constexpr i8 kTwoChoice[] = {-1, 0, 1};
constexpr i8 kMinus1Long[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
constexpr i8 kGlowBasedOn[] = {-1, 1, 0};                        // Alpha Channel, Color Channels
constexpr i8 kGlowCompositeMode[] = {-1, 0, 1, 2};               // On Top, Behind, None
constexpr i8 kLevelsChannel[] = {-1, 0, 1, 2, 3, 4};
constexpr i8 kLevelsClip[] = {-1, 1, 0, 1};                       // On, Off, Off for 32 bpc
// RGB, R, G, B, (-, HLS, Hue, Lightness, Saturation, (-, YIQ, Luminance, I, Q, (-, Alpha
constexpr i8 kInvertChannel[] = {-1, 0, 1, 2, 3, -1, 4, 5, 6, 7, -1, 8, 9, 10, 11, -1, 12};
constexpr i8 kInvertColorAll[] = {-1, 1, 1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1, 1, 1, -1, 0};
// None, Normal, Stencil Alpha, (-, Add, Multiply, Screen, Overlay → Nenhum, Normal, Estêncil, Somar, Mult., Tela, Sobrepor
constexpr i8 kCircleBlend[] = {-1, 0, 1, 6, -1, 2, 3, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
// Uniform Random, Squared Random, (-, Uniform Animation, Squared Animation
constexpr i8 kNoiseAlphaType[] = {-1, 0, 1, -1, 2, 3};

// --- parâmetros por efeito (ordem da interface do AE) -------------------------
constexpr PRule kSlider[] = {P(1, "Slider", "slider")};
constexpr PRule kAngle[] = {P(1, "Angle", "angle")};
constexpr PRule kCheckbox[] = {P(1, "Checkbox", "checkbox", PConv::Bool)};
constexpr PRule kColor[] = {P(1, "Color", "color", PConv::Color)};
constexpr PRule kPoint[] = {P(1, "Point", "point", PConv::PointPx)};
// AE expressions (lote E): Dropdown Menu (o valor do AE é 1..; o Aurea guarda
// o índice 0.. e a expressão volta a ler 1..), Layer (a camada escolhida não
// vem no preset: fica para escolher aqui) e 3D Point Control.
constexpr PRule kDropdown[] = {E(1, "Menu", "menu", kMinus1Long)};
constexpr PRule kLayerControl[] = {U(1, "Layer", 0)};
constexpr PRule kPoint3D[] = {P(1, "3D Point", "point3d")};

constexpr PRule kGeometry2[] = {
    P(1, "Anchor Point", "anchor", PConv::PointRel),
    P(2, "Position", "position", PConv::PointRel),
    I(11, "Uniform Scale"),
    // Escala uniforme (o padrão do AE): "Scale Height" vale para os dois eixos.
    P(3, "Scale Height", "scale", PConv::Same, 1, 0, 0, 1),
    P(3, "Scale Height", "scale", PConv::Same, 1, 0, 0, 0),
    I(4, "Scale Width"),
    P(5, "Skew", "skew"),
    P(6, "Skew Axis", "skew_axis"),
    P(7, "Rotation", "rotation"),
    P(8, "Opacity", "opacity"),
    I(9, "Use Composition's Shutter Angle"),
    I(10, "Shutter Angle"),
    I(12, "Sampling"),
};

constexpr PRule kFractalNoise[] = {
    E(1, "Fractal Type", "fractal_type", kFractalType),
    E(2, "Noise Type", "noise_type", kNoiseType),
    P(3, "Invert", "invert", PConv::Bool),
    P(4, "Contrast", "contrast"),
    P(5, "Brightness", "brightness"),
    E(6, "Overflow", "overflow", kOverflow),
    P(8, "Rotation", "rotation"),
    I(9, "Uniform Scaling"),
    P(10, "Scale", "scale", PConv::Same, 2.0),
    U(11, "Scale Width", 100),
    U(12, "Scale Height", 100),
    P(13, "Offset Turbulence", "offset", PConv::PointCenter),
    I(31, "Perspective Offset"),
    P(15, "Complexity", "complexity"),
    P(17, "Sub Influence (%)", "sub_influence"),
    U(18, "Sub Scaling", 56),
    U(19, "Sub Rotation", 0),
    U(20, "Sub Offset", 0),
    I(21, "Center Subscale"),
    P(23, "Evolution", "evolution"),
    U(25, "Cycle Evolution", 0),
    U(26, "Cycle (in Revolutions)", 1),
    P(27, "Random Seed", "seed"),
    P(29, "Opacity", "opacity"),
    E(30, "Blending Mode", "blend", kGenBlend),
};

constexpr PRule kTritone[] = {
    P(1, "Highlights", "highlights", PConv::Color),
    P(2, "Midtones", "midtones", PConv::Color),
    P(3, "Shadows", "shadows", PConv::Color),
    P(4, "Blend With Original", "mix", PConv::Same, -1, 100),
};

constexpr PRule kLevels[] = {
    E(1, "Channel:", "channel", kLevelsChannel),
    I(2, "Histogram"),
    P(3, "Input Black", "input_black", PConv::Same, 255),
    P(4, "Input White", "input_white", PConv::Same, 255),
    P(5, "Gamma", "gamma"),
    P(6, "Output Black", "output_black", PConv::Same, 255),
    P(7, "Output White", "output_white", PConv::Same, 255),
    E(8, "Clip To Output Black", "clip_to_output_black", kLevelsClip),
    E(9, "Clip To Output White", "clip_to_output_white", kLevelsClip),
};

constexpr PRule kBoxBlur[] = {
    P(1, "Blur Radius", "radius"),
    U(2, "Iterations", 3),
    U(3, "Blur Dimensions", 1),
    P(4, "Repeat Edge Pixels", "repeat_edges", PConv::Bool),
};

constexpr PRule kLinearWipe[] = {
    P(1, "Transition Completion", "completion"),
    P(2, "Wipe Angle", "angle"),
    P(3, "Feather", "feather", PConv::PxPercentMax),
};

constexpr PRule kGlow[] = {
    E(1, "Glow Based On", "glow_based_on", kGlowBasedOn),
    P(2, "Glow Threshold", "threshold", PConv::Same, 100.0 / 255.0),
    P(3, "Glow Radius", "radius"),
    P(4, "Glow Intensity", "intensity"),
    E(5, "Composite Original", "glow_only", kGlowComposite),
    E(5, "Composite Original", "composite_original", kGlowCompositeMode),
    E(6, "Glow Operation", "add_mode", kGlowOperation),
    I(7, "Glow Colors"),
    E(8, "Color Looping", "color_looping", kMinus1),
    P(9, "Color Loops", "color_loops"),
    P(10, "Color Phase", "color_phase"),
    P(11, "A & B Midpoint", "ab_midpoint"),
    P(12, "Color A", "color_a", PConv::Color),
    P(13, "Color B", "color_b", PConv::Color),
    E(14, "Glow Dimensions", "glow_dimensions", kMinus1),   // H e V, Horizontal, Vertical
};

// PEDG 1.6.6: parameter numbers are matchName suffixes, not UI positions.
// REA ParamSetup and the installed host property enumeration agree on these IDs.
constexpr PRule kDeepGlow[] = {
    I(72, "Version"), I(73, "Vendor"), I(74, "Renderer"), I(75, "Shader Version"),
    P(1, "Radius", "ref_radius"),
    P(2, "Exposure", "ref_exposure"),
    P(4, "Threshold", "ref_threshold"),
    P(5, "Threshold Smooth", "ref_threshold_smooth"),
    U(56, "Layer", 0), // AE layer indices need composition-level reference resolution.
    E(57, "Mask Mode", "ref_mask_mode", kTwoChoice),
    P(59, "Invert", "ref_mask_invert", PConv::Bool),
    E(8, "Blend Mode", "ref_blend", kTwoChoice),
    P(35, "Smooth Blending", "ref_smooth_blending", PConv::Bool),
    P(11, "Spread", "ref_spread"),
    P(43, "Auto Detect Gamma", "ref_auto_gamma", PConv::Bool),
    P(9, "Gamma Correction", "ref_gamma", PConv::Bool),
    P(10, "Aspect Ratio", "ref_aspect"),
    P(52, "Enable Angle", "ref_angle_enabled", PConv::Bool),
    P(53, "Aspect Angle", "ref_angle"),
    P(13, "Enable Chromatic", "ref_chromatic_enabled", PConv::Bool),
    E(14, "Channels", "ref_channels", kMinus1),
    P(15, "Amount", "ref_chromatic_amount"),
    P(37, "Enable Tint", "ref_tint_enabled", PConv::Bool),
    P(38, "Color", "ref_tint_color", PConv::Color),
    E(39, "Mode", "ref_tint_mode", kMinus1),
    P(41, "Mix", "ref_tint_mix"),
    P(47, "Fixed Steps", "ref_fixed_steps", PConv::Bool),
    P(48, "Number of Steps", "ref_steps"),
    P(19, "Downsample", "ref_downsample"),
    P(20, "Steps Multiplier", "ref_steps_multiplier"),
    P(50, "Auto Iterations", "ref_auto_iterations", PConv::Bool),
    P(21, "Glow Iterations", "ref_iterations"),
    E(84, "Buffer Expansion", "ref_buffer_expansion", kTwoChoice),
    P(85, "Aux Expansion", "ref_aux_expansion"),
    P(31, "Enable Dither", "ref_dither_enabled", PConv::Bool),
    P(32, "Monochromatic", "ref_dither_mono", PConv::Bool),
    P(33, "Dither Amount", "ref_dither_amount"),
    E(23, "View", "ref_view", kTwoChoice),
    P(24, "Source Opacity", "ref_source_opacity"),
    P(25, "Unmult", "ref_unmult", PConv::Bool),
    E(55, "Edge Behaviour", "ref_edge_behaviour", kMinus1),
    I(66, "Show Buffers"), I(88, "Draw Dimensions Comp UI"),
    I(86, "Output PreRender Rects"), I(87, "Output Buffer Dimensions"),
    U(89, "View Input CPU", 0),
    P(26, "Left", "ref_left"), P(27, "Top", "ref_top"),
    P(28, "Right", "ref_right"), P(29, "Bottom", "ref_bottom"),
};
constexpr FixedValue kDeepGlowFixed[] = {{"render_mode", {1, 0, 0, 0}}};

constexpr PRule kInvert[] = {
    E(1, "Channel", "invert_color", kInvertColorAll),
    E(1, "Channel", "invert_alpha", kInvertAlpha),
    E(1, "Channel", "channel", kInvertChannel),
    P(2, "Blend With Original", "blend"),
};

constexpr PRule kRamp[] = {
    P(1, "Start of Ramp", "start", PConv::PointRel),
    P(2, "Start Color", "start_color", PConv::Color),
    P(3, "End of Ramp", "end", PConv::PointRel),
    P(4, "End Color", "end_color", PConv::Color),
    E(5, "Ramp Shape", "shape", kMinus1),
    P(6, "Ramp Scatter", "scatter"),
    P(7, "Blend With Original", "blend_original"),
    I(8, "Swap Colors"),
};

constexpr PRule kVenetian[] = {
    P(1, "Transition Completion", "completion"),
    P(2, "Direction", "direction"),
    P(3, "Width", "count", PConv::BlocksH),
    P(4, "Feather", "feather", PConv::PxPercentMax),
};

constexpr PRule kBevelAlpha[] = {
    P(1, "Edge Thickness", "width"),
    P(2, "Light Angle", "angle"),
    P(3, "Light Color", "highlight", PConv::Color),
    P(4, "Light Intensity", "strength", PConv::Same, 100),
};

constexpr PRule kShiftChannels[] = {
    I(1, "Take Alpha From"),
    I(2, "Take Red From"),
    I(3, "Take Green From"),
    I(4, "Take Blue From"),
};

constexpr PRule kTone[] = {
    E(1, "Waveform options", "waveform", kMinus1),
    P(2, "Frequency 1", "frequency1"),
    P(3, "Frequency 2", "frequency2"),
    P(4, "Frequency 3", "frequency3"),
    P(5, "Frequency 4", "frequency4"),
    P(6, "Frequency 5", "frequency5"),
    P(7, "Level", "level"),
};

constexpr PRule kBalanceHls[] = {
    P(1, "Hue", "hue"),
    P(2, "Lightness", "lightness"),
    P(3, "Saturation", "saturation"),
};

constexpr PRule kDropShadow[] = {
    P(1, "Shadow Color", "shadow_color", PConv::Color),
    P(2, "Opacity", "opacity", PConv::Same, 100.0 / 255.0),
    P(3, "Direction", "direction"),
    P(4, "Distance", "distance"),
    P(5, "Softness", "softness"),
    P(6, "Shadow Only", "shadow_only", PConv::Bool),
};

constexpr PRule kMosaic[] = {
    P(1, "Horizontal Blocks", "cell_size", PConv::BlocksW),
    I(2, "Vertical Blocks"),
    I(3, "Sharp Colors"),
};

constexpr PRule kMinimax[] = {
    E(1, "Operation", "mode", kMinimaxOp),
    P(2, "Radius", "radius"),
    E(3, "Channel", "channel", kMinimaxChannel),
    U(4, "Direction", 1),
    I(5, "Don't Shrink Edges"),
};

constexpr PRule kWaveWarp[] = {
    E(1, "Wave Type", "wave_type", kMinus1),
    P(2, "Wave Height", "height"),
    P(3, "Wave Width", "width"),
    P(4, "Direction", "direction"),
    P(5, "Wave Speed", "speed"),
    E(6, "Pinning", "pinning", kWavePin),
    P(7, "Phase", "phase"),
    E(8, "Antialiasing (Best Quality)", "antialias", kMinus1),
};

constexpr PRule kSharpen[] = {P(1, "Sharpen Amount", "amount")};

constexpr PRule kNoise[] = {
    P(1, "Amount of Noise", "intensity"),
    P(2, "Use Color Noise", "monochrome", PConv::Bool, -1, 1),
    I(3, "Clip Result Values"),
};

constexpr PRule kBlockDissolve[] = {
    P(1, "Transition Completion", "completion"),
    P(2, "Block Width", "block_width"),
    P(3, "Block Height", "block_height"),
    P(4, "Feather", "feather"),
    P(5, "Soft Edges (Best Quality)", "soft_edges", PConv::Bool),
};

constexpr PRule kDirectionalBlur[] = {
    // Direção 0 do AE = borrão vertical; o ângulo do Aurea mede o eixo do borrão.
    P(1, "Direction", "angle", PConv::Same, 1, 90),
    P(2, "Blur Length", "radius", PConv::Same, 0.5),
};

constexpr PRule kPosterizeTime[] = {P(1, "Frame Rate", "frame_rate")};

// AE reference batch A2 (Time_Displace.aex). The layer menu defaults to the
// layer itself, which is the Aurea default (no map layer); another AE layer
// needs composition-level index resolution and is left at that default.
constexpr PRule kTimeDisplacementReference[] = {
    I(1, "Time Displacement Layer"),
    P(2, "Max Displacement Time [sec]", "max_time"),
    P(3, "Time Resolution [fps]", "time_resolution"),
    P(4, "If Layer Sizes Differ", "stretch", PConv::Bool),
};

constexpr PRule kGaussian[] = {
    P(1, "Blurriness", "blurriness"),
    E(2, "Blur Dimensions", "dimensions", kMinus1),
    P(3, "Repeat Edge Pixels", "repeat_edges", PConv::Bool),
};

constexpr PRule kFastBlurReference[] = {
    P(1, "Blurriness", "radius"),
    E(2, "Blur Dimensions", "dimensions", kMinus1),
    P(3, "Repeat Edge Pixels", "repeat_edges", PConv::Bool),
};

constexpr PRule kTint[] = {
    P(1, "Map Black To", "map_black", PConv::Color),
    P(2, "Map White To", "map_white", PConv::Color),
    P(3, "Amount to Tint", "amount"),
    I(4, "Swap Colors"),
};

constexpr PRule kColorama[] = {
    E(2, "Get Phase From", "input", kColoramaInput),
    U(4, "Add Phase", 0),
    I(6, "Add Phase From"),
    I(40, "Add Mode"),
    P(8, "Phase Shift", "phase", PConv::Same, 1.0 / 360.0),
    U(10, "Use Preset Palette", 1),
    I(12, "Output Cycle"),
    P(38, "Cycle Repetitions", "cycles"),
    I(14, "Interpolate Palette"),
    U(16, "Modify", 1),
    I(18, "Modify Alpha"),
    I(34, "Change Empty Pixels"),
    I(20, "Matching Color"),
    I(22, "Matching Tolerance"),
    I(24, "Matching Softness"),
    U(26, "Matching Mode", 1),
    I(28, "Mask Layer"),
    U(30, "Masking Mode", 1),
    I(32, "Composite Over Layer"),
    P(36, "Blend With Original", "mix", PConv::Same, -1, 100),
};

constexpr PRule kFindEdges[] = {
    P(1, "Invert", "invert", PConv::Bool),
    P(2, "Blend With Original", "blend_original"),
};

constexpr PRule kRadialFastBlur[] = {
    P(1, "Center", "center", PConv::PointRel),
    P(2, "Amount", "amount"),
    E(3, "Zoom", "mode", kRadialFastZoom),   // Standard, Brightest, Darkest (0.4.5)
};

constexpr PRule kTurbulentDisplace[] = {
    E(1, "Displacement", "displacement", kTurbDisplace),
    P(2, "Amount", "amount"),
    P(3, "Size", "size"),
    P(4, "Offset (Turbulence)", "offset", PConv::PointRel),
    P(5, "Complexity", "complexity"),
    P(6, "Evolution", "evolution"),
    I(7, "Evolution Options"),
    P(8, "Cycle Evolution", "cycle_evolution", PConv::Bool),
    P(9, "Cycle (in Revolutions)", "cycle"),
    P(10, "Random Seed", "seed"),
    I(11, "Random Seed"),
    E(12, "Pinning", "pinning", kTurbPinning),
    P(13, "Resize Layer", "resize", PConv::Bool),
    E(14, "Antialiasing for Best Quality", "antialias", kTwoChoice),
};

constexpr PRule kFill[] = {
    I(1, "Fill Mask"),
    I(7, "All Masks"),
    P(2, "Color", "color", PConv::Color),
    U(6, "Invert", 0),
    U(3, "Horizontal Feather", 0),
    U(4, "Vertical Feather", 0),
    P(5, "Opacity", "opacity", PConv::Same, 100),
};

constexpr PRule kHueSaturation[] = {
    U(2, "Channel Control", 1),
    I(3, "Channel Range"),
    P(4, "Master Hue", "hue"),
    P(5, "Master Saturation", "saturation"),
    P(6, "Master Lightness", "lightness"),
    P(7, "Colorize", "colorize", PConv::Bool),
    P(8, "Colorize Hue", "colorize_hue"),
    P(9, "Colorize Saturation", "colorize_saturation"),
    U(10, "Colorize Lightness", 0),
};

constexpr PRule kToner[] = {
    U(5, "Tones", 2),
    P(1, "Highlights", "highlights", PConv::Color),
    I(6, "Brights"),
    P(2, "Midtones", "midtones", PConv::Color),
    I(7, "Darktones"),
    P(3, "Shadows", "shadows", PConv::Color),
    P(4, "Blend w. Original", "mix", PConv::Same, -1, 100),
};

constexpr PRule kMotionTile[] = {
    P(1, "Tile Center", "tile_center", PConv::PointRel),
    P(2, "Tile Width", "tile_width"),
    P(3, "Tile Height", "tile_height"),
    P(4, "Output Width", "output_width"),
    P(5, "Output Height", "output_height"),
    P(6, "Mirror Edges", "mirror_edges", PConv::Bool),
    P(7, "Phase", "phase"),
    P(8, "Horizontal Phase Shift", "horizontal_phase_shift", PConv::Bool),
};

constexpr PRule kPosterize[] = {P(1, "Level", "levels")};

// AE reference batch A (RadialBlurReferenceEffect.cpp): every native control.
constexpr PRule kRadialBlurReference[] = {
    P(1, "Amount", "amount"),
    P(2, "Center", "center", PConv::PointRel),
    E(3, "Type", "type", kMinus1),
    E(4, "Antialiasing (Best Quality)", "antialias", kMinus1),
    I(5, ""),
    P(6, "Random Seed", "seed"),
};

// Detail-preserving Upscale (batch A2): the Fit buttons have no value.
constexpr PRule kUpscaleReference[] = {
    I(1, "Fit to Comp Width"),
    I(2, "Fit to Comp Height"),
    P(3, "Scale", "scale"),
    P(4, "Reduce Noise", "reduce_noise"),
    P(5, "Detail", "detail"),
    E(6, "Alpha", "alpha", kMinus1),
};

constexpr PRule kRipple[] = {
    I(1, "Radius"),
    P(2, "Center of Ripple", "center_x", PConv::PointPercent, 1, 0, 0),
    P(2, "Center of Ripple", "center_y", PConv::PointPercent, 1, 0, 1),
    I(3, "Type of Conversion"),
    U(4, "Wave Speed", 1),
    P(5, "Wave Width", "wavelength"),
    P(6, "Wave Height", "amplitude"),
    P(7, "Ripple Phase", "phase"),
};

constexpr PRule kEcho[] = {
    // O tempo do eco do AE é negativo (para trás); o atraso do Aurea é positivo.
    P(1, "Echo Time (seconds)", "delay", PConv::Seconds, -1),
    P(2, "Number Of Echoes", "copies"),
    P(3, "Starting Intensity", "starting_intensity", PConv::Same, 100),
    P(4, "Decay", "decay"),
    E(5, "Echo Operator", "echo_operator", kMinus1),   // Add, Maximum, Minimum, Screen, In Back, In Front, Blend
};

constexpr PRule kRadialWipe[] = {
    P(1, "Transition Completion", "completion"),
    P(2, "Start Angle", "angle"),
    P(3, "Wipe Center", "center", PConv::PointRel),
    E(4, "Wipe", "direction", kMinus1),
    P(5, "Feather", "feather"),
};

constexpr PRule kBulge[] = {
    P(1, "Horizontal Radius", "radius", PConv::PxPercentMin),
    I(2, "Vertical Radius"),
    P(3, "Bulge Center", "center", PConv::PointRel),
    P(4, "Bulge Height", "height", PConv::Same, 25),
    U(5, "Taper Radius", 0),
    I(6, "Antialiasing (Best Qual Only)"),
    U(7, "Pinning", 0),
};

constexpr PRule kPowerPin[] = {
    P(2, "Top Left", "tl_x", PConv::PointPercent, 1, 0, 0),
    P(2, "Top Left", "tl_y", PConv::PointPercent, 1, 0, 1),
    P(3, "Top Right", "tr_x", PConv::PointPercent, 1, 0, 0),
    P(3, "Top Right", "tr_y", PConv::PointPercent, 1, 0, 1),
    P(4, "Bottom Left", "bl_x", PConv::PointPercent, 1, 0, 0),
    P(4, "Bottom Left", "bl_y", PConv::PointPercent, 1, 0, 1),
    P(5, "Bottom Right", "br_x", PConv::PointPercent, 1, 0, 0),
    P(5, "Bottom Right", "br_y", PConv::PointPercent, 1, 0, 1),
    U(6, "Perspective", 100),
    I(7, "Unstretch"),
    U(9, "Top", 0),
    U(10, "Left", 0),
    U(11, "Right", 0),
    U(12, "Bottom", 0),
};

constexpr PRule kThreshold[] = {P(1, "Level", "limit", PConv::Same, 100)};

constexpr PRule kVignette[] = {
    P(1, "Amount", "amount"),
    I(2, "Angle of View"),
    P(3, "Center", "center", PConv::PointRel),
    U(5, "Pin Highlights", 0),
};

constexpr PRule kFourColor[] = {
    P(1, "Point 1", "point_1", PConv::PointRel),
    P(2, "Color 1", "color_1", PConv::Color),
    P(3, "Point 2", "point_2", PConv::PointRel),
    P(4, "Color 2", "color_2", PConv::Color),
    P(5, "Point 3", "point_3", PConv::PointRel),
    P(6, "Color 3", "color_3", PConv::Color),
    P(7, "Point 4", "point_4", PConv::PointRel),
    P(8, "Color 4", "color_4", PConv::Color),
    P(9, "Blend", "softness", PConv::Same, 0.5),
    P(10, "Jitter", "jitter"),
    P(13, "Opacity", "opacity"),
    E(14, "Blending Mode", "blend", kGenBlend),
};

constexpr PRule kSpherize[] = {
    P(1, "Radius", "radius"),
    P(2, "Center of Sphere", "center", PConv::PointRel),
};
constexpr FixedValue kSpherizeFixed[] = {{"mode", {3, 0, 0, 0}}};

constexpr PRule kOffset[] = {
    P(1, "Shift Center To", "offset", PConv::PointCenter),
    U(2, "Blend With Original", 0),
};

// Solid Composite = a camada sobre um sólido: o mesmo do "Solid Matte" do Aurea
// quando a opacidade da origem fica em 100% e a mistura em Normal.
constexpr PRule kSolidComposite[] = {
    U(1, "Source Opacity", 100),
    P(2, "Color", "color", PConv::Color),
    P(3, "Opacity", "opacity"),
    U(4, "Blending Mode", 1),
};

constexpr PRule kChannelMixer[] = {
    P(1, "Red-Red", "rr", PConv::Same, 0.01), P(2, "Red-Green", "rg", PConv::Same, 0.01),
    P(3, "Red-Blue", "rb", PConv::Same, 0.01), P(4, "Red-Const", "ro", PConv::Same, 0.01),
    P(5, "Green-Red", "gr", PConv::Same, 0.01), P(6, "Green-Green", "gg", PConv::Same, 0.01),
    P(7, "Green-Blue", "gb", PConv::Same, 0.01), P(8, "Green-Const", "go", PConv::Same, 0.01),
    P(9, "Blue-Red", "br", PConv::Same, 0.01), P(10, "Blue-Green", "bg", PConv::Same, 0.01),
    P(11, "Blue-Blue", "bb", PConv::Same, 0.01), P(12, "Blue-Const", "bo", PConv::Same, 0.01),
    U(13, "Monochrome", 0),
};

constexpr PRule kKeylight[] = {P(4, "Screen Colour", "key_color", PConv::Color)};

constexpr PRule kReverb[] = {
    P(1, "Reverb Time (ms)", "reverb_time"),
    P(2, "Diffusion", "diffusion"),
    P(3, "Decay", "decay"),
    P(4, "Brightness", "brightness"),
    P(5, "Dry Out", "dry_out"),
    P(6, "Wet Out", "wet_out"),
};

constexpr PRule kPixelMotionBlur[] = {
    I(1, "Shutter Control"),
    P(2, "Shutter Angle", "shutter"),
    I(3, "Shutter Samples"),
    I(4, "Vector Detail"),
};

constexpr PRule kBrightnessContrast[] = {
    P(1, "Brightness", "brightness"),
    P(2, "Contrast", "contrast"),
    I(3, "Use Legacy (supports HDR)"),
};


// --- AE 2026: os efeitos que passaram a existir no Aurea ---------------------
// Conferidos no ae_effect_params.json: Leave Color, Circle, Color Emboss, Noise
// Alpha, Remove Color Matting, Magnify, Polar Coordinates, Roughen Edges,
// Advanced Lightning e Cell Pattern. Os outros (sem preset instalado para
// conferir) seguem a ordem da interface da documentação pública — a regra diz
// isso no motivo, e o relatório mostra.

constexpr PRule kLeaveColor[] = {
    P(1, "Amount to Decolor", "amount"),
    P(2, "Color To Leave", "color", PConv::Color),
    P(3, "Tolerance", "tolerance"),
    P(4, "Edge Softness", "softness"),
    E(5, "Match colors", "match", kTwoChoice),
};

constexpr PRule kCircle[] = {
    P(1, "Center", "center", PConv::PointRel),
    P(2, "Radius", "radius"),
    E(3, "Edge", "edge", kMinus1),
    P(4, "Thickness", "thickness"),
    P(6, "Feather Outer Edge", "feather_outer"),
    P(7, "Feather Inner Edge", "feather_inner"),
    P(9, "Invert Circle", "invert", PConv::Bool),
    P(10, "Color", "color", PConv::Color),
    P(11, "Opacity", "opacity"),
    E(12, "Blending Mode", "blend", kCircleBlend),
};

constexpr PRule kColorEmboss[] = {
    P(1, "Direction", "direction"),
    P(2, "Relief", "relief"),
    P(3, "Contrast", "contrast"),
    P(4, "Blend With Original", "blend"),
};

constexpr PRule kNoiseAlpha[] = {
    E(1, "Noise", "noise", kNoiseAlphaType),
    P(2, "Amount", "amount"),
    E(3, "Original Alpha", "original_alpha", kMinus1),
    E(4, "Overflow", "overflow", kMinus1),
    P(5, "Noise Phase", "phase"),
    P(7, "Cycle Noise", "cycle", PConv::Bool),
    P(8, "Cycle (in Revolutions)", "cycles"),
};

constexpr PRule kRemoveColorMatting[] = {
    P(1, "Background Color", "background", PConv::Color),
    P(2, "Clip HDR Results", "clip_hdr", PConv::Bool),
};

constexpr PRule kMagnify[] = {
    U(10, "Shape", 1),
    P(1, "Center", "center", PConv::PointRel),
    P(3, "Magnification", "zoom"),
    I(11, "Link"),
    P(2, "Size", "radius", PConv::PxPercentMin),
    U(4, "Feather", 0),
    U(5, "Opacity", 100),
    U(8, "Scaling", 1),
    U(12, "Blending Mode", 2),
    I(9, "Resize Layer"),
};

constexpr PRule kPolarCoordinates[] = {
    P(1, "Interpolation", "amount"),
    E(2, "Type of Conversion", "mode", kMinus1),
};

constexpr PRule kRoughenEdges[] = {
    U(1, "Edge Type", 1),
    I(10, "Edge Color"),
    P(2, "Border", "border"),
    P(3, "Edge Sharpness", "sharpness", PConv::Same, 10),
    U(4, "Fractal Influence", 1),
    I(5, "Scale"),
    U(6, "Stretch Width or Height", 0),
    I(7, "Offset (Turbulence)"),
    I(8, "Complexity"),
    P(9, "Evolution", "evolution"),
    I(12, "Cycle Evolution"),
    I(13, "Cycle (in Revolutions)"),
    I(14, "Random Seed"),
};

constexpr PRule kAdvancedLightning[] = {
    I(1, "Lightning Type"),
    P(2, "Origin", "start", PConv::PointRel),
    P(3, "Direction", "end", PConv::PointRel),
    I(4, "Conductivity State"),
    P(6, "Core Radius", "width"),
    I(7, "Core Opacity"),
    I(8, "Core Color"),
    P(11, "Glow Radius", "glow"),
    I(12, "Glow Opacity"),
    P(13, "Glow Color", "color", PConv::Color),
    I(15, "Alpha Obstacle"),
    I(16, "Turbulence"),
    I(17, "Forking"),
    I(18, "Decay"),
    I(19, "Decay Main Core"),
    I(20, "Composite on Original"),
    I(22, "Complexity"),
    I(23, "Min. Forkdistance"),
    I(24, "Termination Threshold"),
    I(25, "Main Core Collision Only"),
    I(26, "Fractal Type"),
    I(27, "Core Drain"),
    I(28, "Fork Strength"),
    I(29, "Fork Variation"),
};

constexpr PRule kCellPattern[] = {
    I(1, "Cell Pattern"),
    I(2, "Invert"),
    I(3, "Contrast"),
    I(4, "Overflow"),
    I(5, "Disperse"),
    P(6, "Size", "size"),
    I(7, "Offset"),
    I(9, "Enable Tiling"),
    I(10, "Cells Horizontal"),
    I(11, "Cells Vertical"),
    P(13, "Evolution", "evolution"),
    I(15, "Cycle Evolution"),
    I(16, "Cycle (in Revolutions)"),
    I(17, "Random Seed"),
};

constexpr PRule kTwirl[] = {
    P(1, "Angle", "angle"),
    P(2, "Twirl Radius", "radius"),
    P(3, "Twirl Center", "center", PConv::PointRel),
};

// Pela ordem da documentação (sem preset para conferir os números).
constexpr PRule kEmboss[] = {
    P(1, "Direction", "direction"),
    P(2, "Relief", "relief"),
    P(3, "Contrast", "contrast"),
    P(4, "Blend With Original", "blend"),
};
constexpr PRule kUnmult[] = {
    E(1, "Background Color", "background", kTwoChoice),
    P(2, "Black Level", "black_level"),
    P(3, "White Level", "white_level"),
    P(4, "Softness", "softness"),
    P(5, "Remove Color Matting", "remove_matting", PConv::Bool),
    P(6, "Clip HDR Results", "clip_hdr", PConv::Bool),
};
constexpr PRule kChangeColor[] = {
    E(1, "View", "view", kMinus1),
    P(2, "Hue Transform", "hue_transform"),
    P(3, "Lightness Transform", "lightness_transform"),
    P(4, "Saturation Transform", "saturation_transform"),
    P(5, "Color To Change", "color_to_change", PConv::Color),
    P(6, "Matching Tolerance", "tolerance"),
    P(7, "Matching Softness", "softness"),
    E(8, "Match Colors", "match", kMinus1),
    P(9, "Invert Color Correction Mask", "invert_mask", PConv::Bool),
};
constexpr PRule kChangeToColor[] = {
    P(1, "From", "from", PConv::Color),
    P(2, "To", "to", PConv::Color),
    E(3, "Change", "change", kMinus1),
    E(4, "Change By", "change_by", kMinus1),
    P(6, "Hue", "hue_tolerance"),
    P(7, "Lightness", "lightness_tolerance"),
    P(8, "Saturation", "saturation_tolerance"),
    P(10, "Softness", "softness"),
    P(11, "View Correction Matte", "view_matte", PConv::Bool),
};
constexpr PRule kAlphaLevels[] = {
    // Níveis gravados em 0..1, como nos Níveis (cabeçalho deste arquivo).
    P(1, "Input Black Level", "input_black", PConv::Same, 100),
    P(2, "Input White Level", "input_white", PConv::Same, 100),
    P(3, "Gamma", "gamma"),
    P(4, "Output Black Level", "output_black", PConv::Same, 100),
    P(5, "Output White Level", "output_white", PConv::Same, 100),
};
constexpr PRule kArithmetic[] = {
    E(1, "Operator", "operator", kMinus1Long),
    P(2, "Red Value", "red"),
    P(3, "Green Value", "green"),
    P(4, "Blue Value", "blue"),
    P(5, "Clip Result Values", "clip", PConv::Bool),
};
constexpr PRule kBilateral[] = {
    P(1, "Radius", "radius"),
    P(2, "Threshold", "threshold"),
    P(3, "Colorize", "colorize", PConv::Bool),
};
constexpr PRule kSmartBlur[] = {
    P(1, "Radius", "radius"),
    P(2, "Threshold", "threshold"),
    E(3, "Quality", "quality", kMinus1),
    E(4, "Mode", "mode", kMinus1),
};
constexpr PRule kBrushStrokes[] = {
    P(1, "Stroke Angle", "stroke_angle"),
    P(2, "Brush Size", "brush_size"),
    P(3, "Stroke Length", "stroke_length"),
    P(4, "Stroke Density", "stroke_density"),
    P(5, "Stroke Randomness", "stroke_randomness"),
    E(6, "Paint Surface", "paint_surface", kMinus1),
    P(7, "Blend With Original", "blend"),
};
// AE reference batch A (RadialShadowReferenceEffect.cpp): opacity and color
// influence are stored as fractions (1 = 100%).
constexpr PRule kRadialShadowReference[] = {
    P(1, "Shadow Color", "shadow_color", PConv::Color),
    P(2, "Opacity", "opacity", PConv::Same, 100),
    P(3, "Light Source", "light_source", PConv::PointRel),
    P(4, "Projection Distance", "projection_distance"),
    P(5, "Softness", "softness"),
    E(6, "Render", "render", kMinus1),
    P(7, "Color Influence", "color_influence", PConv::Same, 100),
    P(8, "Shadow Only", "shadow_only", PConv::Bool),
    P(9, "Resize Layer", "resize_layer", PConv::Bool),
};
#define AUREA_CARD_CAMERA(sys, rx, ry, rz, xy, z, fl, ord, ul, ur, ll, lr, af, cf)              E(sys, "Camera System", "camera_system", kMinus1), P(rx, "X Rotation", "camera_rotation_x"),     P(ry, "Y Rotation", "camera_rotation_y"), P(rz, "Z Rotation", "camera_rotation_z"),             P(xy, "X,Y Position", "camera_position", PConv::PointRel), P(z, "Z Position", "camera_z"),       P(fl, "Focal Length", "focal_length"), E(ord, "Transform Order", "camera_order", kMinus1Long),     P(ul, "Upper Left Corner", "corner_upper_left", PConv::PointRel),                               P(ur, "Upper Right Corner", "corner_upper_right", PConv::PointRel),                             P(ll, "Lower Left Corner", "corner_lower_left", PConv::PointRel),                               P(lr, "Lower Right Corner", "corner_lower_right", PConv::PointRel),                             P(af, "Auto Focal Length", "auto_focal_length", PConv::Bool), P(cf, "Focal Length", "corner_focal_length")
#define AUREA_CARD_LIGHT(in, co, po, de, ty, am, di, sp, sh)                                          P(in, "Light Intensity", "light_intensity"), P(co, "Light Color", "light_color", PConv::Color),     P(po, "Light Position", "light_position", PConv::PointRel), P(de, "Light Depth", "light_depth"),     E(ty, "Light Type", "light_type", kMinus1), P(am, "Ambient Light", "ambient_light"),              P(di, "Diffuse Reflection", "diffuse_reflection"), P(sp, "Specular Reflection", "specular_reflection"),     P(sh, "Highlight Sharpness", "highlight_sharpness")
// AE reference batch B (CardReferenceEffects.cpp): the NNNN of the live AE
// matchNames (added/runtime/parameters.jsonl). Layer choices are not in the
// preset: the back layer stays this layer, the gradient layers are reported.
constexpr PRule kCardWipeReference[] = {
    P(2, "Transition Completion", "completion"), P(4, "Transition Width", "width"), I(6, "Back Layer"),
    E(8, "Rows & Columns", "rows_columns", kMinus1), P(10, "Rows", "rows"), P(12, "Columns", "columns"),
    P(14, "Card Scale", "card_scale"), E(16, "Flip Axis", "flip_axis", kMinus1),
    E(18, "Flip Direction", "flip_direction", kMinus1), E(20, "Flip Order", "flip_order", kMinus1),
    U(22, "Gradient Layer", 1), P(24, "Timing Randomness", "timing_randomness"),
    P(26, "Random Seed", "random_seed"),
    AUREA_CARD_CAMERA(28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54),
    AUREA_CARD_LIGHT(56, 58, 60, 62, 64, 66, 68, 70, 72),
    P(74, "X Jitter Amount", "x_jitter"), P(76, "X Jitter Speed", "x_jitter_speed"),
    P(78, "Y Jitter Amount", "y_jitter"), P(80, "Y Jitter Speed", "y_jitter_speed"),
    P(82, "Z Jitter Amount", "z_jitter"), P(84, "Z Jitter Speed", "z_jitter_speed"),
    P(86, "X Rot Jitter Amount", "x_rotation_jitter"), P(88, "X Rot Jitter Speed", "x_rotation_jitter_speed"),
    P(90, "Y Rot Jitter Amount", "y_rotation_jitter"), P(92, "Y Rot Jitter Speed", "y_rotation_jitter_speed"),
    P(94, "Z Rot Jitter Amount", "z_rotation_jitter"), P(96, "Z Rot Jitter Speed", "z_rotation_jitter_speed"),
};
constexpr PRule kCardDanceReference[] = {
    E(2, "Rows & Columns", "rows_columns", kMinus1), P(4, "Rows", "rows"), P(6, "Columns", "columns"),
    I(8, "Back Layer"), U(10, "Gradient Layer 1", 0), U(12, "Gradient Layer 2", 0),
    E(14, "Rotation Order", "rotation_order", kMinus1), E(16, "Transformation Order", "transformation_order", kMinus1),
    E(18, "Source", "x_position_source", kMinus1Long), P(20, "Multiplier", "x_position_multiplier"),
    P(22, "Offset", "x_position_offset"),
    E(24, "Source", "y_position_source", kMinus1Long), P(26, "Multiplier", "y_position_multiplier"),
    P(28, "Offset", "y_position_offset"),
    E(30, "Source", "z_position_source", kMinus1Long), P(32, "Multiplier", "z_position_multiplier"),
    P(34, "Offset", "z_position_offset"),
    E(36, "Source", "x_rotation_source", kMinus1Long), P(38, "Multiplier", "x_rotation_multiplier"),
    P(40, "Offset", "x_rotation_offset"),
    E(42, "Source", "y_rotation_source", kMinus1Long), P(44, "Multiplier", "y_rotation_multiplier"),
    P(46, "Offset", "y_rotation_offset"),
    E(48, "Source", "z_rotation_source", kMinus1Long), P(50, "Multiplier", "z_rotation_multiplier"),
    P(52, "Offset", "z_rotation_offset"),
    E(54, "Source", "x_scale_source", kMinus1Long), P(56, "Multiplier", "x_scale_multiplier"),
    P(58, "Offset", "x_scale_offset"),
    E(60, "Source", "y_scale_source", kMinus1Long), P(62, "Multiplier", "y_scale_multiplier"),
    P(64, "Offset", "y_scale_offset"),
    AUREA_CARD_CAMERA(66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92),
    AUREA_CARD_LIGHT(94, 96, 98, 100, 102, 104, 106, 108, 110),
};
#undef AUREA_CARD_CAMERA
#undef AUREA_CARD_LIGHT
// AE reference batch C (ExtrudeReferenceEffect.cpp): Plugin Everything Extrude.
// The NNNN numbers come from the live AE property matchNames
// (batch-c/status.json, deepglow-extrude/PEMUEXTRUDE-runtime-controls.json).
// The gradient-wheel editor (Gradient Pt Select / Point Angle) is UI state.
constexpr PRule kExtrudeReference[] = {
    P(35, "Mask Path", "mask_path"),
    P(36, "Use All Paths", "all_paths", PConv::Bool),
    E(2, "Mode", "mode", kMinus1),
    P(10, "Inner Extrude", "inner", PConv::Bool),
    P(1002, "Extrusion Direction", "direction"),
    P(3, "Extrusion Length Back", "back_length"),
    P(4, "Extrusion Length Front", "front_length"),
    P(5, "Vanishing Point", "vanishing", PConv::PointRel),
    P(6, "Extrusion Amount Back", "back_amount"),
    P(7, "Extrusion Amount Front", "front_amount"),
    P(8, "Stroke Width", "stroke_width"),
    P(9, "Stroke Color", "stroke_color", PConv::Color),
    P(66, "Stroke Opacity", "stroke_opacity"),
    P(11, "Inherit Layer Color", "inherit_color", PConv::Bool),
    P(12, "Front Fill Color", "front_color", PConv::Color),
    E(28, "Side Fill Mode", "side_mode", kMinus1),
    P(13, "Number of Side Fill Colors", "color_count"),
    P(14, "Side Fill Color", "side_color", PConv::Color),
    I(33, "Gradient Pt Select"),
    I(17, "Point Angle"),
    P(15, "Global Offset", "global_offset"),
    P(58, "Interpolate Gradient", "interpolate", PConv::Bool),
    P(70, "Fill Opacity", "fill_opacity"),
    P(16, "Side Fill 2 Color", "color_2", PConv::Color),
    P(18, "Side Fill 3 Color", "color_3", PConv::Color),
    P(19, "Side Fill 3 Angle", "angle_3"),
    P(20, "Side Fill 4 Color", "color_4", PConv::Color),
    P(21, "Side Fill 4 Angle", "angle_4"),
    P(22, "Side Fill 5 Color", "color_5", PConv::Color),
    P(23, "Side Fill 5 Angle", "angle_5"),
    U(80, "Parent Layer", 0),
    P(81, "Anchor Point", "anchor", PConv::PointRel),
    P(65, "Input Opacity", "input_opacity"),
    P(57, "Master Resolution", "resolution"),
    P(24, "Corner Angle Threshold", "corner_threshold"),
    P(25, "Flatness Threshold", "flatness"),
    P(26, "Front Mask Resolution Scale", "front_resolution"),
    E(59, "Debug View", "debug_view", kMinus1),
    P(60, "Debug View X-Offset", "debug_x"),
    P(61, "Debug View Y-Offset", "debug_y"),
    P(71, "Left", "left"),
    P(72, "Top", "top"),
    P(73, "Right", "right"),
    P(74, "Bottom", "bottom"),
    I(75, "Sentinel Init Value"),
};
// AE reference batch C: Depth of Field (Z from the Aurea AI depth, see
// ReferenceBatchC.cpp), Smear (masks by index) and Glitchify (native PF ids).
constexpr PRule kDepthFieldReference[] = {
    P(1, "Focal Plane", "focal_plane"),
    P(2, "Maximum Radius", "max_radius"),
    P(3, "Focal Plane Thickness", "focal_thickness"),
    P(4, "Focal Bias", "focal_bias"),
};
constexpr PRule kSmearReference[] = {
    P(1, "Source Mask", "source_mask"),
    P(2, "Boundary Mask", "boundary_mask"),
    P(3, "Mask Offset", "mask_offset", PConv::PointRel),
    P(4, "Mask Rotation", "mask_rotation"),
    P(5, "Mask Scale", "mask_scale"),
    P(6, "Percent", "percent"),
    E(7, "Elasticity", "elasticity", kMinus1),
    E(8, "Interpolation Method", "interpolation", kMinus1),
};
// Glitchify menus keep their separators in AE: Red, Green, Blue, Alpha, (-,
// Luminance, Lightness, Hue, Saturation, (-, Noise …
constexpr i8 kGlitchTransition[] = {-1, 0, 1, 2, 3, -1, 4, 5, 6, 7, -1, 8};
constexpr i8 kGlitchSplit[] = {-1, 0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8};
constexpr i8 kGlitchScale[] = {-1, 0, 1, 2, 3, -1, 4, 5, 6, 7};
constexpr i8 kGlitchTransfer[] = {-1, 0, 1, 2, 3, -1, 4, 5, 6, 7, 8, 9, -1, 10, 11, 12, 13, 14, 15, 16, -1,
                                  17, 18, 19, 20, 21, 22, 23, -1, 24, 25, 26, 27, 28, -1, 29, 30, 31, 32, -1,
                                  33, 34, 35, 36, -1, 37, 38};
constexpr PRule kGlitchifyReference[] = {
    P(1, "Glitchify Amount", "amount"), P(2, "Glitchify Speed", "speed"), P(3, "Random Seed", "seed"),
    P(5, "Completion", "completion"), P(6, "Softness", "softness"),
    E(7, "Channel", "transition_channel", kGlitchTransition),
    P(10, "Speed", "transform_speed"), P(11, "Position", "position", PConv::PointRel),
    P(12, "Position Wiggle", "position_wiggle"), P(13, "Scale", "scale"), P(14, "Scale Wiggle", "scale_wiggle"),
    P(15, "Crop", "crop_top_left", PConv::PointRel), P(16, "Crop", "crop_bottom_right", PConv::PointRel),
    P(17, "Crop Wiggle", "crop_wiggle"), P(18, "Opacity", "transform_opacity"),
    E(19, "Transfer Mode", "transform_mode", kGlitchTransfer), P(20, "Composite Over", "composite_over", PConv::Bool),
    P(23, "Enable", "channel_enabled", PConv::Bool), E(25, "Channel", "split_channel", kGlitchSplit),
    P(26, "Horizontal Amount", "split_horizontal"), P(27, "Vertical amount", "split_vertical"),
    P(28, "Speed Multiplier", "split_speed"), P(31, "Scale", "channel_scale"),
    E(32, "Channel", "scale_channel", kGlitchScale), P(33, "Vertical", "scale_vertical", PConv::Bool),
    P(34, "Offset", "scale_offset"), P(35, "Speed Multiplier", "scale_speed"),
    E(37, "Transfer Mode", "channel_mode", kGlitchTransfer),
    P(40, "Enable", "color_enabled", PConv::Bool), P(41, "Offset", "color_offset"), P(42, "Amount", "color_amount"),
    P(43, "Opacity", "color_opacity"), E(44, "Transfer Mode", "color_mode", kGlitchTransfer),
    P(47, "Enable", "image_enabled", PConv::Bool), P(48, "Pixel Streak", "pixel_streak"),
    P(49, "Vertical Sort", "vertical_sort", PConv::Bool), P(51, "Amount", "slice_amount"),
    P(52, "Position", "slice_position"), P(53, "Opacity", "slice_opacity"),
    E(54, "Transfer Mode", "slice_mode", kGlitchTransfer), P(57, "Amount", "block_amount"),
    P(58, "Speed Multiplier", "block_speed"), P(59, "Horizontal Size", "block_width"),
    P(60, "Vertical Size", "block_height"), E(61, "Direction", "block_direction", kMinus1),
    E(63, "Transfer Mode", "image_mode", kGlitchTransfer),
    P(66, "Enable", "compression_enabled", PConv::Bool), P(67, "Multiplier", "compression_multiplier"),
    P(68, "Dither Amount", "dither_amount"), P(69, "JPEG Amount", "jpeg_amount"),
    P(73, "Show noise engine", "show_noise", PConv::Bool), P(74, "Gain", "noise_gain"),
    P(75, "Lacunarity", "noise_lacunarity"), P(76, "Octaves", "noise_octaves"),
    P(79, "Repeat Edge Pixels", "repeat_edges", PConv::Bool), P(82, "Sigmoidal Steps", "sigmoid_steps", PConv::Bool),
    P(83, "Quantization Steps", "scale_steps"), P(84, "Fill Gaps", "fill_gaps", PConv::Bool),
    P(87, "Use Noise", "use_noise", PConv::Bool), P(88, "Noise Cutoff", "noise_cutoff"),
    P(91, "Quality", "slice_quality"), P(92, "Max Random Shift", "slice_shift"),
    P(93, "Quantization Steps", "slice_steps"), P(96, "Grouping Factor", "grouping"),
    P(99, "Wiggle Quantization Steps", "wiggle_steps"),
};
constexpr PRule kBevelEdges[] = {
    P(1, "Edge Thickness", "edge_thickness", PConv::Same, 100),
    P(2, "Light Angle", "light_angle"),
    P(3, "Light Color", "light_color", PConv::Color),
    P(4, "Light Intensity", "light_intensity", PConv::Same, 100),
};
constexpr PRule kEllipse[] = {
    P(1, "Center", "center", PConv::PointRel),
    P(2, "Width", "width"),
    P(3, "Height", "height"),
    P(4, "Thickness", "thickness"),
    P(5, "Softness", "softness"),
    P(6, "Inside Color", "inside_color", PConv::Color),
    P(7, "Outside Color", "outside_color", PConv::Color),
    P(8, "Composite On Original", "composite_on_original", PConv::Bool),
};
constexpr PRule kEyedropperFill[] = {
    P(1, "Sample Point", "sample_point", PConv::PointRel),
    P(2, "Sample Radius", "sample_radius"),
    E(3, "Average Pixel Colors", "average", kMinus1),
    P(4, "Maintain Original Alpha", "maintain_alpha", PConv::Bool),
    P(5, "Blend With Original", "blend"),
};
constexpr PRule kStrobe[] = {
    P(1, "Strobe Color", "color", PConv::Color),
    P(2, "Blend With Original", "blend"),
    P(3, "Strobe Duration (secs)", "duration"),
    P(4, "Strobe Period (secs)", "period"),
    P(5, "Random Strobe Probability", "probability"),
    E(6, "Strobe", "strobe", kMinus1),
    I(7, "Strobe Operator"),
    P(8, "Random Seed", "seed"),
};
constexpr PRule kNoiseHls[] = {
    E(1, "Noise", "noise", kMinus1),
    P(2, "Hue", "hue"),
    P(3, "Lightness", "lightness"),
    P(4, "Saturation", "saturation"),
    P(5, "Grain Size", "grain_size"),
    P(6, "Noise Phase", "phase"),
};
constexpr PRule kNoiseHlsAuto[] = {
    E(1, "Noise", "noise", kMinus1),
    P(2, "Hue", "hue"),
    P(3, "Lightness", "lightness"),
    P(4, "Saturation", "saturation"),
    P(5, "Grain Size", "grain_size"),
    P(6, "Noise Animation Speed", "speed"),
};
constexpr PRule kBlend[] = {
    I(1, "Blend With Layer"),
    E(2, "Mode", "mode", kMinus1),
    P(3, "Blend With Original", "blend_with_original"),
    E(4, "If Layer Sizes Differ", "size_mode", kMinus1),
};
constexpr PRule kDifferenceMatte[] = {
    E(1, "View", "view", kMinus1),
    I(2, "Difference Layer"),
    E(3, "If Layer Sizes Differ", "size_mode", kMinus1),
    P(4, "Matching Tolerance", "tolerance"),
    P(5, "Matching Softness", "softness"),
    P(6, "Blur Before Difference", "blur"),
};
constexpr PRule kColorRange[] = {
    I(1, "Preview"),
    P(2, "Fuzziness", "fuzziness"),
    E(3, "Color Space", "color_space", kMinus1),
    P(4, "Min (L, Y, R)", "min_1"),
    P(5, "Max (L, Y, R)", "max_1"),
    P(6, "Min (a, U, G)", "min_2"),
    P(7, "Max (a, U, G)", "max_2"),
    P(8, "Min (b, V, B)", "min_3"),
    P(9, "Max (b, V, B)", "max_3"),
};
constexpr PRule kDepthMatte[] = {
    P(1, "Depth", "depth"),
    P(2, "Feather", "feather"),
    P(3, "Invert", "invert", PConv::Bool),
};
constexpr PRule kFog3D[] = {
    P(1, "Fog Color", "fog_color", PConv::Color),
    P(2, "Fog Start Depth", "start_depth"),
    P(3, "Fog End Depth", "end_depth"),
    P(4, "Fog Opacity", "opacity"),
    P(5, "Scattering Density", "scattering_density"),
    P(6, "Foggy Background", "foggy_background", PConv::Bool),
};
constexpr PRule kBassTreble[] = {
    P(1, "Bass", "bass"),
    P(2, "Treble", "treble"),
};
constexpr PRule kWarp[] = {
    E(1, "Warp Style", "style", kMinus1Long),
    E(2, "Warp Axis", "axis", kMinus1),
    P(3, "Bend", "bend"),
    P(4, "Horizontal Distortion", "horizontal_distortion"),
    P(5, "Vertical Distortion", "vertical_distortion"),
};
constexpr PRule kGridWipe[] = {
    P(1, "Completion", "completion"),
    P(2, "Center", "center", PConv::PointRel),
    P(3, "Rotation", "rotation"),
    P(4, "Border", "border"),
    P(5, "Tiles", "tiles"),
    E(6, "Shape", "shape", kMinus1),
    P(7, "Reverse Transition", "reverse", PConv::Bool),
};
constexpr PRule kLightWipe[] = {
    P(1, "Completion", "completion"),
    P(2, "Center", "center", PConv::PointRel),
    P(3, "Intensity", "intensity"),
    E(4, "Shape", "shape", kMinus1),
    P(5, "Direction", "direction"),
    P(6, "Color from Source", "color_from_source", PConv::Bool),
    P(7, "Color", "color", PConv::Color),
    P(8, "Reverse Transition", "reverse", PConv::Bool),
};

constexpr PRule kJaws[] = {
    P(1, "Completion", "completion"),
    P(2, "Center", "center", PConv::PointRel),
    P(3, "Direction", "direction"),
    P(4, "Height", "height"),
    P(5, "Width", "width"),
    E(6, "Shape", "shape", kMinus1),
};
constexpr PRule kLineSweep[] = {
    P(1, "Completion", "completion"),
    P(2, "Direction", "direction"),
    P(3, "Thickness", "thickness"),
    P(4, "Slant", "slant"),
    P(5, "Flip Direction", "flip_direction", PConv::Bool),
};
constexpr PRule kRain[] = {
    P(1, "Amount", "amount"),
    P(2, "Speed", "speed"),
    P(3, "Angle", "angle"),
    P(4, "Angle Variation", "angle_variation"),
    P(5, "Drop Size", "drop_size"),
    P(6, "Source Depth", "source_depth"),
    P(7, "Opacity", "opacity"),
};
constexpr PRule kSnow[] = {
    P(1, "Amount", "amount"),
    P(2, "Speed", "speed"),
    P(3, "Amplitude", "amplitude"),
    P(4, "Frequency", "frequency"),
    P(5, "Flake Size", "flake_size"),
    P(6, "Source Depth", "source_depth"),
    P(7, "Opacity", "opacity"),
};
constexpr PRule kCrossBlur[] = {
    P(1, "Radius X", "radius_x"),
    P(2, "Radius Y", "radius_y"),
    E(3, "Transfer Mode", "transfer_mode", kMinus1),
    P(4, "Repeat Edge Pixels", "repeat_edge", PConv::Bool),
};
constexpr PRule kWireRemoval[] = {
    P(1, "Point A", "point_a", PConv::PointRel),
    P(2, "Point B", "point_b", PConv::PointRel),
    E(3, "Removal Style", "removal_style", kMinus1),
    P(4, "Thickness", "thickness"),
    P(5, "Slope", "slope"),
    P(6, "Mirror Blend", "mirror_blend"),
};
constexpr PRule kInnerShadow[] = {
    I(1, "Blend Mode"),
    P(2, "Color", "color", PConv::Color),
    P(3, "Opacity", "opacity"),
    I(4, "Use Global Light"),
    P(5, "Angle", "angle"),
    P(6, "Distance", "distance"),
    P(7, "Choke", "choke"),
    P(8, "Size", "size"),
};

// --- acertos que dependem de mais de um parâmetro (.ffx) ----------------------

u32 find_param(const ParameterRegistry& s, const char* id) noexcept { return s.find(id); }
void set_param(EffectInstance& fx, const ParameterRegistry& s, const char* id, u32 comp, f32 v) noexcept {
    const u32 p = find_param(s, id);
    if (p != kInvalidIndex && p < fx.params.size() && comp < 4) fx.params[p].constant.v[comp] = v;
}

void post_geometry2(EffectInstance& fx, const ParameterRegistry& s, const PostIn& in, ImportReport&) {
    if (in.get(11, 0, 1.0) < 0.5)   // escala não uniforme: a largura vale no X
        set_param(fx, s, "scale", 0, static_cast<f32>(in.get(4, 0, 100.0)));
}

void post_glow(EffectInstance& fx, const ParameterRegistry& s, const PostIn& in, ImportReport& rep) {
    const f64 mode = in.get(7, 0, 1.0);
    if (mode >= 1.5 && mode < 2.5) {   // A & B: as cores A e B do Aurea (AE 2026); a cor única fica na A
        const PostIn::V* a = in.find(12);
        if (a && a->v.size() >= 3) {
            for (u32 c = 0; c < 3; ++c)
                set_param(fx, s, "color", c, Color::srgb_to_linear(static_cast<f32>(std::clamp(a->v[c], 0.0, 1.0))));
            set_param(fx, s, "color", 3, 1.0f);
        }
        set_param(fx, s, "tint_amount", 0, 100.0f);
        set_param(fx, s, "glow_colors", 0, 1.0f);
    } else {
        set_param(fx, s, "tint_amount", 0, 0.0f);   // cores originais
        if (mode > 2.5) rep.note("parametro", "Glow > Glow Colors", "mapa arbitrário de cores não existe no Aurea");
    }
}

void post_mosaic(EffectInstance&, const ParameterRegistry&, const PostIn& in, ImportReport& rep) {
    const f64 hb = std::max(1.0, in.get(1, 0, 10.0)), vb = std::max(1.0, in.get(2, 0, 10.0));
    const f64 cw = in.layerW / hb, ch = in.layerH / vb;
    if (std::fabs(cw - ch) > 0.05 * std::max(cw, ch))
        rep.note("parametro", "Mosaic > Vertical Blocks", "o Aurea usa células quadradas: valeu a largura dos blocos horizontais");
}

void post_shift_channels(EffectInstance& fx, const ParameterRegistry& s, const PostIn& in, ImportReport& rep) {
    // Pegar R/G/B de outro canal = uma linha da matriz de cor.
    static const char* rows[3][4] = {{"rr", "rg", "rb", "ro"}, {"gr", "gg", "gb", "go"}, {"br", "bg", "bb", "bo"}};
    for (u32 t = 0; t < 3; ++t) {
        const i32 src = static_cast<i32>(std::lround(in.get(static_cast<u16>(2 + t), 0, 2.0 + t)));
        f32 row[4] = {0, 0, 0, 0};
        switch (src) {
            case 2: row[0] = 1; break;
            case 3: row[1] = 1; break;
            case 4: row[2] = 1; break;
            case 5: case 7: row[0] = 0.299f; row[1] = 0.587f; row[2] = 0.114f; break;
            case 9: row[3] = 1; break;
            case 10: break;
            default:
                row[t] = 1;
                rep.note("parametro", "Shift Channels", "canal de origem (alfa/matiz/saturação) sem equivalente; ficou o próprio canal");
        }
        for (u32 c = 0; c < 4; ++c) set_param(fx, s, rows[t][c], 0, row[c]);
    }
    if (std::lround(in.get(1, 0, 1.0)) != 1)
        rep.note("parametro", "Shift Channels > Take Alpha From", "o alfa não muda de canal no Aurea");
}

// --- Lote "AE 2026", segunda leva (AeEffects2026Extra.cpp) ----------------------
// Menus pela lista do .aex (com os separadores "(-"); a ordem dos parâmetros
// segue a documentação pública (os grupos de tópico do AE contam um índice).
// RGB to HLS, HLS to RGB, RGB to YUV, YUV to RGB, Straight to Premultiplied, (-, Red, Green,
// Blue, Alpha, (-, Hue, Lightness, Luminance, Saturation, Saturation Multiplied, (-, Min RGB, Max RGB
constexpr i8 kCombinerFrom[] = {-1, 10, 11, 12, 13, -1, -1, 4, 5, 6, 7, -1, 0, 1, 3, 2, 2, -1, 8, 9};
// Red, Green, Blue, Alpha, (-, Hue, Lightness, Saturation, (-, Red only ... Saturation only
constexpr i8 kCombinerTo[] = {-1, 0, 1, 2, 3, -1, 4, 5, 6, -1, 0, 1, 2, 3, -1, 4, 5, 6};
constexpr PRule kChannelCombiner[] = {
    U(1, "Use 2nd Layer", 0),
    I(2, "Source Layer"),
    E(3, "From", "from", kCombinerFrom),
    E(4, "To", "to", kCombinerTo),
    P(5, "Invert", "invert", PConv::Bool),
    P(6, "Solid Alpha", "solid_alpha", PConv::Bool),
};
// Reduce Luminance, Reduce Saturation, (-, Key Out Unsafe, Key Out Safe
constexpr i8 kBroadcastMethod[] = {-1, 0, 1, -1, 2, 3};
constexpr PRule kBroadcastColors[] = {
    E(1, "Broadcast Locale", "locale", kMinus1),
    E(2, "How to Make Color Safe", "method", kBroadcastMethod),
    P(3, "Maximum Signal Amplitude (IRE)", "max_amplitude"),
};
constexpr PRule kCompoundArithmetic[] = {
    I(1, "Second Source Layer"),
    E(2, "Operator", "operator", kMinus1Long),
    E(3, "Operate on Channels", "channels", kMinus1),
    E(4, "Overflow Behavior", "overflow", kMinus1),
    P(5, "Stretch Second Source to Fit", "stretch", PConv::Bool),
    P(6, "Blend With Original", "blend"),
};
// Stereo Pair (Side by Side), Over Under, Interlace Upper L Lower R, Difference, (-, Red Green LR,
// Red Blue LR, (-, Balanced Red Green LR, Balanced Red Blue LR, (-, Balanced Colored Red Blue
constexpr i8 kGlassesView[] = {-1, 0, -1, 1, -1, -1, 2, 3, -1, 2, 4, -1, 5};
constexpr PRule kGlasses3D[] = {
    I(1, "Left View"),
    I(2, "Right View"),
    P(3, "Scene Convergence", "convergence"),
    P(4, "Vertical Alignment", "vertical_alignment"),
    U(5, "Units", 1),
    E(6, "3D View", "view", kGlassesView),
    P(7, "Balance", "balance"),
    P(8, "Swap Left-Right", "swap", PConv::Bool),
};
constexpr PRule kGlassWipe[] = {
    P(1, "Completion", "completion"),
    I(2, "Layer to Reveal"),
    I(3, "Gradient Layer"),
    P(4, "Softness", "softness"),
    P(5, "Displacement Amount", "displacement"),
};
constexpr PRule kWarpoMatic[] = {
    P(1, "Completion", "completion"),
    I(2, "Layer to Reveal"),
    E(3, "Reactor", "reactor", kMinus1),
    P(4, "Smoothness", "smoothness"),
    P(5, "Warp Amount", "warp_amount"),
    E(6, "Warp Direction", "warp_direction", kMinus1),
    P(7, "Blend Span", "blend_span"),
};
constexpr PRule kEnvironment[] = {
    I(1, "Environment"),
    E(2, "Mapping", "mapping", kMinus1),
    P(3, "Filter Environment", "filter_environment", PConv::Bool),
};
constexpr PRule kThreads[] = {
    P(1, "Width", "width"),
    P(2, "Height", "height"),
    P(3, "Overlaps", "overlaps"),
    P(4, "Direction", "direction"),
    P(5, "Center", "center", PConv::PointRel),
    P(6, "Coverage", "coverage"),
    P(7, "Shadowing", "shadowing"),
    P(8, "Texture", "texture"),
};
constexpr PRule kColorNeutralizer[] = {
    P(1, "Shadows Unbalance", "shadows_unbalance", PConv::Color),
    P(2, "Midtones Unbalance", "midtones_unbalance", PConv::Color),
    P(3, "Highlights Unbalance", "highlights_unbalance", PConv::Color),
    P(4, "Pinning", "pinning"),
    P(5, "Blend", "blend"),
};
// Distortion: os grupos "Amp" e "Bitcrusher" do AE contam um índice cada (palpite pela ordem do .aex).
constexpr PRule kAudDistortion[] = {
    I(1, "Amp"),
    E(2, "Type", "type", kMinus1),
    P(3, "Drive", "drive"),
    P(4, "Gain", "gain"),
    P(5, "Mix", "mix"),
    P(6, "Volume", "volume"),
    I(7, "Bitcrusher"),
    P(8, "Resolution (bit)", "resolution"),
    P(9, "Downsample (x)", "downsample"),
};
constexpr PRule kAudGate[] = {
    P(1, "Threshold (dB)", "threshold"),
    P(2, "Attack (ms)", "attack"),
    P(3, "Hold (ms)", "hold"),
    P(4, "Release (ms)", "release"),
};
constexpr PRule kOverbrights[] = {
    U(1, "Channel", 1),
    P(2, "Clip Color", "clip_color", PConv::Color),
};

// Mapeamentos a partir dos match names e pard do projeto AEP de referência.
constexpr i8 kExposureMode[] = {-1, 0, 1};
constexpr PRule kAeExposure[] = {
    E(1, "Channel", "channels", kExposureMode),
    I(2, "Master Group"), P(3, "Exposure", "exposure"), P(4, "Offset", "offset"), P(5, "Gamma Correction", "gamma"),
    I(6, "Master End"), I(7, "Red Group"),
    P(8, "Red Exposure", "red_exposure"), P(9, "Red Offset", "red_offset"), P(10, "Red Gamma", "red_gamma"),
    I(11, "Red End"), I(12, "Green Group"),
    P(13, "Green Exposure", "green_exposure"), P(14, "Green Offset", "green_offset"), P(15, "Green Gamma", "green_gamma"),
    I(16, "Green End"), I(17, "Blue Group"),
    P(18, "Blue Exposure", "blue_exposure"), P(19, "Blue Offset", "blue_offset"), P(20, "Blue Gamma", "blue_gamma"),
    I(21, "Blue End"),
};
constexpr PRule kAeBlackWhite[] = {
    P(1, "Reds", "reds"), P(2, "Yellows", "yellows"), P(3, "Greens", "greens"),
    P(4, "Cyans", "cyans"), P(5, "Blues", "blues"), P(6, "Magentas", "magentas"),
    P(7, "Tint", "tint", PConv::Bool), P(8, "Tint Color", "tint_color", PConv::Color),
};
constexpr PRule kAeVibrance[] = {
    P(1, "Vibrance", "vibrance"), P(2, "Saturation", "saturation", PConv::Same, 1, 100),
};
constexpr PRule kSapphireRandomEdits[] = {
    P(50, "Edit Frame Length", "edit_length"), P(51, "Seed", "seed"),
};
constexpr PRule kSapphireShake[] = {
    P(50, "Amplitude", "amount", PConv::Same, 100),
    P(51, "Frequency", "frequency"), P(52, "Phase", "phase"),
    P(56, "Seed", "seed"), P(59, "X Rand Amp", "amplitude_x"),
    P(64, "Y Rand Amp", "amplitude_y"), P(74, "Tilt Rand Amp", "rotation"),
};
constexpr PRule kSapphireRays[] = {
    P(50, "Center XY", "center", PConv::PointRel),
    P(51, "Rays Length", "length", PConv::Same, 100),
    P(52, "Rays Brightness", "intensity"), P(53, "Rays Color", "color", PConv::Color),
    P(56, "Threshold", "threshold", PConv::Same, 100),
};
constexpr PRule kBccUnsharp[] = {
    P(2, "Radius", "radius"), P(3, "Amount", "amount"), P(4, "Threshold", "threshold"),
    P(6, "Mix with Original", "blend", PConv::Same, -1, 100),
};
constexpr PRule kBccHls[] = {
    P(2, "Hue", "hue"), P(3, "Saturation", "saturation"), P(4, "Lightness", "lightness"),
};

// 1.0.8: "Importar tudo do AE". Efeitos de plugins e do AE que o Aurea tem,
// sem a lista de parâmetros conferida num preset: entram com os padrões do
// Aurea (nota "parâmetros aproximados"). I(0, "*") = os parâmetros do AE que
// a regra não conhece ficam de fora em silêncio (a nota já diz).
constexpr PRule kApprox[] = {I(0, "*")};
// Unsharp Mask do AE: Amount (%), Radius (px), Threshold (0..255 níveis).
constexpr PRule kAeUnsharp[] = {
    P(1, "Amount", "amount"), P(2, "Radius", "radius"),
    P(3, "Threshold", "threshold", PConv::Same, 100.0 / 255.0),
    I(0, "*"),
};
// Camera Lens Blur: o raio; íris, mapa de desfoque e realces ficam no padrão.
constexpr PRule kCameraLensBlur[] = {P(1, "Blur Radius", "radius"), I(0, "*")};
// O padrão do Flow Retime é 50 %; sem a velocidade do plugin, fica 100 %.
constexpr FixedValue kRetimeFixed[] = {{"speed", {100, 0, 0, 0}}};

// --- a tabela ----------------------------------------------------------------
#define RULE(ae, name, key, params) ERule{ae, name, key, params, static_cast<u32>(std::size(params)), nullptr, nullptr, 0, nullptr}
#define RULEX(ae, name, key, params, reason, fixed, nfixed, post) \
    ERule{ae, name, key, params, static_cast<u32>(std::size(params)), reason, fixed, nfixed, post}
#define NONE(ae, name, reason) ERule{ae, name, nullptr, nullptr, 0, reason, nullptr, 0, nullptr}
#define SAPPHIRE(ae, key) RULEX(ae, ae, key, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr)

const ERule kRules[] = {
    // Controles de expressão
    RULE("ADBE Slider Control", "Slider Control", effect_keys::kSliderControl, kSlider),
    RULE("ADBE Angle Control", "Angle Control", effect_keys::kAngleControl, kAngle),
    RULE("ADBE Checkbox Control", "Checkbox Control", effect_keys::kCheckboxControl, kCheckbox),
    RULE("ADBE Color Control", "Color Control", effect_keys::kColorControl, kColor),
    RULE("ADBE Point Control", "Point Control", effect_keys::kPointControl, kPoint),
    RULE("ADBE Dropdown Control", "Dropdown Menu Control", effect_keys::kDropdownControl, kDropdown),
    RULE("ADBE Layer Control", "Layer Control", effect_keys::kLayerControl, kLayerControl),
    RULE("ADBE Point3D Control", "3D Point Control", effect_keys::kPoint3DControl, kPoint3D),
    // Transformar e geradores
    RULEX("ADBE Geometry2", "Transform", effect_keys::kTransform, kGeometry2, nullptr, nullptr, 0, post_geometry2),
    RULEX("ADBE Fractal Noise", "Fractal Noise", effect_keys::kFractalNoise, kFractalNoise,
          "escala do ruído convertida pela escala base do Aurea (inferido)", nullptr, 0, nullptr),
    RULE("ADBE Ramp", "Gradient Ramp", effect_keys::kGradientRamp, kRamp),
    RULE("ADBE 4ColorGradient", "4-Color Gradient", effect_keys::kFourColorGradient, kFourColor),
    // Cor
    RULE("ADBE Tritone", "Tritone", "aurea.color.gradient_map", kTritone),
    RULE("CC Toner", "CC Toner", "aurea.color.gradient_map", kToner),
    RULE("ADBE Easy Levels2", "Levels", effect_keys::kLevels, kLevels),
    RULE("ADBE Exposure2", "Exposure", effect_keys::kExposure, kAeExposure),
    RULE("ADBE Black&White", "Black & White", "aurea.color.black_white", kAeBlackWhite),
    RULEX("ADBE Vibrance", "Vibrance", effect_keys::kColorGrade, kAeVibrance,
          "vibração e saturação usam a correção de cor nativa", nullptr, 0, nullptr),
    RULEX("BCC3Hue-Sat-Lightness", "BCC Hue-Sat-Lightness", effect_keys::kColorBalanceHls, kBccHls,
          "matiz, saturação e luminosidade entram; opções de espaço de cor e recortes do plugin precisam de revisão",
          nullptr, 0, nullptr),
    RULE("ADBE Tint", "Tint", effect_keys::kTint, kTint),
    RULE("ADBE Fill", "Fill", effect_keys::kFill, kFill),
    RULE("ADBE Invert", "Invert", effect_keys::kInvert, kInvert),
    RULE("ADBE HUE SATURATION", "Hue/Saturation", effect_keys::kHueSaturation, kHueSaturation),
    RULE("ADBE Color Balance (HLS)", "Color Balance (HLS)", effect_keys::kColorBalanceHls, kBalanceHls),
    RULEX("APC Colorama", "Colorama", effect_keys::kColorama, kColorama,
          "o ciclo de cores do AE não é lido: o Aurea usa o arco-íris padrão", nullptr, 0, nullptr),
    RULE("ADBE Posterize", "Posterize", "aurea.color.posterize", kPosterize),
    RULE("ADBE Threshold2", "Threshold", "aurea.color.threshold", kThreshold),
    RULE("ADBE CHANNEL MIXER", "Channel Mixer", effect_keys::kColorMatrix, kChannelMixer),
    RULEX("ADBE Shift Channels", "Shift Channels", effect_keys::kColorMatrix, kShiftChannels, nullptr, nullptr, 0,
          post_shift_channels),
    RULE("ADBE Brightness & Contrast 2", "Brightness & Contrast", effect_keys::kBrightnessContrast, kBrightnessContrast),
    // Desfoque
    RULE("ADBE Gaussian Blur 2", "Gaussian Blur", effect_keys::kGaussianBlur, kGaussian),
    RULE("ADBE Gaussian Blur", "Gaussian Blur (Legacy)", effect_keys::kGaussianBlur, kGaussian),
    RULE("ADBE Fast Blur", "Fast Blur (Legacy)", "aurea.blur.fast_blur_reference", kFastBlurReference),
    RULE("ADBE Sharpen", "Sharpen", effect_keys::kSharpenReference, kSharpen),
    RULEX("BCC3Unsharp Mask", "BCC Unsharp Mask", effect_keys::kUnsharp, kBccUnsharp,
          "o ganho e o raio entram na máscara de nitidez nativa; o filtro e a seleção de pixels do plugin diferem",
          nullptr, 0, nullptr),
    RULE("ADBE Box Blur2", "Fast Box Blur", effect_keys::kBoxBlur, kBoxBlur),
    RULE("ADBE Motion Blur", "Directional Blur", effect_keys::kDirectionalBlur, kDirectionalBlur),
    RULE("ADBE Radial Blur", "Radial Blur", "aurea.blur.radial_blur_reference", kRadialBlurReference),
    RULE("ADBE Upscale", "Detail-preserving Upscale", "aurea.distort.upscale_reference", kUpscaleReference),
    RULE("CC Radial Fast Blur", "CC Radial Fast Blur", effect_keys::kZoomBlur, kRadialFastBlur),
    // Luz e estilo
    RULEX("ADBE Glo2", "Glow", effect_keys::kGlow, kGlow, nullptr, nullptr, 0, post_glow),
    RULEX("PEDG", "Deep Glow", effect_keys::kDeepGlow, kDeepGlow, nullptr, kDeepGlowFixed, 1, nullptr),
    RULE("ADBE Drop Shadow", "Drop Shadow", "aurea.stylize.drop_shadow_reference", kDropShadow),
    RULE("ADBE Bevel Alpha", "Bevel Alpha", "aurea.stylize.bevel_alpha", kBevelAlpha),
    RULEX("ADBE Mosaic", "Mosaic", effect_keys::kMosaic, kMosaic, nullptr, nullptr, 0, post_mosaic),
    RULE("ADBE Minimax", "Minimax", effect_keys::kMinimax, kMinimax),
    RULE("ADBE Noise2", "Noise", effect_keys::kGrain, kNoise),
    RULE("ADBE Find Edges", "Find Edges", effect_keys::kFindEdgesReference, kFindEdges),
    RULE("ADBE Tile", "Motion Tile", effect_keys::kMotionTile, kMotionTile),
    RULE("CS Vignette", "CC Vignette", effect_keys::kVignette, kVignette),
    RULEX("S_Rays", "Rays", effect_keys::kRays, kSapphireRays,
          "raios de luz nativos; o perfil luminoso e a dispersão espectral diferem", nullptr, 0, nullptr),
    // Distorção
    RULE("ADBE Wave Warp", "Wave Warp", effect_keys::kWaveWarpReference, kWaveWarp),
    RULE("ADBE Turbulent Displace", "Turbulent Displace", "aurea.distort.turbulent_displace_reference", kTurbulentDisplace),
    RULEX("ADBE Ripple", "Ripple", effect_keys::kRipple, kRipple, "o Aurea ondula a camada inteira (sem o raio do AE)", nullptr, 0,
          nullptr),
    RULEX("ADBE Bulge", "Bulge", effect_keys::kBulge, kBulge, "raio e altura do bojo convertidos por aproximação", nullptr, 0,
          nullptr),
    RULEX("ADBE Spherize", "Spherize", effect_keys::kWarp, kSpherize, nullptr, kSpherizeFixed, 1, nullptr),
    RULE("CC Power Pin", "CC Power Pin", effect_keys::kCornerPin, kPowerPin),
    RULE("ADBE Offset", "Offset", "aurea.transform.offset", kOffset),
    RULEX("S_Shake", "Shake", effect_keys::kShake, kSapphireShake,
          "amplitudes e frequência entram; a trajetória de ruído do Aurea difere", nullptr, 0, nullptr),
    // Transições
    RULE("ADBE Linear Wipe", "Linear Wipe", effect_keys::kLinearWipe, kLinearWipe),
    RULE("CC Jaws", "CC Jaws", effect_keys::kJaws, kJaws),
    RULE("ADBE Radial Wipe", "Radial Wipe", "aurea.transition.radial_wipe_reference", kRadialWipe),
    RULE("ADBE Block Dissolve", "Block Dissolve", "aurea.transition.block_dissolve_reference", kBlockDissolve),
    RULEX("ADBE Venetian Blinds", "Venetian Blinds", effect_keys::kVenetianBlinds, kVenetian,
          "largura das faixas convertida em quantidade de faixas", nullptr, 0, nullptr),
    // Tempo
    RULE("ADBE Posterize Time", "Posterize Time", "aurea.time.posterize_reference", kPosterizeTime),
    RULE("ADBE Time Displacement", "Time Displacement", "aurea.time.time_displacement_reference", kTimeDisplacementReference),
    RULE("ADBE Echo", "Echo", effect_keys::kEchoTrail, kEcho),
    RULEX("S_RandomEdits", "Random Edits", effect_keys::kRandomEdits, kSapphireRandomEdits,
          "duração dos cortes arredondada em quadros; a ordem sorteada pelo Aurea difere", nullptr, 0, nullptr),
    RULEX("ADBE OFMotionBlur", "Pixel Motion Blur", effect_keys::kFlowMotionBlur, kPixelMotionBlur,
          "desfoque pelo movimento dos pixels do Aurea (fluxo óptico próprio)", nullptr, 0, nullptr),
    // Chave
    RULEX("Keylight 906", "Keylight (1.2)", effect_keys::kChromaKey, kKeylight,
          "Keylight: só a cor da tela entra; o resto é a chave de croma do Aurea", nullptr, 0, nullptr),
    // Áudio
    RULE("ADBE Aud Tone", "Tone", "aurea.audio.tone", kTone),
    RULE("ADBE Aud Reverb", "Reverb", "aurea.audio.room_reverb", kReverb),

    // AE 2026: efeitos novos do Aurea (AeEffects2026*.cpp) e os da 0.4.6.
    RULE("ADBE Leave Color", "Leave Color", "aurea.color.leave_color", kLeaveColor),
    RULE("ADBE Circle", "Circle", "aurea.generate.circle", kCircle),
    RULE("ADBE Color Emboss", "Color Emboss", "aurea.stylize.color_emboss", kColorEmboss),
    RULE("ADBE Noise Alpha2", "Noise Alpha", "aurea.stylize.noise_alpha", kNoiseAlpha),
    RULE("ADBE Remove Color Matting", "Remove Color Matting", "aurea.color.remove_color_matting", kRemoveColorMatting),
    RULEX("ADBE Magnify", "Magnify", "aurea.distort.magnify", kMagnify,
          "lupa redonda do Aurea: tamanho vira raio em % da altura (forma, ligação e escala do AE ficam de fora)",
          nullptr, 0, nullptr),
    RULE("ADBE Polar Coordinates", "Polar Coordinates", "aurea.distort.polar_coordinates", kPolarCoordinates),
    RULEX("ADBE Roughen Edges", "Roughen Edges", "aurea.stylize.roughen_edges", kRoughenEdges,
          "bordas ásperas do Aurea: tipo de borda, influência fractal e escala do AE não são lidos", nullptr, 0, nullptr),
    RULEX("ADBE Lightning 2", "Advanced Lightning", "aurea.generate.lightning", kAdvancedLightning,
          "raio do Aurea: origem, direção, raios do núcleo e do brilho e a cor do brilho", nullptr, 0, nullptr),
    RULEX("ADBE Cell Pattern", "Cell Pattern", "aurea.generate.voronoi_cells", kCellPattern,
          "células do Aurea: tamanho e evolução; o estilo do AE não é lido", nullptr, 0, nullptr),
    RULEX("ADBE Twirl", "Twirl", "aurea.distort.swirl", kTwirl, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Emboss", "Emboss", "aurea.stylize.emboss", kEmboss, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Unmult", "Unmult", "aurea.key.unmult", kUnmult, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Change Color", "Change Color", "aurea.color.change_color", kChangeColor, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Change To Color", "Change to Color", "aurea.color.change_to_color", kChangeToColor, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Alpha Levels2", "Alpha Levels", "aurea.color.alpha_levels", kAlphaLevels, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Arithmetic", "Arithmetic", "aurea.color.arithmetic", kArithmetic, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Bilateral", "Bilateral Blur", "aurea.blur.bilateral", kBilateral, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Smart Blur", "Smart Blur", "aurea.blur.smart", kSmartBlur, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Brush Strokes", "Brush Strokes", "aurea.stylize.brush_strokes", kBrushStrokes, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULE("ADBE Radial Shadow", "Radial Shadow", "aurea.stylize.radial_shadow_reference", kRadialShadowReference),
    RULE("PEMUEXTRUDE", "Extrude", "aurea.generate.extrude_reference", kExtrudeReference),
    RULEX("ADBE DEPTH FIELD", "Depth of Field", "aurea.ai.depth_field_reference", kDepthFieldReference,
          "a profundidade vem da IA do Aurea, não do canal 3D do render", nullptr, 0, nullptr),
    RULE("ADBE SCHMEAR", "Smear", "aurea.distort.smear_mask_reference", kSmearReference),
    RULE("CSpice Glitchify", "Glitchify", "aurea.glitch.glitchify_reference", kGlitchifyReference),
    RULEX("ADBE Bevel Edges", "Bevel Edges", "aurea.stylize.bevel_edges", kBevelEdges, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE ELLIPSE", "Ellipse", "aurea.generate.ellipse", kEllipse, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Eyedropper Fill", "Eyedropper Fill", "aurea.generate.eyedropper_fill", kEyedropperFill, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0,
          nullptr),
    RULEX("ADBE Strobe", "Strobe Light", "aurea.stylize.strobe_light", kStrobe, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Noise HLS2", "Noise HLS", "aurea.stylize.noise_hls", kNoiseHls, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Noise HLS Auto2", "Noise HLS Auto", "aurea.stylize.noise_hls_auto", kNoiseHlsAuto, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0,
          nullptr),
    RULEX("ADBE Blend", "Blend", "aurea.color.blend_layers", kBlend,
          "a camada de mesclagem do AE não é lida: escolha a camada no Aurea", nullptr, 0, nullptr),
    RULEX("ADBE Difference Matte2", "Difference Matte", "aurea.key.difference_matte", kDifferenceMatte,
          "a camada de diferença do AE não é lida: escolha a camada no Aurea", nullptr, 0, nullptr),
    RULEX("ADBE Color Range", "Color Range", "aurea.key.color_range", kColorRange, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE DEPTH MATTE", "Depth Matte", "aurea.ai.depth_matte", kDepthMatte,
          "a profundidade vem da IA do Aurea, não do canal 3D do render", nullptr, 0, nullptr),
    RULEX("ADBE FOG_3D", "Fog 3D", "aurea.ai.fog_3d", kFog3D, "a profundidade vem da IA do Aurea, não do canal 3D do render",
          nullptr, 0, nullptr),
    RULEX("ADBE Aud BT", "Bass & Treble", "aurea.audio.bass_treble", kBassTreble, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE WRPMESH", "Warp", "aurea.distort.warp_styles", kWarp, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Grid Wipe", "CC Grid Wipe", "aurea.transition.grid_wipe", kGridWipe, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Light Wipe", "CC Light Wipe", "aurea.transition.light_wipe", kLightWipe, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Line Sweep", "CC Line Sweep", "aurea.transition.line_sweep", kLineSweep, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Rain", "CC Rain", "aurea.generate.rain", kRain, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Snow", "CC Snow", "aurea.generate.snow", kSnow, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Cross Blur", "CC Cross Blur", "aurea.blur.cross", kCrossBlur, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Simple Wire Removal", "CC Simple Wire Removal", "aurea.key.simple_wire_removal", kWireRemoval, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)",
          nullptr, 0, nullptr),
    RULEX("ADBE PSL Inner Shadow", "Photoshop Inner Shadow", "aurea.stylize.inner_shadow", kInnerShadow, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0,
          nullptr),

    // Segunda leva do lote "AE 2026" (AeEffects2026Extra.cpp).
    RULEX("ADBE Channel Combiner", "Channel Combiner", "aurea.color.channel_combiner", kChannelCombiner,
          "a segunda camada do AE não é lida; os destinos \"só o canal\" viram o próprio canal", nullptr, 0, nullptr),
    RULEX("ADBE Broadcast Colors", "Broadcast Colors", "aurea.color.broadcast_colors", kBroadcastColors, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Compound Arithmetic", "Compound Arithmetic", "aurea.color.compound_arithmetic", kCompoundArithmetic,
          "a segunda camada do AE não é lida: escolha a camada no Aurea", nullptr, 0, nullptr),
    RULEX("ADBE 3D Glasses2", "3D Glasses", "aurea.stylize.glasses_3d", kGlasses3D,
          "a camada é a vista esquerda; a direita do AE não é lida (escolha no Aurea); unidades em px", nullptr, 0, nullptr),
    RULEX("CC Glass Wipe", "CC Glass Wipe", "aurea.transition.glass_wipe", kGlassWipe,
          "as camadas do AE não são lidas: escolha a revelada e a do gradiente no Aurea", nullptr, 0, nullptr),
    RULEX("CC WarpoMatic", "CC WarpoMatic", "aurea.transition.warpomatic", kWarpoMatic,
          "a camada a revelar do AE não é lida: escolha no Aurea", nullptr, 0, nullptr),
    RULEX("CC Environment", "CC Environment", "aurea.stylize.environment", kEnvironment,
          "a camada de ambiente do AE não é lida: escolha no Aurea", nullptr, 0, nullptr),
    RULEX("CC Threads", "CC Threads", "aurea.stylize.threads", kThreads, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("CC Color Neutralizer", "CC Color Neutralizer", "aurea.color.color_neutralizer", kColorNeutralizer, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0,
          nullptr),
    RULEX("CC Overbrights", "CC Overbrights", "aurea.color.overbrights", kOverbrights,
          "o canal do AE não é lido (sem a lista do menu): fica RGB", nullptr, 0, nullptr),
    RULEX("ADBE Aud Distortion", "Distortion", "aurea.audio.distortion", kAudDistortion,
          "curvas do Aurea com os nomes do AE; índices dos grupos pela ordem do .aex (sem preset para conferir)", nullptr, 0, nullptr),
    RULEX("ADBE Aud Gate", "Gate", "aurea.audio.gate", kAudGate, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),

    // 1.0.8: plugins e efeitos que antes ficavam "sem equivalente".
    RULEX("ADBE Unsharp Mask", "Unsharp Mask", effect_keys::kUnsharp, kAeUnsharp,
          "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Unsharp Mask2", "Unsharp Mask", effect_keys::kUnsharp, kAeUnsharp,
          "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
    RULEX("ADBE Camera Lens Blur", "Camera Lens Blur", effect_keys::kLensBlur, kCameraLensBlur, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("ADBE Lumetri", "Lumetri Color", effect_keys::kColorGrade, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("ADBE CurvesCustom", "Curves", effect_keys::kCurves, kApprox,
          "curva aproximada: os pontos da curva do AE não são lidos; a curva do Aurea entrou neutra para ajustar",
          nullptr, 0, nullptr),
    RULEX("ADBE Displacement Map", "Displacement Map", effect_keys::kDisplacementMap, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("ADBE Optics Compensation", "Optics Compensation", effect_keys::kOpticsCompensation, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("CC Scale Wipe", "CC Scale Wipe", effect_keys::kScaleWipe, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("CC Light Rays", "CC Light Rays", effect_keys::kRays, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("MB LookSuite3", "Looks", "aurea.color.looks", kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("RG MB COLORISTA IV", "Colorista", effect_keys::kColorGrade, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("Twixtor Pro", "Twixtor Pro", effect_keys::kFlowRetime, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", kRetimeFixed, 1, nullptr),
    RULEX("RSMB", "RSMB", effect_keys::kFlowMotionBlur, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("CROSSPHERE_Bokeh", "Bokeh", effect_keys::kLensBlur, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("Universe_Blur_Box_Bokeh", "uni.Bokeh", effect_keys::kLensBlur, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("DRFL Depth of Field", "FL Depth Of Field", effect_keys::kLensBlur, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("DRFL RadialBlur", "FL Radial Blur", "aurea.blur.radial_blur_reference", kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("FilmConvert Nitrate", "FilmConvert Nitrate", "aurea.stylize.film_profile_grain", kApprox,
          "parâmetros aproximados: só o grão de película entra; a emulação de cor do FilmConvert não", nullptr, 0, nullptr),
    RULEX("BCC Magic Sharp", "BCC Magic Sharp", effect_keys::kUnsharp, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("BCC Fast Film Glow", "BCC Fast Film Glow", effect_keys::kGlow, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("Universe_Stylize_Glitch", "uni.Glitch", "aurea.glitch.video_glitch", kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("com.FXHOME.HitFilm.Flicker", "HitFilm Flicker", "aurea.motion.flicker", kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),
    RULEX("com.FXHOME.HitFilm.Echo", "HitFilm Echo", effect_keys::kEchoTrail, kApprox, "parâmetros aproximados: o que o Aurea não lê do plugin ficou no padrão do Aurea", nullptr, 0, nullptr),

    // Sapphire (docs/release/1.0.7-effects.md): o efeito do Aurea de cada um,
    // com os padrões do Aurea. S_Rays, S_Shake e S_RandomEdits têm tabela acima.
    SAPPHIRE("S_Invert", "aurea.color.invert"), SAPPHIRE("S_Threshold", "aurea.color.soft_rgb_threshold"),
    SAPPHIRE("S_Gamma", "aurea.color.levels"), SAPPHIRE("S_Tint", "aurea.color.tint"),
    SAPPHIRE("S_DuoTone", "aurea.color.duotone"), SAPPHIRE("S_TriTone", "aurea.color.gradient_map"),
    SAPPHIRE("S_QuadTone", "aurea.color.quadtone"), SAPPHIRE("S_Monochrome", "aurea.color.black_white"),
    SAPPHIRE("S_HueSatBright", "aurea.color.hue_saturation"), SAPPHIRE("S_ChannelSwitcher", "aurea.color.shift_channels"),
    SAPPHIRE("S_Posterize", "aurea.color.luma_posterize"), SAPPHIRE("S_PseudoColor", "aurea.color.luma_hue_cycle"),
    SAPPHIRE("S_BleachBypass", "aurea.color.silver_retention"), SAPPHIRE("S_Solarize", "aurea.color.solarize"),
    SAPPHIRE("S_ClampChroma", "aurea.color.video_limiter"), SAPPHIRE("S_MathOps", "aurea.color.arithmetic"),
    SAPPHIRE("S_MatteOps", "aurea.key.matte_refine"), SAPPHIRE("S_ShowBadColors", "aurea.color.overbrights"),
    SAPPHIRE("S_Vignette", "aurea.stylize.optical_vignette"), SAPPHIRE("S_Blur", "aurea.blur.photographic"),
    SAPPHIRE("S_BlurChannels", "aurea.blur.rgb_channels"), SAPPHIRE("S_BlurChroma", "aurea.blur.chroma_luma"),
    SAPPHIRE("S_BlurDirectional", "aurea.blur.photographic_direction"), SAPPHIRE("S_BlurMoCurves", "aurea.blur.motion_curves"),
    SAPPHIRE("S_BlurMotion", "aurea.blur.transform_motion"), SAPPHIRE("S_AdvancedDefocus", "aurea.blur.lens"),
    SAPPHIRE("S_DefocusPrism", "aurea.blur.prism_defocus"), SAPPHIRE("S_EdgeAwareBlur", "aurea.blur.bilateral"),
    SAPPHIRE("S_EdgeBlur", "aurea.blur.edge_masked"), SAPPHIRE("S_RackDefocus", "aurea.blur.rack_defocus"),
    SAPPHIRE("S_Sharpen", "aurea.blur.detail_sharpen"), SAPPHIRE("S_SoftFocus", "aurea.blur.soft_focus"),
    SAPPHIRE("S_Median", "aurea.blur.median"), SAPPHIRE("S_Glow", "aurea.light.chromatic_halo"),
    SAPPHIRE("S_GlowAura", "aurea.light.spectral_aura"), SAPPHIRE("S_GlowDarks", "aurea.light.shadow_halo"),
    SAPPHIRE("S_GlowEdges", "aurea.light.outline_halo"), SAPPHIRE("S_GlowRainbow", "aurea.light.rainbow_halo"),
    SAPPHIRE("S_GlowRings", "aurea.light.glow_rings"), SAPPHIRE("S_UltraGlow", "aurea.light.multiscale_glow"),
    SAPPHIRE("S_Glare", "aurea.light.diffraction_glare"), SAPPHIRE("S_Glint", "aurea.light.spectral_glint"),
    SAPPHIRE("S_GlintRainbow", "aurea.light.prismatic_glints"), SAPPHIRE("S_LensFlare", "aurea.light.lens_flare"),
    SAPPHIRE("S_EdgeRays", "aurea.light.outline_rays"), SAPPHIRE("S_SpotLight", "aurea.light.spotlight"),
    SAPPHIRE("S_Hotspots", "aurea.light.hotspots"), SAPPHIRE("S_Streaks", "aurea.light.exposure_streaks"),
    SAPPHIRE("S_LightLeak", "aurea.light.light_leak"), SAPPHIRE("S_Caustics", "aurea.light.caustics"),
    SAPPHIRE("S_DropShadow", "aurea.stylize.drop_shadow_reference"), SAPPHIRE("S_Flicker", "aurea.motion.flicker"),
    SAPPHIRE("S_MotionDetect", "aurea.time.motion_detect"), SAPPHIRE("S_Retime", "aurea.time.flow_retime"),
    SAPPHIRE("S_TimeDisplace", "aurea.time.time_displacement_reference"), SAPPHIRE("S_TimeSlice", "aurea.time.slice"),
    SAPPHIRE("S_TimeWarpRGB", "aurea.time.warp_rgb"), SAPPHIRE("S_TimeAverage", "aurea.time.wide_time"),
    SAPPHIRE("S_Trails", "aurea.time.echo"), SAPPHIRE("S_PixelSort", "aurea.stylize.pixel_sort"),
    SAPPHIRE("S_PixelMosh", "aurea.glitch.datamosh"), SAPPHIRE("S_JpegDamage", "aurea.stylize.jpeg_compression_damage"),
    SAPPHIRE("S_VHSDamage", "aurea.stylize.analog_tape_damage"), SAPPHIRE("S_TVDamage", "aurea.glitch.signal_reference"),
    SAPPHIRE("S_FilmDamage", "aurea.stylize.photochemical_film_damage"), SAPPHIRE("S_Grain", "aurea.stylize.photographic_grain"),
    SAPPHIRE("S_UltraGrain", "aurea.stylize.film_profile_grain"), SAPPHIRE("S_ScanLines", "aurea.stylize.sinusoidal_scanlines"),
    SAPPHIRE("S_ScanLinesMono", "aurea.stylize.sinusoidal_scanlines_mono"), SAPPHIRE("S_HalfTone", "aurea.stylize.radial_screen"),
    SAPPHIRE("S_HalfToneColor", "aurea.stylize.radial_screen_color"), SAPPHIRE("S_Mosaic", "aurea.stylize.mosaic"),
    SAPPHIRE("S_Emboss", "aurea.stylize.emboss"), SAPPHIRE("S_EdgeDetect", "aurea.stylize.chromatic_edges"),
    SAPPHIRE("S_Distort", "aurea.distort.refraction"), SAPPHIRE("S_DistortRGB", "aurea.distort.refraction_rgb"),
    SAPPHIRE("S_DistortChroma", "aurea.distort.refraction_rgb"), SAPPHIRE("S_WarpCornerPin", "aurea.distort.projective_pin"),
    SAPPHIRE("S_WarpFishEye", "aurea.distort.optical_fisheye"), SAPPHIRE("S_WarpMagnify", "aurea.distort.optical_lens"),
    SAPPHIRE("S_WarpPerspective", "aurea.distort.plane_perspective"), SAPPHIRE("S_WarpPolar", "aurea.distort.polar_coordinates"),
    SAPPHIRE("S_WarpRepeat", "aurea.distort.repeated_transform"), SAPPHIRE("S_WarpTransform", "aurea.transform.raster"),
    SAPPHIRE("S_WarpVortex", "aurea.distort.swirl"), SAPPHIRE("S_WarpWaves", "aurea.distort.sine_waves"),
    SAPPHIRE("S_WarpBubble", "aurea.distort.turbulent_displace_reference"), SAPPHIRE("S_Kaleido", "aurea.distort.kaleidoscope"),
    SAPPHIRE("S_KaleidoPolar", "aurea.distort.polar_kaleidoscope"), SAPPHIRE("S_DissolveShake", "aurea.glitch.dissolve_shake"),
    SAPPHIRE("S_DissolveLuma", "aurea.transition.luma_dissolve"), SAPPHIRE("S_DissolveBlur", "aurea.transition.blur_dissolve"),
    SAPPHIRE("S_WipeLine", "aurea.transition.range_wipe"), SAPPHIRE("S_WipeFlux", "aurea.transition.wipe_flux"),
    SAPPHIRE("S_WipePlasma", "aurea.transition.wipe_plasma"), SAPPHIRE("S_WipeCircle", "aurea.transition.circular_aperture"),

    // --- sem equivalente no Aurea (relatados com o motivo) --------------------
    RULE("ADBE Solid Composite", "Solid Composite", "aurea.key.solid_matte", kSolidComposite),
    // Cálculos existe (aurea.color.calculations), mas a importação fica de fora: a segunda camada e
    // os 30 modos do menu do AE não têm leitura confiável (os testes AeImport usam este como "desconhecido").
    NONE("ADBE Calculations", "Calculations", "a importação de Cálculos não lê a segunda camada nem os modos do AE: aplique Cálculos no Aurea"),
    NONE("ADBE Set Matte3", "Set Matte", "fosco de outra camada: use o recorte (track matte) do Aurea"),
    NONE("CS Composite", "CC Composite", "composição com o original não existe no Aurea"),
    NONE("ADBE Layer Control", "Layer Control", "controle de camada não existe no Aurea"),
    RULE("APC CardWipeCam", "Card Wipe", "aurea.transition.card_wipe_reference", kCardWipeReference),
    RULE("APC CardDanceCam", "Card Dance", "aurea.3d.card_dance_reference", kCardDanceReference),
    NONE("APC Shatter", "Shatter", "Shatter (simulação de peças) ainda não foi reconstruído no Aurea"),
    NONE("ADBE Stroke", "Stroke", "traço de máscara não existe no Aurea"),
    NONE("ADBE Channel Blur", "Channel Blur", "desfoque por canal não existe no Aurea"),
    NONE("APC Vegas", "Vegas", "traço luminoso em contornos não existe no Aurea"),
    NONE("ADBE Median", "Median (Legacy)", "filtro de mediana não existe no Aurea"),
    NONE("ADBE Dust & Scratches", "Dust & Scratches", "remoção de poeira não existe no Aurea"),
    NONE("ADBE Color Balance 2", "Color Balance", "equilíbrio de cor por faixa tonal não existe no Aurea"),
    NONE("ADBE AutoContrast", "Auto Contrast", "contraste automático não existe no Aurea"),
    NONE("ADBE Basic 3D", "Basic 3D", "use a camada 3D do Aurea"),
    NONE("ADBE Spill2", "Advanced Spill Suppressor", "use a supressão da chave de croma do Aurea"),
    NONE("ADBE KeyCleaner", "Key Cleaner", "use o refinar recorte do Aurea"),
    NONE("ADBE Difference", "Time Difference", "diferença no tempo não existe no Aurea"),
    NONE("ADBE GROW BOUNDS", "Grow Bounds", "o Aurea cresce a área do efeito sozinho"),
    NONE("ADBE Sample Image", "Sample Image", "amostragem de cor não existe no Aurea"),
    NONE("ADBE Compander", "HDR Compander", "não existe no Aurea"),
    NONE("ADBE Set Channels", "Set Channels", "canais de outras camadas não existem no Aurea"),
    NONE("APC Radio Waves", "Radio Waves", "ondas de rádio não existem no Aurea"),
    NONE("APC Wave World", "Wave World", "simulação de ondas não existe no Aurea"),
    NONE("ADBE Scribble Fill", "Scribble", "rabisco de máscara não existe no Aurea"),
    NONE("ADBE Separate XYZ Position", "Separate XYZ Position", "controle de expressão próprio do AE"),
    NONE("ADBE Separate XYZ Scale", "Separate XYZ Scale", "controle de expressão próprio do AE"),
};

#undef RULE
#undef RULEX
#undef NONE
#undef SAPPHIRE

/// Número do sufixo "-NNNN" do matchName do parâmetro (0 = não tem).
u16 suffix_number(std::string_view m, std::string_view effect) noexcept {
    if (m.size() != effect.size() + 5 || m.compare(0, effect.size(), effect) != 0 || m[effect.size()] != '-') return 0;
    u32 v = 0;
    for (usize i = effect.size() + 1; i < m.size(); ++i) {
        if (m[i] < '0' || m[i] > '9') return 0;
        v = v * 10 + static_cast<u32>(m[i] - '0');
    }
    return static_cast<u16>(std::min<u32>(v, 9999));
}

} // namespace

namespace {
/// matchNames e nomes de exibição alternativos de plugins de terceiros (a
/// versão e o fabricante mudam o matchName; o nome no painel do AE fica).
struct RuleAlias { const char* name; const char* rule; };
constexpr RuleAlias kAliases[] = {
    {"Twixtor", "Twixtor Pro"}, {"Twixtor Pro, Vectors In", "Twixtor Pro"},
    {"REVisionEffects Twixtor", "Twixtor Pro"}, {"REVisionEffects Twixtor Pro", "Twixtor Pro"},
    {"RE:Vision Twixtor Pro", "Twixtor Pro"},
    {"RSMB Pro", "RSMB"}, {"RSMB Pro Vectors", "RSMB"}, {"ReelSmart Motion Blur", "RSMB"},
    {"ReelSmart Motion Blur Pro", "RSMB"}, {"RE:Vision Smart Motion Blur", "RSMB"},
    {"REVisionEffects RSMB", "RSMB"}, {"REVisionEffects RSMB Pro", "RSMB"},
    {"Magic Bullet Looks", "MB LookSuite3"}, {"Looks", "MB LookSuite3"}, {"MB LookSuite", "MB LookSuite3"},
    {"MB LookSuite4", "MB LookSuite3"}, {"MB Looks", "MB LookSuite3"},
    {"FilmConvert", "FilmConvert Nitrate"}, {"FilmConvert Pro", "FilmConvert Nitrate"},
    {"FilmConvert Nitrate AE", "FilmConvert Nitrate"},
    {"Colorista V", "RG MB COLORISTA IV"}, {"Colorista IV", "RG MB COLORISTA IV"},
    {"VIDEOCOPILOT CROSSPHERE", "CROSSPHERE_Bokeh"}, {"CROSSPHERE Bokeh", "CROSSPHERE_Bokeh"},
    {"Lumetri Color", "ADBE Lumetri"}, {"BCC Magic Sharp OBSOLETE", "BCC Magic Sharp"},
    {"BCC Fast Film Glow OBSOLETE", "BCC Fast Film Glow"},
};

const ERule* exact_rule(std::string_view m) noexcept {
    for (const ERule& r : kRules) if (m == r.ae) return &r;
    return nullptr;
}
} // namespace

const ERule* find_effect_rule(std::string_view m) noexcept {
    if (const ERule* r = exact_rule(m)) return r;
    for (const RuleAlias& a : kAliases) if (iequals(m, a.name)) return exact_rule(a.rule);
    return nullptr;
}

const ERule* find_effect_rule_alias(std::string_view displayName) noexcept {
    for (const RuleAlias& a : kAliases) if (iequals(displayName, a.name)) return exact_rule(a.rule);
    // Plugin cujo nome no painel é o matchName de uma regra ("Twixtor Pro").
    return exact_rule(displayName);
}

const ERule* find_effect_rule_by_name(std::string_view name) noexcept {
    for (const ERule& r : kRules) if (iequals(name, r.aeName) || iequals(name, r.ae)) return &r;
    return nullptr;
}

u16 effect_param_number(const ERule& r, std::string_view s) noexcept {
    if (const u16 n = suffix_number(s, r.ae)) return n;
    for (u32 i = 0; i < r.paramCount; ++i) if (iequals(s, r.params[i].aeName) && r.params[i].aeName[0]) return r.params[i].ae;
    return 0;
}

u16 effect_param_by_position(const ERule& r, u32 position) noexcept {
    if (std::strcmp(r.ae, "PEDG") == 0) {
        // The native UI includes nested groups and help fields between controls.
        // Numeric JSX property access must retain the observed physical positions.
        struct Position { u16 ui; u16 suffix; };
        static constexpr Position positions[] = {
            {10,72},{11,73},{12,74},{13,75},
            {21,1},{22,2},{24,4},{25,5},{27,56},{28,57},{29,59},
            {33,8},{34,35},{35,11},{37,43},{38,9},{42,10},{43,52},{44,53},
            {47,13},{48,14},{49,15},{52,37},{53,38},{54,39},{55,41},
            {58,47},{59,48},{63,19},{64,20},{65,50},{66,21},{67,84},{68,85},
            {70,31},{71,32},{72,33},{75,23},{76,24},{77,25},{78,55},
            {80,66},{81,88},{82,86},{83,87},{84,89},
            {85,26},{86,27},{87,28},{88,29},
        };
        for (const Position& p : positions) if (p.ui == position) return p.suffix;
        return 0;
    }
    // Posição na interface = ordem das regras, sem repetir o mesmo parâmetro.
    u32 seen = 0;
    u16 last = 0;
    for (u32 i = 0; i < r.paramCount; ++i) {
        if (r.params[i].ae == last) continue;
        last = r.params[i].ae;
        if (++seen == position) return last;
    }
    return 0;
}

const char* aurea_param_for(const ERule& r, u16 ae) noexcept {
    for (u32 i = 0; i < r.paramCount; ++i) if (r.params[i].ae == ae && r.params[i].id) return r.params[i].id;
    return nullptr;
}

u32 rule_count() noexcept { return static_cast<u32>(std::size(kRules)); }
const ERule& rule_at(u32 i) noexcept { return kRules[std::min<u32>(i, rule_count() - 1)]; }

Binding bind_effect_param(const ERule& r, u32 effectId, u16 ae, u32 dims, const EffectRegistry& reg, f64 w, f64 h, f64 fps) {
    Binding b;
    b.label = std::string(r.aeName) + " > ";
    if (std::strcmp(r.ae, "PEDG") == 0 && ae == 11)
        b.approx = "Spread foi preservado no projeto; seu comportamento visual ainda não foi implementado";
    // CC Jaws stores Completion as a fraction in the project/preset. The
    // scripting API and the Aurea parameter both use percentages.
    if (std::strcmp(r.ae, "CC Jaws") == 0 && ae == 1) b.fileMul[0] = 100.0;
    const ParameterRegistry* specs = r.key ? reg.params(effect_type_id(r.key)) : nullptr;
    w = std::max(1.0, w);
    h = std::max(1.0, h);
    bool named = false;
    for (u32 i = 0; i < r.paramCount; ++i) {
        const PRule& p = r.params[i];
        if (p.ae != ae) continue;
        if (!named) { b.label += p.aeName; named = true; }
        if (p.conv == PConv::None) {
            if (!b.ok()) b.unsupported = p.silent ? "~" : "parâmetro sem equivalente no Aurea";
            continue;
        }
        if (!specs || !p.id) continue;
        const u32 pi = specs->find(p.id);
        if (pi == kInvalidIndex) continue;
        const ParamSpec& spec = specs->at(pi);
        const u32 comps = std::max<u32>(1, component_count(spec.type));
        b.unsupported.clear();
        auto make = [&](u32 tc, u8 src) {
            Channel c;
            c.prop = TrackProperty::EffectParam;
            c.ei = effectId;
            c.pi = param_track_key(pi, tc);
            c.src = src;
            c.mul = p.k;
            c.add = p.b;
            c.lo = spec.typed_min();
            c.hi = spec.typed_max();
            if (spec.type == ParamType::Bool) { c.lo = 0; c.hi = 1; }
            return c;
        };
        const bool point = p.conv == PConv::PointRel || p.conv == PConv::PointPx || p.conv == PConv::PointCenter;
        const bool vector = spec.type == ParamType::Point2D || spec.type == ParamType::Point3D || spec.type == ParamType::Color;
        if (point) {
            b.spatial = true;
            b.dims = std::max<u32>(b.dims, 2);
            b.fileMul[0] = w;   // o .ffx guarda fração do tamanho da camada
            b.fileMul[1] = h;
            const f64 size[2] = {w, h};
            for (u32 c = 0; c < std::min<u32>(comps, 2); ++c) {
                Channel ch = make(c, static_cast<u8>(c));
                ch.mul = 1.0;
                ch.add = 0.0;
                if (p.conv == PConv::PointRel) ch.mul = 1.0 / size[c];
                else if (p.conv == PConv::PointCenter) ch.add = -0.5 * size[c];
                b.ch.push_back(ch);
            }
            continue;
        }
        if (p.conv == PConv::PointPercent) {
            b.spatial = true;
            b.dims = std::max<u32>(b.dims, 2);
            b.fileMul[0] = w;
            b.fileMul[1] = h;
            Channel ch = make(0, p.comp);
            ch.mul = 100.0 / (p.comp == 0 ? w : h);
            ch.add = 0.0;
            b.ch.push_back(ch);
            continue;
        }
        if (p.conv == PConv::Color) {
            b.dims = 4;
            for (u32 c = 0; c < 4; ++c) {
                Channel ch = make(c, static_cast<u8>(c));
                ch.conv = c < 3 ? Conv::SrgbToLinear : Conv::Linear;
                ch.mul = 1.0; ch.add = 0.0;
                ch.lo = 0.0; ch.hi = 1.0;
                b.ch.push_back(ch);
            }
            continue;
        }
        if (vector && p.tcomp == 0xFF) {
            b.dims = std::max(b.dims, comps);
            for (u32 c = 0; c < comps; ++c) b.ch.push_back(make(c, static_cast<u8>(c)));
            continue;
        }
        Channel ch = make(p.tcomp == 0xFF ? 0u : p.tcomp, p.comp);
        switch (p.conv) {
            case PConv::Bool: ch.conv = Conv::Bool; break;
            case PConv::Enum: ch.conv = Conv::Enum; ch.map = p.map; ch.mapN = p.mapN; break;
            case PConv::Seconds: ch.mul = p.k * fps; break;
            case PConv::BlocksW: ch.conv = Conv::Reciprocal; ch.mul = w; break;
            case PConv::BlocksH: ch.conv = Conv::Reciprocal; ch.mul = h; break;
            case PConv::PxPercentMax: ch.mul = 100.0 / std::max(w, h); break;
            case PConv::PxPercentMin: ch.mul = 100.0 / std::min(w, h); break;
            default: break;
        }
        if (spec.type == ParamType::Int && ch.conv == Conv::Linear) ch.conv = Conv::Round;
        b.dims = std::max<u32>(b.dims, std::max<u32>(dims, static_cast<u32>(p.comp) + 1));
        b.ch.push_back(ch);
    }
    if (!named) b.label += "#" + std::to_string(ae);
    if (b.ch.empty() && b.unsupported.empty()) b.unsupported = "parâmetro sem equivalente no Aurea";
    return b;
}

} // namespace detail

const char* aurea_effect_for(std::string_view m) noexcept {
    const detail::ERule* r = detail::find_effect_rule(m);
    return r ? r->key : nullptr;
}

u32 table_size() noexcept { return detail::rule_count(); }

std::vector<std::string> mapping_problems(const EffectRegistry& registry) {
    std::vector<std::string> out;
    for (u32 i = 0; i < detail::rule_count(); ++i) {
        const detail::ERule& r = detail::rule_at(i);
        if (!r.key) continue;
        const ParameterRegistry* specs = registry.params(effect_type_id(r.key));
        if (!specs) { out.push_back(std::string(r.ae) + ": chave " + r.key + " não existe"); continue; }
        for (u32 k = 0; k < r.paramCount; ++k) {
            const detail::PRule& p = r.params[k];
            if (p.conv == detail::PConv::None || !p.id) continue;
            if (specs->find(p.id) == kInvalidIndex) out.push_back(std::string(r.ae) + ": parâmetro " + p.id + " não existe em " + r.key);
        }
        for (u32 k = 0; k < r.fixedCount; ++k)
            if (specs->find(r.fixed[k].id) == kInvalidIndex)
                out.push_back(std::string(r.ae) + ": fixo " + r.fixed[k].id + " não existe em " + r.key);
    }
    return out;
}

} // namespace aurea::ae
