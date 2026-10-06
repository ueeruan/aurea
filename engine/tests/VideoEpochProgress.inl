// Reproduce a real in-flight decode, then change only the seek/session epoch.
// The caller asks for the same PTS again; it must not wait for a different seek
// or another UI gesture to replace the discarded old-generation picture.
AUREA_TEST(VideoEpochProgress, SameTargetAfterEpochChangeRestartsOutstandingDecode) {
    struct Decoder final : VideoDecoderBackend {
        VideoStreamInfo stream{};
        std::mutex mutex;
        std::condition_variable ready;
        bool entered = false, release = false;
        std::atomic<u32> reads{0};
        i64 cursor = 0;
        Decoder() { stream.fps=30; stream.durationUs=1'000'000; stream.preciseFrameTiming=true; }
        const VideoStreamInfo& info() const noexcept override { return stream; }
        Status seek_to_keyframe(i64 target) noexcept override { cursor=target; return OkStatus; }
        Status next_frame(i64, FrameRef& out, i64& pts, bool& eos) noexcept override {
            if (++reads == 1) {
                std::unique_lock<std::mutex> lock(mutex); entered=true; ready.notify_all();
                ready.wait_for(lock,std::chrono::seconds(3),[&]{return release;});
            }
            pts=cursor; eos=false; out=frame_at(cursor); out->durationUs=kFrame;
            cursor+=kFrame;
            return OkStatus;
        }
    };
    auto decoder=std::make_unique<Decoder>(); auto* observed=decoder.get();
    VideoSource source(std::move(decoder),MediaPriority::Preview);
    source.start(); source.set_epoch(1);
    const DecodeRequest request{0,DecodeMode::Still,0,1.f};
    source.request(request);
    {
        std::unique_lock<std::mutex> lock(observed->mutex);
        AUREA_CHECK(observed->ready.wait_for(lock,std::chrono::seconds(2),[&]{return observed->entered;}));
    }
    source.set_epoch(2);
    source.request(request);
    {
        std::lock_guard<std::mutex> lock(observed->mutex); observed->release=true;
    }
    observed->ready.notify_all();
    const bool ready=source.wait_for(0,500);
    bool exact=false; const auto frame=source.frame_for(0,&exact);
    source.stop();
    AUREA_CHECK(ready && frame && exact);
    AUREA_CHECK(observed->reads.load()>=2u);
}
