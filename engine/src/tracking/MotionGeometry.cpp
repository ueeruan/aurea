#include "aurea/tracking/MotionGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace aurea::tracking {
Vec2 Homography::project(Vec2 p) const noexcept {
    const f64 w = m[6]*p.x + m[7]*p.y + m[8];
    if (!std::isfinite(w) || std::abs(w) < 1e-10) return {NAN, NAN};
    return {static_cast<f32>((m[0]*p.x+m[1]*p.y+m[2])/w),
            static_cast<f32>((m[3]*p.x+m[4]*p.y+m[5])/w)};
}
bool Homography::inverse(Homography& o) const noexcept {
    const auto& a = m;
    const f64 det = a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    if (!std::isfinite(det) || std::abs(det) < 1e-12) return false;
    o.m = {(a[4]*a[8]-a[5]*a[7])/det,(a[2]*a[7]-a[1]*a[8])/det,(a[1]*a[5]-a[2]*a[4])/det,
           (a[5]*a[6]-a[3]*a[8])/det,(a[0]*a[8]-a[2]*a[6])/det,(a[2]*a[3]-a[0]*a[5])/det,
           (a[3]*a[7]-a[4]*a[6])/det,(a[1]*a[6]-a[0]*a[7])/det,(a[0]*a[4]-a[1]*a[3])/det};
    return true;
}
Homography Homography::operator*(const Homography& b) const noexcept {
    Homography o; o.m.fill(0);
    for (int r=0;r<3;++r) for(int c=0;c<3;++c) for(int k=0;k<3;++k) o.m[r*3+c]+=m[r*3+k]*b.m[k*3+c];
    if (std::abs(o.m[8]) > 1e-12) { const f64 s=o.m[8]; for(auto& v:o.m) v/=s; }
    return o;
}
bool quad_map(const std::array<Vec2,4>& p,Homography& h) noexcept {
    f64 sign=0;
    for(int i=0;i<4;++i) {
        if(!Tracks2D::present(p[i]))return false;
        const auto a=p[i],b=p[(i+1)%4],c=p[(i+2)%4];
        const f64 cross=(b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x);
        if(std::abs(cross)<1e-8 || (i && cross*sign<=0))return false;
        sign=cross;
    }
    const f64 dx1=p[1].x-p[2].x,dx2=p[3].x-p[2].x,dx3=p[0].x-p[1].x+p[2].x-p[3].x;
    const f64 dy1=p[1].y-p[2].y,dy2=p[3].y-p[2].y,dy3=p[0].y-p[1].y+p[2].y-p[3].y;
    f64 g=0,k=0;
    if(std::abs(dx3)+std::abs(dy3)>1e-10) {
        const f64 det=dx1*dy2-dx2*dy1;if(std::abs(det)<1e-12)return false;
        g=(dx3*dy2-dx2*dy3)/det;k=(dx1*dy3-dx3*dy1)/det;
    }
    h.m={p[1].x-p[0].x+g*p[1].x,p[3].x-p[0].x+k*p[3].x,p[0].x,
         p[1].y-p[0].y+g*p[1].y,p[3].y-p[0].y+k*p[3].y,p[0].y,g,k,1};
    return std::min({1.0,1+g,1+k,1+g+k})>1e-8;
}
namespace {
bool solve8(f64 a[8][9], f64* x) {
    for(int k=0;k<8;++k) {
        int pivot=k; for(int i=k+1;i<8;++i) if(std::abs(a[i][k])>std::abs(a[pivot][k])) pivot=i;
        if(std::abs(a[pivot][k])<1e-11) return false;
        for(int j=k;j<9;++j) std::swap(a[k][j],a[pivot][j]);
        const f64 d=a[k][k]; for(int j=k;j<9;++j) a[k][j]/=d;
        for(int i=0;i<8;++i) if(i!=k) { const f64 v=a[i][k]; for(int j=k;j<9;++j) a[i][j]-=v*a[k][j]; }
    }
    for(int i=0;i<8;++i) x[i]=a[i][8];
    return true;
}
bool fit(const std::vector<Vec2>& a,const std::vector<Vec2>& b,const std::vector<u32>& ids,MotionModel model,Homography& h) {
    if(ids.empty()) return false;
    f64 ax=0,ay=0,bx=0,by=0;
    for(u32 i:ids) {ax+=a[i].x;ay+=a[i].y;bx+=b[i].x;by+=b[i].y;}
    const f64 n=static_cast<f64>(ids.size()); ax/=n;ay/=n;bx/=n;by/=n;
    if(model==MotionModel::Position) { h.m={1,0,bx-ax,0,1,by-ay,0,0,1}; return true; }
    f64 aa=0,bb=0,re=0,im=0;
    for(u32 i:ids) {const f64 x=a[i].x-ax,y=a[i].y-ay,u=b[i].x-bx,v=b[i].y-by;aa+=x*x+y*y;bb+=u*u+v*v;re+=x*u+y*v;im+=x*v-y*u;}
    if(aa<1e-8 || bb<1e-8) return false;
    if(model==MotionModel::Similarity) {
        const f64 c=re/aa,s=im/aa;
        h.m={c,-s,bx-c*ax+s*ay,s,c,by-s*ax-c*ay,0,0,1};
        return std::hypot(c,s)>0.05 && std::hypot(c,s)<20;
    }
    // Hartley normalization keeps the DLT normal equations well conditioned
    // even for 4K coordinates and small tracking regions.
    const f64 ka=std::sqrt(2*n/aa),kb=std::sqrt(2*n/bb);
    f64 system[8][9]{};
    for(u32 i:ids) {
        const f64 x=(a[i].x-ax)*ka,y=(a[i].y-ay)*ka,u=(b[i].x-bx)*kb,v=(b[i].y-by)*kb;
        const f64 rows[2][9]={{x,y,1,0,0,0,-u*x,-u*y,u},{0,0,0,x,y,1,-v*x,-v*y,v}};
        for(const auto& row:rows) for(int r=0;r<8;++r) for(int c=0;c<9;++c) system[r][c]+=row[r]*row[c];
    }
    f64 x[8]; if(!solve8(system,x)) return false;
    Homography normalized; for(int k=0;k<8;++k) normalized.m[k]=x[k];
    Homography ta,tbi;ta.m={ka,0,-ka*ax,0,ka,-ka*ay,0,0,1};tbi.m={1/kb,0,bx,0,1/kb,by,0,0,1};
    h=tbi*normalized*ta;
    Homography inv; return h.inverse(inv);
}
f64 error2(const Homography& h,Vec2 a,Vec2 b) { const auto p=h.project(a); return std::isfinite(p.x)&&std::isfinite(p.y)?std::pow(p.x-b.x,2)+std::pow(p.y-b.y,2):1e30; }
bool inside(Vec2 p,const std::vector<Vec2>& polygon) {
    if(polygon.empty()) return true;
    if(polygon.size()<3 || !Tracks2D::present(p)) return false;
    bool yes=false;
    for(usize i=0,j=polygon.size()-1;i<polygon.size();j=i++) {
        const auto a=polygon[i],b=polygon[j];
        if((a.y>p.y)!=(b.y>p.y) && p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x) yes=!yes;
    }
    return yes;
}
Homography blend(const Homography& h,f64 strength) {
    Homography o;
    for(int k=0;k<9;++k) o.m[k]+=strength*(h.m[k]-o.m[k]);
    return o;
}
Homography zoom(f64 s,f64 w,f64 h) { Homography o;o.m={s,0,(1-s)*w*.5,0,s,(1-s)*h*.5,0,0,1};return o; }
bool covered(const Homography& h,f64 s,u32 w,u32 height) {
    Homography inv;
    if(!(zoom(s,w,height)*h).inverse(inv)) return false;
    f64 denominatorSign=0;
    for(Vec2 p:{Vec2{0,0},Vec2{static_cast<f32>(w),0},Vec2{static_cast<f32>(w),static_cast<f32>(height)},Vec2{0,static_cast<f32>(height)}}) {
        const f64 denominator=inv.m[6]*p.x+inv.m[7]*p.y+inv.m[8];
        if(std::abs(denominator)<1e-8 || (denominatorSign && denominator*denominatorSign<=0))return false;
        denominatorSign=denominator;
        const Vec2 q=inv.project(p);
        if(!Tracks2D::present(q)||q.x < -0.001f ||q.y < -0.001f ||q.x>w+0.001f ||q.y>height+0.001f) return false;
    }
    return true;
}
}

MotionEstimate estimate_motion(const std::vector<Vec2>& from,const std::vector<Vec2>& to,MotionModel model,f32 threshold) {
    MotionEstimate best;
    if(from.size()!=to.size() || from.size()<4 || !std::isfinite(threshold) || threshold<=0) return best;
    if(model==MotionModel::Auto) {
        auto simple=estimate_motion(from,to,MotionModel::Similarity,threshold);
        auto perspective=estimate_motion(from,to,MotionModel::Perspective,threshold);
        // Prefer the simpler model unless perspective gains substantial support
        // or error reduction; this prevents a noisy background from wobbling.
        if(perspective.valid && (!simple.valid || perspective.count>simple.count*1.15 ||
            (perspective.count>=simple.count && perspective.rms+0.2f<simple.rms*.75f))) return perspective;
        return simple;
    }
    std::vector<u32> valid;
    for(u32 i=0;i<from.size();++i) if(Tracks2D::present(from[i])&&Tracks2D::present(to[i])) valid.push_back(i);
    const usize minimal=model==MotionModel::Position?1:model==MotionModel::Similarity?2:4;
    const usize support=std::max<usize>(minimal*2,static_cast<usize>(std::ceil(valid.size()*.25)));
    if(valid.size()<std::max<usize>(4,support)) return best;
    std::mt19937 random(0x4d4f544eu);
    f64 bestError=1e30;const f64 limit=threshold*threshold;
    std::vector<u32> selected(minimal),inliers;
    for(int iteration=0;iteration<320;++iteration) {
        for(usize k=0;k<minimal;++k) {
            do { selected[k]=valid[random()%valid.size()]; } while(std::find(selected.begin(),selected.begin()+k,selected[k])!=selected.begin()+k);
        }
        Homography h; if(!fit(from,to,selected,model,h)) continue;
        inliers.clear();f64 error=0;
        for(u32 i:valid) {const f64 e=error2(h,from[i],to[i]);if(e<=limit) {inliers.push_back(i);error+=e;}}
        if(inliers.size()<support) continue;
        if(inliers.size()>best.count ||(inliers.size()==best.count &&error<bestError)) {
            best.transform=h;best.count=static_cast<u32>(inliers.size());bestError=error;
            best.inlier.assign(from.size(),0);for(u32 i:inliers)best.inlier[i]=1;
            if(best.count==valid.size() && error<valid.size()*.001) break;
        }
    }
    if(best.count<support) return best;
    for(int pass=0;pass<3;++pass) {
        inliers.clear();for(u32 i:valid)if(best.inlier[i])inliers.push_back(i);
        Homography h;if(!fit(from,to,inliers,model,h))break;
        u32 count=0;f64 sum=0;std::vector<u8> mask(from.size(),0);
        for(u32 i:valid){const f64 e=error2(h,from[i],to[i]);if(e<=limit){mask[i]=1;++count;sum+=e;}}
        if(count<support)break;
        best.transform=h;best.inlier=std::move(mask);best.count=count;bestError=sum;
    }
    best.valid=true;best.rms=static_cast<f32>(std::sqrt(bestError/best.count));
    best.confidence=static_cast<f32>(best.count)/valid.size()/(1+best.rms);
    return best;
}

std::vector<MotionFrame> estimate_path(const Tracks2D& tracks,MotionModel model,const std::vector<Vec2>& polygon,const std::atomic<bool>* cancel) {
    if(!tracks.frames || !tracks.width || !tracks.height || (!polygon.empty()&&polygon.size()<3))return {};
    for(const auto& row:tracks.pos)if(row.size()!=tracks.frames)return {};
    std::vector<MotionFrame> path(tracks.frames);path[0].valid=true;path[0].confidence=1;
    for(u32 f=1;f<tracks.frames;++f) {
        if(cancel&&cancel->load())return {};
        if(!path[f-1].valid)break; // Stop at a lost frame. Never generate a fabricated path.
        Homography inv;if(!path[f-1].path.inverse(inv))break;
        std::vector<Vec2> a,b,reference,current;
        for(const auto& row:tracks.pos) {
            if(!Tracks2D::present(row[f])||!Tracks2D::present(row[f-1]))continue;
            if(!inside(inv.project(row[f-1]),polygon))continue;
            a.push_back(row[f-1]);b.push_back(row[f]);
            if(Tracks2D::present(row[0])&&inside(row[0],polygon)){reference.push_back(row[0]);current.push_back(row[f]);}
        }
        MotionEstimate fit=estimate_motion(a,b,model);
        if(!fit.valid)break;
        path[f]={fit.transform*path[f-1].path,fit.confidence,fit.rms,fit.count,true};
        if(reference.size()>=12) {
            auto absolute=estimate_motion(reference,current,model);
            if(absolute.valid && absolute.count>=reference.size()*.6)
                path[f]={absolute.transform,absolute.confidence,absolute.rms,absolute.count,true};
        }
    }
    return path;
}

std::vector<StabilizedFrame> stabilize_path(const std::vector<MotionFrame>& path,u32 width,u32 height,f64 fps,const StabilizationOptions& opt) {
    if(path.empty()||!width||!height||!std::isfinite(fps)||fps<=0)return {};
    const int radius=std::clamp(static_cast<int>(std::ceil(std::clamp(opt.smoothSeconds,0.02f,3.f)*fps)),1,360);
    const f64 sigma=std::max(1.0,radius/2.0),maxScale=std::clamp(opt.maxScale,1.f,1.5f);
    std::vector<StabilizedFrame> out(path.size());
    for(usize i=0;i<path.size();++i) {
        if(!path[i].valid)continue;
        Homography smooth;
        if(!opt.lock) {
            smooth.m.fill(0);f64 total=0;
            for(int j=std::max(0,static_cast<int>(i)-radius);j<=std::min(static_cast<int>(path.size())-1,static_cast<int>(i)+radius);++j) {
                if(!path[j].valid)continue;
                const f64 delta=j-static_cast<f64>(i),weight=std::exp(-delta*delta/(2*sigma*sigma));
                for(int k=0;k<9;++k)smooth.m[k]+=weight*path[j].path.m[k];total+=weight;
            }
            if(total<=0)continue;
            for(auto& value:smooth.m)value/=total;
        }
        Homography inv;if(!path[i].path.inverse(inv))continue;
        const Homography raw=smooth*inv;
        f64 strength=std::clamp(opt.strength,0.f,1.f);
        Homography correction=blend(raw,strength);
        f64 scale=1;
        if(opt.crop!=CropMode::None) {
            if(!covered(correction,maxScale,width,height)) {
                f64 lo=0,hi=strength;
                for(int k=0;k<20;++k){const f64 mid=(lo+hi)/2;if(covered(blend(raw,mid),maxScale,width,height))lo=mid;else hi=mid;}
                strength=lo;correction=blend(raw,strength);
            }
            f64 lo=1,hi=maxScale;
            for(int k=0;k<20;++k){const f64 mid=(lo+hi)/2;if(covered(correction,mid,width,height))hi=mid;else lo=mid;}
            scale=hi;
        }
        out[i]={correction,static_cast<f32>(scale),static_cast<f32>(strength),true};
    }
    if(opt.crop==CropMode::Static) {
        f32 peak=1;for(const auto& f:out)if(f.valid)peak=std::max(peak,f.scale);
        for(auto& f:out)if(f.valid)f.scale=peak;
    } else if(opt.crop==CropMode::Dynamic) {
        // Two-sided envelope anticipates crop changes; stays above every
        // required scale and limits the rate to 4% per second.
        const f32 step=static_cast<f32>(0.04/fps);
        for(usize i=1;i<out.size();++i)out[i].scale=std::max(out[i].scale,out[i-1].scale-step);
        for(usize i=out.size()-1;i>0;--i)out[i-1].scale=std::max(out[i-1].scale,out[i].scale-step);
    }
    for(auto& f:out)if(f.valid)f.correction=zoom(f.scale,width,height)*f.correction;
    return out;
}
} // namespace aurea::tracking
