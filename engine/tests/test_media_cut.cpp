// =============================================================================
//  1.0.2 "importar só o trecho cortado": MediaCut (remux do trecho com
//  libavformat), SourceWindow (decoders na régua do original) e a seção
//  SourceWindow do .aurea.
//
//  Os vídeos sintéticos (H.264 com B-frames e HEVC hvc1 com GOP aberto, os dois
//  com matriz de rotação de 90°) são gerados pelo ffmpeg do PATH — o mesmo
//  FFmpeg de host cujas DLLs o aurea_tests já precisa. Sem ffmpeg no PATH os
//  testes de remux avisam e não verificam nada.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/media/ImageSequence.hpp"
#include "aurea/media/MediaCut.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/media/SourceWindow.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/project/Serialization.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
}
#endif

using namespace aurea;
namespace fs = std::filesystem;

namespace {

// --- decoder falso: quadros de 40 ms, keyframe a cada 1 s ---------------------
class FakeVideo final : public VideoDecoderBackend {
public:
    explicit FakeVideo(i64 frames) : frames_(frames) {
        info_.fps = 25.0;
        info_.durationUs = frames * kFrameUs;
        info_.codedWidth = 16; info_.codedHeight = 16;
    }
    static constexpr i64 kFrameUs = 40'000;
    const VideoStreamInfo& info() const noexcept override { return info_; }
    Status seek_to_keyframe(i64 targetUs) noexcept override {
        lastSeek = targetUs;
        pos_ = std::max<i64>(0, targetUs / 1'000'000 * 1'000'000);
        return OkStatus;
    }
    Status next_frame(i64 deliverFromUs, FrameRef& out, i64& outPtsUs, bool& eos) noexcept override {
        if (pos_ >= frames_ * kFrameUs) { eos = true; return OkStatus; }
        outPtsUs = pos_;
        if (pos_ >= deliverFromUs) {
            DecodedFrame* f = new DecodedFrame();
            f->ptsUs = pos_;
            out = FrameRef::adopt(f);
        }
        pos_ += kFrameUs;
        return OkStatus;
    }
    i64 lastSeek = -1;

private:
    i64 frames_;
    i64 pos_ = 0;
    VideoStreamInfo info_{};
};

class FakeAudio final : public audio::AudioDecoderBackend {
public:
    FakeAudio() { info_.sampleRate = 48000; info_.channels = 2; info_.durationUs = 2'000'000; }
    const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    Status seek(i64 us) noexcept override { lastSeek = us; pos_ = us; return OkStatus; }
    Status read(std::vector<f32>& out, i64& ptsUs, bool& eos) noexcept override {
        out.assign(960, 0.0f);
        ptsUs = pos_;
        pos_ += 10'000;
        eos = pos_ >= info_.durationUs;
        return OkStatus;
    }
    i64 lastSeek = -1;

private:
    audio::AudioStreamInfo info_{};
    i64 pos_ = 0;
};

class FakeFactory final : public VideoSourceFactory {
public:
    bool probe(const char*, MediaProbe& out) override {
        out = MediaProbe{};
        out.hasVideo = true;
        out.video.fps = 25.0;
        out.video.durationUs = 2'000'000;
        out.video.codedWidth = 16; out.video.codedHeight = 16;
        out.hasAudio = true;
        out.audioSampleRate = 48000; out.audioChannels = 2;
        out.audioDurationUs = 2'000'000;
        return true;
    }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<FakeVideo>(50);
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override { return std::make_unique<FakeAudio>(); }
};

} // namespace

AUREA_TEST(SourceWindow, RegistryAddsAndRemovesWindows) {
    register_source_window("trecho-a.mp4", 5'000'000);
    AUREA_CHECK_EQ(source_window_us("trecho-a.mp4"), i64{5'000'000});
    AUREA_CHECK_EQ(source_window_us("outro.mp4"), i64{0});
    register_source_window("trecho-a.mp4", 0);
    AUREA_CHECK_EQ(source_window_us("trecho-a.mp4"), i64{0});
}

AUREA_TEST(SourceWindow, VideoDecoderUsesTheOriginalTimeline) {
    auto inner = std::make_unique<FakeVideo>(50);   // trecho de 2 s
    FakeVideo* raw = inner.get();
    auto dec = window_video_decoder(std::move(inner), 10'000'000);
    AUREA_CHECK_EQ(dec->info().durationUs, i64{12'000'000});

    // Seek dentro da janela: o decoder de dentro recebe o instante do trecho.
    AUREA_CHECK(dec->seek_to_keyframe(11'500'000).ok());
    AUREA_CHECK_EQ(raw->lastSeek, i64{1'500'000});
    FrameRef f;
    i64 pts = 0;
    bool eos = false;
    for (int i = 0; i < 40 && !f; ++i) AUREA_CHECK(dec->next_frame(11'500'000 - 20'000, f, pts, eos).ok());
    AUREA_CHECK(bool(f));
    if (f) {
        AUREA_CHECK_EQ(f->ptsUs, i64{11'480'000});
        AUREA_CHECK_EQ(pts, i64{11'480'000});
        AUREA_CHECK(!f->covers(3'000'000));
    }

    // Antes da janela: volta ao 1º quadro, que segura todo o trecho anterior.
    f.reset();
    AUREA_CHECK(dec->seek_to_keyframe(3'000'000).ok());
    AUREA_CHECK_EQ(raw->lastSeek, i64{0});
    AUREA_CHECK(dec->next_frame(3'000'000 - 20'000, f, pts, eos).ok());
    AUREA_CHECK(bool(f));
    if (f) {
        AUREA_CHECK_EQ(f->ptsUs, i64{0});
        AUREA_CHECK(f->covers(0));
        AUREA_CHECK(f->covers(3'000'000));
        AUREA_CHECK(f->covers(10'000'000));            // o próprio 1º quadro
        AUREA_CHECK(!f->covers(10'000'000 + FakeVideo::kFrameUs));
        AUREA_CHECK_EQ(pts, i64{10'000'000});          // a posição do decoder é a real
    }
    // O seguinte é um quadro normal, na régua do original.
    FrameRef next;
    AUREA_CHECK(dec->next_frame(0, next, pts, eos).ok());
    AUREA_CHECK(bool(next));
    if (next) AUREA_CHECK_EQ(next->ptsUs, i64{10'040'000});

    // Sem janela: o mesmo decoder, sem embrulho.
    auto plain = std::make_unique<FakeVideo>(10);
    VideoDecoderBackend* plainRaw = plain.get();
    AUREA_CHECK(window_video_decoder(std::move(plain), 0).get() == plainRaw);
}

AUREA_TEST(SourceWindow, VideoSourceShowsTheHeldFirstFrameExactly) {
    // Pela fonte de verdade (thread de decode, cache): um pedido antes da
    // janela e o 1º quadro dela saem EXATOS — o export não espera à toa.
    VideoSource source(window_video_decoder(std::make_unique<FakeVideo>(50), 10'000'000), MediaPriority::Export);
    source.start();
    source.request(DecodeRequest{2'000'000, DecodeMode::Still, 0, 1.0f, false});
    AUREA_CHECK(source.wait_for(2'000'000, 3000));
    bool exact = false;
    FrameRef held = source.frame_for(2'000'000, &exact);
    AUREA_CHECK(bool(held) && exact);
    source.request(DecodeRequest{10'000'000, DecodeMode::Still, 0, 1.0f, false});
    AUREA_CHECK(source.wait_for(10'000'000, 3000));
    source.request(DecodeRequest{11'000'000, DecodeMode::Still, 0, 1.0f, false});
    AUREA_CHECK(source.wait_for(11'000'000, 3000));
    FrameRef later = source.frame_for(11'000'000, &exact);
    AUREA_CHECK(bool(later) && exact);
    if (later) AUREA_CHECK_EQ(later->ptsUs, i64{11'000'000});
    source.stop();
}

AUREA_TEST(SourceWindow, AudioDecoderAndProbeUseTheOriginalTimeline) {
    auto inner = std::make_unique<FakeAudio>();
    FakeAudio* raw = inner.get();
    auto dec = window_audio_decoder(std::move(inner), 10'000'000);
    AUREA_CHECK_EQ(dec->info().durationUs, i64{12'000'000});
    AUREA_CHECK(dec->seek(10'500'000).ok());
    AUREA_CHECK_EQ(raw->lastSeek, i64{500'000});
    std::vector<f32> pcm;
    i64 pts = 0;
    bool eos = false;
    AUREA_CHECK(dec->read(pcm, pts, eos).ok());
    AUREA_CHECK_EQ(pts, i64{10'500'000});
    AUREA_CHECK(dec->seek(1'000'000).ok());   // antes da janela: o começo do trecho
    AUREA_CHECK_EQ(raw->lastSeek, i64{0});

    // A fábrica do motor aplica a janela registrada a sondagem, vídeo e áudio.
    FakeFactory platform;
    ImageSequenceFactory factory(&platform);
    register_source_window("trecho-fabrica.mp4", 7'000'000);
    MediaProbe probe;
    AUREA_CHECK(factory.probe("trecho-fabrica.mp4", probe));
    AUREA_CHECK_EQ(probe.video.durationUs, i64{9'000'000});
    AUREA_CHECK_EQ(probe.audioDurationUs, i64{9'000'000});
    Asset asset;
    asset.kind = AssetKind::Video;
    asset.sourcePath = "trecho-fabrica.mp4";
    auto video = factory.open_video(asset, MediaPriority::Preview);
    AUREA_CHECK(video && video->info().durationUs == 9'000'000);
    auto sound = factory.open_audio("trecho-fabrica.mp4");
    AUREA_CHECK(sound && sound->info().durationUs == 9'000'000);
    // O proxy (outro caminho, já na régua do original) não é embrulhado.
    asset.sourcePath = "proxy.mp4";
    auto proxy = factory.open_video(asset, MediaPriority::Preview);
    AUREA_CHECK(proxy && proxy->info().durationUs == 2'000'000);
    AUREA_CHECK(factory.probe("proxy.mp4", probe) && probe.video.durationUs == 2'000'000);
    register_source_window("trecho-fabrica.mp4", 0);
}

AUREA_TEST(SourceWindow, ProjectRoundTripKeepsTheWindowAndOldProjectsLoad) {
    auto created = Project::create_new(1280, 720, 30.0, "Trecho");
    AUREA_CHECK(created.ok());
    if (!created.ok()) return;
    Project original = std::move(*created);
    Asset windowed;
    windowed.kind = AssetKind::Video;
    windowed.name = "longo";
    windowed.sourcePath = "/dados/import-sources/cut-1.mp4";
    windowed.windowStartUs = 3'000'000'000;   // 50 min
    windowed.originRef = "content://media/external/video/media/7#123456";
    windowed.duration = FrameIndex{108'000};
    windowed.timebaseFps = 30.0;
    const AssetId wid = original.add_asset(windowed);
    Asset plain;
    plain.kind = AssetKind::Video;
    plain.name = "inteiro";
    plain.sourcePath = "/dados/import-sources/inteiro.mp4";
    const AssetId pid = original.add_asset(plain);
    register_source_window(windowed.sourcePath, 0);

    std::vector<u8> bytes;
    AUREA_CHECK(ProjectSerializer::encode(original, SaveOptions{}, bytes).ok());
    Project restored;
    LoadReport report;
    AUREA_CHECK(ProjectSerializer::load_bytes(restored, bytes.data(), bytes.size(), LoadOptions{}, &report).ok());
    (void)wid; (void)pid;
    const Asset* a = nullptr;
    const Asset* b = nullptr;
    restored.for_each_asset([&](AssetId, const Asset& x) {
        if (x.name == "longo") a = &x;
        if (x.name == "inteiro") b = &x;
    });
    AUREA_CHECK(a && b);
    if (a) {
        AUREA_CHECK_EQ(a->windowStartUs, i64{3'000'000'000});
        AUREA_CHECK(a->originRef == windowed.originRef);
        AUREA_CHECK(std::abs(a->source_first_frame(30.0) - 90'000.0) < 1e-6);
    }
    if (b) AUREA_CHECK(b->windowStartUs == 0 && b->originRef.empty());
    // Abrir já registra a janela do arquivo (os decoders abrem por ele).
    AUREA_CHECK_EQ(source_window_us(windowed.sourcePath), i64{3'000'000'000});
    register_source_window(windowed.sourcePath, 0);

    // Projeto sem trecho cortado não grava a seção (fica igual ao de antes).
    auto old = Project::create_new(640, 360, 30.0, "Antigo");
    if (!old.ok()) return;
    Project before = std::move(*old);
    (void)before.add_asset(plain);
    std::vector<u8> oldBytes;
    AUREA_CHECK(ProjectSerializer::encode(before, SaveOptions{}, oldBytes).ok());
    Project reopened;
    LoadReport oldReport;
    AUREA_CHECK(ProjectSerializer::load_bytes(reopened, oldBytes.data(), oldBytes.size(), LoadOptions{}, &oldReport).ok());
    bool sawWindowSection = false;
    for (SectionKind k : oldReport.sectionsRead) sawWindowSection = sawWindowSection || k == SectionKind::SourceWindow;
    AUREA_CHECK(!sawWindowSection);
    bool anyWindow = false;
    reopened.for_each_asset([&](AssetId, const Asset& x) { anyWindow = anyWindow || x.windowStartUs != 0; });
    AUREA_CHECK(!anyWindow);
}

AUREA_TEST(MediaCut, MarginPolicy) {
    AUREA_CHECK_EQ(cut_margin_us(0, 10'000'000), i64{2'000'000});            // curto: 2 s
    AUREA_CHECK_EQ(cut_margin_us(0, 600'000'000), i64{30'000'000});          // 10 min: 5% = 30 s
}

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
namespace {

const std::string kDir = "build/reference/media-cut";

bool run(const std::string& command) {
#if defined(_WIN32)
    const std::string line = "\"" + command + "\"";   // cmd /c tira o par de aspas de fora
#else
    const std::string& line = command;
#endif
    return std::system(line.c_str()) == 0;
}

/// Gera (uma vez) o vídeo sintético: 60 s, 320×240 a 30 fps, keyframe a cada
/// 1 s, áudio AAC, matriz de rotação de 90°.
std::string synthetic(bool hevc) {
    std::error_code ec;
    fs::create_directories(kDir, ec);
    const std::string name = kDir + (hevc ? "/fonte-hevc.mp4" : "/fonte-h264.mp4");
    if (fs::exists(name, ec) && fs::file_size(name, ec) > 10000) return name;
    const std::string raw = kDir + (hevc ? "/bruto-hevc.mp4" : "/bruto-h264.mp4");
    const std::string codec = hevc
        ? "-c:v libx265 -tag:v hvc1 -x265-params log-level=error:keyint=30:min-keyint=30:bframes=3"
        : "-c:v libx264 -g 30 -keyint_min 30 -sc_threshold 0 -bf 2";
    if (!run("ffmpeg -y -v error -f lavfi -i testsrc2=size=320x240:rate=30 -f lavfi -i sine=frequency=440:sample_rate=48000 "
             "-t 60 " + codec + " -pix_fmt yuv420p -c:a aac -b:a 64k -shortest \"" + raw + "\""))
        return {};
    if (!run("ffmpeg -y -v error -display_rotation:v:0 90 -i \"" + raw + "\" -c copy \"" + name + "\"")) return {};
    return name;
}

struct Picture {
    bool ok = false;
    i64 ptsUs = 0;
    u64 hash = 0;
};

/// Decodifica (software) o quadro que um player mostraria em `targetUs`.
Picture decode_at(const std::string& path, i64 targetUs) {
    Picture out;
    AVFormatContext* fc = nullptr;
    if (avformat_open_input(&fc, path.c_str(), nullptr, nullptr) < 0) return out;
    if (avformat_find_stream_info(fc, nullptr) < 0) { avformat_close_input(&fc); return out; }
    const int v = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVStream* st = v >= 0 ? fc->streams[v] : nullptr;
    const AVCodec* codec = st ? avcodec_find_decoder(st->codecpar->codec_id) : nullptr;
    AVCodecContext* cc = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!cc || avcodec_parameters_to_context(cc, st->codecpar) < 0 || avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        avformat_close_input(&fc);
        return out;
    }
    const AVRational us{1, 1'000'000};
    (void)av_seek_frame(fc, v, av_rescale_q(targetUs, us, st->time_base), AVSEEK_FLAG_BACKWARD);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    const i64 half = 16'000;
    bool done = false, flushing = false;
    while (!done) {
        if (!flushing) {
            const int r = av_read_frame(fc, pkt);
            if (r < 0) { flushing = true; (void)avcodec_send_packet(cc, nullptr); }
            else {
                if (pkt->stream_index == v) (void)avcodec_send_packet(cc, pkt);
                av_packet_unref(pkt);
            }
        }
        for (;;) {
            const int r = avcodec_receive_frame(cc, frame);
            if (r < 0) { if (flushing) done = true; break; }
            const i64 t = av_rescale_q(frame->best_effort_timestamp, st->time_base, us);
            if (t >= targetUs - half) {
                u64 h = 1469598103934665603ull;
                for (int y = 0; y < frame->height; ++y) {
                    const u8* row = frame->data[0] + static_cast<i64>(y) * frame->linesize[0];
                    for (int x = 0; x < frame->width; ++x) { h ^= row[x]; h *= 1099511628211ull; }
                }
                out = Picture{true, t, h};
                done = true;
                break;
            }
            av_frame_unref(frame);
        }
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&cc);
    avformat_close_input(&fc);
    return out;
}

struct StreamFacts {
    bool ok = false;
    f64 rotation = 0.0;
    u32 tag = 0;
    bool hasAudio = false;
    i64 firstPtsUs = -1;
};

StreamFacts facts(const std::string& path) {
    StreamFacts out;
    AVFormatContext* fc = nullptr;
    if (avformat_open_input(&fc, path.c_str(), nullptr, nullptr) < 0) return out;
    if (avformat_find_stream_info(fc, nullptr) < 0) { avformat_close_input(&fc); return out; }
    const int v = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (v >= 0) {
        AVStream* st = fc->streams[v];
        out.ok = true;
        out.tag = st->codecpar->codec_tag;
        if (const AVPacketSideData* sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                st->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX))
            out.rotation = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    }
    out.hasAudio = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
    avformat_close_input(&fc);
    const Picture first = decode_at(path, 0);
    if (first.ok) out.firstPtsUs = first.ptsUs;
    return out;
}

void check_cut(bool hevc) {
    const std::string source = synthetic(hevc);
    if (source.empty()) {
        std::printf("\n    (ffmpeg fora do PATH: remux de %s nao verificado)\n", hevc ? "HEVC" : "H.264");
        return;
    }
    const std::string dst = kDir + (hevc ? "/trecho-hevc.mp4" : "/trecho-h264.mp4");
    MediaCutRequest rq;
    rq.source = source;
    rq.destination = dst;
    rq.inUs = 30'000'000;
    rq.outUs = 36'000'000;
    MediaCutResult res;
    const Status s = cut_media(rq, res);
    AUREA_CHECK_MSG(s.ok(), s.detail().data());
    if (!s.ok()) return;
    // Janela: do keyframe ≤ 28 s (entrada − 2 s) até o 1º keyframe ≥ 38 s.
    AUREA_CHECK(res.windowStartUs <= 28'000'000 && res.windowStartUs >= 26'900'000);
    AUREA_CHECK(res.windowEndUs >= 38'000'000 && res.windowEndUs <= 39'100'000);
    AUREA_CHECK(res.bytes > 0 && res.bytes < res.sourceBytes / 3);
    std::printf("\n    %s: %llu de %llu bytes, janela %.3f..%.3f s", hevc ? "HEVC" : "H.264",
                static_cast<unsigned long long>(res.bytes), static_cast<unsigned long long>(res.sourceBytes),
                static_cast<f64>(res.windowStartUs) / 1e6, static_cast<f64>(res.windowEndUs) / 1e6);

    const StreamFacts f = facts(dst);
    AUREA_CHECK(f.ok && f.hasAudio);
    AUREA_CHECK(std::abs(std::abs(f.rotation) - 90.0) < 0.5);   // a matriz de rotação seguiu
    if (hevc) AUREA_CHECK_EQ(f.tag, static_cast<u32>(MKTAG('h', 'v', 'c', '1')));
    AUREA_CHECK_EQ(f.firstPtsUs, i64{0});                        // o trecho começa em 0

    // O mesmo instante do original, antes e depois do corte: o MESMO quadro.
    for (const i64 t : {i64{28'000'000}, i64{30'000'000}, i64{30'533'333}, i64{33'366'667}, i64{35'966'667}}) {
        const Picture a = decode_at(source, t);
        const Picture b = decode_at(dst, t - res.windowStartUs);
        AUREA_CHECK(a.ok && b.ok);
        if (!a.ok || !b.ok) continue;
        AUREA_CHECK_EQ(b.ptsUs + res.windowStartUs, a.ptsUs);
        AUREA_CHECK_MSG(a.hash == b.hash, hevc ? "quadro HEVC diferente" : "quadro H.264 diferente");
    }
}

} // namespace

AUREA_TEST(MediaCut, H264WithBFramesKeepsFramesRotationAndAudio) { check_cut(false); }
AUREA_TEST(MediaCut, HevcOpenGopKeepsHvc1FramesAndRotation) { check_cut(true); }

AUREA_TEST(MediaCut, RefusesWhenTheRangeIsMostOfTheSource) {
    const std::string source = synthetic(false);
    if (source.empty()) return;
    MediaCutRequest rq;
    rq.source = source;
    rq.destination = kDir + "/grande.mp4";
    rq.inUs = 2'000'000;
    rq.outUs = 50'000'000;
    MediaCutResult res;
    const Status s = cut_media(rq, res);
    AUREA_CHECK(s.code() == Errc::NotSupported);
    std::error_code ec;
    AUREA_CHECK(!fs::exists(rq.destination, ec));
    // Fonte que não existe: erro, nada no destino (o app copia inteiro).
    rq.source = kDir + "/nao-existe.mp4";
    rq.inUs = 1'000'000; rq.outUs = 2'000'000;
    AUREA_CHECK(!cut_media(rq, res).ok());
}

AUREA_TEST(MediaCut, ImportReusesOrWidensTheSameOrigin) {
    const std::string source = synthetic(false);
    if (source.empty()) return;
    const std::string dir = kDir + "/importados";
    std::error_code ec;
    fs::remove_all(dir, ec);
    TrimmedImportRequest rq;
    rq.source = source;
    rq.directory = dir;
    rq.originRef = "content://media/external/video/media/42#123";
    rq.extension = "mp4";
    rq.inUs = 10'000'000;
    rq.outUs = 14'000'000;
    TrimmedImportResult first;
    AUREA_CHECK(import_trimmed_media(rq, first).ok());
    AUREA_CHECK(!first.reused && !first.path.empty());

    // Corte de dentro do mesmo trecho: reaproveita, nada é cortado de novo.
    rq.inUs = 11'000'000; rq.outUs = 13'000'000;
    TrimmedImportResult inside;
    AUREA_CHECK(import_trimmed_media(rq, inside).ok());
    AUREA_CHECK(inside.reused && inside.path == first.path && inside.windowStartUs == first.windowStartUs);

    // Sobrepõe: o novo trecho cobre a união.
    rq.inUs = 13'000'000; rq.outUs = 20'000'000;
    TrimmedImportResult wider;
    AUREA_CHECK(import_trimmed_media(rq, wider).ok());
    AUREA_CHECK(!wider.reused && wider.path != first.path);
    AUREA_CHECK(wider.windowStartUs <= first.windowStartUs && wider.windowEndUs >= 20'000'000);

    // Outro original: nunca reaproveita.
    rq.originRef = "content://media/external/video/media/43#999";
    rq.inUs = 11'000'000; rq.outUs = 13'000'000;
    TrimmedImportResult other;
    AUREA_CHECK(import_trimmed_media(rq, other).ok());
    AUREA_CHECK(!other.reused);
}
#endif
