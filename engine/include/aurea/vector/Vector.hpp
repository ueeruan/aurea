// =============================================================================
//  Aurea / vector / Vector.hpp
//
//  Geometria vetorial da camada vetorial, toda na CPU e determinística; a GPU
//  só recebe triângulos com a distância até a borda (antisserrilhado exato na
//  derivada da tela) e a tinta (sólida ou degradê, avaliada no fragment).
//
//  Cadeia de um grupo, no quadro:
//    caminhos (paramétrico → bezier; morph pelos keyframes de forma)
//      → achatar (tolerância em px de TEXTURA: nítido em qualquer zoom)
//      → Mesclar (booleanas no arranjo planar)
//      → Aparar (trim por comprimento de arco)
//      → Preencher (regra não-zero / par-ímpar resolvida em anéis limpos)
//      → Contorno (tracejado → peças por segmento + juntas + pontas, unidas)
//      → Repetidor (cópias com transform e opacidade)
//      → transform do grupo.
//
//  O recorte é um só algoritmo para tudo (regra de preenchimento, união do
//  contorno, booleanas): arranjo planar das arestas (interseções em double,
//  vértices numa grade de 1/256 px), enrolamento de cada lado de cada aresta
//  por raio horizontal, e as arestas cujo "dentro" muda de um lado para o
//  outro viram os anéis do resultado — sempre com o preenchido à ESQUERDA
//  (borda externa com área positiva, furos com área negativa).
// =============================================================================
#pragma once

#include "aurea/vector/VectorData.hpp"

#include <string>
#include <vector>

namespace aurea { class TrackSet; }

namespace aurea::vector {

/// Afim 2D: x' = a·x + c·y + tx, y' = b·x + d·y + ty.
struct Affine2 {
    f32 a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f, tx = 0.0f, ty = 0.0f;
    [[nodiscard]] Vec2 apply(Vec2 p) const noexcept { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    [[nodiscard]] Vec2 apply_vec(Vec2 p) const noexcept { return {a * p.x + c * p.y, b * p.x + d * p.y}; }
    friend Affine2 operator*(const Affine2& m, const Affine2& n) noexcept {
        return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d, m.b * n.c + m.d * n.d,
                m.a * n.tx + m.c * n.ty + m.tx, m.b * n.tx + m.d * n.ty + m.ty};
    }
    [[nodiscard]] static Affine2 translate(Vec2 t) noexcept { return {1, 0, 0, 1, t.x, t.y}; }
    [[nodiscard]] static Affine2 scale(Vec2 s) noexcept { return {s.x, 0, 0, s.y, 0, 0}; }
    [[nodiscard]] static Affine2 rotate(f32 degrees) noexcept;
    [[nodiscard]] Affine2 inverse() const noexcept;
    [[nodiscard]] f32 max_scale() const noexcept;
    [[nodiscard]] f32 mean_scale() const noexcept;   ///< √|det|
};

/// Polilinha achatada. Fechada = o último ponto liga no primeiro.
struct Contour {
    std::vector<Vec2> pts;
    bool closed = true;
};

enum class FillRule : u8 { NonZero = 0, EvenOdd = 1 };
enum class BoolOp : u8 { Union = 1, Subtract = 2, Intersect = 3, Exclude = 4 };

// --- Caminhos ----------------------------------------------------------------

/// Retângulo com cantos arredondados (raio em px), elipse, polígono/estrela.
[[nodiscard]] BezierPath make_rect(Vec2 center, Vec2 size, f32 roundness);
[[nodiscard]] BezierPath make_ellipse(Vec2 center, Vec2 size);
[[nodiscard]] BezierPath make_polystar(Vec2 center, f32 points, f32 outerRadius, f32 innerRadius,
                                       f32 outerRoundness, f32 innerRoundness, f32 rotation, bool star);
/// Caminho bezier do VectorPath (paramétrico gerado, livre como está), no
/// instante local `frame` (morph pelos keyframes de forma).
[[nodiscard]] BezierPath path_at(const VectorPath& p, f64 frame);
/// Converte o paramétrico em caminho livre editável (mesma forma).
void make_editable(VectorPath& p);
/// Morph: interpolação de dois caminhos (contagens diferentes são
/// reamostradas subdividindo os segmentos mais longos, sem mudar a forma).
[[nodiscard]] BezierPath lerp_path(const BezierPath& a, const BezierPath& b, f32 t);
/// Subdivide até `count` vértices (a forma não muda).
[[nodiscard]] BezierPath resample(const BezierPath& p, usize count);
[[nodiscard]] BezierPath transform_path(const BezierPath& p, const Affine2& m);

/// Achatamento adaptativo: nenhum ponto da curva fica a mais de `tolerance`
/// da polilinha (subdivisão pela planura do polígono de controle).
void flatten(const BezierPath& p, f32 tolerance, Contour& out);

// --- Medidas -----------------------------------------------------------------
[[nodiscard]] f64 signed_area(const std::vector<Vec2>& ring) noexcept;
[[nodiscard]] f64 area_of(const std::vector<Contour>& rings) noexcept;   ///< soma assinada
[[nodiscard]] f64 length_of(const Contour& c) noexcept;
/// Ponto e tangente (unitária) no comprimento de arco `s` (fechado: s cíclico).
bool sample_at(const Contour& c, f64 s, Vec2& point, Vec2& tangent) noexcept;

// --- Recorte -----------------------------------------------------------------

/// Regra de preenchimento → anéis limpos (sem auto-interseção, preenchido à
/// esquerda). Contornos abertos entram fechados implicitamente.
void resolve_fill(const std::vector<Contour>& in, FillRule rule, std::vector<Contour>& out);
/// Booleana entre conjuntos (cada um com a sua regra). União/interseção/
/// excluir valem para todos; subtrair = o primeiro menos os demais.
void boolean_op(const std::vector<std::vector<Contour>>& sets, FillRule rule, BoolOp op, std::vector<Contour>& out);

// --- Operadores --------------------------------------------------------------

/// Aparar: fatia [start, end] (0..1) do comprimento, girada por `offset`
/// (fração). `sequential` = todos os caminhos como um comprimento só.
void trim(std::vector<Contour>& contours, f32 start, f32 end, f32 offset, bool sequential);
/// Tracejado (padrão em px, deslocamento em px).
void dash(std::vector<Contour>& contours, const std::vector<f32>& pattern, f32 offset);
/// Contorno → anéis limpos (peças unidas em não-zero).
void stroke_to_rings(const std::vector<Contour>& contours, f32 width, u8 cap, u8 join, f32 miterLimit,
                     f32 tolerance, std::vector<Contour>& out);

// --- Avaliação e malha ---------------------------------------------------------

/// Grupo no instante local `frame`: valores animados (TrackSet, VectorParam)
/// aplicados; caminhos continuam bezier (o morph é resolvido em path_at).
[[nodiscard]] VectorGroup evaluate_group(const VectorGroup& g, const TrackSet& tracks, u32 groupIndex, f64 frame);
[[nodiscard]] f32* param_ref(VectorGroup& g, u32 param) noexcept;
[[nodiscard]] f32 clamp_param(u32 param, f32 v) noexcept;

/// Transform do grupo e de cada cópia do repetidor (cópia k, 0 = original).
[[nodiscard]] Affine2 group_matrix(const VectorGroup& g) noexcept;
[[nodiscard]] Affine2 repeater_matrix(const VectorRepeater& r, f32 k) noexcept;

/// Tinta pronta para o shader: 12 vec4 (tipo/nº de paradas/opacidade,
/// pontos, 8 cores lineares pré-multiplicadas, 8 posições).
inline constexpr u32 kPaintVec4 = 12;
inline constexpr u32 kMaxStops = 8;

/// Malha de uma camada vetorial: 2 vec4 por vértice (x, y, gx, gy — posição
/// na camada e no espaço do degradê — e d, tinta, alfa, _ — distância
/// assinada até a borda em px da camada, positiva dentro; interior = 1e4),
/// triângulos em sequência. `paints` = kPaintVec4 por tinta.
struct VectorMesh {
    std::vector<Vec4> verts;
    std::vector<Vec4> paints;
    Vec2 min{0, 0}, max{0, 0};
    bool empty() const noexcept { return verts.empty(); }
    [[nodiscard]] u32 vertex_count() const noexcept { return static_cast<u32>(verts.size() / 2); }
};

/// Geometria de um grupo pronta para pintar (espaço da camada).
struct GroupGeometry {
    std::vector<Contour> fill;     ///< anéis limpos
    std::vector<Contour> stroke;   ///< anéis limpos
    std::vector<Contour> lines;    ///< caminhos depois de mesclar/aparar (espaço do grupo)
};
/// Geometria do grupo JÁ avaliado, sem repetidor nem transform do grupo
/// (espaço do grupo). `tolerance` em px do grupo.
void group_geometry(const VectorGroup& g, f64 frame, f32 tolerance, GroupGeometry& out);

/// Malha de todos os grupos avaliados. `density` = texels por px da camada
/// (a tolerância e a largura do antisserrilhado saem dela).
void build_mesh(const std::vector<VectorGroup>& evaluated, f64 frame, f32 density, VectorMesh& out);

/// Caixa (px da camada) aproximada dos grupos avaliados (controle + contorno
/// + repetidor), para o palco e o hit-test. Falso se vazio.
bool bounds_of(const std::vector<VectorGroup>& evaluated, f64 frame, Vec2& mn, Vec2& mx);

/// Chave de conteúdo dos grupos avaliados (cache da malha): muda quando
/// qualquer coisa que altera a geometria ou a tinta muda.
[[nodiscard]] u64 content_hash(const std::vector<VectorGroup>& evaluated, f64 frame) noexcept;

/// Texto no caminho: posição (no espaço do caminho) e ângulo (graus) de uma
/// letra cujo centro fica em `x` px da margem inicial `offset`, deslocada `dy`
/// px para baixo da linha de base (linhas seguintes). Fora de um caminho
/// aberto a letra continua pela tangente da ponta.
bool place_on_path(const Contour& path, f32 offset, bool reverse, bool perpendicular, f32 x, f32 dy, Vec2& pos, f32& angleDeg) noexcept;

/// Caminho-guia para texto: o primeiro caminho visível do primeiro grupo com
/// caminho, achatado, com o transform do grupo (px da camada).
bool guide_contour(const VectorData& data, const TrackSet& tracks, f64 frame, Contour& out);

// --- Desenho à mão livre -------------------------------------------------------

/// Curva suave pelos pontos do dedo (Schneider: cúbicas ajustadas por mínimos
/// quadrados, reparametrização de Newton e divisão no ponto de maior erro).
/// `error` em px. `closed` fecha quando o fim encosta no começo.
[[nodiscard]] BezierPath fit_curve(const std::vector<Vec2>& points, f32 error, bool closeIfNear);

// --- Documento (UI e projeto) ----------------------------------------------------

/// O VectorData como fluxo de floats (versão 1) + os nomes dos grupos (um por
/// linha). É o formato da ponte com a UI (JNI) e o da seção do projeto — um
/// codec só. Leitura tolerante: fluxo curto/corrompido = falso, nada muda.
void encode_document(const VectorData& data, std::vector<f32>& out, std::string& names);
bool decode_document(const f32* data, usize count, const std::string& names, VectorData& out);
/// Um caminho bezier (fechado, n, n × [p, in, out]).
void encode_path(const BezierPath& p, std::vector<f32>& out);
bool decode_path(const f32* data, usize count, usize& pos, BezierPath& out);

// --- SVG -----------------------------------------------------------------------

struct SvgResult {
    VectorData data;
    Vec2 size{0, 0};          ///< tamanho do desenho (viewBox → px)
    u32 elements = 0;         ///< formas lidas
};
/// Subconjunto prático do SVG: path (todos os comandos, arcos incluídos),
/// rect, circle, ellipse, line, polyline, polygon, g com transform,
/// fill/stroke/stroke-width/opacity/fill-rule (atributo ou style=""),
/// linearGradient/radialGradient com stops, viewBox. Falso se nada foi lido.
bool parse_svg(const std::string& text, SvgResult& out, std::string* error = nullptr);

} // namespace aurea::vector
