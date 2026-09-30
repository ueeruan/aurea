#include "aurea/Engine.hpp"
#include "aurea/project/Psd.hpp"
#include "aurea/project/FileIO.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include "aurea/core/Time.hpp"
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace aurea {
namespace {
// Write RGBA PNG in bounded chunks. Stored deflate preserves every pixel and
// needs only 64 KiB of scratch even for a large Photoshop layer.
bool persist_png(const std::string& path,const psd::Layer& layer) {
    auto* file=fileio::open_file(path,"wb"); if(!file)return false;
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
    if(std::fclose(file)!=0)ok=false;
    return ok;
}
}
Result<u64> Engine::import_psd(const std::string& path, const char* name, bool* approximated) noexcept {
    std::vector<u8> bytes;
    if (!fileio::read_all(path, bytes, psd::kMaxFileBytes)) return Status{Errc::IoError};
    psd::Document doc;
    if (auto s = psd::read(bytes, doc); !s.ok()) return s;
    bytes.clear(); bytes.shrink_to_fit();
    namespace fs=std::filesystem;std::error_code error;
    const fs::path base=config_.documentsDirectory.empty()?fs::temp_directory_path(error):fs::u8path(config_.documentsDirectory)/"Media";
    if(error)return Status{Errc::IoError};
    const auto folder=base/("PSD-"+std::to_string(monotonic_ns()));fs::create_directories(folder,error);
    if(error)return Status{Errc::IoError};
    std::vector<std::string> sources(doc.layers.size());
    auto cleanup=[&]{for(const auto& file:sources)if(!file.empty())fileio::remove_file(file);fs::remove(folder,error);};
    for(usize i=0;i<doc.layers.size();++i)if(!doc.layers[i].group){
        const auto utf=(folder/(std::to_string(i)+".png")).u8string();sources[i]={utf.begin(),utf.end()};
        if(!persist_png(sources[i],doc.layers[i])){cleanup();return Status{Errc::IoError};}
    }
    std::lock_guard<std::mutex> lock(modelMutex_);
    auto* parent = project_ ? current_composition() : nullptr;
    if (!parent) {cleanup();return Status{Errc::InvalidState};}
    std::vector<u32> depths(doc.layers.size());u32 groupCount=0,maxDepth=0;
    for(usize i=0;i<doc.layers.size();++i){const auto& layer=doc.layers[i];
        depths[i]=(layer.parent<0?0:depths[static_cast<usize>(layer.parent)])+(layer.group?1u:0u);
        maxDepth=std::max(maxDepth,depths[i]);if(layer.group)++groupCount;
    }
    if(parent->layers().count()>=kMaxLayerCount || project_->timeline().composition_count()+groupCount+1>256
       || parent->nesting_depth()+maxDepth+1>kMaxNestingDepth) {
        cleanup();return Status{Errc::BudgetExceeded};
    }
    // Parse and validate the entire file before modifying the project.
    history_.before_mutation(*parent, project_->timeline().current(), "importar PSD");
    auto& timeline = project_->timeline();
    const std::string title = name && *name ? name : "PSD";
    const auto root = timeline.create_composition(title, doc.width, doc.height, parent->fps());
    const auto duration = parent->duration();
    timeline.composition(root)->set_duration(duration);
    timeline.composition(root)->set_nesting_depth(parent->nesting_depth()+1);
    timeline.composition(root)->set_transparent_background(true);
    std::vector<CompositionId> groups(doc.layers.size());
    for (usize i=0;i<doc.layers.size();++i) if (doc.layers[i].group) {
        groups[i] = timeline.create_composition(doc.layers[i].name, doc.width, doc.height, parent->fps());
        timeline.composition(groups[i])->set_duration(duration);
        timeline.composition(groups[i])->set_nesting_depth(parent->nesting_depth()+depths[i]+1);
        timeline.composition(groups[i])->set_transparent_background(true);
    }
    for (usize i=doc.layers.size();i-- > 0;) {
        auto& src=doc.layers[i];
        auto* comp=timeline.composition(src.parent<0 ? root : groups[static_cast<usize>(src.parent)]);
        const auto id=comp->add_layer(src.group ? LayerKind::Composition : LayerKind::Image,src.name);
        auto* l=comp->layer(id); l->start=FrameIndex{0}; l->end=duration;
        l->visible=src.visible; l->blendMode=src.blend; l->transform.opacity=src.opacity;
        if (src.group) {
            l->nested.composition=groups[i]; l->transform.position={0,0,0}; l->transform.anchor={0,0,0};
        } else {
            Asset asset; asset.kind=AssetKind::Image; asset.name=src.name; asset.originalFilename=src.name+".png";
            asset.sourcePath=store_asset_path(sources[i]);
            asset.video.width=src.width; asset.video.height=src.height;
            l->source=project_->add_asset(std::move(asset));
            ImagePixels pixels; pixels.width=src.width; pixels.height=src.height; pixels.rgba=std::move(src.rgba);
            images_[l->source.pack()]=std::move(pixels);
            l->transform.anchor={src.width*.5f,src.height*.5f,0};
            l->transform.position={src.left+src.width*.5f,src.top+src.height*.5f,0};
        }
    }
    const auto id=parent->add_layer(LayerKind::Composition,title);
    auto* layer=parent->layer(id); layer->nested.composition=root;
    layer->start=FrameIndex{std::clamp<i64>(playback_.current().value,0,std::max<i64>(0,duration.value-1))}; layer->end=duration;
    layer->transform.anchor={doc.width*.5f,doc.height*.5f,0};
    layer->transform.position={parent->width()*.5f,parent->height()*.5f,0};
    const f32 fit=std::min({1.f,static_cast<f32>(parent->width())/doc.width,static_cast<f32>(parent->height())/doc.height});
    layer->transform.scale={fit,fit,1};
    if (approximated) *approximated=doc.approximated;
    modelRevision_.fetch_add(1,std::memory_order_acq_rel); project_->mark_dirty(); request_render();
    return id.pack();
}
}
