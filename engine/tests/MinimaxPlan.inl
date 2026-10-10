AUREA_TEST(MinimaxPlan, MaximumRadiusKeepsInputDensityAndUsesAtMostThirteenPasses) {
    EffectRegistry registry;register_builtin_effects(registry);
    for(u32 shape=0;shape<3;++shape) {
        GraphFixture f;ShaderLibrary shaders;AUREA_CHECK(shaders.initialize(f.backend).ok());
        FakeResources resources;Arena arena;
        EffectBuildContext ctx(f.graph,shaders,arena,resources,SurfaceFormat::RGBA16F,4096);
        Layer layer;layer.effects.push_back(make_effect(registry,effect_keys::kMinimax,0));
        auto& fx=layer.effects[0];fx.params[0].constant=ParamValue::scalar(120);
        fx.params[1].constant=ParamValue::scalar(2);fx.params[2].constant=ParamValue::scalar(static_cast<f32>(shape));
        fx.params[3].constant=ParamValue::scalar(37.5f);
        EffectPlan plan;EffectGraph::plan(layer,registry,FrameIndex{0},1,placement(),&resources,plan);
        const auto source=f.graph.create_texture("source",rt(1920,1080));
        f.graph.add_raster_pass("source",PassStage::Upload,source,LoadOp::Clear,{},[](PassContext&){});
        LayerImage output;
        AUREA_CHECK(EffectGraph::build(plan,ctx,{source,{0,0,1920,1080},1920,1080},output).ok());
        AUREA_CHECK_EQ(output.width,1920u);AUREA_CHECK_EQ(output.height,1080u);
        AUREA_CHECK_NEAR(output.texel_scale_x(),1,1e-6f);AUREA_CHECK_NEAR(output.texel_scale_y(),1,1e-6f);
        f.graph.set_output(output.texture,ResourceState::ShaderRead);
        u32 kernels=0;bool fullRadius=false;
        f.backend.beforeSetUniforms=[&](const void* data,u32 size) {
            if(size!=128)return;
            std::array<Vec4,8> u{};std::memcpy(u.data(),data,128);
            if(u[2].x==0) {
                ++kernels;
                if(shape!=2)fullRadius|=u[3].z==-120&&u[3].w==120;
                else fullRadius|=u[3].z==-60&&u[3].w==60;
            }
        };
        AUREA_CHECK(f.run().ok());
        AUREA_CHECK(fullRadius);AUREA_CHECK(kernels>=4);
        const auto passes=f.graph.stats().passesExecuted;
        AUREA_CHECK(passes<=14u); // source + <=13 FX passes, independent of r
        std::printf("    Minimax FullHD open radius120 shape%u passes=%u physical=%u bytes=%llu\n",
            shape,passes,f.graph.stats().physicalTextures,static_cast<unsigned long long>(f.graph.stats().transientBytes));
        shaders.shutdown();
    }
}
