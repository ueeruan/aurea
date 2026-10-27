#include "TestFramework.hpp"
#include "aurea/Engine.hpp"
#include "aurea/timeline/GridLayout.hpp"
#include "aurea/render/Renderer.hpp"
#include <cmath>
#include "aurea/project/FileIO.hpp"
using namespace aurea;
AUREA_TEST(GridLayout, LayoutsMorphProximityAndIndividualAnimation) {
    Engine e;EngineConfig cfg;cfg.disableAutosave=true;cfg.workerCount=2;
    AUREA_CHECK(e.initialize(cfg).ok());AUREA_CHECK(e.new_project(1000,1000,30,nullptr).ok());
    std::vector<u64> ids;for(u32 i=0;i<6;++i){auto r=e.add_shape(0);AUREA_CHECK(r.ok());if(!r.ok())return;ids.push_back(*r);}
    auto result=e.create_grid(ids.data(),static_cast<u32>(ids.size()));AUREA_CHECK(result.ok());if(!result.ok())return;
    auto* comp=e.project()->timeline().composition(e.project()->timeline().root());
    auto* controller=comp->layer(LayerId::unpack(*result));auto& effect=controller->effects.front();
    auto* item=comp->layer(LayerId::unpack(ids[0]));
    const auto rect=grid::evaluate(*comp,*item,0);
    AUREA_CHECK_NEAR(rect.matrix.col[3].x,-220,.001f);AUREA_CHECK_NEAR(rect.matrix.col[3].y,-110,.001f);
    effect.params[0].constant=ParamValue::scalar(1);auto radial=grid::evaluate(*comp,*item,0);
    AUREA_CHECK_NEAR(radial.matrix.col[3].x,350,.001f);AUREA_CHECK_NEAR(radial.matrix.col[3].y,0,.001f);
    effect.params[0].constant=ParamValue::scalar(0);effect.params[11].constant=ParamValue::scalar(50);
    auto morph=grid::evaluate(*comp,*item,0);AUREA_CHECK_NEAR(morph.matrix.col[3].x,65,.001f);
    effect.params[11].constant=ParamValue::scalar(0);
    auto& track=controller->tracks.get_or_create(TrackProperty::EffectParam,effect.id,11*4);track.set(FrameIndex{0},0);track.set(FrameIndex{30},100);
    AUREA_CHECK_NEAR(grid::evaluate(*comp,*item,15).matrix.col[3].x,65,.001f);
    effect.params[15].constant=ParamValue::scalar(-220);effect.params[16].constant=ParamValue::scalar(-110);effect.params[19].constant=ParamValue::scalar(100);effect.params[23].constant=ParamValue::scalar(0);
    AUREA_CHECK_NEAR(grid::evaluate(*comp,*item,0).opacity,0,.001f);
    effect.params[19].constant=ParamValue::scalar(0);
    const auto original=layer_world_matrix(*comp,*item,FrameIndex{0});
    item->transform.position.x+=17;
    const auto edited=layer_world_matrix(*comp,*item,FrameIndex{0});
    AUREA_CHECK_NEAR(edited.col[3].x-original.col[3].x,17,.001f);
    for(u32 mode=0;mode<4;++mode){effect.params[0].constant=ParamValue::scalar(static_cast<f32>(mode));for(auto id:ids){const auto p=grid::evaluate(*comp,*comp->layer(LayerId::unpack(id)),0);for(auto col:p.matrix.col)AUREA_CHECK(std::isfinite(col.x)&&std::isfinite(col.y)&&std::isfinite(col.z));}}
    // Save/load retains the controller, membership, parameter keyframes and offsets.
    AUREA_CHECK(e.save_project("aurea_test_grid.aurea").ok());
    AUREA_CHECK(e.load_project("aurea_test_grid.aurea").ok());
    comp=e.project()->timeline().composition(e.project()->timeline().root());
    controller=comp->layer(LayerId::unpack(*result));item=comp->layer(LayerId::unpack(ids[0]));
    AUREA_CHECK(controller&&item);if(!controller||!item)return;
    AUREA_CHECK_EQ(item->parent,LayerId::unpack(*result));
    controller->effects.front().params[0].constant=ParamValue::scalar(0);
    const auto duplicate=comp->duplicate_layer(LayerId::unpack(ids[0]),FrameIndex{0});
    AUREA_CHECK(duplicate.valid());
    auto a=grid::evaluate(*comp,*item,0).matrix.col[3],b=grid::evaluate(*comp,*comp->layer(duplicate),0).matrix.col[3];
    AUREA_CHECK(a.x!=b.x||a.y!=b.y);
    AUREA_CHECK(comp->remove_layer(duplicate));
    AUREA_CHECK_NEAR(grid::evaluate(*comp,*item,15).matrix.col[3].x,65,.001f);
    e.shutdown();fileio::remove_file("aurea_test_grid.aurea");
}
AUREA_TEST(GridLayout, FullCompositionRefusesWithoutMutation) {
    Engine e;EngineConfig cfg;cfg.disableAutosave=true;cfg.workerCount=2;
    AUREA_CHECK(e.initialize(cfg).ok());AUREA_CHECK(e.new_project(100,100,30,nullptr).ok());
    auto* comp=e.project()->timeline().composition(e.project()->timeline().root());
    for(u32 i=0;i<kMaxLayerCount;++i)(void)comp->add_layer(LayerKind::Shape,"layer");
    const u64 id=comp->order().at(0).pack();
    AUREA_CHECK(!e.create_grid(&id,1).ok());AUREA_CHECK_EQ(comp->order().size(),kMaxLayerCount);
    AUREA_CHECK(!comp->layer(LayerId::unpack(id))->parent.valid());e.shutdown();
}

// =============================================================================
// Omega grid (0.3.5): a grade "controlada por" um nulo (nulo de controle)
// =============================================================================
namespace {
struct GridLinkRig {
    Engine e;
    std::vector<u64> items;
    u64 grid = 0;
    GridLinkRig() {
        EngineConfig cfg; cfg.disableAutosave = true; cfg.workerCount = 2;
        AUREA_CHECK(e.initialize(cfg).ok()); AUREA_CHECK(e.new_project(1000, 1000, 30, nullptr).ok());
        for (u32 i = 0; i < 6; ++i) { auto r = e.add_shape(0); AUREA_CHECK(r.ok()); if (r.ok()) items.push_back(*r); }
        auto g = e.create_grid(items.data(), static_cast<u32>(items.size()));
        AUREA_CHECK(g.ok()); if (g.ok()) grid = *g;
    }
    ~GridLinkRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().root()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    /// Onde a célula está no mundo: o ponto de ancoragem da camada.
    Vec3 cell(u64 id) {
        const Layer* l = L(id);
        const Vec4 p = layer_world_matrix(*comp(), *l, FrameIndex{0}) * Vec4{l->transform.anchor.x, l->transform.anchor.y, l->transform.anchor.z, 1};
        return Vec3{p.x, p.y, p.z};
    }
    std::vector<Vec3> cells() { std::vector<Vec3> out; for (u64 id : items) out.push_back(cell(id)); return out; }
    u64 null3d(Vec3 at) {
        auto r = e.add_null(true); AUREA_CHECK(r.ok()); if (!r.ok()) return 0;
        L(*r)->transform.position = at;
        return *r;
    }
    Vec3 pivot(u64 id) { return cell(id); }
    void undo() { Command u; u.type = CommandType::Undo; AUREA_CHECK(e.apply_command(u).ok()); }
};

void check_same(const std::vector<Vec3>& a, const std::vector<Vec3>& b, f32 tol = .05f) {
    AUREA_CHECK_EQ(a.size(), b.size());
    for (usize i = 0; i < a.size() && i < b.size(); ++i) {
        AUREA_CHECK_NEAR(a[i].x, b[i].x, tol); AUREA_CHECK_NEAR(a[i].y, b[i].y, tol); AUREA_CHECK_NEAR(a[i].z, b[i].z, tol);
    }
}

/// Cada célula girou 90° em volta do eixo `axis` (0 X, 1 Y, 2 Z) passando por `p`:
/// a componente do eixo fica, o resto gira um quarto de volta com o mesmo raio.
void check_quarter_turn(const std::vector<Vec3>& before, const std::vector<Vec3>& after, Vec3 p, int axis) {
    for (usize i = 0; i < before.size() && i < after.size(); ++i) {
        const f32 d0[3] = {before[i].x - p.x, before[i].y - p.y, before[i].z - p.z};
        const f32 d1[3] = {after[i].x - p.x, after[i].y - p.y, after[i].z - p.z};
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        AUREA_CHECK_NEAR(d1[axis], d0[axis], .05f);
        const f32 r0 = std::sqrt(d0[u] * d0[u] + d0[v] * d0[v]), r1 = std::sqrt(d1[u] * d1[u] + d1[v] * d1[v]);
        AUREA_CHECK_NEAR(r1, r0, .05f);
        if (r0 > 1.f) AUREA_CHECK_NEAR((d0[u] * d1[u] + d0[v] * d1[v]) / (r0 * r1), 0.f, 1e-3f);
    }
}
} // namespace

// Vincular: nada pula; girar (X, Y, Z), escalar, mover e mudar a âncora do nulo
// transforma a grade inteira como um corpo em volta do pivô do nulo.
AUREA_TEST(GridControlNull, GridFollowsNullRotationScalePositionAndAnchor) {
    GridLinkRig r;
    if (!r.grid) return;
    AUREA_CHECK(r.e.is_grid_controller(r.grid));
    AUREA_CHECK(!r.e.is_grid_controller(r.items[0]));
    const auto before = r.cells();
    const u64 n = r.null3d(Vec3{700, 300, 0});
    AUREA_CHECK_EQ(r.e.grid_control_null(r.grid), u64{0});
    AUREA_CHECK_EQ(r.e.set_grid_control_null(r.grid, n), grid::kLinkOk);
    AUREA_CHECK_EQ(r.e.grid_control_null(r.grid), n);
    check_same(r.cells(), before);   // compensação: o vínculo não move nada
    const Vec3 p = r.pivot(n);
    AUREA_CHECK_NEAR(p.x, 700, .01f); AUREA_CHECK_NEAR(p.y, 300, .01f);
    for (int axis = 0; axis < 3; ++axis) {
        Vec3 rot{0, 0, 0};
        (axis == 0 ? rot.x : axis == 1 ? rot.y : rot.z) = 90;
        r.L(n)->transform.rotation = rot;
        check_quarter_turn(before, r.cells(), p, axis);
        r.L(n)->transform.rotation = Vec3{0, 0, 0};
        check_same(r.cells(), before);
    }
    r.L(n)->transform.scale = Vec3{2, 2, 2};
    {
        const auto scaled = r.cells();
        for (usize i = 0; i < before.size(); ++i) {
            AUREA_CHECK_NEAR(scaled[i].x - p.x, 2 * (before[i].x - p.x), .05f);
            AUREA_CHECK_NEAR(scaled[i].y - p.y, 2 * (before[i].y - p.y), .05f);
        }
    }
    r.L(n)->transform.scale = Vec3{1, 1, 1};
    r.L(n)->transform.position = Vec3{750, 320, 40};
    {
        const auto moved = r.cells();
        for (usize i = 0; i < before.size(); ++i) {
            AUREA_CHECK_NEAR(moved[i].x - before[i].x, 50, .05f);
            AUREA_CHECK_NEAR(moved[i].y - before[i].y, 20, .05f);
            AUREA_CHECK_NEAR(moved[i].z - before[i].z, 40, .05f);
        }
    }
    r.L(n)->transform.position = Vec3{700, 300, 0};
    // Âncora do nulo +10 em X: o conteúdo dele (a grade) anda −10 (como toda camada).
    r.L(n)->transform.anchor.x += 10;
    {
        const auto shifted = r.cells();
        for (usize i = 0; i < before.size(); ++i) AUREA_CHECK_NEAR(shifted[i].x - before[i].x, -10, .05f);
    }
    r.L(n)->transform.anchor.x -= 10;
    // Desvincular: fica onde está e deixa de seguir o nulo.
    r.L(n)->transform.rotation.z = 45;
    const auto turned = r.cells();
    AUREA_CHECK_EQ(r.e.set_grid_control_null(r.grid, 0), grid::kLinkOk);
    AUREA_CHECK_EQ(r.e.grid_control_null(r.grid), u64{0});
    check_same(r.cells(), turned);
    r.L(n)->transform.rotation.z = 0;
    check_same(r.cells(), turned);
    // Alvo que não é nulo / camada que não é grade.
    AUREA_CHECK_EQ(r.e.set_grid_control_null(r.grid, r.items[1]), grid::kLinkNotNull);
    AUREA_CHECK_EQ(r.e.set_grid_control_null(r.items[0], n), grid::kLinkNotGrid);
}

// Vínculo "infinito": uma cadeia de 40 nulos (mais funda que o teto antigo de 17);
// girar a raiz gira a grade em volta do pivô da raiz. Duas grades no mesmo nulo e
// uma grade num nulo filho de outro seguem juntas.
AUREA_TEST(GridControlNull, DeepNullChainsAndSeveralGridsFollowTheRoot) {
    GridLinkRig r;
    if (!r.grid) return;
    constexpr u32 kDepth = 40;
    std::vector<u64> chain;
    for (u32 i = 0; i < kDepth; ++i) chain.push_back(r.null3d(Vec3{400.f + 3.f * static_cast<f32>(i), 600.f - 2.f * static_cast<f32>(i), 0}));
    // chain[0] → chain[1] → … → chain[kDepth-1] (raiz), pelo vincular de sempre.
    for (u32 i = 0; i + 1 < kDepth; ++i) {
        Command c; c.type = CommandType::LayerSetParent;
        c.layer_parent.layer = LayerId::unpack(chain[i]); c.layer_parent.parent = LayerId::unpack(chain[i + 1]);
        AUREA_CHECK(r.e.apply_command(c).ok());
    }
    AUREA_CHECK_EQ(r.e.set_grid_control_null(r.grid, chain[0]), grid::kLinkOk);
    // Segunda grade, ligada a um nulo do MEIO da cadeia (grade dentro de grade).
    std::vector<u64> more;
    for (u32 i = 0; i < 4; ++i) { auto s = r.e.add_shape(0); AUREA_CHECK(s.ok()); if (s.ok()) more.push_back(*s); }
    auto second = r.e.create_grid(more.data(), static_cast<u32>(more.size()));
    AUREA_CHECK(second.ok()); if (!second.ok()) return;
    AUREA_CHECK_EQ(r.e.set_grid_control_null(*second, chain[kDepth / 2]), grid::kLinkOk);
    // Terceira grade no MESMO nulo da primeira.
    std::vector<u64> third;
    for (u32 i = 0; i < 3; ++i) { auto s = r.e.add_shape(0); AUREA_CHECK(s.ok()); if (s.ok()) third.push_back(*s); }
    auto thirdGrid = r.e.create_grid(third.data(), static_cast<u32>(third.size()));
    AUREA_CHECK(thirdGrid.ok()); if (!thirdGrid.ok()) return;
    AUREA_CHECK_EQ(r.e.set_grid_control_null(*thirdGrid, chain[0]), grid::kLinkOk);
    auto cellsOf = [&](const std::vector<u64>& ids) { std::vector<Vec3> out; for (u64 id : ids) out.push_back(r.cell(id)); return out; };
    const auto a0 = cellsOf(r.items), b0 = cellsOf(more), c0 = cellsOf(third);
    const Vec3 p = r.pivot(chain.back());
    r.L(chain.back())->transform.rotation.z = 90;
    check_quarter_turn(a0, cellsOf(r.items), p, 2);
    check_quarter_turn(b0, cellsOf(more), p, 2);
    check_quarter_turn(c0, cellsOf(third), p, 2);
    r.L(chain.back())->transform.rotation.z = 0;
    check_same(cellsOf(r.items), a0);
    // Girar um nulo do meio mexe só o que está abaixo dele.
    const Vec3 mid = r.pivot(chain[kDepth / 2]);
    r.L(chain[kDepth / 2])->transform.rotation.y = 90;
    check_quarter_turn(a0, cellsOf(r.items), mid, 1);
    check_quarter_turn(b0, cellsOf(more), mid, 1);
    r.L(chain[kDepth / 2])->transform.rotation.y = 0;
    check_same(cellsOf(more), b0);
}

// Ciclo: o nulo que já segue a grade (um elemento dela, direto ou no fim de uma
// cadeia) não pode controlá-la; o vincular comum também recusa fechar o ciclo,
// mesmo numa cadeia mais funda que o teto antigo.
AUREA_TEST(GridControlNull, CycleIsRefusedWithoutChangingTheProject) {
    Engine e; EngineConfig cfg; cfg.disableAutosave = true; cfg.workerCount = 2;
    AUREA_CHECK(e.initialize(cfg).ok()); AUREA_CHECK(e.new_project(1000, 1000, 30, nullptr).ok());
    auto* comp = e.project()->timeline().composition(e.project()->timeline().root());
    auto shape = e.add_shape(0); auto member = e.add_null(true);
    AUREA_CHECK(shape.ok() && member.ok()); if (!shape.ok() || !member.ok()) { e.shutdown(); return; }
    const u64 ids[2] = {*shape, *member};
    auto g = e.create_grid(ids, 2); AUREA_CHECK(g.ok()); if (!g.ok()) { e.shutdown(); return; }
    // O nulo-elemento é filho da grade: controlar a grade por ele fecharia o ciclo.
    AUREA_CHECK_EQ(e.set_grid_control_null(*g, *member), grid::kLinkCycle);
    AUREA_CHECK(!comp->layer(LayerId::unpack(*g))->parent.valid());
    AUREA_CHECK_EQ(e.set_grid_control_null(*g, *g), grid::kLinkCycle);
    // Cadeia de 3 nulos: grade → n0 → n1 → n2. Pôr n2 sob o elemento fecha o ciclo.
    std::vector<u64> chain;
    for (u32 i = 0; i < 3; ++i) { auto n = e.add_null(true); AUREA_CHECK(n.ok()); if (n.ok()) chain.push_back(*n); }
    if (chain.size() != 3) { e.shutdown(); return; }
    for (u32 i = 0; i + 1 < 3; ++i) {
        Command c; c.type = CommandType::LayerSetParent;
        c.layer_parent.layer = LayerId::unpack(chain[i]); c.layer_parent.parent = LayerId::unpack(chain[i + 1]);
        AUREA_CHECK(e.apply_command(c).ok());
    }
    AUREA_CHECK_EQ(e.set_grid_control_null(*g, chain[0]), grid::kLinkOk);
    AUREA_CHECK(grid::would_cycle(*comp, LayerId::unpack(chain[2]), LayerId::unpack(*member)));
    Command close; close.type = CommandType::LayerSetParent;
    close.layer_parent.layer = LayerId::unpack(chain[2]); close.layer_parent.parent = LayerId::unpack(*member);
    AUREA_CHECK(!e.apply_command(close).ok());
    AUREA_CHECK(!comp->layer(LayerId::unpack(chain[2]))->parent.valid());
    // Cadeia de 40 (mais que o teto antigo de 32 passos do vincular): o ciclo também é visto.
    std::vector<u64> deep;
    for (u32 i = 0; i < 40; ++i) { auto n = e.add_null(true); AUREA_CHECK(n.ok()); if (n.ok()) deep.push_back(*n); }
    for (u32 i = 0; i + 1 < deep.size(); ++i) {
        Command c; c.type = CommandType::LayerSetParent;
        c.layer_parent.layer = LayerId::unpack(deep[i]); c.layer_parent.parent = LayerId::unpack(deep[i + 1]);
        AUREA_CHECK(e.apply_command(c).ok());
    }
    Command loop; loop.type = CommandType::LayerSetParent;
    loop.layer_parent.layer = LayerId::unpack(deep.back()); loop.layer_parent.parent = LayerId::unpack(deep.front());
    AUREA_CHECK(!e.apply_command(loop).ok());
    AUREA_CHECK(!comp->layer(LayerId::unpack(deep.back()))->parent.valid());
    // O mundo de uma cadeia corrompida (ciclo vindo de fora) continua finito.
    comp->layer(LayerId::unpack(deep.back()))->parent = LayerId::unpack(deep.front());
    const Mat4 w = layer_world_matrix(*comp, *comp->layer(LayerId::unpack(deep[5])), FrameIndex{0});
    for (const Vec4& col : w.col) AUREA_CHECK(std::isfinite(col.x) && std::isfinite(col.y) && std::isfinite(col.z));
    comp->layer(LayerId::unpack(deep.back()))->parent = LayerId{};
    e.shutdown();
}

// Desfazer volta o vínculo num passo; "Criar nulo de controle" põe o nulo no
// centro da grade, já ligado, sem mover nada, e desfaz num passo. O vínculo é
// salvo e volta ao abrir (pai + base de vínculo, já no formato do projeto).
AUREA_TEST(GridControlNull, UndoCreateControlNullAndSaveLoad) {
    GridLinkRig r;
    if (!r.grid) return;
    const auto before = r.cells();
    const u64 n = r.null3d(Vec3{650, 420, 0});
    AUREA_CHECK_EQ(r.e.set_grid_control_null(r.grid, n), grid::kLinkOk);
    r.undo();
    AUREA_CHECK(!r.L(r.grid)->parent.valid());
    check_same(r.cells(), before);
    // Criar nulo de controle: um passo, no centro dos elementos.
    const u32 count = r.comp()->layers().count();
    auto made = r.e.create_grid_control_null(r.grid, "Controle da grade");
    AUREA_CHECK(made.ok()); if (!made.ok()) return;
    AUREA_CHECK_EQ(r.comp()->layers().count(), count + 1);
    const Layer* control = r.L(*made);
    AUREA_CHECK(control && control->kind == LayerKind::Null && control->threeD);
    AUREA_CHECK(control && control->name == "Controle da grade");
    AUREA_CHECK_EQ(r.e.grid_control_null(r.grid), *made);
    Vec3 center{0, 0, 0};
    for (const Vec3& c : before) center = Vec3{center.x + c.x, center.y + c.y, center.z + c.z};
    const f32 k = 1.f / static_cast<f32>(before.size());
    const Vec3 pivot = r.pivot(*made);
    AUREA_CHECK_NEAR(pivot.x, center.x * k, .05f); AUREA_CHECK_NEAR(pivot.y, center.y * k, .05f);
    check_same(r.cells(), before);
    r.L(*made)->transform.rotation.z = 90;
    check_quarter_turn(before, r.cells(), pivot, 2);
    r.L(*made)->transform.rotation.z = 0;
    r.undo();   // UM passo: some o nulo e o vínculo
    AUREA_CHECK_EQ(r.comp()->layers().count(), count);
    AUREA_CHECK(!r.L(r.grid)->parent.valid());
    check_same(r.cells(), before);
    // Salvar e abrir.
    AUREA_CHECK_EQ(r.e.set_grid_control_null(r.grid, n), grid::kLinkOk);
    r.L(n)->transform.rotation = Vec3{20, -35, 60};
    r.L(n)->transform.scale = Vec3{1.5f, 1.5f, 1.5f};
    const auto saved = r.cells();
    AUREA_CHECK(r.e.save_project("aurea_test_grid_control.aurea").ok());
    AUREA_CHECK(r.e.load_project("aurea_test_grid_control.aurea").ok());
    AUREA_CHECK(r.L(r.grid) && r.L(n));
    if (!r.L(r.grid) || !r.L(n)) { fileio::remove_file("aurea_test_grid_control.aurea"); return; }
    AUREA_CHECK_EQ(r.e.grid_control_null(r.grid), n);
    check_same(r.cells(), saved);
    r.L(n)->transform.rotation = Vec3{0, 0, 0};
    r.L(n)->transform.scale = Vec3{1, 1, 1};
    check_same(r.cells(), before);
    fileio::remove_file("aurea_test_grid_control.aurea");
}
