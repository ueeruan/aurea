#include "PluginExpr.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace aurea::plugins {

namespace {
constexpr usize kMaxExprBytes = 512;
constexpr usize kMaxInstructions = 256;
constexpr u32 kMaxStack = 64;
constexpr u32 kMaxDepth = 32;
constexpr f32 kPi = 3.14159265358979323846f;
} // namespace

/// Analisador descendente recursivo (precedência: ^ > unário > * / % > + -).
struct ExprParser {
    std::string_view text;
    const std::vector<ExprSymbol>& symbols;
    Expr& out;
    std::string& error;
    usize pos = 0;
    u32 depth = 0;
    i32 stack = 0;
    i32 maxStack = 0;

    void push(Expr::Instr in, i32 delta) {
        out.code_.push_back(in);
        stack += delta;
        maxStack = std::max(maxStack, stack);
    }
    bool fail(const std::string& what) {
        if (error.empty()) error = what + " (coluna " + std::to_string(pos + 1) + ")";
        return false;
    }
    void skip() { while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos; }
    bool eat(char c) {
        skip();
        if (pos < text.size() && text[pos] == c) { ++pos; return true; }
        return false;
    }

    bool sum() {
        if (!product()) return false;
        for (;;) {
            if (eat('+')) { if (!product()) return false; push({Expr::Op::Add}, -1); }
            else if (eat('-')) { if (!product()) return false; push({Expr::Op::Sub}, -1); }
            else return true;
        }
    }
    bool product() {
        if (!unary()) return false;
        for (;;) {
            if (eat('*')) { if (!unary()) return false; push({Expr::Op::Mul}, -1); }
            else if (eat('/')) { if (!unary()) return false; push({Expr::Op::Div}, -1); }
            else if (eat('%')) { if (!unary()) return false; push({Expr::Op::Mod}, -1); }
            else return true;
        }
    }
    bool unary() {
        if (++depth > kMaxDepth) return fail("expressao aninhada demais");
        bool ok;
        if (eat('-')) { ok = unary(); if (ok) push({Expr::Op::Neg}, 0); }
        else if (eat('+')) ok = unary();
        else ok = power();
        --depth;
        return ok;
    }
    bool power() {
        if (!primary()) return false;
        if (eat('^')) {
            if (!unary()) return false;   // à direita: 2^3^2 = 2^(3^2)
            push({Expr::Op::Pow}, -1);
        }
        return true;
    }
    bool number() {
        const char* begin = text.data() + pos;
        char* end = nullptr;
        // strtod precisa de terminação: copia o trecho numérico.
        usize n = 0;
        while (pos + n < text.size() && (std::isdigit(static_cast<unsigned char>(text[pos + n])) || text[pos + n] == '.'
               || ((text[pos + n] == 'e' || text[pos + n] == 'E') && n > 0)
               || ((text[pos + n] == '-' || text[pos + n] == '+') && n > 0
                   && (text[pos + n - 1] == 'e' || text[pos + n - 1] == 'E')))) ++n;
        const std::string digits(begin, n);
        const double v = std::strtod(digits.c_str(), &end);
        if (!end || end == digits.c_str() || static_cast<usize>(end - digits.c_str()) != n || !std::isfinite(v))
            return fail("numero invalido");
        pos += n;
        Expr::Instr in{Expr::Op::Push};
        in.value = static_cast<f32>(v);
        push(in, 1);
        return true;
    }
    bool call(std::string_view name) {
        struct Fn { const char* name; Expr::Op op; i32 minArgs; i32 maxArgs; };
        static const Fn kFns[] = {
            {"min", Expr::Op::Min, 2, 8}, {"max", Expr::Op::Max, 2, 8}, {"clamp", Expr::Op::Clamp, 3, 3},
            {"mix", Expr::Op::Mix, 3, 3}, {"step", Expr::Op::Step, 2, 2}, {"smoothstep", Expr::Op::Smoothstep, 3, 3},
            {"sin", Expr::Op::Sin, 1, 1}, {"cos", Expr::Op::Cos, 1, 1}, {"tan", Expr::Op::Tan, 1, 1},
            {"atan", Expr::Op::Atan, 1, 2}, {"abs", Expr::Op::Abs, 1, 1}, {"sign", Expr::Op::Sign, 1, 1},
            {"pow", Expr::Op::PowFn, 2, 2}, {"sqrt", Expr::Op::Sqrt, 1, 1}, {"exp", Expr::Op::Exp, 1, 1},
            {"log", Expr::Op::Log, 1, 1}, {"floor", Expr::Op::Floor, 1, 1}, {"ceil", Expr::Op::Ceil, 1, 1},
            {"round", Expr::Op::Round, 1, 1}, {"fract", Expr::Op::Fract, 1, 1},
            {"radians", Expr::Op::Radians, 1, 1}, {"degrees", Expr::Op::Degrees, 1, 1},
        };
        const Fn* fn = nullptr;
        for (const Fn& f : kFns) if (name == f.name) { fn = &f; break; }
        if (!fn) return fail("funcao desconhecida '" + std::string(name) + "'");
        i32 argc = 0;
        if (!eat(')')) {
            do {
                if (!sum()) return false;
                ++argc;
            } while (eat(','));
            if (!eat(')')) return fail("falta ')' depois dos argumentos de " + std::string(name));
        }
        if (argc < fn->minArgs || argc > fn->maxArgs)
            return fail(std::string(name) + " recebe " + std::to_string(fn->minArgs)
                        + (fn->maxArgs != fn->minArgs ? " a " + std::to_string(fn->maxArgs) : std::string{}) + " argumento(s)");
        Expr::Instr in{fn->op};
        if (fn->op == Expr::Op::Atan && argc == 2) in.op = Expr::Op::Atan2;
        in.argc = static_cast<u32>(argc);
        push(in, 1 - argc);
        return true;
    }
    bool primary() {
        skip();
        if (pos >= text.size()) return fail("expressao incompleta");
        const char c = text[pos];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') return number();
        if (c == '(') {
            ++pos;
            if (!sum()) return false;
            if (!eat(')')) return fail("falta ')'");
            return true;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const usize start = pos;
            while (pos < text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) ++pos;
            std::string name(text.substr(start, pos - start));
            if (eat('(')) return call(name);
            if (pos < text.size() && text[pos] == '.') {
                const usize dot = pos++;
                const usize cs = pos;
                while (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos]))) ++pos;
                if (pos == cs) { pos = dot; return fail("componente vazio depois do ponto"); }
                name += '.';
                name.append(text.substr(cs, pos - cs));
            }
            if (name == "pi") { Expr::Instr in{Expr::Op::Push}; in.value = kPi; push(in, 1); return true; }
            for (const ExprSymbol& s : symbols) {
                if (s.name != name) continue;
                Expr::Instr in{Expr::Op::Load};
                in.slot = s.slot;
                in.component = s.component;
                out.constant_ = false;
                push(in, 1);
                return true;
            }
            pos = start;
            return fail("nome desconhecido '" + name + "' (use o id de um parametro do plugin)");
        }
        return fail(std::string("caractere inesperado '") + c + "'");
    }
};

bool Expr::compile(std::string_view text, const std::vector<ExprSymbol>& symbols, std::string& error) {
    code_.clear();
    constant_ = true;
    error.clear();
    if (text.size() > kMaxExprBytes) { error = "expressao com mais de 512 caracteres"; return false; }
    ExprParser p{text, symbols, *this, error};
    if (!p.sum()) { code_.clear(); return false; }
    p.skip();
    if (p.pos != text.size()) {
        p.fail(std::string("texto sobrando: '") + std::string(text.substr(p.pos)) + "'");
        code_.clear();
        return false;
    }
    if (code_.size() > kMaxInstructions) { error = "expressao longa demais"; code_.clear(); return false; }
    if (p.maxStack > static_cast<i32>(kMaxStack) || p.stack != 1) { error = "expressao invalida"; code_.clear(); return false; }
    return true;
}

f32 Expr::evaluate(const ParamValue* values, u32 count) const noexcept {
    f32 st[kMaxStack];
    u32 sp = 0;
    for (const Instr& in : code_) {
        switch (in.op) {
            case Op::Push: st[sp++] = in.value; break;
            case Op::Load: st[sp++] = in.slot < count && values ? values[in.slot].v[in.component & 3] : 0.0f; break;
            case Op::Neg: st[sp - 1] = -st[sp - 1]; break;
            case Op::Add: --sp; st[sp - 1] += st[sp]; break;
            case Op::Sub: --sp; st[sp - 1] -= st[sp]; break;
            case Op::Mul: --sp; st[sp - 1] *= st[sp]; break;
            case Op::Div: --sp; st[sp - 1] = st[sp] != 0.0f ? st[sp - 1] / st[sp] : 0.0f; break;
            case Op::Mod: --sp; st[sp - 1] = st[sp] != 0.0f ? st[sp - 1] - st[sp] * std::floor(st[sp - 1] / st[sp]) : 0.0f; break;
            case Op::Pow: case Op::PowFn: --sp; st[sp - 1] = std::pow(st[sp - 1], st[sp]); break;
            case Op::Min: case Op::Max: {
                const u32 n = in.argc;
                f32 r = st[sp - n];
                for (u32 k = 1; k < n; ++k) r = in.op == Op::Min ? std::min(r, st[sp - n + k]) : std::max(r, st[sp - n + k]);
                sp -= n;
                st[sp++] = r;
                break;
            }
            case Op::Clamp: sp -= 2; st[sp - 1] = std::min(std::max(st[sp - 1], st[sp]), st[sp + 1]); break;
            case Op::Mix: sp -= 2; st[sp - 1] = st[sp - 1] + (st[sp] - st[sp - 1]) * st[sp + 1]; break;
            case Op::Step: --sp; st[sp - 1] = st[sp] < st[sp - 1] ? 0.0f : 1.0f; break;
            case Op::Smoothstep: {
                sp -= 2;
                const f32 e0 = st[sp - 1], e1 = st[sp], x = st[sp + 1];
                const f32 t = e1 != e0 ? std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f) : (x < e0 ? 0.0f : 1.0f);
                st[sp - 1] = t * t * (3.0f - 2.0f * t);
                break;
            }
            case Op::Sin: st[sp - 1] = std::sin(st[sp - 1]); break;
            case Op::Cos: st[sp - 1] = std::cos(st[sp - 1]); break;
            case Op::Tan: st[sp - 1] = std::tan(st[sp - 1]); break;
            case Op::Atan: st[sp - 1] = std::atan(st[sp - 1]); break;
            case Op::Atan2: --sp; st[sp - 1] = std::atan2(st[sp - 1], st[sp]); break;
            case Op::Abs: st[sp - 1] = std::fabs(st[sp - 1]); break;
            case Op::Sign: st[sp - 1] = st[sp - 1] > 0.0f ? 1.0f : st[sp - 1] < 0.0f ? -1.0f : 0.0f; break;
            case Op::Sqrt: st[sp - 1] = std::sqrt(std::max(0.0f, st[sp - 1])); break;
            case Op::Exp: st[sp - 1] = std::exp(st[sp - 1]); break;
            case Op::Log: st[sp - 1] = st[sp - 1] > 0.0f ? std::log(st[sp - 1]) : 0.0f; break;
            case Op::Floor: st[sp - 1] = std::floor(st[sp - 1]); break;
            case Op::Ceil: st[sp - 1] = std::ceil(st[sp - 1]); break;
            case Op::Round: st[sp - 1] = std::round(st[sp - 1]); break;
            case Op::Fract: st[sp - 1] = st[sp - 1] - std::floor(st[sp - 1]); break;
            case Op::Radians: st[sp - 1] *= kPi / 180.0f; break;
            case Op::Degrees: st[sp - 1] *= 180.0f / kPi; break;
        }
    }
    const f32 r = sp ? st[sp - 1] : 0.0f;
    return std::isfinite(r) ? r : 0.0f;
}

} // namespace aurea::plugins
