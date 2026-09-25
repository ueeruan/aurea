// =============================================================================
//  Aurea / project / AlightMotion.cpp
//
//  Efeitos do Alight Motion → preset "effects" do Aurea (regras em
//  AlightMotion.hpp). Três peças:
//
//   1. o pacote: zip mínimo (diretório central, entradas stored/deflate; o
//      inflate é o do stb_image, que o motor já compila para o PNG);
//   2. a TABELA: efeito do AM (nome curto) → chave do Aurea, parâmetro a
//      parâmetro, com a conversão de unidade. Só entra efeito que o Aurea tem
//      DE VERDADE — um "parecido" daria um visual que o arquivo não pediu;
//   3. a montagem: um `Preset` comum, escrito pelo `presets::write` e relido
//      pelo `presets::parse` antes de sair (o que sai daqui sempre aplica).
//
//  As faixas do AM não vêm no arquivo. A conversão "Auto" lê o número como
//  FRAÇÃO (0..1, o jeito do AM guardar a maioria dos controles) quando toda a
//  trilha cabe em |v| ≤ 1, e como já na unidade do Aurea quando não cabe; o
//  resultado sempre passa pela faixa do parâmetro.
// =============================================================================
#include "aurea/project/AlightMotion.hpp"

#include "aurea/core/Math.hpp"
#include "aurea/core/MiniXml.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Presets.hpp"

#include "stb_image.h"   // só o inflate (stbi_zlib_decode_noheader_buffer)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <span>

namespace aurea::presets {
namespace {

constexpr usize kMaxInput = 64u << 20;     ///< pacote inteiro
constexpr usize kMaxXml = 16u << 20;       ///< o XML (descompactado)
constexpr u32   kMaxXmlDepth = 256;
constexpr usize kMaxWarnings = 200;
constexpr usize kMaxKeys = 10'000;         ///< por propriedade

// =============================================================================
// Texto
// =============================================================================

std::string lower_alnum(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (const char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) o.push_back(static_cast<char>(std::tolower(u)));
    }
    return o;
}

/// "com.alightcreative.effects.gaussianblur2" → "gaussianblur": o último
/// pedaço, minúsculo, sem separador e sem o número de versão do AM.
std::string short_name(std::string_view id) {
    const usize dot = id.rfind('.');
    std::string s = lower_alnum(dot == std::string_view::npos ? id : id.substr(dot + 1));
    usize end = s.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(s[end - 1]))) --end;
    if (end > 0) s.resize(end);
    return s;
}

/// `list` = nomes separados por '|'; '~' na frente = "contém".
bool alias_has(std::string_view list, std::string_view name) {
    if (name.empty()) return false;
    while (!list.empty()) {
        const usize bar = list.find('|');
        std::string_view a = list.substr(0, bar);
        list = bar == std::string_view::npos ? std::string_view{} : list.substr(bar + 1);
        if (a.empty()) continue;
        if (a.front() == '~') {
            if (name.find(a.substr(1)) != std::string_view::npos) return true;
        } else if (a == name) {
            return true;
        }
    }
    return false;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool number(std::string_view s, f64& out) {
    s = trim(s);
    if (s.empty() || s.size() > 64) return false;
    const std::string tok(s);
    char* end = nullptr;
    out = std::strtod(tok.c_str(), &end);
    return end == tok.c_str() + tok.size() && std::isfinite(out);
}

/// UTF-8 inválido (XML salvo em Latin-1, arquivo corrompido) vira '?': o
/// texto atravessa as pontes, e o NSString/JNI recusaria o envelope inteiro.
std::string clean_utf8(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    usize i = 0;
    while (i < s.size()) {
        const u8 c = static_cast<u8>(s[i]);
        usize len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = len > 0 && i + len <= s.size();
        for (usize k = 1; ok && k < len; ++k) ok = (static_cast<u8>(s[i + k]) & 0xC0) == 0x80;
        if (ok) { o.append(s.data() + i, len); i += len; }
        else { o.push_back('?'); ++i; }
    }
    return o;
}

bool ends_with_nocase(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && xml::iequals(s.substr(s.size() - suffix.size()), suffix);
}

// =============================================================================
// Pacote (.zip / .amproj)
// =============================================================================

u32 rd16(const u8* p) noexcept { return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8); }
u32 rd32(const u8* p) noexcept {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) | (static_cast<u32>(p[3]) << 24);
}

bool is_zip(std::string_view d) noexcept {
    return d.size() >= 4 && d[0] == 'P' && d[1] == 'K' && ((d[2] == 3 && d[3] == 4) || (d[2] == 5 && d[3] == 6));
}

/// O maior XML com `<scene` dentro (os outros costumam ser miniaturas de
/// elemento). Entrada que não dá para ler vira aviso; nenhuma cena = erro.
bool unzip_scene(std::string_view zip, std::string& xmlOut, std::vector<std::string>& warnings, std::string& err) {
    const u8* d = reinterpret_cast<const u8*>(zip.data());
    const usize n = zip.size();
    if (n < 22) { err = "pacote zip truncado"; return false; }
    // Fim do diretório central: nos últimos 22 + 65535 (comentário) bytes.
    usize eocd = std::string_view::npos;
    const usize lo = n > 22 + 65535 ? n - (22 + 65535) : 0;
    for (usize i = n - 22 + 1; i-- > lo;) {
        if (rd32(d + i) == 0x06054b50u) { eocd = i; break; }
    }
    if (eocd == std::string_view::npos) { err = "pacote zip sem diretorio central"; return false; }
    const u32 count = rd16(d + eocd + 10);
    const u32 cdSize = rd32(d + eocd + 12);
    const u32 cdOff = rd32(d + eocd + 16);
    if (cdOff > n || cdSize > n - cdOff) { err = "diretorio do zip fora do arquivo"; return false; }
    auto warn = [&](std::string w) { if (warnings.size() < kMaxWarnings) warnings.push_back(std::move(w)); };
    usize p = cdOff;
    bool found = false;
    for (u32 k = 0; k < count; ++k) {
        if (p + 46 > n || rd32(d + p) != 0x02014b50u) { err = "diretorio do zip corrompido"; return false; }
        const u32 flags = rd16(d + p + 8), method = rd16(d + p + 10);
        const u32 csize = rd32(d + p + 20), usize_ = rd32(d + p + 24);
        const u32 nameLen = rd16(d + p + 28), extraLen = rd16(d + p + 30), commentLen = rd16(d + p + 32);
        const u32 local = rd32(d + p + 42);
        if (p + 46 + nameLen > n) { err = "diretorio do zip corrompido"; return false; }
        const std::string name = clean_utf8(std::string_view(reinterpret_cast<const char*>(d + p + 46), nameLen));
        p += 46u + nameLen + extraLen + commentLen;
        if (!ends_with_nocase(name, ".xml")) continue;
        if (flags & 1u) { warn("entrada criptografada ignorada: " + name); continue; }
        if (usize_ > kMaxXml || csize == 0xFFFFFFFFu) { warn("XML grande demais no pacote: " + name); continue; }
        if (static_cast<usize>(local) + 30 > n || rd32(d + local) != 0x04034b50u) { warn("entrada do zip ilegivel: " + name); continue; }
        const usize data = static_cast<usize>(local) + 30 + rd16(d + local + 26) + rd16(d + local + 28);
        if (data > n || csize > n - data) { warn("entrada do zip truncada: " + name); continue; }
        std::string text(usize_, '\0');
        if (method == 0) {
            if (csize != usize_) { warn("entrada do zip inconsistente: " + name); continue; }
            if (usize_ > 0) std::memcpy(text.data(), d + data, usize_);
        } else if (method == 8) {
            const int got = usize_ == 0 ? 0
                : stbi_zlib_decode_noheader_buffer(text.data(), static_cast<int>(usize_),
                                                   reinterpret_cast<const char*>(d + data), static_cast<int>(csize));
            if (got != static_cast<int>(usize_)) { warn("entrada do zip nao descompactou: " + name); continue; }
        } else {
            warn("compressao do zip nao suportada (" + std::to_string(method) + "): " + name);
            continue;
        }
        if (text.find("<scene") == std::string::npos) continue;
        if (!found || text.size() > xmlOut.size()) { xmlOut = std::move(text); found = true; }
    }
    if (!found) { err = "o pacote nao tem uma cena do Alight Motion em XML"; return false; }
    return true;
}

// =============================================================================
// Valores do AM
// =============================================================================

/// Número, vetor ("x,y"), booleano ou cor ("#aarrggbb", sRGB → RGBA linear,
/// que é como o Aurea guarda cor de efeito).
struct AmValue {
    f64  v[4]{0.0, 0.0, 0.0, 0.0};
    u32  n = 0;
    bool color = false;
};

bool parse_value(std::string_view s, AmValue& out) {
    s = trim(s);
    out = AmValue{};
    if (s.empty()) return false;
    if (s.front() == '#') {
        std::string_view h = s.substr(1);
        if (h.size() != 6 && h.size() != 8) return false;
        u32 x = 0;
        for (const char c : h) {
            x <<= 4;
            if (c >= '0' && c <= '9') x |= static_cast<u32>(c - '0');
            else if (c >= 'a' && c <= 'f') x |= static_cast<u32>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') x |= static_cast<u32>(c - 'A' + 10);
            else return false;
        }
        if (h.size() == 6) x |= 0xFF000000u;
        const f32 a = static_cast<f32>((x >> 24) & 0xFF) / 255.0f;
        out.v[0] = Color::srgb_to_linear(static_cast<f32>((x >> 16) & 0xFF) / 255.0f);
        out.v[1] = Color::srgb_to_linear(static_cast<f32>((x >> 8) & 0xFF) / 255.0f);
        out.v[2] = Color::srgb_to_linear(static_cast<f32>(x & 0xFF) / 255.0f);
        out.v[3] = a;
        out.n = 4;
        out.color = true;
        return true;
    }
    if (xml::iequals(s, "true")) { out.v[0] = 1.0; out.n = 1; return true; }
    if (xml::iequals(s, "false")) { out.v[0] = 0.0; out.n = 1; return true; }
    while (!s.empty() && out.n < 4) {
        const usize comma = s.find(',');
        if (!number(s.substr(0, comma), out.v[out.n])) return false;
        ++out.n;
        s = comma == std::string_view::npos ? std::string_view{} : s.substr(comma + 1);
    }
    return out.n > 0;
}

struct AmKey {
    f64         t = 0.0;   ///< 0..1 na duração da camada
    AmValue     v;
    std::string e;         ///< curva de CHEGADA neste keyframe
};

struct AmProp {
    std::string name;      ///< como veio (para os avisos)
    std::string norm;      ///< minúsculo, sem separador
    AmValue     value;     ///< valor parado (o de t = 0 quando há keyframes)
    std::vector<AmKey> keys;
};

bool read_prop(const xml::Node& n, AmProp& p) {
    if (const std::string* nm = n.attr_nocase("name")) p.name = *nm;
    p.norm = lower_alnum(p.name);
    if (p.norm.empty()) return false;
    bool has = false;
    if (const std::string* v = n.attr_nocase("value")) has = parse_value(*v, p.value);
    for (const xml::Node& k : n.kids) {
        if (!xml::iequals(k.name, "kf") || p.keys.size() >= kMaxKeys) continue;
        const std::string* ts = k.attr_nocase("t");
        const std::string* vs = k.attr_nocase("v");
        AmKey key;
        if (!ts || !vs || !number(*ts, key.t) || !parse_value(*vs, key.v)) continue;
        key.t = std::clamp(key.t, 0.0, 1.0);
        if (const std::string* es = k.attr_nocase("e")) key.e = *es;
        p.keys.push_back(std::move(key));
    }
    std::stable_sort(p.keys.begin(), p.keys.end(), [](const AmKey& a, const AmKey& b) { return a.t < b.t; });
    if (!p.keys.empty()) {
        p.value = p.keys.front().v;
        has = true;
    }
    return has;
}

// =============================================================================
// A tabela
// =============================================================================

enum class Conv : u8 {
    Same,       ///< v × k
    Auto,       ///< trilha toda em |v| ≤ 1 → fração: v × k; senão já na unidade: v × kBig
    Color,      ///< cor (já linear)
    Bool,
    Int,        ///< v × k, arredondado
    Seconds,    ///< segundos → quadros (× fps)
    PointRel,   ///< px da cena → fração 0..1 (valores já em 0..1 passam direto)
};

struct ParamRule {
    const char* am;       ///< nomes do AM, '|' (minúsculas, sem '_'/'-'/espaço)
    const char* id;       ///< id do parâmetro no Aurea
    Conv        conv = Conv::Same;
    f64         k = 1.0;
    f64         kBig = 1.0;
    u32         comp = 0; ///< componente do valor do AM (vec2 → dois parâmetros)
};

struct FixedValue {
    const char* id;
    f32         v;
};

struct EffectRule {
    const char* names;    ///< nomes curtos do AM, '|'; '~' = contém
    const char* key;      ///< chave do Aurea
    std::span<const ParamRule> params;
    std::span<const FixedValue> fixed = {};
    const char* ignore = "";   ///< parâmetros do AM sem efeito visual aqui (sem aviso)
};

// --- parâmetros por efeito (ids conferidos em effects/builtin/*.cpp) ---------

constexpr ParamRule kChromaKeyP[] = {
    {"color|keycolor|key", "key_color", Conv::Color},
    {"tolerance|threshold|strength|range|similarity", "tolerance", Conv::Auto, 100.0},
    {"softness|feather|smoothness|edge|blend", "softness", Conv::Auto, 100.0},
    {"spill|spillsuppression|despill", "spill", Conv::Auto, 100.0},
};
constexpr ParamRule kLumaKeyP[] = {
    {"threshold|tolerance|strength", "threshold", Conv::Auto, 100.0},
    {"softness|feather|smoothness", "softness", Conv::Auto, 100.0},
};
constexpr ParamRule kBrightnessContrastP[] = {
    {"brightness", "brightness", Conv::Auto, 100.0},
    {"contrast", "contrast", Conv::Auto, 100.0},
};
constexpr ParamRule kExposureP[] = {
    {"exposure|stops|amount|strength", "exposure", Conv::Same},
    {"offset", "offset", Conv::Same},
    {"gamma", "gamma", Conv::Same},
};
constexpr ParamRule kLevelsP[] = {
    {"inputblack|inblack|blackin|blackinput|blacks", "input_black", Conv::Auto, 255.0},
    {"inputwhite|inwhite|whitein|whiteinput|whites", "input_white", Conv::Auto, 255.0},
    {"gamma|midtones", "gamma", Conv::Same},
    {"outputblack|outblack|blackout|blackoutput", "output_black", Conv::Auto, 255.0},
    {"outputwhite|outwhite|whiteout|whiteoutput", "output_white", Conv::Auto, 255.0},
};
constexpr ParamRule kSaturationP[] = {
    {"saturation|amount|strength|vibrance", "saturation", Conv::Auto, 100.0},
};
constexpr ParamRule kInvertP[] = {
    {"alpha|invertalpha", "invert_alpha", Conv::Bool},
};
/// Preenchimento sólido = tingir com preto E branco na mesma cor (a
/// transparência fica): o mesmo resultado, sem efeito novo.
constexpr ParamRule kFillP[] = {
    {"color|fillcolor", "map_black", Conv::Color},
    {"color|fillcolor", "map_white", Conv::Color},
    {"strength|amount|intensity|opacity|mix", "amount", Conv::Auto, 100.0},
};
constexpr ParamRule kTintP[] = {
    {"color|tintcolor", "map_white", Conv::Color},
    {"strength|amount|intensity|mix", "amount", Conv::Auto, 100.0},
};
constexpr ParamRule kLensBlurP[] = {
    {"strength|amount|intensity", "radius", Conv::Auto, 100.0},
    {"radius|size|blurriness", "radius", Conv::Same},
};
constexpr ParamRule kGaussianP[] = {
    {"strength|amount|intensity", "blurriness", Conv::Auto, 100.0},
    {"radius|size|blurriness|blurradius", "blurriness", Conv::Same},
};
constexpr FixedValue kBlurHorizontal[] = {{"dimensions", 1.0f}};
constexpr FixedValue kBlurVertical[] = {{"dimensions", 2.0f}};
constexpr ParamRule kUnsharpP[] = {
    {"strength|amount|intensity", "amount", Conv::Auto, 100.0},
    {"radius|size", "radius", Conv::Same},
    {"threshold", "threshold", Conv::Auto, 100.0},
};
constexpr ParamRule kSharpenP[] = {
    {"strength|amount|intensity|sharpness", "amount", Conv::Auto, 100.0},
};
constexpr ParamRule kGlowP[] = {
    {"intensity|strength|amount|brightness", "intensity", Conv::Auto, 2.0},
    {"radius|size|spread|blur", "radius", Conv::Auto, 100.0},
    {"threshold", "threshold", Conv::Auto, 100.0},
    {"color|glowcolor", "color", Conv::Color},
};
constexpr ParamRule kRaysP[] = {
    {"intensity|strength|amount", "intensity", Conv::Auto, 2.0},
    {"length", "length", Conv::Auto, 100.0},
    {"threshold", "threshold", Conv::Auto, 100.0},
    {"decay|falloff", "decay", Conv::Auto, 100.0},
    {"center|position|origin|source", "center", Conv::PointRel},
    {"color", "color", Conv::Color},
};
constexpr ParamRule kShakeP[] = {
    {"strength|amplitude|amount|intensity|magnitude", "amplitude_x", Conv::Auto, 100.0, 1.0, 0},
    {"strength|amplitude|amount|intensity|magnitude", "amplitude_y", Conv::Auto, 100.0, 1.0, 1},
    {"strengthx|amplitudex|horizontal", "amplitude_x", Conv::Auto, 100.0},
    {"strengthy|amplitudey|vertical", "amplitude_y", Conv::Auto, 100.0},
    {"speed|frequency|rate", "frequency", Conv::Same},
    {"seed|randomseed", "seed", Conv::Int},
    {"rotation|angle|rotate", "rotation", Conv::Same},
};
constexpr ParamRule kTurbulenceP[] = {
    {"strength|amount|intensity|amplitude", "amount", Conv::Auto, 100.0},
    {"size|scale", "size", Conv::Same},
    {"complexity|detail|octaves", "complexity", Conv::Same},
    {"evolution|speed", "evolution", Conv::Same},
    {"seed|randomseed", "seed", Conv::Int},
};
constexpr ParamRule kWaveP[] = {
    {"amplitude|strength|height|amount", "height", Conv::Auto, 100.0},
    {"wavelength|length|size", "wavelength", Conv::Same},
    {"speed", "speed", Conv::Same},
    {"phase", "phase", Conv::Auto, 360.0},
};
constexpr ParamRule kWarpP[] = {
    {"strength|amount|angle|twist|intensity", "amount", Conv::Auto, 100.0},
    {"radius|size", "radius", Conv::Auto, 500.0},
    {"center|position|origin", "center", Conv::PointRel},
};
constexpr FixedValue kWarpPush[] = {{"mode", 0.0f}};
constexpr FixedValue kWarpPull[] = {{"mode", 1.0f}};
constexpr FixedValue kWarpTwist[] = {{"mode", 2.0f}};
constexpr FixedValue kWarpSphere[] = {{"mode", 3.0f}};
constexpr ParamRule kEchoP[] = {
    {"count|copies|echoes|number|numechoes|repeat", "copies", Conv::Int},
    {"delay|interval|time|spacing", "delay", Conv::Seconds},
    {"decay|falloff|fade|intensity|opacity", "decay", Conv::Auto, 1.0, 0.01},
};
constexpr ParamRule kPosterizeTimeP[] = {
    {"fps|framerate|rate|frames", "frame_rate", Conv::Same},
};
constexpr ParamRule kVhsP[] = {
    {"strength|intensity|amount|mix", "mix", Conv::Auto, 100.0},
    {"noise", "noise", Conv::Auto, 100.0},
    {"blur", "blur", Conv::Auto, 10.0},
    {"distortion|tracking|jitter|wave", "tracking", Conv::Auto, 100.0},
    {"chroma|colorbleed|bleed", "bleed", Conv::Auto, 100.0},
    {"seed|randomseed", "seed", Conv::Int},
};
constexpr ParamRule kGlitchP[] = {
    {"strength|intensity|amount|displacement|displace", "displace", Conv::Auto, 100.0},
    {"split|rgbsplit|colorsplit|chromatic", "channel_split", Conv::Auto, 50.0},
    {"blocksize|bandheight|height", "band_height", Conv::Auto, 100.0},
    {"seed|randomseed", "seed", Conv::Int},
    {"mix|opacity", "mix", Conv::Auto, 100.0},
};
constexpr ParamRule kGrainP[] = {
    {"strength|amount|intensity|opacity", "intensity", Conv::Auto, 100.0},
    {"size|grainsize|scale", "grain_size", Conv::Same},
    {"monochrome|mono|grayscale|greyscale", "monochrome", Conv::Bool},
    {"animated|animate", "animated", Conv::Bool},
    {"seed|randomseed", "seed", Conv::Int},
};
constexpr ParamRule kScanlinesP[] = {
    {"size|height|thickness|spacing|linesize", "height", Conv::Same},
    {"strength|intensity|opacity|amount", "intensity", Conv::Auto, 100.0},
    {"speed", "speed", Conv::Same},
    {"color", "color", Conv::Color},
};
constexpr ParamRule kHalftoneP[] = {
    {"size|dotsize|cell|scale", "cell", Conv::Same},
    {"angle|rotation", "angle", Conv::Same},
};
constexpr ParamRule kFilmDamageP[] = {
    {"strength|amount|intensity|mix", "mix", Conv::Auto, 100.0},
    {"dust", "dust", Conv::Auto, 100.0},
    {"scratches|scratch", "scratches", Conv::Auto, 100.0},
    {"flicker", "flicker", Conv::Auto, 100.0},
    {"seed|randomseed", "seed", Conv::Int},
};
constexpr ParamRule kPixelSortP[] = {
    {"length|amount|strength", "length", Conv::Auto, 100.0},
    {"threshold|thresholdlow|low", "threshold_low", Conv::Auto, 100.0},
    {"thresholdhigh|high", "threshold_high", Conv::Auto, 100.0},
};

/// Ordem importa: a primeira regra que casa vale (os específicos antes dos
/// "contém" genéricos — "colorbalance" não é "tint", "lensblur" não é "blur").
/// Sem regra (e por isso em `skipped`): curves, mosaic/pixelate, vignette,
/// mirror, rgbsplit/chromatic (o Glitchify separa por faixa, não igual),
/// hue, colorbalance, flicker, edge, posterize de cor.
constexpr EffectRule kRules[] = {
    {"chromakey|greenscreen|colorkey|~chromakey", effect_keys::kChromaKey, kChromaKeyP},
    {"lumakey|luminancekey|~lumakey", effect_keys::kLumaKey, kLumaKeyP},
    {"brightnesscontrast|brightness|contrast|~brightnesscontrast", effect_keys::kBrightnessContrast, kBrightnessContrastP},
    {"exposure|~exposure", effect_keys::kExposure, kExposureP},
    {"levels|~levels", effect_keys::kLevels, kLevelsP},
    {"saturation|vibrance|huesaturation|~saturation|~vibrance", effect_keys::kSaturation, kSaturationP},
    {"invert|invertcolor|invertcolors|negative", effect_keys::kInvert, kInvertP},
    {"fill|fillcolor|solidcolor|colorfill|~solidcolor|~fillcolor", effect_keys::kTint, kFillP},
    {"tint|colortint|colorize|~tint", effect_keys::kTint, kTintP},
    {"lensblur|bokeh|bokehblur|~lensblur", effect_keys::kLensBlur, kLensBlurP, {}, "quality|samples"},
    {"horizontalblur", effect_keys::kGaussianBlur, kGaussianP, kBlurHorizontal, "quality|samples"},
    {"verticalblur", effect_keys::kGaussianBlur, kGaussianP, kBlurVertical, "quality|samples"},
    {"gaussianblur|blur|boxblur|fastblur|~gaussian", effect_keys::kGaussianBlur, kGaussianP, {}, "quality|samples"},
    {"unsharpmask|unsharp", effect_keys::kUnsharp, kUnsharpP},
    {"sharpen|~sharpen", effect_keys::kSharpen, kSharpenP},
    {"glow|softglow|~glow", effect_keys::kGlow, kGlowP, {}, "quality|samples"},
    {"lightrays|rays|godrays|~lightray", effect_keys::kRays, kRaysP},
    {"shake|wiggle|camerashake|~shake|~wiggle", effect_keys::kShake, kShakeP},
    {"turbulence|turbulentdisplace|~turbulen", effect_keys::kTurbulence, kTurbulenceP},
    {"wave|waves|wavewarp|~wavewarp", effect_keys::kWaveWarp, kWaveP},
    {"swirl|twirl|twist|~swirl|~twirl", effect_keys::kWarp, kWarpP, kWarpTwist},
    {"pinch|~pinch", effect_keys::kWarp, kWarpP, kWarpPull},
    {"bulge|fisheye|~bulge", effect_keys::kWarp, kWarpP, kWarpPush},
    {"sphere|spherize", effect_keys::kWarp, kWarpP, kWarpSphere},
    {"echo|echoes|trail|~echo", effect_keys::kEchoTrail, kEchoP},
    {"posterizetime|choppy|stopmotion|framerate|lowfps|~posterizetime", effect_keys::kPosterizeTime, kPosterizeTimeP},
    {"vhs|~vhs", effect_keys::kVhs, kVhsP},
    {"glitch|digitalglitch|~glitch", effect_keys::kGlitchify, kGlitchP},
    {"oldfilm|filmdamage|~oldfilm|~filmdamage", effect_keys::kFilmDamage, kFilmDamageP},
    {"noise|grain|filmgrain|~grain", effect_keys::kGrain, kGrainP},
    {"scanlines|scanline|~scanline", effect_keys::kScanlines, kScanlinesP},
    {"halftone|~halftone", effect_keys::kHalftone, kHalftoneP},
    {"pixelsort|~pixelsort", effect_keys::kPixelSort, kPixelSortP},
};

const EffectRule* find_rule(std::string_view shortName) {
    for (const EffectRule& r : kRules) if (alias_has(r.names, shortName)) return &r;
    return nullptr;
}

const EffectRegistry& builtin_registry() {
    static const EffectRegistry r = [] {
        EffectRegistry x;
        register_builtin_effects(x);
        return x;
    }();
    return r;
}

// =============================================================================
// Montagem
// =============================================================================

struct Ctx {
    const EffectRegistry* reg = nullptr;
    f64 fps = 30.0;
    f64 sceneW = 1920.0, sceneH = 1080.0;
};

/// Efeitos de UMA camada, já no formato do preset.
struct LayerOut {
    std::vector<EffectInstance> effects;
    std::vector<Track> tracks;
    u32 mapped = 0;
    std::vector<std::string> skipped;
    std::vector<std::string> warnings;
    void warn(std::string w) {
        if (warnings.size() < kMaxWarnings) warnings.push_back(std::move(w));
    }
};

/// A curva `e` do AM (chegada no próximo keyframe) no keyframe que COMEÇA o
/// trecho. Falso = curva desconhecida (fica linear).
bool set_easing(std::string_view e, Keyframe& k) {
    e = trim(e);
    k.interp = Interpolation::Linear;
    if (e.empty()) return true;
    std::vector<std::string_view> tok;
    while (!e.empty()) {
        usize sp = 0;
        while (sp < e.size() && !std::isspace(static_cast<unsigned char>(e[sp]))) ++sp;
        tok.push_back(e.substr(0, sp));
        e = trim(e.substr(sp));
    }
    const std::string name = lower_alnum(tok[0]);
    auto at = [&](usize i) {
        f64 x = 0.0;
        return i < tok.size() && number(tok[i], x) ? x : 0.0;
    };
    if (name == "linear") return true;
    if (name == "cubicbezier" || name == "bezier" || name == "cubic") {
        k.interp = Interpolation::Bezier;
        k.bx1 = static_cast<f32>(std::clamp(at(1), 0.0, 1.0));
        k.by1 = static_cast<f32>(std::clamp(at(2), -2.0, 3.0));
        k.bx2 = static_cast<f32>(std::clamp(at(3), 0.0, 1.0));
        k.by2 = static_cast<f32>(std::clamp(at(4), -2.0, 3.0));
        return true;
    }
    if (name == "hold" || name == "step" || name == "steps" || name == "constant") { k.interp = Interpolation::Hold; return true; }
    if (name == "ease" || name == "easeinout") { k.interp = Interpolation::EaseInOut; return true; }
    if (name == "easein") { k.interp = Interpolation::EaseIn; return true; }
    if (name == "easeout") { k.interp = Interpolation::EaseOut; return true; }
    if (name == "elastic") { k.interp = Interpolation::Elastic; return true; }
    if (name == "bounce") { k.interp = Interpolation::Bounce; return true; }
    return false;
}

/// Faixa do parâmetro do Aurea para o componente.
void param_range(const ParamSpec& s, f64& lo, f64& hi) {
    switch (s.type) {
        case ParamType::Color:
        case ParamType::Bool: lo = 0.0; hi = 1.0; break;
        case ParamType::Enum: lo = 0.0; hi = s.enumCount > 0 ? static_cast<f64>(s.enumCount - 1) : 0.0; break;
        default: lo = s.minValue; hi = s.maxValue; break;
    }
}

f64 component(const AmValue& v, u32 c) { return c < v.n ? v.v[c] : v.v[0]; }

/// Um parâmetro do AM → um parâmetro (todos os componentes) do Aurea.
void apply_param(const ParamRule& r, const AmProp& prop, const ParameterRegistry& specs, u32 pos, f64 durSec,
                 const Ctx& ctx, std::string_view fxName, EffectInstance& e, LayerOut& out) {
    const u32 idx = specs.find(r.id);
    if (idx == kInvalidIndex || idx >= e.params.size()) {
        out.warn(std::string(fxName) + ": parametro '" + r.id + "' nao existe (tabela desatualizada)");
        return;
    }
    const ParamSpec& spec = specs.at(idx);
    u32 comps = 1;
    if (spec.type == ParamType::Color) {
        if (!prop.value.color) { out.warn(std::string(fxName) + ": '" + prop.name + "' deveria ser uma cor"); return; }
        comps = 4;
    } else if (spec.type == ParamType::Point2D) {
        if (prop.value.n < 2) { out.warn(std::string(fxName) + ": '" + prop.name + "' deveria ser um ponto"); return; }
        comps = 2;
    } else if (component_count(spec.type) != 1) {
        out.warn(std::string(fxName) + ": '" + prop.name + "' nao e numerico no Aurea");
        return;
    }
    const bool scalar = comps == 1;
    const bool animated = prop.keys.size() >= 2;
    const bool timed = animated && durSec > 0.0 && spec.animatable();
    if (animated && !timed) {
        out.warn(std::string(fxName) + ": '" + prop.name + "' animado virou valor fixo ("
                 + (spec.animatable() ? "a camada nao diz a duracao" : "parametro nao animavel no Aurea") + ")");
    }

    // PointRel: o ponto inteiro já em 0..1 passa direto; senão, px da cena.
    bool pointFrac = true;
    if (r.conv == Conv::PointRel) {
        auto inside = [](const AmValue& v) { return v.v[0] >= 0.0 && v.v[0] <= 1.0 && v.v[1] >= 0.0 && v.v[1] <= 1.0; };
        pointFrac = inside(prop.value);
        for (const AmKey& k : prop.keys) pointFrac = pointFrac && k.v.n >= 2 && inside(k.v);
    }

    bool clamped = false;
    f64 lo = 0.0, hi = 0.0;
    param_range(spec, lo, hi);
    const f64 durFrames = durSec * ctx.fps;
    for (u32 c = 0; c < comps; ++c) {
        const u32 src = scalar ? r.comp : c;
        f64 factor = r.k;
        switch (r.conv) {
            case Conv::Auto: {
                f64 m = std::fabs(component(prop.value, src));
                for (const AmKey& k : prop.keys) m = std::max(m, std::fabs(component(k.v, src)));
                factor = m <= 1.0 + 1e-6 ? r.k : r.kBig;
                break;
            }
            case Conv::Seconds: factor = ctx.fps * r.k; break;
            case Conv::PointRel: factor = pointFrac ? 1.0 : 1.0 / (c == 0 ? ctx.sceneW : ctx.sceneH); break;
            case Conv::Color:
            case Conv::Bool: factor = 1.0; break;
            default: break;
        }
        auto conv = [&](const AmValue& v) -> f32 {
            f64 x = component(v, src);
            if (r.conv == Conv::Bool) x = x >= 0.5 ? 1.0 : 0.0;
            else x *= factor;
            if (spec.type == ParamType::Int || spec.type == ParamType::Enum || r.conv == Conv::Int) x = std::round(x);
            if (!std::isfinite(x)) x = spec.defaultValue.v[c];
            if (x < lo) { x = lo; clamped = true; }
            if (x > hi) { x = hi; clamped = true; }
            return static_cast<f32>(x);
        };
        const f32 base = conv(prop.value);
        e.params[idx].constant.v[c] = base;
        if (!timed) continue;
        Track t;
        t.property = TrackProperty::EffectParam;
        t.effectIndex = pos;
        t.effectParamIndex = param_track_key(idx, c);
        t.staticValue = base;
        bool unknownEase = false;
        for (usize i = 0; i < prop.keys.size(); ++i) {
            Keyframe k;
            k.time = FrameIndex{static_cast<i64>(std::llround(prop.keys[i].t * durFrames))};
            k.value = conv(prop.keys[i].v);
            if (i + 1 < prop.keys.size() && !set_easing(prop.keys[i + 1].e, k)) unknownEase = true;
            // Mesmo quadro depois de arredondar: fica o último.
            if (!t.keys.empty() && t.keys.back().time == k.time) t.keys.back() = k;
            else t.keys.push_back(k);
        }
        if (unknownEase && c == 0) out.warn(std::string(fxName) + ": curva desconhecida em '" + prop.name + "' virou linear");
        if (t.keys.size() >= 2) out.tracks.push_back(std::move(t));
    }
    if (clamped) out.warn(std::string(fxName) + ": '" + prop.name + "' fora da faixa de '" + r.id + "' foi ajustado");
}

EffectInstance new_instance(EffectTypeId type, const ParameterRegistry& specs, u32 pos, bool enabled) {
    EffectInstance e;
    e.type = type;
    e.id = pos;
    e.enabled = enabled;
    initialize_instance(e, specs);
    return e;
}

bool effect_enabled(const xml::Node& fx) {
    if (const std::string* v = fx.attr_nocase("enabled")) if (xml::iequals(trim(*v), "false")) return false;
    if (const std::string* v = fx.attr_nocase("hidden")) if (xml::iequals(trim(*v), "true")) return false;
    return true;
}

/// FADE de entrada/saída (segundos) → opacidade do efeito Transformar: o
/// mesmo resultado, dentro de um preset de efeitos.
void convert_fade(const xml::Node& fx, f64 durSec, const Ctx& ctx, LayerOut& out) {
    f64 in = 0.0, outT = 0.0;
    for (const xml::Node& pn : fx.kids) {
        if (!xml::iequals(pn.name, "property")) continue;
        AmProp p;
        if (!read_prop(pn, p)) continue;
        if (p.norm == "intime") in = std::max(0.0, p.value.v[0]);
        else if (p.norm == "outtime") outT = std::max(0.0, p.value.v[0]);
    }
    const EffectTypeId type = effect_type_id(effect_keys::kTransform);
    const ParameterRegistry* specs = ctx.reg->params(type);
    const u32 idx = specs ? specs->find("opacity") : kInvalidIndex;
    if (idx == kInvalidIndex) { out.skipped.emplace_back("com.alightcreative.effects.fade"); return; }
    const f64 durF = durSec * ctx.fps;
    std::vector<Keyframe> keys;
    auto key = [&](f64 frame, f32 v) {
        Keyframe k;
        k.time = FrameIndex{static_cast<i64>(std::llround(std::max(0.0, frame)))};
        k.value = v;
        keys.push_back(k);
    };
    if (in > 0.0) { key(0.0, 0.0f); key(in * ctx.fps, 100.0f); }
    if (outT > 0.0) {
        if (durSec > 0.0) { key(durF - outT * ctx.fps, 100.0f); key(durF, 0.0f); }
        else out.warn("fade: a saida precisa da duracao da camada, que o arquivo nao traz");
    }
    std::stable_sort(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
    std::vector<Keyframe> uniq;
    for (const Keyframe& k : keys) {
        if (!uniq.empty() && uniq.back().time == k.time) uniq.back() = k;
        else uniq.push_back(k);
    }
    if (uniq.size() < 2) {
        out.warn("fade sem tempo de entrada/saida aproveitavel");
        return;
    }
    const u32 pos = static_cast<u32>(out.effects.size());
    EffectInstance e = new_instance(type, *specs, pos, effect_enabled(fx));
    e.params[idx].constant.v[0] = uniq.front().value;
    Track t;
    t.property = TrackProperty::EffectParam;
    t.effectIndex = pos;
    t.effectParamIndex = param_track_key(idx, 0);
    t.staticValue = uniq.front().value;
    t.keys = std::move(uniq);
    out.tracks.push_back(std::move(t));
    out.effects.push_back(std::move(e));
    ++out.mapped;
}

void convert_layer(const xml::Node& layer, f64 durSec, const Ctx& ctx, LayerOut& out) {
    for (const xml::Node& fx : layer.kids) {
        if (!xml::iequals(fx.name, "effect")) continue;
        const std::string* idp = fx.attr_nocase("id");
        const std::string id = idp && !idp->empty() ? *idp : std::string("(efeito sem id)");
        if (out.effects.size() >= kMaxEffectCount) {
            out.skipped.push_back(id);
            out.warn("limite de " + std::to_string(kMaxEffectCount) + " efeitos por camada");
            continue;
        }
        const std::string sn = short_name(id);
        if (sn.rfind("motionblur", 0) == 0) {
            out.skipped.push_back(id);
            out.warn("desfoque de movimento do AM e da camada, nao um efeito: ligue o motion blur da camada no Aurea");
            continue;
        }
        if (sn == "fade") { convert_fade(fx, durSec, ctx, out); continue; }
        const EffectRule* rule = find_rule(sn);
        const EffectTypeId type = rule ? effect_type_id(rule->key) : 0;
        const ParameterRegistry* specs = rule ? ctx.reg->params(type) : nullptr;
        if (!specs) {
            out.skipped.push_back(id);
            continue;
        }
        const u32 pos = static_cast<u32>(out.effects.size());
        EffectInstance e = new_instance(type, *specs, pos, effect_enabled(fx));
        for (const FixedValue& f : rule->fixed) {
            const u32 k = specs->find(f.id);
            if (k != kInvalidIndex && k < e.params.size()) e.params[k].constant.v[0] = f.v;
        }
        for (const xml::Node& pn : fx.kids) {
            if (!xml::iequals(pn.name, "property")) continue;
            AmProp prop;
            if (!read_prop(pn, prop)) {
                if (!prop.name.empty()) out.warn(sn + ": parametro '" + prop.name + "' ilegivel");
                continue;
            }
            bool used = false;
            for (const ParamRule& r : rule->params) {
                if (!alias_has(r.am, prop.norm)) continue;
                used = true;
                apply_param(r, prop, *specs, pos, durSec, ctx, sn, e, out);
            }
            if (!used && !alias_has(rule->ignore, prop.norm)) out.warn(sn + ": parametro '" + prop.name + "' sem equivalente no Aurea");
        }
        out.effects.push_back(std::move(e));
        ++out.mapped;
    }
}

// --- camadas -----------------------------------------------------------------

struct Candidate {
    const xml::Node* node = nullptr;
    f64 durSec = 0.0;          ///< 0 = desconhecida
    std::string label;
};

bool has_effect_child(const xml::Node& n) {
    for (const xml::Node& k : n.kids) if (xml::iequals(k.name, "effect")) return true;
    return false;
}

f64 attr_number(const xml::Node& n, std::string_view a, f64 def) {
    f64 x = 0.0;
    const std::string* s = n.attr_nocase(a);
    return s && number(*s, x) ? x : def;
}

/// Pré-ordem (o grupo antes dos filhos). `sceneDur` = totalTime da cena em
/// volta, para camada sem startTime/endTime.
void collect(const xml::Node& n, f64 sceneDur, std::vector<Candidate>& out) {
    for (const xml::Node& k : n.kids) {
        if (xml::iequals(k.name, "effect") || xml::iequals(k.name, "property") || xml::iequals(k.name, "transform")) continue;
        if (xml::iequals(k.name, "scene")) {
            const f64 total = attr_number(k, "totalTime", 0.0);
            collect(k, total > 0.0 ? total / 1000.0 : sceneDur, out);
            continue;
        }
        if (has_effect_child(k)) {
            Candidate c;
            c.node = &k;
            const f64 start = attr_number(k, "startTime", 0.0);
            const f64 end = attr_number(k, "endTime", -1.0);
            c.durSec = end > start ? (end - start) / 1000.0 : sceneDur;
            if (const std::string* l = k.attr_nocase("label")) c.label = std::string(trim(*l));
            if (c.label.empty()) c.label = k.name;
            out.push_back(std::move(c));
        }
        collect(k, sceneDur, out);
    }
}

const xml::Node* find_scene(const xml::Node& n) {
    for (const xml::Node& k : n.kids) {
        if (xml::iequals(k.name, "scene")) return &k;
        if (const xml::Node* s = find_scene(k)) return s;
    }
    return nullptr;
}

void append(std::vector<std::string>& to, std::vector<std::string>& from) {
    for (std::string& s : from) if (to.size() < kMaxWarnings) to.push_back(std::move(s));
    from.clear();
}

} // namespace

// =============================================================================
// API
// =============================================================================

std::string import_alight_motion(std::string_view data, const EffectRegistry* registry, AlightImportReport& report) {
    report = AlightImportReport{};
    Ctx ctx;
    ctx.reg = registry ? registry : &builtin_registry();

    if (data.size() > kMaxInput) { report.error = "arquivo grande demais"; return {}; }
    std::string text;
    if (is_zip(data)) {
        if (!unzip_scene(data, text, report.warnings, report.error)) return {};
    } else {
        if (data.size() > kMaxXml) { report.error = "XML grande demais"; return {}; }
        text.assign(data.data(), data.size());
    }
    text = clean_utf8(text);
    xml::Node doc;
    if (!xml::parse(text, doc, kMaxXmlDepth)) {
        report.error = "XML invalido: vazio, sem elementos ou aninhado demais";
        return {};
    }

    const xml::Node* scene = find_scene(doc);
    std::string title;
    if (scene) {
        const f64 fps = attr_number(*scene, "fps", 30.0);
        ctx.fps = std::clamp(fps > 0.0 ? fps : 30.0, 1.0, 240.0);
        const f64 w = attr_number(*scene, "width", 0.0), h = attr_number(*scene, "height", 0.0);
        if (w > 0.0 && h > 0.0) { ctx.sceneW = w; ctx.sceneH = h; }
        if (const std::string* t = scene->attr_nocase("title")) title = std::string(trim(*t));
    }

    // Efeitos soltos na raiz (sem camada) também contam, com duração desconhecida.
    std::vector<Candidate> cands;
    if (has_effect_child(doc)) cands.push_back(Candidate{&doc, 0.0, {}});
    collect(doc, 0.0, cands);
    if (cands.empty()) {
        report.error = scene ? "a cena do Alight Motion nao tem efeitos"
                             : "isso nao parece um XML do Alight Motion: nao achei <scene> nem <effect>";
        return {};
    }

    LayerOut chosen;
    bool have = false;
    u32 others = 0;
    std::vector<std::string> allSkipped, allWarnings;
    for (const Candidate& c : cands) {
        LayerOut lo;
        convert_layer(*c.node, c.durSec, ctx, lo);
        if (!have && lo.mapped > 0) {
            chosen = std::move(lo);
            report.layer = c.label;
            have = true;
            continue;
        }
        ++others;
        if (!have) {
            append(allSkipped, lo.skipped);
            append(allWarnings, lo.warnings);
        }
    }
    if (!have) {
        report.skipped = std::move(allSkipped);
        append(report.warnings, allWarnings);
        report.error = "nenhum efeito do Alight Motion tem equivalente no Aurea";
        return {};
    }

    Preset p;
    p.kind = PresetKind::Effects;
    p.name = !title.empty() ? title : (!report.layer.empty() ? report.layer : std::string("Alight Motion"));
    p.fps = ctx.fps;
    p.effects = std::move(chosen.effects);
    p.effectTracks = std::move(chosen.tracks);
    std::string js = write(p, ctx.reg);
    // O que sai daqui passa pelo MESMO leitor que o apply usa.
    Preset check;
    std::string err;
    if (!parse(js, check, ctx.reg, &err)) {
        report.error = "a conversao gerou um preset invalido: " + err;
        return {};
    }
    report.mapped = chosen.mapped;
    report.skipped = std::move(chosen.skipped);
    append(report.warnings, chosen.warnings);
    if (others > 0) {
        report.warnings.push_back(std::to_string(others) + " outra(s) camada(s) com efeitos ficaram de fora: o preset leva so os efeitos de '"
                                  + report.layer + "'");
    }
    report.name = p.name;
    return js;
}

std::string alight_import_envelope(const std::string& presetJson, const AlightImportReport& r) {
    json::Writer w;
    w.begin_object();
    w.key("preset").value(presetJson);
    w.key("name").value(r.name);
    w.key("layer").value(r.layer);
    w.key("mapped").value(r.mapped);
    w.key("skipped").begin_array();
    for (const std::string& s : r.skipped) w.value(s);
    w.end_array();
    w.key("warnings").begin_array();
    for (const std::string& s : r.warnings) w.value(s);
    w.end_array();
    w.key("error").value(r.error);
    w.end_object();
    return w.str();
}

std::vector<std::string> alight_mapping_problems(const EffectRegistry& registry) {
    std::vector<std::string> out;
    for (const EffectRule& r : kRules) {
        const ParameterRegistry* specs = registry.params(effect_type_id(r.key));
        if (!specs) { out.push_back(std::string("chave desconhecida: ") + r.key); continue; }
        for (const ParamRule& p : r.params) {
            const u32 k = specs->find(p.id);
            if (k == kInvalidIndex) { out.push_back(std::string(r.key) + ": parametro desconhecido " + p.id); continue; }
            const ParamType t = specs->at(k).type;
            if ((p.conv == Conv::Color) != (t == ParamType::Color)) out.push_back(std::string(r.key) + ": " + p.id + " cor x nao cor");
            if ((p.conv == Conv::PointRel) != (t == ParamType::Point2D)) out.push_back(std::string(r.key) + ": " + p.id + " ponto x nao ponto");
            if (component_count(t) == 0) out.push_back(std::string(r.key) + ": " + p.id + " nao numerico");
        }
        for (const FixedValue& f : r.fixed) {
            if (specs->find(f.id) == kInvalidIndex) out.push_back(std::string(r.key) + ": fixo desconhecido " + f.id);
        }
    }
    return out;
}

} // namespace aurea::presets
