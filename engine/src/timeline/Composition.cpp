#include "aurea/timeline/Composition.hpp"
#include "aurea/core/Log.hpp"

#if !defined(NDEBUG)
    #include <cstdio>
    #include <cstdlib>
    #define AUREA_ASSERT(cond)                                                     \
        do {                                                                       \
            if (!(cond)) {                                                         \
                std::fprintf(stderr,                                               \
                    "AUREA_ASSERT falhou: %s (%s:%d)\n", #cond, __FILE__, __LINE__);\
                std::abort();                                                      \
            }                                                                      \
        } while (0)
#else
    #define AUREA_ASSERT(cond) ((void)0)
#endif

namespace aurea {
namespace {

/// Nome padrão por tipo — a UI usa isto como "Vídeo 1", "Texto 2". O sufixo
/// numérico é responsabilidade da UI (que conhece os nomes já em uso); aqui só
/// o substantivo.
const char* default_layer_noun(LayerKind kind) noexcept {
    switch (kind) {
        case LayerKind::Video:          return "Video";
        case LayerKind::Image:          return "Imagem";
        case LayerKind::Audio:          return "Audio";
        case LayerKind::Text:           return "Texto";
        case LayerKind::Shape:          return "Forma";
        case LayerKind::Null:           return "Nulo";
        case LayerKind::Adjustment:     return "Ajuste";
        case LayerKind::Camera:         return "Camera";
        case LayerKind::Light:          return "Luz";
        case LayerKind::Model3D:        return "Modelo 3D";
        case LayerKind::ParticleSystem: return "Particulas";
        case LayerKind::Composition:    return "Composicao";
        case LayerKind::Unknown:        break;
    }
    return "Camada";
}

/// Nome do arquivo para o painel — não é usado aqui, mas mantém a tabela
/// completa para o compilador avisar quando um tipo novo for acrescentado.

} // namespace

LayerId Composition::add_layer(LayerKind kind, std::string name) {
    if (layers_.count() >= kMaxLayerCount) {
        AUREA_LOG_ERROR("composicao '%s' atingiu o limite de %u camadas",
                        name_.c_str(), kMaxLayerCount);
        return LayerId{};
    }

    if (name.empty()) name = default_layer_noun(kind);

    Layer layer;
    layer.kind = kind;
    layer.name = std::move(name);
    layer.start = FrameIndex{0};
    layer.end = duration_;
    layer.zOrder = order_.size();

    const LayerId id = layers_.create(std::move(layer));
    order_.push_back(id);
    rebuild_draw_order();
    touch();
    return id;
}

LayerId Composition::duplicate_layer(LayerId source, FrameIndex atTime) {
    Layer* src = layers_.get(source);
    if (!src) return LayerId{};

    if (layers_.count() >= kMaxLayerCount) return LayerId{};

    Layer copy = *src;   // cópia profunda: tracks, efeitos, máscaras, texto
    copy.name = src->name + " copia";

    // Duplicar posiciona a cópia logo depois do original, na mesma ordem
    // vertical — é o que o usuário espera ao duplicar no lugar.
    const FrameIndex len = src->duration();
    copy.start = atTime;
    copy.end = FrameIndex{atTime.value + len.value};

    // O parenting aponta para a layer original, não para a cópia: se apontasse
    // para a cópia, a duplicata se moveria sozinha e a original ficaria parada.
    // (O chamador remapeia explicitamente se quiser outro comportamento.)

    const LayerId id = layers_.create(std::move(copy));

    // A cópia entra no FIM da ordem vertical e só então é movida para logo
    // acima do original. `move_to` reposiciona um id que já está na lista — ele
    // não insere. Chamá-lo sem o `push_back` antes deixaria a cópia fora da
    // ordem de desenho: ela existiria na tabela de layers, contaria em
    // `layers().count()`, e nunca seria desenhada nem poderia ser arrastada.
    order_.push_back(id);

    const i32 srcIndex = order_.index_of(source);
    if (srcIndex >= 0) {
        order_.move_to(id, static_cast<u32>(srcIndex) + 1);
    }

    // Invariante: a ordem de desenho contém exatamente as layers vivas. Se esta
    // asserção dispara, alguma operação inseriu ou removeu de um lado só.
    AUREA_ASSERT(order_.size() == layers_.count());

    rebuild_draw_order();
    touch();
    return id;
}

bool Composition::remove_layer(LayerId id) noexcept {
    Layer* l = layers_.get(id);
    if (!l) return false;

    // Filhos que apontavam para esta layer perdem o pai. Deixar o ponteiro
    // pendurado faria a avaliação de transform subir para uma layer morta.
    layers_.for_each([id](LayerId childId, Layer& child) {
        if (child.parent == id) child.parent = LayerId{};
        (void)childId;
    });

    (void)order_.erase(id);
    (void)layers_.destroy(id);
    rebuild_draw_order();
    touch();
    return true;
}

bool Composition::reorder_layer(LayerId id, u32 targetIndex) noexcept {
    if (!layers_.contains(id)) return false;
    if (targetIndex >= order_.size()) return false;
    if (!order_.move_to(id, targetIndex)) return false;
    rebuild_draw_order();
    touch();
    return true;
}

void Composition::rebuild_draw_order() noexcept {
    // A ordem vertical é a ordem do vetor `order_`. zOrder é derivado e existe
    // só para consulta rápida (a UI mostra "camada 3 de 7"). drawIndex é a
    // ordem de desenho: do FUNDO para a FRENTE — a última da lista é a que
    // aparece na frente.
    const u32 n = order_.size();
    for (u32 i = 0; i < n; ++i) {
        Layer* l = layers_.get(order_.at(i));
        if (!l) continue;
        l->zOrder = i;
        l->drawIndex = i;
    }
}

u32 Composition::collect_active(FrameIndex t, std::vector<LayerId>& out) const {
    out.clear();
    const u32 n = order_.size();
    for (u32 i = 0; i < n; ++i) {
        const LayerId id = order_.at(i);
        const Layer* l = layers_.get(id);
        if (!l) continue;
        if (!l->visible) continue;
        if (!l->contains_time(t)) continue;
        out.push_back(id);
    }
    return static_cast<u32>(out.size());
}

bool Composition::can_nest(const Composition& candidate) const noexcept {
    if (&candidate == this) return false;
    // Profundidade máxima: cada nível de aninhamento custa um render target e
    // uma recursão no avaliador. Oito níveis é o teto que mantém o preview
    // dentro do orçamento mesmo num aparelho modesto.
    if (candidate.nestingDepth_ >= kMaxNestingDepth) return false;
    if (nestingDepth_ + candidate.nestingDepth_ + 1 > kMaxNestingDepth) return false;
    return true;
}

} // namespace aurea
