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
constexpr i8 kWaveSquare[] = {-1, 0, 1, -1, -1, -1, -1, -1, -1, -1};
constexpr i8 kWavePin[] = {-1, 0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
constexpr i8 kTurbDisplace[] = {-1, 0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8};
constexpr i8 kTurbPinning[] = {-1, 0, -1, 1, 6, 7, 2, 3, 4, 5, -1, 8, 9, 10, -1, -1, -1, -1};
constexpr i8 kRadialWipeDir[] = {-1, 0, 1, -1};
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
    E(1, "Wave Type", "square", kWaveSquare),
    E(1, "Wave Type", "wave_type", kMinus1),
    P(2, "Wave Height", "height"),
    P(3, "Wave Width", "wavelength"),
    P(4, "Direction", "direction_angle"),
    P(5, "Wave Speed", "speed"),
    E(6, "Pinning", "pin_edges", kWavePin),
    P(7, "Phase", "phase"),
    I(8, "Antialiasing (Best Quality)"),
};
constexpr FixedValue kWaveWarpFixed[] = {{"direction", {4, 0, 0, 0}}};

constexpr PRule kNoise[] = {
    P(1, "Amount of Noise", "intensity"),
    P(2, "Use Color Noise", "monochrome", PConv::Bool, -1, 1),
    I(3, "Clip Result Values"),
};

constexpr PRule kBlockDissolve[] = {
    P(1, "Transition Completion", "completion"),
    P(2, "Block Width", "block_width"),
    P(3, "Block Height", "block_height"),
    P(4, "Feather", "feather", PConv::PxPercentMax),
    I(5, "Soft Edges (Best Quality)"),
};

constexpr PRule kDirectionalBlur[] = {
    // Direção 0 do AE = borrão vertical; o ângulo do Aurea mede o eixo do borrão.
    P(1, "Direction", "angle", PConv::Same, 1, 90),
    P(2, "Blur Length", "radius", PConv::Same, 0.5),
};

constexpr PRule kPosterizeTime[] = {P(1, "Frame Rate", "frame_rate")};

constexpr PRule kGaussian[] = {
    P(1, "Blurriness", "blurriness"),
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
    // O "Inverter" do Aurea nasce ligado = o visual padrão do AE (desligado).
    P(1, "Invert", "invert", PConv::Bool, -1, 1),
    P(2, "Blend With Original", "mix", PConv::Same, -1, 100),
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
    P(4, "Offset (Turbulence)", "offset_x", PConv::PointCenter, 1, 0, 0),
    P(4, "Offset (Turbulence)", "offset_y", PConv::PointCenter, 1, 0, 1),
    P(5, "Complexity", "complexity"),
    P(6, "Evolution", "evolution"),
    P(8, "Cycle Evolution", "cycle_evolution", PConv::Bool),
    P(9, "Cycle (in Revolutions)", "cycle"),
    P(10, "Random Seed", "seed"),
    E(12, "Pinning", "pinning", kTurbPinning),
    I(13, "Resize Layer"),
    I(14, "Antialiasing for Best Quality"),
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

constexpr PRule kRadialBlur[] = {
    P(1, "Amount", "amount"),
    P(2, "Center", "center", PConv::PointRel),
    E(3, "Type", "type", kMinus1),
    I(4, "Antialiasing (Best Quality)"),
    I(5, ""),
    I(6, "Random Seed"),
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
    P(3, "Wipe Center", "center_x", PConv::PointPercent, 1, 0, 0),
    P(3, "Wipe Center", "center_y", PConv::PointPercent, 1, 0, 1),
    E(4, "Wipe", "reverse", kRadialWipeDir),
    P(5, "Feather", "feather", PConv::PxPercentMax),
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
constexpr PRule kRadialShadow[] = {
    P(1, "Shadow Color", "shadow_color", PConv::Color),
    P(2, "Opacity", "opacity"),
    P(3, "Light Source", "light_source", PConv::PointRel),
    P(4, "Projection Distance", "projection_distance"),
    P(5, "Softness", "softness"),
    E(6, "Render", "render", kMinus1),
    P(7, "Color Influence", "color_influence"),
    P(8, "Shadow Only", "shadow_only", PConv::Bool),
    P(9, "Resize Layer", "resize_layer", PConv::Bool),
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

void post_wave_warp(EffectInstance& fx, const ParameterRegistry& s, const PostIn& in, ImportReport&) {
    // Velocidade do AE em ondas por segundo → px por quadro do Aurea.
    const f64 speed = in.get(5, 0, 0.0), width = in.get(3, 0, 40.0);
    set_param(fx, s, "speed", 0, static_cast<f32>(std::clamp(speed * width / std::max(1.0, in.fps), -5000.0, 5000.0)));
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

// --- a tabela ----------------------------------------------------------------
#define RULE(ae, name, key, params) ERule{ae, name, key, params, static_cast<u32>(std::size(params)), nullptr, nullptr, 0, nullptr}
#define RULEX(ae, name, key, params, reason, fixed, nfixed, post) \
    ERule{ae, name, key, params, static_cast<u32>(std::size(params)), reason, fixed, nfixed, post}
#define NONE(ae, name, reason) ERule{ae, name, nullptr, nullptr, 0, reason, nullptr, 0, nullptr}

const ERule kRules[] = {
    // Controles de expressão
    RULE("ADBE Slider Control", "Slider Control", effect_keys::kSliderControl, kSlider),
    RULE("ADBE Angle Control", "Angle Control", effect_keys::kAngleControl, kAngle),
    RULE("ADBE Checkbox Control", "Checkbox Control", effect_keys::kCheckboxControl, kCheckbox),
    RULE("ADBE Color Control", "Color Control", effect_keys::kColorControl, kColor),
    RULE("ADBE Point Control", "Point Control", effect_keys::kPointControl, kPoint),
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
    RULE("ADBE Box Blur2", "Fast Box Blur", effect_keys::kBoxBlur, kBoxBlur),
    RULE("ADBE Motion Blur", "Directional Blur", effect_keys::kDirectionalBlur, kDirectionalBlur),
    RULE("ADBE Radial Blur", "Radial Blur", effect_keys::kRadialBlur, kRadialBlur),
    RULE("CC Radial Fast Blur", "CC Radial Fast Blur", effect_keys::kZoomBlur, kRadialFastBlur),
    // Luz e estilo
    RULEX("ADBE Glo2", "Glow", effect_keys::kGlow, kGlow, nullptr, nullptr, 0, post_glow),
    RULE("ADBE Drop Shadow", "Drop Shadow", effect_keys::kDropShadow, kDropShadow),
    RULE("ADBE Bevel Alpha", "Bevel Alpha", "aurea.stylize.bevel_alpha", kBevelAlpha),
    RULEX("ADBE Mosaic", "Mosaic", effect_keys::kMosaic, kMosaic, nullptr, nullptr, 0, post_mosaic),
    RULE("ADBE Minimax", "Minimax", effect_keys::kMinimax, kMinimax),
    RULE("ADBE Noise2", "Noise", effect_keys::kGrain, kNoise),
    RULE("ADBE Find Edges", "Find Edges", effect_keys::kFindEdges, kFindEdges),
    RULE("ADBE Tile", "Motion Tile", effect_keys::kMotionTile, kMotionTile),
    RULE("CS Vignette", "CC Vignette", effect_keys::kVignette, kVignette),
    // Distorção
    RULEX("ADBE Wave Warp", "Wave Warp", effect_keys::kWaveWarp, kWaveWarp, nullptr, kWaveWarpFixed, 1, post_wave_warp),
    RULE("ADBE Turbulent Displace", "Turbulent Displace", effect_keys::kTurbulence, kTurbulentDisplace),
    RULEX("ADBE Ripple", "Ripple", effect_keys::kRipple, kRipple, "o Aurea ondula a camada inteira (sem o raio do AE)", nullptr, 0,
          nullptr),
    RULEX("ADBE Bulge", "Bulge", effect_keys::kBulge, kBulge, "raio e altura do bojo convertidos por aproximação", nullptr, 0,
          nullptr),
    RULEX("ADBE Spherize", "Spherize", effect_keys::kWarp, kSpherize, nullptr, kSpherizeFixed, 1, nullptr),
    RULE("CC Power Pin", "CC Power Pin", effect_keys::kCornerPin, kPowerPin),
    RULE("ADBE Offset", "Offset", "aurea.transform.offset", kOffset),
    // Transições
    RULE("ADBE Linear Wipe", "Linear Wipe", effect_keys::kLinearWipe, kLinearWipe),
    RULE("ADBE Radial Wipe", "Radial Wipe", effect_keys::kRadialWipe, kRadialWipe),
    RULE("ADBE Block Dissolve", "Block Dissolve", effect_keys::kBlockDissolve, kBlockDissolve),
    RULEX("ADBE Venetian Blinds", "Venetian Blinds", effect_keys::kVenetianBlinds, kVenetian,
          "largura das faixas convertida em quantidade de faixas", nullptr, 0, nullptr),
    // Tempo
    RULE("ADBE Posterize Time", "Posterize Time", effect_keys::kPosterizeTime, kPosterizeTime),
    RULE("ADBE Echo", "Echo", effect_keys::kEchoTrail, kEcho),
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
    RULEX("ADBE Radial Shadow", "Radial Shadow", "aurea.stylize.radial_shadow", kRadialShadow, "parâmetros pela ordem da documentação do AE (sem preset para conferir os números)", nullptr, 0, nullptr),
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

    // --- sem equivalente no Aurea (relatados com o motivo) --------------------
    RULE("ADBE Solid Composite", "Solid Composite", "aurea.key.solid_matte", kSolidComposite),
    // Cálculos existe (aurea.color.calculations), mas a importação fica de fora: a segunda camada e
    // os 30 modos do menu do AE não têm leitura confiável (os testes AeImport usam este como "desconhecido").
    NONE("ADBE Calculations", "Calculations", "a importação de Cálculos não lê a segunda camada nem os modos do AE: aplique Cálculos no Aurea"),
    NONE("ADBE CurvesCustom", "Curves", "a curva é um dado próprio do AE que o importador não lê"),
    NONE("ADBE Set Matte3", "Set Matte", "fosco de outra camada: use o recorte (track matte) do Aurea"),
    NONE("CS Composite", "CC Composite", "composição com o original não existe no Aurea"),
    NONE("ADBE Layer Control", "Layer Control", "controle de camada não existe no Aurea"),
    NONE("APC CardWipeCam", "Card Wipe", "transição de cartões 3D não existe no Aurea"),
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

const ERule* find_effect_rule(std::string_view m) noexcept {
    for (const ERule& r : kRules) if (m == r.ae) return &r;
    return nullptr;
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
