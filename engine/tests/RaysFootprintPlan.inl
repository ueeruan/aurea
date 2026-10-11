AUREA_TEST(RaysFootprintPlan, FullHdAuxiliaryPreservesDensityAndCapFallbackHasNoExtraImage) {
    EffectRegistry registry;register_builtin_effects(registry);
    for(const auto& settings:std::array<std::pair<float,u32>,4>{{{1.f,4096u},{8.f,4096u},{16.f,4096u},{16.f,1920u}}}) {
        const auto [cap,maxTexture]=settings;const bool padded=cap>1&&maxTexture>1920;
        GraphFixture f;f.backend.caps.maxSamplerAnisotropy=cap;
        ShaderLibrary shaders;AUREA_CHECK(shaders.initialize(f.backend).ok());
        FakeResources resources;Arena arena;EffectBuildContext ctx(f.graph,shaders,arena,resources,SurfaceFormat::RGBA16F,maxTexture);
        Layer layer;layer.effects.push_back(make_effect(registry,effect_keys::kRays,0));
        auto place=placement();place.compWidth=1920;place.compHeight=1080;
        place.layerWidth=1920;place.layerHeight=1080;
        EffectPlan plan;EffectGraph::plan(layer,registry,FrameIndex{0},1,place,&resources,plan);
        const auto source=f.graph.create_texture("source",rt(1920,1080));
        f.graph.add_raster_pass("source",PassStage::Upload,source,LoadOp::Clear,{},[](PassContext&){});
        LayerImage output;AUREA_CHECK(EffectGraph::build(plan,ctx,{source,{0,0,1920,1080},1920,1080},output).ok());
        AUREA_CHECK_EQ(output.width,1920u);AUREA_CHECK_EQ(output.height,1080u);
        f.graph.set_output(output.texture,ResourceState::ShaderRead);
        bool final=false;f.backend.beforeSetUniforms=[&](const void* data,u32 size){
            if(size!=112)return;std::array<Vec4,7> u{};std::memcpy(u.data(),data,112);
            if(u[4].z!=1){final=true;AUREA_CHECK_NEAR(u[4].w,padded?cap:1.f,1e-6f);AUREA_CHECK_NEAR(u[3].z,32.f,1e-6f);}
        };
        AUREA_CHECK(f.run().ok());AUREA_CHECK(final);
        const auto count=f.graph.stats().passesExecuted;AUREA_CHECK_EQ(count,padded?3u:2u);
        std::printf("    Rays FullHD cap%.0f passes%u transient%llu\n",cap,count,static_cast<unsigned long long>(f.graph.stats().transientBytes));
        // Source + output + exactly one native padded image, no mips or
        // prefix hierarchy. No modification of the pool/resource budget.
        AUREA_CHECK(f.graph.stats().transientBytes<=3ull*(1922ull*1082ull*8));
        shaders.shutdown();
    }
}

AUREA_TEST(RaysFootprintPlan, AuxiliaryIsSubjectToTheExistingResourceBudget) {
    EffectRegistry registry;register_builtin_effects(registry);
    GraphFixture f;f.backend.caps.maxSamplerAnisotropy=16;
    const u64 budget=32ull<<20;f.pool.set_allocation_limit(budget);
    ShaderLibrary shaders;AUREA_CHECK(shaders.initialize(f.backend).ok());
    FakeResources resources;Arena arena;EffectBuildContext ctx(f.graph,shaders,arena,resources,SurfaceFormat::RGBA16F,4096);
    Layer layer;layer.effects.push_back(make_effect(registry,effect_keys::kRays,0));
    auto place=placement();place.compWidth=1920;place.compHeight=1080;place.layerWidth=1920;place.layerHeight=1080;
    EffectPlan plan;EffectGraph::plan(layer,registry,FrameIndex{0},1,place,&resources,plan);
    const auto source=f.graph.create_texture("source",rt(1920,1080));
    f.graph.add_raster_pass("source",PassStage::Upload,source,LoadOp::Clear,{},[](PassContext&){});
    LayerImage output;AUREA_CHECK(EffectGraph::build(plan,ctx,{source,{0,0,1920,1080},1920,1080},output).ok());
    f.graph.set_output(output.texture,ResourceState::ShaderRead);
    const auto status=f.run();AUREA_CHECK_EQ(status.code(),Errc::OutOfDeviceMemory);
    AUREA_CHECK(f.backend.memory_stats().usedBytes<=budget);
    shaders.shutdown();
}
