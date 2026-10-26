// =============================================================================
//  Aurea / hyper / HyperPlan.cpp — caminhos, passagens, estimativa e o
//  arquivo do medidor.
// =============================================================================
#include "HyperPlan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace aurea::hyper {

namespace {

u32 even_down(f64 v) noexcept { return static_cast<u32>(std::max(2.0, std::floor(v / 2.0) * 2.0)); }

/// A implementação de rede para (etapa, caminho): o pedido; Max sem rede cai
/// para Lite. Nulo = Turbo (ou nada, para quem não tem Turbo).
const StageDescriptor* neural_for(const StageRegistry& reg, StageKind kind, Path want, i32 mode,
                                  const DeviceProfile& p) noexcept {
    if (want == Path::Turbo || want == Path::Auto) return nullptr;
    if (const StageDescriptor* d = reg.find(kind, want, mode, p)) return d;
    if (want == Path::Max) return reg.find(kind, Path::Lite, mode, p);
    return nullptr;
}

} // namespace

Path effective_path(const Recipe& r, const DeviceProfile& p) noexcept {
    if (r.path != Path::Auto) return r.path;
    return p.measured ? p.defaultPath : Path::Turbo;
}

bool fit_output(u32 wantW, u32 wantH, const OutputCaps& caps, u32& w, u32& h) noexcept {
    w = h = 0;
    if (!wantW || !wantH) return false;
    f64 scale = 1.0;
    const u32 longSide = std::max(wantW, wantH), shortSide = std::min(wantW, wantH);
    if (caps.capLong > 0) scale = std::min(scale, static_cast<f64>(caps.capLong) / longSide);
    if (caps.capShort > 0) scale = std::min(scale, static_cast<f64>(caps.capShort) / shortSide);
    if (caps.maxTexture > 0) scale = std::min(scale, static_cast<f64>(caps.maxTexture) / longSide);
    w = even_down(wantW * scale);
    h = even_down(wantH * scale);
    return scale < 1.0 - 1e-9;
}

u64 frames_for(f64 seconds, f64 fps) noexcept {
    if (!(seconds > 0.0) || !(fps > 0.0)) return 1;
    const f64 f = std::ceil(seconds * fps - 1e-6);
    return static_cast<u64>(std::max(1.0, f));
}

Result<JobPlan> plan_job(const Recipe& recipe, const DeviceProfile& profile, const SourceInfo& source,
                         const OutputCaps& caps, const StageRegistry& registry) {
    if (!source.width || !source.height || !(source.fps > 0.0) || source.durationUs <= 0)
        return Status{Errc::UnsupportedFormat, "fonte sem video valido"};
    JobPlan plan;
    const Recipe r = sanitize(recipe);
    plan.recipe = r;
    const Path want = effective_path(r, profile);
    const i32 upMode = static_cast<i32>(r.upscaleMode);
    auto step = [&](Step s) -> usize { return static_cast<usize>(s); };
    plan.present[step(Step::Prepare)] = plan.present[step(Step::Encode)] = plan.present[step(Step::Finish)] = true;
    for (Path& p : plan.paths) p = Path::Turbo;

    // --- Saída: fps e tamanho ------------------------------------------------
    const f64 maxFps = caps.maxFps > 0.0 ? std::min(caps.maxFps, 240.0) : 240.0;
    f64 outFps = r.targetFps > 0.0 ? r.targetFps : source.fps;
    if (outFps > maxFps + 1e-9) { outFps = maxFps; plan.fpsCapped = true; }
    plan.outFps = outFps;
    const u32 N = r.slowMotion;
    const f64 seconds = static_cast<f64>(source.durationUs) / 1e6 * N;
    plan.outFrames = frames_for(seconds, outFps);
    plan.muteAudio = N > 1;
    // Interpola quando a saída pede instantes entre quadros da fonte.
    const f64 srcPerOut = source.fps / (static_cast<f64>(N) * outFps);
    const bool motion = N > 1 || (r.targetFps > 0.0 && srcPerOut < 1.0 - 1e-6);
    u32 outW = 0, outH = 0;
    plan.resolutionCapped = fit_output(source.width * r.upscale, source.height * r.upscale, caps, outW, outH);
    plan.outW = outW;
    plan.outH = outH;
    const bool upscale = r.upscale > 1 && static_cast<u64>(outW) * outH > static_cast<u64>(source.width) * source.height;
    if (r.upscale > 1 && !upscale) plan.note += "upscale sem efeito: a fonte ja esta no teto do aparelho; ";

    // --- Caminho de cada etapa -------------------------------------------------
    const bool clean = r.denoise > 0.0f || r.deblock || r.deinterlace != 0;
    const StageDescriptor* cleanNet = r.denoise > 0.0f ? neural_for(registry, StageKind::Clean, want, -1, profile) : nullptr;
    if (clean) {
        plan.present[step(Step::Clean)] = true;
        plan.paths[step(Step::Clean)] = cleanNet ? cleanNet->path : Path::Turbo;
        if (want != Path::Turbo && r.denoise > 0.0f && !cleanNet) plan.note += "Clean: sem rede neste aparelho, Turbo; ";
    }
    if (r.stabilize) plan.present[step(Step::Stable)] = true;
    const StageDescriptor* upNet = upscale ? neural_for(registry, StageKind::Upscale, want, upMode, profile) : nullptr;
    if (upscale) {
        plan.present[step(Step::Upscale)] = true;
        plan.paths[step(Step::Upscale)] = upNet ? upNet->path : Path::Turbo;
        if (want != Path::Turbo && !upNet) plan.note += "Upscale: sem rede para este modo, Turbo; ";
    }
    const StageDescriptor* faceNet = nullptr;
    if (r.face) {
        // Rosto não tem Turbo: no Turbo usa a rede mais leve que houver.
        faceNet = neural_for(registry, StageKind::Face, want == Path::Turbo ? Path::Lite : want, -1, profile);
        if (!faceNet && want == Path::Turbo) faceNet = registry.find(StageKind::Face, Path::Max, -1, profile);
        if (faceNet) {
            plan.present[step(Step::Face)] = true;
            plan.paths[step(Step::Face)] = faceNet->path;
        } else {
            plan.note += "Face: rede indisponivel, etapa pulada; ";
        }
    }
    // Motion: Lite/Max (rede) no grafo, sobre quadros repetidos pelo render;
    // Turbo no render (Hyper Motion, vetores de bloco).
    const StageDescriptor* motionNet = motion ? neural_for(registry, StageKind::Motion, want, -1, profile) : nullptr;
    if (motion) {
        plan.present[step(Step::Motion)] = true;
        plan.paths[step(Step::Motion)] = motionNet ? motionNet->path : Path::Turbo;
        if (want != Path::Turbo && !motionNet) plan.note += "Motion: sem rede neste aparelho, Turbo; ";
    }

    // --- Passagens -------------------------------------------------------------
    plan.passes.emplace_back();
    auto open_render = [&]() -> PassPlan& {
        if (!plan.passes.back().graph.empty()) plan.passes.emplace_back();
        return plan.passes.back();
    };
    if (clean) {
        PassPlan& p = plan.passes.back();
        p.renderClean = true;
        p.renderDenoise = r.denoise > 0.0f && !cleanNet;
    }
    if (r.stabilize) plan.passes.back().renderStable = true;
    if (cleanNet) plan.passes.back().graph.push_back(cleanNet);
    if (upscale) {
        if (upNet) plan.passes.back().graph.push_back(upNet);
        else open_render().renderUpscale = true;
    }
    if (faceNet) plan.passes.back().graph.push_back(faceNet);
    if (motion) {
        if (motionNet) plan.passes.back().graph.push_back(motionNet);
        else open_render().renderMotion = true;
    }

    // Tamanhos e tempo de cada passagem.
    u32 srcW = source.width, srcH = source.height;
    f64 srcFps = source.fps;
    bool upscaled = false;
    for (usize k = 0; k < plan.passes.size(); ++k) {
        PassPlan& p = plan.passes[k];
        p.final = k + 1 == plan.passes.size();
        p.sourceW = srcW;
        p.sourceH = srcH;
        p.sourceFps = srcFps;
        p.fps = p.final ? outFps : source.fps;
        p.slowMotion = p.final ? N : 1;
        p.muteAudio = plan.muteAudio;
        const f64 s = static_cast<f64>(source.durationUs) / 1e6 * p.slowMotion;
        p.frames = frames_for(s, p.fps);
        const bool graphUpscale = std::any_of(p.graph.begin(), p.graph.end(),
                                              [](const StageDescriptor* d) { return d->kind == StageKind::Upscale; });
        if (p.renderUpscale && !upscaled) {
            p.renderW = outW;
            p.renderH = outH;
            upscaled = true;
        } else {
            // NV12: a composição tem lados pares (a fonte ímpar é ajustada).
            p.renderW = std::max(2u, srcW & ~1u);
            p.renderH = std::max(2u, srcH & ~1u);
        }
        p.outW = p.renderW;
        p.outH = p.renderH;
        if (graphUpscale && !upscaled) {
            p.outW = outW;
            p.outH = outH;
            upscaled = true;
        }
        srcW = p.outW;
        srcH = p.outH;
        srcFps = p.fps;
    }
    if (plan.passes.size() > 1) plan.note += "passagens: " + std::to_string(plan.passes.size()) + "; ";
    return plan;
}

namespace {

/// Custo Turbo em "quadros de FSR 1080p" por quadro de saída.
f64 turbo_units(const PassPlan& p, const Recipe& r) noexcept {
    const f64 outMp = static_cast<f64>(p.renderW) * p.renderH / 2.0736e6;
    const f64 srcMp = static_cast<f64>(p.sourceW) * p.sourceH / 2.0736e6;
    f64 u = 0.15 * outMp;   // composição + YUV
    if (p.renderClean) u += srcMp * ((p.renderDenoise ? 1.6 * std::max(1.0, 2.0 * r.denoise + 1.0) : 0.0)
                                     + (r.deblock ? 0.25 : 0.0) + (r.deinterlace ? 0.3 : 0.0));
    if (p.renderUpscale) u += outMp * (r.upscale == 4 ? 1.6 : 1.0) * (r.upscaleMode == UpscaleMode::Anime ? 1.5 : 1.0);
    if (p.renderMotion) u += srcMp * 1.4;
    return u;
}

} // namespace

Estimate estimate_job(const Recipe& recipe, const DeviceProfile& profile, const SourceInfo& source,
                      const OutputCaps& caps, const StageRegistry& registry) {
    Estimate e;
    const Recipe base = sanitize(recipe);
    const f64 turboMs = profile.measured && profile.turboMsPerFrame > 0.0f ? profile.turboMsPerFrame : 30.0;
    const Path chosen = effective_path(base, profile);
    for (Path path : {Path::Turbo, Path::Lite, Path::Max}) {
        Recipe r = base;
        r.path = path;
        auto plan = plan_job(r, profile, source, caps, registry);
        if (!plan.ok()) return e;
        // Lite/Max indisponível: nenhuma etapa pedida ganhou rede nesse caminho.
        bool anyNet = false;
        for (const PassPlan& p : plan->passes) anyNet = anyNet || !p.graph.empty();
        if (path != Path::Turbo && !anyNet) continue;
        f64 ms = 0.0;
        u64 peak = 0;
        for (const PassPlan& p : plan->passes) {
            const f64 frames = static_cast<f64>(p.frames);
            const f64 srcFrames = static_cast<f64>(source.durationUs) / 1e6 * p.sourceFps;
            const f64 decodeMs = srcFrames * 4.0 * static_cast<f64>(p.sourceW) * p.sourceH / 2.0736e6;
            const f64 encodeMs = frames * 6.0 * static_cast<f64>(p.outW) * p.outH / 2.0736e6;
            ms += decodeMs + encodeMs + frames * turbo_units(p, r) * turboMs;
            u32 w = p.renderW, h = p.renderH;
            for (const StageDescriptor* d : p.graph) {
                const f64 mp = static_cast<f64>(w) * h / 1e6;
                const f64 perMp = d->msPerMegapixel ? d->msPerMegapixel(profile)
                                : (profile.liteMsPerTile > 0.0f ? profile.liteMsPerTile : 400.0) / 0.0128;
                // As etapas antes do Motion só processam os quadros de ORIGEM
                // (o grafo copia o quadro repetido); o Motion, todos.
                const bool graphMotion = std::any_of(p.graph.begin(), p.graph.end(),
                    [](const StageDescriptor* g) { return g->kind == StageKind::Motion; });
                const f64 stageFrames = d->kind == StageKind::Motion ? frames : (graphMotion ? std::min(frames, srcFrames) : frames);
                ms += stageFrames * perMp * mp;
                if (d->kind == StageKind::Upscale) { w = p.outW; h = p.outH; }
                peak = std::max<u64>(peak, static_cast<u64>(w) * h * 3 / 2 * 8 + (160ull << 20));
            }
            // Render: ~8 alvos RGBA16F do tamanho da composição.
            peak = std::max<u64>(peak, static_cast<u64>(p.renderW) * p.renderH * 8 * 8);
        }
        if (base.stabilize) ms += static_cast<f64>(source.durationUs) / 1e6 * source.fps * 6.0;   // análise ¼
        e.seconds[static_cast<usize>(path)] = ms / 1000.0;
        if (path == chosen || (chosen == Path::Turbo && path == Path::Turbo)) {
            e.peakBytes = peak;
            e.outputWidth = plan->outW;
            e.outputHeight = plan->outH;
            e.outputFps = plan->outFps;
        }
    }
    // O pedido sem rede cai para Turbo: a estimativa do escolhido é a do Turbo.
    const usize ci = static_cast<usize>(chosen);
    if (e.seconds[ci] < 0.0) {
        e.seconds[0] = e.seconds[static_cast<usize>(Path::Turbo)];
        if (!e.outputWidth) {
            Recipe r = base;
            r.path = Path::Turbo;
            if (auto plan = plan_job(r, profile, source, caps, registry); plan.ok()) {
                e.outputWidth = plan->outW;
                e.outputHeight = plan->outH;
                e.outputFps = plan->outFps;
            }
        }
    } else {
        e.seconds[0] = e.seconds[ci];
    }
    return e;
}

Path suggest_path(const DeviceProfile& p) noexcept {
    if (!p.measured) return Path::Turbo;
    const bool netOk = p.liteMsPerTile > 0.0f && (p.vulkanFp16 || p.abi64);
    if (!netOk) return Path::Turbo;
    if (p.abi64 && p.vulkanFp16 && p.liteMsPerTile <= 60.0f && p.freeRamBytes >= (3ull << 30)) return Path::Max;
    if (p.abi64 && p.liteMsPerTile <= 400.0f && p.freeRamBytes >= (1ull << 30)) return Path::Lite;
    return Path::Turbo;
}

std::string profile_to_json(const DeviceProfile& p, const std::string& stamp) {
    std::string note = p.backendNote;
    for (char& c : note) if (c == '"' || c == '\\' || c == '\n' || c == '\r') c = ' ';
    std::string st = stamp;
    for (char& c : st) if (c == '"' || c == '\\' || c == '\n' || c == '\r') c = ' ';
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "{\n"
        "\"version\": 1,\n"
        "\"stamp\": \"%s\",\n"
        "\"measured\": %d,\n"
        "\"abi64\": %d,\n"
        "\"freeRamBytes\": %llu,\n"
        "\"vulkan\": %d,\n"
        "\"vulkanFp16\": %d,\n"
        "\"gles3\": %d,\n"
        "\"npu\": %d,\n"
        "\"turboMsPerFrame\": %.4f,\n"
        "\"liteMsPerTile\": %.4f,\n"
        "\"thermalAfter\": %.4f,\n"
        "\"defaultPath\": %u,\n"
        "\"tile\": %u,\n"
        "\"tileMargin\": %u,\n",
        st.c_str(), p.measured ? 1 : 0, p.abi64 ? 1 : 0, static_cast<unsigned long long>(p.freeRamBytes),
        p.vulkan ? 1 : 0, p.vulkanFp16 ? 1 : 0, p.gles3 ? 1 : 0, p.npu ? 1 : 0,
        static_cast<f64>(p.turboMsPerFrame), static_cast<f64>(p.liteMsPerTile), static_cast<f64>(p.thermalAfter),
        static_cast<u32>(p.defaultPath), p.tile, p.tileMargin);
    return std::string(buf) + "\"backendNote\": \"" + note + "\"\n}\n";
}

bool profile_from_json(const std::string& text, DeviceProfile& p, std::string& stamp) {
    auto find = [&](const char* key) -> const char* {
        const std::string k = std::string("\"") + key + "\":";
        const usize at = text.find(k);
        if (at == std::string::npos) return nullptr;
        const char* v = text.c_str() + at + k.size();
        while (*v == ' ') ++v;
        return v;
    };
    auto str = [&](const char* key, std::string& out) {
        const char* v = find(key);
        if (!v || *v != '"') return false;
        const char* end = std::strchr(v + 1, '"');
        if (!end) return false;
        out.assign(v + 1, end);
        return true;
    };
    auto num = [&](const char* key, f64& out) {
        const char* v = find(key);
        if (!v) return false;
        char* end = nullptr;
        out = std::strtod(v, &end);
        return end != v && std::isfinite(out);
    };
    f64 version = 0, v = 0;
    if (!num("version", version) || version != 1.0 || !str("stamp", stamp)) return false;
    DeviceProfile q;
    if (!num("measured", v)) return false;
    q.measured = v != 0.0;
    if (num("abi64", v)) q.abi64 = v != 0.0;
    if (num("freeRamBytes", v)) q.freeRamBytes = static_cast<u64>(std::max(0.0, v));
    if (num("vulkan", v)) q.vulkan = v != 0.0;
    if (num("vulkanFp16", v)) q.vulkanFp16 = v != 0.0;
    if (num("gles3", v)) q.gles3 = v != 0.0;
    if (num("npu", v)) q.npu = v != 0.0;
    if (num("turboMsPerFrame", v)) q.turboMsPerFrame = static_cast<f32>(v);
    if (num("liteMsPerTile", v)) q.liteMsPerTile = static_cast<f32>(v);
    if (num("thermalAfter", v)) q.thermalAfter = static_cast<f32>(v);
    if (num("defaultPath", v)) q.defaultPath = static_cast<Path>(std::clamp(static_cast<i32>(v), 1, 3));
    if (num("tile", v)) q.tile = static_cast<u32>(std::clamp(v, 64.0, 128.0));
    if (num("tileMargin", v)) q.tileMargin = static_cast<u32>(std::clamp(v, 8.0, 12.0));
    str("backendNote", q.backendNote);
    p = std::move(q);
    return true;
}

} // namespace aurea::hyper
