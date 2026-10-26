// =============================================================================
//  Aurea / vector / PathEdit.hpp
//
//  Omega conteúdo (área 06, P1-8): as REGRAS de edição de ponto de um caminho
//  bezier, num lugar só. Antes moravam duplicadas no palco do Android
//  (VectorStage.kt) e do iOS (PreviewMetalView.swift); agora as duas
//  interfaces chamam `Engine::edit_vector_path`, que usa estas funções.
//
//  Tudo no espaço do GRUPO (o mesmo dos vértices). As tangentes são
//  RELATIVAS ao ponto (BezierVertex::in/out), como no resto do motor.
//  Funções puras: nada de motor, desfazer ou keyframe aqui.
// =============================================================================
#pragma once

#include "aurea/vector/VectorData.hpp"

namespace aurea::vector::pathedit {

/// Quantos trechos o caminho tem (fechado: n; aberto: n − 1).
[[nodiscard]] u32 segment_count(const BezierPath& p) noexcept;

/// Ponto da cúbica do trecho `segment` em `t` (0..1). Falso se o trecho não existe.
bool point_on_segment(const BezierPath& p, u32 segment, f32 t, Vec2& out) noexcept;

/// Comprimento aproximado do trecho (polilinha de 24 passos).
[[nodiscard]] f32 segment_length(const BezierPath& p, u32 segment) noexcept;

/// Divide o trecho em `t` (De Casteljau): a curva continua IGUAL. Devolve o
/// índice do ponto novo, ou −1 se o trecho não existe.
i32 insert_at(BezierPath& p, u32 segment, f32 t) noexcept;

/// Tira o ponto `i`. Com menos de 3 pontos o caminho abre. Falso se `i` não existe.
bool remove_point(BezierPath& p, u32 i) noexcept;

/// O ponto tem alça (alguma tangente não nula)?
[[nodiscard]] bool point_is_curve(const BezierPath& p, u32 i) noexcept;

/// Canto ↔ curva. Curva: alças a 1/3 de cada trecho vizinho, na direção que
/// liga os vizinhos (ponta de caminho aberto: a direção do único vizinho).
/// Canto: as duas alças somem. Falso se nada muda.
bool set_point_type(BezierPath& p, u32 i, bool curve) noexcept;

/// Move os pontos (e as alças junto) por `delta`.
void move_points(BezierPath& p, const u32* indices, usize count, Vec2 delta) noexcept;

/// Move a alça `out` (true) ou `in` (false) do ponto `i` por `delta`.
///  - `lockAngle`: a alça oposta fica na MESMA reta (direção contrária),
///    com o comprimento dela;
///  - `lockLength` (só com `lockAngle`): a oposta também copia o comprimento.
/// Sem trava as duas alças andam soltas. Falso se `i` não existe.
bool move_handle(BezierPath& p, u32 i, bool out, Vec2 delta, bool lockAngle, bool lockLength) noexcept;

/// Põe um ponto (canto) em `at`, no fim do caminho ou no começo (`atStart`).
/// Devolve o índice dele.
u32 append_point(BezierPath& p, Vec2 at, bool atStart) noexcept;

/// Fecha/abre o caminho (fechar pede ao menos 2 pontos). Falso se nada muda.
bool set_closed(BezierPath& p, bool closed) noexcept;

} // namespace aurea::vector::pathedit
