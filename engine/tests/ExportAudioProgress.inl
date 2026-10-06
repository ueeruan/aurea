namespace export_audio_progress_test {
struct State {
    bool fail = true;
    std::atomic<bool> entered{false};
    std::atomic<u32> reads{0};
};
class Decoder final : public audio::AudioDecoderBackend {
public:
    explicit Decoder(State& s) : state_(s) { info_.sampleRate=48000; info_.channels=2; info_.durationUs=1'000'000; }
    const audio::AudioStreamInfo& info() const noexcept override { return info_; }
    Status seek(i64) noexcept override { cursor_=0; return OkStatus; }
    Status read(std::vector<f32>& out, i64& pts, bool& eos) noexcept override {
        state_.entered.store(true, std::memory_order_release); ++state_.reads;
        if (state_.fail) return Status{Errc::DecodeFailed, "audio fixture ilegivel"};
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        pts=cursor_*1'000'000/48000;
        out.assign(480*2,.25f); cursor_+=480; eos=cursor_>=48000;
        return OkStatus;
    }
private:
    State& state_; audio::AudioStreamInfo info_{}; i64 cursor_=0;
};
class Factory final : public VideoSourceFactory {
public:
    Factory(const SyntheticConfig& c, State& s) : config_(c), state_(s) {}
    bool probe(const char* path, MediaProbe& out) override { return SyntheticFactory(config_).probe(path,out); }
    std::unique_ptr<VideoDecoderBackend> open_video(const Asset&, MediaPriority) override {
        return std::make_unique<SyntheticDecoder>(config_);
    }
    std::unique_ptr<audio::AudioDecoderBackend> open_audio(const char*) override { return std::make_unique<Decoder>(state_); }
private:
    SyntheticConfig config_; State& state_;
};
}

AUREA_TEST(ExportAudioProgress, BrokenAudibleMediaMustAbortInsteadOfFinishingSilentVideo) {
    SyntheticConfig cfg; cfg.width=64; cfg.height=36; cfg.audioRate=48000; cfg.audioSeconds=1;
    export_audio_progress_test::State state;
    export_audio_progress_test::Factory factory(cfg,state);
    auto* backend=new MockBackend(); backend->mapBuffers=true;
    Rig rig(cfg,30,3,2,&factory,backend); AUREA_CHECK(rig.ok); if(!rig.ok) return;
    rig.comp()->layer(rig.video_layer())->visible=false;
    const auto result=run_export(rig,36,30,false,5);
    AUREA_CHECK(result.finished);
    AUREA_CHECK(state.entered.load());
    AUREA_CHECK_EQ(result.p.result,Errc::DecodeFailed);
    AUREA_CHECK_EQ(result.p.failure,5u); // ExportFailure::Media on Android and iOS
    AUREA_CHECK(rig.cap.aborted && !rig.cap.finished);
    AUREA_CHECK_EQ(rig.cap.audioFrames,0);
}

AUREA_TEST(ExportAudioProgress, CancelInterruptsAudioBlockPreparationBetweenNativeReads) {
    SyntheticConfig cfg; cfg.width=64; cfg.height=36; cfg.audioRate=48000; cfg.audioSeconds=1;
    export_audio_progress_test::State state; state.fail=false;
    export_audio_progress_test::Factory factory(cfg,state);
    auto* backend=new MockBackend(); backend->mapBuffers=true;
    Rig rig(cfg,30,3,2,&factory,backend); AUREA_CHECK(rig.ok); if(!rig.ok) return;
    rig.comp()->layer(rig.video_layer())->visible=false;
    ExportSettings settings; settings.height=36;
    AUREA_CHECK(rig.e.start_export(settings,"audio-cancel-unused.mp4").ok());
    const auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!state.entered.load() && std::chrono::steady_clock::now()<readyDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(state.entered.load());
    const auto start=std::chrono::steady_clock::now();
    AUREA_CHECK(rig.e.cancel_export().ok());
    const auto deadline=start+std::chrono::seconds(3);
    while(!rig.e.export_progress().finished && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto elapsed=std::chrono::steady_clock::now()-start;
    std::printf("    cancel during PCM prepare: %.1f ms, reads=%u\n",
        std::chrono::duration<double,std::milli>(elapsed).count(),state.reads.load());
    AUREA_CHECK(rig.e.export_progress().finished);
    AUREA_CHECK_EQ(rig.e.export_progress().result,Errc::Cancelled);
    AUREA_CHECK(elapsed<std::chrono::milliseconds(250));
    AUREA_CHECK(rig.cap.aborted && !rig.cap.finished);
    AUREA_CHECK_EQ(rig.cap.audioFrames,0);
}
