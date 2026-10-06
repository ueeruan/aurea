// Shared-engine regressions for outline changes on transformed text.
AUREA_TEST(TextOutline, AddingStrokePreservesCustomAndKeyframedAnchorsInPerspective) {
    AUREA_REQUIRE_GPU();
    std::filesystem::create_directories("build/reference/community-fixes-20261005");
    for (u32 anchorMode = 0; anchorMode < 3; ++anchorMode) {
        for (const f32 scale : {.55f, 1.f, 1.75f}) {
            for (const bool perspective : {false, true}) {
                Scene3DRig rig(640, 360);
                const auto id = rig.e.add_text("AVAVA");
                AUREA_CHECK(id.ok()); if (!id.ok()) return;
                auto* comp = current_comp(rig.e);
                auto* layer = comp->layer(LayerId::unpack(*id));
                layer->text.size = 88;
                layer->text.tracking = -55;
                layer->text.color = {1, 1, 1, 1};
                layer->text.strokeColor = {1, 0, 0, 1};
                // Set size through the same editing command so the base text
                // box is initialized before testing the independent outline.
                Command size; size.type = CommandType::TextSetSize;
                size.text_size.layer = LayerId::unpack(*id); size.text_size.size = 88;
                AUREA_CHECK(rig.e.apply_command(size).ok());
                auto stroke = [&](f32 width) {
                    Command command; command.type = CommandType::TextSetStrokeWidth;
                    command.text_stroke_width.layer = LayerId::unpack(*id);
                    command.text_stroke_width.width = width;
                    AUREA_CHECK(rig.e.apply_command(command).ok());
                };
                stroke(0);
                layer->transform.scale = {scale, scale, 1};
                if (perspective) layer->transform.rotation = {14, 48, -9};
                if (anchorMode == 1) layer->transform.anchor = layer->transform.anchor + Vec3{24, -15, 0};
                if (anchorMode == 2) {
                    for (const auto property : {TrackProperty::AnchorX, TrackProperty::AnchorY}) {
                        Track track; track.property = property;
                        track.staticValue = property == TrackProperty::AnchorX ? layer->transform.anchor.x : layer->transform.anchor.y;
                        Keyframe first; first.time = FrameIndex{0}; first.value = track.staticValue;
                        first.interp = Interpolation::Bezier; first.bx1 = .22f; first.by1 = .1f;
                        first.bx2 = .75f; first.by2 = .9f; first.tangentIn = -2.5f; first.tangentOut = 3.75f;
                        Keyframe last = first; last.time = FrameIndex{30}; last.value += property == TrackProperty::AnchorX ? 24.f : -15.f;
                        track.keys = {first, last};
                        layer->tracks.add(std::move(track));
                    }
                    seek_frame(rig.e, 12);
                }
                const auto anchorsBefore = layer->tracks;
                const Vec3 anchorBefore = layer->transform.anchor;
                const auto plain = rig.capture(640);
                stroke(18);
                AUREA_CHECK_NEAR(layer->transform.anchor.x, anchorBefore.x + 18, .001f);
                AUREA_CHECK_NEAR(layer->transform.anchor.y, anchorBefore.y + 18, .001f);
                AUREA_CHECK_NEAR(layer->transform.anchor.z, anchorBefore.z, .001f);
                for (const TrackProperty property : {TrackProperty::AnchorX, TrackProperty::AnchorY}) {
                    const auto* before = anchorsBefore.find(property);
                    const auto* after = layer->tracks.find(property);
                    if (!before) continue;
                    AUREA_CHECK(after != nullptr); if (!after) return;
                    AUREA_CHECK_EQ(before->keys.size(), after->keys.size());
                    for (usize k = 0; k < before->keys.size(); ++k) {
                        const auto& a = before->keys[k]; const auto& b = after->keys[k];
                        AUREA_CHECK_EQ(a.time.value, b.time.value);
                        AUREA_CHECK_NEAR(b.value, a.value + 18, .001f);
                        AUREA_CHECK(a.interp == b.interp && a.bx1 == b.bx1 && a.by1 == b.by1 && a.bx2 == b.bx2 && a.by2 == b.by2);
                        AUREA_CHECK(a.tangentIn == b.tangentIn && a.tangentOut == b.tangentOut);
                    }
                }
                const auto outlined = rig.capture(640);
                AUREA_CHECK_EQ(plain.rgba.size(), outlined.rgba.size());
                if (plain.rgba.size() != outlined.rgba.size()) return;
                u32 filled = 0, lost = 0, red = 0;
                for (u32 y = 1; y + 1 < plain.height; ++y) for (u32 x = 1; x + 1 < plain.width; ++x) {
                    const auto* after = outlined.at(x, y);
                    red += after[0] > 200 && after[1] < 80;
                    if (plain.at(x, y)[1] < 250) continue;
                    ++filled;
                    bool remains = false;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                        remains |= outlined.at(x + dx, y + dy)[1] >= 235;
                    lost += !remains;
                }
                std::printf("    anchor %u, scale %.2f, perspective %u: filled %u, moved/covered %u, outline %u\n",
                            anchorMode, scale, perspective ? 1u : 0u, filled, lost, red);
                AUREA_CHECK(filled > 100);
                AUREA_CHECK(red > 100);
                AUREA_CHECK_MSG(lost * 20 <= filled, "adding text stroke must not move the filled glyphs or their animation");
                const std::string prefix = "build/reference/community-fixes-20261005/outline-a" + std::to_string(anchorMode)
                    + "-s" + std::to_string(int(scale * 100)) + "-p" + std::to_string(perspective);
                (void)write_png(prefix + "-plain.png", plain);
                (void)write_png(prefix + "-stroke.png", outlined);
                stroke(0);
                AUREA_CHECK_MSG(max_diff(plain, rig.capture(640)) <= 2, "removing the outline must restore the original glyph placement");
            }
        }
    }
}
