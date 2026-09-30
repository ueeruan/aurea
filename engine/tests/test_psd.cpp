#include "TestFramework.hpp"
#include "aurea/project/Psd.hpp"
#include "aurea/project/FileIO.hpp"
#include "aurea/Engine.hpp"
#include <algorithm>
using namespace aurea;
#define PSD_REQUIRE(expr) do { const bool passed = (expr); AUREA_CHECK(passed); if (!passed) return; } while (false)
namespace {
using Bytes=std::vector<u8>;
void w(Bytes& b,u16 v) { b.push_back(static_cast<u8>(v>>8)); b.push_back(static_cast<u8>(v)); }
void d(Bytes& b,u32 v) { w(b,static_cast<u16>(v>>16)); w(b,static_cast<u16>(v)); }
void tag(Bytes& b,const char* s) { b.insert(b.end(),s,s+4); }
void block(Bytes& b,const Bytes& v) { d(b,static_cast<u32>(v.size())); b.insert(b.end(),v.begin(),v.end()); }
Bytes fixture(bool rle=false) {
    Bytes b; tag(b,"8BPS");w(b,1); b.insert(b.end(),6,0);w(b,4);d(b,4);d(b,8);w(b,8);w(b,3);d(b,0);d(b,0);
    Bytes info, pixels;w(info,2);
    for(u32 i=0;i<2;++i) {
        d(info,1);d(info,i*3);d(info,3);d(info,i*3+2);w(info,4);
        for(i16 c: {i16(0),i16(1),i16(2),i16(-1)}) {
            w(info,static_cast<u16>(c));d(info,rle?12:6);
            w(pixels,rle?1:0); if(rle){w(pixels,3);w(pixels,3);}
            const u8 value=c==-1?u8(128):c==static_cast<i16>(i)?u8(255):u8(0);
            for(u32 y=0;y<2;++y){if(rle)pixels.push_back(1);pixels.push_back(value);pixels.push_back(value);}
        }
        tag(info,"8BIM");tag(info,i?"mul ":"norm");info.push_back(i?128:255);info.push_back(0);info.push_back(i?2:0);info.push_back(0);
        Bytes extra;d(extra,0);d(extra,0);extra.push_back(1);extra.push_back(static_cast<u8>('A'+i));w(extra,0);block(info,extra);
    }
    info.insert(info.end(),pixels.begin(),pixels.end());Bytes lm;block(lm,info);d(lm,0);block(b,lm);return b;
}
}
AUREA_TEST(Psd, RawAndPackbitsPreserveLayers) {
    for(bool rle:{false,true}) {psd::Document doc;PSD_REQUIRE(psd::read(fixture(rle),doc).ok());
        AUREA_CHECK_EQ(doc.width,8u);AUREA_CHECK_EQ(doc.layers.size(),2u);
        AUREA_CHECK_EQ(doc.layers[0].rgba[0],255);AUREA_CHECK_EQ(doc.layers[0].rgba[3],128);
        AUREA_CHECK_EQ(doc.layers[1].left,3);AUREA_CHECK(!doc.layers[1].visible);
        AUREA_CHECK_EQ(doc.layers[1].blend,BlendMode::Multiply);
    }
}
AUREA_TEST(Psd, TruncationNeverReturnsPartialDocument) {
    auto b=fixture(true);
    for(usize n=0;n<b.size();++n) {psd::Document doc;doc.width=777;AUREA_CHECK(!psd::read(std::span(b).first(n),doc).ok());AUREA_CHECK_EQ(doc.width,777u);}
    b[18]=255; b[19]=255;psd::Document doc;AUREA_CHECK(!psd::read(b,doc).ok());
}
AUREA_TEST(Psd, ImportedLayersAnimateAndSurviveSave) {
    const auto data=fixture(); const std::string path="aurea_test_layers.psd",project="aurea_test_layers.aurea";
    PSD_REQUIRE(fileio::write_atomic(path,data.data(),data.size()).ok());
    Engine e;EngineConfig c;c.disableAutosave=true;c.workerCount=2;PSD_REQUIRE(e.initialize(c).ok());PSD_REQUIRE(e.new_project(8,4,30,nullptr).ok());
    const auto id=e.import_psd(path,"Camadas");PSD_REQUIRE(id.ok());
    auto* root=e.project()->timeline().composition(e.project()->timeline().root());
    auto* nested=e.project()->timeline().composition(root->layer(LayerId::unpack(*id))->nested.composition);
    PSD_REQUIRE(nested);AUREA_CHECK_EQ(nested->order().size(),2u);
    auto* layer=nested->layer(nested->order().at(1));
    auto& track=layer->tracks.get_or_create(TrackProperty::PositionX);track.set(FrameIndex{0},1);track.set(FrameIndex{30},7);
    AUREA_CHECK_EQ(track.value_or(FrameIndex{15},0),4.f);PSD_REQUIRE(e.save_project(project.c_str()).ok());
    PSD_REQUIRE(e.load_project(project.c_str()).ok());
    root=e.project()->timeline().composition(e.project()->timeline().root());
    nested=e.project()->timeline().composition(root->layer(root->order().at(0))->nested.composition);
    PSD_REQUIRE(nested);layer=nested->layer(nested->order().at(1));
    PSD_REQUIRE(layer->tracks.find(TrackProperty::PositionX));
    AUREA_CHECK_EQ(layer->tracks.find(TrackProperty::PositionX)->value_or(FrameIndex{15},0),4.f);
    e.shutdown();fileio::remove_file(path);fileio::remove_file(project);
}

AUREA_TEST(Psd, GroupsMaskUnicodeZipAnd16BitTaggedLayers) {
    for(u32 depth:{8u,16u})for(u32 compression=0;compression<4;++compression) {
        std::vector<u8> bytes;
        const auto path="engine/tests/fixtures/psd/groups-mask-"+std::to_string(depth)+"-"+std::to_string(compression)+".psd";
        PSD_REQUIRE(fileio::read_all(path,bytes,psd::kMaxFileBytes));psd::Document doc;PSD_REQUIRE(psd::read(bytes,doc).ok());
        PSD_REQUIRE(doc.layers.size()==2);AUREA_CHECK(doc.layers[0].group);AUREA_CHECK_NEAR(doc.layers[0].opacity,128.f/255,.001f);
        const auto& child=doc.layers[1];AUREA_CHECK_EQ(child.parent,0);AUREA_CHECK_EQ(child.name,std::string("Coração"));
        AUREA_CHECK_EQ(child.rgba[0],255);AUREA_CHECK_EQ(child.rgba[3],128);AUREA_CHECK_EQ(child.rgba[7],0);AUREA_CHECK_EQ(child.rgba[15],128);
    }
}
AUREA_TEST(Psd, CompositionAndNestingLimitsRefuseWithoutMutation) {
    Engine e;EngineConfig cfg;cfg.disableAutosave=true;cfg.workerCount=2;
    PSD_REQUIRE(e.initialize(cfg).ok());PSD_REQUIRE(e.new_project(8,4,30,nullptr).ok());
    auto& timeline=e.project()->timeline();auto* root=timeline.composition(timeline.root());
    const auto path="engine/tests/fixtures/psd/groups-mask-8-2.psd";
    root->set_nesting_depth(kMaxNestingDepth-1);
    AUREA_CHECK(!e.import_psd(path,"too deep").ok());AUREA_CHECK(root->order().empty());
    root->set_nesting_depth(0);
    while(timeline.composition_count()<255)(void)timeline.create_composition("full",8,4,30);
    AUREA_CHECK(!e.import_psd(path,"too many groups").ok());
    AUREA_CHECK_EQ(timeline.composition_count(),255u);AUREA_CHECK(root->order().empty());e.shutdown();
}
