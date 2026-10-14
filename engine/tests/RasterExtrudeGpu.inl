#if defined(AUREA_TEST_VULKAN)
namespace raster_volume_test {
u32 count_color(const FloatImage& image,u32 channel) {
    u32 count=0;
    for(usize i=0;i<image.px.size();i+=4) {
        const f32 value=image.px[i+channel];
        count+=value>.15f && value>image.px[i+(channel+1)%3]*2 && value>image.px[i+(channel+2)%3]*2;
    }
    return count;
}
void configure(Scene& s,LayerId id,f32 depth,u32 quality=1) {
    auto* layer=s.comp->layer(id);layer->threeD=true;
    auto& effect=s.add_effect(id,effect_keys::kRasterExtrude);
    effect.params[0].constant=ParamValue::scalar(depth);
    effect.params[1].constant=ParamValue::color(0,1,0,1);
    effect.params[2].constant=ParamValue::scalar(0);
    effect.params[3].constant=ParamValue::scalar(static_cast<f32>(quality));
    // Estes testes medem a forma do volume: sem o reflexo do ambiente.
    if(effect.params.size()>4)effect.params[4].constant=ParamValue::scalar(0);
}
}
AUREA_TEST(RasterExtrudeGpu, FrontalCapsPreservePremultipliedColorAndTransparentHoles) {
    AUREA_REQUIRE_GPU();
    for(u8 alpha:{u8(255),u8(128)}) {
        Scene s(160,120);s.comp->set_transparent_background(true);
        auto pixels=uniform_image(64,48,255,0,0,alpha);
        for(u32 y=16;y<32;++y)for(u32 x=24;x<40;++x)pixels.rgba[(y*64+x)*4+3]=0;
        const LayerId id=s.image(std::move(pixels),80,60);
        raster_volume_test::configure(s,id,0);
        const auto flat=s.render();
        s.comp->layer(id)->effects.back().params[0].constant.v[0]=32;
        const auto volume=s.render();
        f32 maximum=0;
        u32 frontalSamples=0;
        for(u32 y=38;y<82;++y)for(u32 x=50;x<110;++x) {
            // Perspective can reveal the inner walls of a hole even with
            // zero layer rotation. Compare occupied cap interiors only.
            if(std::abs(flat.at(x,y)[3]-alpha/255.f)>.002f)continue;
            ++frontalSamples;
            for(u32 c=0;c<4;++c)maximum=std::max(maximum,std::abs(volume.at(x,y)[c]-flat.at(x,y)[c]));
        }
        std::printf("    frontal alpha%u maxRGBA=%.6f hole=%.6f face=%.6f\n",alpha,maximum,volume.at(80,60)[3],volume.at(64,60)[3]);
        AUREA_CHECK_NEAR(volume.at(80,60)[3],0,.001f);
        AUREA_CHECK_NEAR(volume.at(64,60)[3],alpha/255.f,.002f);
        AUREA_CHECK(maximum<.003f);
        AUREA_CHECK(frontalSamples>1500);
    }
}
AUREA_TEST(RasterExtrudeGpu, RotatedDepthMakesVisibleWallsAtEveryQualityAndIntersectsAnotherPlane) {
    AUREA_REQUIRE_GPU();
    for(u32 quality:{0u,1u,2u})for(f32 rotation:{-60.f,60.f}) {
        Scene s(192,144);
        const LayerId id=s.image(uniform_image(64,48,255,0,0),96,72);
        raster_volume_test::configure(s,id,0,quality);
        s.comp->layer(id)->transform.rotation.y=rotation;
        const auto flat=s.render();
        s.comp->layer(id)->effects.back().params[0].constant.v[0]=40;
        const auto volume=s.render();
        const u32 red=raster_volume_test::count_color(volume,0),green=raster_volume_test::count_color(volume,1);
        std::printf("    quality%u Y%.0f face%u side%u flatSide%u\n",quality,rotation,red,green,raster_volume_test::count_color(flat,1));
        AUREA_CHECK(red>300);AUREA_CHECK(green>400);AUREA_CHECK_EQ(raster_volume_test::count_color(flat,1),0u);
        const LayerId blocker=s.image(uniform_image(192,144,0,0,255),96,72);
        auto* plane=s.comp->layer(blocker);plane->threeD=true;
        plane->transform.position.z=-100;
        const auto front=s.render();
        AUREA_CHECK_EQ(raster_volume_test::count_color(front,0),0u);
        AUREA_CHECK_EQ(raster_volume_test::count_color(front,1),0u);
        plane->transform.position.z=100;
        const auto behind=s.render();
        u32 compared=0,wrong=0;
        for(usize i=0;i<volume.px.size();i+=4) {
            if(volume.px[i+3]<.999f||std::max(volume.px[i],volume.px[i+1])<.999f)continue;
            ++compared;
            for(u32 c=0;c<3;++c)wrong+=std::abs(volume.px[i+c]-behind.px[i+c])>.001f;
        }
        AUREA_CHECK(compared>1500);AUREA_CHECK_EQ(wrong,0u);
        // The camera-facing wall afterY60 lives between-32sin60 and
        // -32sin60+40cos60, hence its mid-depth is negative, not+20.
        plane->transform.position.z=-32.f*std::abs(std::sin(rotation*kDeg2Rad))+20.f*std::cos(rotation*kDeg2Rad);
        const auto crossing=s.render();
        const u32 remaining=raster_volume_test::count_color(crossing,1);
        std::printf("    physical crossing side%u vs%u\n",remaining,green);
        AUREA_CHECK(remaining>20&&remaining<green);
    }
}
AUREA_TEST(RasterExtrudeGpu, RasterShapePrecompAndVideoUseTheirActualAlphaAndTheFullEffectStack) {
    AUREA_REQUIRE_GPU();
    for(u32 source:{0u,1u,2u}) {
        Scene s(192,144);
        LayerId id;
        if(source==0)id=s.solid(64,48,{1,0,0,1},96,72);
        else if(source==1) {
            const CompositionId childId=s.project.timeline().create_composition("own alpha group",64,48,30);
            auto* child=s.project.timeline().composition(childId);child->set_transparent_background(true);
            auto* shape=child->layer(child->add_layer(LayerKind::Shape,"front"));
            shape->shape.bounds={8,8,48,32};shape->shape.fillColor={1,0,0,1};
            id=s.comp->add_layer(LayerKind::Composition,"own group");
            auto* parent=s.comp->layer(id);parent->nested.composition=childId;
            parent->transform.anchor={32,24,0};parent->transform.position={96,72,0};
        } else {
            SyntheticConfig cfg;cfg.width=64;cfg.height=48;cfg.frameCount=12;
            id=s.video(cfg,96,72);
        }
        raster_volume_test::configure(s,id,32);
        auto* layer=s.comp->layer(id);layer->transform.rotation.y=60;
        // A later2D color stage modifies the face without removing the volume.
        s.add_effect(id,effect_keys::kExposure).params[0].constant.v[0]=-.5f;
        const auto volume=s.render();
        u32 visible=0;for(usize i=0;i<volume.px.size();i+=4)visible+=std::max({volume.px[i],volume.px[i+1],volume.px[i+2]})>.05f;
        const u32 green=raster_volume_test::count_color(volume,1);
        std::printf("    rasterSource%u visible%u green%u\n",source,visible,green);
        AUREA_CHECK(visible>600);AUREA_CHECK(green>300);
    }
}
AUREA_TEST(RasterExtrudeGpu, StrokeOutlineDepthExtrudesItsRasterContourAndZeroPreservesLegacyOutput) {
    AUREA_REQUIRE_GPU();
    Scene s(192,144);
    auto pixels=uniform_image(64,48,255,0,0,0);
    for(u32 y=10;y<38;++y)for(u32 x=10;x<54;++x)pixels.rgba[(y*64+x)*4+3]=255;
    const LayerId id=s.image(std::move(pixels),96,72);auto* layer=s.comp->layer(id);layer->threeD=true;layer->transform.rotation.y=60;
    auto& outline=s.add_effect(id,effect_keys::kStrokeOutline);outline.params[1].constant=ParamValue::color(0,1,0,1);
    const auto legacy=s.render();
    layer->effects.back().params.resize(5);const auto oldLayout=s.render();
    f32 maximum=0;for(usize i=0;i<legacy.px.size();++i)maximum=std::max(maximum,std::abs(legacy.px[i]-oldLayout.px[i]));
    AUREA_CHECK(maximum<.001f);
    layer->effects.back().params.resize(6);layer->effects.back().params[5].constant.v[0]=32;
    const auto volume=s.render();
    const auto flatGreen=raster_volume_test::count_color(legacy,1),volumeGreen=raster_volume_test::count_color(volume,1);
    std::printf("    stroke legacy %.6f flatGreen%u volumeGreen%u\n",maximum,flatGreen,volumeGreen);
    AUREA_CHECK(volumeGreen>flatGreen+200);
}
AUREA_TEST(RasterExtrudeGpu, NativeTextKeepsItsGlyphRasterAndCreatesRealSideWalls) {
    AUREA_REQUIRE_GPU();Scene3DRig rig(256,192);
    const auto text=rig.e.add_text("B8");AUREA_CHECK(text.ok());if(!text.ok())return;
    auto* comp=rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    const LayerId id=LayerId::unpack(*text);
    auto* layer=comp->layer(id);layer->text.size=80;layer->text.color={1,0,0,1};layer->transform.position={128,96,0};
    Command add;add.type=CommandType::EffectAdd;add.effect_add.layer=id;add.effect_add.effectType=effect_type_id(effect_keys::kRasterExtrude);add.effect_add.index=kInvalidIndex;
    AUREA_CHECK(rig.e.apply_command(add).ok());layer=comp->layer(id);
    AUREA_CHECK_EQ(layer->kind,LayerKind::Text);AUREA_CHECK(layer->threeD);
    layer->transform.rotation.y=55;
    layer->effects.back().params[1].constant=ParamValue::color(0,1,0,1);layer->effects.back().params[2].constant.v[0]=0;
    const auto image=rig.capture(256);
    u32 red=0,green=0;
    for(usize i=0;i<image.rgba.size();i+=4){red+=image.rgba[i]>100&&image.rgba[i+1]<40;green+=image.rgba[i+1]>100&&image.rgba[i]<40;}
    std::printf("    nativeText cap%u sides%u\n",red,green);
    AUREA_CHECK(red>300);AUREA_CHECK(green>200);
}
AUREA_TEST(RasterExtrudeGpu, DegenerateScaleIsEmptyWithoutAnUnresolvedFrame) {
    AUREA_REQUIRE_GPU();Scene s(96,96);
    const LayerId id=s.image(uniform_image(32,32,255,0,0),48,48);
    raster_volume_test::configure(s,id,32);s.comp->layer(id)->transform.scale={0,1,1};
    const auto image=s.render();AUREA_CHECK_EQ(raster_volume_test::count_color(image,0),0u);
    AUREA_CHECK_EQ(raster_volume_test::count_color(image,1),0u);
}
// Reflexo do ambiente, como nos modelos 3D: um volume metálico deixa de ser a
// cor lateral chapada; a instância salva antes do slot (4 parâmetros) sai
// idêntica ao visual de antes.
AUREA_TEST(RasterExtrudeGpu, EnvironmentReflectionTintsMetalAndLegacyInstancesStayUnchanged) {
    AUREA_REQUIRE_GPU();
    Scene s(192,144);
    const LayerId id=s.image(uniform_image(64,48,255,0,0),96,72);
    raster_volume_test::configure(s,id,40,1);
    s.comp->layer(id)->transform.rotation.y=55;
    const auto plain=s.render();
    auto& fx=s.comp->layer(id)->effects.back();
    AUREA_CHECK_EQ(fx.params.size(),7u);
    fx.params[4].constant=ParamValue::scalar(1);
    fx.params[5].constant=ParamValue::scalar(1);
    fx.params[6].constant=ParamValue::scalar(.15f);
    const auto metal=s.render();
    f32 largest=0;u32 changed=0;
    for(usize i=0;i<plain.px.size();i+=4) {
        if(plain.px[i+3]<.999f)continue;
        f32 d=0;for(u32 c=0;c<3;++c)d=std::max(d,std::abs(metal.px[i+c]-plain.px[i+c]));
        largest=std::max(largest,d);changed+=d>.02f;
    }
    std::printf("    metal: maior diferenca %.4f, pixels mudados %u, paredes verdes %u -> %u\n",largest,changed,
        raster_volume_test::count_color(plain,1),raster_volume_test::count_color(metal,1));
    AUREA_CHECK(largest>.05f);AUREA_CHECK(changed>300);
    for(const f32 v:metal.px)AUREA_CHECK(std::isfinite(v));
    // Dielétrico (metal 0): a face continua com a cor da camada, só ganha brilho.
    fx.params[5].constant=ParamValue::scalar(0);
    const auto dielectric=s.render();
    AUREA_CHECK(raster_volume_test::count_color(dielectric,0)>300);
    // Projeto antigo: o slot não existe e o volume sai exatamente como antes.
    fx.params.resize(4);
    const auto legacy=s.render();
    f32 legacyDiff=0;
    for(usize i=0;i<plain.px.size();++i)legacyDiff=std::max(legacyDiff,std::abs(legacy.px[i]-plain.px[i]));
    AUREA_CHECK(legacyDiff<1e-5f);
}
#endif
