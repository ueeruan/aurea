#include "aurea/Engine.hpp"
#include "aurea/tracking/MotionTrackData.hpp"
#include "aurea/tracking/CameraTrackData.hpp"
#include "aurea/tracking/TrackingValidation.hpp"
#include "../tracking/AnalysisDecode.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace aurea {
using namespace tracking;
struct Engine::MotionTrackJob {
    std::thread thread;
    std::atomic<bool> cancel{false}, finished{true};
    std::atomic<u32> state{0};
    std::atomic<f32> progress{0};
    std::mutex mutex;
    std::shared_ptr<const MotionTrackData> result;
    std::string message;
    u64 layer = 0, session = 0;
    CompositionId composition{};
    AssetId source{};
    f32 cropPercent = 0;
};
namespace {
bool timing_matches(const Layer& l, const MotionTrackData& d, f64 fps) {
    if (!std::isfinite(fps) || fps<=0) return false;
    if (d.path.empty() || d.localFrames.size()!=d.path.size() || d.sourceUs.size()!=d.path.size()) return false;
    for (usize i=0;i<d.path.size();++i) {
        if(d.localFrames[i]<0 || d.localFrames[i]>=l.duration().value) return false;
        if(static_cast<i64>(std::llround(std::max(0.0,l.source_frame(FrameIndex{l.start.value+d.localFrames[i]}))*1e6/fps))!=d.sourceUs[i])return false;
    }
    return true;
}
Vec2 projected(const Mat4& m, Vec2 p) {
    const auto q=m*Vec4{p.x,p.y,0,1};
    return std::abs(q.w)>1e-8f?Vec2{q.x/q.w,q.y/q.w}:Vec2{NAN,NAN};
}
bool inverse_affine2(const Mat4& m, Homography& out) {
    Homography h;h.m={m.col[0].x,m.col[1].x,m.col[3].x,m.col[0].y,m.col[1].y,m.col[3].y,m.col[0].w,m.col[1].w,m.col[3].w};
    return h.inverse(out);
}
/// Caminho (1º quadro medido → quadro) dos pontos rastreados, em px da fonte:
/// um ponto = translação; dois = semelhança (giro e escala). É o que
/// "Estabilizar pelo ponto" corrige: o detalhe fica parado na tela.
std::vector<MotionFrame> point_path(const MotionTrackData& d) {
    std::vector<MotionFrame> out(d.path.size());
    usize ref=0;while(ref<d.path.size()&&!d.path[ref].valid)++ref;
    if(ref>=d.path.size()||d.points.size()!=d.path.size())return out;
    for(usize i=0;i<out.size();++i){
        if(!d.path[i].valid)continue;
        const Vec2 a0=d.points[ref][0],a1=d.points[i][0];
        Homography h;
        if(d.pointCount>=2){
            const Vec2 u=d.points[ref][1]-a0,v=d.points[i][1]-a1;
            const f64 len=static_cast<f64>(u.x)*u.x+static_cast<f64>(u.y)*u.y;if(len<1e-6)continue;
            const f64 c=(static_cast<f64>(u.x)*v.x+static_cast<f64>(u.y)*v.y)/len,s=(static_cast<f64>(u.x)*v.y-static_cast<f64>(u.y)*v.x)/len;
            h.m={c,-s,a1.x-(c*a0.x-s*a0.y),s,c,a1.y-(s*a0.x+c*a0.y),0,0,1};
        } else h.m={1,0,static_cast<f64>(a1.x)-a0.x,0,1,static_cast<f64>(a1.y)-a0.y,0,0,1};
        out[i].path=h;out[i].valid=true;out[i].confidence=d.path[i].confidence;out[i].inliers=d.pointCount;
    }
    return out;
}
}
void Engine::join_motion_track() noexcept {
    if(!motionTrack_)return;
    motionTrack_->cancel.store(true);
    if(motionTrack_->thread.joinable())motionTrack_->thread.join();
}
void Engine::cancel_motion_track() noexcept {
    if(!motionTrack_)return;
    motionTrack_->cancel.store(true);
    if(motionTrack_->state.load()==1)motionTrack_->state.store(4);
}
bool Engine::restore_motion_track(u64 id) noexcept {
    if(motionTrack_&&!motionTrack_->finished.load())return false;
    join_motion_track();
    // A failed restore for another layer must not leave the previous layer's
    // completed result available for an accidental Apply. Saved layer caches
    // remain intact; an active worker was excluded above.
    motionTrack_.reset();
    std::lock_guard<std::mutex> guard(modelMutex_);
    const auto* comp=project_?current_composition():nullptr;
    const auto* layer=comp?comp->layer(LayerId::unpack(id)):nullptr;
    if(!layer||!layer->motionTrack||layer->motionTrack->sourceSignature!=source_signature(project_->asset(layer->source))||!timing_matches(*layer,*layer->motionTrack,comp->fps()))return false;
    auto job=std::make_shared<MotionTrackJob>();job->layer=id;job->session=projectSession_;
    job->composition=project_->timeline().current();job->source=layer->source;job->result=layer->motionTrack;
    job->state.store(2);job->progress.store(1);motionTrack_=std::move(job);return true;
}
bool Engine::start_motion_track(u64 id,u32 tool,u32 model,bool backward,const f32* xy,u32 count,f32 featureRadius,f32 searchRadius) noexcept {
    if(camera_track_status().state==1)return false;
    if(tool>4||model>3||count>4||(count&&!xy)||!std::isfinite(featureRadius)||!std::isfinite(searchRadius))return false;
    const u32 required=tool==0?1:tool==1?2:tool==4?0:4;
    if(count!=required)return false;
    // Uma análise por vez. Uma CANCELADA ainda saindo não bloqueia a próxima:
    // o trabalhador olha o cancelamento a cada quadro decodificado, então o
    // join abaixo é curto (antes: "Cancelar" e tocar de novo = recusado).
    if(motionTrack_&&!motionTrack_->finished.load()&&!motionTrack_->cancel.load())return false;
    join_motion_track();
    Asset asset;auto data=std::make_shared<MotionTrackData>();auto job=std::make_shared<MotionTrackJob>();
    std::array<Vec2,4> seeds{};
    {
        std::lock_guard<std::mutex> guard(modelMutex_);
        const auto* comp=project_?current_composition():nullptr;
        const auto* layer=comp?comp->layer(LayerId::unpack(id)):nullptr;
        const auto* a=layer?project_->asset(layer->source):nullptr;
        if(!layer||layer->kind!=LayerKind::Video||!a||!a->has_video()||!a->video.width||!a->video.height)return false;
        data->tool=static_cast<MotionTool>(tool);data->model=static_cast<MotionModel>(model);
        data->fps=comp->fps();data->sourceW=a->video.width;data->sourceH=a->video.height;data->pointCount=count;
        data->sourceSignature=source_signature(a);
        for(u32 i=0;i<count;++i){seeds[i]={xy[2*i],xy[2*i+1]};if(!Tracks2D::present(seeds[i])||seeds[i].x<0||seeds[i].y<0||seeds[i].x>=data->sourceW||seeds[i].y>=data->sourceH)return false;}
        if(count==2&&(seeds[1]-seeds[0]).length()<8)return false;
        if(count==4){Homography h;if(!quad_map(seeds,h))return false;}
        const i64 first=tool==4?layer->start.value:std::clamp(playback_.current().value,layer->start.value,layer->end.value-1);
        const i64 step=backward&&tool!=4?-1:1;
        // Clipe mais longo que o limite: analisa até o limite (não recusa).
        for(i64 f=first;f>=layer->start.value&&f<layer->end.value&&data->localFrames.size()<kMaxMotionTrackFrames;f+=step){
            data->localFrames.push_back(f-layer->start.value);
            data->sourceUs.push_back(static_cast<i64>(std::llround(std::max(0.0,layer->source_frame(FrameIndex{f}))*1e6/data->fps)));
        }
        if(data->sourceUs.size()<2)return false;
        asset=*a;asset.sourcePath=resolve_asset_path(a->sourcePath);
        job->layer=id;job->source=layer->source;job->session=projectSession_;job->composition=project_->timeline().current();
    }
    motionTrack_=job;job->state.store(1);job->finished.store(false);
    auto* factory=config_.mediaFactory;
    // Capture a raw job pointer: the Engine owns and joins it before reset. A
    // shared_ptr capture could destroy its own joinable std::thread on exit.
    job->thread=std::thread([this,j=job.get(),data,asset,seeds,featureRadius,searchRadius,factory]{
        struct Done{MotionTrackJob* j;~Done(){j->finished.store(true);}}done{j};
        auto fail=[&](const char* text){std::lock_guard<std::mutex>g(j->mutex);j->message=text;j->state.store(j->cancel.load()?4:3);};
        if(!factory)return fail("Decoder unavailable");
        // Decoder PRÓPRIO (como o export): nunca disputa o do preview.
        auto decoder=factory->open_video(asset,MediaPriority::Thumbnail);if(!decoder)return fail("Cannot decode video");
        AnalysisFramePicker picker(*decoder,asset.timebaseFps>0?asset.timebaseFps:30,j->cancel);
        const bool points=data->pointCount>0&&data->pointCount<3;
        // Bound both dimensions, including ultra-wide videos. Ponto/dois pontos
        // só olham um bloco em volta do detalhe: analisam a até 720 linhas
        // (precisão subpixel na fonte); o movimento global fica em 360.
        const u32 analysisH=points
            ?std::max(1u,std::min({720u,data->sourceH,static_cast<u32>(1280ull*data->sourceH/data->sourceW)}))
            :std::max(1u,std::min({360u,data->sourceH,static_cast<u32>(720ull*data->sourceH/data->sourceW)}));
        // px da fonte <-> px da análise, pelos CENTROS de pixel (redução por área).
        f32 kx=1,ky=1;
        auto toA=[&](Vec2 s){return Vec2{(s.x+.5f)/kx-.5f,(s.y+.5f)/ky-.5f};};
        auto toS=[&](Vec2 a){return Vec2{(a.x+.5f)*kx-.5f,(a.y+.5f)*ky-.5f};};
        // Ponto: bloco do quadro escolhido (sem deriva), pesos robustos.
        std::array<TemplateTracker,2> trackers;std::array<Vec2,2> pos{},velocity{};std::array<u32,2> missing{};
        // Estabilizador / planar: trechos de memória limitada, encadeados.
        constexpr u32 kSegment=240;
        FeatureTracker features(TrackMode::Balanced);usize segmentStart=0;
        auto flush=[&]()->bool{
            const Homography base=segmentStart<data->path.size()?data->path[segmentStart].path:Homography{};
            const bool baseValid=segmentStart==0||(segmentStart<data->path.size()&&data->path[segmentStart].valid);
            std::vector<Vec2> polygon;
            if(data->pointCount==4)for(auto p:seeds)polygon.push_back(base.project(toA(p)));
            auto part=estimate_path(features.tracks(),data->model,polygon,&j->cancel);
            if(part.empty())return false;
            for(usize k=segmentStart==0?0:1;k<part.size();++k){
                MotionFrame m=part[k];m.path=part[k].path*base;
                if(!baseValid&&!polygon.empty())m.valid=false;
                data->path.push_back(m);
            }
            return true;
        };
        const usize total=data->sourceUs.size();
        for(usize i=0;i<total;++i){
            if(j->cancel.load())return fail("Cancelled");
            FrameRef frame;
            const auto picked=picker.pick(data->sourceUs[i],frame);
            if(picked==AnalysisFramePicker::Result::Cancelled)return fail("Cancelled");
            if(picked!=AnalysisFramePicker::Result::Ok||!frame)return fail("Incomplete video: analysis stopped before the end");
            Gray gray;if(!frame_to_gray(*frame.get(),analysisH,gray))return fail("Cannot read analysis frame");
            frame={};
            if(i==0){data->analysisW=gray.width;data->analysisH=gray.height;kx=static_cast<f32>(data->sourceW)/gray.width;ky=static_cast<f32>(data->sourceH)/gray.height;}
            else if(gray.width!=data->analysisW||gray.height!=data->analysisH)return fail("Video dimensions changed during analysis");
            if(points){
                MotionFrame mf;mf.valid=true;mf.confidence=1;mf.inliers=data->pointCount;std::array<Vec2,4> tracked{};
                // Raio do painel = bloco MÍNIMO; o bloco cresce até conter o
                // objeto tocado (escala característica), até 4× o mínimo.
                const i32 minHalf=std::max(3,static_cast<i32>(std::lround(featureRadius/ky)));
                const i32 maxHalf=std::max(minHalf,std::min(4*minHalf,static_cast<i32>(std::min(gray.width,gray.height)/6)));
                for(u32 p=0;p<data->pointCount;++p){
                    if(i==0){pos[p]=toA(seeds[p]);trackers[p].start(gray,pos[p],feature_half_at(gray,pos[p],minHalf,maxHalf));}
                    else {
                        const i32 radius=static_cast<i32>(std::clamp(searchRadius/ky+missing[p]*3,4.f,160.f));
                        // Depois de muito tempo sumido, reencontrar exige um casamento melhor.
                        const f32 accept=missing[p]>15?.75f:.65f;
                        const auto result=trackers[p].track(gray,pos[p]+velocity[p]*static_cast<f32>(missing[p]+1),radius,accept);
                        if(result.score<accept){++missing[p];mf.valid=false;mf.confidence=0;}
                        else {
                            if(missing[p])++data->reacquired;
                            velocity[p]=(result.pos-pos[p])*(1.f/(missing[p]+1));pos[p]=result.pos;missing[p]=0;mf.confidence=std::min(mf.confidence,result.score);
                        }
                    }
                    tracked[p]=toS(pos[p]);
                }
                if(!mf.valid)++data->lost;
                data->points.push_back(tracked);data->path.push_back(mf);
            }else{
                features.add_frame(gray);
                if(static_cast<u64>(features.tracks().pos.size())*features.tracks().frames>kMaxCameraTrackCells)return fail("Analysis memory limit: use a smaller area");
                if(features.tracks().frames>kSegment||i+1==total){
                    if(!flush())return fail(j->cancel.load()?"Cancelled":"Not enough reliable motion. Choose a textured area");
                    if(i+1<total){features=FeatureTracker(TrackMode::Balanced);features.add_frame(gray);segmentStart=i;}
                }
            }
            j->progress.store(.97f*static_cast<f32>(i+1)/total);
        }
        if(!points){
            for(const auto& mf:data->path){std::array<Vec2,4> tracked{};for(u32 p=0;p<data->pointCount;++p){const auto q=mf.path.project(toA(seeds[p]));tracked[p]=Tracks2D::present(q)?toS(q):seeds[p];}data->points.push_back(tracked);if(!mf.valid)++data->lost;}
        }
        if(j->cancel.load())return fail("Cancelled");
        if(data->path.size()!=data->sourceUs.size())return fail("Incomplete video: analysis stopped before the end");
        if(std::count_if(data->path.begin(),data->path.end(),[](auto& p){return p.valid;})<2)return fail("Not enough reliable motion. Choose a textured area");
        {
            std::lock_guard<std::mutex>guard(modelMutex_);
            auto* comp=project_&&projectSession_==j->session?project_->timeline().composition(j->composition):nullptr;
            auto* layer=comp?comp->layer(LayerId::unpack(j->layer)):nullptr;
            if(!layer||layer->source!=j->source||data->sourceSignature!=source_signature(project_->asset(layer->source))||!timing_matches(*layer,*data,comp->fps()))return fail("Source or timing changed: analyse again");
            layer->motionTrack=data;project_->mark_dirty();
        }
        std::lock_guard<std::mutex>guard(j->mutex);j->result=data;j->progress.store(1);j->state.store(2);
        // Quadros sem medida não bloqueiam mais: ficam sem key (interpolados).
        // A UI mostra a contagem (status.lost) no idioma do app.
    });
    return true;
}
Engine::MotionTrackStatus Engine::motion_track_status() noexcept {
    MotionTrackStatus s;auto j=motionTrack_;if(!j)return s;
    s.state=j->state.load();s.progress=j->progress.load();std::lock_guard<std::mutex>g(j->mutex);s.message=j->message;s.cropPercent=j->cropPercent;
    if(auto d=j->result){s.tool=static_cast<u32>(d->tool);s.frames=static_cast<u32>(d->path.size());s.lost=d->lost;s.reacquired=d->reacquired;s.memoryBytes=d->memory_bytes();for(auto& p:d->path)if(p.valid){++s.validFrames;s.confidence+=p.confidence;s.errorPx+=p.rms;}if(s.validFrames){s.confidence/=s.validFrames;s.errorPx/=s.validFrames;}}
    return s;
}
u64 Engine::motion_track_source() noexcept {
    auto job=motionTrack_;if(!job)return 0;
    std::lock_guard<std::mutex> guard(modelMutex_);
    const auto* comp=project_&&projectSession_==job->session&&project_->timeline().current()==job->composition?current_composition():nullptr;
    const auto* source=comp?comp->layer(LayerId::unpack(job->layer)):nullptr;
    return source&&source->source==job->source?job->layer:0;
}
u32 Engine::motion_track_features(i64 frame,f32* out,u32 capacity) noexcept {
    auto j=motionTrack_;if(!j||!out||!capacity)return 0;
    std::shared_ptr<const MotionTrackData>d;{std::lock_guard<std::mutex>g(j->mutex);d=j->result;}if(!d)return 0;
    std::lock_guard<std::mutex>g(modelMutex_);auto* comp=project_&&projectSession_==j->session?current_composition():nullptr;
    auto* layer=comp&&project_->timeline().current()==j->composition?comp->layer(LayerId::unpack(j->layer)):nullptr;
    if(!layer||layer->source!=j->source||layer->motionTrack!=d||
       d->sourceSignature!=source_signature(project_->asset(layer->source))||
       !timing_matches(*layer,*d,comp->fps())||d->points.size()!=d->path.size())return 0;
    auto it=std::find(d->localFrames.begin(),d->localFrames.end(),frame-layer->start.value);if(it==d->localFrames.end())return 0;
    const usize k=static_cast<usize>(it-d->localFrames.begin());const auto matrix=layer_comp_matrix(*comp,*layer,FrameIndex{frame});
    const u32 n=std::min(capacity,d->pointCount);for(u32 p=0;p<n;++p){const auto q=projected(matrix,d->points[k][p]);out[p*3]=q.x;out[p*3+1]=q.y;out[p*3+2]=d->path[k].valid?d->path[k].confidence:0;}return n;
}
Result<u64> Engine::apply_motion_track(u64 target,u32 apply,bool lock,f32 smooth,f32 maxScale,u32 crop) noexcept {
    auto j=motionTrack_;if(!j||j->state.load()!=2||apply>5||crop>2||!std::isfinite(smooth)||!std::isfinite(maxScale))return Status{Errc::InvalidState,"No completed analysis"};
    std::shared_ptr<const MotionTrackData>d;{std::lock_guard<std::mutex>g(j->mutex);d=j->result;}
    if(!d||d->path.size()!=d->points.size())return Status{Errc::InvalidState,"No completed analysis"};
    // Quadros perdidos não recusam o clipe inteiro: sem medida, sem key (a
    // curva interpola entre os vizinhos). Só precisa de dois quadros medidos.
    if(std::count_if(d->path.begin(),d->path.end(),[](const auto& p){return p.valid;})<2)return Status{Errc::InvalidState,"Not enough tracked frames: choose a textured detail and analyse again"};
    std::vector<StabilizedFrame> stabilized;
    // Estabilizar: pelo movimento global (Estabilizador) ou pelo(s) ponto(s)
    // rastreado(s) — "Estabilizar pelo ponto" deixa o detalhe parado na tela.
    u32 stabW=d->analysisW,stabH=d->analysisH;
    if(apply==3){
        std::vector<MotionFrame> path=d->path;
        if(d->tool!=MotionTool::Stabilizer){
            if(d->pointCount<1||d->pointCount>2)return Status{Errc::InvalidArgument,"Stabilize with the Stabilizer, Point or Two Points analysis"};
            path=point_path(*d);stabW=d->sourceW;stabH=d->sourceH;
        }
        StabilizationOptions o;o.lock=lock;o.smoothSeconds=std::clamp(smooth,.05f,3.f);o.maxScale=std::clamp(maxScale,1.f,2.f);o.crop=static_cast<CropMode>(crop);
        stabilized=stabilize_path(path,stabW,stabH,d->fps,o);
        if(stabilized.size()!=d->path.size())return Status{Errc::InvalidState,"Stabilization failed"};
    }
    if(apply==2&&d->pointCount!=4)return Status{Errc::InvalidArgument,"Corner Pin needs four tracked corners"};
    std::lock_guard<std::mutex>g(modelMutex_);auto* comp=project_&&projectSession_==j->session?current_composition():nullptr;
    auto* src=comp&&project_->timeline().current()==j->composition?comp->layer(LayerId::unpack(j->layer)):nullptr;
    if(!src||src->source!=j->source||src->motionTrack!=d||d->sourceSignature!=source_signature(project_->asset(src->source))||!timing_matches(*src,*d,comp->fps()))return Status{Errc::InvalidState,"Source or timing changed"};
    Layer* dst=(apply==1||apply==2)?comp->layer(LayerId::unpack(target)):apply==3?src:nullptr;
    if((apply==1||apply==2)&&(!dst||dst==src||dst->threeD||dst->parent.valid()||dst->hasParentBasis))return Status{Errc::InvalidArgument,"Select a separate, unparented 2D target"};
    if(apply!=3&&!d->pointCount)return Status{Errc::InvalidArgument,"Analysis has no attachment points"};
    // Prepare all samples BEFORE history or mutation. Invalid geometry must
    // not leave an empty object/effect or partial keyframes behind.
    std::vector<std::array<Vec2,4>> samples(d->path.size());
    std::vector<u8> keyed(d->path.size(),0);
    struct TargetSample { f32 x=0,y=0,angle=0,sx=1,sy=1; };
    std::vector<TargetSample> targetSamples(samples.size());
    bridge::LayerDetailPOD detail{};if(dst&&!fill_layer_detail_locked(apply==3?j->layer:target,detail))return Status{Errc::InvalidArgument};
    for(usize i=0;i<samples.size();++i){
        if(!d->path[i].valid||(apply==3&&!stabilized[i].valid))continue;
        keyed[i]=1;
        const FrameIndex frame{src->start.value+d->localFrames[i]};
        if(apply==1){
            const auto local=dst->local_time(frame);
            auto v=[&](TrackProperty p,f32 fallback){const auto* track=dst->tracks.find(p);return track?track->value_or(local,fallback):fallback;};
            targetSamples[i]={v(TrackProperty::PositionX,dst->transform.position.x),v(TrackProperty::PositionY,dst->transform.position.y),v(TrackProperty::RotationZ,dst->transform.rotation.z),v(TrackProperty::ScaleX,dst->transform.scale.x),v(TrackProperty::ScaleY,dst->transform.scale.y)};
        }
        if(apply==3){const std::array<Vec2,4> corners={Vec2{0,0},Vec2{static_cast<f32>(stabW),0},Vec2{static_cast<f32>(stabW),static_cast<f32>(stabH)},Vec2{0,static_cast<f32>(stabH)}};for(u32 p=0;p<4;++p){auto q=stabilized[i].correction.project(corners[p]);if(!Tracks2D::present(q))return Status{Errc::InvalidArgument};samples[i][p]={q.x*100/stabW,q.y*100/stabH};}}
        else {const auto m=layer_comp_matrix(*comp,*src,frame);Homography inv;if(apply==2&&(!detail.sourceWidth||!detail.sourceHeight||!inverse_affine2(layer_comp_matrix(*comp,*dst,frame),inv)))return Status{Errc::InvalidArgument};
            for(u32 p=0;p<d->pointCount;++p){auto q=projected(m,d->points[i][p]);if(apply==2){q=inv.project(q);q={q.x*100/detail.sourceWidth,q.y*100/detail.sourceHeight};}if(!Tracks2D::present(q))return Status{Errc::InvalidArgument};samples[i][p]=q;}}
    }
    usize firstKey=0;while(firstKey<keyed.size()&&!keyed[firstKey])++firstKey;
    if(firstKey>=keyed.size())return Status{Errc::InvalidState,"Not enough tracked frames"};
    if(apply==2||apply==3){
        const bool reusable=std::any_of(dst->effects.begin(),dst->effects.end(),[&](const auto& e){return e.id==dst->motionTrackEffect&&e.type==effect_type_id(effect_keys::kCornerPin);});
        if(!effectRegistry_.params(effect_type_id(effect_keys::kCornerPin))||(!reusable&&dst->effects.size()>=kMaxEffectCount))return Status{Errc::InvalidState};
    }
    history_.before_mutation(*comp,project_->timeline().current(),apply==3?"Stabilize video":"Apply motion tracking");modelRevision_.fetch_add(1,std::memory_order_acq_rel);
    LayerId destId=dst?LayerId::unpack(apply==3?j->layer:target):comp->add_layer(apply==4?LayerKind::Shape:apply==5?LayerKind::Text:LayerKind::Null,"Motion track");
    dst=comp->layer(destId);src=comp->layer(LayerId::unpack(j->layer));if(!dst||!src)return Status{Errc::OutOfMemory};
    if(apply==0||apply==4||apply==5){dst->start=src->start;dst->end=src->end;dst->transform.anchor={50,50,0};if(apply==4){dst->shape.bounds={0,0,100,100};dst->shape.filled=true;}if(apply==5)dst->text.content="Text";}
    if(apply==2||apply==3){
        auto found=std::find_if(dst->effects.begin(),dst->effects.end(),[&](const auto& e){return e.id==dst->motionTrackEffect&&e.type==effect_type_id(effect_keys::kCornerPin);});
        if(found==dst->effects.end()) {
            EffectInstance effect;effect.id=dst->alloc_effect_id();effect.type=effect_type_id(effect_keys::kCornerPin);
            initialize_instance(effect,*effectRegistry_.params(effect.type));dst->motionTrackEffect=effect.id;dst->effects.push_back(std::move(effect));found=dst->effects.end()-1;
        }
        const u32 eid=found->id;found->params[8].constant=ParamValue::boolean(apply==3);found->enabled=true;
        for(u32 p=0;p<8;++p)dst->tracks.get_or_create(TrackProperty::EffectParam,eid,param_track_key(p,0)).clear();
        for(usize i=0;i<samples.size();++i){if(!keyed[i])continue;auto local=dst->local_time(FrameIndex{src->start.value+d->localFrames[i]});for(u32 p=0;p<4;++p){dst->tracks.find(TrackProperty::EffectParam,eid,param_track_key(p*2,0))->set(local,samples[i][p].x);dst->tracks.find(TrackProperty::EffectParam,eid,param_track_key(p*2+1,0))->set(local,samples[i][p].y);}}
        if(apply==3){f32 peak=1;for(auto& f:stabilized)if(f.valid)peak=std::max(peak,f.scale);std::lock_guard<std::mutex>jl(j->mutex);j->cropPercent=(peak-1)*100;}
    }else {
        for(auto p:{TrackProperty::PositionX,TrackProperty::PositionY})(void)dst->tracks.get_or_create(p);
        if(d->pointCount>=2)for(auto p:{TrackProperty::RotationZ,TrackProperty::ScaleX,TrackProperty::ScaleY})(void)dst->tracks.get_or_create(p);
        const auto first=samples[firstKey][1]-samples[firstKey][0];const f32 firstLength=first.length(),firstAngle=std::atan2(first.y,first.x);f32 prevAngle=0;
        for(usize i=0;i<samples.size();++i){if(!keyed[i])continue;auto local=dst->local_time(FrameIndex{src->start.value+d->localFrames[i]});const auto& base=targetSamples[i];
            const auto offset=apply==1?Vec2{base.x-samples[firstKey][0].x,base.y-samples[firstKey][0].y}:Vec2{};
            dst->tracks.find(TrackProperty::PositionX)->set(local,samples[i][0].x+offset.x);dst->tracks.find(TrackProperty::PositionY)->set(local,samples[i][0].y+offset.y);
            if(d->pointCount>=2&&firstLength>1e-4f){auto delta=samples[i][1]-samples[i][0];f32 angle=(std::atan2(delta.y,delta.x)-firstAngle)/kDeg2Rad;while(angle-prevAngle>180)angle-=360;while(angle-prevAngle< -180)angle+=360;prevAngle=angle;dst->tracks.find(TrackProperty::RotationZ)->set(local,base.angle+angle);dst->tracks.find(TrackProperty::ScaleX)->set(local,base.sx*delta.length()/firstLength);dst->tracks.find(TrackProperty::ScaleY)->set(local,base.sy*delta.length()/firstLength);}}
        if(apply!=1){const auto* x=dst->tracks.find(TrackProperty::PositionX);const auto* y=dst->tracks.find(TrackProperty::PositionY);if(x&&y&&!x->keys.empty()&&!y->keys.empty())dst->transform.position={x->keys.front().value,y->keys.front().value,0};}
    }
    comp->rebuild_draw_order();project_->mark_dirty();request_render();return destId.pack();
}
} // namespace aurea
