// =============================================================================
//  Aurea / media / MediaCut.cpp — ver MediaCut.hpp.
// =============================================================================
#include "aurea/media/MediaCut.hpp"

#include "aurea/core/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
}
#endif

namespace aurea {
namespace fs = std::filesystem;

i64 cut_margin_us(i64 inUs, i64 outUs) noexcept {
    const i64 span = std::max<i64>(0, outUs - inUs);
    return std::max<i64>(2'000'000, span / 20);
}

namespace {

/// Caminho em UTF-8 (C++20: u8string() é std::u8string).
std::string utf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string extension_of(const std::string& path) {
    const fs::path p = fs::u8path(path);
    std::string ext = utf8(p.extension());
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    return lower(ext);
}

u64 file_bytes(const std::string& path) noexcept {
    std::error_code ec;
    const auto n = fs::file_size(fs::u8path(path), ec);
    return ec ? 0 : static_cast<u64>(n);
}

bool cancelled(const std::atomic<bool>* c) noexcept { return c && c->load(std::memory_order_acquire); }

} // namespace

#if defined(AUREA_HAS_FFMPEG) && AUREA_HAS_FFMPEG
namespace {

constexpr AVRational kMicros{1, 1'000'000};

std::string av_text(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

struct InputCloser { void operator()(AVFormatContext* c) const noexcept { if (c) avformat_close_input(&c); } };
struct PacketFree { void operator()(AVPacket* p) const noexcept { av_packet_free(&p); } };

/// Saída com o arquivo temporário: some se não chegar ao fim.
struct Output {
    AVFormatContext* oc = nullptr;
    std::string partial;
    bool headerWritten = false;
    bool finished = false;
    ~Output() {
        if (oc) {
            if (headerWritten && !finished) (void)av_write_trailer(oc);
            if (oc->pb) avio_closep(&oc->pb);
            avformat_free_context(oc);
        }
        if (!finished && !partial.empty()) {
            std::error_code ec;
            fs::remove(fs::u8path(partial), ec);
        }
    }
};

i64 packet_time(const AVPacket& p) noexcept { return p.pts != AV_NOPTS_VALUE ? p.pts : p.dts; }

} // namespace

Status cut_media(const MediaCutRequest& request, MediaCutResult& result) noexcept {
    result = MediaCutResult{};
    if (request.source.empty() || request.destination.empty())
        return Status{Errc::InvalidArgument, "corte: caminho vazio"};
    if (request.outUs <= request.inUs || request.inUs < 0)
        return Status{Errc::InvalidArgument, "corte: intervalo invalido"};

    AVFormatContext* rawIn = nullptr;
    if (const int e = avformat_open_input(&rawIn, request.source.c_str(), nullptr, nullptr); e < 0) {
        AUREA_LOG_WARN("corte: fonte nao abre (%s)", av_text(e).c_str());
        return Status{Errc::IoError, "corte: fonte nao abre"};
    }
    std::unique_ptr<AVFormatContext, InputCloser> in(rawIn);
    // Só MP4/MOV (o demuxer "mov,mp4,m4a,3gp,3g2,mj2"): o remux para MP4/MOV
    // é o que todos os decoders do app leem igual.
    if (!in->iformat || !std::strstr(in->iformat->name, "mov"))
        return Status{Errc::NotSupported, "corte: container sem suporte"};
    if (const int e = avformat_find_stream_info(in.get(), nullptr); e < 0)
        return Status{Errc::DecodeFailed, "corte: sem informacao das trilhas"};
    if (!in->pb || !(in->pb->seekable & AVIO_SEEKABLE_NORMAL))
        return Status{Errc::NotSupported, "corte: fonte sem seek"};

    const int vIndex = av_find_best_stream(in.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIndex < 0) return Status{Errc::UnsupportedFormat, "corte: sem video"};
    const int aIndex = av_find_best_stream(in.get(), AVMEDIA_TYPE_AUDIO, -1, vIndex, nullptr, 0);
    AVStream* vs = in->streams[vIndex];
    AVStream* as = aIndex >= 0 ? in->streams[aIndex] : nullptr;

    i64 durationUs = 0;
    if (vs->duration != AV_NOPTS_VALUE && vs->duration > 0) durationUs = av_rescale_q(vs->duration, vs->time_base, kMicros);
    if (durationUs <= 0 && in->duration != AV_NOPTS_VALUE && in->duration > 0)
        durationUs = av_rescale_q(in->duration, AVRational{1, AV_TIME_BASE}, kMicros);
    if (durationUs <= 0) return Status{Errc::NotSupported, "corte: duracao desconhecida"};
    result.sourceDurationUs = durationUs;
    result.sourceBytes = file_bytes(request.source);

    const i64 margin = request.marginUs >= 0 ? request.marginUs : cut_margin_us(request.inUs, request.outUs);
    const i64 winIn = std::max<i64>(0, request.inUs - margin);
    const i64 winOut = std::min<i64>(durationUs, request.outUs + margin);
    if (winOut <= winIn) return Status{Errc::InvalidArgument, "corte: trecho fora da midia"};
    if (static_cast<f64>(winOut - winIn) >= request.maxFraction * static_cast<f64>(durationUs))
        return Status{Errc::NotSupported, "corte: trecho grande demais para valer a pena"};

    // --- saída -----------------------------------------------------------------
    std::string ext = extension_of(request.destination);
    const char* muxer = ext == "mov" ? "mov" : "mp4";
    Output out;
    {
        const fs::path dst = fs::u8path(request.destination);
        out.partial = utf8(dst.parent_path() / fs::u8path(".cut-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".part"));
    }
    if (const int e = avformat_alloc_output_context2(&out.oc, nullptr, muxer, out.partial.c_str()); e < 0 || !out.oc)
        return Status{Errc::NotSupported, "corte: muxer indisponivel"};
    // Dolby Vision (dvcC/dvvC) só sai com a conformidade "não oficial".
    out.oc->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    if (const AVDictionaryEntry* t = av_dict_get(in->metadata, "creation_time", nullptr, 0))
        av_dict_set(&out.oc->metadata, "creation_time", t->value, 0);

    const auto add_stream = [&](AVStream* src) -> AVStream* {
        AVStream* dst = avformat_new_stream(out.oc, nullptr);
        if (!dst) return nullptr;
        // Copia também os dados de lado do codec: matriz de exibição (rotação),
        // mastering display / content light (HDR10), configuração Dolby Vision.
        if (avcodec_parameters_copy(dst->codecpar, src->codecpar) < 0) return nullptr;
        dst->codecpar->codec_tag = 0;
        // HEVC: 'hvc1' (parâmetros no cabeçalho) é o que o AVFoundation toca.
        if (dst->codecpar->codec_id == AV_CODEC_ID_HEVC) dst->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        dst->time_base = src->time_base;
        dst->disposition = src->disposition;
        av_dict_copy(&dst->metadata, src->metadata, 0);
        return dst;
    };
    AVStream* ovs = add_stream(vs);
    AVStream* oas = as ? add_stream(as) : nullptr;
    if (!ovs || (as && !oas)) return Status{Errc::NotSupported, "corte: trilha nao copiavel"};
    if (const int e = avio_open(&out.oc->pb, out.partial.c_str(), AVIO_FLAG_WRITE); e < 0) {
        AUREA_LOG_WARN("corte: destino nao abre (%s)", av_text(e).c_str());
        return Status{Errc::IoError, "corte: destino nao abre"};
    }
    if (const int e = avformat_write_header(out.oc, nullptr); e < 0) {
        AUREA_LOG_WARN("corte: cabecalho recusado (%s)", av_text(e).c_str());
        return Status{Errc::NotSupported, "corte: muxer recusou as trilhas"};
    }
    out.headerWritten = true;

    // --- leitura ---------------------------------------------------------------
    const i64 seekTs = av_rescale_q(winIn, kMicros, vs->time_base);
    if (av_seek_frame(in.get(), vIndex, seekTs, AVSEEK_FLAG_BACKWARD) < 0)
        (void)av_seek_frame(in.get(), vIndex, 0, AVSEEK_FLAG_BACKWARD);
    const i64 endTs = av_rescale_q(winOut, kMicros, vs->time_base);

    std::unique_ptr<AVPacket, PacketFree> pkt(av_packet_alloc());
    if (!pkt) return Status{Errc::OutOfMemory, "corte: sem memoria"};
    bool started = false, videoDone = false, audioDone = !as;
    i64 t0 = 0;            // pts do keyframe inicial (régua do vídeo)
    i64 t0Us = 0;
    i64 endUs = 0;         // até onde o vídeo copiado cobre (régua do original)
    i64 lastVideoEndUs = 0;
    u64 afterVideo = 0;    // pacotes lidos depois do fim do vídeo (só o áudio que falta)
    u64 videoPackets = 0;
    std::deque<AVPacket*> pendingAudio;   // áudio lido antes do keyframe inicial
    const auto drop_pending = [&] { for (AVPacket* p : pendingAudio) av_packet_free(&p); pendingAudio.clear(); };

    const auto write = [&](AVPacket* p, AVStream* src, AVStream* dst) -> int {
        const i64 shift = av_rescale_q(t0, vs->time_base, src->time_base);
        if (p->pts != AV_NOPTS_VALUE) p->pts -= shift;
        if (p->dts != AV_NOPTS_VALUE) p->dts -= shift;
        av_packet_rescale_ts(p, src->time_base, dst->time_base);
        p->stream_index = dst->index;
        p->pos = -1;
        return av_interleaved_write_frame(out.oc, p);
    };
    const auto audio_write = [&](AVPacket* p) -> int {
        const i64 t = packet_time(*p);
        if (t == AV_NOPTS_VALUE) return 0;
        const i64 us = av_rescale_q(t, as->time_base, kMicros);
        if (us < t0Us) return 0;   // antes do 1º quadro: fora do trecho
        if (videoDone && us >= endUs) { audioDone = true; return 0; }
        return write(p, as, oas);
    };

    int status = 0;
    while (!(videoDone && audioDone)) {
        if (cancelled(request.cancel)) { drop_pending(); return Status{Errc::Cancelled, "corte cancelado"}; }
        const int r = av_read_frame(in.get(), pkt.get());
        if (r == AVERROR_EOF) break;
        if (r < 0) { drop_pending(); return Status{Errc::IoError, "corte: leitura falhou"}; }
        if (pkt->stream_index == vIndex && !videoDone) {
            const i64 t = packet_time(*pkt);
            if (t == AV_NOPTS_VALUE) { av_packet_unref(pkt.get()); continue; }
            const bool key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            if (!started) {
                if (!key) { av_packet_unref(pkt.get()); continue; }
                started = true;
                t0 = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
                t0Us = av_rescale_q(t0, vs->time_base, kMicros);
                // O áudio que chegou antes do keyframe e cai dentro do trecho.
                for (AVPacket* p : pendingAudio) {
                    if (status >= 0) status = audio_write(p);
                    av_packet_free(&p);
                }
                pendingAudio.clear();
                if (status < 0) break;
            } else if (key && pkt->pts != AV_NOPTS_VALUE && pkt->pts >= endTs) {
                // 1º keyframe depois de saída + folga: o trecho acaba aqui.
                videoDone = true;
                endUs = av_rescale_q(pkt->pts, vs->time_base, kMicros);
                av_packet_unref(pkt.get());
                continue;
            }
            // Quadros "anteriores" de um GOP aberto (pts antes do keyframe
            // inicial) dependem do GOP de antes: não entram.
            if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < t0) { av_packet_unref(pkt.get()); continue; }
            if (pkt->pts != AV_NOPTS_VALUE) {
                const i64 dur = pkt->duration > 0 ? pkt->duration : 0;
                lastVideoEndUs = std::max(lastVideoEndUs, av_rescale_q(pkt->pts + dur, vs->time_base, kMicros));
            }
            ++videoPackets;
            status = write(pkt.get(), vs, ovs);
            av_packet_unref(pkt.get());
            if (status < 0) break;
        } else if (as && pkt->stream_index == aIndex && !audioDone) {
            if (!started) {
                // Fica para quando o keyframe inicial disser onde o trecho começa.
                if (pendingAudio.size() < 4096) {
                    AVPacket* keep = av_packet_clone(pkt.get());
                    if (keep) pendingAudio.push_back(keep);
                }
                av_packet_unref(pkt.get());
                continue;
            }
            status = audio_write(pkt.get());
            av_packet_unref(pkt.get());
            if (status < 0) break;
        } else {
            av_packet_unref(pkt.get());
        }
        // Arquivo sem áudio intercalado no fim: não lê o resto do original.
        if (videoDone && ++afterVideo > 20000) break;
    }
    drop_pending();
    if (status < 0) {
        AUREA_LOG_WARN("corte: pacote recusado pelo muxer (%s)", av_text(status).c_str());
        return Status{Errc::EncodeFailed, "corte: pacote recusado"};
    }
    if (!started || videoPackets == 0) return Status{Errc::DecodeFailed, "corte: nenhum keyframe no trecho"};
    if (!videoDone) endUs = std::max(lastVideoEndUs, std::min(durationUs, winOut));
    if (const int e = av_write_trailer(out.oc); e < 0) {
        out.headerWritten = false;
        return Status{Errc::IoError, "corte: final do arquivo nao gravado"};
    }
    out.headerWritten = false;
    avio_closep(&out.oc->pb);

    std::error_code ec;
    fs::rename(fs::u8path(out.partial), fs::u8path(request.destination), ec);
    if (ec) return Status{Errc::IoError, "corte: destino nao gravado"};
    out.finished = true;
    result.windowStartUs = t0Us;
    result.windowEndUs = endUs;
    result.bytes = file_bytes(request.destination);
    AUREA_LOG_INFO("corte: %.1f s de %.1f s (%llu de %llu MB), janela em %.3f s",
                   static_cast<f64>(endUs - t0Us) / 1e6, static_cast<f64>(durationUs) / 1e6,
                   static_cast<unsigned long long>(result.bytes >> 20),
                   static_cast<unsigned long long>(result.sourceBytes >> 20), static_cast<f64>(t0Us) / 1e6);
    return OkStatus;
}

#else

Status cut_media(const MediaCutRequest&, MediaCutResult& result) noexcept {
    result = MediaCutResult{};
    return Status{Errc::NotSupported, "corte: motor sem FFmpeg"};
}

#endif

// =============================================================================
// Importação com reaproveitamento
// =============================================================================
namespace {

struct CutEntry {
    std::string origin;
    i64 startUs = 0;
    i64 endUs = 0;
    std::string file;   ///< nome dentro da pasta
};

constexpr const char* kIndexName = ".aurea-cuts";

/// Um corte por vez por processo: dois iguais seguidos reaproveitam em vez de
/// cortar duas vezes, e o índice não é reescrito por dois ao mesmo tempo.
std::mutex& cut_mutex() {
    static std::mutex m;
    return m;
}

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (usize i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            out += n == 't' ? '\t' : n == 'n' ? '\n' : n;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::vector<CutEntry> read_index(const fs::path& dir) {
    std::vector<CutEntry> entries;
    std::ifstream f(dir / kIndexName, std::ios::binary);
    if (!f) return entries;
    std::string line;
    while (std::getline(f, line)) {
        // origem \t início \t fim \t arquivo
        std::vector<std::string> parts;
        usize from = 0;
        for (usize i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == '\t') { parts.push_back(line.substr(from, i - from)); from = i + 1; }
        }
        if (parts.size() != 4) continue;
        CutEntry e;
        e.origin = unescape(parts[0]);
        e.startUs = std::strtoll(parts[1].c_str(), nullptr, 10);
        e.endUs = std::strtoll(parts[2].c_str(), nullptr, 10);
        e.file = unescape(parts[3]);
        // Só nomes simples (o índice nunca aponta para fora da pasta).
        if (e.file.empty() || e.file.find('/') != std::string::npos || e.file.find('\\') != std::string::npos
            || e.file == "." || e.file == ".." || e.endUs <= e.startUs || e.startUs < 0) continue;
        std::error_code ec;
        if (!fs::is_regular_file(dir / fs::u8path(e.file), ec)) continue;   // apagado (limpeza do armazenamento)
        entries.push_back(std::move(e));
    }
    return entries;
}

bool write_index(const fs::path& dir, const std::vector<CutEntry>& entries) {
    const fs::path tmp = dir / ".aurea-cuts.tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        for (const CutEntry& e : entries)
            f << escape(e.origin) << '\t' << e.startUs << '\t' << e.endUs << '\t' << escape(e.file) << '\n';
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, dir / kIndexName, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

u64 fnv(const std::string& s) noexcept {
    u64 h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

/// Parciais de um corte interrompido (app morto no meio) com mais de 1 h.
void sweep_partials(const fs::path& dir) {
    std::error_code ec;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = utf8(it->path().filename());
        if (name.rfind(".cut-", 0) != 0) continue;
        std::error_code e2;
        const auto when = fs::last_write_time(it->path(), e2);
        if (!e2 && now - when > std::chrono::hours(1)) fs::remove(it->path(), e2);
    }
}

} // namespace

Status import_trimmed_media(const TrimmedImportRequest& request, TrimmedImportResult& result) noexcept {
    result = TrimmedImportResult{};
    if (request.source.empty() || request.directory.empty() || request.originRef.empty())
        return Status{Errc::InvalidArgument, "corte: pedido incompleto"};
    if (request.inUs < 0 || request.outUs <= request.inUs) return Status{Errc::InvalidArgument, "corte: intervalo invalido"};
    std::lock_guard<std::mutex> lock(cut_mutex());
    const fs::path dir = fs::u8path(request.directory);
    std::error_code ec;
    fs::create_directories(dir, ec);
    sweep_partials(dir);
    std::vector<CutEntry> entries = read_index(dir);

    // 1. Um trecho do mesmo original que já cobre o corte.
    const CutEntry* overlap = nullptr;
    for (const CutEntry& e : entries) {
        if (e.origin != request.originRef) continue;
        if (e.startUs <= request.inUs && e.endUs >= request.outUs) {
            result.path = utf8(dir / fs::u8path(e.file));
            result.windowStartUs = e.startUs;
            result.windowEndUs = e.endUs;
            result.bytes = file_bytes(result.path);
            result.reused = true;
            AUREA_LOG_INFO("corte: trecho reaproveitado (%.1f s a %.1f s)", static_cast<f64>(e.startUs) / 1e6,
                           static_cast<f64>(e.endUs) / 1e6);
            return OkStatus;
        }
        const i64 margin = cut_margin_us(request.inUs, request.outUs);
        if (e.startUs < request.outUs + margin && e.endUs > request.inUs - margin) overlap = &e;
    }

    // 2. Corta (a união com o trecho que sobrepõe, se houver).
    MediaCutRequest cut;
    cut.source = request.source;
    cut.cancel = request.cancel;
    cut.inUs = request.inUs;
    cut.outUs = request.outUs;
    if (overlap) {
        const i64 margin = cut_margin_us(request.inUs, request.outUs);
        cut.inUs = std::max<i64>(0, std::min(overlap->startUs, request.inUs - margin));
        cut.outUs = std::max(overlap->endUs, request.outUs + margin);
        cut.marginUs = 0;
    }
    std::string ext = lower(request.extension);
    if (ext != "mov") ext = "mp4";
    char name[96];
    std::snprintf(name, sizeof(name), "cut-%016" PRIx64 "-%" PRId64 "-%" PRId64 ".%s", fnv(request.originRef),
                  static_cast<i64>(cut.inUs / 1000), static_cast<i64>(cut.outUs / 1000), ext.c_str());
    cut.destination = utf8(dir / fs::u8path(name));
    MediaCutResult r;
    if (const Status s = cut_media(cut, r); !s.ok()) return s;

    entries.push_back(CutEntry{request.originRef, r.windowStartUs, r.windowEndUs, name});
    if (!write_index(dir, entries)) AUREA_LOG_WARN("corte: indice de trechos nao gravado");
    result.path = cut.destination;
    result.windowStartUs = r.windowStartUs;
    result.windowEndUs = r.windowEndUs;
    result.bytes = r.bytes;
    result.sourceBytes = r.sourceBytes;
    return OkStatus;
}

} // namespace aurea
