#include "TestFramework.hpp"
#include "aurea/tracking/MotionGeometry.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include "SyntheticVideo.hpp"
#include "aurea/Engine.hpp"
#include "aurea/tracking/MotionTrackData.hpp"
#include "aurea/project/Serialization.hpp"
#include "aurea/tracking/TrackingValidation.hpp"

using namespace aurea;
using namespace aurea::tracking;
namespace {
Homography movement(f64 x,f64 y,f64 angle=0,f64 scale=1) {
    Homography h;const f64 c=std::cos(angle)*scale,s=std::sin(angle)*scale;
    h.m={c,-s,320+x-c*320+s*180,s,c,180+y-s*320-c*180,0,0,1};return h;
}
std::vector<Vec2> grid() {
    std::vector<Vec2> out;for(int y=0;y<8;++y)for(int x=0;x<12;++x)out.push_back({30.f+x*48,30.f+y*40});return out;
}
f64 jitter(const std::vector<Vec2>& points) {
    f64 sum=0;for(usize i=1;i+1<points.size();++i)sum+=std::pow(points[i+1].x-2*points[i].x+points[i-1].x,2)+std::pow(points[i+1].y-2*points[i].y+points[i-1].y,2);
    return std::sqrt(sum/std::max<usize>(1,points.size()-2));
}
}
AUREA_TEST(MotionGeometry, PerspectiveConsensusRejectsMovingForeground) {
    auto a=grid(),b=a;
    Homography truth;truth.m={1.01,.08,22,-.025,.98,13,.00021,-.00013,1};
    for(usize i=0;i<a.size();++i){b[i]=truth.project(a[i]);if(i%4==0){b[i].x+=80;b[i].y-=40;}}
    auto fit=estimate_motion(a,b,MotionModel::Perspective);
    AUREA_CHECK(fit.valid);AUREA_CHECK_EQ(fit.count,72u);AUREA_CHECK(fit.rms<0.001f);
    for(Vec2 p:{Vec2{0,0},Vec2{640,360},Vec2{270,180}}){auto x=fit.transform.project(p),y=truth.project(p);AUREA_CHECK_NEAR(x.x,y.x,.002);AUREA_CHECK_NEAR(x.y,y.y,.002);}
    for(usize i=0;i<a.size();++i)AUREA_CHECK_EQ(fit.inlier[i],i%4?1:0);
}
AUREA_TEST(MotionGeometry, CornerPinRejectsFoldAndMapsEachCorner) {
    Homography h;const std::array<Vec2,4> q={Vec2{20,30},Vec2{280,5},Vec2{330,230},Vec2{-12,180}};
    AUREA_CHECK(quad_map(q,h));const std::array<Vec2,4> uv={Vec2{0,0},Vec2{1,0},Vec2{1,1},Vec2{0,1}};
    for(u32 i=0;i<4;++i){auto p=h.project(uv[i]);AUREA_CHECK_NEAR(p.x,q[i].x,.001);AUREA_CHECK_NEAR(p.y,q[i].y,.001);}
    AUREA_CHECK(!quad_map({q[0],q[2],q[1],q[3]},h));
    AUREA_CHECK(!quad_map({q[0],q[0],q[0],q[0]},h));
}
AUREA_TEST(MotionGeometry, PredictedSearchRecoversPointBeyondOriginalWindow) {
    Gray a;a.width=120;a.height=90;a.px.resize(120*90);
    for(u32 y=0;y<90;++y)for(u32 x=0;x<120;++x)a.px[y*120+x]=static_cast<f32>((x*271+y*761+x*y*31)%1024)/1024;
    Gray b=a;for(u32 y=0;y<90;++y)for(u32 x=0;x<120;++x)b.px[y*120+x]=a.at(static_cast<i32>(x)-25,y);
    const auto r=track_step(a,b,{40,45},6,8,{65,45});AUREA_CHECK(r.score>.99);AUREA_CHECK_NEAR(r.pos.x,65,.1);AUREA_CHECK_NEAR(r.pos.y,45,.1);
    Gray flat=a;std::fill(flat.px.begin(),flat.px.end(),.5f);AUREA_CHECK(track_step(flat,b,{40,45}).score<.65);
}

AUREA_TEST(MotionGeometry, EngineCacheApplyUndoAndTimingInvalidation) {
    test::SyntheticConfig cfg;cfg.width=320;cfg.height=180;cfg.frameCount=30;cfg.pattern=test::SyntheticPattern::Scene3D;
    test::SyntheticFactory factory(cfg);Engine e;EngineConfig ec;ec.workerCount=2;ec.memoryBudgetBytes=128ull<<20;ec.disableAutosave=true;ec.mediaFactory=&factory;
    AUREA_CHECK(e.initialize(ec).ok());AUREA_CHECK(e.new_project(320,180,30,"motion").ok());VideoImport imp;imp.sourcePath="synthetic-motion";auto source=e.import_video(imp);AUREA_CHECK(source.ok());if(!source.ok())return;
    AUREA_CHECK(e.start_motion_track(*source,4,0,false,nullptr,0));
    const auto started=std::chrono::steady_clock::now();while(e.motion_track_status().state==1&&std::chrono::steady_clock::now()-started<std::chrono::seconds(45))std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto status=e.motion_track_status();std::printf("    motion worker: state=%u valid=%u/%u lost=%u rms=%.3f %s\n",status.state,status.validFrames,status.frames,status.lost,status.errorPx,status.message.c_str());
    AUREA_CHECK_EQ(status.state,2u);AUREA_CHECK_EQ(status.validFrames,30u);
    auto* comp=e.project()->timeline().composition(e.project()->timeline().current());auto* layer=comp->layer(LayerId::unpack(*source));
    AUREA_CHECK(layer->motionTrack!=nullptr);if(!layer->motionTrack){e.shutdown();return;}
    const auto transform=layer->transform;AUREA_CHECK(e.apply_motion_track(*source,3).ok());layer=comp->layer(LayerId::unpack(*source));
    AUREA_CHECK_EQ(layer->effects.size(),1u);AUREA_CHECK_EQ(layer->effects[0].type,effect_type_id(effect_keys::kCornerPin));AUREA_CHECK_NEAR(layer->transform.position.x,transform.position.x,.001);
    for(u32 p=0;p<8;++p){auto* tr=layer->tracks.find(TrackProperty::EffectParam,layer->effects[0].id,param_track_key(p,0));AUREA_CHECK(tr!=nullptr);if(tr)AUREA_CHECK_EQ(tr->keys.size(),30u);}
    const auto effectId=layer->effects[0].id;
    AUREA_CHECK(e.apply_motion_track(*source,3,true,.4f,1.1f,2).ok());layer=comp->layer(LayerId::unpack(*source));
    AUREA_CHECK_EQ(layer->effects.size(),1u);AUREA_CHECK_EQ(layer->effects[0].id,effectId);
    Command undoRepeat;undoRepeat.type=CommandType::Undo;AUREA_CHECK(e.apply_command(undoRepeat).ok());
    Command undo;undo.type=CommandType::Undo;AUREA_CHECK(e.apply_command(undo).ok());layer=comp->layer(LayerId::unpack(*source));AUREA_CHECK(layer->effects.empty());AUREA_CHECK(layer->motionTrack!=nullptr);
    const char* file="aurea_test_motion_cache.aurea";AUREA_CHECK(e.save_project(file).ok());Project saved;LoadReport report;AUREA_CHECK(ProjectSerializer::load(saved,file,LoadOptions{},&report).ok());AUREA_CHECK_EQ(report.timelineVersion,kTimelineSectionVersion);
    AUREA_CHECK(e.load_project(file).ok());comp=e.project()->timeline().composition(e.project()->timeline().current());u64 restored=0;comp->layers().for_each([&](LayerId id,const Layer& l){if(l.kind==LayerKind::Video)restored=id.pack();});AUREA_CHECK(e.restore_motion_track(restored));
    layer=comp->layer(LayerId::unpack(restored));layer->speed=2;AUREA_CHECK(!e.apply_motion_track(restored,3).ok());AUREA_CHECK(!e.restore_motion_track(restored));
    AUREA_CHECK(e.new_project(320,180,30,"new").ok());AUREA_CHECK_EQ(e.motion_track_status().state,0u);AUREA_CHECK(!e.apply_motion_track(restored,3).ok());e.shutdown();std::remove(file);
}
AUREA_TEST(MotionGeometry, AttachmentPreservesAuthoredAnimationAndRejectsChangedSource) {
    test::SyntheticConfig cfg;cfg.width=320;cfg.height=180;cfg.frameCount=30;
    test::SyntheticFactory factory(cfg);Engine e;EngineConfig ec;ec.workerCount=2;ec.disableAutosave=true;ec.mediaFactory=&factory;
    AUREA_CHECK(e.initialize(ec).ok());AUREA_CHECK(e.new_project(320,180,30,"attach").ok());
    VideoImport imp;imp.sourcePath="synthetic-attach";auto imported=e.import_video(imp);AUREA_CHECK(imported.ok());if(!imported.ok())return;
    auto* comp=e.project()->timeline().composition(e.project()->timeline().current());
    auto* src=comp->layer(LayerId::unpack(*imported));const auto source=src->source;
    auto data=std::make_shared<MotionTrackData>();data->sourceW=320;data->sourceH=180;data->analysisW=320;data->analysisH=180;data->pointCount=1;
    data->sourceSignature=source_signature(e.project()->asset(source));
    for(int i=0;i<3;++i){data->localFrames.push_back(i);data->sourceUs.push_back(static_cast<i64>(std::llround(i*1e6/30)));MotionFrame f;f.valid=true;f.confidence=1;data->path.push_back(f);std::array<Vec2,4> p{};p[0]={100.f+10*i,90};data->points.push_back(p);}
    src->motionTrack=data;AUREA_CHECK(e.restore_motion_track(*imported));
    auto id=comp->add_layer(LayerKind::Shape,"Animated target");auto* dst=comp->layer(id);dst->end=FrameIndex{30};
    dst->transform.position={20,30,0};dst->transform.rotation.z=17;dst->transform.scale={2,3,1};
    auto& x=dst->tracks.get_or_create(TrackProperty::PositionX);x.set(FrameIndex{0},20);x.set(FrameIndex{2},40);
    const auto middle=x.value_or(FrameIndex{1},0);
    AUREA_CHECK(e.apply_motion_track(id.pack(),1).ok());dst=comp->layer(id);
    AUREA_CHECK_NEAR(dst->tracks.find(TrackProperty::PositionX)->value_or(FrameIndex{0},0),20,.01);
    AUREA_CHECK_NEAR(dst->tracks.find(TrackProperty::PositionX)->value_or(FrameIndex{1},0),middle+10,.01);
    AUREA_CHECK_NEAR(dst->tracks.find(TrackProperty::PositionX)->value_or(FrameIndex{2},0),60,.01);
    AUREA_CHECK(dst->tracks.find(TrackProperty::RotationZ)==nullptr);AUREA_CHECK(dst->tracks.find(TrackProperty::ScaleX)==nullptr);
    auto* asset=e.project()->asset(source);asset->sourcePath="replacement-video";
    AUREA_CHECK(!e.restore_motion_track(*imported));AUREA_CHECK(!e.apply_motion_track(id.pack(),1).ok());
    e.shutdown();
}
AUREA_TEST(MotionGeometry, SimilarityEstimatesScaleRotationAndTranslation) {
    auto a=grid(),b=a;const auto truth=movement(20,-12,.23,1.07);
    for(usize i=0;i<a.size();++i)b[i]=truth.project(a[i]);
    auto fit=estimate_motion(a,b,MotionModel::Similarity);
    AUREA_CHECK(fit.valid);AUREA_CHECK(fit.rms<.001f);
    for(int i=0;i<9;++i)AUREA_CHECK_NEAR(fit.transform.m[i],truth.m[i],.0001);
    auto automatic=estimate_motion(a,b,MotionModel::Auto);AUREA_CHECK(automatic.valid);
    AUREA_CHECK_NEAR(automatic.transform.m[6],0,1e-12);
}
AUREA_TEST(MotionGeometry, DegenerateAndLostTracksDoNotInventMotion) {
    std::vector<Vec2>a(20,{10,10}),b(20,{20,20});
    AUREA_CHECK(!estimate_motion(a,b,MotionModel::Perspective).valid);
    Tracks2D t;t.frames=30;t.width=640;t.height=360;
    for(auto p:grid()) {std::vector<Vec2> row(30);for(int f=0;f<30;++f)row[f]=f==15?Vec2{NAN,NAN}:movement(f*.5,2*std::sin(f)).project(p);t.pos.push_back(row);}
    auto path=estimate_path(t,MotionModel::Similarity);
    AUREA_CHECK_EQ(path.size(),30u);for(int i=0;i<15;++i)AUREA_CHECK(path[i].valid);
    // O quadro sem medida fica inválido (nada inventado), mas a análise não
    // para ali: a referência do 1º quadro reancora o caminho EXATO depois do
    // buraco. Antes um quadro ruim invalidava o resto do clipe (e o
    // estabilizador/Corner Pin recusavam aplicar).
    AUREA_CHECK(!path[15].valid);
    for(int i=16;i<30;++i){AUREA_CHECK(path[i].valid);const auto q=path[i].path.project({320,180}),r=movement(i*.5,2*std::sin(i)).project({320,180});AUREA_CHECK_NEAR(q.x,r.x,.01);AUREA_CHECK_NEAR(q.y,r.y,.01);}
    // Sem referência visível depois do buraco: o movimento global (estabilizar)
    // atravessa pelo último caminho conhecido; numa REGIÃO (planar) os quadros
    // seguintes só valem quando reancorados — ali ficam inválidos.
    Tracks2D gap;gap.frames=30;gap.width=640;gap.height=360;
    for(auto p:grid()){
        std::vector<Vec2> early(30,Vec2{NAN,NAN}),late(30,Vec2{NAN,NAN});
        for(int f=0;f<30;++f){const auto q=movement(f*.5,0).project(p);if(f<15)early[f]=q;if(f>=10&&f!=15)late[f]=q;}
        gap.pos.push_back(early);gap.pos.push_back(late);
    }
    auto global=estimate_path(gap,MotionModel::Similarity);
    auto region=estimate_path(gap,MotionModel::Similarity,{{0,0},{640,0},{640,360},{0,360}});
    AUREA_CHECK(!global[15].valid&&!global[16].valid&&!region[15].valid);
    for(int i=17;i<30;++i){AUREA_CHECK(global[i].valid);AUREA_CHECK(!region[i].valid);}
    // Atravessou sem inventar giro/escala: só o deslocamento perdido no buraco.
    AUREA_CHECK_NEAR(global[29].path.project({320,180}).x-global[17].path.project({320,180}).x,6.0,.01);
}
AUREA_TEST(MotionGeometry, PlaneRegionSeparatesIndependentObjectFromBackground) {
    Tracks2D t;t.frames=60;t.width=640;t.height=360;
    const auto points=grid();
    for(auto p:points){std::vector<Vec2> row(60);for(int f=0;f<60;++f)row[f]=movement(p.x<300?f*.4:-f*.6,0).project(p);t.pos.push_back(row);}
    auto plane=estimate_path(t,MotionModel::Similarity,{{0,0},{300,0},{300,360},{0,360}});
    AUREA_CHECK_EQ(plane.size(),60u);
    for(int f=0;f<60;++f){AUREA_CHECK(plane[f].valid);AUREA_CHECK_NEAR(plane[f].path.project({100,100}).x,100+f*.4,.05);}
}
AUREA_TEST(MotionGeometry, SmoothRemovesJitterWhileKeepingCameraPan) {
    std::vector<MotionFrame> path(300);std::vector<Vec2> raw,smoothed;
    for(usize i=0;i<path.size();++i){path[i].valid=true;path[i].path=movement(i*.25+5*std::sin(i*1.7),4*std::sin(i*1.3),.008*std::sin(i*1.1));raw.push_back(path[i].path.project({320,180}));}
    StabilizationOptions o;o.crop=CropMode::None;o.smoothSeconds=.4f;
    const auto result=stabilize_path(path,640,360,30,o);
    AUREA_CHECK_EQ(result.size(),path.size());
    for(usize i=0;i<path.size();++i){AUREA_CHECK(result[i].valid);smoothed.push_back((result[i].correction*path[i].path).project({320,180}));}
    AUREA_CHECK(jitter(smoothed)<jitter(raw)*.1);
    AUREA_CHECK(smoothed.back().x-smoothed.front().x>65);
    o.lock=true;
    const auto locked=stabilize_path(path,640,360,30,o);
    for(usize i=0;i<path.size();++i){const auto p=(locked[i].correction*path[i].path).project({320,180});AUREA_CHECK_NEAR(p.x,320,.001);AUREA_CHECK_NEAR(p.y,180,.001);}
}
AUREA_TEST(MotionGeometry, CropHonorsMaximumAndDynamicRateWithoutExposingBorders) {
    std::vector<MotionFrame> path(90);
    for(usize i=0;i<path.size();++i){path[i].valid=true;path[i].path=movement(180*std::sin(i*.05),40*std::cos(i*.05),.1*std::sin(i*.1));}
    StabilizationOptions o;o.lock=true;o.maxScale=1.10f;o.crop=CropMode::Dynamic;
    const auto result=stabilize_path(path,640,360,30,o);
    bool reduced=false;
    for(usize i=0;i<result.size();++i){
        AUREA_CHECK(result[i].valid);AUREA_CHECK(result[i].scale<=1.10001f);reduced|=result[i].strength<.8f;
        if(i)AUREA_CHECK(std::abs(result[i].scale-result[i-1].scale)<=.04/30+.00001);
        Homography inv;AUREA_CHECK(result[i].correction.inverse(inv));
        for(Vec2 p:{Vec2{0,0},Vec2{640,0},Vec2{640,360},Vec2{0,360}}){auto q=inv.project(p);AUREA_CHECK(q.x>=-.01&&q.y>=-.01&&q.x<=640.01&&q.y<=360.01);}
    }
    AUREA_CHECK(reduced);
}

AUREA_TEST(MotionGeometry, RealMotionProbe) {
    const char* file=std::getenv("AUREA_MOTION_OBSERVATIONS");
    if(!file){std::printf("    (skipped: AUREA_MOTION_OBSERVATIONS)\n");return;}
    FILE* input=std::fopen(file,"r");AUREA_CHECK(input!=nullptr);if(!input)return;
    Tracks2D tracks;tracks.width=480;tracks.height=360;char line[512];
    while(std::fgets(line,sizeof(line),input)) {
        u32 id=0,frame=0;f32 x=0,y=0;
        if(std::sscanf(line,"%u,%u,%f,%f",&id,&frame,&x,&y)!=4||id>=100000||frame>=1800)continue;
        if(tracks.pos.size()<=id)tracks.pos.resize(id+1);
        auto& row=tracks.pos[id];if(row.size()<=frame)row.resize(frame+1,{NAN,NAN});row[frame]={x,y};
        tracks.frames=std::max(tracks.frames,frame+1);
    }
    std::fclose(input);for(auto& row:tracks.pos)row.resize(tracks.frames,{NAN,NAN});
    const auto begin=std::chrono::steady_clock::now();
    const auto path=estimate_path(tracks,MotionModel::Auto);
    const auto correction=stabilize_path(path,480,360,30,{});
    const auto elapsed=std::chrono::duration<f64>(std::chrono::steady_clock::now()-begin).count();
    const std::string name=std::string(file)+".stabilization.csv";
    FILE* output=std::fopen(name.c_str(),"w");
    if(output)std::fprintf(output,"frame,valid,confidence,rms,scale,strength,h00,h01,h02,h10,h11,h12,h20,h21,h22\n");
    u32 valid=0;f64 rms=0,scale=0,strength=0;std::vector<Vec2> raw,stable;
    for(usize i=0;i<path.size();++i){
        if(path[i].valid){++valid;rms+=path[i].rms;scale+=correction[i].scale;strength+=correction[i].strength;raw.push_back(path[i].path.project({240,180}));stable.push_back((correction[i].correction*path[i].path).project({240,180}));}
        if(output){std::fprintf(output,"%zu,%d,%.6g,%.6g,%.6g,%.6g",i,correction[i].valid,path[i].confidence,path[i].rms,correction[i].scale,correction[i].strength);for(f64 v:correction[i].correction.m)std::fprintf(output,",%.12g",v);std::fprintf(output,"\n");}
    }
    if(output)std::fclose(output);
    std::printf("    REAL STABILIZER: frames=%u/%u rms=%.4f meanScale=%.4f meanStrength=%.4f jitterBefore=%.4f jitterAfter=%.4f analysis=%.3fs\n",valid,tracks.frames,rms/std::max(1u,valid),scale/std::max(1u,valid),strength/std::max(1u,valid),jitter(raw),jitter(stable),elapsed);
    AUREA_CHECK(tracks.frames>=300);AUREA_CHECK_EQ(valid,tracks.frames);AUREA_CHECK(jitter(stable)<jitter(raw));
}
