AUREA_TEST(P010Video, HalfFallbackRetriesCompletePlanesAndCachesTheUploadFormat) {
    RenderFixture fixture;
    fixture.comp->set_size(32, 16);
    fixture.solid("P010", Vec4{1,1,1,1}, 16, 8);
    FrameSnapshot snapshot; fixture.prepare(snapshot);
    auto* raw = new test::P010VideoFrame;
    const FrameRef frame = FrameRef::adopt(raw);
    auto& layer = snapshot.layers[0]; layer.compFromLayer = Mat4::identity();
    layer.source.kind = LayerSource::Kind::Video; layer.source.width = 32; layer.source.height = 16;
    u32 uploads = 0, uniforms = 0;
    bool failChroma = true;
    fixture.backend.caps.r16UnormSampled = false;
    fixture.backend.beforeTextureUpload = [&](TextureHandle texture, const void* bytes, u32 stride) {
        const auto desc = fixture.backend.texture_desc(texture);
        if (!desc.debugName || std::string_view(desc.debugName) != "plano-de-video") return OkStatus;
        ++uploads;
        const bool chroma = desc.width != raw->width;
        const u32 p = chroma ? 1 : 0;
        if (!fixture.backend.caps.r16UnormSampled) {
            AUREA_CHECK(desc.format == (chroma ? SurfaceFormat::RG16F : SurfaceFormat::R16F));
            AUREA_CHECK_EQ(stride, raw->width * 2);
            AUREA_CHECK(bytes != raw->planes[p]);
            const auto* half = static_cast<const u16*>(bytes);
            const u32 components = chroma ? 2 : 1;
            for (u32 y = 0; y < desc.height; ++y) for (u32 x = 0; x < desc.width * components; ++x) {
                const auto* input = raw->planes[p] + y * raw->strides[p] + x * 2;
                const u32 code = (input[0] | (input[1] << 8)) >> 6;
                AUREA_CHECK_EQ(scene3d::half_to_float(half[y * desc.width * components + x]), code / 1024.f);
            }
        } else {
            AUREA_CHECK(desc.format == (chroma ? SurfaceFormat::RG16 : SurfaceFormat::R16));
            AUREA_CHECK_EQ(stride, raw->strides[p]); AUREA_CHECK(bytes == raw->planes[p]);
        }
        return chroma && failChroma ? Status{Errc::OutOfDeviceMemory} : OkStatus;
    };
    fixture.backend.beforeSetUniforms = [&](const void* bytes, u32 count) {
        if (count != 6 * sizeof(Vec4)) return;
        Vec4 parameters[6]; std::memcpy(parameters, bytes, sizeof(parameters));
        if (parameters[0].w != 10.f) return;
        ++uniforms;
        const f32 expected = fixture.backend.caps.r16UnormSampled ? 65535.f / (64.f * 1023.f) : 1024.f / 1023.f;
        AUREA_CHECK_EQ(parameters[4].y, expected);
        AUREA_CHECK_EQ(parameters[4].x, fixture.backend.caps.r16UnormSampled ? 0.f : 3.f);
        AUREA_CHECK_EQ(parameters[0].z, 0.f); AUREA_CHECK_EQ(parameters[1].x, 0.f);
    };
    FrameStats stats; RenderTimings timings; RenderSettings settings;
    auto render = [&] {
        layer.source.frame = frame;
        AUREA_CHECK(fixture.renderer.render(snapshot, settings, nullptr, stats, timings).ok());
    };
    render(); AUREA_CHECK(fixture.renderer.take_incomplete()); AUREA_CHECK_EQ(uploads, 2u);
    failChroma = false;
    render(); AUREA_CHECK(!fixture.renderer.take_incomplete()); AUREA_CHECK_EQ(uploads, 4u);
    render(); AUREA_CHECK(!fixture.renderer.take_incomplete()); AUREA_CHECK_EQ(uploads, 4u);
    // Same decoded content, but a different backend upload format: textures
    // must be recreated and the content uploaded again rather than aliased.
    fixture.backend.caps.r16UnormSampled = true;
    render(); AUREA_CHECK(!fixture.renderer.take_incomplete()); AUREA_CHECK_EQ(uploads, 6u);
    fixture.backend.caps.r16UnormSampled = false;
    render(); AUREA_CHECK(!fixture.renderer.take_incomplete()); AUREA_CHECK_EQ(uploads, 8u);
    AUREA_CHECK(uniforms >= 4u);
}
