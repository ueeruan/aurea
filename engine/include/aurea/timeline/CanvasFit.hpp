#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include "aurea/core/Math.hpp"

namespace aurea {
// cw,ch,w,h,sx,sy,anchorX,anchorY,rotationZ,skewX,skewY -> sx,sy,x,y,uniformScale.
// Shared by both interfaces; keep scaling and anchor compensation out of the UI.
inline std::array<float,5> canvas_fit(const std::array<float,11>& a,bool fill) noexcept {
    for(float v:a)if(!std::isfinite(v))return {};
    if(a[0]<=0||a[1]<=0||a[2]<=0||a[3]<=0)return {};
    const float c=std::cos(a[8]*kDeg2Rad),s=std::sin(a[8]*kDeg2Rad);
    const float kx=std::tan(std::clamp(a[9],-85.f,85.f)*kDeg2Rad),ky=std::tan(std::clamp(a[10],-85.f,85.f)*kDeg2Rad);
    const float signX=a[4]<0?-1.f:1.f,signY=a[5]<0?-1.f:1.f;
    const float m00=(c-s*ky)*signX,m01=(c*kx-s)*signY,m10=(s+c*ky)*signX,m11=(s*kx+c)*signY;
    const float det=m00*m11-m01*m10;
    if(std::abs(det)<1e-6f)return {};
    float k;
    if(fill) k=std::max((std::abs(m11)*a[0]+std::abs(m01)*a[1])/(std::abs(det)*a[2]),
                        (std::abs(m10)*a[0]+std::abs(m00)*a[1])/(std::abs(det)*a[3]));
    else k=std::min(a[0]/(std::abs(m00)*a[2]+std::abs(m01)*a[3]),a[1]/(std::abs(m10)*a[2]+std::abs(m11)*a[3]));
    const float x=a[2]*.5f-a[6],y=a[3]*.5f-a[7];
    return {k*signX,k*signY,a[0]*.5f-k*(m00*x+m01*y),a[1]*.5f-k*(m10*x+m11*y),k};
}
}
