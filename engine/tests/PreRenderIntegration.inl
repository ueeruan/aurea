#if defined(AUREA_TEST_VULKAN)
#include "MockBackend.hpp"
#include "aurea/export/ImageEncode.hpp"
#include "stb_image.h"
#include <fstream>

namespace prerender_integration {
namespace fs = std::filesystem;
struct Rig {
    Engine engine;
    fs::path root;
    test::MockBackend* mock = nullptr;
    explicit Rig(bool fake, u32 width = 32, u32 height = 32, i64 duration = 3) {
        static std::atomic<u64> sequence{0};
        const auto workspace = fs::path(AUREA_TEST_DATA_DIR).parent_path().parent_path().parent_path();
        root = workspace / "build/validation/prerender-integration-20261013/fixtures" /
            ("own-" + std::to_string(monotonic_ns()) + "-" + std::to_string(++sequence));
        std::error_code error; fs::create_directories(root, error); AUREA_CHECK(!error);
        EngineConfig config; config.workerCount = 2; config.disableAutosave = true;
        config.enableTelemetry = false; config.memoryBudgetBytes = 256ull << 20;
        config.cacheDirectory = root.string(); config.exportGpuTimeoutMs = 1000;
        config.initialPreviewScale = PreviewScale::Full;
        if (fake) {
            mock = new test::MockBackend(); mock->mapBuffers = true;
            mock->beforeReadTexture = [this](TextureHandle texture, void* data, u32 stride) {
                const auto desc = mock->texture_desc(texture);
                auto* half = static_cast<u16*>(data);
                for (u32 y = 0; y < desc.height; ++y) for (u32 x = 0; x < desc.width; ++x) {
                    const usize at = y * (stride / 2) + x * 4;
                    half[at] = 0x3800; half[at + 1] = half[at + 2] = 0; half[at + 3] = 0x3800;
                }
                return OkStatus;
            };
            config.backend = mock;
        } else config.backend = new vk::Backend();
        config.backendConfig.enableValidation = false;
        AUREA_CHECK(engine.initialize(config).ok()); AUREA_CHECK(engine.new_project(width, height, 30, "own PreRender scene").ok());
        comp()->set_duration(FrameIndex{duration});
    }
    ~Rig() { engine.cancel_prerender(); engine.shutdown(); }
    Composition* comp() { return current_comp(engine); }
    u64 shape(f32 opacity = 1) {
        const auto added = engine.add_shape(0); AUREA_CHECK(added.ok()); if (!added.ok()) return 0;
        auto* layer = comp()->layer(LayerId::unpack(*added));
        layer->shape.shapeType = 1; layer->shape.bounds = Rect{0,0,f32(comp()->width()),f32(comp()->height())};
        layer->shape.fillColor = Vec4{1,0,0,1}; layer->transform.anchor = Vec3{comp()->width()*.5f,comp()->height()*.5f,0};
        layer->transform.position = layer->transform.anchor; layer->transform.opacity = opacity;
        return *added;
    }
    Status start(i32 direction) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            const Status result = engine.start_prerender(direction);
            if (result.code() != Errc::Busy || std::chrono::steady_clock::now() >= deadline) return result;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    PreRenderProgress wait(u32 seconds = 120) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        for (;;) {
            const auto progress = engine.query_prerender_progress();
            if (!progress.running || std::chrono::steady_clock::now() >= deadline) return progress;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    fs::path cache() const {
        std::error_code error;
        for (const auto& path : fs::directory_iterator(root / "pre-render", error))
            if (path.is_directory() && fs::exists(path.path() / "complete.bin")) return path.path();
        return {};
    }
};
bool is_cache_texture(const TextureDesc& desc) { return desc.debugName && std::strcmp(desc.debugName, "prerender-540p") == 0; }
u32 get32(const u8* p) { return u32(p[0]) | (u32(p[1])<<8) | (u32(p[2])<<16) | (u32(p[3])<<24); }
u64 get64(const u8* p) { return get32(p) | (u64(get32(p + 4))<<32); }
std::vector<u16> frame(const fs::path& directory, u64 index, u32 width, u32 height) {
    char name[40]; std::snprintf(name,sizeof(name),"frame-%020llu.bin",static_cast<unsigned long long>(index));
    std::ifstream input(directory/name,std::ios::binary); AUREA_CHECK(input.good());
    std::array<u8,96> header{}; input.read(reinterpret_cast<char*>(header.data()),header.size());
    AUREA_CHECK(input.good()); AUREA_CHECK_EQ(get64(header.data()+32),index);
    AUREA_CHECK_EQ(get64(header.data()+48),u64(width)*height*8);
    if (!input.good() || get64(header.data()+48) != u64(width)*height*8) return {};
    std::vector<u16> data(usize(width)*height*4); u64 offset=0;
    for (u32 n=0;n<get32(header.data()+60);++n) {
        u8 block[12]; input.read(reinterpret_cast<char*>(block),12); AUREA_CHECK(input.good());
        const u32 raw=get32(block),encoded=get32(block+4);
        AUREA_CHECK(raw<=65536 && encoded<=132096 && offset+raw<=data.size()*2);
        if (raw>65536 || encoded>132096 || offset+raw>data.size()*2) return {};
        std::vector<u8> payload(encoded); input.read(reinterpret_cast<char*>(payload.data()),encoded);
        AUREA_CHECK(input.good()); auto* out=reinterpret_cast<u8*>(data.data())+offset;
        AUREA_CHECK_EQ(stbi_zlib_decode_buffer(reinterpret_cast<char*>(out),raw,reinterpret_cast<const char*>(payload.data()),encoded),i32(raw));
        AUREA_CHECK_EQ(crc32_update(0,out,raw),get32(block+8)); offset+=raw;
    }
    AUREA_CHECK_EQ(offset,data.size()*2); AUREA_CHECK_EQ(crc32_update(0,reinterpret_cast<const u8*>(data.data()),offset),get32(header.data()+56));
    return data;
}
}

AUREA_TEST(PreRenderIntegration, NoSurfacePreparationCannotPublishAReadyUnuploadedSlot) {
    using namespace prerender_integration;
    Rig rig(true); rig.shape(.5f); std::atomic<u32> created{0},uploaded{0};
    rig.mock->beforeCreateTexture=[&](const TextureDesc& desc){if(is_cache_texture(desc))++created;return OkStatus;};
    rig.mock->beforeTextureUpload=[&](TextureHandle texture,const void*,u32){if(is_cache_texture(rig.mock->texture_desc(texture)))++uploaded;return OkStatus;};
    AUREA_CHECK(rig.start(1).ok()); const auto complete=rig.wait(); AUREA_CHECK(complete.complete && complete.error==0);
    for(u32 tick=0;tick<100 && !created.load();++tick){AUREA_CHECK(rig.engine.render_frame(false).ok());std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    AUREA_CHECK_EQ(uploaded.load(),0u);
    int window=0; AUREA_CHECK(rig.engine.attach_surface(&window,32,32).ok());
    for(u32 tick=0;tick<100 && !uploaded.load();++tick){AUREA_CHECK(rig.engine.render_frame(false).ok());std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    AUREA_CHECK(uploaded.load()>0); AUREA_CHECK(rig.engine.query_prerender_progress().complete);
}

AUREA_TEST(PreRenderIntegration, SurfaceLostAndTimeoutBeforeBeginPreserveCompletedDiskCache) {
    using namespace prerender_integration;
    for(const Errc failure:{Errc::SurfaceLost,Errc::Timeout}) {
        Rig rig(true); rig.shape(); std::atomic<u32> uploaded{0};
        rig.mock->beforeTextureUpload=[&](TextureHandle texture,const void*,u32){if(is_cache_texture(rig.mock->texture_desc(texture)))++uploaded;return OkStatus;};
        AUREA_CHECK(rig.start(1).ok()); AUREA_CHECK(rig.wait().complete);
        int window=0; AUREA_CHECK(rig.engine.attach_surface(&window,32,32).ok());
        for(u32 tick=0;tick<100 && !uploaded.load();++tick){AUREA_CHECK(rig.engine.render_frame(false).ok());std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        AUREA_CHECK(uploaded.load()>0);
        bool once=true; rig.mock->beforeBeginFrameStatus=[&]{if(once){once=false;return Status{failure};}return OkStatus;};
        (void)rig.engine.render_frame(false);
        AUREA_CHECK(!once); const auto progress=rig.engine.query_prerender_progress();
        AUREA_CHECK(progress.complete); AUREA_CHECK_EQ(progress.error,0);
        AUREA_CHECK(!rig.cache().empty());
        rig.mock->beforeBeginFrameStatus={};
    }
}

AUREA_TEST(PreRenderIntegration, CacheTextureAllocationFailureKeepsItsRealErrorInsteadOfCancellation) {
    using namespace prerender_integration;
    Rig rig(true); rig.shape(); AUREA_CHECK(rig.start(1).ok()); AUREA_CHECK(rig.wait().complete);
    std::atomic<u32> failed{0}; rig.mock->beforeCreateTexture=[&](const TextureDesc& desc){
        if(is_cache_texture(desc)){++failed;return Status{Errc::OutOfDeviceMemory,"own injected cache texture allocation failure"};}return OkStatus;};
    int window=0; AUREA_CHECK(rig.engine.attach_surface(&window,32,32).ok());
    for(u32 tick=0;tick<100 && !failed.load();++tick){(void)rig.engine.render_frame(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    AUREA_CHECK(failed.load()>0); const auto progress=rig.engine.query_prerender_progress();
    AUREA_CHECK(!progress.complete); AUREA_CHECK_EQ(progress.error,i32(Errc::OutOfDeviceMemory));
}

AUREA_TEST(PreRenderIntegration, PendingUploadRetriesAfterTransientBeginFailure) {
    using namespace prerender_integration;
    for(const Errc failure:{Errc::SurfaceLost,Errc::Timeout}) {
        Rig rig(true);rig.shape();AUREA_CHECK(rig.start(1).ok());AUREA_CHECK(rig.wait().complete);
        std::atomic<u32> created{0},uploaded{0};bool once=true;
        rig.mock->beforeCreateTexture=[&](const TextureDesc& desc){if(is_cache_texture(desc))++created;return OkStatus;};
        rig.mock->beforeTextureUpload=[&](TextureHandle texture,const void*,u32){if(is_cache_texture(rig.mock->texture_desc(texture)))++uploaded;return OkStatus;};
        rig.mock->beforeBeginFrameStatus=[&]{if(once && created.load()){once=false;return Status{failure};}return OkStatus;};
        int window=0;AUREA_CHECK(rig.engine.attach_surface(&window,32,32).ok());
        for(u32 tick=0;tick<100 && once;++tick){(void)rig.engine.render_frame(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        AUREA_CHECK(!once);AUREA_CHECK_EQ(uploaded.load(),0u);AUREA_CHECK(rig.engine.query_prerender_progress().complete);
        rig.mock->beforeBeginFrameStatus={};
        for(u32 tick=0;tick<100 && !uploaded.load();++tick){AUREA_CHECK(rig.engine.render_frame(false).ok());std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        AUREA_CHECK(uploaded.load()>0);AUREA_CHECK(rig.engine.query_prerender_progress().complete);
    }
}

AUREA_TEST(PreRenderIntegration, CorruptDiskFrameReportsCorruptDataAndReturnsToOrdinaryComposition) {
    using namespace prerender_integration;
    Rig rig(true);rig.shape();AUREA_CHECK(rig.start(1).ok());AUREA_CHECK(rig.wait().complete);
    const auto directory=rig.cache();AUREA_CHECK(!directory.empty());if(directory.empty())return;
    {
        std::fstream bad(directory/"frame-00000000000000000000.bin",std::ios::binary|std::ios::in|std::ios::out);
        AUREA_CHECK(bad.good());const char invalid=0;bad.write(&invalid,1);AUREA_CHECK(bad.good());
    }
    int window=0;AUREA_CHECK(rig.engine.attach_surface(&window,32,32).ok());
    PreRenderProgress progress;
    for(u32 tick=0;tick<100;++tick){AUREA_CHECK(rig.engine.render_frame(false).ok());progress=rig.engine.query_prerender_progress();
        if(!progress.complete)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    AUREA_CHECK(!progress.complete);AUREA_CHECK_EQ(progress.error,i32(Errc::CorruptData));
    AUREA_CHECK(rig.engine.render_frame(false).ok());AUREA_CHECK(rig.mock->presents>0);
}

AUREA_TEST(PreRenderIntegration, OtherEngineTranslationUnitMutationCancelsBeforeAnyProgressPoll) {
    using namespace prerender_integration;
    Rig rig(true);const u64 id=rig.shape();AUREA_CHECK(rig.start(1).ok());AUREA_CHECK(rig.wait().complete);
    AUREA_CHECK(rig.comp()->layer(LayerId::unpack(id))->layerAnimators.empty());
    AUREA_CHECK(rig.engine.add_layer_animator(id)>=0);
    AUREA_CHECK_EQ(rig.comp()->layer(LayerId::unpack(id))->layerAnimators.size(),usize{1});
    // Do not query progress or render after this mutation: either would mask
    // the missing eager invalidation by comparing the revised cache key.
    const Status restarted=rig.engine.start_prerender(-1);
    std::printf("    own other-TU edit immediate restart status=%d\n",restarted.raw());
    AUREA_CHECK(restarted.ok());
    if(restarted.ok())AUREA_CHECK(rig.wait().complete);
}

AUREA_TEST(PreRenderIntegration, Whole307Frame540pCompositionKeepsAlphaAndOriginalModelBothDirections) {
    AUREA_REQUIRE_GPU(); using namespace prerender_integration;
    for(const i32 direction:{1,-1}) {
        Rig rig(false,4,4,307); rig.comp()->set_transparent_background(true);
        const u64 id=rig.shape(); auto* layer=rig.comp()->layer(LayerId::unpack(id));
        auto& opacity=layer->tracks.get_or_create(TrackProperty::Opacity);
        (void)opacity.set(FrameIndex{0},.25f,Interpolation::Linear);(void)opacity.set(FrameIndex{306},1.f,Interpolation::Linear);
        const auto revision=rig.engine.read_status().modelRevision;
        const auto position=layer->transform.position;
        AUREA_CHECK(rig.start(direction).ok());const auto progress=rig.wait();
        std::printf("    own whole540p direction=%d frames=%llu/%llu size=%u,%u error=%d bytes=%llu\n",direction,
            static_cast<unsigned long long>(progress.framesDone),static_cast<unsigned long long>(progress.framesTotal),
            progress.width,progress.height,progress.error,static_cast<unsigned long long>(progress.diskBytes));
        AUREA_CHECK(progress.complete && !progress.running && progress.error==0);
        AUREA_CHECK_EQ(progress.framesDone,307ull);AUREA_CHECK_EQ(progress.framesTotal,307ull);
        AUREA_CHECK_EQ(progress.width,540u);AUREA_CHECK_EQ(progress.height,540u);AUREA_CHECK_EQ(progress.direction,direction);
        AUREA_CHECK_EQ(rig.engine.read_status().modelRevision,revision);
        layer=rig.comp()->layer(LayerId::unpack(id)); AUREA_CHECK_EQ(layer->tracks.get_or_create(TrackProperty::Opacity).keys.size(),usize{2});
        AUREA_CHECK_NEAR(layer->transform.position.x,position.x,1e-6);AUREA_CHECK_NEAR(layer->transform.position.y,position.y,1e-6);
        AUREA_CHECK(!rig.engine.export_progress().running);AUREA_CHECK_EQ(rig.engine.export_progress().framesTotal,0u);
        const auto directory=rig.cache();AUREA_CHECK(!directory.empty()); if(directory.empty())return;
        u32 count=0;for(const auto& path:fs::directory_iterator(directory)) if(path.path().filename().string().starts_with("frame-"))++count;
        AUREA_CHECK_EQ(count,307u);
        for(const u64 index:{0ull,153ull,306ull}) {
            const auto half=frame(directory,index,540,540); if(half.empty())return;
            const usize center=(270*540+270)*4;const f32 expected=.25f+.75f*f32(index)/306.f;
            AUREA_CHECK_NEAR(half_to_float(half[center]),expected,.002);
            AUREA_CHECK_NEAR(half_to_float(half[center+3]),expected,.002);
            AUREA_CHECK_NEAR(half_to_float(half[center+1]),0.,.0001);
        }
    }
}

AUREA_TEST(PreRenderIntegration, CompletedCacheInvalidatesOnEditUndoRedoAddAndProjectSwitch) {
    AUREA_REQUIRE_GPU(); using namespace prerender_integration;
    Rig rig(false,32,32,2);const u64 id=rig.shape();
    auto build=[&]{AUREA_CHECK(rig.start(1).ok());AUREA_CHECK(rig.wait().complete);};
    build();
    Command edit;edit.type=CommandType::LayerSetOpacity;edit.opacity.layer=LayerId::unpack(id);edit.opacity.opacity=.5f;
    AUREA_CHECK(rig.engine.apply_command(edit).ok());AUREA_CHECK(!rig.engine.query_prerender_progress().complete);
    build();Command undo;undo.type=CommandType::Undo;AUREA_CHECK(rig.engine.apply_command(undo).ok());AUREA_CHECK(!rig.engine.query_prerender_progress().complete);
    build();Command redo;redo.type=CommandType::Redo;AUREA_CHECK(rig.engine.apply_command(redo).ok());AUREA_CHECK(!rig.engine.query_prerender_progress().complete);
    build();AUREA_CHECK(rig.engine.add_shape(0).ok());AUREA_CHECK(!rig.engine.query_prerender_progress().complete);
    build();AUREA_CHECK(rig.engine.new_project(32,32,30,"different own project").ok());AUREA_CHECK(!rig.engine.query_prerender_progress().complete);
}

AUREA_TEST(PreRenderIntegration, MultipleLayersGaussianBlurAnimated3DAndMotionBlurMatchNativeFramesAndPngExport) {
    AUREA_REQUIRE_GPU(); using namespace prerender_integration;
    // capture_frame_rgba only downsizes. Use a native 960x540 composition so
    // this independent direct-render oracle has the cache's exact dimensions.
    Rig rig(false,960,540,4); rig.comp()->set_transparent_background(true); rig.shape(.25f);
    const auto card=rig.engine.add_shape(0);AUREA_CHECK(card.ok());if(!card.ok())return;
    auto* layer=rig.comp()->layer(LayerId::unpack(*card));
    layer->shape.shapeType=1;layer->shape.bounds=Rect{0,0,144,144};layer->shape.fillColor=Vec4{0,1,0,.7f};
    layer->transform.anchor=Vec3{72,72,0};layer->transform.position=Vec3{240,270,0};layer->motionBlur=true;
    auto& position=layer->tracks.get_or_create(TrackProperty::PositionX);
    (void)position.set(FrameIndex{0},240,Interpolation::Linear);(void)position.set(FrameIndex{4},720,Interpolation::Linear);
    Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=LayerId::unpack(*card);
    add.effect_add.effectType=effect_type_id(effect_keys::kGaussianBlur);add.effect_add.index=kInvalidIndex;
    AUREA_CHECK(rig.engine.apply_command(add).ok());
    rig.comp()->layer(LayerId::unpack(*card))->effects.back().params[0].constant.v[0]=3;
    const auto cube=rig.engine.add_shape3d(0,"own animated cube");AUREA_CHECK(cube.ok());if(!cube.ok())return;
    auto* object=rig.comp()->layer(LayerId::unpack(*cube));object->transform.rotation=Vec3{12,25,0};object->motionBlur=true;
    auto& rotate=object->tracks.get_or_create(TrackProperty::RotationY);
    (void)rotate.set(FrameIndex{0},-20,Interpolation::Linear);(void)rotate.set(FrameIndex{4},75,Interpolation::Linear);
    auto& blur=rig.comp()->motion_blur();blur.enabled=true;blur.samples=blur.previewSamples=8;blur.shutterAngle=180;blur.shutterPhase=-90;
    std::array<Image8,3> reference;
    for(u32 n=0;n<3;++n){seek_frame(rig.engine,n? n+1:0);auto& image=reference[n];
        AUREA_CHECK(rig.engine.capture_frame_rgba(960,image.rgba,image.width,image.height).ok());
        AUREA_CHECK_EQ(image.width,960u);AUREA_CHECK_EQ(image.height,540u);}
    const auto revision=rig.engine.read_status().modelRevision;
    AUREA_CHECK(rig.start(1).ok());const auto result=rig.wait();AUREA_CHECK(result.complete && result.error==0);
    AUREA_CHECK_EQ(rig.engine.read_status().modelRevision,revision);
    const auto directory=rig.cache();AUREA_CHECK(!directory.empty());if(directory.empty())return;
    for(u32 n=0;n<3;++n){const auto data=frame(directory,n?n+1:0,960,540);if(data.empty())return;
        std::vector<u8> pixels(data.size());linear_half_to_srgb8(data.data(),data.size()/4,pixels.data());
        AUREA_CHECK_EQ(pixels.size(),reference[n].rgba.size());
        if(pixels.size()!=reference[n].rgba.size())return;
        u32 maximum=0;f64 mean=0;
        for(usize c=0;c<pixels.size();++c){const u32 delta=std::abs(int(pixels[c])-int(reference[n].rgba[c]));maximum=std::max(maximum,delta);mean+=delta;}
        mean/=pixels.size();std::printf("    own layers/effects/blur/3D frame=%u direct/cache max=%u mean=%.6f\n",n?n+1:0,maximum,mean);
        AUREA_CHECK(maximum<=3);AUREA_CHECK(mean<.02);
    }
    seek_frame(rig.engine,2);ImageExportSettings png;png.shortSide=540;
    const auto output=rig.root/"ordinary-export.png";
    AUREA_CHECK(rig.engine.start_image_export(png,output.string().c_str()).ok());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!rig.engine.export_progress().finished && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto exported=rig.engine.export_progress();AUREA_CHECK(exported.finished && exported.result==Errc::Ok);AUREA_CHECK_EQ(exported.framesDone,1u);
    AUREA_CHECK(fs::exists(output));AUREA_CHECK(rig.engine.query_prerender_progress().complete);
}

AUREA_TEST(PreRenderIntegration, CachedCompositionBypassesHeavySceneAndKeepsZoomPanAndCheckerboardOutput) {
    AUREA_REQUIRE_GPU(); using namespace prerender_integration;
    Rig rig(false,32,32,1);rig.comp()->set_transparent_background(true);const u64 sourceId=rig.shape(.5f);
    AUREA_CHECK(rig.start(1).ok());AUREA_CHECK(rig.wait().complete);
    const auto directory=rig.cache();AUREA_CHECK(!directory.empty());if(directory.empty())return;
    const auto cachedPixels=frame(directory,0,540,540);if(cachedPixels.empty())return;
    Scene scene(32,32);scene.comp->set_transparent_background(true);
    const auto id=scene.comp->add_layer(LayerKind::Shape,"own half alpha card");auto* shape=scene.comp->layer(id);
    // Reuse the whole authored shape specification, including preset fields,
    // rather than silently comparing a different set of default parameters.
    shape->shape=rig.comp()->layer(LayerId::unpack(sourceId))->shape;
    shape->transform=rig.comp()->layer(LayerId::unpack(sourceId))->transform;
    auto& device=gpu().backend;auto& renderer=gpu().renderer;
    TextureDesc composition;composition.width=composition.height=540;composition.format=SurfaceFormat::RGBA16F;
    composition.renderTarget=composition.sampled=composition.transferSrc=composition.transferDst=true;
    const auto work=device.create_texture(composition),cache=device.create_texture(composition);AUREA_CHECK(work.ok()&&cache.ok());if(!work.ok()||!cache.ok())return;
    TextureDesc displayDesc=composition;displayDesc.width=displayDesc.height=64;
    const auto output=device.create_texture(displayDesc);AUREA_CHECK(output.ok());if(!output.ok())return;
    OffscreenTarget target;target.texture=*work;target.width=target.height=540;
    target.display=*output;target.displayWidth=target.displayHeight=64;
    for(const f32 zoom:{1.f,1.7f}) {
        // Match the offline composition contract (finalQuality, native source
        // raster factor, 540p target). Increasing previewFactor here would
        // compare differently rasterized ellipse edges with the cached image.
        RenderSettings settings;settings.dither=false;settings.finalQuality=true;
        settings.viewportZoom=zoom;settings.viewportPan=Vec2{5,-3};
        FrameSnapshot snapshot;renderer.prepare(*scene.comp,scene.project,FrameIndex{0},nullptr,nullptr,nullptr,settings,1,0,DecodeMode::Still,1,snapshot);
        FrameStats stats;RenderTimings timings;AUREA_CHECK(renderer.render(snapshot,settings,&target,stats,timings).ok());device.wait_idle();
        std::vector<u16> expected(64*64*4);AUREA_CHECK(device.read_texture(*output,expected.data(),64*8).ok());
        snapshot=FrameSnapshot{};snapshot.compWidth=snapshot.compHeight=32;snapshot.background=Vec4{0,0,0,0};
        settings.cachedComposition=*cache;settings.cachedCompositionPixels=cachedPixels.data();
        AUREA_CHECK(renderer.render(snapshot,settings,&target,stats,timings).ok());AUREA_CHECK_EQ(stats.passesExecuted,1u);device.wait_idle();
        std::vector<u16> actual(64*64*4);AUREA_CHECK(device.read_texture(*output,actual.data(),64*8).ok());
        f32 maximum=0;usize maximumAt=0;u32 differences=0;
        for(usize n=0;n<actual.size();++n){const f32 delta=std::abs(half_to_float(actual[n])-half_to_float(expected[n]));
            differences+=delta>.005f;if(delta>maximum){maximum=delta;maximumAt=n;}}
        std::printf("    own cached output zoom=%.1f pan=5,-3 checker maxDelta=%.6f passes=%u changed=%u at=%u,%u channel=%u direct=%.6f cache=%.6f\n",zoom,maximum,stats.passesExecuted,differences,
            u32(maximumAt/4%64),u32(maximumAt/4/64),u32(maximumAt%4),half_to_float(expected[maximumAt]),half_to_float(actual[maximumAt]));
        AUREA_CHECK(maximum<.005f);
        // Half alpha keeps the native checker under red. A green-channel
        // contrast across two checker cells proves it was not baked away.
        const f32 a=half_to_float(actual[(24*64+24)*4+1]),b=half_to_float(actual[(24*64+30)*4+1]);
        AUREA_CHECK(std::abs(a-b)>.01f);
    }
    device.destroy_texture(*output);device.destroy_texture(*cache);device.destroy_texture(*work);
}
#endif
