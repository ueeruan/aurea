// A CONTA DO 3D: VETOR, QUATERNION E MATRIZ.
//
// ESCRITO A MAO, E NAO PEGO DO DILIGENT. O `BasicMath.hpp` dele e bom, mas
// ele mora no `third_party` — e este cabecalho e usado tambem por quem
// importa modelo e por quem avalia a timeline, os dois caminhos que nao
// podem depender de uma biblioteca de desenho para existir. Uma conta de
// matriz nao precisa de GPU, e a prova disso e que ela e testada sem
// nenhuma.
//
// A CONVENCAO E A DO GRAFICO, e nao a da tela:
//   - MAO DIREITA, Y PARA CIMA, -Z PARA FRENTE. E a convencao do glTF, que
//     e o formato de entrada principal; adotar a outra obrigaria a virar o
//     modelo inteiro na entrada e a lembrar disso em cada conta seguinte.
//   - MATRIZ COLUNA A COLUNA, como o GLSL e o SPIR-V esperam. `m[c * 4 + r]`
//     e a linha `r` da coluna `c`. Trocar isso nao da erro de compilacao:
//     da uma cena espelhada, que e o tipo de defeito que parece arte.
#ifndef AUREA_RENDER_VETOR_H
#define AUREA_RENDER_VETOR_H

#include <cmath>
#include <cstdint>

namespace aurea::render::geo {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kGrau = kPi / 180.0F;

[[nodiscard]] constexpr float radianos(float graus) noexcept {
  return graus * kGrau;
}

struct Vec2 {
  float x = 0.0F, y = 0.0F;
};

struct Vec3 {
  float x = 0.0F, y = 0.0F, z = 0.0F;

  [[nodiscard]] friend constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
  }
  [[nodiscard]] friend constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
  }
  [[nodiscard]] friend constexpr Vec3 operator*(Vec3 a, float s) noexcept {
    return {a.x * s, a.y * s, a.z * s};
  }
  [[nodiscard]] friend constexpr Vec3 operator*(float s, Vec3 a) noexcept {
    return a * s;
  }
  [[nodiscard]] friend constexpr Vec3 operator-(Vec3 a) noexcept {
    return {-a.x, -a.y, -a.z};
  }
  constexpr Vec3& operator+=(Vec3 b) noexcept {
    x += b.x; y += b.y; z += b.z;
    return *this;
  }
};

struct Vec4 {
  float x = 0.0F, y = 0.0F, z = 0.0F, w = 0.0F;
};

[[nodiscard]] constexpr float ponto(Vec3 a, Vec3 b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cruzado(Vec3 a, Vec3 b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] constexpr float comprimento2(Vec3 a) noexcept {
  return ponto(a, a);
}

[[nodiscard]] inline float comprimento(Vec3 a) noexcept {
  return std::sqrt(ponto(a, a));
}

/// NORMALIZA SEM DIVIDIR POR ZERO. Um vetor nulo (que aparece em modelo
/// mal formado, e aparece mais do que se imagina) devolve o eixo Y em vez
/// de NaN — porque um NaN aqui contamina a matriz inteira e some da tela,
/// enquanto um Y arbitrario so desenha torto.
[[nodiscard]] inline Vec3 normalizado(Vec3 a) noexcept {
  const float n = comprimento(a);
  if (n <= 1e-8F) return {0.0F, 1.0F, 0.0F};
  return a * (1.0F / n);
}

[[nodiscard]] constexpr float misturar(float a, float b, float t) noexcept {
  return a + (b - a) * t;
}

[[nodiscard]] constexpr Vec3 misturar(Vec3 a, Vec3 b, float t) noexcept {
  return {misturar(a.x, b.x, t), misturar(a.y, b.y, t), misturar(a.z, b.z, t)};
}

/// QUATERNION NA ORDEM (x, y, z, w) — a mesma do glTF e a que o shader le.
struct Quat {
  float x = 0.0F, y = 0.0F, z = 0.0F, w = 1.0F;

  [[nodiscard]] static constexpr Quat identidade() noexcept { return {}; }

  /// EULER EM GRAUS, NA ORDEM X DEPOIS Y DEPOIS Z, COM O Y APLICADO POR
  /// FORA DA TELA (intrinseca). E a ordem que o dono espera ao digitar tres
  /// numeros num painel: girar 90 no Y e depois 90 no X tem de dar o mesmo
  /// resultado que dar os dois numeros e olhar — nao o mesmo que aplicar as
  /// duas rotacoes num eixo fixo.
  [[nodiscard]] static Quat de_euler(Vec3 graus) noexcept {
    const float hx = radianos(graus.x) * 0.5F;
    const float hy = radianos(graus.y) * 0.5F;
    const float hz = radianos(graus.z) * 0.5F;
    const float sx = std::sin(hx), cx = std::cos(hx);
    const float sy = std::sin(hy), cy = std::cos(hy);
    const float sz = std::sin(hz), cz = std::cos(hz);
    return Quat{
        sx * cy * cz + cx * sy * sz,
        cx * sy * cz - sx * cy * sz,
        cx * cy * sz + sx * sy * cz,
        cx * cy * cz - sx * sy * sz,
    };
  }

  [[nodiscard]] static Quat de_eixo_angulo(Vec3 eixo, float graus) noexcept {
    const Vec3 a = normalizado(eixo);
    const float h = radianos(graus) * 0.5F;
    const float s = std::sin(h);
    return Quat{a.x * s, a.y * s, a.z * s, std::cos(h)};
  }
};

[[nodiscard]] inline Quat operator*(const Quat& a, const Quat& b) noexcept {
  return Quat{
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
  };
}

/// GIRO DE UM VETOR PELO QUATERNION. Feito pela formula do sanduiche
/// (`v + 2w(q x v) + 2(q x (q x v))`) em vez de montar a matriz: e menos
/// multiplicacao e nao aloca.
[[nodiscard]] inline Vec3 girar(const Quat& q, Vec3 v) noexcept {
  const Vec3 u{q.x, q.y, q.z};
  const Vec3 t = cruzado(u, v) * 2.0F;
  return v + t * q.w + cruzado(u, t);
}

/// MATRIZ 4x4 COLUNA A COLUNA. `m[c * 4 + r]`.
struct Mat4 {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  [[nodiscard]] static constexpr Mat4 identidade() noexcept { return {}; }

  [[nodiscard]] static Mat4 de_trs(Vec3 posicao, const Quat& giro,
                                   Vec3 escala) noexcept {
    Mat4 r;
    const float x = giro.x, y = giro.y, z = giro.z, w = giro.w;
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;

    // Coluna 0.
    r.m[0] = (1.0F - 2.0F * (yy + zz)) * escala.x;
    r.m[1] = (2.0F * (xy + wz)) * escala.x;
    r.m[2] = (2.0F * (xz - wy)) * escala.x;
    // Coluna 1.
    r.m[4] = (2.0F * (xy - wz)) * escala.y;
    r.m[5] = (1.0F - 2.0F * (xx + zz)) * escala.y;
    r.m[6] = (2.0F * (yz + wx)) * escala.y;
    // Coluna 2.
    r.m[8] = (2.0F * (xz + wy)) * escala.z;
    r.m[9] = (2.0F * (yz - wx)) * escala.z;
    r.m[10] = (1.0F - 2.0F * (xx + yy)) * escala.z;
    // Coluna 3: a translacao.
    r.m[12] = posicao.x;
    r.m[13] = posicao.y;
    r.m[14] = posicao.z;
    return r;
  }

  [[nodiscard]] static constexpr Mat4 de_translacao(Vec3 t) noexcept {
    Mat4 r;
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
  }

  [[nodiscard]] static constexpr Mat4 de_escala(Vec3 e) noexcept {
    Mat4 r;
    r.m[0] = e.x;
    r.m[5] = e.y;
    r.m[10] = e.z;
    return r;
  }
};

[[nodiscard]] inline Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
  Mat4 r;
  for (int c = 0; c < 4; ++c) {
    for (int l = 0; l < 4; ++l) {
      float s = 0.0F;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + l] * b.m[c * 4 + k];
      r.m[c * 4 + l] = s;
    }
  }
  return r;
}

[[nodiscard]] inline Vec4 operator*(const Mat4& a, Vec4 v) noexcept {
  return Vec4{
      a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w,
      a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w,
      a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
      a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w,
  };
}

[[nodiscard]] inline Vec3 transformar_ponto(const Mat4& a, Vec3 p) noexcept {
  const Vec4 r = a * Vec4{p.x, p.y, p.z, 1.0F};
  return {r.x, r.y, r.z};
}

[[nodiscard]] inline Vec3 transformar_vetor(const Mat4& a, Vec3 v) noexcept {
  const Vec4 r = a * Vec4{v.x, v.y, v.z, 0.0F};
  return {r.x, r.y, r.z};
}

/// A INVERSA. Devolve falso quando a matriz e singular (escala zero em
/// algum eixo, que o painel permite digitar) — e quem chama decide o que
/// fazer com isso em vez de receber NaN.
[[nodiscard]] inline bool inverter(const Mat4& a, Mat4& saida) noexcept {
  const float* m = a.m;
  float inv[16];

  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::fabs(det) < 1e-12F) return false;
  det = 1.0F / det;
  for (int i = 0; i < 16; ++i) saida.m[i] = inv[i] * det;
  return true;
}

[[nodiscard]] inline Mat4 transposta(const Mat4& a) noexcept {
  Mat4 r;
  for (int c = 0; c < 4; ++c)
    for (int l = 0; l < 4; ++l) r.m[c * 4 + l] = a.m[l * 4 + c];
  return r;
}

/// A MATRIZ QUE LEVA UMA BASE LOCAL PARA A BASE DO MODELO EM PIXELS DO
/// QUADRO. A mesma conta do painel do Aurea: a ancora sai da posicao, a
/// escala multiplica, o giro vem em ultimo e a posicao soma.
///
/// `ancora` esta em unidades do MODELO (0..1 do tamanho da caixa), e nao em
/// pixels: e assim que o painel do editor escreve, e converter aqui evita
/// que o mesmo modelo mude de lugar quando a camada e reescalada.
[[nodiscard]] inline Mat4 local_da_camada(Vec3 posicao, Vec3 rotacao_graus,
                                          Vec3 escala, Vec3 ancora,
                                          Vec3 tamanho_do_modelo) noexcept {
  const Vec3 deslocacao{ancora.x * tamanho_do_modelo.x,
                        ancora.y * tamanho_do_modelo.y,
                        ancora.z * tamanho_do_modelo.z};
  const Mat4 giro = Mat4::de_trs({0.0F, 0.0F, 0.0F},
                                 Quat::de_euler(rotacao_graus),
                                 {1.0F, 1.0F, 1.0F});
  const Mat4 desloc = Mat4::de_translacao(-deslocacao);
  const Mat4 escala_m = Mat4::de_escala(escala);
  return Mat4::de_translacao(posicao) * giro * escala_m * desloc;
}

/// A PROJECAO PERSPECTIVA. `fov` EM GRAUS VERTICAIS, e `perto`/`longe` em
/// unidades do mundo. Profundidade no intervalo [0, 1], que e o que a
/// Vulkan usa — a OpenGL usaria [-1, 1] e o resultado seria um z-fight
/// silencioso na metade da cena.
[[nodiscard]] inline Mat4 perspectiva(float fov_graus, float aspecto,
                                       float perto, float longe) noexcept {
  const float f = 1.0F / std::tan(radianos(fov_graus) * 0.5F);
  Mat4 r;
  for (float& v : r.m) v = 0.0F;
  r.m[0] = f / (aspecto <= 1e-6F ? 1.0F : aspecto);
  r.m[5] = f;
  r.m[10] = longe / (perto - longe);
  r.m[11] = -1.0F;
  r.m[14] = (longe * perto) / (perto - longe);
  return r;
}

/// A PROJECAO ORTOGRAFICA. Ja existe porque o plano pede que ela esteja
/// pronta — e porque ela e a mesma conta do 2D, o que faz a camada 3D
/// poder cair num quadro sem camera sem inventar um caso especial.
[[nodiscard]] inline Mat4 ortografica(float esquerda, float direita,
                                       float baixo, float cima, float perto,
                                       float longe) noexcept {
  Mat4 r;
  for (float& v : r.m) v = 0.0F;
  r.m[0] = 2.0F / (direita - esquerda);
  r.m[5] = 2.0F / (cima - baixo);
  r.m[10] = 1.0F / (perto - longe);
  r.m[12] = -(direita + esquerda) / (direita - esquerda);
  r.m[13] = -(cima + baixo) / (cima - baixo);
  r.m[14] = perto / (perto - longe);
  r.m[15] = 1.0F;
  return r;
}

/// A VISTA. `alvo` e o ponto para onde a camera olha.
[[nodiscard]] inline Mat4 olhar(Vec3 de, Vec3 para, Vec3 cima) noexcept {
  const Vec3 f = normalizado(para - de);
  Vec3 s = normalizado(cruzado(f, cima));
  // CAMERA APONTADA PARA O PROPRIO EIXO `cima`: o produto vetorial zera e a
  // base nao existe. Acontece de verdade quando o dono digita 90 graus de
  // rotacao em X no painel, e o sintoma seria a cena sumir. Escolhe-se
  // outro eixo de apoio e a imagem continua coerente.
  if (comprimento2(s) < 1e-12F) {
    s = normalizado(cruzado(f, Vec3{0.0F, 0.0F, 1.0F}));
    if (comprimento2(s) < 1e-12F) s = {1.0F, 0.0F, 0.0F};
  }
  const Vec3 u = cruzado(s, f);

  Mat4 r;
  r.m[0] = s.x;  r.m[4] = s.y;  r.m[8] = s.z;   r.m[12] = -ponto(s, de);
  r.m[1] = u.x;  r.m[5] = u.y;  r.m[9] = u.z;   r.m[13] = -ponto(u, de);
  r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = ponto(f, de);
  r.m[3] = 0.0F; r.m[7] = 0.0F; r.m[11] = 0.0F; r.m[15] = 1.0F;
  return r;
}

// -------------------------------------------------------------- caixas

/// A CAIXA QUE ENVOLVE. Serve para tres coisas ao mesmo tempo: enquadrar a
/// camera quando o modelo chega, cortar o que esta fora da tela (§17) e
/// responder ao toque antes de olhar triangulo por triangulo (§30).
struct Caixa {
  Vec3 minimo{0.0F, 0.0F, 0.0F};
  Vec3 maximo{0.0F, 0.0F, 0.0F};
  bool vazia = true;

  void incluir(Vec3 p) noexcept {
    if (vazia) {
      minimo = p;
      maximo = p;
      vazia = false;
      return;
    }
    if (p.x < minimo.x) minimo.x = p.x;
    if (p.y < minimo.y) minimo.y = p.y;
    if (p.z < minimo.z) minimo.z = p.z;
    if (p.x > maximo.x) maximo.x = p.x;
    if (p.y > maximo.y) maximo.y = p.y;
    if (p.z > maximo.z) maximo.z = p.z;
  }

  void incluir(const Caixa& outra) noexcept {
    if (outra.vazia) return;
    incluir(outra.minimo);
    incluir(outra.maximo);
  }

  [[nodiscard]] Vec3 tamanho() const noexcept {
    if (vazia) return {0.0F, 0.0F, 0.0F};
    return maximo - minimo;
  }

  [[nodiscard]] Vec3 centro() const noexcept {
    if (vazia) return {0.0F, 0.0F, 0.0F};
    return (minimo + maximo) * 0.5F;
  }

  [[nodiscard]] float raio() const noexcept {
    if (vazia) return 0.0F;
    return comprimento(maximo - minimo) * 0.5F;
  }
};

/// A CAIXA DEPOIS DE UMA MATRIZ. Os oito cantos, e nao dois: uma caixa
/// girada deixa de ser alinhada aos eixos, e transformar so o minimo e o
/// maximo daria uma caixa menor do que o objeto — o que corta a ponta do
/// modelo que aparece na tela.
[[nodiscard]] inline Caixa transformar_caixa(const Caixa& c,
                                             const Mat4& m) noexcept {
  Caixa r;
  if (c.vazia) return r;
  for (int i = 0; i < 8; ++i) {
    const Vec3 p{(i & 1) ? c.maximo.x : c.minimo.x,
                 (i & 2) ? c.maximo.y : c.minimo.y,
                 (i & 4) ? c.maximo.z : c.minimo.z};
    r.incluir(transformar_ponto(m, p));
  }
  return r;
}

/// O RAIO ATINGE A CAIXA? Teste de laje (slab), o barato que responde
/// "talvez" antes do teste fino por triangulo.
[[nodiscard]] inline bool raio_atinge(const Caixa& c, Vec3 origem, Vec3 dir,
                                      float& t_entrada) noexcept {
  if (c.vazia) return false;
  float t0 = 0.0F, t1 = 1e30F;
  const float o[3] = {origem.x, origem.y, origem.z};
  const float d[3] = {dir.x, dir.y, dir.z};
  const float lo[3] = {c.minimo.x, c.minimo.y, c.minimo.z};
  const float hi[3] = {c.maximo.x, c.maximo.y, c.maximo.z};
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-9F) {
      if (o[i] < lo[i] || o[i] > hi[i]) return false;
      continue;
    }
    const float inv = 1.0F / d[i];
    float a = (lo[i] - o[i]) * inv;
    float b = (hi[i] - o[i]) * inv;
    if (a > b) {
      const float t = a;
      a = b;
      b = t;
    }
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
    if (t0 > t1) return false;
  }
  t_entrada = t0;
  return true;
}

/// MOLLER-TRUMBORE. Devolve a distancia ao longo do raio, ou negativo
/// quando nao bate. O sinal fica no retorno porque zero e uma resposta
/// legitima (o raio nasce em cima do triangulo).
[[nodiscard]] inline float raio_triangulo(Vec3 origem, Vec3 dir, Vec3 a,
                                          Vec3 b, Vec3 c) noexcept {
  const Vec3 e1 = b - a;
  const Vec3 e2 = c - a;
  const Vec3 p = cruzado(dir, e2);
  const float det = ponto(e1, p);
  if (std::fabs(det) < 1e-12F) return -1.0F;
  const float inv = 1.0F / det;
  const Vec3 t = origem - a;
  const float u = ponto(t, p) * inv;
  if (u < -1e-6F || u > 1.0F + 1e-6F) return -1.0F;
  const Vec3 q = cruzado(t, e1);
  const float v = ponto(dir, q) * inv;
  if (v < -1e-6F || u + v > 1.0F + 1e-6F) return -1.0F;
  const float dist = ponto(e2, q) * inv;
  return dist > 1e-6F ? dist : -1.0F;
}

}  // namespace aurea::render::geo

#endif  // AUREA_RENDER_VETOR_H
