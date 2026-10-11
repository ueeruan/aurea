namespace minimax_reference {
using Pixel=std::array<float,4>;
float score(const Pixel& p,u32 channel) {
    if(!(p[3]>1e-6f))return 0;
    if(channel==1)return p[3];
    if(channel==2)return std::max({p[0],p[1],p[2]})/p[3];
    if(channel==3)return std::max(std::max({p[0],p[1],p[2]})/p[3],p[3]);
    return p[0]*.2126f+p[1]*.7152f+p[2]*.0722f;
}
bool wins(const Pixel& a,const Pixel& b,u32 channel,bool dilation) {
    const float aa=score(a,channel),bb=score(b,channel);
    if(aa!=bb)return dilation?aa>bb:aa<bb;
    // Independent lexicographic comparison, then center wins in operation().
    return std::lexicographical_compare(b.begin(),b.end(),a.begin(),a.end());
}
struct Grid {
    int w,h;std::vector<Pixel> pixels;
    Pixel at(int x,int y)const {return x>=0&&y>=0&&x<w&&y<h?pixels[y*w+x]:Pixel{};}
};
Grid operation(const Grid& source,int r,u32 shape,u32 channel,bool dilation) {
    Grid out{source.w,source.h,std::vector<Pixel>(source.pixels.size())};
    for(int y=0;y<source.h;++y)for(int x=0;x<source.w;++x) {
        Pixel best=source.at(x,y);
        // Brute-force support enumeration, independent of GPU separations.
        for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
            if(shape==0&&dx!=0&&dy!=0)continue;
            if(shape==2&&std::abs(dx)+std::abs(dy)>r)continue;
            const Pixel value=source.at(x+dx,y+dy);
            if(wins(value,best,channel,dilation))best=value;
        }
        const auto center=source.at(x,y);
        if(score(center,channel)==score(best,channel))best=center;
        out.pixels[y*source.w+x]=best;
    }
    return out;
}
FloatImage effect(const FloatImage& base,int radius,u32 mode,u32 shape,u32 channel,float amount) {
    const int pad=2*radius+2;
    Grid grid{static_cast<int>(base.width)+2*pad,static_cast<int>(base.height)+2*pad,{}};
    grid.pixels.resize(grid.w*grid.h);
    for(u32 y=0;y<base.height;++y)for(u32 x=0;x<base.width;++x)
        std::copy_n(base.at(x,y),4,grid.pixels[(y+pad)*grid.w+x+pad].begin());
    Grid processed=operation(grid,radius,shape,channel,mode==0||mode==3);
    if(mode>=2)processed=operation(processed,radius,shape,channel,mode==2);
    FloatImage out=base;
    for(u32 y=0;y<base.height;++y)for(u32 x=0;x<base.width;++x) {
        const auto a=grid.at(x+pad,y+pad),b=processed.at(x+pad,y+pad);
        const float alpha=a[3]*(1-amount)+b[3]*amount;
        for(u32 c=0;c<3;++c) {
            const float ac=a[3]>1e-6f?a[c]/a[3]:0,bc=b[3]>1e-6f?b[c]/b[3]:0;
            out.px[(y*out.width+x)*4+c]=std::max(0.f,ac*(1-amount)+bc*amount)*alpha;
        }
        out.px[(y*out.width+x)*4+3]=alpha;
    }
    return out;
}
void compare(const FloatImage& actual,const FloatImage& expected,const char* context) {
    AUREA_CHECK_EQ(actual.width,expected.width);AUREA_CHECK_EQ(actual.height,expected.height);
    float error=0;
    for(usize i=0;i<std::min(actual.px.size(),expected.px.size());++i)
        error=std::max(error,std::fabs(actual.px[i]-expected.px[i]));
    std::printf("    %s maxError=%.7f\n",context,error);
    AUREA_CHECK_MSG(error<.002f,context);
}
}

AUREA_TEST(MinimaxGpu, OperationsShapesChannelsIntensityAndTiesMatchBruteForce) {
    AUREA_REQUIRE_GPU();
    Scene scene(48,32);scene.comp->set_transparent_background(true);
    auto pixels=uniform_image(12,10,0,0,0,0);
    for(u32 y=1;y<9;++y)for(u32 x=1;x<11;++x) {
        auto* p=&pixels.rgba[(y*12+x)*4];
        p[0]=static_cast<u8>(30+(x*41+y*19)%190);
        p[1]=static_cast<u8>(20+(x*29+y*61)%210);
        p[2]=static_cast<u8>(10+(x*71+y*13)%225);
        p[3]=static_cast<u8>(64+((x+y)%4)*48); // deliberate equal-alpha ties
    }
    const auto id=scene.image(std::move(pixels),24,16);
    const auto original=scene.render();auto& fx=scene.add_effect(id,effect_keys::kMinimax);
    for(u32 shape=0;shape<3;++shape)for(u32 mode=0;mode<4;++mode)for(u32 channel=0;channel<4;++channel) {
        const int radius=(mode+channel)%3+1;
        const float amount=((mode+channel)&1)?1.f:.375f;
        fx.params[0].constant=ParamValue::scalar(static_cast<f32>(radius));
        fx.params[1].constant=ParamValue::scalar(static_cast<f32>(mode));
        fx.params[2].constant=ParamValue::scalar(static_cast<f32>(shape));
        fx.params[3].constant=ParamValue::scalar(amount*100);
        fx.params[4].constant=ParamValue::scalar(static_cast<f32>(channel));
        char context[120];std::snprintf(context,sizeof(context),"shape%u mode%u channel%u radius%d amount%.3f",shape,mode,channel,radius,amount);
        minimax_reference::compare(scene.render(),minimax_reference::effect(original,radius,mode,shape,channel,amount),context);
    }
    fx.params[3].constant=ParamValue::scalar(0);
    minimax_reference::compare(scene.render(),original,"zero intensity identity");
}

AUREA_TEST(MinimaxGpu, MaximumRadiusHasContinuousCoverageAndPreviewUsesInputTexels) {
    AUREA_REQUIRE_GPU();
    Scene scene(256,48);scene.comp->set_transparent_background(true);
    auto pixels=uniform_image(1,1,255,128,64,128);
    const auto id=scene.image(std::move(pixels),128.5f,24.5f);
    const auto original=scene.render();auto& fx=scene.add_effect(id,effect_keys::kMinimax);
    fx.params[0].constant=ParamValue::scalar(120);fx.params[4].constant=ParamValue::scalar(1);
    for(u32 shape=0;shape<3;++shape) {
        fx.params[2].constant=ParamValue::scalar(static_cast<float>(shape));
        const auto result=scene.render();u32 covered=0,errors=0;
        for(u32 y=0;y<result.height;++y)for(u32 x=0;x<result.width;++x) {
            const int dx=static_cast<int>(x)-128,dy=static_cast<int>(y)-24;
            const bool inside=shape==0?((dx==0||dy==0)&&std::max(std::abs(dx),std::abs(dy))<=120):
                shape==1?std::max(std::abs(dx),std::abs(dy))<=120:std::abs(dx)+std::abs(dy)<=120;
            covered+=result.at(x,y)[3]>.49f;
            errors+=std::fabs(result.at(x,y)[3]-(inside?original.at(128,24)[3]:0.f))>.002f;
        }
        std::printf("    radius120 shape%u covered=%u errors=%u\n",shape,covered,errors);
        AUREA_CHECK(covered>200u);AUREA_CHECK_EQ(errors,0u);
    }
    Scene reduced(48,32);reduced.comp->set_transparent_background(true);
    const auto reducedId=reduced.image(uniform_image(12,12,180,90,30,150),24,16);
    const auto halfBase=reduced.render(FrameIndex{0},2);
    auto& reducedFx=reduced.add_effect(reducedId,effect_keys::kMinimax);
    reducedFx.params[0].constant=ParamValue::scalar(4);
    minimax_reference::compare(reduced.render(FrameIndex{0},2),minimax_reference::effect(halfBase,2,0,1,0,1),"half preview radius4 = 2 input texels");
}
