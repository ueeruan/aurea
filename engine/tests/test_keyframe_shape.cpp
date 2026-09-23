// =============================================================================
//  Keyframe de FORMA e de CAMINHO: criar um keyframe novo tem de copiar o
//  estado AVALIADO no cabeçote — nunca a forma parada, o caminho original ou o
//  primeiro keyframe.
//
//  O caso que motivou o teste (relatado pelo dono): K1 -> editar -> K2 ->
//  editar -> criar K3, e a forma voltava para a original.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/vector/Vector.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace aurea;
using namespace aurea::vector;

/// `vector_path_at` devolve 6 floats de matriz + 1 de flags antes do caminho.
constexpr usize kPathOffset = 7;

namespace {

struct ShapeRig {
    Engine e;
    u64 layer = 0;
    ShapeRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
        // Camada VETORIAL (shapeType = kShapeVector): é ela que tem grupos e
        // caminhos com keyframe; a forma SDF é outro caminho de código.
        auto id = e.add_vector_layer(1);   // 1 = retângulo (o 0 é desenho livre, vazio)
        AUREA_CHECK(id.ok());
        layer = *id;
        // `add_vector_layer` já traz o grupo 0 com um caminho (o preset).
    }
    ~ShapeRig() { e.shutdown(); }

    void seek(i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    }
    /// O caminho AVALIADO agora, em floats (é o que a prévia desenha).
    std::vector<f32> path(u32 group = 0u, u32 p = 0u) {
        std::vector<f32> out;
        AUREA_CHECK(e.vector_path_at(layer, group, p, out));
        return out;
    }
    /// Mexe no caminho: desloca todos os pontos em `dx`.
    void nudge(f32 dx) {
        std::vector<f32> v = path();
        BezierPath b;
        usize pos = kPathOffset;   // pula matriz (6) + flags (1)
        AUREA_CHECK(decode_path(v.data(), v.size(), pos, b));
        for (auto& pt : b.v) {
            pt.p.x += dx;
            pt.in.x += dx;
            pt.out.x += dx;
        }
        std::vector<f32> enc;
        encode_path(b, enc);
        AUREA_CHECK(e.set_vector_path(layer, 0, 0, enc.data(), enc.size(), true));
    }
};

/// O X do primeiro vértice: resume a forma num número que MUDA quando o
/// caminho é deslocado (largura não muda ao transladar — foi o que me enganou
/// na primeira versão deste teste).
f32 marca(const std::vector<f32>& v) {
    BezierPath b;
    usize pos = kPathOffset;   // pula matriz (6) + flags (1)
    if (!decode_path(v.data(), v.size(), pos, b) || b.v.empty()) return -1.0f;
    return b.v.front().p.x;
}

} // namespace

AUREA_TEST(Shape, ThirdPathKeyframeKeepsTheEvaluatedShape) {
    ShapeRig r;
    // K1 na forma de origem.
    r.seek(0);
    AUREA_CHECK(r.e.toggle_vector_path_key(r.layer, 0, 0));
    const f32 k1 = marca(r.path());

    // K2: empurra os pontos e edita — com um keyframe já feito, editar grava
    // NO keyframe do cabeçote.
    r.seek(60);
    AUREA_CHECK(r.e.toggle_vector_path_key(r.layer, 0, 0));
    r.nudge(30.0f);
    const f32 k2 = marca(r.path());
    AUREA_CHECK(k2 > k1);

    // No meio do caminho a prévia mostra o MORPH — nem K1 nem K2.
    r.seek(30);
    const f32 meio = marca(r.path());
    AUREA_CHECK(meio > k1 + 0.5f && meio < k2 - 0.5f);

    // O bug: criar K3 aqui devolvia a forma ORIGINAL. Agora K3 tem de ser
    // exatamente o que a prévia mostrava.
    AUREA_CHECK(r.e.toggle_vector_path_key(r.layer, 0, 0));
    const f32 k3 = marca(r.path());
    const f32 diferenca = std::fabs(k3 - meio);
    std::printf("    caminho: K1 %.2f K2 %.2f meio %.2f K3 %.2f (diferenca %.4f)\n", k1, k2, meio, k3, diferenca);
    AUREA_CHECK(diferenca < 0.01f);

    // E os três instantes continuam certos depois de criado o K3.
    r.seek(0);
    AUREA_CHECK(std::fabs(marca(r.path()) - k1) < 0.01f);
    r.seek(30);
    AUREA_CHECK(std::fabs(marca(r.path()) - k3) < 0.01f);
    r.seek(60);
    AUREA_CHECK(std::fabs(marca(r.path()) - k2) < 0.01f);
}

AUREA_TEST(Shape, EditingAnAnimatedPathDoesNotDropTheKeys) {
    ShapeRig r;
    // Uma forma PRIMITIVA (retângulo) com keyframe: ao editar um ponto ela vira
    // caminho livre — e a conversão não pode apagar os keyframes nem voltar a
    // forma para o quadro 0.
    r.seek(0);
    AUREA_CHECK(r.e.toggle_vector_path_key(r.layer, 0, 0));
    r.nudge(10.0f);
    r.seek(45);
    r.nudge(25.0f);

    const f32 em45 = marca(r.path());
    r.seek(0);
    const f32 em0 = marca(r.path());
    AUREA_CHECK(em45 > em0 + 0.5f);

    // O caminho continua animado: o valor do quadro 0 não virou o de 45.
    r.seek(0);
    const f32 volta = marca(r.path());
    std::printf("    caminho animado depois de editar: q0 %.2f q45 %.2f (de volta %.2f)\n", em0, em45, volta);
    AUREA_CHECK(std::fabs(volta - em0) < 0.01f);
}

AUREA_TEST(Shape, ShapeParamKeysCopyTheValueAtThePlayhead) {
    // Camada de forma SDF (a vetorial edita pelo caminho, não por estes
    // valores): é ela que tem raio, lados, largura.
    Engine e;
    EngineConfig ec;
    ec.workerCount = 1;
    ec.disableAutosave = true;
    AUREA_CHECK(e.initialize(ec).ok());
    AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    auto id = e.add_shape(0);
    AUREA_CHECK(id.ok());
    const u64 layer = *id;
    auto seek = [&](i64 f) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{f}, 30.0);
        AUREA_CHECK(e.apply_command(c).ok());
    };
    auto largura = [&]() {
        std::vector<f32> out(Engine::kShapeParamFloats);
        AUREA_CHECK(e.query_shape_params(layer, out.data(), Engine::kShapeParamFloats) == Engine::kShapeParamFloats);
        return out[5];   // 5 = largura da forma
    };

    // Mesma regra nos parâmetros da forma: o keyframe novo copia o valor
    // AVALIADO no cabeçote, não o campo parado.
    seek(0);
    AUREA_CHECK(e.set_shape_param(layer, 5, 100.0f, false));
    AUREA_CHECK(e.ensure_shape_param_key(layer, 5));

    seek(60);
    AUREA_CHECK(e.set_shape_param(layer, 5, 300.0f, false));
    // A edição já criou o keyframe do quadro (o valor é animado); marcar de
    // novo tem de REGRAVAR, não apagar.
    AUREA_CHECK(e.ensure_shape_param_key(layer, 5));

    seek(30);
    const f32 noMeio = largura();
    AUREA_CHECK(noMeio > 150.0f && noMeio < 250.0f);

    AUREA_CHECK(e.ensure_shape_param_key(layer, 5));
    const f32 k3 = largura();
    // Apertar "criar keyframe" outra vez (o quadro agora tem um) NÃO pode
    // apagar o valor: era assim que a forma voltava ao estado anterior.
    AUREA_CHECK(e.ensure_shape_param_key(layer, 5));
    const f32 k3deNovo = largura();
    std::printf("    largura: meio %.1f K3 %.1f K3 de novo %.1f\n", noMeio, k3, k3deNovo);
    AUREA_CHECK(std::fabs(k3 - noMeio) < 0.01f);
    AUREA_CHECK(std::fabs(k3deNovo - k3) < 0.01f);
    seek(0);
    AUREA_CHECK(std::fabs(largura() - 100.0f) < 0.01f);
    seek(60);
    AUREA_CHECK(std::fabs(largura() - 300.0f) < 0.01f);
    e.shutdown();
}
