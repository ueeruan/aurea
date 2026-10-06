AUREA_TEST(LightRaysTextGpu, TextCastsVisibleBeamsOutsideGlyphsAndItsOriginalBounds) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    const auto id = rig.e.add_text("LIGHT");
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const Image8 original = rig.capture(640);
    u32 x0=original.width,y0=original.height,x1=0,y1=0,letters=0;
    for(u32 y=0;y<original.height;++y)for(u32 x=0;x<original.width;++x) {
        const auto* p=original.at(x,y);
        if(std::max({p[0],p[1],p[2]})>32) {
            ++letters;x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);
        }
    }
    AUREA_CHECK(letters>500u);
    Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=LayerId::unpack(*id);
    add.effect_add.effectType=effect_type_id(effect_keys::kRays);add.effect_add.index=kInvalidIndex;
    AUREA_CHECK(rig.e.apply_command(add).ok());
    auto* comp=rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    auto& fx=comp->layer(LayerId::unpack(*id))->effects.back();
    fx.params[0].constant=ParamValue::scalar(3);
    fx.params[1].constant=ParamValue::scalar(85);
    fx.params[2].constant=ParamValue::scalar(10);
    fx.params[3].constant=ParamValue::scalar(10);
    fx.params[5].constant=ParamValue::scalar(64);
    const auto rays=rig.capture(640);
    u32 outsideLetters=0,outsideBounds=0;
    for(u32 y=0;y<rays.height;++y)for(u32 x=0;x<rays.width;++x) {
        const auto* p=rays.at(x,y);const auto* b=original.at(x,y);
        if(std::max({p[0],p[1],p[2]})>12&&std::max({b[0],b[1],b[2]})<2) {
            ++outsideLetters;
            outsideBounds+=x+4<x0||x>x1+4||y+4<y0||y>y1+4;
        }
    }
    std::printf("    text pixels=%u, beam pixels outside glyphs=%u, beyond text bounds=%u\n",letters,outsideLetters,outsideBounds);
    AUREA_CHECK(outsideLetters>1000u);AUREA_CHECK(outsideBounds>500u);
    fx.params[0].constant=ParamValue::scalar(0);
    AUREA_CHECK_EQ(max_diff(original,rig.capture(640)),0u);
}

AUREA_TEST(LightRaysTextGpu, TransparentSourceCanEmitColoredBeamsButHiddenRgbCannot) {
    AUREA_REQUIRE_GPU();
    Scene scene(192,128);scene.comp->set_transparent_background(true);
    auto image=uniform_image(80,40,0,0,0,0);
    for(u32 y=8;y<32;++y)for(u32 x=34;x<46;++x) {
        auto* p=image.rgba.data()+(usize(y)*80+x)*4;p[0]=p[1]=p[2]=p[3]=255;
    }
    const auto id=scene.image(std::move(image),96,64);
    const auto original=scene.render();
    auto& fx=scene.add_effect(id,effect_keys::kRays);
    fx.params[0].constant.v[0]=4;fx.params[1].constant.v[0]=85;
    fx.params[2].constant.v[0]=0;fx.params[3].constant.v[0]=0;
    fx.params[7].constant=ParamValue::boolean(false);
    fx.params[9].constant=ParamValue::color(1,0,0,1);
    const auto result=scene.render();
    u32 beam=0,bad=0;
    for(usize i=0;i<result.px.size();i+=4) {
        if(original.px[i+3]<.001f&&result.px[i]>.005f&&result.px[i+3]>.001f) {
            ++beam;bad+=result.px[i+1]>.001f||result.px[i+2]>.001f;
        }
    }
    AUREA_CHECK(beam>300u);AUREA_CHECK_EQ(bad,0u);
    // RGB hidden behind zero alpha must not become a light source.
    for(auto& pair:scene.images)for(usize i=0;i<pair.second.rgba.size();i+=4) {
        pair.second.rgba[i]=pair.second.rgba[i+1]=pair.second.rgba[i+2]=255;pair.second.rgba[i+3]=0;
    }
    gpu().renderer.release_project_resources();
    const auto hidden=scene.render();
    f32 energy=0;for(f32 value:hidden.px)energy=std::max(energy,std::fabs(value));
    AUREA_CHECK_NEAR(energy,0,.001f);
}

// Mesma causa no Reflexo de lente: a saída era a caixa do texto (justa nos
// glifos), e o clarão saía cortado num retângulo em volta das letras.
AUREA_TEST(LightRaysTextGpu, LensFlareOnTextShinesBeyondTheTextBox) {
    AUREA_REQUIRE_GPU();
    Scene3DRig rig(640, 360);
    const auto id = rig.e.add_text("FLARE");
    AUREA_CHECK(id.ok()); if (!id.ok()) return;
    const Image8 original = rig.capture(640);
    u32 x0=original.width,y0=original.height,x1=0,y1=0,letters=0;
    for(u32 y=0;y<original.height;++y)for(u32 x=0;x<original.width;++x) {
        const auto* p=original.at(x,y);
        if(std::max({p[0],p[1],p[2]})>32) {
            ++letters;x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);
        }
    }
    AUREA_CHECK(letters>300u);
    Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=LayerId::unpack(*id);
    add.effect_add.effectType=effect_type_id(effect_keys::kLensFlare);add.effect_add.index=kInvalidIndex;
    AUREA_CHECK(rig.e.apply_command(add).ok());
    const auto flare=rig.capture(640);
    u32 beyond=0;
    for(u32 y=0;y<flare.height;++y)for(u32 x=0;x<flare.width;++x) {
        const bool outside=x+6<x0||x>x1+6||y+6<y0||y>y1+6;
        if(outside&&std::max({flare.at(x,y)[0],flare.at(x,y)[1],flare.at(x,y)[2]})>12) ++beyond;
    }
    std::printf("    text pixels=%u, flare pixels beyond the text box=%u\n",letters,beyond);
    AUREA_CHECK(beyond>500u);
}
