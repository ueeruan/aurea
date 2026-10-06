// Faults occur at the platform decoder boundary; expectations are externally
// observable PCM, bounded decoder work and absence of cached partial audio.
namespace audio_progress_test {
struct State {
    enum Mode { RepeatedPts, EmptyPackets, ValidPreroll, RecoverOnReopen } mode = RepeatedPts;
    u32 reads = 0, opens = 0;
    std::atomic<bool>* cancelOnRead = nullptr;
};
class Decoder final : public audio::AudioDecoderBackend {
public:
    explicit Decoder(State& state) : state_(state), instance_(++state.opens) {
        info_.sampleRate = 48000; info_.channels = 2; info_.durationUs = 10'000'000;
    }
    const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    Status seek(i64) noexcept override { packet_ = 0; return OkStatus; }
    Status read(std::vector<f32>& out, i64& pts, bool& eos) noexcept override {
        ++state_.reads; eos = false;
        const bool progressing = state_.mode == State::ValidPreroll
            || (state_.mode == State::RecoverOnReopen && instance_ > 1);
        // The valid source intentionally seeks to zero, requiring >64 packets
        // of advancing preroll before a block at 2 seconds is available.
        pts = progressing ? static_cast<i64>(packet_) * 10'000 : 0;
        out.assign(state_.mode == State::EmptyPackets ? 0 : 480 * 2, .25f);
        ++packet_;
        eos = progressing && packet_ == 1000;
        if (state_.cancelOnRead) state_.cancelOnRead->store(true, std::memory_order_release);
        return OkStatus;
    }
private:
    State& state_;
    u32 instance_ = 0, packet_ = 0;
    audio::AudioStreamInfo info_{};
};
class Factory final : public VideoSourceFactory {
public:
    explicit Factory(State& s) : state(s) {}
    bool probe(const char*, MediaProbe&) override { return false; }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override { return {}; }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override {
        return std::make_unique<Decoder>(state);
    }
    State& state;
};
}

AUREA_TEST(AudioProgress, RepeatedPtsOrEmptyPacketsCannotProduceCachedSilence) {
    for (auto mode : {audio_progress_test::State::RepeatedPts, audio_progress_test::State::EmptyPackets}) {
        audio_progress_test::State state; state.mode = mode;
        audio_progress_test::Factory factory(state);
        audio::AudioBlockCache cache(&factory, 1ull << 20, false);
        cache.register_asset(1, {"broken-audio", 480000});
        const auto result = cache.fetch(1, 0);
        std::printf("    audio mode=%d reads=%u opens=%u cached=%d\n", int(mode), state.reads, state.opens, !!result);
        AUREA_CHECK(!result);
        AUREA_CHECK(!cache.find(1, 0));
        AUREA_CHECK(state.reads < 512u);
        AUREA_CHECK_EQ(state.opens, 2u); // one recovery attempt, then a real failure
    }
}

AUREA_TEST(AudioProgress, AdvancingPrerollIsNotMistakenForAStall) {
    audio_progress_test::State state; state.mode = audio_progress_test::State::ValidPreroll;
    audio_progress_test::Factory factory(state);
    audio::AudioBlockCache cache(&factory, 1ull << 20, false);
    cache.register_asset(1, {"long-preroll", 480000});
    const auto result = cache.fetch(1, 4);
    AUREA_CHECK(result);
    AUREA_CHECK(state.reads > 200u);
    AUREA_CHECK_EQ(state.opens, 1u);
    if (result) {
        f32 error=0; for(f32 value:result->pcm) error=std::max(error,std::abs(value-.25f));
        AUREA_CHECK_NEAR(error,0.f,1e-6f);
    }
}

AUREA_TEST(AudioProgress, ReopenRecoversWithoutPublishingTheBrokenAttempt) {
    audio_progress_test::State state; state.mode = audio_progress_test::State::RecoverOnReopen;
    audio_progress_test::Factory factory(state);
    audio::AudioBlockCache cache(&factory, 1ull << 20, false);
    cache.register_asset(1, {"recoverable", 480000});
    const auto result = cache.fetch(1, 1);
    AUREA_CHECK(result);
    AUREA_CHECK_EQ(state.opens, 2u);
    AUREA_CHECK(state.reads < 512u);
    if (result) {
        f32 error=0; for(f32 value:result->pcm) error=std::max(error,std::abs(value-.25f));
        AUREA_CHECK_NEAR(error,0.f,1e-6f);
    }
}

AUREA_TEST(AudioProgress, CancelledReadDoesNotCacheOrPoisonTheNextRequest) {
    audio_progress_test::State state; state.mode=audio_progress_test::State::ValidPreroll;
    std::atomic<bool> cancel{false}; state.cancelOnRead=&cancel;
    audio_progress_test::Factory factory(state);
    audio::AudioBlockCache cache(&factory,1ull<<20,false);
    cache.register_asset(1,{"cancelled",480000});
    AUREA_CHECK(!cache.fetch(1,0,&cancel));
    AUREA_CHECK(!cache.find(1,0));
    AUREA_CHECK_EQ(state.reads,1u);
    AUREA_CHECK_EQ(cache.stats().failures,0u);
    state.cancelOnRead=nullptr; cancel=false;
    const auto recovered=cache.fetch(1,0,&cancel);
    AUREA_CHECK(recovered);
    AUREA_CHECK_EQ(state.opens,2u);
    // Ignore only the source's initial sinc boundary, not interior missing PCM.
    if (recovered) {
        f32 error=0;
        for(usize i=64;i+64<recovered->pcm.size();++i) error=std::max(error,std::abs(recovered->pcm[i]-.25f));
        AUREA_CHECK_NEAR(error,0.f,1e-6f);
    }
}
