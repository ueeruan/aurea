namespace {

// The public capability object is backed by mutable backend state. This test
// only disables an optional upload format: it never claims an unsupported
// format is available or adds a production setting to force the fallback.
struct P010HalfCapabilityScope {
    GPUCapabilities& caps;
    bool saved;
    explicit P010HalfCapabilityScope(bool half) : caps(const_cast<GPUCapabilities&>(gpu().backend.capabilities())),
        saved(caps.r16UnormSampled) { if (half) caps.r16UnormSampled = false; }
    ~P010HalfCapabilityScope() { caps.r16UnormSampled = saved; }
};

FloatImage render_p010(Scene& scene, const FrameRef& frame, u32 denominator, bool half) {
    P010HalfCapabilityScope capability(half);
    RenderSettings settings; settings.previewDenominator = denominator;
    settings.finalQuality = true; settings.dither = false;
    FrameSnapshot snapshot;
    static u64 frameNumber = 0;
    gpu().renderer.prepare(*scene.comp, scene.project, FrameIndex{0}, nullptr, nullptr, nullptr,
        settings, ++frameNumber, 0, DecodeMode::Still, 1.f, snapshot);
    AUREA_CHECK_EQ(snapshot.layers.size(), 1u); if (snapshot.layers.empty()) return {};
    auto& layer = snapshot.layers[0];
    layer.compFromLayer = Mat4::identity(); layer.source.kind = LayerSource::Kind::Video;
    layer.source.width = scene.comp->width(); layer.source.height = scene.comp->height();
    layer.source.frame = frame; layer.source.frameExact = true;
    const u32 width = scene.comp->width() / denominator, height = scene.comp->height() / denominator;
    OffscreenTarget target{gpu().target(width, height), width, height};
    FrameStats stats; RenderTimings timings;
    AUREA_CHECK(gpu().renderer.render(snapshot, settings, &target, stats, timings).ok());
    AUREA_CHECK(!gpu().renderer.take_incomplete()); AUREA_CHECK_EQ(stats.layersRendered, 1u);
    gpu().backend.wait_idle();
    std::vector<u16> pixels(static_cast<usize>(width) * height * 4);
    AUREA_CHECK(gpu().backend.read_texture(target.texture, pixels.data(), width * 8).ok());
    FloatImage out; out.width = width; out.height = height; out.px.resize(pixels.size());
    for (usize i = 0; i < pixels.size(); ++i) out.px[i] = half_to_float(pixels[i]);
    return out;
}

f32 expected_p010_gray(u32 code, bool fullRange, TransferFunction transfer) {
    const double encoded = std::clamp(fullRange ? code / 1023.0 : (static_cast<double>(code) - 64) / 876.0, 0.0, 1.0);
    double linear = encoded;
    if (transfer == TransferFunction::SRGB) linear = encoded <= .04045 ? encoded / 12.92 : std::pow((encoded + .055) / 1.055, 2.4);
    if (transfer == TransferFunction::PQ) {
        const double p = std::pow(encoded, 1.0 / (2523.0 / 32.0));
        linear = 10000.0 * std::pow(std::max(p - 3424.0 / 4096.0, 0.0) / (2413.0 / 128.0 - 2392.0 / 128.0 * p),
            1.0 / (2610.0 / 16384.0)) / 203.0;
    }
    if (transfer == TransferFunction::HLG) {
        const double scene = encoded <= .5 ? encoded * encoded / 3.0
            : (std::exp((encoded - .55991073) / .17883277) + .28466892) / 12.0;
        linear = 1000.0 * scene * std::pow(std::max(scene, 1e-6), .2) / 203.0;
    }
    if (transfer == TransferFunction::PQ || transfer == TransferFunction::HLG) {
        const double peak = 1000.0 / 203.0;
        linear = linear * (1.0 + linear / (peak * peak)) / (1.0 + linear);
    }
    return static_cast<f32>(linear);
}

void compare_p010_uploads(const FloatImage& half, const FloatImage& unorm, u32 profile = 0) {
    AUREA_CHECK_EQ(half.px.size(), unorm.px.size()); if (half.px.size() != unorm.px.size()) return;
    f64 squares = 0; f32 worst = 0; usize worstAt = 0;
    for (usize i = 0; i < half.px.size(); ++i) {
        AUREA_CHECK(std::isfinite(half.px[i]) && std::isfinite(unorm.px[i]));
        const f32 difference = std::abs(half.px[i] - unorm.px[i]);
        if (difference > worst) { worst = difference; worstAt = i; }
        squares += static_cast<f64>(difference) * difference;
    }
    // Both paths finish in RGBA16F. Allow one half ULP around the HDR white,
    // while the aggregate error catches broad color/range changes.
    const f64 rms = std::sqrt(squares / std::max<usize>(1, half.px.size()));
    if (worst > .0021f || rms > .0002)
        std::printf("\n    P010 profile=%u max=%.9f rms=%.9f at=(%zu,%zu,%zu) half=%.9f unorm=%.9f\n",
            profile, worst, rms, (worstAt / 4) % half.width, (worstAt / 4) / half.width,
            worstAt % 4, half.px[worstAt], unorm.px[worstAt]);
    AUREA_CHECK(worst <= .0021f);
    AUREA_CHECK(rms <= .0002);
}
}

AUREA_TEST(P010VideoGpu, HalfUploadRendersAll1024DistinctCodes) {
    AUREA_REQUIRE_GPU();
    std::vector<u8> luma(1024 * 2 * 2), chroma(512 * 4);
    for (u32 y = 0; y < 2; ++y) for (u32 x = 0; x < 1024; ++x) {
        const u32 word = x << 6;
        luma[y * 2048 + x * 2] = static_cast<u8>(word);
        luma[y * 2048 + x * 2 + 1] = static_cast<u8>(word >> 8);
    }
    for (u32 x = 0; x < 1024; ++x) { chroma[x * 2] = 0; chroma[x * 2 + 1] = 128; }
    auto* raw = new DecodedFrame;
    raw->width = 1024; raw->height = 2; raw->format = PixelFormat::P010;
    raw->planeCount = 2; raw->planes[0] = luma.data(); raw->planes[1] = chroma.data();
    raw->strides[0] = raw->strides[1] = 2048; raw->color.bitDepth = 10;
    raw->color.fullRange = true; raw->color.transfer = TransferFunction::Linear;
    FrameRef frame = FrameRef::adopt(raw);
    Scene scene(1024, 2); scene.solid(1024, 2, Vec4{1,1,1,1}, 512, 1);
    const FloatImage half = render_p010(scene, frame, 1, true);
    AUREA_CHECK_EQ(half.width, 1024u); AUREA_CHECK_EQ(half.height, 2u);
    if (half.width != 1024 || half.height != 2) return;
    for (u32 x = 0; x < 1024; ++x) {
        const auto pixel = half.at(x, 0);
        for (u32 c = 0; c < 3; ++c) {
            // The final RGBA16F store may round toward zero on both upload
            // paths. Require recovery of the original code, not a float32
            // error bound narrower than one half-precision output step.
            AUREA_CHECK(std::isfinite(pixel[c]));
            AUREA_CHECK_EQ(std::lround(pixel[c] * 1023.f), static_cast<long>(x));
        }
        AUREA_CHECK_NEAR(pixel[3], 1.f, .001f);
        if (x) AUREA_CHECK(pixel[0] > half.at(x - 1, 0)[0]);
    }
    if (gpu().backend.capabilities().r16UnormSampled) {
        const FloatImage unorm = render_p010(scene, frame, 1, false);
        u32 halfCodes = 0, unormCodes = 0; f32 halfAnalytic = 0, unormAnalytic = 0;
        for (u32 x = 0; x < 1024; ++x) for (u32 c = 0; c < 3; ++c) {
            const f32 a = half.at(x, 0)[c], b = unorm.at(x, 0)[c];
            halfCodes += std::lround(a * 1023.f) != static_cast<long>(x);
            unormCodes += std::lround(b * 1023.f) != static_cast<long>(x);
            AUREA_CHECK_EQ(std::lround(b * 1023.f), static_cast<long>(x));
            halfAnalytic = std::max(halfAnalytic, std::abs(a - x / 1023.f));
            unormAnalytic = std::max(unormAnalytic, std::abs(b - x / 1023.f));
        }
        std::printf("\n    P010 ramp half/unorm analytic=%.9f/%.9f code_mismatches=%u/%u\n",
            halfAnalytic, unormAnalytic, halfCodes, unormCodes);
        compare_p010_uploads(half, unorm);
    }
}

AUREA_TEST(P010VideoGpu, CpuHalfPreservesTenBitRangesHdrCropAndEveryRotation) {
    AUREA_REQUIRE_GPU();
    const bool unormAvailable = gpu().backend.capabilities().r16UnormSampled;
    std::printf(" (norm16=%u; CPU-only P010, no codec/native import) ", unormAvailable ? 1u : 0u);
    constexpr u32 quadrants[4][4] = {{0,1,2,3}, {2,0,3,1}, {3,2,1,0}, {1,3,0,2}};
    for (bool fullRange : {false, true}) for (u32 rotation : {0u,90u,180u,270u})
    for (auto transfer : {TransferFunction::SRGB, TransferFunction::Linear, TransferFunction::PQ, TransferFunction::HLG}) {
        // Unsupported norm16 devices additionally cover odd source strides.
        auto* raw = new P010VideoFrame(fullRange, false, !unormAvailable);
        raw->rotation = rotation; raw->color.transfer = transfer;
        FrameRef frame = FrameRef::adopt(raw);
        const u32 width = rotation % 180 ? raw->visibleHeight : raw->visibleWidth;
        const u32 height = rotation % 180 ? raw->visibleWidth : raw->visibleHeight;
        Scene scene(width, height);
        scene.solid(static_cast<f32>(width), static_cast<f32>(height), Vec4{1,1,1,1}, width * .5f, height * .5f);
        for (u32 denominator : {1u, 2u}) {
            const FloatImage half = render_p010(scene, frame, denominator, true);
            for (u32 y = 0; y < half.height; ++y) for (u32 x = 0; x < half.width; ++x) {
                if (std::abs(static_cast<i32>(x) - static_cast<i32>(half.width / 2)) <= 1
                    || std::abs(static_cast<i32>(y) - static_cast<i32>(half.height / 2)) <= 1) continue;
                const u32 quadrant = quadrants[rotation / 90][(x >= half.width / 2 ? 1u : 0u) + (y >= half.height / 2 ? 2u : 0u)];
                const u32 code = (fullRange ? P010VideoFrame::fullCodes : P010VideoFrame::limitedCodes)[quadrant];
                const f32 expected = expected_p010_gray(code, fullRange, transfer);
                const auto pixel = half.at(x, y);
                for (u32 c = 0; c < 3; ++c) {
                    AUREA_CHECK(std::isfinite(pixel[c])); AUREA_CHECK_NEAR(pixel[c], expected, .0021f);
                }
                AUREA_CHECK_NEAR(pixel[3], 1.f, .001f);
            }
            if (unormAvailable) compare_p010_uploads(half, render_p010(scene, frame, denominator, false));
        }
    }
}

AUREA_TEST(P010VideoGpu, HalfAndUnormUseTheSameChromaMatrixPrimariesAndTransfers) {
    AUREA_REQUIRE_GPU();
    if (!gpu().backend.capabilities().r16UnormSampled) {
        std::printf(" (norm16 unavailable: equivalence requires a supporting GPU; analytic fallback test runs above) ");
        return;
    }
    for (bool fullRange : {false, true}) for (auto matrix : {YCbCrMatrix::BT601, YCbCrMatrix::BT709, YCbCrMatrix::BT2020})
    for (auto transfer : {TransferFunction::SRGB, TransferFunction::Linear, TransferFunction::PQ, TransferFunction::HLG}) {
        auto* raw = new P010VideoFrame(fullRange, true);
        raw->color.matrix = matrix; raw->color.transfer = transfer;
        raw->color.primaries = matrix == YCbCrMatrix::BT2020 ? ColorPrimaries::BT2020
            : matrix == YCbCrMatrix::BT601 ? ColorPrimaries::BT601 : ColorPrimaries::BT709;
        FrameRef frame = FrameRef::adopt(raw);
        Scene scene(raw->visibleWidth, raw->visibleHeight);
        scene.solid(static_cast<f32>(raw->visibleWidth), static_cast<f32>(raw->visibleHeight), Vec4{1,1,1,1},
            raw->visibleWidth * .5f, raw->visibleHeight * .5f);
        for (u32 denominator : {1u, 2u}) {
            const FloatImage half = render_p010(scene, frame, denominator, true);
            compare_p010_uploads(half, render_p010(scene, frame, denominator, false),
                (fullRange ? 1000u : 0u) + static_cast<u32>(matrix) * 100u + static_cast<u32>(transfer) * 10u + denominator);
        }
    }
}
