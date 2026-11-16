// =============================================================================
//  Aurea / platform / desktop / DesktopDecodeBench.cpp
//
//  Medida do decode de vídeo do Aurea PC. NÃO vai no app. Abre o arquivo pelo
//  MESMO windows::MediaFactory que a aurea.dll usa e mede:
//    - sequencial: ms por quadro (média, p50, p95, máx) e onde vai o tempo
//      (decoder do MF, textura → memória, cópia dos planos);
//    - seeks: keyframe + decode até cobrir o alvo, em pontos espalhados;
//    - paralelo: N decoders do mesmo arquivo ao mesmo tempo (várias camadas).
//  Sem janela e sem áudio (só vídeo é aberto).
//
//  Uso: aurea_pc_decode_bench.exe <video> [quadros=300] [paralelos=3]
// =============================================================================
#include "WindowsMedia.hpp"
#include <limits>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double now_ms() { return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count(); }

double cpu_seconds() {
    FILETIME c{}, e{}, k{}, u{};
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    auto v = [](FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32 | f.dwLowDateTime) * 1e-7; };
    return v(k) + v(u);
}

struct Series {
    std::vector<double> v;
    double pct(double p) {
        if (v.empty()) return 0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, static_cast<size_t>(p * (s.size() - 1) + 0.5))];
    }
    double avg() const { double t = 0; for (double x : v) t += x; return v.empty() ? 0 : t / v.size(); }
    double max() const { return v.empty() ? 0 : *std::max_element(v.begin(), v.end()); }
};

const char* fmt_name(aurea::PixelFormat f) {
    static const char* k[] = {"?", "RGBA8", "RGBA16F", "NV12", "P010", "NV21", "YUV420P", "Opaque"};
    return k[std::min<unsigned>(static_cast<unsigned>(f), 7u)];
}

std::unique_ptr<aurea::VideoDecoderBackend> open(aurea::windows::MediaFactory& factory, const std::string& path, double* ms = nullptr) {
    aurea::Asset asset;
    asset.sourcePath = path;
    const double t0 = now_ms();
    auto dec = factory.open_video(asset, aurea::MediaPriority::Preview);
    if (ms) *ms = now_ms() - t0;
    return dec;
}

// Decode sequencial de `count` quadros; devolve ms por next_frame com quadro.
Series sequential(aurea::VideoDecoderBackend& dec, int count, aurea::FrameRef* keep = nullptr) {
    Series s;
    bool eos = false;
    while (!eos && static_cast<int>(s.v.size()) < count) {
        aurea::FrameRef f;
        aurea::i64 pts = 0;
        const double t0 = now_ms();
        if (!dec.next_frame(std::numeric_limits<aurea::i64>::min() / 4, f, pts, eos).ok()) { std::printf("  erro no decode\n"); break; }
        const double ms = now_ms() - t0;
        if (f) {
            s.v.push_back(ms);
            if (keep) *keep = f;
            // Soma (FNV-1a) da parte visível dos planos do 100º quadro: compara os
            // caminhos de leitura (AUREA_MF_LOCK2D=1 contra o anel).
            if (s.v.size() == 100 && f->planeCount == 2) {
                unsigned long long h = 1469598103934665603ull;
                const unsigned bpp = f->format == aurea::PixelFormat::P010 ? 2 : 1;
                for (unsigned p = 0; p < 2; ++p)
                    for (unsigned y = 0; y < (p ? (f->visibleHeight + 1) / 2 : f->visibleHeight); ++y) {
                        const unsigned char* row = f->planes[p] + static_cast<size_t>(y + (p ? f->cropTop / 2 : f->cropTop)) * f->strides[p];
                        for (size_t x = 0; x < static_cast<size_t>(f->visibleWidth) * bpp; ++x) h = (h ^ row[x]) * 1099511628211ull;
                    }
                std::printf("  soma dos planos do quadro 100 (pts %lld): %016llx\n", static_cast<long long>(f->ptsUs), h);
            }
        }
    }
    return s;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("uso: aurea_pc_decode_bench <video> [quadros] [paralelos]\n"); return 2; }
    const std::string path = aurea::windows::utf8(argv[1]);
    const int count = argc > 2 ? std::max(30, _wtoi(argv[2])) : 300;
    const int parallel = argc > 3 ? std::max(0, _wtoi(argv[3])) : 3;
    aurea::windows::initialize_media();
    aurea::windows::MediaFactory factory;

    double openMs = 0;
    auto dec = open(factory, path, &openMs);
    if (!dec) { std::printf("FALHA: nao abriu %s\n", path.c_str()); return 1; }
    const auto& info = dec->info();
    std::printf("arquivo: %ls\n", std::filesystem::path(argv[1]).filename().c_str());
    std::printf("  %s %ux%u %.2f fps, %.1f s | %s | hw=%d | abrir %.0f ms\n", info.codec, info.codedWidth, info.codedHeight, info.fps,
                info.durationUs / 1e6, info.decoderName, info.hardwareDecoder ? 1 : 0, openMs);

    // --- sequencial ---------------------------------------------------------
    aurea::FrameRef last;
    const double cpu0 = cpu_seconds(), wall0 = now_ms();
    Series seq = sequential(*dec, count, &last);
    const double wall = now_ms() - wall0, cpu = cpu_seconds() - cpu0;
    aurea::windows::VideoDecodeTimings t{};
    aurea::windows::video_decode_timings(dec.get(), t);
    std::printf("  hw (1o quadro) = %d | caminho: %s | quadro %s %ux%u\n", dec->info().hardwareDecoder ? 1 : 0, t.path,
                last ? fmt_name(last->format) : "-", last ? last->width : 0, last ? last->height : 0);
    // Os primeiros quadros pagam a partida do decoder: regime = sem os 10 primeiros.
    Series steady;
    steady.v.assign(seq.v.begin() + std::min<size_t>(10, seq.v.size()), seq.v.end());
    std::printf("SEQUENCIAL %zu quadros em %.0f ms = %.1f fps | CPU %.0f%% de um nucleo\n", seq.v.size(), wall,
                wall > 0 ? seq.v.size() * 1000.0 / wall : 0, wall > 0 ? cpu * 1000.0 / wall * 100.0 : 0);
    std::printf("  ms/quadro (regime): media %.2f  p50 %.2f  p95 %.2f  max %.2f | 1o quadro %.1f ms\n", steady.avg(), steady.pct(0.5),
                steady.pct(0.95), steady.max(), seq.v.empty() ? 0 : seq.v[0]);
    if (t.samples)
        std::printf("  por amostra: decoder %.2f ms | textura->memoria %.2f ms | copia %.2f ms  (%llu amostras, %llu quadros)\n",
                    t.readMs / t.samples, t.frames ? t.mapMs / t.frames : 0, t.frames ? t.copyMs / t.frames : 0,
                    static_cast<unsigned long long>(t.samples), static_cast<unsigned long long>(t.frames));

    // --- seeks --------------------------------------------------------------
    last.reset();
    const aurea::i64 dur = info.durationUs > 0 ? info.durationUs : 10'000'000;
    const double fracs[] = {0.5, 0.1, 0.83, 0.3, 0.66, 0.05, 0.95, 0.42, 0.2, 0.75};
    Series seeks, firstOut;
    int seekMiss = 0;
    unsigned long long seekSamples = 0;
    for (double fr : fracs) {
        const aurea::i64 target = static_cast<aurea::i64>(dur * fr);
        aurea::windows::VideoDecodeTimings before{};
        aurea::windows::video_decode_timings(dec.get(), before);
        const double t0 = now_ms();
        if (!dec->seek_to_keyframe(target).ok()) { ++seekMiss; continue; }
        bool eos = false, hit = false;
        for (int guard = 0; guard < 2000 && !eos; ++guard) {
            aurea::FrameRef f;
            aurea::i64 pts = 0;
            if (!dec->next_frame(target, f, pts, eos).ok()) break;
            if (guard == 0) firstOut.v.push_back(now_ms() - t0);
            if (f && (f->covers(target) || f->ptsUs >= target)) { hit = f->covers(target) || f->ptsUs - target < 40'000; break; }
        }
        const double ms = now_ms() - t0;
        aurea::windows::VideoDecodeTimings after{};
        aurea::windows::video_decode_timings(dec.get(), after);
        seekSamples += after.samples - before.samples;
        if (!hit) ++seekMiss;
        seeks.v.push_back(ms);
    }
    std::printf("SEEK %zu alvos: media %.1f ms  p50 %.1f  max %.1f ms | sem acerto: %d | 1a amostra %.1f ms | %.1f amostras por seek\n",
                seeks.v.size(), seeks.avg(), seeks.pct(0.5), seeks.max(), seekMiss, firstOut.avg(),
                seeks.v.empty() ? 0.0 : static_cast<double>(seekSamples) / seeks.v.size());
    dec.reset();

    // --- paralelo -----------------------------------------------------------
    if (parallel > 1) {
        std::vector<std::unique_ptr<aurea::VideoDecoderBackend>> decs;
        for (int i = 0; i < parallel; ++i) if (auto d = open(factory, path)) decs.push_back(std::move(d));
        std::vector<Series> results(decs.size());
        std::vector<std::thread> threads;
        const double p0 = now_ms(), pc0 = cpu_seconds();
        for (size_t i = 0; i < decs.size(); ++i)
            threads.emplace_back([&, i] {
                // Pontos diferentes do arquivo, como camadas diferentes.
                decs[i]->seek_to_keyframe(static_cast<aurea::i64>(dur * (0.1 + 0.25 * i)) % std::max<aurea::i64>(1, dur));
                results[i] = sequential(*decs[i], count / 2);
            });
        for (auto& th : threads) th.join();
        const double pw = now_ms() - p0, pcpu = cpu_seconds() - pc0;
        size_t frames = 0;
        double worst = 0;
        for (auto& r : results) { frames += r.v.size(); worst = std::max(worst, r.avg()); }
        std::printf("PARALELO %zu decoders: %zu quadros em %.0f ms = %.1f fps no total | pior media %.2f ms/quadro | CPU %.0f%% de um nucleo\n",
                    decs.size(), frames, pw, pw > 0 ? frames * 1000.0 / pw : 0, worst, pw > 0 ? pcpu * 1000.0 / pw * 100.0 : 0);
    }
    return 0;
}
