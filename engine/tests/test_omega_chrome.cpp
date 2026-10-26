// =============================================================================
//  Omega 0.3.2, área 02 (moldura do editor): trilha de grupos (P0-2), barra
//  inferior da seleção múltipla (P0-4), colar estilo por parte (P1-3), modos de
//  visão da prévia (P2-1) e setas do transporte (P2-3).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <string>
#include <vector>

using namespace aurea;

namespace {

struct ChromeRig {
    Engine e;
    ChromeRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~ChromeRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 null_layer(i64 start, i64 end) {
        const u64 id = *e.add_null(false);
        range(id, start, end);
        return id;
    }
    void range(u64 id, i64 s, i64 en) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        cmd.layer_range.layer = LayerId::unpack(id);
        cmd.layer_range.start = FrameIndex{s};
        cmd.layer_range.end = FrameIndex{en};
        cmd.layer_range.offset = FrameIndex{0};
        cmd.layer_range.setOffset = false;
        AUREA_CHECK(e.apply_command(cmd).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    i64 step(u64 layer, i64 from, i32 direction) {
        i64 target = -1;
        return e.query_transport_navigation(layer, from, direction, target) ? target : -1;
    }
};

std::vector<std::string> split_trail(const std::string& s) {
    std::vector<std::string> out(1);
    for (const char c : s) {
        if (c == '\x1F') out.emplace_back();
        else out.back().push_back(c);
    }
    return out;
}

} // namespace

AUREA_TEST(OmegaChrome, TrailListsTheOpenGroupsFromTheRoot) {
    ChromeRig r;
    AUREA_CHECK_EQ(split_trail(r.e.composition_trail()).size(), static_cast<usize>(1));
    const u64 a = r.null_layer(0, 30);
    const u64 group = *r.e.precompose(&a, 1, "Grupo de fora");
    AUREA_CHECK(r.e.open_precomp(group));
    const auto trail = split_trail(r.e.composition_trail());
    AUREA_CHECK_EQ(trail.size(), static_cast<usize>(2));
    AUREA_CHECK(trail.back() == r.e.current_composition_name());
    AUREA_CHECK(r.e.close_precomp());
    AUREA_CHECK_EQ(split_trail(r.e.composition_trail()).size(), static_cast<usize>(1));
}

AUREA_TEST(OmegaChrome, TransportArrowsAddTheEdgesOfTheProjectAndOfTheLayer) {
    ChromeRig r;
    const u64 a = r.null_layer(20, 50);
    AUREA_CHECK(r.e.toggle_marker(35));
    const i64 last = r.comp()->duration().value - 1;
    // Sem camada: começo, marcadores e o último quadro.
    AUREA_CHECK_EQ(r.step(0, 10, 1), static_cast<i64>(35));
    AUREA_CHECK_EQ(r.step(0, 35, 1), last);
    AUREA_CHECK_EQ(r.step(0, 10, -1), static_cast<i64>(0));
    AUREA_CHECK_EQ(r.step(0, 0, -1), static_cast<i64>(-1));
    AUREA_CHECK_EQ(r.step(0, last, 1), static_cast<i64>(-1));
    // Com a camada: o início e o fim dela também.
    AUREA_CHECK_EQ(r.step(a, 10, 1), static_cast<i64>(20));
    AUREA_CHECK_EQ(r.step(a, 35, 1), static_cast<i64>(50));
    AUREA_CHECK_EQ(r.step(a, 35, -1), static_cast<i64>(20));
    AUREA_CHECK_EQ(r.step(a, 20, -1), static_cast<i64>(0));
}

AUREA_TEST(OmegaChrome, MultiSelectionBottomBarFollowsThePlayhead) {
    ChromeRig r;
    const u64 ids[2] = {r.null_layer(20, 50), r.null_layer(30, 60)};
    AUREA_CHECK_EQ(r.e.query_multi_time_actions(ids, 2, 25), 0u);   // cruza: cortes
    AUREA_CHECK_EQ(r.e.query_multi_time_actions(ids, 2, 10), 1u);   // todas depois: o início
    AUREA_CHECK_EQ(r.e.query_multi_time_actions(ids, 2, 20), 1u);   // começa no cabeçote
    AUREA_CHECK_EQ(r.e.query_multi_time_actions(ids, 2, 60), 2u);   // todas antes: o fim
    AUREA_CHECK_EQ(r.e.query_multi_time_actions(ids, 2, 90), 2u);
    AUREA_CHECK_EQ(r.e.query_multi_time_actions(nullptr, 0, 10), 0u);
}

AUREA_TEST(OmegaChrome, PasteStyleOnlyTheChosenParts) {
    ChromeRig r;
    const u64 a = *r.e.add_shape(0);
    const u64 b = *r.e.add_shape(0);
    r.L(a)->shape.fillColor = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    r.L(a)->shape.strokeWidth = 7.0f;
    r.L(a)->blendMode = BlendMode::Multiply;
    r.L(a)->transform.opacity = 0.25f;
    r.L(a)->transform.position = Vec3{11.0f, 22.0f, 0.0f};
    r.L(b)->shape.fillColor = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
    AUREA_CHECK_EQ(r.e.copied_style_parts(), 0u);
    AUREA_CHECK(r.e.copy_style(a));
    const u32 parts = r.e.copied_style_parts();
    AUREA_CHECK((parts & Engine::StyleColorFill) != 0);
    AUREA_CHECK((parts & Engine::StyleBorderShadow) != 0);
    AUREA_CHECK((parts & Engine::StyleBlendOpacity) != 0);
    AUREA_CHECK((parts & Engine::StyleTransform) != 0);
    AUREA_CHECK((parts & Engine::StyleText) == 0);
    AUREA_CHECK((parts & Engine::StyleVolume) == 0);
    // Só a cor: a mesclagem, a opacidade e a posição ficam.
    AUREA_CHECK_EQ(r.e.paste_style_parts(&b, 1, Engine::StyleColorFill), 1u);
    AUREA_CHECK_EQ(r.L(b)->shape.fillColor.x, 1.0f);
    AUREA_CHECK_EQ(r.L(b)->shape.fillColor.z, 0.0f);
    AUREA_CHECK(r.L(b)->blendMode == BlendMode::Normal);
    AUREA_CHECK_EQ(r.L(b)->transform.opacity, 1.0f);
    AUREA_CHECK_EQ(r.L(b)->shape.strokeWidth, 0.0f);
    // Mesclagem e opacidade + transformar, num passo de desfazer.
    AUREA_CHECK_EQ(r.e.paste_style_parts(&b, 1, Engine::StyleBlendOpacity | Engine::StyleTransform), 1u);
    AUREA_CHECK(r.L(b)->blendMode == BlendMode::Multiply);
    AUREA_CHECK_EQ(r.L(b)->transform.opacity, 0.25f);
    AUREA_CHECK_EQ(r.L(b)->transform.position.x, 11.0f);
    r.undo();
    AUREA_CHECK(r.L(b)->blendMode == BlendMode::Normal);
    AUREA_CHECK_EQ(r.L(b)->transform.opacity, 1.0f);
    AUREA_CHECK_EQ(r.L(b)->shape.fillColor.x, 1.0f);   // a colagem anterior fica
    // Camada travada não muda.
    r.L(b)->locked = true;
    AUREA_CHECK_EQ(r.e.paste_style_parts(&b, 1, Engine::StyleBorderShadow), 0u);
}

AUREA_TEST(OmegaChrome, PreviewViewModesAreKeptAndClamped) {
    ChromeRig r;
    const u64 a = r.null_layer(0, 30);
    AUREA_CHECK_EQ(r.e.preview_view_mode(), 0u);
    r.e.set_preview_view(1, &a, 1, false);
    AUREA_CHECK_EQ(r.e.preview_view_mode() & 0xFFu, 1u);
    AUREA_CHECK_EQ(r.e.preview_view_mode() >> 16, 1u);
    r.e.set_preview_view(2, &a, 1, true);
    AUREA_CHECK_EQ(r.e.preview_view_mode() & 0xFFu, 2u);
    AUREA_CHECK((r.e.preview_view_mode() & 0x100u) != 0);
    // Modo desconhecido volta ao normal e não guarda ids.
    r.e.set_preview_view(9, &a, 1, false);
    AUREA_CHECK_EQ(r.e.preview_view_mode(), 0u);
}
