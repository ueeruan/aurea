// Behavioral checks of independently authored implementations, not vendor parity.
AUREA_TEST(RequestedEffects, PixDitherDiffusesAcrossImageAndIsDeterministic) {
    AUREA_REQUIRE_GPU();
    Scene s(37,21);
    const auto id=s.image(uniform_image(37,21,160,160,160),18.5f,10.5f);
    const auto source=s.render();
    auto& fx=s.add_effect(id,"aurea.stylize.pix_dither");
    std::vector<FloatImage> results;
    for(u32 method=0;method<26;++method) {
        fx.params[0].constant.v[0]=static_cast<f32>(method);
        auto image=s.render();
        for(u32 y=0;y<21;++y) for(u32 x=0;x<37;++x) {
            const auto value=image.v(x,y);
            AUREA_CHECK(value.x<.001f||value.x>.999f);
            AUREA_CHECK(value.w>.999f);
        }
        if(method>=1&&method<=11) AUREA_CHECK(std::abs(image.mean().x-160.f/255.f)<.08f);
        results.push_back(std::move(image));
    }
    fx.params[0].constant.v[0]=1;
    AUREA_CHECK(s.render().px==results[1].px);
    AUREA_CHECK(results[1].px!=results[12].px);
    // Independent scalar Floyd Steinberg reference, including propagation
    // across the width (no workgroup/tile reset).
    std::vector<f32> errors(37*21,0);
    u32 differences=0;
    for(i32 y=0;y<21;++y) for(i32 x=0;x<37;++x) {
        const usize index=static_cast<usize>(y)*37+x;
        const f32 adjusted=Color::linear_to_srgb(source.v(x,y).x)+errors[index];
        const f32 value=adjusted>=.5f?1.f:0.f;
        if(std::abs(results[1].v(x,y).x-value)>.01f) ++differences;
        const f32 error=adjusted-value;
        auto distribute=[&](i32 nx,i32 ny,f32 weight) {
            if(nx>=0&&nx<37&&ny<21) errors[static_cast<usize>(ny)*37+nx]+=error*weight;
        };
        distribute(x+1,y,7.f/16);distribute(x-1,y+1,3.f/16);
        distribute(x,y+1,5.f/16);distribute(x+1,y+1,1.f/16);
    }
    std::printf("    Floyd Steinberg: %u differing pixels, source sRGB %.8f\n",differences,Color::linear_to_srgb(source.v(0,0).x));
    AUREA_CHECK(differences<8);
}

AUREA_TEST(RequestedEffects, VideoGlitchIndependentGroupsAndStableScrubbing) {
    AUREA_REQUIRE_GPU();
    Scene s(128,72);
    const auto id=s.image(reference_image(128,72),64,36);
    const auto original=s.render();
    auto& fx=s.add_effect(id,"aurea.glitch.video_glitch");
    fx.params[2].constant.v[0]=100;
    AUREA_CHECK(s.render().px==original.px);
    const auto peak=s.render(FrameIndex{30});
    AUREA_CHECK(peak.px!=original.px);
    (void)s.render(FrameIndex{120});
    AUREA_CHECK(s.render(FrameIndex{30}).px==peak.px);
    for(u32 group=0;group<4;++group) fx.params[10+group*8].constant.v[0]=0;
    AUREA_CHECK(s.render(FrameIndex{30}).px==original.px);
    for(u32 group=0;group<4;++group) {
        fx.params[10+group*8].constant.v[0]=1;
        AUREA_CHECK(s.render(FrameIndex{30}).px!=original.px);
        fx.params[10+group*8].constant.v[0]=0;
    }
    fx.params[56].constant.v[0]=1;
    AUREA_CHECK(s.render(FrameIndex{30}).px!=original.px);
}

AUREA_TEST(RequestedEffects, DisplaceNeutralMapSignedTranslationAndTransform) {
    AUREA_REQUIRE_GPU();
    Scene s(128,72);
    const auto gray=s.image(uniform_image(128,72,128,128,128),64,36);
    const auto white=s.image(uniform_image(128,72,255,255,255),64,36);
    s.comp->layer(gray)->visible=false;s.comp->layer(white)->visible=false;
    const auto id=s.image(reference_image(128,72),64,36);
    const auto original=s.render();
    auto& fx=s.add_effect(id,"aurea.distort.displace_transform");
    fx.params[0].constant.ref=gray.pack();
    fx.params[9].constant.v[0]=128.f/255*100;
    fx.params[15].constant.v[0]=0;
    const auto neutral=s.render();
    f32 neutralDifference=0;
    for(usize i=0;i<neutral.px.size();++i) neutralDifference=std::max(neutralDifference,std::abs(neutral.px[i]-original.px[i]));
    AUREA_CHECK(neutralDifference<.01f); // Half-float map quantization.
    fx.params[0].constant.ref=white.pack();fx.params[9].constant.v[0]=50;
    fx.params[1].constant.v[0]=12;
    const auto positive=s.render();
    AUREA_CHECK(positive.px!=original.px);
    fx.params[4].constant.v[0]=-100;
    AUREA_CHECK(s.render().px!=positive.px);
    fx.params[1].constant.v[0]=0;fx.params[4].constant.v[0]=100;
    fx.params[2].constant.v[0]=50;
    AUREA_CHECK(s.render().px!=original.px);
    fx.params[2].constant.v[0]=0;fx.params[3].constant.v[0]=30;
    AUREA_CHECK(s.render().px!=original.px);
    fx.params[16].constant.v[0]=0;
    AUREA_CHECK(s.render().px==original.px);
}

AUREA_TEST(RequestedEffects, GlitchHistoryUsesEventStartAndManualCrossing) {
    AUREA_REQUIRE_GPU();
    Scene s(96,72);
    SyntheticConfig cfg;cfg.width=96;cfg.height=72;cfg.frameCount=120;cfg.pattern=SyntheticPattern::MovingSquare;
    const auto id=s.video(cfg,48,36);
    auto& fx=s.add_effect(id,"aurea.glitch.video_glitch");
    fx.params[2].constant.v[0]=100;
    for(u32 group=1;group<4;++group) fx.params[10+group*8].constant.v[0]=0;
    const auto held=s.render(FrameIndex{30});
    fx.params[58].constant.v[0]=0;
    AUREA_CHECK(s.render(FrameIndex{30}).px!=held.px);
    fx.params[58].constant.v[0]=1;
    AUREA_CHECK(s.render(FrameIndex{30}).px==held.px);
    std::vector<ParamValue> values;
    for(const auto& param:fx.params) values.push_back(param.constant);
    EffectEval eval;eval.values=values.data();eval.count=static_cast<u32>(values.size());
    eval.layer=s.comp->layer(id);eval.instance=&fx;eval.localTime=FrameIndex{30};
    const auto* effect=gpu().effects.find(fx.type);
    AUREA_CHECK(effect->history_delay_frames(eval)==30);
    auto& track=s.comp->layer(id)->tracks.get_or_create(TrackProperty::EffectParam,fx.id,param_track_key(0,0));
    track.set(FrameIndex{0},0.f,Interpolation::Linear);track.set(FrameIndex{20},100.f,Interpolation::Linear);
    values[5]=ParamValue::scalar(1); // Cross threshold 50 between frames 10 and 11.
    AUREA_CHECK(effect->history_delay_frames(eval)==19);
    effect->resolve_resources(eval);
    AUREA_CHECK(eval.auxTimeFrames==11);
    eval.localTime=FrameIndex{100};
    AUREA_CHECK(effect->history_delay_frames(eval)==0);
}

// Optional native renders for human review and performance measurement. These
// use our own input, never a third-party asset or a vendor golden frame.
AUREA_TEST(RequestedEffects, RenderRemainingEffectsTemporalReview) {
    AUREA_REQUIRE_GPU();
    const char* directory=std::getenv("AUREA_REQUESTED_EFFECTS_REVIEW");
    if(!directory||!*directory) return;
    struct Review { const char* key; const char* name; };
    const Review cases[]={
        {"aurea.distort.lens_gradient","distort"},
        {"aurea.stylize.pixel_encoder","pixel-encoder"},
        {"aurea.light.starglow","starglow"},
        {"aurea.stylize.pix_dither","pixdither"},
        {"aurea.glitch.video_glitch","video-glitch"},
        {"aurea.distort.displace_transform","displace"}};
    const auto root=std::filesystem::path(directory);
    std::filesystem::create_directories(root);
    std::ofstream timing(root/"native-render-timing.csv");
    timing<<"effect,width,height,frames,render_readback_ms_per_frame\n";
    for(const auto& item:cases) {
        const auto folder=root/item.name;
        std::filesystem::create_directories(folder);
        Scene s(320,180);
        SyntheticConfig cfg;cfg.width=320;cfg.height=180;cfg.frameCount=120;cfg.pattern=SyntheticPattern::MovingSquare;
        auto id=s.video(cfg,160,90);
        auto& fx=s.add_effect(id,item.key);
        if(std::string(item.name)=="video-glitch") fx.params[2].constant.v[0]=100;
        if(std::string(item.name)=="pixdither") fx.params[0].constant.v[0]=1;
        double elapsed=0;
        for(int frame=0;frame<60;++frame) {
            if(std::string(item.name)=="starglow") fx.params[11].constant.v[0]=static_cast<f32>(frame)*.1f;
            auto before=std::chrono::steady_clock::now();
            auto result=s.render(FrameIndex{frame});
            elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
            char filename[32];std::snprintf(filename,sizeof(filename),"frame-%03d.png",frame);
            AUREA_CHECK(write_png((folder/filename).string(),result.encoded()));
        }
        timing<<item.name<<",320,180,60,"<<elapsed/60<<"\n";
    }
    // HD exposes the cost of serial dependencies in true error diffusion.
    Scene hd(1280,720);
    auto id=hd.image(reference_image(1280,720),640,360);
    auto& fx=hd.add_effect(id,"aurea.stylize.pix_dither");
    fx.params[0].constant.v[0]=1;
    double elapsed=0;
    for(int frame=0;frame<3;++frame) {
        auto before=std::chrono::steady_clock::now();
        auto result=hd.render();
        elapsed+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();
        AUREA_CHECK(result.width==1280 && result.height==720);
        if(frame==2) AUREA_CHECK(write_png((root/"pixdither-720p.png").string(),result.encoded()));
    }
    timing<<"pixdither,1280,720,3,"<<elapsed/3<<"\n";
    const auto trackerFolder=root/"tracker-source";
    std::filesystem::create_directories(trackerFolder);
    Scene tracker(640,360);
    SyntheticConfig cfg;cfg.width=640;cfg.height=360;cfg.frameCount=90;cfg.pattern=SyntheticPattern::Scene3D;
    tracker.video(cfg,320,180);
    for(int frame=0;frame<90;++frame) {
        char filename[32];std::snprintf(filename,sizeof(filename),"frame-%03d.png",frame);
        AUREA_CHECK(write_png((trackerFolder/filename).string(),tracker.render(FrameIndex{frame}).encoded()));
    }
}
