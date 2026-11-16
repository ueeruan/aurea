namespace {
constexpr const char* kFailureTestEffect="test.render.effect_failure";
class FailureTestEffect final : public Effect {
public:
    const EffectInfo& info() const noexcept override {
        static const EffectInfo value{kFailureTestEffect,"Failure injection","Test",EffectClass::Domain};
        return value;
    }
    void declare_parameters(ParameterRegistry& params) const override {
        params.add_int("failure","Failure",0,0,5);
    }
    void resolve_resources(EffectEval& eval) const noexcept override {
        if (eval.e(0)==1 && eval.resources)
            eval.resources->effect_failure(Status{Errc::InvalidArgument,"test effect preparation rejected"});
    }
    Status build(EffectBuildContext&,const EffectEval& eval,const LayerImage& input,f32,LayerImage& out) const override {
        switch (eval.e(0)) {
        case 2: return Status{Errc::OutOfRange,"test effect output exceeds limit"};
        case 3: return Status{Errc::Busy,"test effect input pending"};
        case 4: return Status{Errc::PipelineCompileFailed,"test effect pipeline pending"};
        case 5: return OkStatus; // Invalid output is also an effect failure.
        default: out=input; return OkStatus;
        }
    }
};
struct FailureTestRenderer {
    EffectRegistry effects;
    Renderer renderer;
    bool ready=false;
    FailureTestRenderer() {
        register_builtin_effects(effects);
        if (!effects.add(std::make_unique<FailureTestEffect>()).ok()) return;
        renderer.set_startup_prewarm(false);
        ready=renderer.initialize(gpu().backend,effects).ok();
    }
    EffectInstance& add(Layer& layer) {
        EffectInstance instance;
        instance.id=layer.alloc_effect_id();
        instance.type=effect_type_id(kFailureTestEffect);
        initialize_instance(instance,*effects.params(instance.type));
        layer.effects.push_back(std::move(instance));
        return layer.effects.back();
    }
};
Status render_failure_snapshot(Renderer& renderer,FrameSnapshot& snapshot,bool finalQuality,FloatImage* image=nullptr) {
    (void)renderer.take_incomplete();
    (void)renderer.take_unavailable();
    (void)renderer.take_memory_short();
    RenderSettings settings;
    settings.finalQuality=finalQuality;
    settings.dither=false;
    OffscreenTarget target{gpu().target(snapshot.compWidth,snapshot.compHeight),snapshot.compWidth,snapshot.compHeight};
    FrameStats stats;
    RenderTimings timings;
    const Status status=renderer.render(snapshot,settings,&target,stats,timings);
    if (status.ok() && image) {
        gpu().backend.wait_idle();
        std::vector<u16> half(static_cast<usize>(snapshot.compWidth)*snapshot.compHeight*4);
        AUREA_CHECK(gpu().backend.read_texture(target.texture,half.data(),snapshot.compWidth*8).ok());
        image->width=snapshot.compWidth;
        image->height=snapshot.compHeight;
        image->px.resize(half.size());
        for (usize i=0;i<half.size();++i) image->px[i]=half_to_float(half[i]);
    }
    return status;
}
} // namespace

AUREA_TEST(Gpu, EffectFailureFinalRejectsSpatialBuildAndPreviewPreservesSource) {
    AUREA_REQUIRE_GPU();
    FailureTestRenderer fixture;
    AUREA_CHECK(fixture.ready);
    if (!fixture.ready) return;
    Scene scene(32,24,30);
    const LayerId id=scene.solid(12,12,Vec4{1,.5f,.25f,1},16,12);
    auto& effect=fixture.add(*scene.comp->layer(id));
    FrameSnapshot snapshot;
    RenderSettings settings;
    settings.finalQuality=true;
    fixture.renderer.prepare(*scene.comp,scene.project,FrameIndex{0},nullptr,&Scene::lookup,&scene,
        settings,1,0,DecodeMode::Still,1,snapshot);
    FloatImage baseline,preview;
    AUREA_CHECK(render_failure_snapshot(fixture.renderer,snapshot,true,&baseline).ok());
    effect.params[0].constant=ParamValue::scalar(2);
    fixture.renderer.prepare(*scene.comp,scene.project,FrameIndex{0},nullptr,&Scene::lookup,&scene,
        settings,2,0,DecodeMode::Still,1,snapshot);
    AUREA_CHECK(render_failure_snapshot(fixture.renderer,snapshot,false,&preview).ok());
    AUREA_CHECK(max_diff(preview,baseline)<.002f);
    const Status failed=render_failure_snapshot(fixture.renderer,snapshot,true);
    AUREA_CHECK_EQ(failed.code(),Errc::OutOfRange);
    AUREA_CHECK(failed.detail()=="test effect output exceeds limit");
    effect.params[0].constant=ParamValue::scalar(5);
    fixture.renderer.prepare(*scene.comp,scene.project,FrameIndex{0},nullptr,&Scene::lookup,&scene,
        settings,2,0,DecodeMode::Still,1,snapshot);
    const Status invalid=render_failure_snapshot(fixture.renderer,snapshot,true);
    AUREA_CHECK_EQ(invalid.code(),Errc::InvalidState);
    AUREA_CHECK(invalid.detail()=="effect produced an invalid output image");
}

AUREA_TEST(Gpu, EffectFailurePreparedSnapshotsOwnRecursiveStatusAndClearOnRetry) {
    AUREA_REQUIRE_GPU();
    FailureTestRenderer fixture;
    AUREA_CHECK(fixture.ready);
    if (!fixture.ready) return;
    for (bool nested:{false,true}) {
        fixture.renderer.release_project_resources();
        Scene scene(kPtW,kPtH,30);
        const LayerId host=precomp_temporal_square(scene,nested,false);
        Composition* sourceComp=scene.comp;
        LayerId source=host;
        if (nested) {
            sourceComp=scene.project.timeline().composition(scene.comp->layer(host)->nested.composition);
            source=sourceComp->order().at(0);
        }
        Layer* layer=sourceComp->layer(source);
        auto& effect=fixture.add(*layer);
        auto prepare=[&](u32 failure,FrameSnapshot& snapshot) {
            effect.params[0].constant=ParamValue::scalar(static_cast<f32>(failure));
            RenderSettings settings;
            settings.finalQuality=true;
            fixture.renderer.prepare(*scene.comp,scene.project,FrameIndex{10},nullptr,&Scene::lookup,&scene,
                settings,3,0,DecodeMode::Still,1,snapshot);
        };
        FrameSnapshot failed,ready;
        prepare(1,failed);
        prepare(0,ready);
        AUREA_CHECK_EQ(failed.effectFailure.code(),Errc::InvalidArgument);
        AUREA_CHECK(ready.effectFailure.ok());
        AUREA_CHECK(render_failure_snapshot(fixture.renderer,ready,true).ok());
        const Status owned=render_failure_snapshot(fixture.renderer,failed,true);
        AUREA_CHECK_EQ(owned.code(),Errc::InvalidArgument);
        AUREA_CHECK(owned.detail()=="test effect preparation rejected");
        prepare(0,failed);
        AUREA_CHECK(failed.effectFailure.ok());
        AUREA_CHECK(render_failure_snapshot(fixture.renderer,failed,true).ok());
        for (u32 transient:{3u,4u}) {
            prepare(transient,failed);
            AUREA_CHECK(render_failure_snapshot(fixture.renderer,failed,true).ok());
            AUREA_CHECK(fixture.renderer.take_incomplete());
            AUREA_CHECK(!fixture.renderer.take_unavailable());
        }
    }
}
