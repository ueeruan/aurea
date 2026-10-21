#include "aurea/Engine.hpp"
#include "aurea/project/Psd.hpp"
#include "aurea/project/FileIO.hpp"
#include "stb_image.h"
#include <algorithm>
#include <limits>
#include <array>
#include <filesystem>
#include <new>
#include <type_traits>
#include "aurea/core/Time.hpp"
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace aurea {
namespace {
thread_local i32 importFailureAfter = -1;
void allocation_checkpoint() {
    if (importFailureAfter < 0) return;
    if (importFailureAfter-- == 0) { importFailureAfter = -1; throw std::bad_alloc{}; }
}

// Write RGBA PNG in bounded chunks. Stored deflate preserves every pixel and
// needs only 64 KiB of scratch even for a large Photoshop layer.
bool persist_png(const std::string& path,const psd::Layer& layer) {
    auto* file=fileio::open_file(path,"wb"); if(!file)return false;
    std::unique_ptr<FILE, decltype(&std::fclose)> fileGuard(file, &std::fclose);
    allocation_checkpoint();
    bool ok=true;
    auto write=[&](const void* data,usize n){if(ok&&std::fwrite(data,1,n,file)!=n)ok=false;};
    auto word=[&](u32 n){const u8 b[]={u8(n>>24),u8(n>>16),u8(n>>8),u8(n)};write(b,4);};
    static const auto table=[] {std::array<u32,256> t{};for(u32 i=0;i<256;++i){u32 c=i;for(int j=0;j<8;++j)c=(c>>1)^((c&1)?0xedb88320u:0);t[i]=c;}return t;}();
    auto chunk=[&](const char* type,const std::vector<u8>& data){
        word(static_cast<u32>(data.size()));write(type,4);write(data.data(),data.size());u32 crc=0xffffffffu;
        for(u32 i=0;i<4;++i)crc=table[(crc^u8(type[i]))&255]^(crc>>8);
        for(u8 b:data)crc=table[(crc^b)&255]^(crc>>8);word(crc^0xffffffffu);
    };
    const u8 signature[]={137,80,78,71,13,10,26,10};write(signature,8);
    std::vector<u8> header;
    for(u32 n:{layer.width,layer.height})for(int s=24;s>=0;s-=8)header.push_back(u8(n>>s));
    header.insert(header.end(),{8,6,0,0,0});chunk("IHDR",header);
    const usize row=static_cast<usize>(layer.width)*4+1,total=row*layer.height;u32 a=1,b=0;
    std::vector<u8> data;data.reserve(65546);
    for(usize pos=0;pos<total;){
        const usize n=std::min<usize>(65535,total-pos);const bool last=pos+n==total;data.clear();
        if(pos==0)data.insert(data.end(),{0x78,0x01});
        data.insert(data.end(),{u8(last?1:0),u8(n),u8(n>>8),u8(~n),u8((~n)>>8)});
        for(usize i=0;i<n;++i){const usize at=pos+i,x=at%row;const u8 v=x?layer.rgba[(at/row)*(row-1)+x-1]:0;data.push_back(v);a=(a+v)%65521;b=(b+a)%65521;}
        if(last){const u32 adler=(b<<16)|a;for(int s=24;s>=0;s-=8)data.push_back(u8(adler>>s));}
        chunk("IDAT",data);pos+=n;
    }
    chunk("IEND",{});if(std::fflush(file)!=0)ok=false;
#if defined(_WIN32)
    if(_commit(_fileno(file))!=0)ok=false;
#else
    if(::fsync(fileno(file))!=0)ok=false;
#endif
    if(std::fclose(fileGuard.release())!=0)ok=false;
    return ok;
}
}
// Scoped, thread-local fault injection for transaction regressions; no allocator
// replacement and no cross-thread failures. Not exposed by either mobile bridge.
namespace psd { void set_import_failure_after_for_testing(i32 after) noexcept { importFailureAfter = after; } }
Result<u64> Engine::import_psd(const std::string& path, const char* name, bool* approximated) noexcept try {
    std::lock_guard<std::recursive_mutex> admission(sourceImportMutex_);
    u64 room = 0, session = 0;
    CompositionId composition{};
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_ || !current_composition()) return Status{Errc::InvalidState};
        session = projectSession_;
        composition = project_->timeline().current();
        room = source_asset_room_locked();
    }
    // Keep file/metadata and decoded layers/scratch within the remaining
    // project quota. Previously a PSD could add 128 MiB on top of all assets.
    if (room < 2) return Status{Errc::BudgetExceeded};
    std::vector<u8> bytes;
    const usize fileLimit = static_cast<usize>(std::min<u64>(psd::kMaxFileBytes, room / 2));
    std::error_code sizeError;
    const auto fileBytes = std::filesystem::file_size(std::filesystem::u8path(path), sizeError);
    if (!sizeError && fileBytes > fileLimit) return Status{Errc::BudgetExceeded};
    if (!fileio::read_all(path, bytes, fileLimit)) return Status{Errc::IoError};
    psd::Document doc;
    if (auto s = psd::read(bytes, doc, static_cast<usize>(std::min<u64>(psd::kMaxDecodedBytes, room / 2))); !s.ok()) return s;
    bytes.clear(); bytes.shrink_to_fit();
    namespace fs=std::filesystem;std::error_code error;
    const fs::path base=config_.documentsDirectory.empty()?fs::temp_directory_path(error):fs::u8path(config_.documentsDirectory)/"Media";
    if(error)return Status{Errc::IoError};
    const auto folder=base/("PSD-"+std::to_string(monotonic_ns()));
    if (!fs::create_directories(folder,error) || error) return Status{Errc::IoError};
    struct FolderGuard {
        const fs::path& path;
        bool keep = false;
        ~FolderGuard() noexcept {
            if (!keep) try { std::error_code ignored; fs::remove_all(path, ignored); } catch (...) {}
        }
    } folderGuard{folder};
    std::vector<std::string> sources(doc.layers.size());
    for(usize i=0;i<doc.layers.size();++i)if(!doc.layers[i].group){
        const auto utf=(folder/(std::to_string(i)+".png")).u8string();sources[i]={utf.begin(),utf.end()};
        if(!persist_png(sources[i],doc.layers[i])) return Status{Errc::IoError};
        allocation_checkpoint();
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* parent = project_ ? current_composition() : nullptr;
    if (!parent) return Status{Errc::InvalidState};
    if (session != projectSession_ || composition != project_->timeline().current()) {
        return Status{Errc::Cancelled};
    }
    u64 decoded = 0;
    for (const auto& layer : doc.layers) decoded += layer.rgba.capacity();
    const u64 available = source_asset_room_locked();
    if (decoded > available || History::estimate_bytes(*parent) > (available - decoded) / 2)
        return Status{Errc::BudgetExceeded}; // staged parent plus its undo snapshot coexist
    std::vector<u32> depths(doc.layers.size());u32 groupCount=0,maxDepth=0;
    for(usize i=0;i<doc.layers.size();++i){const auto& layer=doc.layers[i];
        depths[i]=(layer.parent<0?0:depths[static_cast<usize>(layer.parent)])+(layer.group?1u:0u);
        maxDepth=std::max(maxDepth,depths[i]);if(layer.group)++groupCount;
    }
    if(parent->layers().count()>=kMaxLayerCount || project_->timeline().composition_count()+groupCount+1>256
       || parent->nesting_depth()+maxDepth+1>kMaxNestingDepth) {
        return Status{Errc::BudgetExceeded};
    }
    // Stage the visible parent separately; its final move cannot allocate.
    auto stagedParent = parent->clone();
    parent = stagedParent.get();
    auto& timeline = project_->timeline();
    std::vector<CompositionId> createdCompositions; createdCompositions.reserve(groupCount + 1);
    std::vector<AssetId> createdAssets; createdAssets.reserve(doc.layers.size() - groupCount);
    const bool dirtyBefore = project_->autosave().dirty;
    const auto commandsBefore = project_->autosave().commandsSinceRecovery;
    const auto generationBefore = project_->autosave().editGeneration;
    auto rollback = [&]() noexcept {
        // Recover staging memory first: removing a Composition constructs its
        // empty SlotTable, so it needs a small amount of allocation headroom.
        stagedParent.reset(); doc.layers.clear();
        for (AssetId asset : createdAssets) { images_.erase(asset.pack()); project_->remove_asset(asset); }
        for (auto i = createdCompositions.rbegin(); i != createdCompositions.rend(); ++i) timeline.remove_composition(*i);
        project_->autosave().dirty = dirtyBefore;
        project_->autosave().commandsSinceRecovery = commandsBefore;
        project_->autosave().editGeneration = generationBefore;
    };
    struct StagingGuard { decltype(rollback)& cleanup; bool committed = false; ~StagingGuard() noexcept { if (!committed) cleanup(); } } stagingGuard{rollback};
    const std::string title = name && *name ? name : "PSD";
    const auto root = timeline.create_composition(title, doc.width, doc.height, parent->fps());
    if (!root.valid()) return Status{Errc::OutOfMemory};
    createdCompositions.push_back(root);
    allocation_checkpoint();
    const auto duration = parent->duration();
    timeline.composition(root)->set_duration(duration);
    timeline.composition(root)->set_nesting_depth(parent->nesting_depth()+1);
    timeline.composition(root)->set_transparent_background(true);
    std::vector<CompositionId> groups(doc.layers.size());
    for (usize i=0;i<doc.layers.size();++i) if (doc.layers[i].group) {
        groups[i] = timeline.create_composition(doc.layers[i].name, doc.width, doc.height, parent->fps());
        if (!groups[i].valid()) return Status{Errc::OutOfMemory};
        createdCompositions.push_back(groups[i]);
        allocation_checkpoint();
        timeline.composition(groups[i])->set_duration(duration);
        timeline.composition(groups[i])->set_nesting_depth(parent->nesting_depth()+depths[i]+1);
        timeline.composition(groups[i])->set_transparent_background(true);
    }
    for (usize i=doc.layers.size();i-- > 0;) {
        auto& src=doc.layers[i];
        auto* comp=timeline.composition(src.parent<0 ? root : groups[static_cast<usize>(src.parent)]);
        const auto id=comp->add_layer(src.group ? LayerKind::Composition : LayerKind::Image,src.name);
        auto* l=comp->layer(id); if (!l) return Status{Errc::OutOfMemory}; l->start=FrameIndex{0}; l->end=duration;
        l->visible=src.visible; l->blendMode=src.blend; l->transform.opacity=src.opacity;
        if (src.group) {
            l->nested.composition=groups[i]; l->transform.position={0,0,0}; l->transform.anchor={0,0,0};
        } else {
            Asset asset; asset.kind=AssetKind::Image; asset.name=src.name; asset.originalFilename=src.name+".png";
            asset.sourcePath=store_asset_path(sources[i]);
            asset.video.width=src.width; asset.video.height=src.height;
            l->source=project_->add_asset(std::move(asset));
            createdAssets.push_back(l->source);
            ImagePixels pixels; pixels.width=src.width; pixels.height=src.height; pixels.rgba=std::move(src.rgba);
            images_[l->source.pack()]=std::make_shared<const ImagePixels>(std::move(pixels));
            allocation_checkpoint();
            l->transform.anchor={src.width*.5f,src.height*.5f,0};
            l->transform.position={src.left+src.width*.5f,src.top+src.height*.5f,0};
        }
    }
    const auto id=parent->add_layer(LayerKind::Composition,title);
    auto* layer=parent->layer(id); if (!layer) return Status{Errc::OutOfMemory}; layer->nested.composition=root;
    layer->start=FrameIndex{std::clamp<i64>(playback_.current().value,0,std::max<i64>(0,duration.value-1))}; layer->end=duration;
    layer->transform.anchor={doc.width*.5f,doc.height*.5f,0};
    layer->transform.position={parent->width()*.5f,parent->height()*.5f,0};
    const f32 fit=std::min({1.f,static_cast<f32>(parent->width())/doc.width,static_cast<f32>(parent->height())/doc.height});
    layer->transform.scale={fit,fit,1};
    allocation_checkpoint();
    // Creating child compositions may grow the slot table and invalidate old
    // pointers. Reacquire the original only after all staging has finished.
    auto* original = timeline.composition(composition);
    if (!original) return Status{Errc::Cancelled};
    history_.before_mutation(*original, composition, "importar PSD");
    static_assert(std::is_nothrow_move_assignable_v<Composition>);
    *original = std::move(*stagedParent);
    stagingGuard.committed = true; folderGuard.keep = true;
    if (approximated) *approximated=doc.approximated;
    bump_model_revision_locked(); project_->mark_dirty(); request_render();
    return id.pack();
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
}

// GIF animado (0.2.7, "o GIF fica parado como imagem"): os quadros já
// compostos pelo stb, um trecho de camada de imagem por quadro distinto numa
// pré-composição, e o remapeamento de tempo repetindo até o fim da composição.
Result<u64> Engine::import_gif(const std::string& path, const char* name) noexcept try {
    std::lock_guard<std::recursive_mutex> admission(sourceImportMutex_);
    u64 room = 0, session = 0;
    CompositionId composition{};
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        if (!project_ || !current_composition()) return Status{Errc::InvalidState};
        session = projectSession_;
        composition = project_->timeline().current();
        room = source_asset_room_locked();
    }
    if (room < 2) return Status{Errc::BudgetExceeded};
    constexpr usize kMaxGifFile = 64u << 20;
    constexpr int kMaxGifFrames = 600;
    std::vector<u8> bytes;
    const usize fileLimit = static_cast<usize>(std::min<u64>(kMaxGifFile, room / 2));
    if (!fileio::read_all(path, bytes, fileLimit)) return Status{Errc::IoError};
    if (bytes.size() < 6 || bytes.size() > static_cast<usize>(std::numeric_limits<int>::max())) return Status{Errc::UnsupportedFormat};
    int w = 0, h = 0, frames = 0, channels = 0;
    int* delays = nullptr;
    stbi_uc* pixels = stbi_load_gif_from_memory(bytes.data(), static_cast<int>(bytes.size()), &delays, &w, &h, &frames, &channels, 4);
    struct StbGuard {
        stbi_uc* p; int* d;
        ~StbGuard() { if (p) stbi_image_free(p); if (d) stbi_image_free(d); }
    } stbGuard{pixels, delays};
    bytes.clear(); bytes.shrink_to_fit();
    if (!pixels || w <= 0 || h <= 0 || frames <= 0) return Status{Errc::UnsupportedFormat};
    if (w > 8192 || h > 8192 || frames > kMaxGifFrames) return Status{Errc::BudgetExceeded};
    const u64 frameBytes = static_cast<u64>(w) * static_cast<u64>(h) * 4u;
    if (frameBytes * static_cast<u64>(frames) > room / 2) return Status{Errc::BudgetExceeded};
    namespace fs = std::filesystem; std::error_code error;
    const fs::path base = config_.documentsDirectory.empty() ? fs::temp_directory_path(error) : fs::u8path(config_.documentsDirectory) / "Media";
    if (error) return Status{Errc::IoError};
    const auto folder = base / ("GIF-" + std::to_string(monotonic_ns()));
    if (!fs::create_directories(folder, error) || error) return Status{Errc::IoError};
    struct FolderGuard {
        const fs::path& path; bool keep = false;
        ~FolderGuard() noexcept { if (!keep) try { std::error_code ignored; fs::remove_all(path, ignored); } catch (...) {} }
    } folderGuard{folder};
    // Quadro idêntico ao anterior (a "pausa" do GIF) reaproveita a mesma imagem.
    std::vector<psd::Layer> images; images.reserve(static_cast<usize>(frames));
    std::vector<u32> imageOf(static_cast<usize>(frames));
    std::vector<std::string> sources;
    for (int f = 0; f < frames; ++f) {
        const stbi_uc* frame = pixels + frameBytes * static_cast<u64>(f);
        if (!images.empty() && std::equal(frame, frame + frameBytes, images.back().rgba.begin())) {
            imageOf[static_cast<usize>(f)] = static_cast<u32>(images.size() - 1);
            continue;
        }
        psd::Layer image; image.width = static_cast<u32>(w); image.height = static_cast<u32>(h);
        image.name = "Quadro " + std::to_string(f + 1);
        image.rgba.assign(frame, frame + frameBytes);
        const auto utf = (folder / (std::to_string(images.size()) + ".png")).u8string();
        sources.emplace_back(utf.begin(), utf.end());
        if (!persist_png(sources.back(), image)) return Status{Errc::IoError};
        imageOf[static_cast<usize>(f)] = static_cast<u32>(images.size());
        images.push_back(std::move(image));
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* parent = project_ ? current_composition() : nullptr;
    if (!parent) return Status{Errc::InvalidState};
    if (session != projectSession_ || composition != project_->timeline().current()) return Status{Errc::Cancelled};
    u64 decoded = 0;
    for (const auto& image : images) decoded += image.rgba.capacity();
    const u64 available = source_asset_room_locked();
    if (decoded > available || History::estimate_bytes(*parent) > (available - decoded) / 2) return Status{Errc::BudgetExceeded};
    if (parent->layers().count() >= kMaxLayerCount || project_->timeline().composition_count() + 2 > 256
        || parent->nesting_depth() + 2 > kMaxNestingDepth) return Status{Errc::BudgetExceeded};
    // Tempo de cada quadro na taxa da composição. Duração abaixo de 20 ms (ou
    // 0): os navegadores tocam a 100 ms; o mesmo aqui, senão o GIF "voa".
    const f64 fps = parent->fps() > 0.0 ? parent->fps() : 30.0;
    std::vector<i64> starts(static_cast<usize>(frames) + 1, 0);
    f64 ms = 0.0;
    for (int f = 0; f < frames; ++f) {
        starts[static_cast<usize>(f)] = static_cast<i64>(std::llround(ms * fps / 1000.0));
        const int d = delays ? delays[f] : 100;
        ms += d < 20 ? 100.0 : static_cast<f64>(d);
    }
    starts[static_cast<usize>(frames)] = std::max<i64>(starts[static_cast<usize>(frames) - 1] + 1,
                                                       static_cast<i64>(std::llround(ms * fps / 1000.0)));
    const i64 loopFrames = std::max<i64>(1, starts[static_cast<usize>(frames)]);
    auto stagedParent = parent->clone();
    parent = stagedParent.get();
    auto& timeline = project_->timeline();
    std::vector<CompositionId> createdCompositions;
    std::vector<AssetId> createdAssets; createdAssets.reserve(images.size());
    const bool dirtyBefore = project_->autosave().dirty;
    const auto commandsBefore = project_->autosave().commandsSinceRecovery;
    const auto generationBefore = project_->autosave().editGeneration;
    auto rollback = [&]() noexcept {
        stagedParent.reset(); images.clear();
        for (AssetId asset : createdAssets) { images_.erase(asset.pack()); project_->remove_asset(asset); }
        for (auto i = createdCompositions.rbegin(); i != createdCompositions.rend(); ++i) timeline.remove_composition(*i);
        project_->autosave().dirty = dirtyBefore;
        project_->autosave().commandsSinceRecovery = commandsBefore;
        project_->autosave().editGeneration = generationBefore;
    };
    struct StagingGuard {
        decltype(rollback)& cleanup; bool committed = false;
        ~StagingGuard() noexcept { if (!committed) cleanup(); }
    } stagingGuard{rollback};
    const std::string title = name && *name ? name : "GIF";
    const auto root = timeline.create_composition(title, static_cast<u32>(w), static_cast<u32>(h), fps);
    if (!root.valid()) return Status{Errc::OutOfMemory};
    createdCompositions.push_back(root);
    auto* gifComp = timeline.composition(root);
    gifComp->set_duration(FrameIndex{loopFrames});
    gifComp->set_nesting_depth(parent->nesting_depth() + 1);
    gifComp->set_transparent_background(true);
    std::vector<AssetId> assets(images.size());
    for (usize i = 0; i < images.size(); ++i) {
        Asset asset; asset.kind = AssetKind::Image; asset.name = images[i].name; asset.originalFilename = images[i].name + ".png";
        asset.sourcePath = store_asset_path(sources[i]);
        asset.video.width = images[i].width; asset.video.height = images[i].height;
        assets[i] = project_->add_asset(std::move(asset));
        createdAssets.push_back(assets[i]);
        ImagePixels px; px.width = images[i].width; px.height = images[i].height; px.rgba = std::move(images[i].rgba);
        images_[assets[i].pack()] = std::make_shared<const ImagePixels>(std::move(px));
    }
    // Um trecho por imagem distinta: quadros repetidos estendem o anterior.
    for (int f = 0; f < frames;) {
        int g = f + 1;
        while (g < frames && imageOf[static_cast<usize>(g)] == imageOf[static_cast<usize>(f)]) ++g;
        const i64 a = starts[static_cast<usize>(f)];
        const i64 b = std::max(a + 1, starts[static_cast<usize>(g)]);
        const auto layerId = gifComp->add_layer(LayerKind::Image, "Quadro " + std::to_string(f + 1));
        auto* l = gifComp->layer(layerId); if (!l) return Status{Errc::OutOfMemory};
        l->start = FrameIndex{a}; l->end = FrameIndex{b};
        l->source = assets[imageOf[static_cast<usize>(f)]];
        l->transform.anchor = {w * .5f, h * .5f, 0}; l->transform.position = {w * .5f, h * .5f, 0};
        f = g;
    }
    const auto id = parent->add_layer(LayerKind::Composition, title);
    auto* layer = parent->layer(id); if (!layer) return Status{Errc::OutOfMemory};
    layer->nested.composition = root;
    const i64 begin = std::clamp<i64>(playback_.current().value, 0, std::max<i64>(0, parent->duration().value - 1));
    // Loop até o fim da composição (no mínimo uma volta), pelo remapeamento:
    // em cada volta o quadro da fonte anda de 0 a loop−1 e salta para 0.
    const i64 span = std::max(loopFrames, parent->duration().value - begin);
    const i64 loops = std::min<i64>(200, (span + loopFrames - 1) / loopFrames);
    layer->start = FrameIndex{begin};
    layer->end = FrameIndex{begin + std::max<i64>(1, std::min(span, loops * loopFrames))};
    if (loops > 1 && frames > 1 && loopFrames > 1) {
        layer->timeRemap.property = TrackProperty::TimeRemap;
        for (i64 k = 0; k < loops; ++k) {
            const i64 t0 = k * loopFrames;
            const FrameIndex first = layer->local_time(FrameIndex{begin + t0});
            const FrameIndex last = layer->local_time(FrameIndex{begin + t0 + loopFrames - 1});
            layer->timeRemap.set(first, 0.0f);
            layer->timeRemap.set(last, static_cast<f32>(loopFrames - 1));
            layer->timeRemap.set_interpolation(last, Interpolation::Hold, 0.f, 0.f, 1.f, 1.f);
        }
        layer->timeRemapEnabled = true;
    }
    layer->transform.anchor = {w * .5f, h * .5f, 0};
    layer->transform.position = {parent->width() * .5f, parent->height() * .5f, 0};
    const f32 fit = std::min({1.f, static_cast<f32>(parent->width()) / static_cast<f32>(w),
                              static_cast<f32>(parent->height()) / static_cast<f32>(h)});
    layer->transform.scale = {fit, fit, 1};
    auto* original = timeline.composition(composition);
    if (!original) return Status{Errc::Cancelled};
    history_.before_mutation(*original, composition, "importar GIF");
    *original = std::move(*stagedParent);
    stagingGuard.committed = true; folderGuard.keep = true;
    bump_model_revision_locked(); project_->mark_dirty(); request_render();
    return id.pack();
} catch (const std::bad_alloc&) {
    return Status{Errc::OutOfMemory};
}
}
