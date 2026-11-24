#include "aurea/Engine.hpp"
#include "aurea/tracking/MotionTrackData.hpp"
#include "aurea/tracking/CameraTrackData.hpp"
#include "aurea/tracking/TrackingValidation.hpp"
#include "../tracking/AnalysisDecode.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace aurea {
using namespace tracking;
struct Engine::MotionTrackJob {
    std::thread thread;
    std::atomic<bool> cancel{false}, finished{true};
    std::atomic<bool> paused{false};
    std::mutex pauseMutex;
    std::condition_variable unpaused;
    std::atomic<u32> state{0};
    std::atomic<f32> progress{0};
    std::mutex mutex;
    std::shared_ptr<const MotionTrackData> result;
    std::shared_ptr<const MotionTrackData> repairBase;
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
    cancel_motion_track();
    if(motionTrack_->thread.joinable())motionTrack_->thread.join();
}
void Engine::cancel_motion_track() noexcept {
    if(!motionTrack_)return;
    {
        std::lock_guard<std::mutex> lock(motionTrack_->pauseMutex);
        motionTrack_->cancel.store(true);
    }
    motionTrack_->unpaused.notify_all();
    if(motionTrack_->state.load()==1)motionTrack_->state.store(4);
}
bool Engine::pause_motion_track(bool paused) noexcept {
    auto job = motionTrack_;
    if (!job || job->state.load() != 1 || job->cancel.load()) return false;
    {
        std::lock_guard<std::mutex> lock(job->pauseMutex);
        job->paused.store(paused);
    }
    if (!paused) job->unpaused.notify_all();
    return true;
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
bool Engine::start_motion_track(u64 id,u32 tool,u32 model,bool backward,const f32* xy,u32 count,
                                f32 featureRadius,f32 searchRadius,f32 minimumConfidence,bool validateBack,
                                i64 repairFirst,i64 repairLast) noexcept {
    if(camera_track_status().state==1)return false;
    if(tool>4||model>3||count>4||(count&&!xy)||!std::isfinite(featureRadius)||!std::isfinite(searchRadius)||
        !std::isfinite(minimumConfidence))return false;
    minimumConfidence = std::clamp(minimumConfidence, .4f, .95f);
    const u32 required=tool==0?1:tool==1?2:tool==4?0:4;
    const bool autoSeeds = tool <= 1 && count == 0;
    const bool repair = repairFirst >= 0;
    if ((count != required && !autoSeeds) || (repair && (count != 4 || backward || repairLast <= repairFirst))) return false;
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
        data->fps=comp->fps();data->sourceW=a->video.width;data->sourceH=a->video.height;data->pointCount=required;
        data->sourceSignature=source_signature(a);
        for(u32 i=0;i<count;++i){seeds[i]={xy[2*i],xy[2*i+1]};if(!Tracks2D::present(seeds[i])||seeds[i].x<0||seeds[i].y<0||seeds[i].x>=data->sourceW||seeds[i].y>=data->sourceH)return false;}
        if(count==2&&(seeds[1]-seeds[0]).length()<8)return false;
        if(count==4){Homography h;if(!quad_map(seeds,h))return false;}
        if (repair) {
            const auto& original = layer->motionTrack;
            if (layer->locked || !original || original->pointCount != 4 || original->tool != data->tool ||
                !original->analysisW || !original->analysisH ||
                original->sourceSignature != data->sourceSignature || !timing_matches(*layer, *original, comp->fps()) ||
                original->points.size() != original->path.size() ||
                repairFirst < layer->start.value || repairLast >= layer->end.value ||
                repairLast - repairFirst >= kMaxMotionTrackFrames) return false;
            const auto [lo, hi] = std::minmax(original->localFrames.front(), original->localFrames.back());
            if (repairFirst - layer->start.value < lo || repairLast - layer->start.value > hi) return false;
            job->repairBase = original;
            data->model = original->model;
        }
        const i64 first=repair?repairFirst:tool==4?layer->start.value:std::clamp(playback_.current().value,layer->start.value,layer->end.value-1);
        const i64 lastExclusive = repair ? repairLast + 1 : layer->end.value;
        const i64 step=backward&&tool!=4?-1:1;
        // Clipe mais longo que o limite: analisa até o limite (não recusa).
        for(i64 f=first;f>=layer->start.value&&f<lastExclusive&&data->localFrames.size()<kMaxMotionTrackFrames;f+=step){
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
    job->thread=std::thread([this,j=job.get(),data,asset,seeds,autoSeeds,featureRadius,searchRadius,minimumConfidence,validateBack,factory]{
        struct Done{MotionTrackJob* j;~Done(){j->finished.store(true);}}done{j};
        auto fail=[&](const char* text){std::lock_guard<std::mutex>g(j->mutex);j->message=text;j->state.store(j->cancel.load()?4:3);};
        if(!factory)return fail("Decoder unavailable");
        // Decoder PRÓPRIO (como o export): nunca disputa o do preview.
        auto decoder=factory->open_video(asset,MediaPriority::Thumbnail);if(!decoder)return fail("Cannot decode video");
        const bool points=data->pointCount>0&&data->pointCount<3;
        // Bound both dimensions, including ultra-wide videos. Ponto/dois pontos
        // só olham um bloco em volta do detalhe: analisam a até 720 linhas
        // (precisão subpixel na fonte); o movimento global fica em 360.
        // A small quad would otherwise get only a few pixels at 360p.
        f32 area = 0.0f;
        if (data->pointCount == 4) {
            for (u32 i = 0; i < 4; ++i) {
                const auto a = seeds[i], b = seeds[(i + 1) % 4];
                area += a.x * b.y - a.y * b.x;
            }
        }
        const bool smallPlane = data->pointCount == 4 &&
            std::abs(area) < .20f * static_cast<f32>(data->sourceW) * data->sourceH;
        const u32 analysisH=points
            ?std::max(1u,std::min({720u,data->sourceH,static_cast<u32>(1280ull*data->sourceH/data->sourceW)}))
            :std::max(1u,std::min({smallPlane ? 540u : 360u,data->sourceH,
                static_cast<u32>((smallPlane ? 960ull : 720ull)*data->sourceH/data->sourceW)}));
        AnalysisGraySequence sequence(*decoder, asset.timebaseFps, j->cancel, data->sourceUs,
                                      analysisH, data->sourceW, data->sourceH);
        // px da fonte <-> px da análise, pelos CENTROS de pixel (redução por área).
        f32 kx=1,ky=1;
        auto toA=[&](Vec2 s){return Vec2{(s.x+.5f)/kx-.5f,(s.y+.5f)/ky-.5f};};
        auto toS=[&](Vec2 a){return Vec2{(a.x+.5f)*kx-.5f,(a.y+.5f)*ky-.5f};};
        // Ponto: bloco do quadro escolhido (sem deriva), pesos robustos.
        std::array<TemplateTracker,2> trackers;std::array<Vec2,2> pos{},velocity{};std::array<u32,2> missing{};
        // Estabilizador / planar: trechos de memória limitada, encadeados.
        constexpr u32 kSegment=240;
        FeatureTracker features(TrackMode::Balanced);usize segmentStart=0;
        std::vector<Vec2> searchRegion;
        std::vector<f32> motionX, motionY;
        auto region_at = [&](const Homography& base) {
            std::vector<Vec2> region;
            if (data->pointCount == 4) {
                region.reserve(4);
                for (auto seed : seeds) {
                    const auto point = base.project(toA(seed));
                    if (!Tracks2D::present(point)) return std::vector<Vec2>{};
                    region.push_back(point);
                }
            }
            return region;
        };
        auto flush=[&]()->bool{
            const Homography base=segmentStart<data->path.size()?data->path[segmentStart].path:Homography{};
            const bool baseValid=segmentStart==0||(segmentStart<data->path.size()&&data->path[segmentStart].valid);
            const auto polygon = region_at(base);
            if (data->pointCount == 4 && polygon.empty()) return false;
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
            if (j->paused.load()) {
                std::unique_lock<std::mutex> lock(j->pauseMutex);
                j->unpaused.wait(lock, [&] { return j->cancel.load() || !j->paused.load(); });
            }
            if(j->cancel.load())return fail("Cancelled");
            Gray gray;
            const auto picked = sequence.read(i, gray);
            if (picked == AnalysisGraySequence::Result::Cancelled) return fail("Cancelled");
            if (picked != AnalysisGraySequence::Result::Ok) return fail("Incomplete video: analysis stopped before the end");
            if(i==0){data->analysisW=gray.width;data->analysisH=gray.height;kx=static_cast<f32>(data->sourceW)/gray.width;ky=static_cast<f32>(data->sourceH)/gray.height;}
            else if(gray.width!=data->analysisW||gray.height!=data->analysisH)return fail("Video dimensions changed during analysis");
            if(points){
                MotionFrame mf;mf.valid=true;mf.confidence=1;mf.inliers=data->pointCount;std::array<Vec2,4> tracked{};
                // Raio do painel = bloco MÍNIMO; o bloco cresce até conter o
                // objeto tocado (escala característica), até 4× o mínimo.
                const i32 minHalf=std::max(3,static_cast<i32>(std::lround(featureRadius/ky)));
                const i32 maxHalf=std::max(minHalf,std::min(4*minHalf,static_cast<i32>(std::min(gray.width,gray.height)/6)));
                std::array<Vec2, 2> detected{};
                if (i == 0 && autoSeeds) {
                    FeatureTracker detector(TrackMode::Fast);
                    detector.add_frame(gray);
                    u32 found = 0;
                    for (const auto& track : detector.tracks().pos) {
                        const Vec2 p = track.front();
                        if (!Tracks2D::present(p) || (found != 0 && (p - detected[0]).length() < 24.f)) continue;
                        detected[found++] = p;
                        if (found == data->pointCount) break;
                    }
                    if (found != data->pointCount) return fail("Not enough reliable motion. Choose a textured area");
                }
                const TemplateFrame analysis(std::move(gray));
                for(u32 p=0;p<data->pointCount;++p){
                    if(i==0){pos[p]=autoSeeds?detected[p]:toA(seeds[p]);trackers[p].start(analysis,pos[p],feature_half_at(*analysis.full,pos[p],minHalf,maxHalf));}
                    else {
                        const i32 radius=static_cast<i32>(std::clamp(searchRadius/ky+missing[p]*3,4.f,160.f));
                        // Depois de muito tempo sumido, reencontrar exige um casamento melhor.
                        const f32 accept=missing[p]>15?std::max(.75f,minimumConfidence):minimumConfidence;
                        const auto result=trackers[p].track(analysis,pos[p]+velocity[p]*static_cast<f32>(missing[p]+1),
                                                           radius,accept,validateBack && missing[p] == 0);
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
                if (i == 0) searchRegion = region_at(Homography{});
                features.add_frame(gray, searchRegion);
                if (!searchRegion.empty() && features.tracks().frames >= 2) {
                    motionX.clear();
                    motionY.clear();
                    for (const auto& row : features.tracks().pos) {
                        const Vec2 a = row[row.size() - 2], b = row.back();
                        if (!Tracks2D::present(a) || !Tracks2D::present(b)) continue;
                        motionX.push_back(b.x - a.x); motionY.push_back(b.y - a.y);
                    }
                    if (motionX.size() >= 4) {
                        const auto median = [](std::vector<f32>& values) {
                            const auto middle = values.begin() + values.size() / 2;
                            std::nth_element(values.begin(), middle, values.end());
                            return *middle;
                        };
                        const Vec2 advance{median(motionX), median(motionY)};
                        for (auto& corner : searchRegion) corner = corner + advance;
                    }
                }
                if(static_cast<u64>(features.tracks().pos.size())*features.tracks().frames>kMaxCameraTrackCells)return fail("Analysis memory limit: use a smaller area");
                if(features.tracks().frames>kSegment||i+1==total){
                    if(!flush())return fail(j->cancel.load()?"Cancelled":"Not enough reliable motion. Choose a textured area");
                    if (i + 1 < total) {
                        features = FeatureTracker(TrackMode::Balanced);
                        searchRegion = region_at(data->path.back().path);
                        features.add_frame(gray, searchRegion);
                        segmentStart = i;
                    }
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
        if (j->repairBase) {
            const auto& original = *j->repairBase;
            auto merged = std::make_shared<MotionTrackData>(original);
            const bool reverse = original.localFrames.front() > original.localFrames.back();
            const auto analysis_quad = [&](const std::array<Vec2, 4>& points) {
                auto quad = points;
                for (auto& p : quad) {
                    p.x = (p.x + .5f) * original.analysisW / original.sourceW - .5f;
                    p.y = (p.y + .5f) * original.analysisH / original.sourceH - .5f;
                }
                return quad;
            };
            Homography canonical, inverse;
            if (!quad_map(analysis_quad(original.points.front()), canonical) || !canonical.inverse(inverse))
                return fail("Invalid reference quad");
            for (usize i = 0; i < data->path.size(); ++i) {
                const i64 frame = data->localFrames[i];
                const i64 offset = reverse ? original.localFrames.front() - frame : frame - original.localFrames.front();
                if (offset < 0 || static_cast<usize>(offset) >= original.path.size() ||
                    original.localFrames[static_cast<usize>(offset)] != frame) return fail("Source or timing changed: analyse again");
                if (!data->path[i].valid) continue;
                Homography mapped;
                if (!quad_map(analysis_quad(data->points[i]), mapped)) continue;
                auto& cell = merged->path[static_cast<usize>(offset)];
                cell = data->path[i];
                cell.path = mapped * inverse;
                cell.rms *= static_cast<f32>(original.analysisH) / std::max(1u, data->analysisH);
                merged->points[static_cast<usize>(offset)] = data->points[i];
            }
            merged->lost = static_cast<u32>(std::count_if(merged->path.begin(), merged->path.end(),
                [](const MotionFrame& cell) { return !cell.valid; }));
            std::lock_guard<std::mutex> guard(j->mutex);
            if (j->cancel.load()) { j->state.store(4); return; }
            j->result = std::move(merged);
            j->progress.store(1);
            j->state.store(5);
            return;
        }
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

bool Engine::finish_motion_repair(bool accept) noexcept {
    auto job = motionTrack_;
    if (!job || !job->repairBase || job->state.load() != 5) return false;
    std::lock_guard<std::mutex> model(modelMutex_);
    std::lock_guard<std::mutex> result(job->mutex);
    auto* comp = project_ && projectSession_ == job->session && project_->timeline().current() == job->composition
        ? current_composition() : nullptr;
    auto* layer = comp ? comp->layer(LayerId::unpack(job->layer)) : nullptr;
    if (!layer || (accept && layer->locked) || layer->source != job->source || layer->motionTrack != job->repairBase ||
        job->repairBase->sourceSignature != source_signature(project_->asset(layer->source)) ||
        !timing_matches(*layer, *job->repairBase, comp->fps())) return false;
    if (accept) {
        history_.before_mutation(*comp, comp->id(), "corrigir trecho do rastreio");
        layer->motionTrack = job->result;
        comp->touch();
        project_->mark_dirty();
        bump_model_revision_locked();
        request_render();
    } else {
        job->result = job->repairBase;
    }
    job->state.store(2);
    return true;
}
Engine::MotionTrackStatus Engine::motion_track_status() noexcept {
    MotionTrackStatus s;auto j=motionTrack_;if(!j)return s;
    s.state=j->state.load();s.progress=j->progress.load();s.paused=s.state==1&&j->paused.load();std::lock_guard<std::mutex>g(j->mutex);s.message=j->message;s.cropPercent=j->cropPercent;
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
    if (!layer || layer->source != j->source ||
        (layer->motionTrack != d && !(j->state.load() == 5 && layer->motionTrack == j->repairBase)) ||
        d->sourceSignature != source_signature(project_->asset(layer->source)) ||
        d->points.size() != d->path.size() || d->localFrames.empty() ||
        d->localFrames.size() != d->path.size() || d->sourceUs.size() != d->path.size() ||
        frame < layer->start.value || frame >= layer->end.value) return 0;
    // The live overlay only reads this frame. Apply still validates all samples.
    const i64 local = frame - layer->start.value;
    const bool reversedOrder = d->localFrames.front() > d->localFrames.back();
    const i64 delta = reversedOrder ? d->localFrames.front() - local : local - d->localFrames.front();
    if (delta < 0 || static_cast<usize>(delta) >= d->path.size()) return 0;
    const usize k = static_cast<usize>(delta);
    const f64 fps = comp->fps();
    if (d->localFrames[k] != local || !std::isfinite(fps) || fps <= 0 ||
        d->sourceUs[k] != static_cast<i64>(std::llround(std::max(0.0, layer->source_frame(FrameIndex{frame})) * 1e6 / fps))) return 0;
    const auto matrix = layer_comp_matrix(*comp, *layer, FrameIndex{frame});
    const u32 n=std::min(capacity,d->pointCount);for(u32 p=0;p<n;++p){const auto q=projected(matrix,d->points[k][p]);out[p*3]=q.x;out[p*3+1]=q.y;out[p*3+2]=d->path[k].valid?d->path[k].confidence:0;}return n;
}

std::vector<f32> Engine::motion_track_review(u64 source, u32 buckets) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const auto* comp = project_ ? current_composition() : nullptr;
    const auto* layer = comp ? comp->layer(LayerId::unpack(source)) : nullptr;
    const auto data = layer ? layer->motionTrack : nullptr;
    if (!data || data->sourceSignature != source_signature(project_->asset(layer->source)) ||
        !timing_matches(*layer, *data, comp->fps())) return {};
    const usize n = data->path.size();
    const bool descending = data->localFrames.front() > data->localFrames.back();
    const usize cells = std::min<usize>(n, std::clamp(buckets, 1u, 240u));
    std::vector<f32> out(2 + cells, 1.f);
    out[0] = static_cast<f32>(layer->start.value + (descending ? data->localFrames.back() : data->localFrames.front()));
    out[1] = static_cast<f32>(layer->start.value + (descending ? data->localFrames.front() : data->localFrames.back()));
    for (usize i = 0; i < n; ++i) {
        const auto& observation = data->path[descending ? n - i - 1 : i];
        auto& confidence = out[2 + i * cells / n];
        confidence = std::min(confidence, observation.valid ? observation.confidence : -1.f);
    }
    return out;
}

std::vector<f32> Engine::motion_track_trail(u64 source, i64 frame) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const auto* comp = project_ ? current_composition() : nullptr;
    const auto* layer = comp ? comp->layer(LayerId::unpack(source)) : nullptr;
    const auto data = layer ? layer->motionTrack : nullptr;
    if (!data || data->path.size() != data->points.size() || data->localFrames.empty() ||
        data->localFrames.size() != data->path.size() || data->sourceUs.size() != data->path.size() || !data->pointCount ||
        frame < layer->start.value || frame >= layer->end.value ||
        data->sourceSignature != source_signature(project_->asset(layer->source))) return {};
    const bool descending = data->localFrames.front() > data->localFrames.back();
    const i64 origin = data->localFrames.front();
    std::vector<f32> out;
    for (i64 f = std::max(layer->start.value, frame - 32); f <= frame; ++f) {
        const i64 local = f - layer->start.value;
        const i64 k = descending ? origin - local : local - origin;
        if (k < 0 || k >= static_cast<i64>(data->path.size())) continue;
        if (data->localFrames[k] != local ||
            data->sourceUs[k] != static_cast<i64>(std::llround(std::max(0.0, layer->source_frame(FrameIndex{f})) * 1e6 / comp->fps())))
            return {};
        const auto matrix = layer_comp_matrix(*comp, *layer, FrameIndex{f});
        for (u32 p = 0; p < data->pointCount; ++p) {
            const Vec2 pos = projected(matrix, data->points[k][p]);
            const f32 confidence = data->path[k].valid && Tracks2D::present(pos) ? data->path[k].confidence : 0;
            out.insert(out.end(), {static_cast<f32>(p), confidence > 0 ? pos.x : 0, confidence > 0 ? pos.y : 0, confidence});
        }
    }
    return out;
}

std::string Engine::motion_track_csv(u64 source) noexcept {
    std::shared_ptr<const MotionTrackData> data;
    i64 first = 0;
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        const auto* comp = project_ ? current_composition() : nullptr;
        const auto* layer = comp ? comp->layer(LayerId::unpack(source)) : nullptr;
        data = layer ? layer->motionTrack : nullptr;
        if (!data || data->sourceSignature != source_signature(project_->asset(layer->source)) ||
            !timing_matches(*layer, *data, comp->fps()) || data->points.size() != data->path.size()) return {};
        first = layer->start.value;
    }
    std::ostringstream csv;
    csv.imbue(std::locale::classic());
    csv.precision(std::numeric_limits<f32>::max_digits10);
    csv << "timeline_frame,source_us,valid,confidence";
    for (u32 p = 0; p < data->pointCount; ++p) csv << ",point" << p + 1 << "_x_px,point" << p + 1 << "_y_px";
    for (u32 row = 0; row < 3; ++row) for (u32 col = 0; col < 3; ++col) csv << ",h" << row << col;
    csv << '\n';
    const usize n = data->path.size();
    const bool descending = data->localFrames.front() > data->localFrames.back();
    const bool usePoints = data->pointCount > 0 && data->pointCount < 3;
    const auto pointTransforms = usePoints ? point_path(*data) : std::vector<MotionFrame>{};
    const f64 sx = static_cast<f64>(data->sourceW) / std::max(1u, data->analysisW);
    const f64 sy = static_cast<f64>(data->sourceH) / std::max(1u, data->analysisH);
    const Homography toSource{{sx, 0, (sx - 1) * .5, 0, sy, (sy - 1) * .5, 0, 0, 1}};
    Homography toAnalysis;
    const bool invertible = toSource.inverse(toAnalysis);
    for (usize i = 0; i < n; ++i) {
        const usize k = descending ? n - i - 1 : i;
        const auto& observation = data->path[k];
        csv << first + data->localFrames[k] << ',' << data->sourceUs[k] << ','
            << (observation.valid ? "1" : "0") << ',';
        if (observation.valid) csv << observation.confidence;
        for (u32 p = 0; p < data->pointCount; ++p) {
            csv << ',';
            if (observation.valid) csv << data->points[k][p].x;
            csv << ',';
            if (observation.valid) csv << data->points[k][p].y;
        }
        const Homography transform = usePoints ? pointTransforms[k].path : toSource * observation.path * toAnalysis;
        for (f64 value : transform.m) {
            csv << ',';
            if (observation.valid && (usePoints ? pointTransforms[k].valid : invertible)) csv << value;
        }
        csv << '\n';
    }
    return std::move(csv).str();
}

u32 Engine::apply_landmark_tracks(u64 source, const f32* rows, u32 frames, bool face) noexcept {
    constexpr u32 stride = 2 + 33 * 4;
    if (!rows || frames < 2 || frames > 9000) return 0;
    for (u32 i = 0; i < frames; ++i) {
        const f32 time = rows[i * stride];
        if (!std::isfinite(time) || time < 0 || (i > 0 && time <= rows[(i - 1) * stride])) return 0;
    }
    struct Joint { u32 slot; const char* name; };
    constexpr Joint faceJoints[]{{2, "Left eye"}, {5, "Right eye"}, {0, "Nose"}, {9, "Mouth"}, {33, "Head pose"}};
    constexpr Joint bodyJoints[]{{0, "Head"}, {11, "Left shoulder"}, {12, "Right shoulder"},
        {13, "Left elbow"}, {14, "Right elbow"}, {15, "Left wrist"}, {16, "Right wrist"},
        {23, "Left hip"}, {24, "Right hip"}, {25, "Left knee"}, {26, "Right knee"},
        {27, "Left ankle"}, {28, "Right ankle"}};
    const auto joints = face ? std::span<const Joint>(faceJoints) : std::span<const Joint>(bodyJoints);
    struct Sample { FrameIndex frame; Vec2 point; f32 angle = 0, span = 0; };
    std::vector<std::vector<Sample>> paths(joints.size());
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, source, true);
    auto* comp = project_ ? current_composition() : nullptr;
    auto* layer = comp ? comp->layer(LayerId::unpack(source)) : nullptr;
    const auto* asset = layer ? project_->asset(layer->source) : nullptr;
    if (!asset || layer->kind != LayerKind::Video || layer->locked ||
        !asset->video.width || !asset->video.height || !std::isfinite(comp->fps()) || comp->fps() <= 0 ||
        layer->duration().value > kMaxMotionTrackFrames) return 0;
    const auto read = [&](const f32* row, u32 joint, Vec2& point) {
        const f32* p = row + 2 + joint * 4;
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[3]) ||
            p[3] < .52f || p[0] < 0 || p[0] > 1 || p[1] < 0 || p[1] > 1) return false;
        point = {p[0] * asset->video.width, p[1] * asset->video.height};
        return true;
    };
    // Resolve source timestamps through the layer timing before making keys.
    for (i64 f = layer->start.value; f < layer->end.value; ++f) {
        const f64 seconds = layer->source_frame(FrameIndex{f}) / comp->fps();
        if (!std::isfinite(seconds)) continue;
        u32 left = 0, right = frames;
        while (left < right) {
            const u32 mid = left + (right - left) / 2;
            if (rows[mid * stride] < seconds) left = mid + 1;
            else right = mid;
        }
        u32 nearest = std::min(left, frames - 1);
        if (nearest > 0 && std::abs(rows[(nearest - 1) * stride] - seconds) <= std::abs(rows[nearest * stride] - seconds)) --nearest;
        const f32* row = rows + nearest * stride;
        if (std::abs(row[0] - seconds) > .05) continue;
        const auto matrix = layer_comp_matrix(*comp, *layer, FrameIndex{f});
        for (usize j = 0; j < joints.size(); ++j) {
            Vec2 p, other;
            if (joints[j].slot == 33) {
                Vec2 a, b;
                if (!read(row, 2, a) || !read(row, 5, b)) continue;
                a = projected(matrix, a);
                b = projected(matrix, b);
                if (!Tracks2D::present(a) || !Tracks2D::present(b)) continue;
                const auto eye = a.x < b.x ? b - a : a - b;
                if (eye.length_sq() < 4) continue;
                paths[j].push_back({FrameIndex{f}, (a + b) * .5f, std::atan2(eye.y, eye.x) / kDeg2Rad, eye.length()});
                continue;
            }
            if (!read(row, joints[j].slot, p)) continue;
            if (face && joints[j].slot == 9) {
                if (!read(row, 10, other)) continue;
                p = (p + other) * .5f;
            }
            const Vec2 point = projected(matrix, p);
            if (Tracks2D::present(point)) paths[j].push_back({FrameIndex{f}, point});
        }
    }
    const auto count = static_cast<u32>(std::count_if(paths.begin(), paths.end(), [](const auto& p) { return p.size() >= 2; }));
    if (count == 0 || comp->layers().count() + count > kMaxLayerCount) return 0;
    const FrameIndex start = layer->start, end = layer->end;
    history_.before_mutation(*comp, project_->timeline().current(), face ? "Track face" : "Track body");
    u32 created = 0;
    for (usize j = 0; j < joints.size(); ++j) {
        if (paths[j].size() < 2) continue;
        const LayerId id = comp->add_layer(LayerKind::Null, joints[j].name);
        Layer* joint = comp->layer(id);
        if (!joint) continue;
        ++created;
        joint->start = start;
        joint->end = end;
        joint->transform.position = {paths[j].front().point.x, paths[j].front().point.y, 0};
        for (const auto property : {TrackProperty::PositionX, TrackProperty::PositionY}) {
            auto& track = joint->tracks.get_or_create(property);
            for (const auto& sample : paths[j]) {
                const auto local = joint->local_time(sample.frame);
                (void)track.set(local, property == TrackProperty::PositionX ? sample.point.x : sample.point.y);
            }
        }
        if (joints[j].slot == 33) {
            const auto& seed = paths[j].front();
            for (const auto property : {TrackProperty::RotationZ, TrackProperty::ScaleX, TrackProperty::ScaleY}) {
                auto& track = joint->tracks.get_or_create(property);
                for (const auto& sample : paths[j]) {
                    const f32 value = property == TrackProperty::RotationZ ? sample.angle - seed.angle : sample.span / seed.span;
                    (void)track.set(joint->local_time(sample.frame), value);
                }
            }
        }
    }
    bump_model_revision_locked();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return created;
}

Result<u64> Engine::apply_motion_track(u64 target,u32 apply,bool lock,f32 smooth,f32 maxScale,u32 crop) noexcept {
    auto j=motionTrack_;if(!j||j->state.load()!=2||apply>6||crop>2||!std::isfinite(smooth)||!std::isfinite(maxScale))return Status{Errc::InvalidState,"No completed analysis"};
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
    if (apply == 6) {
        if (src->locked || d->pointCount != 4 || src->masks.size() >= kMaxMaskCount)
            return Status{Errc::InvalidArgument, "Tracked mask requires four corners and an editable source"};
        Mask mask;
        mask.name = "Tracking";
        for (usize i = 0; i < d->path.size(); ++i) {
            if (!d->path[i].valid) continue;
            MaskPathKey key;
            key.frame = src->local_time(FrameIndex{src->start.value + d->localFrames[i]}).value;
            for (Vec2 point : d->points[i]) {
                if (!Tracks2D::present(point)) return Status{Errc::InvalidArgument};
                key.points.push_back({point, {}, {}});
            }
            mask.pathKeys.push_back(std::move(key));
        }
        std::sort(mask.pathKeys.begin(), mask.pathKeys.end(), [](const auto& a, const auto& b) { return a.frame < b.frame; });
        mask.points = mask.pathKeys.front().points;
        history_.before_mutation(*comp, j->composition, "Apply tracked mask");
        mask.id = src->alloc_mask_id();
        src->masks.push_back(std::move(mask));
        bump_model_revision_locked();
        comp->touch();
        project_->mark_dirty();
        request_render();
        return j->layer;
    }
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
    history_.before_mutation(*comp,project_->timeline().current(),apply==3?"Stabilize video":"Apply motion tracking");bump_model_revision_locked();
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
