#include "aurea/render/PreviewViewport.hpp"
#include "aurea/render/PreviewCachePolicy.hpp"

AUREA_TEST(PreviewViewport, SmallPhysicalPreviewSavesPixelsWithoutUpscaling) {
    auto p = preview_viewport_scale(true, 1080, 1920, 1080, 600, 1, 1, 1);
    AUREA_CHECK_EQ(1080u * p.numerator / p.denominator, 432u);
    AUREA_CHECK_EQ(1920u * p.numerator / p.denominator, 768u);
    auto landscape = preview_viewport_scale(true, 1920, 1080, 600, 1080, 1, 1, 1);
    AUREA_CHECK_EQ(p.numerator, landscape.numerator);
    AUREA_CHECK_EQ(p.denominator, landscape.denominator);
    auto small = preview_viewport_scale(true, 320, 180, 1080, 600, 1, 1, 1);
    AUREA_CHECK_EQ(small.numerator, 1u); AUREA_CHECK_EQ(small.denominator, 1u);
    AUREA_CHECK(preview_cache_capacity(432, 768, 48ull * 1024 * 1024)
        > preview_cache_capacity(1080, 1920, 48ull * 1024 * 1024));
}

AUREA_TEST(PreviewViewport, ManualScalesAndAdaptiveLowerResolutionArePreserved) {
    for (const u32 d : {1u, 2u, 4u, 8u}) {
        auto p = preview_viewport_scale(false, 3840, 2160, 600, 400, 1, 1, d);
        AUREA_CHECK_EQ(p.numerator, 1u); AUREA_CHECK_EQ(p.denominator, d);
    }
    auto lower = preview_viewport_scale(true, 1920, 1080, 1080, 600, 1, 1, 8);
    AUREA_CHECK_EQ(lower.numerator, 1u); AUREA_CHECK_EQ(lower.denominator, 8u);
    for (const auto p : {preview_viewport_scale(true, 1920,1080,0,0,1,1,2),
                         preview_viewport_scale(true, 0,1080,600,400,1,1,2)}) {
        AUREA_CHECK_EQ(p.numerator, 1u); AUREA_CHECK_EQ(p.denominator, 2u);
    }
}

AUREA_TEST(PreviewViewport, ZoomRestoresDetailAndSmallResizesKeepTheCacheBucket) {
    auto base = preview_viewport_scale(true, 1080,1920,1080,600,1,1,1);
    auto tinyResize = preview_viewport_scale(true,1080,1920,1080,610,1,1,1);
    AUREA_CHECK_EQ(base.numerator, tinyResize.numerator);
    AUREA_CHECK_EQ(base.denominator, tinyResize.denominator);
    auto zoomed = preview_viewport_scale(true,1080,1920,1080,600,2,1,1);
    AUREA_CHECK(f64(zoomed.numerator)/zoomed.denominator > f64(base.numerator)/base.denominator);
    auto close = preview_viewport_scale(true,1080,1920,1080,600,4,1,1);
    AUREA_CHECK_EQ(close.numerator, 1u); AUREA_CHECK_EQ(close.denominator, 1u);
    auto far = preview_viewport_scale(true,1080,1920,1080,600,.01f,1,1);
    AUREA_CHECK(1080u * far.numerator / far.denominator >= 360u);
}

AUREA_TEST(PreviewViewport, SharedEngineUsesTheSurfaceOnlyForAutomaticPreview) {
    Engine engine;
    auto config = headless_config(); config.backend = new aurea::test::MockBackend;
    config.initialPreviewScale = PreviewScale::Full;
    AUREA_CHECK(engine.initialize(config).ok());
    AUREA_CHECK(engine.new_project(1920,1080,30.,"viewport").ok());
    int window = 0;
    AUREA_CHECK(engine.attach_surface(&window,640,360).ok());
    AUREA_CHECK(engine.render_frame().ok());
    AUREA_CHECK_EQ(engine.read_status().previewWidth,1920u);
    Command automatic; automatic.type=CommandType::ViewportSetPreviewScale;
    automatic.preview_scale.automatic=1;
    AUREA_CHECK(engine.apply_command(automatic).ok());
    AUREA_CHECK(engine.render_frame().ok());
    AUREA_CHECK_EQ(engine.read_status().previewWidth,832u);
    AUREA_CHECK_EQ(engine.read_status().previewHeight,468u);
    Command zoom; zoom.type=CommandType::ViewportSetZoom; zoom.viewport_zoom.zoom=4;
    AUREA_CHECK(engine.apply_command(zoom).ok());
    AUREA_CHECK(engine.render_frame().ok());
    AUREA_CHECK_EQ(engine.read_status().previewWidth,1920u);
    zoom.viewport_zoom.zoom=1;
    AUREA_CHECK(engine.apply_command(zoom).ok());
    AUREA_CHECK(engine.resize_surface(960,540).ok());
    AUREA_CHECK(engine.render_frame().ok());
    AUREA_CHECK_EQ(engine.read_status().previewWidth,1216u);
    automatic.preview_scale.automatic=0;
    automatic.preview_scale.scaleNumerator=1; automatic.preview_scale.scaleDenominator=1;
    AUREA_CHECK(engine.apply_command(automatic).ok());
    AUREA_CHECK(engine.render_frame().ok());
    AUREA_CHECK_EQ(engine.read_status().previewWidth,1920u);
    engine.shutdown();
}
