// =============================================================================
//  Copiar fundo (Utilitário) — a camada vira uma JANELA para o que está abaixo.
//
//  Pedido da 0.3.0: "ajustar certa parte do vídeo por camadas de forma". Posto
//  numa forma (ou em qualquer camada 2D), o efeito divide a pilha em dois:
//
//   - os efeitos ANTES dele (e as máscaras, o track matte, o desfoque de
//     movimento, a opacidade de cada amostra...) moldam a JANELA: só o alfa
//     (ou a luma) da camada sobra; a cor dela é descartada;
//   - os efeitos DEPOIS dele agem sobre o FUNDO — as camadas abaixo na mesma
//     composição, neste quadro, em px da composição (como uma camada de
//     ajuste) — e o resultado só aparece dentro da janela, misturado pelo modo
//     de mistura e pela opacidade da camada.
//
//  Sem efeito depois dele a janela mostra o próprio fundo: a camada some. A
//  divisão acontece no renderer (Renderer::prepare / composite_draws); na
//  cadeia de pixels o efeito é neutro. Numa camada de ajuste, 3D, matte, base
//  ou recortada de máscara de recorte ele também é neutro (ver o renderer).
//  "Misturar com a camada" põe a cor da própria camada por cima do resultado.
//  Os índices de parâmetro ficam fixos nos projetos (acrescentar só no fim).
// =============================================================================
#include "BuiltinEffects.hpp"

#include <memory>

namespace aurea::builtin {
namespace {

class CopyBackground final : public Effect {
public:
    enum : u32 { kWindow = 0, kLayerMix, kCount };
    static_assert(kCount == 2, "índices gravados nos projetos: só se acrescenta no fim");

    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{effect_keys::kCopyBackground, "Copiar fundo", "Utilitário", EffectClass::Domain};
        return i;
    }

    void declare_parameters(ParameterRegistry& p) const override {
        // A ordem casa com o modo do mask/track_matte.frag (modo = opção + 1).
        static const char* const kWindows[] = {"Alfa da camada", "Alfa invertido", "Luma da camada", "Luma invertida"};
        p.add_enum("window", "Janela", kWindows, 4, 0);
        p.add_float("layer_mix", "Misturar com a camada", 0.0f, 0.0f, 100.0f, kParamAnimatable | kParamPercent, "%");
        p.set_tip(kWindow, "Os efeitos depois deste agem nas camadas abaixo, só dentro da forma desta camada. "
                           "Os de antes (desfoque, máscara) moldam a janela.");
    }

    // A divisão da pilha é do renderer: na cadeia de pixels nada muda.
    bool is_identity(const EffectEval&) const noexcept override { return true; }
};

} // namespace

void register_copy_background_effect(EffectRegistry& r) { (void)r.add(std::make_unique<CopyBackground>()); }

} // namespace aurea::builtin
