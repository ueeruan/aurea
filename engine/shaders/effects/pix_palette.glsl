#ifndef AUREA_PIX_PALETTE
#define AUREA_PIX_PALETTE
// Public hardware palette values, independently specified as RGB facts.
// Other vendor palettes and ASE project persistence are not implemented yet.
vec3 paletteColor(int palette,int index) {
    const vec3 pico[16]=vec3[16](vec3(0,0,0),vec3(29,43,83),vec3(126,37,83),vec3(0,135,81),vec3(171,82,54),vec3(95,87,79),vec3(194,195,199),vec3(255,241,232),vec3(255,0,77),vec3(255,163,0),vec3(255,236,39),vec3(0,228,54),vec3(41,173,255),vec3(131,118,156),vec3(255,119,168),vec3(255,204,170));
    const vec3 gameboy[4]=vec3[4](vec3(15,56,15),vec3(48,98,48),vec3(139,172,15),vec3(155,188,15));
    if(palette==6) return pico[index]/255.0;
    if(palette==7) return gameboy[index]/255.0;
    if(palette==8) {
        const vec3 cga[4]=vec3[4](vec3(0),vec3(0,1,1),vec3(1,0,1),vec3(1,1,1));
        return cga[index];
    }
    if(palette==9) {
        int bright=index/8;int base=index%8;
        return vec3(float((base>>1)&1),float((base>>2)&1),float(base&1))*(bright==0?205.0/255.0:1.0);
    }
    return aurea_linear_to_srgb(max(index==0?p.color.rgb:p.second.rgb,vec3(0)));
}
vec3 quantize(vec3 value) {
    int palette=int(p.p0.y);
    if(palette<3) {
        float levels=palette==0?2.0:(palette==1?4.0:16.0);
        float gray=dot(value,vec3(.299,.587,.114));
        return vec3(floor(clamp(gray,0.0,1.0)*(levels-1.0)+.5)/(levels-1.0));
    }
    if(palette<6 || palette==10) {
        float levels=palette==3?2.0:(palette==4?4.0:(palette==5?8.0:6.0));
        return floor(clamp(value,0.0,1.0)*(levels-1.0)+.5)/(levels-1.0);
    }
    int count=(palette==7||palette==8)?4:(palette==11?2:16);
    vec3 best=vec3(0);float distance=1e30;
    for(int i=0;i<16;i++) {
        if(i>=count) break;
        vec3 color=paletteColor(palette,i),delta=value-color;
        float d=dot(delta,delta);
        if(d<distance) {best=color;distance=d;}
    }
    return best;
}
float paletteStep() {
    int palette=int(p.p0.y);
    if(palette==1||palette==4) return 1.0/3.0;
    if(palette==2) return 1.0/15.0;
    if(palette==5) return 1.0/7.0;
    if(palette==10) return 1.0/5.0;
    return palette>=6?.25:1.0;
}
#endif
