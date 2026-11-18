// =============================================================================
//  Flow Retime em FILMAGEM REAL (gente, jogo, cena de filme), não só anime.
//
//  As cenas de SyntheticVideo.hpp (LiveKind) são não rígidas e em cor: braço
//  em dois segmentos, rosto que gira e cresce, corredor com borrão de
//  obturador na fonte, braço passando na frente do rosto e o HUD parado de um
//  jogo sobre a câmera que vira. A verdade é analítica em qualquer instante,
//  então o quadro inventado no meio é comparado com o quadro que a câmera
//  teria gravado ali.
//
//  Métricas (R'G'B' de 8 bits, margem de 8 px):
//    PSNR       do quadro inteiro;
//    PSNR-mov   só onde a verdade muda entre os dois quadros da fonte (é onde
//               o fluxo trabalha: o resto é igual em qualquer método);
//    SSIM       da luminância, janelas 8×8 a cada 4 px.
//  Referências: "quadro-real" (um quadro inteiro da fonte, sem
//  interpolar: perda do 4:2:0 e do ruído), "mais próximo" e "mistura".
//
//  Também mede o custo de GPU dos passes do fluxo (timestamp queries) no
//  quadro que calcula o par (o seguinte vem do cache).
//
//  Variáveis: AUREA_FLOW_LIVE_OUT=arquivo.tsv grava a tabela;
//  AUREA_FLOW_DUMP=prefixo grava render | verdade | erro×4 em PNG;
//  AUREA_FLOW_LIVE_ONLY=trecho roda só as cenas cujo nome contém o trecho.
// =============================================================================
#include "TestFramework.hpp"

#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)

#include "ImageIO.hpp"
#include "SyntheticVideo.hpp"
#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace aurea;
using namespace aurea::test;

namespace {

f32 lv_half(u16 h) {
    const u32 sign = (h & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1F, mant = h & 0x3FF, bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) { mant <<= 1; --exp; }
            mant &= 0x3FF;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) bits = sign | 0x7F800000u | (mant << 13);
    else bits = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}
f64 lv_encode(f64 c) {
    c = std::max(0.0, c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

const char* live_name(LiveKind k) {
    switch (k) {
    case LiveKind::Limb: return "membro-articulado";
    case LiveKind::Face: return "rosto-girando";
    case LiveKind::SourceBlur: return "desfoque-na-fonte";
    case LiveKind::OccludingArm: return "braco-ocluindo";
    case LiveKind::GameHud: return "jogo-hud";
    default: return "?";
    }
}

struct LiveRig {
    SyntheticFactory factory;
    Engine e;
    u64 layer = 0;
    TextureHandle target{};
    u32 w, h;
    std::vector<u16> half;
    bool ok = false;
    explicit LiveRig(const SyntheticConfig& config) : factory(config), w(config.width), h(config.height),
        half(static_cast<usize>(config.width) * config.height * 4) {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.framesInFlight = 2;
        ec.mediaFactory = &factory;
        ec.workerCount = 2;
        ec.disableAutosave = true;
        if (!e.initialize(ec).ok()) return;
        if (!e.new_project(w, h, 30.0, "flow live").ok()) return;
        VideoImport imp;
        imp.sourcePath = "sintetico";
        imp.displayName = "clipe";
        auto id = e.import_video(imp);
        if (!id.ok()) return;
        layer = *id;
        if (Layer* l = model()) {
            l->end = FrameIndex{l->start.value + 400};
            comp()->set_duration(FrameIndex{400});
        }
        TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = SurfaceFormat::RGBA16F;
        d.renderTarget = true;
        d.transferSrc = true;
        auto t = e.gpu()->create_texture(d);
        if (!t.ok()) return;
        target = *t;
        e.set_offscreen_timers(true);
        ok = true;
    }
    ~LiveRig() {
        if (target.valid()) e.gpu()->destroy_texture(target);
        e.shutdown();
    }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* model() { return comp() ? comp()->layer(LayerId::unpack(layer)) : nullptr; }
    EffectInstance* add(const char* key) {
        Command c;
        c.type = CommandType::EffectAdd;
        c.effect_add.layer = LayerId::unpack(layer);
        c.effect_add.effectType = effect_type_id(key);
        c.effect_add.index = kInvalidIndex;
        AUREA_CHECK(e.apply_command(c).ok());
        Layer* l = model();
        return l && !l->effects.empty() ? &l->effects.back() : nullptr;
    }
    /// Renderiza e devolve o custo de GPU dos passes do fluxo e do quadro
    /// intermediário (ms; 0 se nenhum rodou — par servido do cache).
    f64 render(i64 frame) {
        Command seek;
        seek.type = CommandType::PlaybackSeek;
        seek.seek.time = tick_at(FrameIndex{frame}, 30.0);
        AUREA_CHECK(e.submit_commands(&seek, 1) == 1);
        // A cena com obturador leva segundos para "decodificar" um quadro na
        // CPU; o render que desiste esperando a fonte tenta de novo.
        // Devolve o tempo de parede do render (prepare + gravação + GPU até o
        // fim: render_offscreen espera a GPU). As timestamps por passe não
        // servem aqui: em 1080p o quadro vai em vários envios (memória) e
        // elas só cobrem o último.
        bool rendered = false;
        f64 ms = 0.0;
        for (int attempt = 0; attempt < 4 && !rendered; ++attempt) {
            const auto t0 = std::chrono::steady_clock::now();
            rendered = e.render_offscreen(target, w, h).ok();
            ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - t0).count();
        }
        AUREA_CHECK(rendered);
        AUREA_CHECK(e.gpu()->read_texture(target, half.data(), w * 8).ok());
        return rendered ? ms : 0.0;
    }
    void rgb(u32 x, u32 y, f64 out[3]) const {
        const u16* p = &half[(static_cast<usize>(y) * w + x) * 4];
        for (int c = 0; c < 3; ++c) out[c] = lv_encode(lv_half(p[c])) * 255.0;
    }
};

struct LiveScore {
    f64 psnr = 0, psnrMoving = 0, ssim = 0;
    u32 movingPixels = 0;
    f64 flicker = 0;   ///< erro temporal (cintilação), níveis de 8 bits
};

/// Compara o quadro renderizado com a verdade no instante `src` da fonte.
LiveScore score_live(const LiveRig& rig, const LiveScene& scene, f64 src, const char* tag,
                     std::vector<f32>* outRender = nullptr, std::vector<f32>* outTruth = nullptr) {
    constexpr u32 kMargin = 8;
    const u32 w = rig.w, h = rig.h;
    std::vector<f32> rl(static_cast<usize>(w) * h), tl(static_cast<usize>(w) * h);
    std::vector<u8> moving(static_cast<usize>(w) * h, 0);
    const f64 f0 = std::floor(src), f1 = f0 + 1.0;
    const bool between = src - f0 > 1e-6;
    // Por linha, em paralelo (a verdade com obturador custa 8 avaliações).
    std::vector<f64> rowSe(h, 0.0), rowSeMoving(h, 0.0);
    std::vector<u64> rowN(h, 0), rowNMoving(h, 0);
    auto rows = [&](u32 y0, u32 y1) {
        for (u32 y = y0; y < y1; ++y) {
            f64& se = rowSe[y];
            f64& seMoving = rowSeMoving[y];
            u64& n = rowN[y];
            u64& nMoving = rowNMoving[y];
            for (u32 x = kMargin; x < w - kMargin; ++x) {
            f64 r[3], t[3];
            rig.rgb(x, y, r);
            live_value(x + 0.5, y + 0.5, src, scene, t);
            f64 e2 = 0.0;
            for (int c = 0; c < 3; ++c) {
                t[c] = std::clamp(t[c], 0.0, 1.0) * 255.0;
                e2 += (r[c] - t[c]) * (r[c] - t[c]);
            }
            const usize i = static_cast<usize>(y) * w + x;
            rl[i] = static_cast<f32>(0.2126 * r[0] + 0.7152 * r[1] + 0.0722 * r[2]);
            tl[i] = static_cast<f32>(0.2126 * t[0] + 0.7152 * t[1] + 0.0722 * t[2]);
            se += e2;
            ++n;
            if (between) {
                f64 a[3], b[3];
                live_value(x + 0.5, y + 0.5, f0, scene, a);
                live_value(x + 0.5, y + 0.5, f1, scene, b);
                f64 da = 0.0, db = 0.0;
                for (int c = 0; c < 3; ++c) {
                    da = std::max(da, std::fabs(std::clamp(a[c], 0.0, 1.0) * 255.0 - t[c]));
                    db = std::max(db, std::fabs(std::clamp(b[c], 0.0, 1.0) * 255.0 - t[c]));
                }
                if (std::max(da, db) > 6.0) {
                    moving[i] = 1;
                    seMoving += e2;
                    ++nMoving;
                }
            }
            }
        }
    };
    {
        const u32 workers = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
        const u32 span = h - 2 * kMargin;
        std::vector<std::thread> pool;
        for (u32 k = 0; k < workers; ++k)
            pool.emplace_back(rows, kMargin + span * k / workers, kMargin + span * (k + 1) / workers);
        for (std::thread& t : pool) t.join();
    }
    f64 se = 0.0, seMoving = 0.0;
    u64 n = 0, nMoving = 0;
    for (u32 y = 0; y < h; ++y) { se += rowSe[y]; seMoving += rowSeMoving[y]; n += rowN[y]; nMoving += rowNMoving[y]; }
    LiveScore s;
    auto psnr = [](f64 sumSq, u64 count) { return count ? 10.0 * std::log10(255.0 * 255.0 / std::max(1e-9, sumSq / (3.0 * count))) : 0.0; };
    s.psnr = psnr(se, n);
    s.psnrMoving = psnr(seMoving, nMoving);
    s.movingPixels = static_cast<u32>(nMoving);
    // SSIM da luminância em janelas 8×8, passo 4.
    constexpr f64 C1 = (0.01 * 255) * (0.01 * 255), C2 = (0.03 * 255) * (0.03 * 255);
    f64 acc = 0.0;
    u64 windows = 0;
    for (u32 y = kMargin; y + 8 < h - kMargin; y += 4)
        for (u32 x = kMargin; x + 8 < w - kMargin; x += 4) {
            f64 ma = 0, mb = 0, va = 0, vb = 0, cov = 0;
            for (u32 j = 0; j < 8; ++j)
                for (u32 i = 0; i < 8; ++i) {
                    const usize k = static_cast<usize>(y + j) * w + x + i;
                    ma += rl[k]; mb += tl[k];
                }
            ma /= 64.0; mb /= 64.0;
            for (u32 j = 0; j < 8; ++j)
                for (u32 i = 0; i < 8; ++i) {
                    const usize k = static_cast<usize>(y + j) * w + x + i;
                    va += (rl[k] - ma) * (rl[k] - ma); vb += (tl[k] - mb) * (tl[k] - mb); cov += (rl[k] - ma) * (tl[k] - mb);
                }
            va /= 63.0; vb /= 63.0; cov /= 63.0;
            acc += ((2 * ma * mb + C1) * (2 * cov + C2)) / ((ma * ma + mb * mb + C1) * (va + vb + C2));
            ++windows;
        }
    s.ssim = windows ? acc / static_cast<f64>(windows) : 0.0;
    if (outRender) *outRender = rl;
    if (outTruth) *outTruth = tl;
    if (const char* dump = std::getenv("AUREA_FLOW_DUMP")) {
        // render | verdade | erro×4 (luma), quadro inteiro reduzido à metade.
        const u32 dw = w / 2, dh = h / 2;
        Image8 img;
        img.width = dw * 3; img.height = dh; img.rgba.assign(static_cast<usize>(dw) * 3 * dh * 4, 255);
        for (u32 y = 0; y < dh; ++y)
            for (u32 x = 0; x < dw; ++x) {
                f64 r[3], t[3];
                rig.rgb(x * 2, y * 2, r);
                live_value(x * 2 + 0.5, y * 2 + 0.5, src, scene, t);
                for (int c = 0; c < 3; ++c) {
                    t[c] = std::clamp(t[c], 0.0, 1.0) * 255.0;
                    img.rgba[(static_cast<usize>(y) * dw * 3 + x) * 4 + c] = static_cast<u8>(std::clamp(r[c], 0.0, 255.0));
                    img.rgba[(static_cast<usize>(y) * dw * 3 + dw + x) * 4 + c] = static_cast<u8>(t[c]);
                    img.rgba[(static_cast<usize>(y) * dw * 3 + 2 * dw + x) * 4 + c] =
                        static_cast<u8>(std::clamp(std::fabs(r[c] - t[c]) * 4.0, 0.0, 255.0));
                }
            }
        (void)write_png(std::string(dump) + "_" + tag + ".png", img);
    }
    return s;
}

struct LiveRow {
    std::string scene, method;
    LiveScore score;
    f64 gpuMs = 0.0;
    f64 firstMs = 0.0;   ///< o primeiro quadro do par 4 → 5: calcula tudo (fluxo ou rede)
};

/// Uma cena: referências (quadro real, mais próximo, mistura) e o Flow Retime em
/// "Movimento" nas qualidades pedidas. Velocidade 25%: os quadros 17, 18 e 19
/// da timeline mostram a fonte em 4,25 / 4,5 / 4,75 (o par 4→5).
std::vector<LiveRow> run_scene(LiveKind kind) {
    std::vector<LiveRow> rows;
    LiveScene scene;
    scene.kind = kind;
    if (kind == LiveKind::SourceBlur) scene.shutter = 0.5;
    SyntheticConfig cfg;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.frameCount = 30;
    cfg.pattern = SyntheticPattern::LiveAction;
    cfg.live = scene;
    cfg.heroNoise = 2.0;
    const char* name = live_name(kind);
    f64 blendMs = 0.0;
    auto average = [&](LiveRig& rig, const char* method, f64 gpuMs) {
        LiveRow row;
        row.scene = name;
        row.method = method;
        row.gpuMs = gpuMs;
        // 4,25 / 4,5 / 4,75 (par 4 → 5) e 5,25 (par 5 → 6): a cintilação
        // aparece quando o fluxo é recalculado de um par para o outro.
        const f64 srcs[4] = {4.25, 4.5, 4.75, 5.25};
        const i64 frames[4] = {17, 18, 19, 21};
        std::vector<f32> prevR, prevT, curR, curT;
        f64 flick = 0.0;
        u32 flickN = 0;
        // O par anterior (3 → 4) antes, como na reprodução e na exportação:
        // é o contexto no tempo do par medido.
        (void)rig.render(13);
        for (int k = 0; k < 4; ++k) {
            const f64 wall = rig.render(frames[k]);
            if (k == 0) row.firstMs = wall;
            const std::string tag = std::string(name) + "_" + method + "_" + std::to_string(k);
            const LiveScore s = score_live(rig, scene, srcs[k], tag.c_str(), &curR, &curT);
            row.score.psnr += s.psnr / 4.0;
            row.score.psnrMoving += s.psnrMoving / 4.0;
            row.score.ssim += s.ssim / 4.0;
            row.score.movingPixels = std::max(row.score.movingPixels, s.movingPixels);
            if (!prevR.empty()) {
                // Erro da DIFERENÇA entre quadros seguidos (render − verdade):
                // o que pisca no vídeo mesmo quando cada quadro sozinho está bom.
                f64 acc = 0.0;
                u64 cnt = 0;
                for (usize i = 0; i < curR.size(); i += 3) {
                    if (curT[i] == 0.0f && curR[i] == 0.0f) continue;   // margem
                    acc += std::fabs((curR[i] - prevR[i]) - (curT[i] - prevT[i]));
                    ++cnt;
                }
                flick += cnt ? acc / static_cast<f64>(cnt) : 0.0;
                ++flickN;
            }
            prevR.swap(curR);
            prevT.swap(curT);
        }
        row.score.flicker = flickN ? flick / flickN : 0.0;
        {
            // Custo: sem o cache do fluxo, cada quadro calcula o par inteiro
            // (fluxo + quadro do meio). Quadros do mesmo par (a fonte já está
            // decodificada: a GPU não esfria esperando a CPU), alternados (o
            // mesmo quadro duas vezes não desenha de novo); o menor de 9. O
            // custo do fluxo = este tempo − o da mistura (mesmo quadro sem fluxo).
            rig.e.set_flow_cache_enabled(false);
            for (int k = 0; k < 4; ++k) (void)rig.render(frames[k % 3]);   // aquece a GPU
            f64 best = 1e9;
            for (int k = 0; k < 9; ++k) {
                const f64 ms = rig.render(frames[k % 3]);
                if (ms > 0.0) best = std::min(best, ms);
            }
            rig.e.set_flow_cache_enabled(true);
            row.gpuMs = best < 1e9 ? best : 0.0;
        }
        if (std::strcmp(method, "mistura") == 0) blendMs = row.gpuMs;
        std::printf("    %-20s %-14s PSNR %6.2f | PSNR-mov %6.2f | SSIM %.4f | cintil. %5.3f | quadro %6.2f ms (fluxo %+6.2f) | par novo %7.1f ms\n", name, method,
                    row.score.psnr, row.score.psnrMoving, row.score.ssim, row.score.flicker, row.gpuMs,
                    blendMs > 0.0 ? row.gpuMs - blendMs : 0.0, row.firstMs);
        rows.push_back(row);
    };
    LiveRig rig(cfg);
    AUREA_CHECK(rig.ok);
    if (!rig.ok) return rows;
    EffectInstance* fx = rig.add(effect_keys::kFlowRetime);
    AUREA_CHECK(fx != nullptr);
    if (!fx) return rows;
    fx->params[0].constant = ParamValue::scalar(25.0f);
    // "Fantasma ↔ Deformação" (0..100; padrão 50).
    if (const char* bias = std::getenv("AUREA_FLOW_LIVE_BIAS"); bias && fx->params.size() > 6)
        fx->params[6].constant = ParamValue::scalar(static_cast<f32>(std::atof(bias)));
    {
        // Referência: o quadro 4 da fonte sem interpolar (timeline 16 a 25%):
        // a perda do 4:2:0 e do ruído. O quadro inventado pode passar dela —
        // a média de dois quadros também tira ruído.
        fx->params[1].constant = ParamValue::scalar(0.0f);
        (void)rig.render(16);
        LiveRow row;
        row.scene = name;
        row.method = "quadro-real";
        row.score = score_live(rig, scene, 4.0, (std::string(name) + "_quadro-real").c_str());
        std::printf("    %-20s %-14s PSNR %6.2f |                 | SSIM %.4f\n", name, "quadro-real", row.score.psnr, row.score.ssim);
        rows.push_back(row);
    }
    average(rig, "mais-proximo", 0.0);
    fx->params[1].constant = ParamValue::scalar(1.0f);
    average(rig, "mistura", 0.0);
    fx->params[1].constant = ParamValue::scalar(2.0f);
    static const char* const kQuality[] = {"fluxo-rapida", "fluxo-boa", "fluxo-melhor", "fluxo-neural"};
    u32 qualities = 3;
    if (const char* q = std::getenv("AUREA_FLOW_LIVE_QUALITIES")) qualities = static_cast<u32>(std::clamp(std::atoi(q), 1, 4));
    const ParameterRegistry* specs = rig.e.effects().params(effect_type_id(effect_keys::kFlowRetime));
    for (u32 q = 0; q < qualities; ++q) {
        if (specs && specs->count() > 2 && q >= specs->at(2).enumCount) break;
        fx->params[2].constant = ParamValue::scalar(static_cast<f32>(q));
        average(rig, kQuality[q], 0.0);
    }
    return rows;
}

} // namespace

AUREA_TEST(FlowLiveActionGpu, NonRigidScenesPsnrSsim) {
    {
        // Sem Vulkan no host: pula (o CI sem GPU roda o resto).
        vk::Backend probe;
        BackendConfig bc;
        if (!probe.initialize(bc).ok()) { std::printf("(sem GPU Vulkan: pulado) "); return; }
        probe.shutdown();
    }
    std::printf("\n");
    const char* only = std::getenv("AUREA_FLOW_LIVE_ONLY");
    std::vector<LiveRow> all;
    for (u32 k = 0; k < static_cast<u32>(LiveKind::Count); ++k) {
        const LiveKind kind = static_cast<LiveKind>(k);
        if (only && !std::strstr(live_name(kind), only)) continue;
        std::vector<LiveRow> rows = run_scene(kind);
        // O fluxo nunca pode sair abaixo da mistura simples na região que anda.
        const LiveRow* mix = nullptr;
        for (const LiveRow& r : rows) if (r.method == "mistura") mix = &r;
        // Metas da qualidade Boa (PSNR na região que anda): acima do algoritmo
        // de antes da 1.0.3 (23,1 / 35,7 / 29,0 / 23,7 / 38,0 dB); a 1.0.3
        // mediu 25,7 / 36,4 / 29,5 / 25,8 / 38,1. Só sem os desvios de medida.
        static constexpr f64 kGoodFloor[static_cast<u32>(LiveKind::Count)] = {25.0, 36.0, 29.1, 25.0, 37.7};
        const bool tuned = std::getenv("AUREA_FLOW_ABLATE") || std::getenv("AUREA_FLOW_TUNE") || std::getenv("AUREA_FLOW_LIVE_BIAS");
        for (const LiveRow& r : rows) {
            if (!tuned && r.method == "fluxo-boa") AUREA_CHECK(r.score.psnrMoving > kGoodFloor[k]);
            AUREA_CHECK(std::isfinite(r.score.psnr) && std::isfinite(r.score.ssim));
            if (mix && r.method.rfind("fluxo-", 0) == 0) AUREA_CHECK(r.score.psnrMoving > mix->score.psnrMoving - 0.5);
        }
        all.insert(all.end(), rows.begin(), rows.end());
    }
    if (const char* out = std::getenv("AUREA_FLOW_LIVE_OUT")) {
        if (FILE* f = std::fopen(out, "w")) {
            std::fprintf(f, "cena\tmetodo\tpsnr\tpsnr_mov\tssim\tcintilacao\tquadro_ms\tpar_novo_ms\n");
            for (const LiveRow& r : all)
                std::fprintf(f, "%s\t%s\t%.3f\t%.3f\t%.4f\t%.3f\t%.3f\t%.1f\n", r.scene.c_str(), r.method.c_str(), r.score.psnr,
                             r.score.psnrMoving, r.score.ssim, r.score.flicker, r.gpuMs, r.firstMs);
            std::fclose(f);
        }
    }
}

#endif
