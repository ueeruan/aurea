#include "aurea/Engine.hpp"
#include "aurea/tracking/MotionTrackData.hpp"
#include "aurea/tracking/CameraTrackData.hpp"
#include "aurea/tracking/TrackingValidation.hpp"
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
    if(count!=required|| (motionTrack_&&!motionTrack_->finished.load()))return false;
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
        for(i64 f=first;f>=layer->start.value&&f<layer->end.value;f+=step){
            if(data->localFrames.size()>=1800)return false;
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
        auto decoder=factory->open_video(asset,MediaPriority::Thumbnail);if(!decoder)return fail("Cannot decode video");
        FeatureTracker features(TrackMode::Balanced);
        const bool points=data->pointCount>0&&data->pointCount<3;
        std::array<Gray,2> lastGood;std::array<Vec2,2> pos{},velocity{};std::array<u32,2> missing{};
        i64 lastPts=std::numeric_limits<i64>::min();FrameRef previous;
        const i64 halfFrame=static_cast<i64>(5e5/(asset.timebaseFps>0?asset.timebaseFps:30));
        // Bound both dimensions, including ultra-wide videos.
        const u32 analysisH=std::max(1u,std::min({360u,data->sourceH,static_cast<u32>(720ull*data->sourceH/data->sourceW)}));
        for(usize i=0;i<data->sourceUs.size();++i){
            if(j->cancel.load())return fail("Cancelled");
            const auto want=data->sourceUs[i];
            if(i==0||want<lastPts-halfFrame){if(!decoder->seek_to_keyframe(want).ok())return fail("Seek failed");lastPts=std::numeric_limits<i64>::min();previous={};}
            FrameRef frame;if(previous&&std::abs(lastPts-want)<=halfFrame)frame=previous;
            bool eos=false;
            for(int guard=0;!frame&&!eos&&guard<600;++guard){FrameRef f;i64 pts=0;if(!decoder->next_frame(want-halfFrame,f,pts,eos).ok())break;if(!f)continue;lastPts=pts;if(pts>=want-halfFrame)frame=std::move(f);}
            if(!frame)return fail("Incomplete video: analysis stopped before the end");
            previous=frame;ThumbnailService::Image img;if(!frame_to_thumbnail(*frame.get(),analysisH,img))return fail("Cannot read analysis frame");
            if(i&&(img.width!=data->analysisW||img.height!=data->analysisH))return fail("Video dimensions changed during analysis");
            data->analysisW=img.width;data->analysisH=img.height;
            auto gray=to_gray(img.rgba.data(),img.width,img.height);
            if(points){
                const f32 sx=static_cast<f32>(img.width)/data->sourceW,sy=static_cast<f32>(img.height)/data->sourceH;
                MotionFrame mf;mf.valid=true;mf.confidence=1;mf.inliers=data->pointCount;std::array<Vec2,4> tracked{};
                for(u32 p=0;p<data->pointCount;++p){
                    if(i==0){pos[p]={seeds[p].x*sx,seeds[p].y*sy};lastGood[p]=gray;}
                    else {
                        const auto result=track_step(lastGood[p],gray,pos[p],static_cast<i32>(std::clamp(featureRadius*sy,3.f,16.f)),static_cast<i32>(std::clamp(searchRadius*sy+missing[p]*3,8.f,72.f)),pos[p]+velocity[p]*static_cast<f32>(missing[p]+1));
                        if(result.score<.65f||missing[p]>=15){++missing[p];mf.valid=false;mf.confidence=0;}
                        else {if(missing[p])++data->reacquired;velocity[p]=(result.pos-pos[p])*(1.f/(missing[p]+1));pos[p]=result.pos;lastGood[p]=gray;missing[p]=0;mf.confidence=std::min(mf.confidence,result.score);}
                    }
                    tracked[p]={pos[p].x/sx,pos[p].y/sy};
                }
                if(!mf.valid)++data->lost;
                data->points.push_back(tracked);data->path.push_back(mf);
            }else{
                features.add_frame(gray);
                if(static_cast<u64>(features.tracks().pos.size())*data->sourceUs.size()>kMaxCameraTrackCells)return fail("Analysis memory limit: use a shorter clip");
            }
            j->progress.store(.8f*static_cast<f32>(i+1)/data->sourceUs.size());
        }
        if(!points){
            std::vector<Vec2> polygon;const f32 sx=static_cast<f32>(data->analysisW)/data->sourceW,sy=static_cast<f32>(data->analysisH)/data->sourceH;
            if(data->pointCount==4)for(auto p:seeds)polygon.push_back({p.x*sx,p.y*sy});
            data->path=estimate_path(features.tracks(),data->model,polygon,&j->cancel);
            for(const auto& mf:data->path){std::array<Vec2,4> tracked{};for(u32 p=0;p<data->pointCount;++p){auto q=mf.path.project({seeds[p].x*sx,seeds[p].y*sy});tracked[p]={q.x/sx,q.y/sy};}data->points.push_back(tracked);if(!mf.valid)++data->lost;}
        }
        if(j->cancel.load())return fail("Cancelled");
        if(std::count_if(data->path.begin(),data->path.end(),[](auto& p){return p.valid;})<2)return fail("Not enough reliable motion. Choose a textured area");
        {
            std::lock_guard<std::mutex>guard(modelMutex_);
            auto* comp=project_&&projectSession_==j->session?project_->timeline().composition(j->composition):nullptr;
            auto* layer=comp?comp->layer(LayerId::unpack(j->layer)):nullptr;
            if(!layer||layer->source!=j->source||data->sourceSignature!=source_signature(project_->asset(layer->source))||!timing_matches(*layer,*data,comp->fps()))return fail("Source or timing changed: analyse again");
            layer->motionTrack=data;project_->mark_dirty();
        }
        std::lock_guard<std::mutex>guard(j->mutex);j->result=data;j->progress.store(1);j->state.store(2);
        if(data->lost)j->message="Tracking lost on some frames. Correct the selection and analyse again before applying";
    });
    return true;
}
Engine::MotionTrackStatus Engine::motion_track_status() noexcept {
    MotionTrackStatus s;auto j=motionTrack_;if(!j)return s;
    s.state=j->state.load();s.progress=j->progress.load();std::lock_guard<std::mutex>g(j->mutex);s.message=j->message;s.cropPercent=j->cropPercent;
    if(auto d=j->result){s.tool=static_cast<u32>(d->tool);s.frames=static_cast<u32>(d->path.size());s.lost=d->lost;s.reacquired=d->reacquired;s.memoryBytes=d->memory_bytes();for(auto& p:d->path)if(p.valid){++s.validFrames;s.confidence+=p.confidence;s.errorPx+=p.rms;}if(s.validFrames){s.confidence/=s.validFrames;s.errorPx/=s.validFrames;}}
    return s;
}
u32 Engine::motion_track_features(i64 frame,f32* out,u32 capacity) noexcept {
    auto j=motionTrack_;if(!j||!out||!capacity)return 0;
    std::shared_ptr<const MotionTrackData>d;{std::lock_guard<std::mutex>g(j->mutex);d=j->result;}if(!d)return 0;
    std::lock_guard<std::mutex>g(modelMutex_);auto* comp=project_&&projectSession_==j->session?current_composition():nullptr;
    auto* layer=comp&&project_->timeline().current()==j->composition?comp->layer(LayerId::unpack(j->layer)):nullptr;if(!layer)return 0;
    auto it=std::find(d->localFrames.begin(),d->localFrames.end(),frame-layer->start.value);if(it==d->localFrames.end())return 0;
    const usize k=static_cast<usize>(it-d->localFrames.begin());const auto matrix=layer_comp_matrix(*comp,*layer,FrameIndex{frame});
    const u32 n=std::min(capacity,d->pointCount);for(u32 p=0;p<n;++p){const auto q=projected(matrix,d->points[k][p]);out[p*3]=q.x;out[p*3+1]=q.y;out[p*3+2]=d->path[k].valid?d->path[k].confidence:0;}return n;
}
Result<u64> Engine::apply_motion_track(u64 target,u32 apply,bool lock,f32 smooth,f32 maxScale,u32 crop) noexcept {
    auto j=motionTrack_;if(!j||j->state.load()!=2||apply>5||crop>2||!std::isfinite(smooth)||!std::isfinite(maxScale))return Status{Errc::InvalidState,"No completed analysis"};
    std::shared_ptr<const MotionTrackData>d;{std::lock_guard<std::mutex>g(j->mutex);d=j->result;}
    if(!d||d->lost)return Status{Errc::InvalidState,"Lost frames: correct the selection and analyse again"};
    std::vector<StabilizedFrame> stabilized;
    if(apply==3){if(d->tool!=MotionTool::Stabilizer)return Status{Errc::InvalidArgument,"Use global stabilization analysis"};StabilizationOptions o;o.lock=lock;o.smoothSeconds=std::clamp(smooth,.05f,3.f);o.maxScale=std::clamp(maxScale,1.f,2.f);o.crop=static_cast<CropMode>(crop);stabilized=stabilize_path(d->path,d->analysisW,d->analysisH,d->fps,o);}
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
    struct TargetSample { f32 x=0,y=0,angle=0,sx=1,sy=1; };
    std::vector<TargetSample> targetSamples(samples.size());
    bridge::LayerDetailPOD detail{};if(dst&&!fill_layer_detail_locked(apply==3?j->layer:target,detail))return Status{Errc::InvalidArgument};
    for(usize i=0;i<samples.size();++i){
        const FrameIndex frame{src->start.value+d->localFrames[i]};
        if(apply==1){
            const auto local=dst->local_time(frame);
            auto v=[&](TrackProperty p,f32 fallback){const auto* track=dst->tracks.find(p);return track?track->value_or(local,fallback):fallback;};
            targetSamples[i]={v(TrackProperty::PositionX,dst->transform.position.x),v(TrackProperty::PositionY,dst->transform.position.y),v(TrackProperty::RotationZ,dst->transform.rotation.z),v(TrackProperty::ScaleX,dst->transform.scale.x),v(TrackProperty::ScaleY,dst->transform.scale.y)};
        }
        if(apply==3){const std::array<Vec2,4> corners={Vec2{0,0},Vec2{static_cast<f32>(d->analysisW),0},Vec2{static_cast<f32>(d->analysisW),static_cast<f32>(d->analysisH)},Vec2{0,static_cast<f32>(d->analysisH)}};for(u32 p=0;p<4;++p){auto q=stabilized[i].correction.project(corners[p]);samples[i][p]={q.x*100/d->analysisW,q.y*100/d->analysisH};}}
        else {const auto m=layer_comp_matrix(*comp,*src,frame);Homography inv;if(apply==2&&(!detail.sourceWidth||!detail.sourceHeight||!inverse_affine2(layer_comp_matrix(*comp,*dst,frame),inv)))return Status{Errc::InvalidArgument};
            for(u32 p=0;p<d->pointCount;++p){auto q=projected(m,d->points[i][p]);if(apply==2){q=inv.project(q);q={q.x*100/detail.sourceWidth,q.y*100/detail.sourceHeight};}if(!Tracks2D::present(q))return Status{Errc::InvalidArgument};samples[i][p]=q;}}
    }
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
        for(usize i=0;i<samples.size();++i){auto local=dst->local_time(FrameIndex{src->start.value+d->localFrames[i]});for(u32 p=0;p<4;++p){dst->tracks.find(TrackProperty::EffectParam,eid,param_track_key(p*2,0))->set(local,samples[i][p].x);dst->tracks.find(TrackProperty::EffectParam,eid,param_track_key(p*2+1,0))->set(local,samples[i][p].y);}}
        if(apply==3){f32 peak=1;for(auto& f:stabilized)peak=std::max(peak,f.scale);std::lock_guard<std::mutex>jl(j->mutex);j->cropPercent=(peak-1)*100;}
    }else {
        for(auto p:{TrackProperty::PositionX,TrackProperty::PositionY})(void)dst->tracks.get_or_create(p);
        if(d->pointCount>=2)for(auto p:{TrackProperty::RotationZ,TrackProperty::ScaleX,TrackProperty::ScaleY})(void)dst->tracks.get_or_create(p);
        const auto first=samples[0][1]-samples[0][0];const f32 firstLength=first.length(),firstAngle=std::atan2(first.y,first.x);f32 prevAngle=0;
        for(usize i=0;i<samples.size();++i){auto local=dst->local_time(FrameIndex{src->start.value+d->localFrames[i]});const auto& base=targetSamples[i];
            const auto offset=apply==1?Vec2{base.x-samples[0][0].x,base.y-samples[0][0].y}:Vec2{};
            dst->tracks.find(TrackProperty::PositionX)->set(local,samples[i][0].x+offset.x);dst->tracks.find(TrackProperty::PositionY)->set(local,samples[i][0].y+offset.y);
            if(d->pointCount>=2&&firstLength>1e-4f){auto delta=samples[i][1]-samples[i][0];f32 angle=(std::atan2(delta.y,delta.x)-firstAngle)/kDeg2Rad;while(angle-prevAngle>180)angle-=360;while(angle-prevAngle< -180)angle+=360;prevAngle=angle;dst->tracks.find(TrackProperty::RotationZ)->set(local,base.angle+angle);dst->tracks.find(TrackProperty::ScaleX)->set(local,base.sx*delta.length()/firstLength);dst->tracks.find(TrackProperty::ScaleY)->set(local,base.sy*delta.length()/firstLength);}}
    }
    comp->rebuild_draw_order();project_->mark_dirty();request_render();return destId.pack();
}
} // namespace aurea
