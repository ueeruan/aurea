// =============================================================================
//  Modo Edição (timeline magnética): aparar empurra/puxa, excluir fecha o
//  buraco, remover espaços vazios, aparar o projeto — tudo com desfazer.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aurea;

namespace {

struct EditRig {
    Engine e;
    u64 a = 0, b = 0, c = 0;
    EditRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        // Três clipes em fila: A [0,30) B [30,60) C [60,90).
        a = *e.add_null(false);
        b = *e.add_null(false);
        c = *e.add_null(false);
        range(a, 0, 30);
        range(b, 30, 60);
        range(c, 60, 90);
    }
    ~EditRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    const Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    void range(u64 id, i64 s, i64 en, i64 offset = -1) {
        Command cmd;
        cmd.type = CommandType::LayerSetTimeRange;
        cmd.layer_range.layer = LayerId::unpack(id);
        cmd.layer_range.start = FrameIndex{s};
        cmd.layer_range.end = FrameIndex{en};
        cmd.layer_range.offset = FrameIndex{offset < 0 ? 0 : offset};
        cmd.layer_range.setOffset = offset >= 0;
        AUREA_CHECK(e.apply_command(cmd).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    bool at(u64 id, i64 s, i64 en) { const Layer* l = L(id); return l && l->start.value == s && l->end.value == en; }
};

} // namespace

AUREA_TEST(Edit, CompositionModeTrimIsFree) {
    EditRig r;
    r.range(r.a, 0, 20);
    AUREA_CHECK(r.at(r.a, 0, 20));
    AUREA_CHECK(r.at(r.b, 30, 60));   // ninguém anda
    AUREA_CHECK(r.at(r.c, 60, 90));
}

AUREA_TEST(Edit, EditModeTrimEndRipples) {
    EditRig r;
    r.e.set_edit_mode(true);
    AUREA_CHECK(r.e.toggle_marker(75));
    r.range(r.a, 0, 20);                // encurta 10: B e C recuam 10
    AUREA_CHECK(r.at(r.a, 0, 20));
    AUREA_CHECK(r.at(r.b, 20, 50));
    AUREA_CHECK(r.at(r.c, 50, 80));
    i64 m[3] = {};
    AUREA_CHECK_EQ(r.e.query_markers(m, 1), 1u);
    AUREA_CHECK_EQ(m[0], 65);           // a marca anda junto
    r.range(r.b, 20, 70);               // alonga B 20: C avança
    AUREA_CHECK(r.at(r.c, 70, 100));
    r.undo();
    AUREA_CHECK(r.at(r.b, 20, 50));
    AUREA_CHECK(r.at(r.c, 50, 80));
}

AUREA_TEST(Edit, EditModeTrimStartKeepsThePlaceAndPullsTheRest) {
    EditRig r;
    r.e.set_edit_mode(true);
    // Aparar 12 do começo de B (conteúdo anda 12 no offset).
    r.range(r.b, 42, 60, 12);
    AUREA_CHECK(r.at(r.b, 30, 48));     // continua encostado em A
    AUREA_CHECK_EQ(r.L(r.b)->offset.value, 12);
    AUREA_CHECK(r.at(r.c, 48, 78));
    AUREA_CHECK(r.at(r.a, 0, 30));
    // Mover (início e fim juntos) não empurra ninguém.
    r.range(r.b, 100, 118, 12);
    AUREA_CHECK(r.at(r.b, 100, 118));
    AUREA_CHECK(r.at(r.c, 48, 78));
}

AUREA_TEST(Edit, RippleDeleteClosesOnlyTheHoleItMade) {
    EditRig r;
    // D cobre parte do trecho de B: onde D está não há buraco.
    const u64 d = *r.e.add_null(false);
    r.range(d, 40, 50);
    AUREA_CHECK(r.e.toggle_marker(80));
    AUREA_CHECK(r.e.ripple_delete(&r.b, 1));
    // B ocupava [30,60); D segura [40,50): buracos [30,40) e [50,60) = 20.
    AUREA_CHECK(r.L(r.b) == nullptr);
    AUREA_CHECK(r.at(r.a, 0, 30));
    AUREA_CHECK(r.at(d, 30, 40));
    AUREA_CHECK(r.at(r.c, 40, 70));
    i64 m[3] = {};
    AUREA_CHECK_EQ(r.e.query_markers(m, 1), 1u);
    AUREA_CHECK_EQ(m[0], 60);
    r.undo();
    AUREA_CHECK(r.L(r.b) != nullptr);
    AUREA_CHECK(r.at(r.c, 60, 90));
    AUREA_CHECK(r.at(d, 40, 50));
}

AUREA_TEST(Edit, RemoveGapsAndTrimProject) {
    EditRig r;
    r.range(r.a, 10, 30);   // buraco inicial [0,10)
    r.range(r.c, 80, 110);  // buraco [60,80)
    AUREA_CHECK_EQ(r.e.remove_gaps(), 30);
    AUREA_CHECK(r.at(r.a, 0, 20));
    AUREA_CHECK(r.at(r.b, 20, 50));
    AUREA_CHECK(r.at(r.c, 50, 80));
    AUREA_CHECK_EQ(r.e.remove_gaps(), 0);      // nada mais a fechar
    r.undo();
    AUREA_CHECK(r.at(r.c, 80, 110));
    AUREA_CHECK(r.at(r.a, 10, 30));
    // Aparar o projeto em 70: C (começa em 80) sai; B fica; duração 70.
    AUREA_CHECK(r.e.trim_composition(70));
    AUREA_CHECK_EQ(r.comp()->duration().value, 70);
    AUREA_CHECK(r.L(r.c) == nullptr);
    AUREA_CHECK(r.at(r.b, 30, 60));
}

AUREA_TEST(Edit, EditModeSurvivesSaveAndReopen) {
    EditRig r;
    r.e.set_edit_mode(true);
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_edicao.aurea";
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    AUREA_CHECK(r.e.edit_mode());
    std::remove(path.c_str());
}
