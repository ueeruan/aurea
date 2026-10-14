// =============================================================================
//  Seções e dicas do painel de efeitos (P1-4 da especificação de 2026-10-14).
//
//  Seção é só APRESENTAÇÃO: agrupa parâmetros já declarados sob um título
//  recolhível (com interruptor opcional) sem mexer em índice nenhum — o índice é
//  a chave do parâmetro no projeto salvo. O estado aberto/recolhido mora na
//  instância e vai no byte `expanded` (bits 1..7), sem mudar o formato.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace aurea;

namespace {

std::string blob_str(const std::vector<char>& blob, u32 offset, u32 length) {
    if (length == 0 || offset + length > blob.size()) return {};
    return std::string(blob.data() + offset, length);
}

struct SectionRig {
    Engine e;
    u64 layer = 0;
    u32 effect = 0;
    bool ok = false;

    explicit SectionRig(const char* key) {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        if (!e.initialize(ec).ok() || !e.new_project(320, 180, 30.0, nullptr).ok()) return;
        std::vector<u8> px(32 * 32 * 4, 255);
        const Result<u64> id = e.import_image(px.data(), 32, 32, "quadro.png", nullptr);
        if (!id.ok()) return;
        layer = *id;
        Command add;
        add.type = CommandType::EffectAdd;
        add.effect_add.layer = LayerId::unpack(layer);
        add.effect_add.effectType = effect_type_id(key);
        if (!e.apply_command(add).ok()) return;
        const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        const Layer* l = comp ? comp->layer(LayerId::unpack(layer)) : nullptr;
        if (!l || l->effects.empty()) return;
        effect = l->effects.back().id;
        ok = true;
    }
    ~SectionRig() { e.shutdown(); }

    u32 query(std::vector<bridge::EffectParamRow>& rows, std::vector<char>& blob) {
        rows.assign(64, {});
        blob.assign(32 * 1024, 0);
        return e.query_effect_params(layer, effect, rows.data(), static_cast<u32>(rows.size()), blob.data(),
                                     static_cast<u32>(blob.size()));
    }
    const EffectInstance* instance() {
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        Layer* l = comp ? comp->layer(LayerId::unpack(layer)) : nullptr;
        return l ? l->find_effect(EffectId{effect, 0}) : nullptr;
    }
};

} // namespace

// O registro: seção sobre parâmetros já declarados, sem sobrepor; o interruptor
// é um Bool e pertence à seção; recusas não mudam nada.
AUREA_TEST(EffectSections, RegistryDeclaresSectionsWithoutTouchingIndices) {
    ParameterRegistry p;
    const u32 on = p.add_bool("on", "Ligar", true);
    const u32 a = p.add_float("a", "A", 0, 0, 1);
    const u32 b = p.add_float("b", "B", 0, 0, 1);
    const u32 c = p.add_float("c", "C", 0, 0, 1);
    const u32 d = p.add_color("d", "D", Vec4{1, 1, 1, 1});
    AUREA_CHECK_EQ(p.count(), 5u);

    AUREA_CHECK_EQ(p.add_section("um", "Um", {a, c}, on), 1u);
    AUREA_CHECK_EQ(p.at(a).section, 1u);
    AUREA_CHECK_EQ(p.at(c).section, 1u);
    AUREA_CHECK_EQ(p.at(on).section, 1u);   // o interruptor é do cabeçalho
    AUREA_CHECK_EQ(p.at(b).section, 0u);
    // Sobreposição, membro inexistente e interruptor que não é Bool: recusados.
    AUREA_CHECK_EQ(p.add_section("x", "X", {b, c}), kInvalidIndex);
    AUREA_CHECK_EQ(p.add_section("x", "X", {b, 99u}), kInvalidIndex);
    AUREA_CHECK_EQ(p.add_section("x", "X", {b}, d), kInvalidIndex);
    AUREA_CHECK_EQ(p.at(b).section, 0u);
    AUREA_CHECK_EQ(p.section_count(), 1u);
    AUREA_CHECK_EQ(p.add_section_range("dois", "Dois", b, b, kInvalidIndex, false), 2u);
    AUREA_CHECK_EQ(p.section_count(), 2u);
    AUREA_CHECK(p.section(0) == nullptr && p.section(3) == nullptr);
    AUREA_CHECK(std::string(p.section(1)->id) == "um" && p.section(1)->toggle == on && p.section(1)->open);
    AUREA_CHECK(std::string(p.section(2)->label) == "Dois" && p.section(2)->toggle == kInvalidIndex && !p.section(2)->open);
    // Dica: só leitura, vazia = sem dica.
    AUREA_CHECK_EQ(p.set_tip(d, "Dica"), d);
    AUREA_CHECK(std::string(p.at(d).tip) == "Dica");
    p.set_tip(d, "");
    AUREA_CHECK(p.at(d).tip == nullptr);
    AUREA_CHECK_EQ(p.set_tip(42, "x"), kInvalidIndex);
    // Limite de 7 seções (os 7 bits do estado persistido).
    for (u32 i = 0; i < 8; ++i) p.add_float("n", "N", 0, 0, 1);
    u32 accepted = 0;
    for (u32 i = 0; i < 8; ++i) accepted += p.add_section("s", "S", {5u + i}) != kInvalidIndex ? 1u : 0u;
    AUREA_CHECK_EQ(p.section_count(), kMaxParamSections);
    AUREA_CHECK_EQ(accepted, kMaxParamSections - 2u);
    // Estado: o declarado trocado pelo bit da instância.
    AUREA_CHECK(section_open(*p.section(1), 0u, 1));
    AUREA_CHECK(!section_open(*p.section(1), 0b1u, 1));
    AUREA_CHECK(!section_open(*p.section(2), 0b1u, 2));
    AUREA_CHECK(section_open(*p.section(2), 0b10u, 2));
}

// Todo o catálogo: seções válidas (interruptor Bool, ≤ 7, membros visíveis) e
// os efeitos que ganharam seções neste trabalho de fato as têm.
AUREA_TEST(EffectSections, CatalogSectionsAreWellFormed) {
    EffectRegistry reg;
    register_builtin_effects(reg);
    u32 withSections = 0, tips = 0;
    std::set<std::string> sectioned;
    for (u32 i = 0; i < reg.count(); ++i) {
        const ParameterRegistry& p = reg.params_at(i);
        AUREA_CHECK(p.section_count() <= kMaxParamSections);
        std::set<std::string> ids;
        for (u32 k = 1; k <= p.section_count(); ++k) {
            const ParamSection* s = p.section(k);
            AUREA_CHECK_MSG(s->id && *s->id && ids.insert(s->id).second, reg.at(i).info().key);
            AUREA_CHECK(s->label && *s->label);
            if (s->toggle != kInvalidIndex) {
                AUREA_CHECK(s->toggle < p.count() && p.at(s->toggle).type == ParamType::Bool);
                AUREA_CHECK_EQ(p.at(s->toggle).section, k);
            }
            u32 members = 0;
            for (u32 j = 0; j < p.count(); ++j) members += (p.at(j).section == k && j != s->toggle) ? 1u : 0u;
            AUREA_CHECK_MSG(members > 0, s->id);
        }
        for (u32 j = 0; j < p.count(); ++j) {
            AUREA_CHECK(p.at(j).section <= p.section_count());
            tips += p.at(j).tip ? 1u : 0u;
        }
        if (p.section_count() > 0) {
            ++withSections;
            sectioned.insert(reg.at(i).info().key);
        }
    }
    std::printf("    efeitos com secoes: %u; dicas: %u\n", withSections, tips);
    for (const char* key : {effect_keys::kSaber, effect_keys::kLensFlare, effect_keys::kDeepGlow,
                            effect_keys::kTurbulence, effect_keys::kShatter3D, effect_keys::kRasterExtrude}) {
        AUREA_CHECK_MSG(sectioned.count(key) == 1, key);
    }
    AUREA_CHECK(tips >= 5);
}

// A consulta da ponte (Android/iOS) traz seção, interruptor, estado e dica por
// linha; a ficha do tipo (sem instância) traz o estado declarado.
AUREA_TEST(EffectSections, ParamQueryCarriesSectionToggleStateAndTip) {
    SectionRig r(effect_keys::kSaber);
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    std::vector<bridge::EffectParamRow> rows;
    std::vector<char> blob;
    const u32 n = r.query(rows, blob);
    AUREA_CHECK(n >= 33);
    if (n < 33) return;
    // Os índices continuam 0..n−1 na ordem declarada (contrato do projeto).
    for (u32 i = 0; i < n; ++i) AUREA_CHECK_EQ(rows[i].index, i);
    // Saber: 0 = "Ativar brilho" é o interruptor da seção 1 ("Brilho externo").
    AUREA_CHECK_EQ(rows[0].section, 1u);
    AUREA_CHECK_EQ(rows[1].section, 1u);
    AUREA_CHECK_EQ(rows[1].sectionToggle, 0);
    AUREA_CHECK(blob_str(blob, rows[1].sectionIdOffset, rows[1].sectionIdLength) == "glow");
    AUREA_CHECK(blob_str(blob, rows[1].sectionLabelOffset, rows[1].sectionLabelLength) == "Brilho externo");
    AUREA_CHECK((rows[1].sectionFlags & bridge::kEffectSectionOpen) != 0);
    // Tipo de núcleo e pontos: fora de seção, sem interruptor.
    AUREA_CHECK_EQ(rows[6].section, 0u);
    AUREA_CHECK_EQ(rows[7].section, 0u);
    AUREA_CHECK_EQ(rows[7].sectionToggle, -1);
    // "Forma do núcleo" nasce recolhida.
    AUREA_CHECK_EQ(rows[11].section, 3u);
    AUREA_CHECK((rows[11].sectionFlags & bridge::kEffectSectionOpen) == 0);
    // Dica da camada do núcleo; os outros sem dica.
    AUREA_CHECK(blob_str(blob, rows[9].tipOffset, rows[9].tipLength).find("Camada / texto") != std::string::npos);
    AUREA_CHECK_EQ(rows[8].tipLength, 0u);
    // As strings da seção vão ao blob uma vez só (linhas da mesma seção apontam o mesmo trecho).
    AUREA_CHECK_EQ(rows[1].sectionLabelOffset, rows[4].sectionLabelOffset);

    // Ficha do tipo: mesmas seções, estado declarado.
    std::vector<bridge::EffectParamRow> spec(64);
    std::vector<char> specBlob(32 * 1024);
    const u32 m = r.e.query_effect_specs(effect_type_id(effect_keys::kSaber), spec.data(), 64, specBlob.data(),
                                         static_cast<u32>(specBlob.size()));
    AUREA_CHECK_EQ(m, n);
    for (u32 i = 0; i < m && i < n; ++i) {
        AUREA_CHECK_EQ(spec[i].section, rows[i].section);
        AUREA_CHECK_EQ(spec[i].sectionToggle, rows[i].sectionToggle);
        AUREA_CHECK_EQ(spec[i].sectionFlags, rows[i].sectionFlags);
        AUREA_CHECK_EQ(spec[i].tipLength, rows[i].tipLength);
    }
    // Efeito sem seção: tudo zero/−1 (linha antiga continua "sem seção").
    const u32 g = r.e.query_effect_specs(effect_type_id(effect_keys::kGaussianBlur), spec.data(), 64,
                                         specBlob.data(), static_cast<u32>(specBlob.size()));
    AUREA_CHECK(g > 0);
    for (u32 i = 0; i < g; ++i) {
        AUREA_CHECK_EQ(spec[i].section, 0u);
        AUREA_CHECK_EQ(spec[i].sectionToggle, -1);
        AUREA_CHECK_EQ(spec[i].tipLength, 0u);
    }
}

// Abrir/recolher: muda só a instância, sem passo de desfazer, e volta igual
// depois de salvar e reabrir. Projeto antigo (byte 1) abre tudo como declarado.
AUREA_TEST(EffectSections, OpenStatePersistsInTheExpandedByte) {
    const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/aurea_teste_secoes.aurea";
    {
        SectionRig r(effect_keys::kSaber);
        AUREA_CHECK(r.ok);
        if (!r.ok) return;
        const u32 undoBefore = r.e.read_status().undoDepth;
        // Seção 1 (aberta por padrão) recolhe; seção 3 (recolhida) abre.
        AUREA_CHECK(r.e.set_effect_section_open(r.layer, r.effect, 1, false));
        AUREA_CHECK(r.e.set_effect_section_open(r.layer, r.effect, 3, true));
        AUREA_CHECK(!r.e.set_effect_section_open(r.layer, r.effect, 0, true));
        AUREA_CHECK(!r.e.set_effect_section_open(r.layer, r.effect, 8, true));
        AUREA_CHECK(!r.e.set_effect_section_open(r.layer, 9999u, 1, true));
        AUREA_CHECK_EQ(r.instance()->sectionsFlipped, static_cast<u8>(0b101));
        AUREA_CHECK_EQ(r.e.read_status().undoDepth, undoBefore);   // apresentação: sem desfazer
        std::vector<bridge::EffectParamRow> rows;
        std::vector<char> blob;
        AUREA_CHECK(r.query(rows, blob) >= 33);
        AUREA_CHECK((rows[1].sectionFlags & bridge::kEffectSectionOpen) == 0);
        AUREA_CHECK((rows[11].sectionFlags & bridge::kEffectSectionOpen) != 0);
        // Voltar ao declarado limpa o bit.
        AUREA_CHECK(r.e.set_effect_section_open(r.layer, r.effect, 3, false));
        AUREA_CHECK_EQ(r.instance()->sectionsFlipped, static_cast<u8>(0b001));
        AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    }
    {
        Engine e;
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.load_project(path.c_str()).ok());
        const Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        u8 flipped = 0xFF;
        bool expanded = false;
        comp->layers().for_each([&](LayerId, const Layer& l) {
            for (const EffectInstance& inst : l.effects) {
                if (inst.type == effect_type_id(effect_keys::kSaber)) { flipped = inst.sectionsFlipped; expanded = inst.expanded; }
            }
        });
        AUREA_CHECK_EQ(flipped, static_cast<u8>(0b001));
        AUREA_CHECK(expanded);   // o bit 0 continua sendo o `expanded` de sempre
        e.shutdown();
    }
    std::remove(path.c_str());
}
