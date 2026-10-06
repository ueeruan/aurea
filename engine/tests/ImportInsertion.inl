namespace {
void seek_import_frame(Engine& engine, i64 frame) {
    Command seek; seek.type=CommandType::PlaybackSeek; seek.seek.time=tick_at(FrameIndex{frame},30.);
    AUREA_CHECK(engine.apply_command(seek).ok());
}
}

AUREA_TEST(ImportInsertion, MediaStartsAtTheRequestedPlayheadAndKeepsItsSourceBeginning) {
    for (const bool audio : {false,true}) for (const bool occupied : {false,true}) {
        SwitchingProbeFactory factory;
        auto config=headless_config(); config.mediaFactory=&factory;
        Engine engine; AUREA_CHECK(engine.initialize(config).ok());
        AUREA_CHECK(engine.new_project(640,360,30.,"insert at ten seconds").ok());
        if (occupied) AUREA_CHECK(engine.add_shape(0).ok());
        auto* comp=engine.project()->timeline().composition(engine.project()->timeline().current());
        comp->set_duration(FrameIndex{900});
        seek_import_frame(engine,300);
        // The probe runs off the model lock. Moving the playhead while it
        // opens must not move the pending clip away from the requested time.
        factory.duringProbe=[&] { seek_import_frame(engine,450); };
        VideoImport request; request.sourcePath="synthetic-insertion";
        auto imported=audio?engine.import_audio(request):engine.import_video(request);
        AUREA_CHECK(imported.ok()); if (!imported.ok()) continue;
        const Layer* layer=comp->layer(LayerId::unpack(*imported));
        AUREA_CHECK_EQ(layer->start.value,300ll); AUREA_CHECK_EQ(layer->end.value,360ll);
        AUREA_CHECK_EQ(layer->local_time(FrameIndex{300}).value,0ll);
        AUREA_CHECK_EQ(engine.read_status().playhead.value,450ll);
        AUREA_CHECK_EQ(comp->duration().value,900ll);
        Command undo; undo.type=CommandType::Undo; AUREA_CHECK(engine.apply_command(undo).ok());
        AUREA_CHECK_EQ(engine.read_status().layerCount,occupied?1u:0u);
        engine.shutdown();
    }
}

AUREA_TEST(ImportInsertion, ImageUsesTheQueuedSeekAndTheCompositionTail) {
    Engine engine; AUREA_CHECK(engine.initialize(headless_config()).ok());
    AUREA_CHECK(engine.new_project(640,360,30.,"image insertion").ok());
    auto* comp=engine.project()->timeline().composition(engine.project()->timeline().current());
    comp->set_duration(FrameIndex{900});
    Command seek; seek.type=CommandType::PlaybackSeek; seek.seek.time=tick_at(FrameIndex{300},30.);
    AUREA_CHECK_EQ(engine.submit_commands(&seek,1),1u);
    std::vector<u8> pixels(4*4*4,255);
    auto image=engine.import_image(pixels.data(),4,4,"image");
    AUREA_CHECK(image.ok()); if (!image.ok()) return;
    const auto* layer=comp->layer(LayerId::unpack(*image));
    AUREA_CHECK_EQ(layer->start.value,300ll); AUREA_CHECK_EQ(layer->end.value,900ll);
    AUREA_CHECK_EQ(layer->local_time(FrameIndex{300}).value,0ll);
    AUREA_CHECK_EQ(engine.read_status().playhead.value,300ll);
    engine.shutdown();
}
