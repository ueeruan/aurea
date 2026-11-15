// =============================================================================
//  0.5.3 — "Please let us expand group, seeing all contents of it without
//  ungrouping!!". A timeline abre o grupo NO LUGAR (EngineGroupInline.cpp):
//  linhas de dentro no tempo de fora, mover/aparar sem entrar no grupo, um
//  passo de desfazer, a composição aberta e o cabeçote intactos.
// =============================================================================

namespace {
struct GroupInlineRig {
    Engine e;
    u64 a = 0, b = 0, group = 0;
    u64 innerA = 0, innerB = 0;
    bool ok = false;
    GroupInlineRig() {
        if (!e.initialize(headless_config()).ok()) return;
        if (!e.new_project(640, 360, 30., "grupo no lugar").ok()) return;
        auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
        comp->set_duration(FrameIndex{300});
        const auto sa = e.add_shape(0), sb = e.add_shape(1);
        if (!sa.ok() || !sb.ok()) return;
        a = *sa; b = *sb;
        comp->layer(LayerId::unpack(a))->name = "A";
        comp->layer(LayerId::unpack(b))->name = "B";
        // A em [0, 60), B em [30, 90) antes de agrupar.
        Command range;
        range.type = CommandType::LayerSetTimeRange;
        range.layer_range.layer = LayerId::unpack(a);
        range.layer_range.start = FrameIndex{0}; range.layer_range.end = FrameIndex{60};
        if (!e.apply_command(range).ok()) return;
        range.layer_range.layer = LayerId::unpack(b);
        range.layer_range.start = FrameIndex{30}; range.layer_range.end = FrameIndex{90};
        if (!e.apply_command(range).ok()) return;
        const u64 ids[2] = {a, b};
        const auto pre = e.precompose(ids, 2, "Grupo");
        if (!pre.ok()) return;
        group = *pre;
        const Layer* g = comp->layer(LayerId::unpack(group));
        const Composition* child = g ? e.project()->timeline().composition(g->nested.composition) : nullptr;
        if (!child) return;
        child->layers().for_each([&](LayerId id, const Layer& l) {
            if (l.name == "A") innerA = id.pack();
            if (l.name == "B") innerB = id.pack();
        });
        ok = innerA && innerB;
    }
    Composition* root() { return e.project()->timeline().composition(e.project()->timeline().root()); }
    Layer* group_layer() { return root()->layer(LayerId::unpack(group)); }
    const Layer* inner(u64 id) {
        const Layer* g = group_layer();
        const Composition* child = g ? e.project()->timeline().composition(g->nested.composition) : nullptr;
        return child ? child->layer(LayerId::unpack(id)) : nullptr;
    }
    /// O grupo começa em `start` mostrando a composição de dentro desde `offset`.
    void place_group(i64 start, i64 offset) {
        Layer* g = group_layer();
        const i64 len = g->end.value - g->start.value;
        g->start = FrameIndex{start};
        g->offset = FrameIndex{offset};
        g->end = FrameIndex{start + len};
    }
    bridge::LayerRow row(u64 id, u32* count = nullptr) {
        bridge::LayerRow rows[8];
        char names[256];
        const u32 n = e.query_group_layers(group, rows, 8, names, sizeof(names));
        if (count) *count = n;
        for (u32 i = 0; i < n; ++i) if (rows[i].id == id) return rows[i];
        return bridge::LayerRow{};
    }
};
} // namespace

AUREA_TEST(GroupInline, RowsComeInTheOuterTimeWithoutOpeningTheGroup) {
    GroupInlineRig r;
    AUREA_CHECK(r.ok); if (!r.ok) return;
    r.place_group(100, 20);   // fora = dentro + 100 − 20
    u32 n = 0;
    const bridge::LayerRow ra = r.row(r.innerA, &n);
    const bridge::LayerRow rb = r.row(r.innerB);
    AUREA_CHECK_EQ(n, 2u);
    AUREA_CHECK_EQ(ra.startFrame, 80);
    AUREA_CHECK_EQ(ra.endFrame, 140);
    AUREA_CHECK_EQ(rb.startFrame, 110);
    AUREA_CHECK_EQ(rb.endFrame, 170);
    AUREA_CHECK(ra.reserved0 & Engine::kGroupRowEditable);
    // Nomes no blob e a composição aberta continua a principal.
    bridge::LayerRow rows[4]; char names[64] = {};
    AUREA_CHECK_EQ(r.e.query_group_layers(r.group, rows, 4, names, sizeof(names)), 2u);
    AUREA_CHECK_EQ(r.e.precomp_depth(), 0u);
    // Camada que não é grupo / id de outra composição: nada.
    AUREA_CHECK_EQ(r.e.query_group_layers(r.innerA, rows, 4, names, sizeof(names)), 0u);
}

AUREA_TEST(GroupInline, MoveAndTrimInPlaceAreOneUndoOfTheInnerComposition) {
    GroupInlineRig r;
    AUREA_CHECK(r.ok); if (!r.ok) return;
    r.place_group(100, 20);
    Command seek; seek.type = CommandType::PlaybackSeek; seek.seek.time = tick_at(FrameIndex{150}, 30.0);
    AUREA_CHECK(r.e.apply_command(seek).ok());
    AUREA_CHECK_EQ(r.e.read_status().playhead.value, 150ll);
    const u32 depthBefore = r.e.history().depth();
    // Mover B (fora: [110,170) → [140,200)): dentro, [30,90) → [60,120).
    const u64 ids[1] = {r.innerB};
    const i64 starts[1] = {140}, ends[1] = {200};
    AUREA_CHECK(r.e.set_group_layer_ranges(r.group, ids, starts, ends, 1).ok());
    const Layer* b = r.inner(r.innerB);
    AUREA_CHECK(b && b->start.value == 60 && b->end.value == 120);
    AUREA_CHECK_EQ(r.row(r.innerB).startFrame, 140);
    AUREA_CHECK_EQ(r.e.history().depth(), depthBefore + 1);
    AUREA_CHECK_EQ(r.e.precomp_depth(), 0u);
    AUREA_CHECK(r.e.project()->timeline().current() == r.e.project()->timeline().root());
    AUREA_CHECK_EQ(r.e.read_status().playhead.value, 150ll);
    // Aparar o fim de A no tempo de fora (fora 140 → dentro 60 → 50).
    AUREA_CHECK(r.e.edit_group_clip_time(r.group, r.innerA, 1, 130));
    const Layer* a = r.inner(r.innerA);
    AUREA_CHECK(a && a->end.value == 50);
    AUREA_CHECK_EQ(r.row(r.innerA).endFrame, 130);
    AUREA_CHECK_EQ(r.e.read_status().playhead.value, 150ll);
    // Desfazer volta cada passo na composição de dentro, sem entrar nela.
    Command undo; undo.type = CommandType::Undo;
    AUREA_CHECK(r.e.apply_command(undo).ok());
    a = r.inner(r.innerA);
    AUREA_CHECK(a && a->end.value == 60);
    AUREA_CHECK(r.e.apply_command(undo).ok());
    b = r.inner(r.innerB);
    AUREA_CHECK(b && b->start.value == 30 && b->end.value == 90);
    AUREA_CHECK_EQ(r.e.precomp_depth(), 0u);
    // Antes do começo da composição de dentro: recusado, nada muda.
    const i64 early[1] = {50}, earlyEnd[1] = {70};   // dentro: −30
    const u32 depthRefused = r.e.history().depth();
    AUREA_CHECK(!r.e.set_group_layer_ranges(r.group, ids, early, earlyEnd, 1).ok());
    AUREA_CHECK_EQ(r.e.history().depth(), depthRefused);
}

AUREA_TEST(GroupInline, RemappedGroupShowsRowsButRefusesInPlaceEdits) {
    GroupInlineRig r;
    AUREA_CHECK(r.ok); if (!r.ok) return;
    r.group_layer()->speed = 2.0f;
    const bridge::LayerRow ra = r.row(r.innerA);
    AUREA_CHECK(ra.id == r.innerA);
    AUREA_CHECK(!(ra.reserved0 & Engine::kGroupRowEditable));
    const u64 ids[1] = {r.innerA};
    const i64 starts[1] = {10}, ends[1] = {40};
    AUREA_CHECK(r.e.set_group_layer_ranges(r.group, ids, starts, ends, 1).code() == Errc::NotSupported);
    AUREA_CHECK(!r.e.edit_group_clip_time(r.group, r.innerA, 0, 10));
    const Layer* a = r.inner(r.innerA);
    AUREA_CHECK(a && a->start.value == 0 && a->end.value == 60);
}

AUREA_TEST(GroupInline, ThumbnailsOfInnerPhotosUseTheOuterTime) {
    Engine e;
    AUREA_CHECK(e.initialize(headless_config()).ok());
    AUREA_CHECK(e.new_project(640, 360, 30., "foto no grupo").ok());
    std::vector<u8> px(64 * 32 * 4, 200);
    const auto photo = e.import_image(px.data(), 64, 32, "foto");
    AUREA_CHECK(photo.ok()); if (!photo.ok()) return;
    const u64 ids[1] = {*photo};
    const auto group = e.precompose(ids, 1, "Grupo");
    AUREA_CHECK(group.ok()); if (!group.ok()) return;
    bridge::LayerRow rows[2]; char names[32];
    AUREA_CHECK_EQ(e.query_group_layers(*group, rows, 2, names, sizeof(names)), 1u);
    std::vector<u8> thumb(64 * 64 * 4); u32 w = 0;
    AUREA_CHECK(e.query_group_thumbnail(*group, rows[0].id, rows[0].startFrame, 8, thumb.data(), static_cast<u32>(thumb.size()), &w) > 0);
    AUREA_CHECK_EQ(w, 16u);
    // O mesmo id fora do grupo não é a foto (cada composição tem os seus ids).
    AUREA_CHECK_EQ(e.query_group_thumbnail(0, rows[0].id, 0, 8, thumb.data(), static_cast<u32>(thumb.size()), &w), 0u);
}
