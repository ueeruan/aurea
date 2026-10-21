// =============================================================================
//  Aurea / plugins / PluginExpr.hpp
//
//  As expressões dos plugins "stack": aritmética pequena e FECHADA, compilada
//  uma vez na instalação e avaliada a cada quadro no planejamento.
//
//      amount * 40          clamp(size, 0, 1) * 300        mix(a, b, 0.5)
//
//  Números, + - * / % ^ (potência), parênteses, os parâmetros do plugin pelo
//  id (componentes: `ponto.x`/`.y`, `cor.r`/`.g`/`.b`/`.a`), `pi`, e as
//  funções min max clamp mix step smoothstep sin cos tan atan abs sign pow sqrt
//  exp log floor ceil round fract radians degrees. Nada de laço, de variável,
//  de texto ou de chamada para fora: o pior caso é um número (NaN/inf viram 0).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/effects/Parameter.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aurea::plugins {

/// Um nome que a expressão pode ler: o parâmetro `slot` do plugin, componente
/// `component` do valor resolvido.
struct ExprSymbol {
    std::string name;
    u32 slot = 0;
    u32 component = 0;
};

class Expr {
public:
    /// Compila `text`. Falso = erro (mensagem em `error`, com a coluna).
    [[nodiscard]] bool compile(std::string_view text, const std::vector<ExprSymbol>& symbols, std::string& error);
    /// Avalia nos valores do plugin (`values[slot].v[component]`).
    [[nodiscard]] f32 evaluate(const ParamValue* values, u32 count) const noexcept;
    [[nodiscard]] bool constant() const noexcept { return constant_; }

private:
    enum class Op : u8 {
        Push, Load, Neg, Add, Sub, Mul, Div, Mod, Pow,
        Min, Max, Clamp, Mix, Step, Smoothstep, Sin, Cos, Tan, Atan, Atan2, Abs, Sign, PowFn, Sqrt,
        Exp, Log, Floor, Ceil, Round, Fract, Radians, Degrees,
    };
    struct Instr {
        Op op = Op::Push;
        f32 value = 0.0f;   ///< Push
        u32 slot = 0;       ///< Load
        u32 component = 0;
        u32 argc = 0;       ///< Min/Max variádicos
    };
    std::vector<Instr> code_;
    bool constant_ = true;
    friend struct ExprParser;
};

} // namespace aurea::plugins
