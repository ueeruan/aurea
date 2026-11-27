// =============================================================================
//  Aurea / hyper / Hyper.cpp — receita (faixas), quadros NV12 e conversões.
// =============================================================================
#include "aurea/hyper/Hyper.hpp"
#include "aurea/hyper/HyperStage.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/scene3d/Text3D.hpp"
#include "aurea/text/TextGradient.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::hyper {

namespace {
f32 unit(f32 v, f32 fallback) noexcept { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : fallback; }
u8 byte_of(f32 v) noexcept { return static_cast<u8>(std::clamp(std::lround(v), 0l, 255l)); }
} // namespace

namespace {
/// Um efeito pago e a assinatura avulsa que o libera (nullptr = só o AureaHyper).
struct PaidEffect {
    u32 typeId;
    const char* product;
};

// 16/11/2026 (decisão do dono): cada efeito pago tinha a sua assinatura avulsa
// (fora de venda desde a 1.0.2.1, reconhecida para quem já tem); o AureaHyper
// (semanal, mensal ou anual desde a 1.0.9) libera todos. docs/release/assinaturas.md.
constexpr PaidEffect kPaidEffects[] = {
    // Ferramentas Hyper: só no AureaHyper (sem assinatura avulsa).
    {effect_type_id(effect_keys::kHyperUpscale), nullptr},
    {effect_type_id(effect_keys::kHyperEnchance), nullptr},
    {effect_type_id(effect_keys::kHyperMotion), nullptr},
    {effect_type_id(effect_keys::kHyperClean), nullptr},
    {effect_type_id(text::kTextPrismEffect), nullptr},
    {effect_type_id("aurea.motion.cartoon_moblur"), "aurea_fx_cartoon_moblur"},
    {effect_type_id("aurea.glitch.signal_reference"), "aurea_fx_signal"},
    {effect_type_id(aurea::effect_keys::kSignal), "aurea_fx_signal"},
    {effect_type_id("aurea.glitch.glitchify_reference"), "aurea_fx_glitchify"},
    {effect_type_id(aurea::effect_keys::kGlitchify), "aurea_fx_glitchify"},
    {effect_type_id("aurea.generate.extrude_reference"), "aurea_fx_extrude"},
    {effect_type_id(aurea::effect_keys::kRasterExtrude), "aurea_fx_extrude"},
    {effect_type_id("aurea.blur.sharpen_reference"), "aurea_fx_sharpen"},
    {effect_type_id("aurea.stylize.find_edges_reference"), "aurea_fx_find_edges"},
    {effect_type_id("aurea.distort.wave_warp_reference"), "aurea_fx_wave_warp"},
    {effect_type_id("aurea.transition.block_dissolve_reference"), "aurea_fx_block_dissolve"},
    {effect_type_id("aurea.transition.radial_wipe_reference"), "aurea_fx_radial_wipe"},
    // Headbop: pago desde a 1.0.2 (pedido do dono, 18/11/2026), mesma regra dos outros.
    {effect_type_id("aurea.distort.headbop"), "aurea_fx_headbop"},
    // Explosão 3D (23/11/2026): efeito 3D premium como o Texto 3D; sem
    // assinatura avulsa na loja ainda, entra no AureaHyper.
    {effect_type_id(aurea::effect_keys::kExplode3D), nullptr},
    // Rig 2D de rosto: o mesmo portão do AutoRig do Fantoche (só o AureaHyper).
    {effect_type_id("aurea.rig.face"), nullptr},
    // Seleções da galeria 1.0.7. Esses efeitos entram no plano AureaHyper.
    {effect_type_id("aurea.blur.rgb_channels"), nullptr},
    {effect_type_id("aurea.blur.chroma_luma"), nullptr},
    {effect_type_id("aurea.blur.transform_motion"), nullptr},
    {effect_type_id("aurea.blur.prism_defocus"), nullptr},
    {effect_type_id("aurea.blur.detail_sharpen"), nullptr},
    {effect_type_id("aurea.blur.soft_focus"), nullptr},
    {effect_type_id("aurea.light.chromatic_halo"), nullptr},
    {effect_type_id("aurea.light.spectral_aura"), nullptr},
    {effect_type_id("aurea.light.shadow_halo"), nullptr},
    {effect_type_id("aurea.light.outline_halo"), nullptr},
    {effect_type_id("aurea.light.rainbow_halo"), nullptr},
    {effect_type_id("aurea.light.glow_rings"), nullptr},
    {effect_type_id("aurea.light.diffraction_glare"), nullptr},
    {effect_type_id("aurea.light.outline_rays"), nullptr},
    {effect_type_id("aurea.light.exposure_streaks"), nullptr},
    {effect_type_id("aurea.light.prismatic_glints"), nullptr},
    {effect_type_id("aurea.light.light_leak"), nullptr},
    {effect_type_id(aurea::effect_keys::kEchoTrail), nullptr},
    {effect_type_id(aurea::effect_keys::kRandomEdits), nullptr},
    {effect_type_id(aurea::effect_keys::kPixelSort), nullptr},
    {effect_type_id(aurea::effect_keys::kDatamosh), nullptr},
    {effect_type_id("aurea.stylize.jpeg_compression_damage"), nullptr},
    {effect_type_id("aurea.stylize.analog_tape_damage"), nullptr},
    {effect_type_id("aurea.stylize.film_profile_grain"), nullptr},
    {effect_type_id("aurea.stylize.emboss"), nullptr},
    {effect_type_id("aurea.distort.refraction"), nullptr},
    {effect_type_id("aurea.distort.refraction_rgb"), nullptr},
    {effect_type_id("aurea.distort.projective_pin"), nullptr},
    // Fast Blur, Turbulent Displace and Drop Shadow: free by the owner (16/11).
    // Radial Blur, Radial Shadow, Posterize Time and Time Displacement
    // (AE reference batches A/A2) are NOT here: their legacy versions were
    // free, and an AE-faithful replacement of a free effect stays free.
};

const PaidEffect* find_paid(u32 effectTypeId) noexcept {
    for (const PaidEffect& e : kPaidEffects)
        if (e.typeId == effectTypeId) return &e;
    return nullptr;
}

bool owns(const std::vector<std::string>& owned, std::string_view product) noexcept {
    if (product.empty()) return false;
    for (const std::string& p : owned)
        if (p == product) return true;
    return false;
}
} // namespace

bool effect_requires_access(u32 effectTypeId) noexcept { return find_paid(effectTypeId) != nullptr; }

bool effect_option_requires_access(u32 effectTypeId, u32 paramIndex, f32 value) noexcept {
    // Neural (IA) do Flow Retime: só no AureaHyper (o produto é o do Hyper Motion).
    return effectTypeId == effect_type_id(aurea::effect_keys::kFlowRetime) && paramIndex == kFlowRetimeVectorsParam
        && std::isfinite(value) && std::lround(value) == static_cast<long>(kFlowRetimeNeuralValue);
}

bool text3d_requires_access() noexcept { return false; }

bool model_source_requires_access(const std::string& sourcePath) noexcept {
    return text3d_requires_access() && sourcePath.rfind(scene3d::kText3DScheme, 0) == 0;
}

const char* effect_product_id(u32 effectTypeId) noexcept {
    const PaidEffect* e = find_paid(effectTypeId);
    return e ? e->product : nullptr;
}

const char* text3d_product_id() noexcept { return kText3DProductId; }

bool product_unlocks_everything(std::string_view productId) noexcept {
    return productId == kHyperWeeklyId || productId == kHyperMonthlyId || productId == kHyperAnnualId
        || productId == kProductId;
}

std::vector<std::string> hyper_plans_on_sale() {
    // 1.0.9: semanal, mensal e anual (o anual é o destacado nas telas).
    return {kHyperWeeklyId, kHyperMonthlyId, kHyperAnnualId};
}

u32 hyper_annual_savings_percent(i64 weeklyMicros, i64 monthlyMicros, i64 annualMicros) noexcept {
    if (annualMicros <= 0) return 0;
    // Um ano do plano curto mais barato (o selo nunca exagera a economia).
    f64 year = 0.0;
    if (weeklyMicros > 0) year = static_cast<f64>(weeklyMicros) * 52.0;
    if (monthlyMicros > 0) {
        const f64 m = static_cast<f64>(monthlyMicros) * 12.0;
        year = year > 0.0 ? std::min(year, m) : m;
    }
    if (year <= static_cast<f64>(annualMicros)) return 0;
    const f64 pct = std::floor((1.0 - static_cast<f64>(annualMicros) / year) * 100.0 + 1e-9);
    return static_cast<u32>(std::clamp(pct, 0.0, 99.0));
}

std::vector<std::string> subscription_product_ids() {
    std::vector<std::string> ids = hyper_plans_on_sale();
    for (const PaidEffect& e : kPaidEffects)
        if (e.product && std::find(ids.begin(), ids.end(), e.product) == ids.end()) ids.emplace_back(e.product);
    // Twitch is free. Retain the retired SKU for receipt restoration.
    ids.emplace_back("aurea_fx_twitch");
    ids.emplace_back(kText3DProductId);
    return ids;
}

bool has_access(const std::vector<std::string>& owned, const char* itemProduct) noexcept {
    for (const std::string& p : owned)
        if (product_unlocks_everything(p)) return true;
    return itemProduct && owns(owned, itemProduct);
}

bool has_effect_access(const std::vector<std::string>& owned, u32 effectTypeId) noexcept {
    const PaidEffect* e = find_paid(effectTypeId);
    return !e || has_access(owned, e->product);
}

bool has_text3d_access(const std::vector<std::string>& owned) noexcept {
    return !text3d_requires_access() || has_access(owned, text3d_product_id());
}

Recipe sanitize(const Recipe& in) noexcept {
    Recipe r = in;
    r.upscale = in.upscale >= 3 ? 4u : in.upscale;
    if (static_cast<u8>(r.upscaleMode) > static_cast<u8>(UpscaleMode::Enchance)) r.upscaleMode = UpscaleMode::General;
    if (r.upscaleMode == UpscaleMode::Enchance && r.upscale != 1) r.upscale = 2;
    r.denoise = unit(in.denoise, 0.0f);
    r.deinterlace = std::min<u8>(in.deinterlace, 2);
    r.faceStrength = unit(in.faceStrength, 0.5f);
    // 0 = manter; senão 1..240 (o teto dos encoders do motor).
    r.targetFps = (std::isfinite(in.targetFps) && in.targetFps >= 1.0) ? std::min(in.targetFps, 240.0) : 0.0;
    r.slowMotion = in.slowMotion >= 6 ? 8u : (in.slowMotion >= 3 ? 4u : (in.slowMotion >= 2 ? 2u : 1u));
    r.stabilizeStrength = unit(in.stabilizeStrength, 0.5f);
    if (static_cast<u8>(r.path) > static_cast<u8>(Path::Max)) r.path = Path::Auto;
    return r;
}

bool is_identity(const Recipe& in) noexcept {
    const Recipe r = sanitize(in);
    return r.upscale == 1 && r.denoise <= 0.0f && !r.deblock && r.deinterlace == 0 && !r.face
        && r.targetFps <= 0.0 && r.slowMotion == 1 && !r.stabilize;
}

const char* stage_kind_name(StageKind kind) noexcept {
    switch (kind) {
        case StageKind::Clean: return "clean";
        case StageKind::Stable: return "stable";
        case StageKind::Upscale: return "upscale";
        case StageKind::Face: return "face";
        case StageKind::Motion: return "motion";
        default: return "?";
    }
}

Status FrameBuffer::resize(u32 w, u32 h) noexcept {
    if (!w || !h || (w | h) & 1u) return Status{Errc::InvalidArgument, "NV12 exige dimensoes pares"};
    const usize bytes = static_cast<usize>(w) * h * 3 / 2;
    try {
        nv12.resize(bytes);
    } catch (...) {
        return Status{Errc::OutOfMemory, "memoria insuficiente para o quadro do Hyper"};
    }
    width = w;
    height = h;
    return OkStatus;
}

FrameView FrameBuffer::view() const noexcept {
    FrameView v;
    if (nv12.empty()) return v;
    v.y = nv12.data();
    v.uv = nv12.data() + static_cast<usize>(width) * height;
    v.width = width;
    v.height = height;
    v.yStride = width;
    v.uvStride = width;
    v.ptsUs = ptsUs;
    v.index = index;
    v.sourcePosition = sourcePosition;
    return v;
}

void nv12_to_rgb24(const FrameView& in, u8* rgb, u32 rgbStride) noexcept {
    if (!in.valid() || !rgb) return;
    for (u32 row = 0; row < in.height; ++row) {
        const u8* yl = in.y + static_cast<usize>(row) * in.yStride;
        const u8* cl = in.uv + static_cast<usize>(row / 2) * in.uvStride;
        u8* out = rgb + static_cast<usize>(row) * rgbStride;
        for (u32 col = 0; col < in.width; ++col) {
            const f32 l = (yl[col] - 16.0f) * (255.0f / 219.0f);
            const f32 cb = (cl[col & ~1u] - 128.0f) * (255.0f / 224.0f);
            const f32 cr = (cl[(col & ~1u) + 1] - 128.0f) * (255.0f / 224.0f);
            out[col * 3 + 0] = byte_of(l + 1.5748f * cr);
            out[col * 3 + 1] = byte_of(l - 0.187324f * cb - 0.468124f * cr);
            out[col * 3 + 2] = byte_of(l + 1.8556f * cb);
        }
    }
}

bool rgb24_to_nv12(const u8* rgb, u32 rgbStride, u32 width, u32 height, FrameBuffer& out) noexcept {
    if (!rgb || width != out.width || height != out.height || (width | height) & 1u
        || out.nv12.size() < static_cast<usize>(width) * height * 3 / 2) return false;
    u8* y = out.y();
    u8* uv = out.uv();
    for (u32 row = 0; row < height; row += 2) {
        for (u32 col = 0; col < width; col += 2) {
            f32 cb = 0.0f, cr = 0.0f;
            for (u32 dy = 0; dy < 2; ++dy)
                for (u32 dx = 0; dx < 2; ++dx) {
                    const u8* p = rgb + static_cast<usize>(row + dy) * rgbStride + (col + dx) * 3;
                    const f32 l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
                    y[static_cast<usize>(row + dy) * width + col + dx] = byte_of(16.0f + l * (219.0f / 255.0f));
                    cb += (p[2] - l) / 1.8556f;
                    cr += (p[0] - l) / 1.5748f;
                }
            const usize at = static_cast<usize>(row / 2) * width + col;
            uv[at] = byte_of(128.0f + cb * (224.0f / (4.0f * 255.0f)));
            uv[at + 1] = byte_of(128.0f + cr * (224.0f / (4.0f * 255.0f)));
        }
    }
    return true;
}

} // namespace aurea::hyper
