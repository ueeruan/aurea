// Properties of the native implementation. These do not assert vendor parity.
AUREA_TEST(RequestedEffects, LensGradientConstantLensAndBlackMatteAreNeutral) {
    AUREA_REQUIRE_GPU();
    Scene s(128,72);
    auto lens=s.image(uniform_image(128,72,128,128,128),64,36);
    auto matte=s.image(uniform_image(128,72,0,0,0),64,36);
    s.comp->layer(lens)->visible=false;
    s.comp->layer(matte)->visible=false;
    auto ramp=uniform_image(128,72,0,0,0);
    for(u32 y=0;y<72;++y) for(u32 x=0;x<128;++x) {
        const usize index=(static_cast<usize>(y)*128+x)*4;
        ramp.rgba[index]=ramp.rgba[index+1]=ramp.rgba[index+2]=static_cast<u8>(x*255/127);
    }
    auto rampId=s.image(std::move(ramp),64,36);
    s.comp->layer(rampId)->visible=false;
    auto id=s.image(quadrants(128,72),64,36);
    const auto baseline=s.render();
    auto& fx=s.add_effect(id,"aurea.distort.lens_gradient");
    fx.params[0].constant.ref=lens.pack();
    fx.params[9].constant.v[0]=0;
    AUREA_CHECK(s.render().px==baseline.px);
    fx.params[0].constant.ref=rampId.pack();
    fx.params[4].constant.v[0]=0;
    fx.params[2].constant.v[0]=5;
    const auto warped=s.render();
    AUREA_CHECK(warped.px!=baseline.px);
    fx.params[1].constant.ref=matte.pack();
    AUREA_CHECK(s.render().px==baseline.px);
    fx.params[11].constant.v[0]=1;
    AUREA_CHECK(s.render().px==warped.px);
    fx.params[3].constant.v[0]=1;
    const auto fine=s.render();
    f64 broadDifference=0,fineDifference=0;
    for(usize i=0;i<baseline.px.size();++i) {
        broadDifference+=std::fabs(warped.px[i]-baseline.px[i]);
        fineDifference+=std::fabs(fine.px[i]-baseline.px[i]);
    }
    AUREA_CHECK(fineDifference < broadDifference*.1);
}

AUREA_TEST(RequestedEffects, WipeEndpointsDirectionsAndStableSeed) {
    AUREA_REQUIRE_GPU();
    for (const char* key : {effect_keys::kWipeFlux, effect_keys::kWipePlasma}) {
        Scene s(128, 72);
        auto id = s.solid(128, 72, {1,1,1,1}, 64,36);
        auto& fx = s.add_effect(id,key);
        const auto full = s.render();
        AUREA_CHECK(full.mean().x > .99f);
        fx.params[0].constant.v[0]=100;
        AUREA_CHECK(s.render().mean().x < .001f);
        fx.params[1].constant.v[0]=1;
        AUREA_CHECK(s.render().mean().x > .99f);
        fx.params[0].constant.v[0]=0;
        AUREA_CHECK(s.render().mean().x < .001f);
        fx.params[0].constant.v[0]=50;
        fx.params[1].constant.v[0]=0;
        const auto mid = s.render();
        AUREA_CHECK(mid.mean().x > .1f && mid.mean().x < .9f);
        AUREA_CHECK(mid.px == s.render().px);
        fx.params[5].constant.v[0]=.789f;
        AUREA_CHECK(mid.px != s.render().px);
        fx.params[5].constant.v[0]=.432f;
        const auto at0 = s.render();
        AUREA_CHECK(at0.px != s.render(FrameIndex{30}).px);
        fx.params[9].constant.v[0]=0;
        AUREA_CHECK(s.render().px == s.render(FrameIndex{30}).px);
    }
}

AUREA_TEST(RequestedEffects, AutomaticWipeUsesTrimmedClipInsteadOfSourceOffset) {
    AUREA_REQUIRE_GPU();
    Scene s(96,64);
    auto id = s.solid(96,64,{1,1,1,1},48,32);
    auto* layer = s.comp->layer(id);
    layer->start=FrameIndex{20}; layer->end=FrameIndex{51}; layer->offset=FrameIndex{200};
    auto& fx=s.add_effect(id,effect_keys::kWipePlasma);
    fx.params[2].constant.v[0]=1;
    AUREA_CHECK(s.render(FrameIndex{20}).mean().x > .99f);
    const auto mid=s.render(FrameIndex{35});
    AUREA_CHECK(mid.mean().x > .1f && mid.mean().x < .9f);
    AUREA_CHECK(s.render(FrameIndex{50}).mean().x < .001f);
}

AUREA_TEST(RequestedEffects, ReferencedWipeBackgroundWorksWithHiddenMapAndTransformedLayer) {
    AUREA_REQUIRE_GPU();
    Scene s(128,72);
    auto bg=s.solid(128,72,{0,1,0,1},64,36);
    s.comp->layer(bg)->visible=false;
    auto id=s.solid(64,40,{1,0,0,1},80,36);
    auto& fx=s.add_effect(id,effect_keys::kWipeFlux);
    fx.params[29].constant.ref=bg.pack();
    fx.params[0].constant.v[0]=100;
    AUREA_CHECK(near4(s.render().v(80,36),{0,1,0,1},.01f));
}

AUREA_TEST(RequestedEffects, RenderProceduralWipeTemporalReview) {
    AUREA_REQUIRE_GPU();
    const char* directory=std::getenv("AUREA_REQUESTED_EFFECTS_REVIEW");
    if (!directory || !*directory) return;
    for (const char* key : {effect_keys::kWipeFlux,effect_keys::kWipePlasma}) {
        const std::string name=key==effect_keys::kWipeFlux?"wipeflux":"wipeplasma";
        const auto folder=std::filesystem::path(directory)/name;
        std::filesystem::create_directories(folder);
        Scene s(320,180);
        s.solid(320,180,{.12f,.01f,.3f,1},160,90);
        auto id=s.solid(320,180,{.05f,1,.1f,1},160,90);
        auto& fx=s.add_effect(id,key);
        fx.params[21].constant.v[0]=.25f;
        for(int frame=0;frame<60;++frame) {
            fx.params[0].constant.v[0]=100.f*static_cast<f32>(frame)/59.f;
            char filename[32];std::snprintf(filename,sizeof(filename),"frame-%03d.png",frame);
            AUREA_CHECK(write_png((folder/filename).string(),s.render(FrameIndex{frame}).encoded()));
        }
    }
}
