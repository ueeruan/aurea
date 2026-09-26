#include "aurea/text/LocalWhisper.hpp"
#include "aurea/audio/Audio.hpp"
#include "aurea/media/MediaManager.hpp"
#include "whisper.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace aurea::text {
Result<std::vector<CaptionWord>> transcribe_local(VideoSourceFactory& factory,
    const std::string& source, const std::string& model, const std::string& language,
    std::atomic<bool>& cancelled, const std::function<void(int)>& progress, double start, double end) noexcept {
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
        auto cp = whisper_context_default_params(); cp.use_gpu = false;
        std::unique_ptr<whisper_context, decltype(&whisper_free)> ctx(
            whisper_init_from_file_with_params(model.c_str(), cp), whisper_free);
        if (!ctx) return Status{Errc::CorruptData, "modelo Whisper indisponivel"};
        const double duration = info.durationUs / 1000000.0;
        const double stop = end > 0 ? std::min(end, duration) : duration;
        if (stop <= start) return Status{Errc::InvalidArgument, "intervalo sem audio"};
        std::vector<CaptionWord> result;
        // Overlap gives the decoder context at boundaries; ownership by midpoint
        // ensures a word is emitted only once. RAM is bounded independently of duration.
        for (double base = start; base < stop; base += 28.0) {
            if (cancelled.load()) return Status{Errc::Cancelled, "transcricao cancelada"};
            const double from = std::max(start, base - 1.0), to = std::min(stop, base + 29.0);
            const i64 first = static_cast<i64>(std::llround(from * info.sampleRate));
            const i64 count = static_cast<i64>(std::ceil((to - from) * info.sampleRate));
            std::vector<float> stereo(static_cast<usize>(count) * 2, 0);
            auto seek = decoder->seek(static_cast<i64>(from * 1000000));
            if (!seek) return seek;
            bool eos = false; int emptyReads = 0;
            while (!eos) {
                if (cancelled.load()) return Status{Errc::Cancelled, "transcricao cancelada"};
                std::vector<float> block; i64 pts = 0;
                auto read = decoder->read(block, pts, eos);
                if (!read) return read;
                if (block.empty()) { if (++emptyReads > 1000) return Status{Errc::DecodeFailed, "decoder sem progresso"}; continue; }
                emptyReads = 0;
                const i64 offset = static_cast<i64>(std::llround(pts * info.sampleRate / 1000000.0)) - first;
                if (offset >= count) break;
                for (usize i = 0; i < block.size() / info.channels; ++i) {
                    const i64 dst = offset + static_cast<i64>(i);
                    if (dst < 0 || dst >= count) continue;
                    audio::to_stereo(block.data() + i * info.channels, info.channels,
                                     stereo[static_cast<usize>(dst) * 2], stereo[static_cast<usize>(dst) * 2 + 1]);
                }
            }
            const u32 samples = static_cast<u32>(std::ceil((to - from) * WHISPER_SAMPLE_RATE));
            std::vector<float> resampled(static_cast<usize>(samples) * 2), pcm(samples);
            audio::resample_to_mix(stereo.data(), count, 0, static_cast<double>(info.sampleRate) / WHISPER_SAMPLE_RATE,
                                   resampled.data(), samples);
            for (u32 i = 0; i < samples; ++i) pcm[i] = (resampled[i*2] + resampled[i*2+1]) * 0.5f;
            stereo.clear(); stereo.shrink_to_fit(); resampled.clear(); resampled.shrink_to_fit();
            auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
            p.n_threads = 2; p.translate = false; p.no_context = true;
            p.language = language.empty() ? "auto" : language.c_str();
            p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
            p.token_timestamps = true; p.split_on_word = true; p.max_len = 1;
            p.abort_callback = [](void* state) { return static_cast<std::atomic<bool>*>(state)->load(); };
            p.abort_callback_user_data = &cancelled;
            struct Progress { const std::function<void(int)>* callback; double base, start, stop; } state{&progress, base, start, stop};
            p.progress_callback = [](whisper_context*, whisper_state*, int percent, void* opaque) {
                auto& s = *static_cast<Progress*>(opaque);
                (*s.callback)(std::clamp(static_cast<int>(100 * (s.base - s.start + 28.0 * percent / 100) / (s.stop - s.start)), 0, 99));
            }; p.progress_callback_user_data = &state;
            if (whisper_full(ctx.get(), p, pcm.data(), static_cast<int>(pcm.size())) != 0)
                return Status{cancelled.load() ? Errc::Cancelled : Errc::DecodeFailed, "Whisper interrompido"};
            CaptionWord word;
            auto flush = [&] {
                const double middle = (word.start + word.end) * .5;
                if (!word.text.empty() && word.end > word.start && middle >= base && middle < std::min(stop, base + 28.0)) {
                    word.start = std::max(start, word.start); word.end = std::min(stop, word.end);
                    if (result.empty() || word.start >= result.back().end - .02) result.push_back(word);
                }
                word = {};
            };
            for (int s = 0; s < whisper_full_n_segments(ctx.get()); ++s) {
                for (int t = 0; t < whisper_full_n_tokens(ctx.get(), s); ++t) {
                    const auto token = whisper_full_get_token_data(ctx.get(), s, t);
                    if (token.id >= whisper_token_eot(ctx.get()) || token.t0 < 0 || token.t1 < token.t0) continue;
                    std::string text = whisper_full_get_token_text(ctx.get(), s, t);
                    if (text.empty()) continue;
                    if (text.front() == ' ') flush();
                    const auto firstChar = text.find_first_not_of(" \r\n\t");
                    if (firstChar == std::string::npos) continue;
                    if (word.text.empty()) word.start = from + token.t0 * .01;
                    word.text += text.substr(firstChar); word.end = from + token.t1 * .01;
                }
            }
            flush();
            if (result.size() > 100000) return Status{Errc::BudgetExceeded, "transcricao muito longa"};
        }
        progress(100); return result;
    } catch (const std::bad_alloc&) { return Status{Errc::OutOfMemory, "memoria insuficiente para Whisper"}; }
      catch (...) { return Status{Errc::DecodeFailed, "falha na transcricao local"}; }
}
}
