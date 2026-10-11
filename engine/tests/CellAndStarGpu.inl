// Native behavior tests; no assertion of equivalence to third-party assets.
AUREA_TEST(RequestedEffects, PixelEncoderUsesDistinctPatternsAndDeterministicCells) {
    AUREA_REQUIRE_GPU();
    Scene s(96,64);
    auto id=s.image(uniform_image(96,64,200,200,200),48,32);
    auto& fx=s.add_effect(id,"aurea.stylize.pixel_encoder");
    fx.params[0].constant.v[0]=fx.params[0].constant.v[1]=16;
    std::vector<FloatImage> results;
    for(u32 mode=0;mode<10;++mode) {
        fx.params[2].constant.v[0]=static_cast<f32>(mode);
        auto image=s.render();
        AUREA_CHECK(image.mean().x>.01f && image.mean().x<.99f);
        for(const auto& earlier:results) AUREA_CHECK(image.px!=earlier.px);
        results.push_back(std::move(image));
    }
    fx.params[11].constant.v[0]=1;
    const auto seeded=s.render();
    AUREA_CHECK(seeded.px==s.render().px);
    fx.params[10].constant.v[0]=234;
    AUREA_CHECK(seeded.px!=s.render().px);
    fx.params[11].constant.v[0]=0;
    fx.params[2].constant.v[0]=2;
    fx.params[3].constant.v[0]=-100;
    AUREA_CHECK(s.render().mean().x<.001f);
    fx.params[14].constant.v[0]=1;
    AUREA_CHECK(s.render().mean().x>.99f);
}

AUREA_TEST(RequestedEffects, StarglowGeneratesOneSidedRaysAndHonorsThreshold) {
    AUREA_REQUIRE_GPU();
    Scene s(160,96);
    auto source=uniform_image(80,64,0,0,0,0);
    for(u32 y=30;y<34;++y) for(u32 x=38;x<42;++x) {
        const usize index=(static_cast<usize>(y)*80+x)*4;
        source.rgba[index]=source.rgba[index+1]=source.rgba[index+2]=source.rgba[index+3]=255;
    }
    auto id=s.image(std::move(source),80,48);
    auto& fx=s.add_effect(id,"aurea.light.starglow");
    fx.params[0].constant.v[0]=48;
    fx.params[14].constant.v[0]=0;
    fx.params[9].constant.v[0]=0;
    for(u32 ray=1;ray<8;++ray) fx.params[17+ray].constant.v[0]=0;
    const auto right=s.render();
    AUREA_CHECK(right.v(98,48).x>.005f);
    AUREA_CHECK(right.v(62,48).x<.001f);
    AUREA_CHECK(right.v(98,36).x<.001f);
    fx.params[17].constant.v[0]=0;
    fx.params[21].constant.v[0]=1;
    const auto left=s.render();
    AUREA_CHECK(left.v(62,48).x>.005f);
    AUREA_CHECK(left.v(98,48).x<.001f);
    fx.params[2].constant.v[0]=100;
    AUREA_CHECK(s.render().mean().x<.001f);
}

AUREA_TEST(RequestedEffects, StarglowPreservesSinglePixelHighlightsAtFullDensity) {
    AUREA_REQUIRE_GPU();
    Scene scene(160,96);
    auto image=uniform_image(160,96,0,0,0,255);
    const usize pixel=(48*160+80)*4;
    image.rgba[pixel]=image.rgba[pixel+1]=image.rgba[pixel+2]=255;
    const auto source=scene.image(std::move(image),80,48);
    auto& fx=scene.add_effect(source,"aurea.light.starglow");
    fx.params[0].constant.v[0]=48;
    fx.params[2].constant.v[0]=70;
    fx.params[9].constant.v[0]=0;
    fx.params[14].constant.v[0]=0;
    for(u32 ray=1;ray<8;++ray)fx.params[17+ray].constant.v[0]=0;
    const auto rendered=scene.render();
    float rightEnergy=0,leftEnergy=0;
    for(u32 y=46;y<=51;++y)for(u32 x=87;x<122;++x)rightEnergy+=rendered.v(x,y).x;
    for(u32 y=46;y<=51;++y)for(u32 x=40;x<73;++x)leftEnergy+=rendered.v(x,y).x;
    std::printf("    Starglow full density single pixel: right=%.7f left=%.7f\n",rightEnergy,leftEnergy);
    AUREA_CHECK(rightEnergy>.01f);AUREA_CHECK(leftEnergy<.0001f);
    // A below-threshold source does not gain brightness through downsampling.
    fx.params[2].constant.v[0]=100;
    AUREA_CHECK(scene.render().mean().x<.0001f);
}
