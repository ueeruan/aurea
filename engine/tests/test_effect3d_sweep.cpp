// Varredura: TODO efeito registrado, numa camada ligada em 3D, tem que
// continuar DESENHANDO a camada.
//
// O defeito relatado foi "a camada some se tiver efeito nela e ativar o 3D".
// A causa era o corte da região do efeito pela visibilidade 2D da camada: num
// plano dentro da cena 3D a matriz 2D não diz onde ele aparece, o cruzamento
// dava vazio e a região caía para 1x1. Um teste com um efeito só não pega isso
// — cada família (um passe, dois passes, brilho com passe extra, ladrilho que
// cresce) passa por um caminho diferente do grafo. Aqui passam todos.
#include "TestFramework.hpp"

#if defined(AUREA_TEST_VULKAN)

#include "VulkanBackend.hpp"

#include "aurea/Engine.hpp"
#include "aurea/effects/EffectRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;

namespace {

/// O registro de efeitos, só para enumerar as chaves (o motor tem o dele).
const EffectRegistry& gpu_effects() {
    static EffectRegistry reg = [] {
        EffectRegistry r;
        register_builtin_effects(r);
        return r;
    }();
    return reg;
}

struct SweepRig {
    Engine e;
    bool ok = false;

    SweepRig() {
        EngineConfig ec;
        ec.backend = new vk::Backend();
        ec.backendConfig.enableValidation = false;
        ec.disableAutosave = true;
        ec.workerCount = 2;
        ok = e.initialize(ec).ok() && e.new_project(320, 180, 30.0, nullptr).ok();
    }
    ~SweepRig() { if (ok) e.shutdown(); }
};

struct Medida {
    u32 acesos = 0;   ///< pixels da camada
    u32 largura = 0, altura = 0;   ///< caixa da camada
    f32 centroX = 0.0f, centroY = 0.0f;
};

Medida medir(const std::vector<u8>& rgba, u32 w, u32 h) {
    Medida m;
    i64 sx = 0, sy = 0;
    u32 minX = w, maxX = 0, minY = h, maxY = 0;
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            const u8* p = &rgba[(static_cast<usize>(y) * w + x) * 4];
            if (p[0] > 8 || p[1] > 8 || p[2] > 8) {
                ++m.acesos;
                sx += x; sy += y;
                minX = minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
        }
    }
    if (m.acesos) {
        m.centroX = static_cast<f32>(sx) / static_cast<f32>(m.acesos);
        m.centroY = static_cast<f32>(sy) / static_cast<f32>(m.acesos);
        m.largura = maxX - minX + 1;
        m.altura = maxY - minY + 1;
    }
    return m;
}

/// Um quadrado claro no meio da composição, com o efeito pedido.
Medida com_efeito(const char* key, bool threeD, u32 quantos = 1, bool forcar_param = true) {
    SweepRig rig;
    if (!rig.ok) return {};
    Composition* comp = rig.e.project()->timeline().composition(rig.e.project()->timeline().current());
    const LayerId id = comp->add_layer(LayerKind::Shape, "quadrado");
    Layer* l = comp->layer(id);
    l->shape.shapeType = 0;
    l->shape.bounds = Rect{0.0f, 0.0f, 90.0f, 90.0f};
    l->shape.fillColor = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    l->shape.filled = true;
    l->transform.anchor = Vec3{45.0f, 45.0f, 0.0f};
    l->transform.position = Vec3{160.0f, 90.0f, 0.0f};
    l->end = FrameIndex{120};
    l->threeD = threeD;
    if (key) {
        for (u32 i = 0; i < quantos; ++i) {
            EffectInstance inst;
            inst.id = l->alloc_effect_id();
            inst.type = effect_type_id(key);
            const ParameterRegistry* params = gpu_effects().params(inst.type);
            if (!params) continue;
            initialize_instance(inst, *params);
            // Um efeito com o parâmetro no neutro nem monta passe: o teste
            // passaria sem exercitar nada. Um valor de verdade no primeiro
            // parâmetro escalar põe o efeito para trabalhar.
            if (forcar_param && !inst.params.empty() && inst.params[0].constant.v[0] == 0.0f) {
                inst.params[0].constant.v[0] = 12.0f;
            }
            l->effects.push_back(std::move(inst));
        }
    }
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{10}, 30.0);
    AUREA_CHECK(rig.e.apply_command(c).ok());

    std::vector<u8> rgba;
    u32 w = 0, h = 0;
    if (!rig.e.capture_frame_rgba(320, rgba, w, h).ok()) return {};
    return medir(rgba, w, h);
}

} // namespace

AUREA_TEST(Effect3D, EveryRegisteredEffectDrawsTheSameWithAndWithout3D) {
    // A primeira medição também é o teste de "tem GPU": sem ela não há o que medir.
    const Medida base2D = com_efeito(nullptr, false, 1, false);
    const Medida base3D = com_efeito(nullptr, true, 1, false);
    if (base2D.acesos == 0 && base3D.acesos == 0) {
        std::printf("(sem GPU Vulkan: pulado) ");
        return;
    }
    const EffectRegistry& reg = gpu_effects();
    const u32 n = reg.count();
    AUREA_CHECK(n > 40);   // o registro tem 47; zero aqui seria o registry vazio
    std::printf("\n    sem efeito: 2D %u px (%.0fx%.0f), 3D %u px (%.0fx%.0f)\n", base2D.acesos,
                static_cast<double>(base2D.largura), static_cast<double>(base2D.altura), base3D.acesos,
                static_cast<double>(base3D.largura), static_cast<double>(base3D.altura));
    AUREA_CHECK(base2D.acesos > 2000u);
    AUREA_CHECK(base3D.acesos > 2000u);

    // O que se cobra do efeito é o MESMO resultado com e sem 3D. Comparar
    // contra o 2D é o que torna o teste honesto: o Luma Key e o Invert deixam o
    // quadro legitimamente preto (o quadrado branco some no luma key; o invert
    // troca branco por preto), e um critério de "tem pixel aceso" os acusaria
    // de quebrados. O que NÃO pode é o 3D dar diferente do 2D.
    u32 ruins = 0;
    for (u32 i = 0; i < n; ++i) {
        const char* key = reg.at(i).info().key;
        const Medida m2 = com_efeito(key, false, 1);
        const Medida m3 = com_efeito(key, true, 1);
        const f32 razao = m2.acesos ? static_cast<f32>(m3.acesos) / static_cast<f32>(m2.acesos) : 1.0f;
        const bool vazio_igual = (m2.acesos == 0 && m3.acesos == 0);
        const bool centro_ok = m2.acesos == 0 || m3.acesos == 0
                            || (std::fabs(m3.centroX - m2.centroX) < 24.0f && std::fabs(m3.centroY - m2.centroY) < 24.0f);
        const bool ok = vazio_igual || (razao >= 0.25f && razao <= 4.0f && centro_ok);
        std::printf("    %-34s 2D %6u px  3D %6u px  %.2fx%s\n", key, m2.acesos, m3.acesos,
                    static_cast<double>(razao), ok ? "" : "   <-- QUEBRADO");
        if (!ok) ++ruins;
    }
    std::printf("    %u efeitos com 3D: %u quebrados\n", n, ruins);
    AUREA_CHECK(ruins == 0);
}

#endif   // AUREA_TEST_VULKAN
