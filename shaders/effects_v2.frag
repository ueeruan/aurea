#version 460 core
#include <flutter/runtime_effect.glsl>

// Float ABI: size[0..1], mode[2], time[3], pixelScale[4], filter[5],
// parameters[6..37], primary color[38..41], other colors[42..53].
uniform vec2 uSize;
uniform float uMode;
uniform float uTime;
uniform float uPixelScale;
uniform float uFilter;
uniform vec4 p0;
uniform vec4 p1;
uniform vec4 p2;
uniform vec4 p3;
uniform vec4 p4;
uniform vec4 p5;
uniform vec4 p6;
uniform vec4 p7;
uniform vec4 uColor;
uniform vec4 uColor2;
uniform vec4 uColor3;
uniform vec4 uColor4;
uniform sampler2D uImage;
out vec4 fragColor;

const vec3 LUMA = vec3(.2126, .7152, .0722);
float lum(vec3 c) { return dot(c,LUMA); }
vec3 straight(vec4 c) { return c.a > .00001 ? c.rgb / c.a : vec3(0.0); }
vec4 premul(vec3 c, float a) { return vec4(clamp(c,0.0,1.0)*a,a); }
vec4 src(vec2 uv) {
  // Flutter 3.47 stores GLES render targets top-down too. Only older
  // engines need the filter-input correction; Canvas images never do.
  #if defined(IMPELLER_TARGET_OPENGLES) && !defined(IMPELLER_OPENGLES_UNFLIPPED_DEPRECATED)
  if (uFilter > .5) uv.y = 1.0-uv.y;
  #endif
  return texture(uImage,clamp(uv,vec2(.5)/uSize,1.0-vec2(.5)/uSize));
}
vec4 decal(vec2 uv) {
  if (uv.x<0.0 || uv.y<0.0 || uv.x>1.0 || uv.y>1.0) return vec4(0.0);
  return src(uv);
}
vec2 rotate2(vec2 v, float a) { return mat2(cos(a),sin(a),-sin(a),cos(a))*v; }
float hash(vec3 p) { p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z); }
float noise(vec3 p) {
  vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
  return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),
                 mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),
             mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),
                 mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float fbm(vec3 p,float octaves) {
  float sum=0.0,total=0.0,weight=1.0;
  for(int i=0;i<6;i++) {
    float octaveWeight=clamp(octaves-float(i),0.0,1.0);
    sum+=noise(p)*weight*octaveWeight;total+=weight*octaveWeight;
    weight*=.5;p=p*2.03+vec3(5.2,1.3,7.1);
  }
  return sum/max(total,.0001);
}
vec3 saturate(vec3 c,float amount) { return mix(vec3(lum(c)),c,amount); }
vec3 srgbToLinear(vec3 c) { return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c)); }
vec3 linearToSrgb(vec3 c) { c=max(c,vec3(0));return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-.055,step(vec3(.0031308),c)); }
vec3 levels(vec3 c) {
  float span=p0.y-p0.x;
  vec3 x=abs(span)<.000001 ? step(vec3(p0.y),c) : clamp((c-p0.x)/span,0.0,1.0);
  vec3 result=p0.w+(p1.x-p0.w)*pow(x,vec3(1.0/max(.01,p0.z)));
  if(p1.y<.5) return result;
  if(p1.y<1.5) return vec3(result.r,c.gb);
  if(p1.y<2.5) return vec3(c.r,result.g,c.b);
  return vec3(c.rg,result.b);
}
vec4 blurLine(vec2 uv,vec2 delta,int count) {
  vec4 sum=vec4(0);float weight=0.0;
  for(int i=0;i<64;i++) {
    if(i>=count) break;
    float t=(float(i)+.5)/float(count)-.5;
    float w=exp(-t*t*12.0);
    sum+=src(uv+t*delta)*w;weight+=w;
  }
  return sum/max(weight,.00001);
}

void main() {
  vec2 uv=FlutterFragCoord().xy/uSize;
  vec4 original=src(uv);
  vec3 c=straight(original);
  float a=original.a;
  int mode=int(uMode+.5);
  if(mode==1) c=levels(c);
  if(mode==2) {float n=max(1.0,floor(p0.x+.5)-1.0);c=floor(clamp(c,0.0,1.0)*n+.5)/n;}
  if(mode==3) {
    // Monotonic smooth S shaping; shadows/highlights have local influence.
    c=clamp(c+p0.y*.35,0.0,1.0);
    vec3 s=c*c*(3.0-2.0*c);
    c=mix(c,s,p0.x);
    c+=p0.z*.4*pow(1.0-c,vec3(2));
    c-=p0.w*.4*c*c;
  }
  if(mode==4) {
    float mx=max(c.r,max(c.g,c.b)),mn=min(c.r,min(c.g,c.b));
    float saturation=(mx-mn)/max(mx,.0001);
    float skin=smoothstep(.0,.15,c.r-c.b)*(1.0-smoothstep(.1,.35,abs(c.g-(c.r*.6+c.b*.4))));
    float gain=1.0+p0.y+p0.x*(1.0-saturation)*(1.0-p0.z*skin);
    c=saturate(c,gain);
  }
  if(mode==5) {
    vec3 light=srgbToLinear(c)*exp2(p0.x);
    c=linearToSrgb(light);
    c=(c-.5)*(1.0+p0.y)+.5;
    c+=p0.w*.3*pow(clamp(1.0-c,0.0,1.0),vec3(2));
    c+=p0.z*.3*pow(clamp(c,0.0,1.0),vec3(2));
    c*=vec3(1.0+p1.x*.18+p1.y*.05,1.0-p1.y*.14,1.0-p1.x*.18+p1.y*.05);
    c=saturate(c,1.0+p1.z);
    c=pow(max(c,vec3(0)),vec3(1.0/max(.01,p1.w)));
  }
  if(mode==6) c*=vec3(1.0+p0.x*.3,1.0-p0.y*.2,1.0-p0.x*.3);
  if(mode==7) {
    float y=clamp(lum(c),0.0,1.0);
    c+=p0.xyz*pow(1.0-y,2.0)+vec3(p0.w,p1.x,p1.y)*y*y;
  }
  if(mode==8) {
    float m=max(c.r,max(c.g,c.b));
    float matte=smoothstep(p0.x,min(1.0,p0.x+max(.001,p0.y)),m);
    float newAlpha=matte*m*a;
    c=m>.00001 ? c/m : vec3(0);
    a=newAlpha;
  }
  if(mode==9) c=mix(c,lum(c)*uColor.rgb,p0.x*uColor.a);
  if(mode==10) {
    vec2 q=(uv-vec2(p1.x,p1.y))*2.0;
    float d=p0.w<.5 ? length(q) : max(abs(q.x),abs(q.y));
    float edge=smoothstep(max(0.0,p0.y-p0.z),p0.y+.0001,d);
    c=mix(c,uColor.rgb,edge*p0.x*uColor.a);
  }
  if(mode==11) {
    vec2 blocks=vec2(max(1.0,p0.x),max(1.0,p0.x*uSize.y/uSize.x));
    fragColor=src((floor(uv*blocks)+.5)/blocks);return;
  }
  if(mode==12) {
    vec2 q=floor(uv*uSize/max(.5,p0.y*uPixelScale));
    float n=hash(vec3(q,p0.z+floor(uTime*24.0)))-.5;
    c+=n*p0.x*.55*(.35+.65*sin(clamp(lum(c),0.0,1.0)*3.14159));
  }
  if(mode==13) {
    vec2 q=uv*uSize/(max(.02,p0.x)*min(uSize.x,uSize.y));
    float n=fbm(vec3(q+p1.y,p0.w),p0.y);
    n=clamp((n-.5)*p0.z+.5,0.0,1.0);
    c=mix(c,vec3(n)*uColor.rgb,p1.x*uColor.a);
  }
  if(mode==14) {
    vec2 q=uv*uSize/max(1.0,p0.y*uPixelScale);
    float evolution=p0.w/180.0;
    vec2 shift=vec2(fbm(vec3(q+p1.x,evolution),p0.z),fbm(vec3(q+37.0+p1.x,evolution+13.0),p0.z))-.5;
    fragColor=decal(uv+shift*2.0*p0.x*uPixelScale/uSize);return;
  }
  if(mode==15 || mode==19) {
    int count=int(clamp(p0.z*4.0,8.0,64.0));
    vec4 sum=vec4(0);
    for(int i=0;i<64;i++) {
      if(i>=count) break;
      float t=(float(i)+.5)/float(count)-.5;
      vec2 v=uv-.5;
      vec2 q;
      if(mode==19) q=.5+v/max(.001,1.0+p0.x*(1.0-p0.y*(t+.5)));
      else if(p0.y<.5) q=.5+v*(1.0+t*p0.x*.5);
      else q=.5+rotate2(v*uSize,t*p0.x*.8)/uSize;
      sum+=src(q);
    }
    fragColor=sum/float(count);return;
  }
  if(mode==16) {
    float angle=p0.y*.01745329252;
    vec2 d=vec2(sin(angle),cos(angle))*p0.x*uPixelScale/uSize;
    fragColor=blurLine(uv,d,48);return;
  }
  if(mode==17) {
    vec4 sum=vec4(0);float total=0.0;
    for(int j=-3;j<=3;j++) for(int i=-3;i<=3;i++) {
      float w=exp(-float(i*i+j*j)*.5);
      sum+=src(uv+vec2(float(i),float(j))*p0.y*uPixelScale/uSize)*w;total+=w;
    }
    vec3 delta=c-straight(sum/total);
    vec3 mask=step(vec3(p0.z),abs(delta));
    c+=delta*p0.x*mask;
  }
  if(mode==18) {
    vec2 center=vec2(p0.w,p1.x);vec3 rays=vec3(0);float total=0.0;
    int count=int(clamp(p0.z*3.0,6.0,60.0));
    for(int i=0;i<60;i++) {
      if(i>=count) break;
      float t=(float(i)+.5)/float(count);
      vec4 raySample=src(mix(uv,center,t*p0.x));
      float w=exp(-t*2.0);
      rays+=raySample.rgb*max(0.0,lum(straight(raySample))-.5)*w;total+=w;
    }
    c+=rays/max(total,.0001)*p0.y*uColor.rgb*2.0;
  }
  if(mode==20) {
    vec2 q=clamp(.5+rotate2(uv-.5,-p0.z*.01745329252),0.0,1.0);
    vec4 gradient=mix(mix(uColor,uColor2,q.x),mix(uColor3,uColor4,q.x),q.y);
    vec3 g=gradient.rgb;
    if(p0.y>.5 && p0.y<1.5) g=c*g;
    else if(p0.y<2.5 && p0.y>1.5) g=1.0-(1.0-c)*(1.0-g);
    else if(p0.y>2.5) g=mix(2.0*c*g,1.0-2.0*(1.0-c)*(1.0-g),step(vec3(.5),c));
    c=mix(c,g,p0.x*gradient.a);
  }
  if(mode==21) {
    // Glow source: luminance threshold and soft knee BEFORE blurring.
    float brightness=p0.w<.5 ? lum(c) : max(c.r,max(c.g,c.b))-min(c.r,min(c.g,c.b));
    float knee=max(.00001,p0.x*p0.y);
    float soft=clamp(brightness-p0.x+knee,0.0,2.0*knee);
    float contribution=max(brightness-p0.x,soft*soft/(4.0*knee));
    c*=vec3(p1.x,p1.y,p1.z);
    c=saturate(c,p2.y);
    vec3 tint=c;
    if(p1.w>.5 && p1.w<1.5) tint=lum(c)*uColor.rgb;
    if(p1.w>1.5 && p1.w<2.5) tint=c*mix(uColor.rgb,uColor2.rgb,uv.y);
    if(p1.w>2.5) tint=c*mix(vec3(1),uColor.rgb,1.0-clamp(brightness,0.0,1.0));
    c=mix(c,tint,p2.x);
    a*=clamp(contribution/max(brightness,.00001),0.0,1.0);
  }
  if(mode==22) {
    // Tone mapping and deterministic procedural lens dirt, not an unused knob.
    c*=exp2(p0.x);
    float dirt=fbm(vec3(uv*9.0,2.4),3.0);
    c*=1.0+p0.z*.01*max(0.0,dirt-.45)*2.0;
    if(p0.y<.5) c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0.0,1.0);
    else if(p0.y<1.5) c=c/(1.0+c);
    else if(p0.y<2.5) c=c*(1.0+c/16.0)/(1.0+c);
  }
  if(mode==23 || mode==24) {
    vec2 delta=mode==23 ? rotate2(vec2(p0.x*uPixelScale,0),p0.y*.01745329252)/uSize
        : (uv-.5)*dot(uv-.5,uv-.5)*p0.x*.35;
    vec4 left=src(uv+delta),right=src(uv-delta),mid=original;
    if(mode==23 && p0.w>0.0) {
      vec2 blur=delta*p0.w*.7;
      left=blurLine(uv+delta,blur,8);right=blurLine(uv-delta,blur,8);
    }
    a=max(mid.a,max(left.a,right.a));
    vec3 channels=vec3(left.r,mid.g,right.b);
    if(mode==23 && p0.z>.5 && p0.z<1.5) channels=vec3(left.r,right.g,mid.b);
    if(mode==23 && p0.z>1.5) channels=vec3(mid.r,left.g,right.b);
    fragColor=vec4(min(channels,vec3(a)),a);return;
  }
  if(mode==25) {
    float t=p0.y<.5 ? uv.x : uv.y;
    float profile=pow(max(0.0,1.0-abs(t-p0.w)/max(.0001,max(p0.w,1.0-p0.w))),p0.z);
    vec2 delta=p0.y<.5 ? vec2(0,p0.x) : vec2(p0.x,0);
    fragColor=decal(uv-delta*profile*uPixelScale/uSize);return;
  }
  if(mode==26) {
    vec2 normal=rotate2(vec2(0,1),p0.y*.01745329252);
    float distance=dot((uv-.5)*uSize,normal)-(p0.z-.5)*uSize.y;
    float split=p0.x*uPixelScale;
    float edge=max(.5,p0.w*8.0*uPixelScale);
    float mask=smoothstep(split-edge,split+edge,abs(distance));
    vec4 value=decal(uv-normal*sign(distance)*split/uSize);
    fragColor=value*mask;return;
  }
  if(mode==27) {
    float tick=floor(uTime/max(.05,p1.x));
    vec2 q=uv;float corruption=0.0;
    for(int i=0;i<24;i++) {
      if(float(i)>=p0.x) break;
      float seed=p1.y+float(i)*7.0;
      float top=hash(vec3(seed,tick,1))*(1.0-p0.y);
      float r=hash(vec3(seed,tick,2));
      if(uv.y>=top && uv.y<top+p0.y && r<.6) {
        q.x+=(r-.3)*p0.z*.8;corruption=p0.w;
      }
    }
    vec4 value=src(q);c=straight(value);a=value.a;
    c=mix(c,c.brg,corruption);
  }
  if(mode==28) {
    float tick=floor(uTime*p1.x);
    float row=floor(uv.y*max(1.0,p0.y));
    float r=hash(vec3(row,tick,p1.z));
    float gate=step(.55,r);
    vec2 q=uv+vec2((r-.5)*gate*p0.z*uPixelScale/uSize.x,0);
    float d=p0.w*gate*.018;
    vec4 value=src(q);float alpha=max(value.a,max(src(q+vec2(d,0)).a,src(q-vec2(d,0)).a));
    vec3 rgb=vec3(src(q+vec2(d,0)).r,value.g,src(q-vec2(d,0)).b);
    float line=hash(vec3(floor(uv.y*uSize.y),tick,p1.z+37.0))-.5;
    vec4 result=premul(alpha>.0001 ? rgb/alpha+line*p1.y*.35 : vec3(0),alpha);
    fragColor=mix(original,result,p0.x);return;
  }
  if(mode==29) {
    float tick=floor(uTime*29.97),row=floor(uv.y*uSize.y);
    float jitter=(noise(vec3(row*.08,tick,p1.z))-.5)*p0.w*12.0*uPixelScale/uSize.x;
    vec2 q=uv+vec2(jitter,0);
    vec4 value=src(q);a=value.a;c=straight(value);
    vec3 bleed=straight(blurLine(q+vec2(p0.z*5.0*uPixelScale/uSize.x,0),vec2(p0.z*12.0*uPixelScale/uSize.x,0),12));
    // Chroma smears while luminance remains sharp, as in composite video.
    c=mix(c,vec3(lum(c))+(bleed-vec3(lum(bleed))),p0.z);
    c=saturate(c,1.0-p1.y*.55);
    c*=1.0-p0.y*.18*(.5+.5*cos(uv.y*uSize.y*3.14159265));
    c+=(hash(vec3(floor(uv*uSize),tick+p1.z))-.5)*p1.x*.28;
    fragColor=mix(original,premul(c,a),p0.x);return;
  }
  if(mode==30) {
    float tick=floor(uTime*16.0),seed=p1.z;
    float jump=(hash(vec3(tick,seed,7))-.5)*p1.y*10.0*uPixelScale/uSize.y;
    vec4 value=src(uv+vec2(0,jump));a=value.a;c=straight(value);
    c*=1.0+(hash(vec3(tick,seed,3))-.5)*p0.z*.4;
    c+=(hash(vec3(floor(uv*uSize),seed+tick))-.5)*p0.w*.3;
    vec2 grid=uv*vec2(60,40);vec2 cell=floor(grid);
    float dust=step(1.0-p0.x*.08,hash(vec3(cell,seed+tick)));
    dust*=1.0-smoothstep(.08,.3,length(fract(grid)-.5));c*=1.0-dust*.85;
    float scratchX=hash(vec3(floor(tick/4.0),seed,13));
    float scratch=1.0-smoothstep(.5,1.8,abs(uv.x-scratchX)*uSize.x);
    c+=scratch*p0.y*.65;
    float burn=p1.x*pow(abs(uv.x-.5)*2.0,5.0)*(.6+.4*noise(vec3(uv*3.0,tick*.04)));
    c=mix(c,vec3(1,.26,.04),burn);
  }
  if(mode==31) {
    float phase=uTime*p0.y;
    float f=hash(vec3(floor(phase),p1.x,19));
    if(p0.z>.5 && p0.z<1.5) f=step(.5,fract(phase));
    if(p0.z>1.5) f=.5+.5*sin(phase*6.283185307);
    float gain=1.0-p0.x+p0.x*f;
    if(p0.w<.5) a*=gain;else c*=gain;
  }
  if(mode==32) {
    float tick=floor(uTime*p0.y/max(.05,p0.z));
    float row=floor(uv.y*24.0),r=hash(vec3(row,tick,p2.y));
    float pulse=step(.65,hash(vec3(tick,p2.y,4)));
    float strength=p0.x*pulse;
    vec2 q=.5+(uv-.5)/max(.1,1.0+(r-.5)*p1.x*strength*.25);
    q.x+=(r-.5)*p0.w*strength*.12;
    vec4 value=blurLine(q,vec2(p1.w*strength*.025,0),12);
    float delta=p2.x*strength*.012;
    a=max(value.a,max(src(q+vec2(delta,0)).a,src(q-vec2(delta,0)).a));
    vec3 rgb=vec3(src(q+vec2(delta,0)).r,value.g,src(q-vec2(delta,0)).b);
    c=a>.0001 ? rgb/a : vec3(0);
    c=mix(c,c.gbr,clamp(p1.y*strength*.6,0.0,1.0));
    c*=1.0+(r-.5)*p1.z*strength;
  }
  fragColor=premul(c,a);
}
