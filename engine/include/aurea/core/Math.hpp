// =============================================================================
//  Aurea / core / Math.hpp
//
//  Matemática do motor. Escrito à mão de propósito:
//
//   - o motor roda no caminho quente do preview (60 fps com dezenas de layers),
//     e a avaliação de transform + interpolação de curva acontece por frame,
//     por layer. Chamar uma biblioteca genérica aqui custa chamadas que o
//     compilador não consegue inline.
//
//   - nada aqui aloca, nada lança, tudo é constexpr onde faz sentido.
//
//  Convenção de matrizes: coluna-maior (column-major), igual a GLSL/MSL/Vulkan.
//  `m[col][row]` — matrizes são `Mat4::col[c]` = Vec4.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cmath>
#include <algorithm>

namespace aurea {

inline constexpr f32 kPi      = 3.14159265358979323846f;
inline constexpr f32 kTwoPi   = 6.28318530717958647692f;
inline constexpr f32 kHalfPi  = 1.57079632679489661923f;
inline constexpr f32 kDeg2Rad = kPi / 180.0f;
inline constexpr f32 kRad2Deg = 180.0f / kPi;
inline constexpr f32 kEpsilon = 1e-6f;

[[nodiscard]] constexpr f32 clampf(f32 v, f32 lo, f32 hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}
[[nodiscard]] constexpr f32 saturate(f32 v) noexcept { return clampf(v, 0.0f, 1.0f); }
[[nodiscard]] constexpr f32 lerpf(f32 a, f32 b, f32 t) noexcept { return a + (b - a) * t; }
[[nodiscard]] constexpr i32 clampi(i32 v, i32 lo, i32 hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Aproximação de `pow` para expoentes pequenos — usada na atenuação de luz e
/// no falloff de partículas, onde a precisão total é irrelevante.
[[nodiscard]] inline f32 fast_pow(f32 base, f32 exp) noexcept { return std::pow(base, exp); }

// -----------------------------------------------------------------------------
// Vec2
// -----------------------------------------------------------------------------
struct Vec2 {
    f32 x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(f32 x_, f32 y_) noexcept : x(x_), y(y_) {}
    constexpr explicit Vec2(f32 s) noexcept : x(s), y(s) {}

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator*(Vec2 a, f32 s) noexcept { return {a.x * s, a.y * s}; }
    friend constexpr Vec2 operator/(Vec2 a, f32 s) noexcept { return {a.x / s, a.y / s}; }
    friend constexpr Vec2 operator-(Vec2 a) noexcept { return {-a.x, -a.y}; }
    constexpr Vec2& operator+=(Vec2 o) noexcept { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) noexcept { x -= o.x; y -= o.y; return *this; }
    friend constexpr bool operator==(Vec2, Vec2) noexcept = default;

    [[nodiscard]] constexpr f32 dot(Vec2 o) const noexcept { return x * o.x + y * o.y; }
    [[nodiscard]] constexpr f32 length_sq() const noexcept { return dot(*this); }
    [[nodiscard]] inline f32 length() const noexcept { return std::sqrt(length_sq()); }
    [[nodiscard]] inline Vec2 normalized() const noexcept {
        const f32 l = length();
        return l > kEpsilon ? Vec2{x / l, y / l} : Vec2{0.0f, 0.0f};
    }
};

// -----------------------------------------------------------------------------
// Vec3
// -----------------------------------------------------------------------------
struct Vec3 {
    f32 x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(f32 x_, f32 y_, f32 z_) noexcept : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec3(f32 s) noexcept : x(s), y(s), z(s) {}

    friend constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
    friend constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
    friend constexpr Vec3 operator*(Vec3 a, f32 s) noexcept { return {a.x*s, a.y*s, a.z*s}; }
    friend constexpr Vec3 operator*(f32 s, Vec3 a) noexcept { return a * s; }
    friend constexpr Vec3 operator/(Vec3 a, f32 s) noexcept { return {a.x/s, a.y/s, a.z/s}; }
    friend constexpr Vec3 operator-(Vec3 a) noexcept { return {-a.x, -a.y, -a.z}; }
    constexpr Vec3& operator+=(Vec3 o) noexcept { x+=o.x; y+=o.y; z+=o.z; return *this; }
    friend constexpr bool operator==(Vec3, Vec3) noexcept = default;

    [[nodiscard]] constexpr f32 dot(Vec3 o) const noexcept { return x*o.x + y*o.y + z*o.z; }
    [[nodiscard]] constexpr Vec3 cross(Vec3 o) const noexcept {
        return {y*o.z - z*o.y, z*o.x - x*o.z, x*o.y - y*o.x};
    }
    [[nodiscard]] constexpr f32 length_sq() const noexcept { return dot(*this); }
    [[nodiscard]] inline f32 length() const noexcept { return std::sqrt(length_sq()); }
    [[nodiscard]] inline Vec3 normalized() const noexcept {
        const f32 l = length();
        return l > kEpsilon ? Vec3{x/l, y/l, z/l} : Vec3{0.0f, 0.0f, 0.0f};
    }
    [[nodiscard]] static constexpr Vec3 lerp(Vec3 a, Vec3 b, f32 t) noexcept {
        return {lerpf(a.x,b.x,t), lerpf(a.y,b.y,t), lerpf(a.z,b.z,t)};
    }
};

// -----------------------------------------------------------------------------
// Vec4 — também usado como cor linear (RGBA, pré-multiplicado onde couber)
// -----------------------------------------------------------------------------
struct Vec4 {
    f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(f32 x_, f32 y_, f32 z_, f32 w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(Vec3 v, f32 w_) noexcept : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr explicit Vec4(f32 s) noexcept : x(s), y(s), z(s), w(s) {}

    friend constexpr Vec4 operator+(Vec4 a, Vec4 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w}; }
    friend constexpr Vec4 operator-(Vec4 a, Vec4 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z,a.w-b.w}; }
    friend constexpr Vec4 operator*(Vec4 a, f32 s) noexcept { return {a.x*s,a.y*s,a.z*s,a.w*s}; }
    friend constexpr Vec4 operator*(f32 s, Vec4 a) noexcept { return a * s; }
    /// Produto componente a componente — é o blend multiplicativo do compositor.
    friend constexpr Vec4 operator*(Vec4 a, Vec4 b) noexcept {
        return {a.x*b.x, a.y*b.y, a.z*b.z, a.w*b.w};
    }
    friend constexpr bool operator==(Vec4, Vec4) noexcept = default;

    [[nodiscard]] constexpr f32 dot(Vec4 o) const noexcept { return x*o.x + y*o.y + z*o.z + w*o.w; }
    [[nodiscard]] constexpr Vec3 xyz() const noexcept { return {x, y, z}; }
    [[nodiscard]] static constexpr Vec4 lerp(Vec4 a, Vec4 b, f32 t) noexcept {
        return {lerpf(a.x,b.x,t), lerpf(a.y,b.y,t), lerpf(a.z,b.z,t), lerpf(a.w,b.w,t)};
    }
};

// -----------------------------------------------------------------------------
// Rect
// -----------------------------------------------------------------------------
struct Rect {
    f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(f32 x_, f32 y_, f32 w_, f32 h_) noexcept : x(x_), y(y_), w(w_), h(h_) {}

    [[nodiscard]] constexpr f32 right()  const noexcept { return x + w; }
    [[nodiscard]] constexpr f32 bottom() const noexcept { return y + h; }
    [[nodiscard]] constexpr f32 area()   const noexcept { return w * h; }
    [[nodiscard]] constexpr bool empty() const noexcept { return w <= 0.0f || h <= 0.0f; }

    [[nodiscard]] constexpr bool contains(Vec2 p) const noexcept {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }
    [[nodiscard]] constexpr bool intersects(Rect o) const noexcept {
        return !(o.x >= right() || o.right() <= x || o.y >= bottom() || o.bottom() <= y);
    }
    [[nodiscard]] static constexpr Rect intersect(Rect a, Rect b) noexcept {
        const f32 nx = std::max(a.x, b.x);
        const f32 ny = std::max(a.y, b.y);
        const f32 nr = std::min(a.right(),  b.right());
        const f32 nb = std::min(a.bottom(), b.bottom());
        return Rect{nx, ny, std::max(0.0f, nr - nx), std::max(0.0f, nb - ny)};
    }
    [[nodiscard]] static constexpr Rect unite(Rect a, Rect b) noexcept {
        const f32 nx = std::min(a.x, b.x);
        const f32 ny = std::min(a.y, b.y);
        const f32 nr = std::max(a.right(),  b.right());
        const f32 nb = std::max(a.bottom(), b.bottom());
        return Rect{nx, ny, nr - nx, nb - ny};
    }
};

// -----------------------------------------------------------------------------
// Quaternion — rotação 3D. Guardado normalizado.
// -----------------------------------------------------------------------------
struct Quat {
    f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(f32 x_, f32 y_, f32 z_, f32 w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}

    [[nodiscard]] static Quat identity() noexcept { return {0,0,0,1}; }

    [[nodiscard]] static Quat from_axis_angle(Vec3 axis, f32 radians) noexcept {
        const Vec3 a = axis.normalized();
        const f32 h = radians * 0.5f;
        const f32 s = std::sin(h);
        return {a.x*s, a.y*s, a.z*s, std::cos(h)};
    }

    /// Ordem ZYX (yaw → pitch → roll), que é a convenção de Euler usada na
    /// UI do editor: rotação em Z é o giro 2D do dia a dia, X e Y inclinam.
    [[nodiscard]] static Quat from_euler_zyx(f32 rx, f32 ry, f32 rz) noexcept {
        const f32 cx = std::cos(rx*0.5f), sx = std::sin(rx*0.5f);
        const f32 cy = std::cos(ry*0.5f), sy = std::sin(ry*0.5f);
        const f32 cz = std::cos(rz*0.5f), sz = std::sin(rz*0.5f);
        return {
            sx*cy*cz - cx*sy*sz,
            cx*sy*cz + sx*cy*sz,
            cx*cy*sz - sx*sy*cz,
            cx*cy*cz + sx*sy*sz,
        };
    }

    [[nodiscard]] Quat operator*(const Quat& o) const noexcept {
        return {
            w*o.x + x*o.w + y*o.z - z*o.y,
            w*o.y - x*o.z + y*o.w + z*o.x,
            w*o.z + x*o.y - y*o.x + z*o.w,
            w*o.w - x*o.x - y*o.y - z*o.z,
        };
    }

    [[nodiscard]] Quat normalized() const noexcept {
        const f32 l = std::sqrt(x*x + y*y + z*z + w*w);
        if (l < kEpsilon) return identity();
        return {x/l, y/l, z/l, w/l};
    }

    [[nodiscard]] static Quat slerp(Quat a, Quat b, f32 t) noexcept {
        f32 d = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
        if (d < 0.0f) { b = {-b.x,-b.y,-b.z,-b.w}; d = -d; }
        if (d > 0.9995f) {
            return Quat{lerpf(a.x,b.x,t), lerpf(a.y,b.y,t), lerpf(a.z,b.z,t), lerpf(a.w,b.w,t)}.normalized();
        }
        const f32 theta = std::acos(clampf(d, -1.0f, 1.0f));
        const f32 st = std::sin(theta);
        const f32 wa = std::sin((1.0f - t) * theta) / st;
        const f32 wb = std::sin(t * theta) / st;
        return {a.x*wa + b.x*wb, a.y*wa + b.y*wb, a.z*wa + b.z*wb, a.w*wa + b.w*wb};
    }
};

// -----------------------------------------------------------------------------
// Mat4 — coluna-maior. `col[c]` é a coluna c.
// -----------------------------------------------------------------------------
struct Mat4 {
    Vec4 col[4] = {
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {0,0,0,1},
    };

    [[nodiscard]] static constexpr Mat4 identity() noexcept { return Mat4{}; }

    [[nodiscard]] static Mat4 translation(Vec3 t) noexcept {
        Mat4 m;
        m.col[3] = {t.x, t.y, t.z, 1.0f};
        return m;
    }

    [[nodiscard]] static Mat4 scale(Vec3 s) noexcept {
        Mat4 m;
        m.col[0].x = s.x;
        m.col[1].y = s.y;
        m.col[2].z = s.z;
        return m;
    }

    [[nodiscard]] static Mat4 from_quat(Quat q) noexcept {
        const f32 xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
        const f32 xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
        const f32 wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
        Mat4 m;
        m.col[0] = {1.0f - 2.0f*(yy+zz),        2.0f*(xy+wz),        2.0f*(xz-wy),        0.0f};
        m.col[1] = {       2.0f*(xy-wz), 1.0f - 2.0f*(xx+zz),        2.0f*(yz+wx),        0.0f};
        m.col[2] = {       2.0f*(xz+wy),        2.0f*(yz-wx), 1.0f - 2.0f*(xx+yy),        0.0f};
        m.col[3] = {0.0f, 0.0f, 0.0f, 1.0f};
        return m;
    }

    /// Matriz de perspectiva com profundidade reversa em [0,1] (Vulkan/Metal).
    /// `fovYRadians` é o campo de visão vertical.
    [[nodiscard]] static Mat4 perspective(f32 fovYRadians, f32 aspect,
                                          f32 nearZ, f32 farZ) noexcept {
        const f32 f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 m;
        m.col[0] = {f / aspect, 0.0f, 0.0f, 0.0f};
        m.col[1] = {0.0f, f, 0.0f, 0.0f};
        m.col[2] = {0.0f, 0.0f, farZ / (nearZ - farZ), -1.0f};
        m.col[3] = {0.0f, 0.0f, (nearZ * farZ) / (nearZ - farZ), 0.0f};
        return m;
    }

    /// Ortográfica em [0,1]. É o que o compositor 2D usa: as layers vivem em
    /// espaço de composição, sem perspectiva.
    [[nodiscard]] static Mat4 ortho(f32 left, f32 right_, f32 bottom, f32 top,
                                    f32 nearZ, f32 farZ) noexcept {
        Mat4 m;
        m.col[0] = {2.0f / (right_ - left), 0.0f, 0.0f, 0.0f};
        m.col[1] = {0.0f, 2.0f / (top - bottom), 0.0f, 0.0f};
        m.col[2] = {0.0f, 0.0f, 1.0f / (nearZ - farZ), 0.0f};
        m.col[3] = {-(right_ + left) / (right_ - left),
                    -(top + bottom) / (top - bottom),
                    nearZ / (nearZ - farZ), 1.0f};
        return m;
    }

    /// Olha de `eye` para `center`. Constrói a base como o Vulkan espera:
    /// Z apontando para trás do alvo (right-handed, profundidade cresce).
    [[nodiscard]] static Mat4 look_at(Vec3 eye, Vec3 center, Vec3 up) noexcept {
        const Vec3 f = (center - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        if (s.length_sq() < kEpsilon) {
            // Câmera olhando exatamente na direção de `up`: escolhe um eixo
            // arbitrário em vez de produzir NaN.
            s = f.cross(Vec3{0.0f, 0.0f, 1.0f}).normalized();
        }
        const Vec3 u = s.cross(f);
        Mat4 m;
        m.col[0] = { s.x,  u.x, -f.x, 0.0f};
        m.col[1] = { s.y,  u.y, -f.y, 0.0f};
        m.col[2] = { s.z,  u.z, -f.z, 0.0f};
        m.col[3] = {-s.dot(eye), -u.dot(eye), f.dot(eye), 1.0f};
        return m;
    }

    [[nodiscard]] Mat4 operator*(const Mat4& o) const noexcept {
        Mat4 r;
        for (int c = 0; c < 4; ++c) {
            const Vec4& oc = o.col[c];
            r.col[c] = {
                col[0].x*oc.x + col[1].x*oc.y + col[2].x*oc.z + col[3].x*oc.w,
                col[0].y*oc.x + col[1].y*oc.y + col[2].y*oc.z + col[3].y*oc.w,
                col[0].z*oc.x + col[1].z*oc.y + col[2].z*oc.z + col[3].z*oc.w,
                col[0].w*oc.x + col[1].w*oc.y + col[2].w*oc.z + col[3].w*oc.w,
            };
        }
        return r;
    }

    [[nodiscard]] Vec4 operator*(Vec4 v) const noexcept {
        return {
            col[0].x*v.x + col[1].x*v.y + col[2].x*v.z + col[3].x*v.w,
            col[0].y*v.x + col[1].y*v.y + col[2].y*v.z + col[3].y*v.w,
            col[0].z*v.x + col[1].z*v.y + col[2].z*v.z + col[3].z*v.w,
            col[0].w*v.x + col[1].w*v.y + col[2].w*v.z + col[3].w*v.w,
        };
    }

    [[nodiscard]] Vec3 transform_point(Vec3 p) const noexcept {
        const Vec4 r = (*this) * Vec4{p, 1.0f};
        if (std::abs(r.w) < kEpsilon) return r.xyz();
        return Vec3{r.x / r.w, r.y / r.w, r.z / r.w};
    }
};

// -----------------------------------------------------------------------------
// Color — linear, com conversões explícitas. Nada converte sozinho: o pipeline
// de cor é declarado em ColorPipeline, não implícito aqui.
// -----------------------------------------------------------------------------
struct Color {
    f32 r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(f32 r_, f32 g_, f32 b_, f32 a_ = 1.0f) noexcept : r(r_), g(g_), b(b_), a(a_) {}

    [[nodiscard]] static constexpr Color from_hex(u32 rgba) noexcept {
        return Color{static_cast<f32>((rgba >> 24) & 0xFF) / 255.0f,
                     static_cast<f32>((rgba >> 16) & 0xFF) / 255.0f,
                     static_cast<f32>((rgba >>  8) & 0xFF) / 255.0f,
                     static_cast<f32>( rgba        & 0xFF) / 255.0f};
    }

    [[nodiscard]] constexpr Vec4 to_vec4() const noexcept { return {r, g, b, a}; }

    /// sRGB codificado → linear. Usado na entrada de textura e no brush.
    [[nodiscard]] static inline f32 srgb_to_linear(f32 c) noexcept {
        return c <= 0.04045f ? c / 12.92f
                             : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    /// linear → sRGB codificado. Usado só na saída para display.
    [[nodiscard]] static inline f32 linear_to_srgb(f32 c) noexcept {
        return c <= 0.0031308f ? c * 12.92f
                               : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }
};

// -----------------------------------------------------------------------------
// Interpolação de curva — núcleo do avaliador de keyframes.
// -----------------------------------------------------------------------------

/// Bezier cúbico na forma de cubic-bezier(x1,y1,x2,y2) da UI.
/// Newton limitado ao intervalo da raiz, com bisseção quando a tangente é
/// horizontal ou o passo sair do intervalo. A convergência é medida no
/// parâmetro, não só em x: uma tangente horizontal pode ter erro mínimo em x
/// e ainda estar longe do valor animado correto.
[[nodiscard]] inline f32 cubic_bezier(f32 x1, f32 y1, f32 x2, f32 y2, f32 x) noexcept {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;

    auto sample = [](f64 t, f64 a1, f64 a2) noexcept {
        const f64 mt = 1.0 - t;
        return 3.0*mt*mt*t*a1 + 3.0*mt*t*t*a2 + t*t*t;
    };
    auto slope = [](f64 t, f64 a1, f64 a2) noexcept {
        const f64 mt = 1.0 - t;
        return 3.0*mt*mt*a1 + 6.0*mt*t*(a2 - a1) + 3.0*t*t*(1.0 - a2);
    };

    f64 lo = 0.0, hi = 1.0, t = x;
    for (int i = 0; i < 28; ++i) {
        const f64 err = sample(t, x1, x2) - static_cast<f64>(x);
        if (err == 0.0) break;
        if (err < 0.0) lo = t; else hi = t;
        const f64 d = slope(t, x1, x2);
        f64 next = (lo + hi) * 0.5;
        if (std::abs(d) > 1e-12) {
            const f64 newton = t - err / d;
            if (newton > lo && newton < hi) next = newton;
        }
        if (std::abs(next - t) < 1e-9) {
            t = next;
            break;
        }
        t = next;
    }
    return static_cast<f32>(sample(t, y1, y2));
}

/// Easing nomeado. `t` já vem normalizado em [0,1] (progresso entre dois
/// keyframes). Devolve o fator de mistura.
[[nodiscard]] inline f32 apply_easing(Interpolation kind, f32 t,
                                      f32 bx1, f32 by1, f32 bx2, f32 by2) noexcept {
    switch (kind) {
        case Interpolation::Hold:      return 0.0f;
        case Interpolation::Linear:    return t;
        case Interpolation::EaseIn:    return t*t;
        case Interpolation::EaseOut:   return 1.0f - (1.0f-t)*(1.0f-t);
        case Interpolation::EaseInOut: return t < 0.5f ? 2.0f*t*t
                                                       : 1.0f - 2.0f*(1.0f-t)*(1.0f-t);
        case Interpolation::Bounce: {
            if (t <= 0.f) return 0.f;
            if (t >= 1.f) return 1.f;
            if (t < 0.5f) return 4.f*t*t;
            // Three shrinking ballistic rebounds after the first landing.
            const f32 start = t < 0.75f ? 0.5f : (t < 0.9f ? 0.75f : 0.9f);
            const f32 span = t < 0.75f ? 0.25f : (t < 0.9f ? 0.15f : 0.1f);
            const f32 height = t < 0.75f ? 0.25f : (t < 0.9f ? 0.0625f : 0.015625f);
            const f32 u = (t-start)/span;
            return 1.f - 4.f*height*u*(1.f-u);
        }
        case Interpolation::Elastic: {
            if (t <= 0.f) return 0.f;
            if (t >= 1.f) return 1.f;
            // Damped oscillator, normalized to land exactly on the endpoint.
            return (1.f-std::exp(-6.f*t)*std::cos(6.f*kPi*t))/(1.f-std::exp(-6.f));
        }
        case Interpolation::Steps:
            return std::floor(clampf(t, 0.f, 1.f)*4.f)*0.25f;
        case Interpolation::Bezier:
        case Interpolation::CustomCurve:
            return cubic_bezier(bx1, by1, bx2, by2, t);
    }
    return t;
}

} // namespace aurea
