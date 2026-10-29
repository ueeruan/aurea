#include "aurea/text/LocalWhisper.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/media/MediaManager.hpp"
#include "whisper.h"
#include "aurea/text/WhisperQuality.hpp"
#include "aurea/text/WhisperWords.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <limits>
#include <thread>

namespace aurea::text {
namespace {

constexpr i64 kRate = WHISPER_SAMPLE_RATE;
constexpr f64 kLookaheadSec = 60.0;   // áudio lido à frente do cursor (decide onde cortar)
constexpr f64 kReadSec = 30.0;        // leitura por vez: limita o estéreo nativo na RAM

/// Anexa a `out` as amostras [first, first + count) da régua de 16 kHz, mono.
Status read_span(audio::AudioDecoderBackend& decoder, const audio::AudioStreamInfo& info, i64 first16, i64 count16,
                 std::atomic<bool>& cancelled, std::vector<f32>& out, bool& ended) {
    const f64 from = static_cast<f64>(first16) / kRate, to = static_cast<f64>(first16 + count16) / kRate;
    const f64 step = static_cast<f64>(info.sampleRate) / kRate;
    const i64 margin = audio::resample_half_width(step) + 2;   // o filtro vê amostras reais nas bordas
    const i64 first = static_cast<i64>(std::llround(from * info.sampleRate)) - margin;
    const i64 count = static_cast<i64>(std::ceil((to - from) * info.sampleRate)) + 2 * margin;
    std::vector<f32> stereo(static_cast<usize>(count) * 2, 0);
    auto seek = decoder.seek(static_cast<i64>(std::max(0.0, static_cast<f64>(first) / info.sampleRate) * 1000000));
    if (!seek) return seek;
    bool eos = false; int emptyReads = 0;
    while (!eos) {
        if (cancelled.load()) return Status{Errc::Cancelled, "transcricao cancelada"};
        std::vector<f32> block; i64 pts = 0;
        auto read = decoder.read(block, pts, eos);
        if (!read) return read;
        if (block.empty()) { if (++emptyReads > 1000) return Status{Errc::DecodeFailed, "decoder sem progresso"}; continue; }
        emptyReads = 0;
        const double timestamp = static_cast<double>(pts) * info.sampleRate / 1000000.0;
        if (std::abs(timestamp) >= static_cast<double>(std::numeric_limits<i64>::max() / 4))
            return Status{Errc::DecodeFailed, "tempo de audio invalido"};
        const i64 offset = static_cast<i64>(std::llround(timestamp)) - first;
        if (offset >= count) break;
        for (usize i = 0; i < block.size() / info.channels; ++i) {
            const i64 dst = offset + static_cast<i64>(i);
            if (dst < 0 || dst >= count) continue;
            audio::to_stereo(block.data() + i * info.channels, info.channels,
                             stereo[static_cast<usize>(dst) * 2], stereo[static_cast<usize>(dst) * 2 + 1]);
        }
    }
    ended = ended || eos;
    std::vector<f32> resampled(static_cast<usize>(count16) * 2);
    audio::resample_to_mix(stereo.data(), count, static_cast<f64>(margin), step, resampled.data(), static_cast<u32>(count16));
    stereo.clear(); stereo.shrink_to_fit();
    const usize base = out.size();
    out.resize(base + static_cast<usize>(count16));
    for (i64 i = 0; i < count16; ++i)
        out[base + static_cast<usize>(i)] = (resampled[static_cast<usize>(i) * 2] + resampled[static_cast<usize>(i) * 2 + 1]) * 0.5f;
    return {};
}

/// Cabeças de alinhamento do DTW (tempo por palavra) para o modelo carregado.
whisper_alignment_heads_preset dtw_preset(const WhisperModelInfo& m) noexcept {
    switch (m.size) {
        case WhisperModelSize::Tiny: return m.english ? WHISPER_AHEADS_TINY_EN : WHISPER_AHEADS_TINY;
        case WhisperModelSize::Base: return m.english ? WHISPER_AHEADS_BASE_EN : WHISPER_AHEADS_BASE;
        case WhisperModelSize::Small: return m.english ? WHISPER_AHEADS_SMALL_EN : WHISPER_AHEADS_SMALL;
        case WhisperModelSize::Medium: return m.english ? WHISPER_AHEADS_MEDIUM_EN : WHISPER_AHEADS_MEDIUM;
        case WhisperModelSize::Large: return WHISPER_AHEADS_LARGE_V2;
        case WhisperModelSize::LargeV3: return WHISPER_AHEADS_LARGE_V3;
        case WhisperModelSize::Turbo: return WHISPER_AHEADS_LARGE_V3_TURBO;
        default: return WHISPER_AHEADS_NONE;
    }
}

struct Token { std::string text; f64 t0, t1, dtw; };

} // namespace

namespace {
/// WAV PCM 16 bits mono 16 kHz (o cabeçalho de 44 bytes).
bool write_wav16(const std::string& path, const f32* samples, usize count) noexcept {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32le = [&](u32 v) { const u8 b[4] = {u8(v), u8(v >> 8), u8(v >> 16), u8(v >> 24)}; std::fwrite(b, 1, 4, f); };
    auto u16le = [&](u16 v) { const u8 b[2] = {u8(v), u8(v >> 8)}; std::fwrite(b, 1, 2, f); };
    const u32 bytes = static_cast<u32>(count * 2);
    std::fwrite("RIFF", 1, 4, f); u32le(36 + bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32le(16); u16le(1); u16le(1); u32le(static_cast<u32>(kRate)); u32le(static_cast<u32>(kRate) * 2); u16le(2); u16le(16);
    std::fwrite("data", 1, 4, f); u32le(bytes);
    std::vector<i16> pcm(count);
    for (usize i = 0; i < count; ++i) pcm[i] = static_cast<i16>(std::lround(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f));
    const bool ok = std::fwrite(pcm.data(), 2, count, f) == count;
    return std::fclose(f) == 0 && ok;
}
} // namespace

Result<std::vector<SpeechChunk>> export_speech_chunks(VideoSourceFactory& factory, const std::string& source, const std::string& dir,
    std::atomic<bool>& cancelled, const std::function<void(int)>& progress) noexcept {
    try {
        auto decoder = factory.open_audio(source.c_str());
        if (!decoder) return Status{Errc::DecodeFailed, "midia sem audio compativel"};
        const auto info = decoder->info();
        if (info.channels == 0 || info.channels > 32 || info.sampleRate < 8000 || info.sampleRate > 192000)
            return Status{Errc::UnsupportedFormat, "formato de audio invalido"};
        const f64 duration = info.durationUs / 1000000.0;
        if (!(duration > 0.05)) return Status{Errc::InvalidArgument, "midia sem audio"};
        const i64 last = static_cast<i64>(std::llround(duration * kRate));
        constexpr i64 kMin = 25 * kRate, kMax = 45 * kRate, kFrame = kRate / 50;   // quadros de 20 ms
        std::vector<SpeechChunk> out;
        i64 cursor = 0;
        u32 index = 0;
        while (cursor < last - kRate / 20) {
            if (cancelled.load()) return Status{Errc::Cancelled, "transcricao cancelada"};
            const i64 want = std::min(last - cursor, kMax);
            std::vector<f32> buf;
            bool ended = false;
            for (i64 at = cursor; at < cursor + want && !ended;) {
                const i64 n = std::min<i64>(cursor + want - at, static_cast<i64>(kReadSec * kRate));
                auto read = read_span(*decoder, info, at, n, cancelled, buf, ended);
                if (!read) return read;
                at += n;
            }
            if (buf.empty()) break;
            // Onde cortar: o fim, se cabe; senão o quadro mais quieto entre 25 e 45 s.
            i64 cut = static_cast<i64>(buf.size());
            if (cursor + cut < last && cut > kMin) {
                f64 best = 1e30;
                for (i64 f = kMin; f + kFrame <= cut; f += kFrame) {
                    f64 e = 0;
                    for (i64 i = 0; i < kFrame; ++i) e += static_cast<f64>(buf[static_cast<usize>(f + i)]) * buf[static_cast<usize>(f + i)];
                    if (e < best) { best = e; cut = f + kFrame / 2; }
                }
            }
            // Sem fala (pico de energia por quadro muito baixo): não manda.
            f64 peak = 0;
            for (i64 f = 0; f + kFrame <= cut; f += kFrame) {
                f64 e = 0;
                for (i64 i = 0; i < kFrame; ++i) e += static_cast<f64>(buf[static_cast<usize>(f + i)]) * buf[static_cast<usize>(f + i)];
                peak = std::max(peak, e / kFrame);
            }
            if (peak > 1e-5) {
                char name[32];
                std::snprintf(name, sizeof(name), "/chunk_%03u.wav", index++);
                SpeechChunk c{dir + name, static_cast<f64>(cursor) / kRate, static_cast<f64>(cursor + cut) / kRate};
                if (!write_wav16(c.path, buf.data(), static_cast<usize>(cut))) return Status{Errc::IoError, "nao foi possivel gravar o audio"};
                out.push_back(std::move(c));
            }
            cursor += std::max<i64>(cut, kRate / 10);
            progress(std::clamp(static_cast<int>(100.0 * static_cast<f64>(cursor) / static_cast<f64>(last)), 0, 99));
        }
        return out;
    } catch (const std::bad_alloc&) {
        return Status{Errc::OutOfMemory, "memoria insuficiente"};
    }
}

Result<std::vector<CaptionWord>> transcribe_local(VideoSourceFactory& factory,
    const std::string& source, const std::string& model, const std::string& language,
    std::atomic<bool>& cancelled, const std::function<void(int)>& progress, double start, double end, bool translateEnglish) noexcept {
    // One model across the process. Do not accumulate multiple 200+ MB contexts.
    static std::mutex inference;
    std::unique_lock lock(inference, std::try_to_lock);
    if (!lock.owns_lock()) return Status{Errc::InvalidState, "transcricao em andamento"};
    try {
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || (end > 0 && end <= start))
            return Status{Errc::InvalidArgument, "intervalo invalido"};
        if (!language.empty() && whisper_lang_id(language.c_str()) < 0)
            return Status{Errc::InvalidArgument, "idioma invalido"};
        auto decoder = factory.open_audio(source.c_str());
        if (!decoder) return Status{Errc::DecodeFailed, "midia sem audio compativel"};
        const auto info = decoder->info();
        if (info.channels == 0 || info.channels > 32 || info.sampleRate < 8000 || info.sampleRate > 192000)
            return Status{Errc::UnsupportedFormat, "formato de audio invalido"};
        // DTW: o tempo de cada palavra sai do alinhamento da atenção (o que o
        // modelo "olhava" ao escrever o token), não do palpite dos tokens de
        // tempo. Exige saber o tamanho do modelo e não roda com flash attention.
        // Em 32 bits o buffer de 128 MB do alinhamento não cabe com folga.
        auto cp = whisper_context_default_params(); cp.use_gpu = false;
        whisper_context* loaded = nullptr;
        const auto preset = sizeof(void*) >= 8 ? dtw_preset(read_whisper_model_info(model)) : WHISPER_AHEADS_NONE;
        if (preset != WHISPER_AHEADS_NONE) {
            auto aligned = cp;
            aligned.flash_attn = false;
            aligned.dtw_token_timestamps = true;
            aligned.dtw_aheads_preset = preset;
            loaded = whisper_init_from_file_with_params(model.c_str(), aligned);
        }
        const bool dtw = loaded != nullptr;
        if (!loaded) loaded = whisper_init_from_file_with_params(model.c_str(), cp);
        std::unique_ptr<whisper_context, decltype(&whisper_free)> ctx(loaded, whisper_free);
        if (!ctx) return Status{Errc::CorruptData, "modelo Whisper indisponivel"};
        if (translateEnglish && !whisper_is_multilingual(ctx.get()))
            return Status{Errc::UnsupportedFormat, "traducao requer modelo Whisper multilingue"};
        const double duration = info.durationUs / 1000000.0;
        const double stop = end > 0 ? std::min(end, duration) : duration;
        if (stop * info.sampleRate >= static_cast<double>(std::numeric_limits<i64>::max() / 4))
            return Status{Errc::InvalidArgument, "duracao de audio invalida"};
        if (stop <= start) return Status{Errc::InvalidArgument, "intervalo sem audio"};

        const i64 first = static_cast<i64>(std::llround(start * kRate)), last = static_cast<i64>(std::llround(stop * kRate));
        const int threads = whisper_thread_count(std::thread::hardware_concurrency());
        const whisper_token eot = whisper_token_eot(ctx.get());
        const bool autoLanguage = language.empty() || language == "auto";
        std::string locked;     // idioma detectado na primeira janela com fala: as outras não oscilam
        std::string prompt;     // fim da janela anterior: o contexto que no_context=true não leva
        std::vector<CaptionWord> result;
        std::vector<f32> buffer;
        i64 bufferStart = first;
        bool ended = false;
        f64 cursor = start, own = start;
        auto report = [&](f64 at) {
            progress(std::clamp(static_cast<int>(100.0 * (at - start) / (stop - start)), 0, 99));
        };
        while (cursor < stop - 0.05) {
            if (cancelled.load()) return Status{Errc::Cancelled, "transcricao cancelada"};
            // Janela deslizante a 16 kHz: o que ficou para trás sai, 60 s à frente entram.
            const i64 head = std::clamp<i64>(static_cast<i64>(std::llround(cursor * kRate)), first, last);
            if (head >= bufferStart + static_cast<i64>(buffer.size())) { buffer.clear(); bufferStart = head; }
            else if (head > bufferStart) {
                buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(head - bufferStart));
                bufferStart = head;
            }
            const i64 want = std::min(last, head + static_cast<i64>(kLookaheadSec * kRate));
            while (!ended && bufferStart + static_cast<i64>(buffer.size()) < want) {
                const i64 from = bufferStart + static_cast<i64>(buffer.size());
                const i64 count = std::min(want - from, static_cast<i64>(kReadSec * kRate));
                auto read = read_span(*decoder, info, from, count, cancelled, buffer, ended);
                if (!read) return read;
            }
            const i64 bufferEnd = bufferStart + static_cast<i64>(buffer.size());
            const bool final = ended || bufferEnd >= last;
            const f64 limit = std::min(stop, static_cast<f64>(bufferEnd) / kRate);
            const SpeechActivity activity = analyze_speech(buffer.data(), buffer.size(), static_cast<u32>(kRate),
                                                           static_cast<f64>(bufferStart) / kRate);
            const WhisperWindow window = plan_whisper_window(activity, cursor, own, limit, final);
            if (window.spans.empty()) {
                if (final && window.next >= limit - 1e-3) break;
                // Silêncio pulado (ou à espera de mais áudio): nenhuma chamada ao modelo.
                cursor = window.next > cursor + 1e-3 ? window.next : (final ? stop : limit);
                own = window.nextOwn;
                report(cursor);
                continue;
            }
            // Os trechos de fala colados, com meio segundo de silêncio entre eles.
            std::vector<f32> pcm;
            WhisperTimeMap map;
            for (const auto& [s, e] : window.spans) {
                if (!pcm.empty()) pcm.insert(pcm.end(), static_cast<usize>(kWhisperGapSec * kRate), 0.0f);
                const i64 a = std::clamp<i64>(static_cast<i64>(std::llround(s * kRate)) - bufferStart, 0, static_cast<i64>(buffer.size()));
                const i64 b = std::clamp<i64>(static_cast<i64>(std::llround(e * kRate)) - bufferStart, a, static_cast<i64>(buffer.size()));
                const f64 packed = static_cast<f64>(pcm.size()) / kRate;
                map.pieces.push_back({packed, packed + static_cast<f64>(b - a) / kRate, static_cast<f64>(bufferStart + a) / kRate});
                pcm.insert(pcm.end(), buffer.begin() + static_cast<std::ptrdiff_t>(a), buffer.begin() + static_cast<std::ptrdiff_t>(b));
            }
            auto p = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
            p.beam_search.beam_size = 3;
            p.greedy.best_of = 3;   // o fallback de temperatura sorteia 3 candidatos, não 1
            p.n_threads = threads; p.translate = translateEnglish; p.no_context = true;
            const std::string& speech = locked.empty() ? language : locked;
            p.language = speech.empty() ? "auto" : speech.c_str();
            p.initial_prompt = prompt.empty() ? nullptr : prompt.c_str();
            p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
            p.token_timestamps = true; p.max_len = 0; p.split_on_word = false;
            p.suppress_blank = true;
            p.suppress_nst = true;   // "♪", "[", "(" e afins não são fala
            p.temperature = 0.0f; p.temperature_inc = 0.2f;
            p.entropy_thold = 2.4f; p.logprob_thold = -1.0f; p.no_speech_thold = 0.6f;
            p.abort_callback = [](void* state) { return static_cast<std::atomic<bool>*>(state)->load(); };
            p.abort_callback_user_data = &cancelled;
            struct Progress { const std::function<void(int)>* callback; double from, to, start, stop; }
                state{&progress, cursor, window.next, start, stop};
            p.progress_callback = [](whisper_context*, whisper_state*, int percent, void* opaque) {
                auto& s = *static_cast<Progress*>(opaque);
                const double at = s.from + (s.to - s.from) * percent / 100.0;
                (*s.callback)(std::clamp(static_cast<int>(100 * (at - s.start) / (s.stop - s.start)), 0, 99));
            }; p.progress_callback_user_data = &state;
            if (whisper_full(ctx.get(), p, pcm.data(), static_cast<int>(pcm.size())) != 0)
                return Status{cancelled.load() ? Errc::Cancelled : Errc::DecodeFailed, "Whisper interrompido"};

            // Segmentos: fora o que é alucinação (silêncio "falado", crédito de
            // legenda, despedida sem voz embaixo, confiança baixa demais).
            bool suspicious = false;
            std::vector<Token> tokens;
            for (int s = 0; s < whisper_full_n_segments(ctx.get()); ++s) {
                std::vector<Token> segment;
                std::string said;
                double logprob = 0;
                const f64 s0 = map.source(whisper_full_get_segment_t0(ctx.get(), s) * .01);
                const f64 s1 = map.source(whisper_full_get_segment_t1(ctx.get(), s) * .01);
                for (int t = 0; t < whisper_full_n_tokens(ctx.get(), s); ++t) {
                    const auto token = whisper_full_get_token_data(ctx.get(), s, t);
                    if (token.id >= eot) continue;
                    const char* piece = whisper_full_get_token_text(ctx.get(), s, t);
                    const std::string value = piece ? piece : "";
                    said += value;
                    logprob += token.plog;
                    const f64 t0 = token.t0 >= 0 ? map.source(token.t0 * .01) : -1.0;
                    const f64 t1 = token.t1 >= 0 ? map.source(token.t1 * .01) : -1.0;
                    const f64 at = dtw && token.t_dtw >= 0 ? map.source(token.t_dtw * .01) : -1.0;
                    segment.push_back({value, t0, t1, at});
                }
                if (segment.empty()) continue;
                const double average = logprob / static_cast<double>(segment.size());
                const double noSpeech = whisper_full_get_segment_no_speech_prob(ctx.get(), s);
                const double voiced = activity.voiced_ratio(s0, std::max(s0 + 0.01, s1));
                const WhisperPhrase phrase = classify_whisper_phrase(said);
                const bool weak = voiced < 0.5 || noSpeech > 0.3 || average < -0.7;
                if (phrase == WhisperPhrase::NonSpeech || phrase == WhisperPhrase::Credit || (phrase == WhisperPhrase::Closing && weak) ||
                    (noSpeech > 0.6 && average < -0.6) || average < -1.6 || (voiced < 0.15 && (noSpeech > 0.2 || average < -0.5))) {
                    suspicious = true;
                    continue;
                }
                for (auto& token : segment) {
                    if (token.t0 < 0) token.t0 = s0;
                    if (token.t1 < token.t0) token.t1 = std::max(token.t0, s1);
                    tokens.push_back(std::move(token));
                }
            }
            // Palavras desta janela. Com DTW o token começa no instante
            // alinhado e vai até o seguinte; o acabamento pela energia corta
            // o que sobrar em cima de pausa.
            std::vector<CaptionWord> words;
            {
                text::WhisperWords builder(words, 0, 0, std::numeric_limits<double>::max());
                for (usize k = 0; k < tokens.size(); ++k) {
                    const Token& token = tokens[k];
                    double a = token.t0, b = token.t1;
                    if (dtw && token.dtw >= 0) {
                        a = token.dtw;
                        const double next = k + 1 < tokens.size() && tokens[k + 1].dtw >= a ? tokens[k + 1].dtw : -1.0;
                        b = next >= 0 ? next : std::max(a + 0.1, std::min(token.t1, a + 1.0));
                    }
                    builder.token(token.text, a, std::max(a, b), a, std::max(a, b));
                }
                builder.flush();
            }
            std::erase_if(words, [](const CaptionWord& w) { return classify_whisper_phrase(w.text) == WhisperPhrase::NonSpeech; });
            refine_word_timing(words, activity);
            if (drop_repeated_ngrams(words) > 0) suspicious = true;
            // Eco do prompt: o começo da janela repete o fim da anterior (não
            // vale no corte sem pausa, em que a sobreposição repete de verdade).
            if (!prompt.empty() && window.ownBegin <= window.spans.front().first + 1e-3 && drop_prompt_echo(words, result) > 0)
                suspicious = true;
            // Posse pelo meio da palavra: na sobreposição de um corte sem
            // pausa, cada palavra sai uma vez só.
            {
                text::WhisperWords owner(result, 0, window.ownBegin, window.ownEnd);
                for (const CaptionWord& w : words) owner.token(" " + w.text, w.start, w.end, w.start, w.end);
                owner.flush();
            }
            if (result.size() > 100000) return Status{Errc::BudgetExceeded, "transcricao muito longa"};
            f64 packedSeconds = 0;
            for (const auto& piece : map.pieces) packedSeconds += piece.packed1 - piece.packed0;
            if (autoLanguage && locked.empty() && packedSeconds >= 4.0 && !words.empty()) {
                const int id = whisper_full_lang_id(ctx.get());
                if (const char* code = id >= 0 ? whisper_lang_str(id) : nullptr) locked = code;
            }
            // Janela com laço ou alucinação não condiciona a próxima.
            prompt = suspicious ? std::string{} : whisper_prompt_tail(result);
            cursor = window.next > cursor + 1e-3 ? window.next : cursor + kWhisperWindowSec;
            own = window.nextOwn;
            report(cursor);
        }
        progress(100); return result;
    } catch (const std::bad_alloc&) { return Status{Errc::OutOfMemory, "memoria insuficiente para Whisper"}; }
      catch (...) { return Status{Errc::DecodeFailed, "falha na transcricao local"}; }
}
}
