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

AUREA_TEST(PreRenderIntegration, GroupCacheIsUsedInsideItsLiveParentAndEditsInvalidateIt) {
    using namespace prerender_integration;
    Rig rig(true);
    const u64 source = rig.shape(.5f);
    const auto group = rig.engine.precompose(&source, 1, "cached child");
    AUREA_CHECK(group.ok());
    if (!group.ok()) return;
    const CompositionId parent = rig.engine.project()->timeline().current();
    rig.shape(.25f);
    AUREA_CHECK(rig.engine.start_prerender(1, *group).ok());
    const auto done = rig.wait();
    AUREA_CHECK(done.complete && done.error == 0);
    AUREA_CHECK(rig.engine.project()->timeline().current() == parent);
    std::atomic<u32> uploads{0};
    u64 cachedTexture = 0;
    rig.mock->beforeTextureUpload = [&](TextureHandle texture, const void*, u32) {
        if (is_cache_texture(rig.mock->texture_desc(texture))) { ++uploads; cachedTexture = texture.id; }
        return OkStatus;
    };
    int window = 0;
    AUREA_CHECK(rig.engine.attach_surface(&window, 32, 32).ok());
    for (u32 tick = 0; tick < 200 && !uploads.load(); ++tick) {
        rig.mock->events.clear();
        AUREA_CHECK(rig.engine.render_frame(false).ok());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    AUREA_CHECK(uploads.load() > 0);
    AUREA_CHECK(std::any_of(rig.mock->events.begin(), rig.mock->events.end(), [&](const auto& event) {
        return event.kind == test::MockBackend::Event::BindTexture && event.texture == cachedTexture;
    }));
    const auto draws = std::count_if(rig.mock->events.begin(), rig.mock->events.end(), [](const auto& event) {
        return event.kind == test::MockBackend::Event::Draw;
    });
    AUREA_CHECK(draws >= 3); // cached child, live sibling and normal output
    AUREA_CHECK(rig.engine.query_prerender_progress().complete);
    AUREA_CHECK(rig.engine.add_shape(0).ok());
    AUREA_CHECK(!rig.engine.query_prerender_progress().complete);
}

AUREA_TEST(PreRenderIntegration, CachedGroupKeepsParentTransformsMasksAndEffectsOnGpu) {
    AUREA_REQUIRE_GPU();
    Scene scene(64, 48);
    scene.comp->set_transparent_background(true);
    auto& timeline = scene.project.timeline();
    const CompositionId childId = timeline.create_composition("red cached group", 32, 24, 30);
    timeline.composition(childId)->set_background(Color{1, 0, 0, 1});
    scene.solid(18, 16, Vec4{0, 1, 0, 1}, 18, 19);
    const LayerId groupId = scene.comp->add_layer(LayerKind::Composition, "live group transform");
    Layer* group = scene.comp->layer(groupId);
    group->nested.composition = childId;
    group->transform.anchor = Vec3{16, 12, 0};
    group->transform.position = Vec3{38, 28, 0};
    group->transform.rotation.z = 23;
    group->transform.opacity = .7f;
    group->masks.push_back(rect_mask(1, 4, 2, 28, 22));
    scene.add_effect(groupId, effect_keys::kGlow);
    auto& device = gpu().backend;
    auto& renderer = gpu().renderer;
    TextureDesc desc;
    desc.width = 8; desc.height = 6; desc.format = SurfaceFormat::RGBA16F;
    desc.sampled = true; desc.transferDst = true;
    const auto texture = device.create_texture(desc);
    AUREA_CHECK(texture.ok());
    if (!texture.ok()) return;
    struct Lookup {
        CompositionId composition;
        TextureHandle texture;
        std::vector<u16> pixels;
        u32 calls = 0;
    } cache{childId, *texture, std::vector<u16>(8 * 6 * 4)};
    for (usize pixel = 0; pixel < cache.pixels.size(); pixel += 4) {
        cache.pixels[pixel] = cache.pixels[pixel + 3] = 0x3c00;
    }
    const FloatImage expected = scene.render();
    RenderSettings settings;
    settings.dither = false;
    settings.nestedCacheContext = &cache;
    settings.nestedCacheLookup = [](void* context, CompositionId composition, FrameIndex) -> CachedCompositionFrame {
        auto& lookup = *static_cast<Lookup*>(context);
        if (composition != lookup.composition) return {};
        ++lookup.calls;
        return {lookup.texture, lookup.pixels.data()};
    };
    FrameSnapshot snapshot;
    renderer.prepare(*scene.comp, scene.project, FrameIndex{0}, nullptr, nullptr, nullptr,
        settings, 1, 1, DecodeMode::Still, 1, snapshot);
    AUREA_CHECK_EQ(cache.calls, 1u);
    AUREA_CHECK_EQ(snapshot.nested.size(), 1u);
    AUREA_CHECK(snapshot.nested[0]->layers.empty());
    OffscreenTarget target{gpu().target(64, 48), 64, 48};
    FrameStats stats;
    RenderTimings timings;
    AUREA_CHECK(renderer.render(snapshot, settings, &target, stats, timings).ok());
    device.wait_idle();
    std::vector<u16> actual(64 * 48 * 4);
    AUREA_CHECK(device.read_texture(target.texture, actual.data(), 64 * 8).ok());
    f32 maximum = 0;
    for (usize channel = 0; channel < actual.size(); ++channel)
        maximum = std::max(maximum, std::fabs(half_to_float(actual[channel]) - expected.px[channel]));
    std::printf("    cached child 8x6 in live parent 64x48 maxDelta=%.6f\n", maximum);
    AUREA_CHECK(maximum < .005f);
    // Final captures always evaluate the authored child at output quality.
    settings.finalQuality = true;
    cache.calls = 0;
    renderer.prepare(*scene.comp, scene.project, FrameIndex{0}, nullptr, nullptr, nullptr,
        settings, 2, 1, DecodeMode::Still, 1, snapshot);
    AUREA_CHECK_EQ(cache.calls, 0u);
    device.destroy_texture(*texture);
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

// O dono: "o PreRender não está funcionando". Parado, a prévia mostrava o
// cache (os testes acima); TOCANDO, cada quadro que o leitor ainda não tinha
// descomprimido caía na composição inteira ao vivo. Um quadro 540p custa mais
// que 33 ms para ler (ReadLatencyAt540pIsMeasured), então tocar nunca saía
// do render ao vivo. Conta: apresentações sem upload do cache = render ao vivo.
AUREA_TEST(PreRenderIntegration, PlaybackFromCompletedCacheNeverFallsBackToLiveComposition) {
    using namespace prerender_integration;
    Rig rig(true,960,540,60); rig.shape(.5f);
    AUREA_CHECK(rig.start(1).ok()); const auto done=rig.wait(); AUREA_CHECK(done.complete && done.error==0);
    std::atomic<u32> uploads{0};
    rig.mock->beforeTextureUpload=[&](TextureHandle texture,const void*,u32){
        if(is_cache_texture(rig.mock->texture_desc(texture)))++uploads;return OkStatus;};
    int window=0; AUREA_CHECK(rig.engine.attach_surface(&window,960,540).ok());
    for(u32 tick=0;tick<200 && !uploads.load();++tick){(void)rig.engine.render_frame(false);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    AUREA_CHECK(uploads.load()>0);
    const u32 presentsBefore=rig.mock->presents, uploadsBefore=uploads.load();
    // O quadro parado já subiu para uma textura do cache: apresentá-lo de novo
    // sem upload é reaproveitar o slot, não render ao vivo.
    const i64 uploadedWhilePaused=rig.engine.read_status().playhead.value;
    Command play; play.type=CommandType::PlaybackPlay; AUREA_CHECK(rig.engine.apply_command(play).ok());
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);
    u32 live=0;
    while(std::chrono::steady_clock::now()<until){
        const u32 p0=rig.mock->presents, u0=uploads.load();
        (void)rig.engine.render_frame(true);
        const i64 at=rig.engine.read_status().playhead.value;
        if(rig.mock->presents!=p0 && uploads.load()==u0 && at!=uploadedWhilePaused){
            ++live; std::printf("    live composition presented at playhead=%lld\n",static_cast<long long>(at));
        }
        if(!rig.engine.read_status().playing && at>0)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const u32 presents=rig.mock->presents-presentsBefore, cached=uploads.load()-uploadsBefore;
    const i64 reached=rig.engine.read_status().playhead.value;
    std::printf("    own playback from cache: playhead=%lld presents=%u cached=%u live=%u\n",
        static_cast<long long>(reached),presents,cached,live);
    AUREA_CHECK(rig.engine.query_prerender_progress().complete);
    AUREA_CHECK_EQ(live,0u);
    AUREA_CHECK(cached>0);
    // Leitores paralelos com antecipação: a maior parte dos quadros que o
    // relógio percorreu sai do cache (no host; o aparelho mede a dele).
    AUREA_CHECK(i64(cached)*2>=reached);
}

// Segurar a imagem num miss não pode virar congelamento: leitor sem entregar
// por mais de ~250 ms seguidos volta ao render ao vivo; cache invalidado
// volta na hora. O mock diz que as texturas do cache nunca ficam livres
// (wait_frame sem espera = Timeout), então nenhum quadro sai do cache.
AUREA_TEST(PreRenderIntegration, StalledCacheDuringPlaybackFallsBackToLiveInsteadOfFreezing) {
    using namespace prerender_integration;
    Rig rig(true,960,540,300); rig.shape(.5f);
    AUREA_CHECK(rig.start(1).ok()); const auto done=rig.wait(); AUREA_CHECK(done.complete && done.error==0);
    std::atomic<u32> uploads{0};
    rig.mock->beforeTextureUpload=[&](TextureHandle texture,const void*,u32){
        if(is_cache_texture(rig.mock->texture_desc(texture)))++uploads;return OkStatus;};
    int window=0; AUREA_CHECK(rig.engine.attach_surface(&window,960,540).ok());
    for(u32 tick=0;tick<200 && !uploads.load();++tick){(void)rig.engine.render_frame(false);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    AUREA_CHECK(uploads.load()>0);
    Command play; play.type=CommandType::PlaybackPlay; AUREA_CHECK(rig.engine.apply_command(play).ok());
    // Toca do cache até ele entregar alguns quadros.
    const u32 warm=uploads.load();
    for(u32 tick=0;tick<500 && uploads.load()<warm+5;++tick){(void)rig.engine.render_frame(true);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    AUREA_CHECK(uploads.load()>=warm+5);
    std::atomic<bool> stalled{true};
    rig.mock->beforeWaitFrame=[&](u64,u64 timeout){ return stalled.load() && timeout==0 ? Status{Errc::Timeout} : OkStatus; };
    // A espera começa no primeiro miss depois do último quadro do cache. Com
    // a segunda textura ainda não criada, o motor pode entregar UM quadro a
    // mais (cria o slot 1); a contagem parte do último upload.
    auto lastCache=std::chrono::steady_clock::now();
    const auto stallStart=lastCache;
    const u32 uploadsAtStall=uploads.load();
    f64 firstLiveMs=-1; u32 livePresents=0;
    while(std::chrono::steady_clock::now()-stallStart<std::chrono::milliseconds(1200)){
        const u32 p0=rig.mock->presents, u0=uploads.load();
        (void)rig.engine.render_frame(true);
        const auto now=std::chrono::steady_clock::now();
        if(uploads.load()!=u0) lastCache=now;
        else if(rig.mock->presents!=p0){
            ++livePresents;
            if(firstLiveMs<0) firstLiveMs=std::chrono::duration<f64,std::milli>(now-lastCache).count();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("    own stalled cache: first live present %.1f ms after the last cache frame, live presents=%u cache uploads=%u playing=%d\n",
        firstLiveMs,livePresents,uploads.load()-uploadsAtStall,rig.engine.read_status().playing?1:0);
    AUREA_CHECK(uploads.load()-uploadsAtStall<=1);        // no máximo o slot novo; depois nada
    AUREA_CHECK(firstLiveMs>=200);                        // segurou a imagem (não caiu ao vivo de cara)
    AUREA_CHECK(firstLiveMs>=0 && firstLiveMs<600);       // mas não congelou
    AUREA_CHECK(livePresents>5);                          // e seguiu tocando ao vivo
    AUREA_CHECK(rig.engine.query_prerender_progress().complete); // o cache continua válido
    // Leitor volta: o cache entrega de novo.
    stalled=false;
    const u32 recovered=uploads.load();
    for(u32 tick=0;tick<500 && uploads.load()<recovered+3 && rig.engine.read_status().playing;++tick){
        (void)rig.engine.render_frame(true);std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    AUREA_CHECK(uploads.load()>=recovered+3 || !rig.engine.read_status().playing);
    // Cache invalidado tocando: ao vivo no próximo quadro, sem espera.
    rig.engine.cancel_prerender();
    const auto cancelAt=std::chrono::steady_clock::now();
    const u32 p0=rig.mock->presents; f64 afterCancelMs=-1;
    while(std::chrono::steady_clock::now()-cancelAt<std::chrono::milliseconds(300) && rig.engine.read_status().playing){
        (void)rig.engine.render_frame(true);
        if(rig.mock->presents!=p0){afterCancelMs=std::chrono::duration<f64,std::milli>(std::chrono::steady_clock::now()-cancelAt).count();break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::printf("    own cancelled cache while playing: live present after %.1f ms\n",afterCancelMs);
    AUREA_CHECK(afterCancelMs>=0 && afterCancelMs<100);
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

AUREA_TEST(PreRenderIntegration, ImmediateRestartWaitsForSlowCancelledWorkerAndIsNotCancelledByIt) {
    using namespace prerender_integration;
    // Slow phone: one readback outlasts the next gesture. The cancelled worker
    // is still inside its frame when the new PreRender starts (no retry loop,
    // exactly as Android/iOS call it once).
    Rig rig(true, 32, 32, 12); rig.shape(.5f);
    std::atomic<bool> slow{true};
    const auto fill = rig.mock->beforeReadTexture;
    rig.mock->beforeReadTexture = [&slow, fill](TextureHandle texture, void* data, u32 stride) {
        if (slow.load()) std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return fill(texture, data, stride);
    };
    AUREA_CHECK(rig.engine.start_prerender(1).ok());
    for (u32 i = 0; i < 5000 && rig.engine.query_prerender_progress().framesDone < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(rig.engine.query_prerender_progress().running);
    rig.engine.cancel_prerender();
    const Status restarted = rig.engine.start_prerender(-1);
    std::printf("    own slow cancelled worker immediate restart status=%d\n", restarted.raw());
    AUREA_CHECK(restarted.ok());
    slow = false;
    const auto done = rig.wait();
    AUREA_CHECK(done.complete); AUREA_CHECK_EQ(done.error, 0); AUREA_CHECK_EQ(done.direction, -1);
    AUREA_CHECK_EQ(done.framesDone, done.framesTotal);
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
// 0.2.4 (C4): o PreRender e o export dividem o contexto. Exportar com o
// PreRender rodando dava "ja existe um export em andamento" (erro 5).
AUREA_TEST(PreRenderIntegration, ExportCancelsRunningPreRenderAndStarts) {
    using namespace prerender_integration;
    Rig rig(true, 32, 32, 12); rig.shape(.5f);
    std::atomic<bool> slow{true};
    const auto fill = rig.mock->beforeReadTexture;
    rig.mock->beforeReadTexture = [&slow, fill](TextureHandle texture, void* data, u32 stride) {
        if (slow.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return fill(texture, data, stride);
    };
    AUREA_CHECK(rig.engine.start_prerender(1).ok());
    for (u32 i = 0; i < 5000 && rig.engine.query_prerender_progress().framesDone < 1; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    AUREA_CHECK(rig.engine.query_prerender_progress().running);
    ImageExportSettings settings; settings.format = ImageExportFormat::Png;
    const auto out = (rig.root / "export-c4.png").string();
    const Status started = rig.engine.start_image_export(settings, out.c_str());
    std::printf("    export com PreRender rodando status=%d\n", started.raw());
    AUREA_CHECK(started.ok());
    slow = false;
    AUREA_CHECK(!rig.engine.query_prerender_progress().running);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (rig.engine.export_progress().running && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto done = rig.engine.export_progress();
    AUREA_CHECK(done.finished); AUREA_CHECK_EQ(done.result, Errc::Ok);
}

// 0.2.4 (C2): duração inflada não é herdada por camada nova e o export
// recusa acima de 3 h com mensagem clara ("Frame 0 de 522520").
AUREA_TEST(PreRenderIntegration, InflatedDurationIsNotInheritedAndExportAboveThreeHoursIsRefused) {
    using namespace prerender_integration;
    Rig rig(true, 32, 32, 30); rig.shape();
    rig.comp()->set_duration(FrameIndex{522520});
    const auto text = rig.engine.add_text("depois da inflacao");
    AUREA_CHECK(text.ok()); if (!text.ok()) return;
    const Layer* layer = rig.comp()->layer(LayerId::unpack(*text));
    AUREA_CHECK(layer != nullptr); if (!layer) return;
    AUREA_CHECK_EQ(layer->end.value, i64{30});
    AUREA_CHECK_EQ(rig.comp()->export_duration(true).value, i64{30});
    ImageExportSettings whole; whole.format = ImageExportFormat::PngSequence; whole.trimToContent = false;
    const auto dir = (rig.root / "seq-c2").string();
    std::error_code ec; fs::create_directories(dir, ec);
    const Status refused = rig.engine.start_image_export(whole, dir.c_str());
    std::printf("    export de 4,8 h status=%d msg=%.*s\n", refused.raw(),
                static_cast<int>(refused.message().size()), refused.message().data());
    AUREA_CHECK_EQ(refused.code(), Errc::NotSupported);
    AUREA_CHECK(refused.message().find("3 h") != std::string_view::npos || refused.detail().find("3 h") != std::string_view::npos);
}

// Lote K (gap_system item 8): com área de trabalho, o PreRender cobre só ela
// e a prévia lê o cache pelo quadro da composição (índice = quadro − começo).
AUREA_TEST(PreRenderIntegration, WorkAreaLimitsPreRenderAndPreviewReadsItByCompositionFrame) {
    using namespace prerender_integration;
    Rig rig(true, 32, 32, 12); rig.shape(.5f);
    AUREA_CHECK(rig.engine.set_work_area(4, 9));
    AUREA_CHECK(rig.start(1).ok()); const auto done = rig.wait();
    AUREA_CHECK(done.complete && done.error == 0);
    AUREA_CHECK_EQ(done.framesTotal, 5ull); AUREA_CHECK_EQ(done.framesDone, 5ull);
    const auto directory = rig.cache(); AUREA_CHECK(!directory.empty());
    u32 count = 0;
    if (!directory.empty())
        for (const auto& path : fs::directory_iterator(directory)) if (path.path().filename().string().starts_with("frame-")) ++count;
    AUREA_CHECK_EQ(count, 5u);
    std::atomic<u32> uploads{0};
    rig.mock->beforeTextureUpload = [&](TextureHandle texture, const void*, u32) {
        if (is_cache_texture(rig.mock->texture_desc(texture))) ++uploads; return OkStatus; };
    int window = 0; AUREA_CHECK(rig.engine.attach_surface(&window, 32, 32).ok());
    const auto seek_to = [&](i64 f) {
        Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{f}, 30);
        AUREA_CHECK(rig.engine.apply_command(c).ok());
    };
    // Fora da área (quadro 2): ao vivo, nenhum quadro do cache sobe.
    seek_to(2);
    for (u32 tick = 0; tick < 40; ++tick) { (void)rig.engine.render_frame(false); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    AUREA_CHECK_EQ(rig.engine.read_status().playhead.value, 2);
    AUREA_CHECK_EQ(uploads.load(), 0u);
    // Dentro (quadro 6 = índice 2 do cache): o quadro sobe do cache.
    seek_to(6);
    for (u32 tick = 0; tick < 200 && !uploads.load(); ++tick) { (void)rig.engine.render_frame(false); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    AUREA_CHECK(uploads.load() > 0);
    AUREA_CHECK(rig.engine.query_prerender_progress().complete);
    std::printf("    PreRender da área [4,9): %llu quadros no cache, quadro 2 ao vivo, quadro 6 do cache\n",
                static_cast<unsigned long long>(done.framesTotal));
}

// 1.0.2 Pre Render & Pre Export: trecho escolhido no cabeçote, qualidade 1/2,
// e o livro por trecho — editar a camada da segunda metade só desatualiza os
// trechos dela (a primeira continua tocando do cache); editar a outra também
// esgota o cache e ele sai.
AUREA_TEST(PreRenderIntegration, RangePanelPreRendersTheChosenPartAndEditsOnlyStaleTheirSegments) {
    using namespace prerender_integration;
    Rig rig(true, 64, 64, 48);
    const u64 a = rig.shape(.5f), b = rig.shape(.5f);
    rig.comp()->layer(LayerId::unpack(a))->start = FrameIndex{0};
    rig.comp()->layer(LayerId::unpack(a))->end = FrameIndex{24};
    rig.comp()->layer(LayerId::unpack(b))->start = FrameIndex{24};
    rig.comp()->layer(LayerId::unpack(b))->end = FrameIndex{48};
    const auto seek_to = [&](i64 f) {
        Command c; c.type = CommandType::PlaybackSeek; c.seek.time = tick_at(FrameIndex{f}, 30);
        AUREA_CHECK(rig.engine.apply_command(c).ok());
    };
    // Início em 8, Fim em 40 (o quadro do cabeçote entra): [8, 41).
    seek_to(8);
    AUREA_CHECK(rig.engine.prerender_range_action(Engine::kRangeStart));
    auto range = rig.engine.query_prerender_range();
    AUREA_CHECK_EQ(range.in, 8); AUREA_CHECK_EQ(range.out, 48);
    seek_to(40);
    AUREA_CHECK(rig.engine.prerender_range_action(Engine::kRangeEnd));
    range = rig.engine.query_prerender_range();
    AUREA_CHECK_EQ(range.in, 8); AUREA_CHECK_EQ(range.out, 41);
    AUREA_CHECK_EQ(range.effectiveIn, 8); AUREA_CHECK_EQ(range.effectiveOut, 41);
    rig.engine.set_prerender_loop(true);
    AUREA_CHECK(rig.engine.query_prerender_range().loop);
    const auto fit = rig.engine.query_prerender_fit(2, 0);
    AUREA_CHECK_EQ(fit.width, 32u); AUREA_CHECK_EQ(fit.height, 32u);
    AUREA_CHECK_NEAR(fit.rangeSeconds, 33.0 / 30.0, 1e-6);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    Status started;
    for (;;) {
        started = rig.engine.start_prerender_range(2);
        if (started.code() != Errc::Busy || std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    AUREA_CHECK(started.ok());
    const auto done = rig.wait();
    AUREA_CHECK(done.complete && done.error == 0);
    AUREA_CHECK_EQ(done.framesTotal, 33ull);
    AUREA_CHECK_EQ(done.width, 32u); AUREA_CHECK_EQ(done.divisor, 2u);
    AUREA_CHECK_EQ(done.firstFrame, 8); AUREA_CHECK_EQ(done.endFrame, 41);
    AUREA_CHECK_EQ(done.dirtySegments, 0u);
    // Editar a camada b ([24, 48)): só os trechos dela ficam velhos.
    Command edit; edit.type = CommandType::LayerSetOpacity; edit.opacity.layer = LayerId::unpack(b); edit.opacity.opacity = .25f;
    AUREA_CHECK(rig.engine.apply_command(edit).ok());
    auto after = rig.engine.query_prerender_progress();
    AUREA_CHECK(after.complete);
    AUREA_CHECK(after.dirtySegments > 0);
    i64 pairs[16]{};
    const u32 count = rig.engine.query_prerender_dirty(0, pairs, 8);
    AUREA_CHECK(count >= 1);
    if (count >= 1) { AUREA_CHECK(pairs[0] >= 16); AUREA_CHECK_EQ(pairs[count * 2 - 1], 41); }
    std::atomic<u32> uploads{0};
    rig.mock->beforeTextureUpload = [&](TextureHandle texture, const void*, u32) {
        if (is_cache_texture(rig.mock->texture_desc(texture))) ++uploads; return OkStatus; };
    int window = 0; AUREA_CHECK(rig.engine.attach_surface(&window, 64, 64).ok());
    // Quadro 30: trecho velho, ao vivo (nada sobe do cache).
    seek_to(30);
    for (u32 tick = 0; tick < 40; ++tick) { (void)rig.engine.render_frame(false); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    AUREA_CHECK_EQ(uploads.load(), 0u);
    // Quadro 10: trecho limpo, do cache.
    seek_to(10);
    for (u32 tick = 0; tick < 200 && !uploads.load(); ++tick) { (void)rig.engine.render_frame(false); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    AUREA_CHECK(uploads.load() > 0);
    AUREA_CHECK(rig.engine.query_prerender_progress().complete);
    // Editar a camada a ([0, 24)) também: nenhum quadro vale mais, o cache sai.
    edit.opacity.layer = LayerId::unpack(a);
    AUREA_CHECK(rig.engine.apply_command(edit).ok());
    AUREA_CHECK(!rig.engine.query_prerender_progress().complete);
    // Limpar o trecho: volta ao conteúdo inteiro.
    AUREA_CHECK(rig.engine.prerender_range_action(Engine::kRangeClear));
    range = rig.engine.query_prerender_range();
    AUREA_CHECK_EQ(range.in, -1); AUREA_CHECK_EQ(range.effectiveIn, 0); AUREA_CHECK_EQ(range.effectiveOut, 48);
    std::printf("    PreRender do trecho [8,41) a 1/2: %u trecho(s) velho(s) depois de editar b; quadro 10 do cache\n",
                after.dirtySegments);
}

#endif
