// =============================================================================
//  Aurea / expr / Expression.cpp
//
//  Léxico → parser Pratt → árvore plana (vetor de nós com índices) →
//  resolução de nomes → avaliador por árvore com contador de instruções.
//
//  Por que árvore e não bytecode: a expressão típica tem de 5 a 40 nós
//  ("wiggle(2,30)" são 4). O custo dominante é a função chamada (ruído,
//  leitura de keyframe), não o despacho do nó. A árvore plana já é contígua
//  na memória, não aloca na avaliação (os buffers de contexto são por thread
//  e reutilizados) e deixa a posição de cada nó à mão para o erro apontar a
//  coluna certa.
//
//  Nenhuma função aqui lança (o motor compila sem exceções): todo erro vira
//  `ctx.fail(pos, msg)` e a avaliação desenrola devolvendo Undef.
// =============================================================================
#include "aurea/expr/Expression.hpp"

#include "aurea/animation/Curve.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace aurea::expr {

// =============================================================================
// Programa
// =============================================================================
enum class N : u8 {
    Num, Str, Name, Local, Global, Array, Unary, Binary, And, Or, Cond, Call, Member, Index,
    Assign, IncDec, VarDecl, Block, If, For, While, Return, Break, Continue, ExprStmt, Empty,
};

struct Node {
    N   kind = N::Empty;
    u8  op = 0;         ///< operador (Unary/Binary/Assign/IncDec)
    u8  flag = 0;       ///< IncDec: 1 = prefixo
    u32 pos = 0;        ///< offset no fonte (erro aponta aqui)
    i32 a = -1, b = -1, c = -1, d = -1;
    u32 first = 0, count = 0;   ///< filhos em Program::kids
    f64 num = 0.0;
    u32 sym = 0;        ///< Str/Name/Member: índice em strings; Local: slot; Global: id
};

class Program {
public:
    std::string              source;
    std::vector<Node>        nodes;
    std::vector<i32>         kids;
    std::vector<std::string> strings;
    std::vector<std::string> localNames;
    i32                      root = -1;
};

namespace {

// Operadores binários/unários.
enum : u8 {
    OpAdd = 1, OpSub, OpMul, OpDiv, OpMod, OpPow, OpLt, OpLe, OpGt, OpGe, OpEq, OpNe,
    OpNeg, OpPos, OpNot, OpInc, OpDec, OpSet,
};

// Identificadores globais.
enum GlobalId : u32 {
    GTime = 1, GValue, GIndex, GFps, GFrame, GThisLayer, GThisComp, GThisProperty, GTransform,
    GPosition, GScale, GRotation, GOpacity, GAnchor, GVelocity, GSpeed, GNumKeys, GInPoint,
    GOutPoint, GWidth, GHeight, GMath,
    // Funções (valor K::Fn quando citadas sem chamar).
    FWiggle = 100, FLoopOut, FLoopIn, FLoopOutDur, FLoopInDur, FLinear, FEase, FEaseIn, FEaseOut,
    FClamp, FRandom, FGaussRandom, FSeedRandom, FNoise, FSin, FCos, FTan, FAsin, FAcos, FAtan,
    FAtan2, FSqrt, FAbs, FFloor, FCeil, FRound, FMin, FMax, FPow, FExp, FLog, FDeg2Rad, FRad2Deg,
    FLength, FNormalize, FAdd, FSub, FMul, FDiv, FDot, FCross, FValueAtTime, FVelocityAtTime,
    FKey, FLayer, FEffect, FFramesToTime, FTimeToFrames,
    // Métodos (valor K::Method: função + objeto receptor).
    MCompLayer = 200, MLayerEffect, MEffectParam, MPropValueAtTime, MPropVelocityAtTime, MPropKey,
};

struct NameEntry { const char* name; u32 id; };

constexpr NameEntry kGlobals[] = {
    {"time", GTime}, {"value", GValue}, {"index", GIndex}, {"fps", GFps}, {"frame", GFrame},
    {"thisLayer", GThisLayer}, {"thisComp", GThisComp}, {"thisProperty", GThisProperty},
    {"transform", GTransform}, {"position", GPosition}, {"scale", GScale}, {"rotation", GRotation},
    {"opacity", GOpacity}, {"anchorPoint", GAnchor}, {"velocity", GVelocity}, {"speed", GSpeed},
    {"numKeys", GNumKeys}, {"inPoint", GInPoint}, {"outPoint", GOutPoint}, {"width", GWidth},
    {"height", GHeight}, {"Math", GMath},
    {"wiggle", FWiggle}, {"loopOut", FLoopOut}, {"loopIn", FLoopIn}, {"loopOutDuration", FLoopOutDur},
    {"loopInDuration", FLoopInDur}, {"linear", FLinear}, {"ease", FEase}, {"easeIn", FEaseIn},
    {"easeOut", FEaseOut}, {"clamp", FClamp}, {"random", FRandom}, {"gaussRandom", FGaussRandom},
    {"seedRandom", FSeedRandom}, {"noise", FNoise}, {"sin", FSin}, {"cos", FCos}, {"tan", FTan},
    {"asin", FAsin}, {"acos", FAcos}, {"atan", FAtan}, {"atan2", FAtan2}, {"sqrt", FSqrt},
    {"abs", FAbs}, {"floor", FFloor}, {"ceil", FCeil}, {"round", FRound}, {"min", FMin},
    {"max", FMax}, {"pow", FPow}, {"exp", FExp}, {"log", FLog}, {"degreesToRadians", FDeg2Rad},
    {"radiansToDegrees", FRad2Deg}, {"length", FLength}, {"normalize", FNormalize}, {"add", FAdd},
    {"sub", FSub}, {"mul", FMul}, {"div", FDiv}, {"dot", FDot}, {"cross", FCross},
    {"valueAtTime", FValueAtTime}, {"velocityAtTime", FVelocityAtTime}, {"key", FKey},
    {"layer", FLayer}, {"effect", FEffect}, {"framesToTime", FFramesToTime},
    {"timeToFrames", FTimeToFrames},
};

u32 find_global(std::string_view s) noexcept {
    for (const NameEntry& e : kGlobals) if (s == e.name) return e.id;
    return 0;
}

// =============================================================================
// Léxico
// =============================================================================
enum class Tok : u8 { End, Num, Str, Ident, Punct };

struct Token {
    Tok         kind = Tok::End;
    u32         pos = 0;
    bool        nl = false;    ///< quebra de linha antes (evita "x\n[a,b]" virar índice)
    f64         num = 0.0;
    std::string text;
};

struct Lexer {
    std::string_view src;
    std::vector<Token> out;
    std::string error;
    u32 errPos = 0;

    bool fail(u32 pos, std::string msg) {
        if (error.empty()) { error = std::move(msg); errPos = pos; }
        return false;
    }

    bool run() {
        usize i = 0;
        bool nl = false;
        const usize n = src.size();
        while (i < n) {
            const char c = src[i];
            if (c == '\n') { nl = true; ++i; continue; }
            if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
            if (c == '/' && i + 1 < n && src[i + 1] == '/') {
                while (i < n && src[i] != '\n') ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && src[i + 1] == '*') {
                const usize end = src.find("*/", i + 2);
                if (end == std::string_view::npos) return fail(static_cast<u32>(i), "comentário /* sem fechar");
                for (usize k = i; k < end; ++k) if (src[k] == '\n') nl = true;
                i = end + 2;
                continue;
            }
            Token t;
            t.pos = static_cast<u32>(i);
            t.nl = nl;
            nl = false;
            if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < n && src[i + 1] >= '0' && src[i + 1] <= '9')) {
                usize j = i;
                while (j < n && ((src[j] >= '0' && src[j] <= '9') || src[j] == '.')) ++j;
                if (j < n && (src[j] == 'e' || src[j] == 'E')) {
                    usize k = j + 1;
                    if (k < n && (src[k] == '+' || src[k] == '-')) ++k;
                    if (k < n && src[k] >= '0' && src[k] <= '9') {
                        j = k;
                        while (j < n && src[j] >= '0' && src[j] <= '9') ++j;
                    }
                }
                const std::string num(src.substr(i, j - i));
                char* endp = nullptr;
                t.num = std::strtod(num.c_str(), &endp);
                if (!endp || *endp != '\0') return fail(t.pos, "número inválido '" + num + "'");
                t.kind = Tok::Num;
                i = j;
            } else if (c == '"' || c == '\'') {
                usize j = i + 1;
                std::string s;
                while (j < n && src[j] != c) {
                    if (src[j] == '\n') return fail(t.pos, "texto sem fechar");
                    if (src[j] == '\\' && j + 1 < n) {
                        const char e = src[j + 1];
                        s.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
                        j += 2;
                    } else {
                        s.push_back(src[j++]);
                    }
                    if (s.size() > kMaxStringBytes) return fail(t.pos, "texto longo demais");
                }
                if (j >= n) return fail(t.pos, "texto sem fechar");
                t.kind = Tok::Str;
                t.text = std::move(s);
                i = j + 1;
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$') {
                usize j = i;
                while (j < n && ((src[j] >= 'a' && src[j] <= 'z') || (src[j] >= 'A' && src[j] <= 'Z')
                                 || (src[j] >= '0' && src[j] <= '9') || src[j] == '_' || src[j] == '$')) ++j;
                t.kind = Tok::Ident;
                t.text = std::string(src.substr(i, j - i));
                i = j;
            } else {
                static constexpr const char* kPunct[] = {
                    "===", "!==", "**", "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=",
                    "+", "-", "*", "/", "%", "^", "<", ">", "!", "?", ":", "(", ")", "[", "]", "{", "}", ",",
                    ".", ";", "=",
                };
                bool found = false;
                for (const char* p : kPunct) {
                    const usize len = std::strlen(p);
                    if (src.substr(i, len) == p) {
                        t.kind = Tok::Punct;
                        t.text = p;
                        i += len;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return fail(t.pos, std::string("caractere inesperado '") + c + "'");
                }
            }
            out.push_back(std::move(t));
        }
        Token end;
        end.pos = static_cast<u32>(n);
        end.nl = true;
        out.push_back(end);
        return true;
    }
};

// =============================================================================
// Parser (descida recursiva nas instruções, Pratt nas expressões)
// =============================================================================
enum Prec : int { PAssign = 1, PCond, POr, PAnd, PEq, PCmp, PAdd, PMul, PPow, PUnary };

struct Parser {
    const std::vector<Token>& toks;
    Program& prog;
    usize at = 0;
    u32 depth = 0;
    std::string error;
    u32 errPos = 0;

    Parser(const std::vector<Token>& t, Program& p) : toks(t), prog(p) {}

    const Token& peek(usize k = 0) const { return toks[std::min(at + k, toks.size() - 1)]; }
    bool is(const char* p, usize k = 0) const { const Token& t = peek(k); return t.kind == Tok::Punct && t.text == p; }
    bool is_kw(const char* w) const { const Token& t = peek(); return t.kind == Tok::Ident && t.text == w; }
    bool accept(const char* p) { if (is(p)) { ++at; return true; } return false; }
    bool failed() const { return !error.empty(); }

    i32 fail(u32 pos, std::string msg) {
        if (error.empty()) { error = std::move(msg); errPos = pos; }
        return -1;
    }
    bool expect(const char* p) {
        if (accept(p)) return true;
        fail(peek().pos, std::string("esperava '") + p + "'" + describe_found());
        return false;
    }
    std::string describe_found() const {
        const Token& t = peek();
        if (t.kind == Tok::End) return " e a expressão terminou";
        if (t.kind == Tok::Num) return " e achou um número";
        if (t.kind == Tok::Str) return " e achou um texto";
        return " e achou '" + t.text + "'";
    }

    /// Sempre devolve um índice válido (quem chama escreve nele sem testar);
    /// passar do limite marca o erro e o programa inteiro é descartado.
    i32 node(N kind, u32 pos) {
        if (prog.nodes.size() >= kMaxNodes) fail(pos, "expressão grande demais");
        Node n;
        n.kind = kind;
        n.pos = pos;
        prog.nodes.push_back(n);
        return static_cast<i32>(prog.nodes.size() - 1);
    }
    Node& nd(i32 i) { return prog.nodes[static_cast<usize>(i)]; }
    u32 intern(const std::string& s) {
        for (u32 i = 0; i < prog.strings.size(); ++i) if (prog.strings[i] == s) return i;
        prog.strings.push_back(s);
        return static_cast<u32>(prog.strings.size() - 1);
    }
    void set_kids(i32 n, const std::vector<i32>& k) {
        nd(n).first = static_cast<u32>(prog.kids.size());
        nd(n).count = static_cast<u32>(k.size());
        prog.kids.insert(prog.kids.end(), k.begin(), k.end());
    }

    struct DepthGuard {
        Parser& p; bool ok;
        explicit DepthGuard(Parser& pp) : p(pp), ok(++pp.depth <= kMaxDepth) {
            if (!ok) p.fail(p.peek().pos, "aninhamento profundo demais");
        }
        ~DepthGuard() { --p.depth; }
    };

    // --- Instruções -----------------------------------------------------------
    i32 program() {
        const i32 blk = node(N::Block, 0);
        std::vector<i32> list;
        while (!failed() && peek().kind != Tok::End) {
            const i32 s = statement();
            if (s < 0) break;
            list.push_back(s);
        }
        if (failed()) return -1;
        set_kids(blk, list);
        return blk;
    }

    i32 statement() {
        DepthGuard g(*this);
        if (!g.ok) return -1;
        const Token& t = peek();
        if (is("{")) {
            ++at;
            const i32 blk = node(N::Block, t.pos);
            std::vector<i32> list;
            while (!failed() && !is("}")) {
                if (peek().kind == Tok::End) return fail(peek().pos, "bloco '{' sem fechar");
                const i32 s = statement();
                if (s < 0) return -1;
                list.push_back(s);
            }
            ++at;
            set_kids(blk, list);
            return blk;
        }
        if (is(";")) { ++at; return node(N::Empty, t.pos); }
        if (t.kind == Tok::Ident) {
            if (t.text == "var" || t.text == "let" || t.text == "const") {
                ++at;
                const i32 s = var_decl(t.pos);
                accept(";");
                return s;
            }
            if (t.text == "if") {
                ++at;
                const i32 n = node(N::If, t.pos);
                if (!expect("(")) return -1;
                const i32 c = expression(PAssign);
                if (c < 0 || !expect(")")) return -1;
                const i32 th = statement();
                if (th < 0) return -1;
                i32 el = -1;
                if (is_kw("else")) {
                    ++at;
                    el = statement();
                    if (el < 0) return -1;
                }
                nd(n).a = c; nd(n).b = th; nd(n).c = el;
                return n;
            }
            if (t.text == "while") {
                ++at;
                const i32 n = node(N::While, t.pos);
                if (!expect("(")) return -1;
                const i32 c = expression(PAssign);
                if (c < 0 || !expect(")")) return -1;
                const i32 body = statement();
                if (body < 0) return -1;
                nd(n).a = c; nd(n).b = body;
                return n;
            }
            if (t.text == "for") {
                ++at;
                const i32 n = node(N::For, t.pos);
                if (!expect("(")) return -1;
                i32 init = -1, cond = -1, step = -1;
                if (!is(";")) {
                    if (is_kw("var") || is_kw("let") || is_kw("const")) {
                        const u32 p = peek().pos;
                        ++at;
                        init = var_decl(p);
                    } else {
                        init = expression(PAssign);
                    }
                    if (init < 0) return -1;
                }
                if (!expect(";")) return -1;
                if (!is(";")) { cond = expression(PAssign); if (cond < 0) return -1; }
                if (!expect(";")) return -1;
                if (!is(")")) { step = expression(PAssign); if (step < 0) return -1; }
                if (!expect(")")) return -1;
                const i32 body = statement();
                if (body < 0) return -1;
                nd(n).a = init; nd(n).b = cond; nd(n).c = step; nd(n).d = body;
                return n;
            }
            if (t.text == "return") {
                ++at;
                const i32 n = node(N::Return, t.pos);
                if (!is(";") && !is("}") && peek().kind != Tok::End && !peek().nl) {
                    const i32 e = expression(PAssign);
                    if (e < 0) return -1;
                    nd(n).a = e;
                }
                accept(";");
                return n;
            }
            if (t.text == "break" || t.text == "continue") {
                ++at;
                accept(";");
                return node(t.text == "break" ? N::Break : N::Continue, t.pos);
            }
            if (t.text == "function" || t.text == "class" || t.text == "new" || t.text == "eval"
                || t.text == "import" || t.text == "require") {
                return fail(t.pos, "'" + t.text + "' não é suportado nas expressões do Aurea");
            }
        }
        const i32 e = expression(PAssign);
        if (e < 0) return -1;
        const i32 s = node(N::ExprStmt, t.pos);
        nd(s).a = e;
        if (!accept(";")) {
            // Sem ';', a próxima instrução precisa começar noutra linha ou num
            // fechamento — "1 2" é erro, como em JS.
            if (!peek().nl && !is("}") && !is_kw("else") && peek().kind != Tok::End) {
                return fail(peek().pos, "esperava ';' ou nova linha" + describe_found());
            }
        }
        return s;
    }

    i32 var_decl(u32 pos) {
        const i32 blk = node(N::Block, pos);
        std::vector<i32> list;
        do {
            const Token& name = peek();
            if (name.kind != Tok::Ident) return fail(name.pos, "esperava o nome da variável" + describe_found());
            ++at;
            const i32 d = node(N::VarDecl, name.pos);
            nd(d).sym = intern(name.text);
            if (accept("=")) {
                const i32 e = expression(PAssign);
                if (e < 0) return -1;
                nd(d).a = e;
            }
            list.push_back(d);
        } while (accept(","));
        set_kids(blk, list);
        return blk;
    }

    // --- Expressões -------------------------------------------------------------
    static int infix_prec(const Token& t) {
        if (t.kind != Tok::Punct) return 0;
        const std::string& s = t.text;
        if (s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=") return PAssign;
        if (s == "?") return PCond;
        if (s == "||") return POr;
        if (s == "&&") return PAnd;
        if (s == "==" || s == "!=" || s == "===" || s == "!==") return PEq;
        if (s == "<" || s == "<=" || s == ">" || s == ">=") return PCmp;
        if (s == "+" || s == "-") return PAdd;
        if (s == "*" || s == "/" || s == "%") return PMul;
        if (s == "^" || s == "**") return PPow;
        return 0;
    }

    i32 expression(int minPrec) {
        DepthGuard g(*this);
        if (!g.ok) return -1;
        i32 left = unary();
        if (left < 0) return -1;
        for (;;) {
            const Token& t = peek();
            const int prec = infix_prec(t);
            if (prec == 0 || prec < minPrec) break;
            ++at;
            const std::string& s = t.text;
            if (prec == PAssign) {
                if (nd(left).kind != N::Name) return fail(t.pos, "só dá para atribuir a uma variável");
                const i32 rhs = expression(PAssign);   // associa à direita
                if (rhs < 0) return -1;
                const i32 n = node(N::Assign, t.pos);
                nd(n).a = left; nd(n).b = rhs;
                nd(n).op = s == "=" ? OpSet : s == "+=" ? OpAdd : s == "-=" ? OpSub : s == "*=" ? OpMul : OpDiv;
                left = n;
                continue;
            }
            if (prec == PCond) {
                const i32 th = expression(PAssign);
                if (th < 0 || !expect(":")) return -1;
                const i32 el = expression(PCond);
                if (el < 0) return -1;
                const i32 n = node(N::Cond, t.pos);
                nd(n).a = left; nd(n).b = th; nd(n).c = el;
                left = n;
                continue;
            }
            // Potência associa à direita; o resto, à esquerda.
            const i32 rhs = expression(prec == PPow ? prec : prec + 1);
            if (rhs < 0) return -1;
            i32 n;
            if (s == "&&" || s == "||") {
                n = node(s == "&&" ? N::And : N::Or, t.pos);
            } else {
                n = node(N::Binary, t.pos);
                nd(n).op = s == "+" ? OpAdd : s == "-" ? OpSub : s == "*" ? OpMul : s == "/" ? OpDiv
                         : s == "%" ? OpMod : (s == "^" || s == "**") ? OpPow : s == "<" ? OpLt
                         : s == "<=" ? OpLe : s == ">" ? OpGt : s == ">=" ? OpGe
                         : (s == "==" || s == "===") ? OpEq : OpNe;
            }
            if (n < 0) return -1;
            nd(n).a = left; nd(n).b = rhs;
            left = n;
        }
        return left;
    }

    i32 unary() {
        DepthGuard g(*this);   // "----…1" encadeia prefixos sem passar por expression()
        if (!g.ok) return -1;
        const Token& t = peek();
        if (t.kind == Tok::Punct && (t.text == "-" || t.text == "+" || t.text == "!")) {
            ++at;
            // O operando liga mais forte que o sinal só na potência: -2^2 = -(2^2).
            const i32 e = expression(PPow);
            if (e < 0) return -1;
            const i32 n = node(N::Unary, t.pos);
            nd(n).op = t.text == "-" ? OpNeg : t.text == "+" ? OpPos : OpNot;
            nd(n).a = e;
            return n;
        }
        if (t.kind == Tok::Punct && (t.text == "++" || t.text == "--")) {
            ++at;
            const i32 e = unary();
            if (e < 0) return -1;
            if (nd(e).kind != N::Name) return fail(t.pos, "++/-- só em variável");
            const i32 n = node(N::IncDec, t.pos);
            nd(n).op = t.text == "++" ? OpInc : OpDec;
            nd(n).flag = 1;
            nd(n).a = e;
            return n;
        }
        return postfix();
    }

    i32 postfix() {
        i32 e = primary();
        if (e < 0) return -1;
        for (;;) {
            const Token& t = peek();
            if (t.kind != Tok::Punct) break;
            if (t.text == ".") {
                ++at;
                const Token& name = peek();
                if (name.kind != Tok::Ident) return fail(name.pos, "esperava um nome depois de '.'");
                ++at;
                const i32 n = node(N::Member, name.pos);
                nd(n).a = e;
                nd(n).sym = intern(name.text);
                e = n;
            } else if (t.text == "[" && !t.nl) {
                ++at;
                const i32 idx = expression(PAssign);
                if (idx < 0 || !expect("]")) return -1;
                const i32 n = node(N::Index, t.pos);
                nd(n).a = e; nd(n).b = idx;
                e = n;
            } else if (t.text == "(" && !t.nl) {
                ++at;
                std::vector<i32> args;
                if (!is(")")) {
                    do {
                        const i32 a = expression(PAssign);
                        if (a < 0) return -1;
                        args.push_back(a);
                        if (args.size() > 16) return fail(t.pos, "argumentos demais");
                    } while (accept(","));
                }
                if (!expect(")")) return -1;
                const i32 n = node(N::Call, t.pos);
                nd(n).a = e;
                set_kids(n, args);
                e = n;
            } else if ((t.text == "++" || t.text == "--") && !t.nl) {
                if (nd(e).kind != N::Name) return fail(t.pos, "++/-- só em variável");
                ++at;
                const i32 n = node(N::IncDec, t.pos);
                nd(n).op = t.text == "++" ? OpInc : OpDec;
                nd(n).a = e;
                e = n;
            } else {
                break;
            }
        }
        return e;
    }

    i32 primary() {
        DepthGuard g(*this);
        if (!g.ok) return -1;
        const Token& t = peek();
        switch (t.kind) {
            case Tok::Num: {
                ++at;
                const i32 n = node(N::Num, t.pos);
                if (n >= 0) nd(n).num = t.num;
                return n;
            }
            case Tok::Str: {
                ++at;
                const i32 n = node(N::Str, t.pos);
                if (n >= 0) nd(n).sym = intern(t.text);
                return n;
            }
            case Tok::Ident: {
                ++at;
                if (t.text == "true" || t.text == "false") {
                    const i32 n = node(N::Num, t.pos);
                    if (n >= 0) nd(n).num = t.text == "true" ? 1.0 : 0.0;
                    return n;
                }
                static constexpr const char* kReserved[] = {"var", "let", "const", "if", "else", "for", "while",
                                                           "return", "break", "continue", "function", "new"};
                for (const char* r : kReserved) {
                    if (t.text == r) return fail(t.pos, "'" + t.text + "' fora do lugar");
                }
                const i32 n = node(N::Name, t.pos);
                if (n >= 0) nd(n).sym = intern(t.text);
                return n;
            }
            case Tok::Punct: {
                if (t.text == "(") {
                    ++at;
                    const i32 e = expression(PAssign);
                    if (e < 0 || !expect(")")) return -1;
                    return e;
                }
                if (t.text == "[") {
                    ++at;
                    const i32 n = node(N::Array, t.pos);
                    std::vector<i32> elems;
                    if (!is("]")) {
                        do {
                            const i32 e = expression(PAssign);
                            if (e < 0) return -1;
                            elems.push_back(e);
                        } while (accept(","));
                    }
                    if (!expect("]")) return -1;
                    if (elems.empty() || elems.size() > 4) return fail(t.pos, "vetor precisa de 1 a 4 componentes");
                    set_kids(n, elems);
                    return n;
                }
                return fail(t.pos, "esperava um valor" + describe_found());
            }
            case Tok::End:
                return fail(t.pos, "a expressão terminou antes do esperado");
        }
        return fail(t.pos, "token inesperado");
    }
};

/// Nomes: variável declarada (var/let/const) ou atribuída vira slot local; o
/// resto precisa ser um global conhecido.
bool resolve_names(Program& p, std::string& error, u32& errPos) {
    std::vector<std::string>& locals = p.localNames;
    auto slot_of = [&](const std::string& s) -> i32 {
        for (u32 i = 0; i < locals.size(); ++i) if (locals[i] == s) return static_cast<i32>(i);
        return -1;
    };
    // 1) declarações explícitas (podem sombrear um global).
    for (const Node& n : p.nodes) {
        if (n.kind == N::VarDecl && slot_of(p.strings[n.sym]) < 0) locals.push_back(p.strings[n.sym]);
    }
    // 2) atribuição a nome não declarado: vira local — a menos que seja global.
    for (const Node& n : p.nodes) {
        if (n.kind != N::Assign && n.kind != N::IncDec) continue;
        const Node& target = p.nodes[static_cast<usize>(n.a)];
        const std::string& name = p.strings[target.sym];
        if (slot_of(name) >= 0) continue;
        if (find_global(name)) {
            error = "não dá para atribuir a '" + name + "'";
            errPos = target.pos;
            return false;
        }
        locals.push_back(name);
    }
    if (locals.size() > kMaxLocals) {
        error = "variáveis demais";
        errPos = 0;
        return false;
    }
    for (Node& n : p.nodes) {
        if (n.kind == N::VarDecl) {
            n.sym = static_cast<u32>(slot_of(p.strings[n.sym]));
        } else if (n.kind == N::Name) {
            const std::string& name = p.strings[n.sym];
            const i32 s = slot_of(name);
            if (s >= 0) {
                n.kind = N::Local;
                n.sym = static_cast<u32>(s);
            } else if (const u32 g = find_global(name)) {
                n.kind = N::Global;
                n.sym = g;
            } else {
                error = "nome desconhecido '" + name + "'";
                errPos = n.pos;
                return false;
            }
        }
    }
    return true;
}

void line_col(std::string_view src, u32 offset, u32& line, u32& col) {
    line = 1;
    col = 1;
    for (u32 i = 0; i < offset && i < src.size(); ++i) {
        if (src[i] == '\n') { ++line; col = 1; }
        else if ((static_cast<u8>(src[i]) & 0xC0) != 0x80) ++col;   // conta caractere, não byte UTF-8
    }
}

Diagnostic make_diag(std::string_view src, u32 offset, std::string msg) {
    Diagnostic d;
    d.ok = false;
    d.message = std::move(msg);
    d.offset = offset;
    line_col(src, offset, d.line, d.column);
    return d;
}

std::shared_ptr<const Program> compile_program(std::string_view source, Diagnostic& diag) {
    diag = Diagnostic{};
    if (source.size() > kMaxSourceBytes) {
        diag = make_diag(source, 0, "expressão longa demais");
        return nullptr;
    }
    Lexer lx{source, {}, {}, 0};
    if (!lx.run()) {
        diag = make_diag(source, lx.errPos, lx.error);
        return nullptr;
    }
    auto prog = std::make_shared<Program>();
    prog->source = std::string(source);
    Parser ps(lx.out, *prog);
    prog->root = ps.program();
    if (ps.failed() || prog->root < 0) {
        diag = make_diag(source, ps.errPos, ps.error.empty() ? "erro de sintaxe" : ps.error);
        return nullptr;
    }
    if (prog->nodes[static_cast<usize>(prog->root)].count == 0) {
        diag = make_diag(source, 0, "expressão vazia");
        return nullptr;
    }
    std::string err;
    u32 pos = 0;
    if (!resolve_names(*prog, err, pos)) {
        diag = make_diag(source, pos, err);
        return nullptr;
    }
    return prog;
}

// =============================================================================
// Valores
// =============================================================================
enum class K : u8 { Undef, Num, Vec, Str, Fn, Method, Layer, Comp, Transform, Effect, Prop, Key, Math };

struct Val {
    K   k = K::Undef;
    u8  n = 0;
    u32 id = 0;      ///< Fn/Method: id; Effect: índice do efeito na camada
    u32 a = 0, b = 0;
    f64 v[4]{};

    static Val num(f64 x) { Val r; r.k = K::Num; r.n = 1; r.v[0] = x; return r; }
    static Val vec(const f64* x, u32 count) {
        Val r;
        r.k = count == 1 ? K::Num : K::Vec;
        r.n = static_cast<u8>(std::clamp<u32>(count, 1, 4));
        for (u32 i = 0; i < r.n; ++i) r.v[i] = x[i];
        return r;
    }
};

/// Onde uma propriedade mora. `kind`: 0 = transform/câmera/luz (componentes
/// são propriedades consecutivas no enum), 1 = parâmetro de efeito (chave =
/// key0 + componente), 2 = trilha avulsa (time remap), 3 = animador de texto.
struct PropDesc {
    const Layer*  layer = nullptr;
    LayerId       id{};
    TrackProperty prop = TrackProperty::Opacity;
    u32           effectIndex = kInvalidIndex;
    u32           key0 = 0;
    u8            count = 1;
    u8            kind = 0;
    f64           scale = 1.0;    ///< unidade da interface ÷ unidade guardada
    const Track*  single = nullptr;
};

bool same_group(const PropDesc& a, const PropDesc& b) noexcept {
    return a.layer == b.layer && a.kind == b.kind && a.prop == b.prop && a.effectIndex == b.effectIndex
        && a.key0 == b.key0 && a.single == b.single;
}

// =============================================================================
// Estado por thread: escopos, pilha de avaliação (ciclos), memo.
// =============================================================================
struct Owner {
    const Composition* comp = nullptr;
    const Layer*       layer = nullptr;
    LayerId            id{};
};

struct MemoKey {
    const Track* t;
    i64          f;
    bool operator==(const MemoKey&) const noexcept = default;
};
struct MemoHash {
    usize operator()(const MemoKey& k) const noexcept {
        return std::hash<const void*>()(k.t) ^ (std::hash<i64>()(k.f) * 0x9E3779B97F4A7C15ull);
    }
};

struct ScopeData {
    const Timeline* tl = nullptr;
    bool built = false;
    std::unordered_map<const Track*, Owner> owners;
    std::unordered_map<MemoKey, f32, MemoHash> memo;

    void reset(const Timeline* t) {
        tl = t;
        built = false;
        owners.clear();
        memo.clear();
    }
    void build() {
        if (built || !tl) return;
        built = true;
        tl->for_each_composition([&](CompositionId, const Composition& c) {
            c.layers().for_each([&](LayerId id, const Layer& l) {
                for (u32 i = 0; i < l.tracks.size(); ++i) {
                    if (l.tracks.at(i).expression) owners[&l.tracks.at(i)] = Owner{&c, &l, id};
                }
                if (l.timeRemap.expression) owners[&l.timeRemap] = Owner{&c, &l, id};
            });
        });
    }
    Owner find(const Track* t) {
        build();
        const auto it = owners.find(t);
        return it == owners.end() ? Owner{} : it->second;
    }
};

struct StackEntry { const Track* track; Owner owner; };

struct Tls {
    std::vector<ScopeData*> scopes;
    std::vector<std::unique_ptr<ScopeData>> pool;
    u32 poolUsed = 0;
    StackEntry stack[kMaxRefDepth + 1];
    u32 depth = 0;
    bool cycle = false;
    std::string cycleMsg;

    ScopeData* acquire(const Timeline* tl) {
        if (poolUsed == pool.size()) pool.push_back(std::make_unique<ScopeData>());
        ScopeData* s = pool[poolUsed++].get();
        s->reset(tl);
        return s;
    }
    void release() { if (poolUsed) --poolUsed; }
};

thread_local Tls g_tls;

struct Provider { TimelineProvider fn; void* ctx; };
std::mutex g_providerMutex;
std::vector<Provider> g_providers;

// =============================================================================
// Propriedades do modelo
// =============================================================================
const char* prop_label(const PropDesc& d) noexcept {
    if (d.kind == 1) return "parâmetro de efeito";
    if (d.kind == 2) return "remapear tempo";
    if (d.kind == 3) return "animador de texto";
    switch (d.prop) {
        case TrackProperty::PositionX: return "posição";
        case TrackProperty::ScaleX: return "escala";
        case TrackProperty::AnchorX: return "âncora";
        case TrackProperty::RotationX: return "rotação X";
        case TrackProperty::RotationY: return "rotação Y";
        case TrackProperty::RotationZ: return "rotação";
        case TrackProperty::Opacity: return "opacidade";
        case TrackProperty::AudioVolume: return "volume";
        default: return "propriedade";
    }
}

f32 text_anim_base(const TextAnimator& a, u32 p) noexcept {
    switch (p) {
        case text::kSelStart: return a.selector.start;
        case text::kSelEnd: return a.selector.end;
        case text::kSelOffset: return a.selector.offset;
        case text::kSelAmount: return a.selector.amount;
        case text::kSelEaseHigh: return a.selector.easeHigh;
        case text::kSelEaseLow: return a.selector.easeLow;
        case text::kWiggleRate: return a.selector.wiggleRate;
        case text::kPosX: return a.position.x;
        case text::kPosY: return a.position.y;
        case text::kPosZ: return a.position.z;
        case text::kScaleX: return a.scale.x;
        case text::kScaleY: return a.scale.y;
        case text::kRotX: return a.rotation.x;
        case text::kRotY: return a.rotation.y;
        case text::kRotZ: return a.rotation.z;
        case text::kOpacity: return a.opacity;
        case text::kTracking: return a.tracking;
        case text::kBlur: return a.blur;
        case text::kSkew: return a.skew;
        case text::kStrokeWidth: return a.strokeWidth;
        case text::kCharOffset: return a.charOffset;
        default: return 0.0f;
    }
}

const Track* find_track(const PropDesc& d, u32 c) noexcept {
    if (!d.layer) return d.single;
    switch (d.kind) {
        case 0: return d.layer->tracks.find(static_cast<TrackProperty>(static_cast<u16>(d.prop) + c));
        case 1: return d.layer->tracks.find(TrackProperty::EffectParam, d.effectIndex, d.key0 + c);
        case 2: return d.single;
        case 3: return d.layer->tracks.find(TrackProperty::TextAnimParam, d.effectIndex, d.key0 + c);
        default: return nullptr;
    }
}

/// Valor parado (sem keyframe), na unidade GUARDADA: o campo da camada que os
/// leitores usam como `fallback`.
f64 static_base(const PropDesc& d, u32 c) noexcept {
    const Layer* l = d.layer;
    const Track* tr = find_track(d, c);
    if (!l) return tr ? tr->staticValue : 0.0;
    if (d.kind == 1) {
        for (const EffectInstance& e : l->effects) {
            if (e.id != d.effectIndex) continue;
            const u32 pi = (d.key0 + c) / 4, comp = (d.key0 + c) % 4;
            return pi < e.params.size() ? e.params[pi].constant.v[comp] : 0.0;
        }
        return tr ? tr->staticValue : 0.0;
    }
    if (d.kind == 3) {
        return d.effectIndex < l->text.animators.size() ? text_anim_base(l->text.animators[d.effectIndex], d.key0 + c) : 0.0;
    }
    if (d.kind == 2) return tr ? tr->staticValue : 0.0;
    const Transform& tf = l->transform;
    using TP = TrackProperty;
    switch (static_cast<TP>(static_cast<u16>(d.prop) + c)) {
        case TP::PositionX: return tf.position.x;
        case TP::PositionY: return tf.position.y;
        case TP::PositionZ: return tf.position.z;
        case TP::ScaleX: return tf.scale.x;
        case TP::ScaleY: return tf.scale.y;
        case TP::ScaleZ: return tf.scale.z;
        case TP::RotationX: return tf.rotation.x;
        case TP::RotationY: return tf.rotation.y;
        case TP::RotationZ: return tf.rotation.z;
        case TP::AnchorX: return tf.anchor.x;
        case TP::AnchorY: return tf.anchor.y;
        case TP::AnchorZ: return tf.anchor.z;
        case TP::Opacity: return tf.opacity;
        case TP::SkewX: return tf.skewX;
        case TP::SkewY: return tf.skewY;
        case TP::Fov: return l->camera.fov;
        case TP::FocalLength: return l->camera.focalLength;
        case TP::FocusDistance: return l->camera.focusDistance;
        case TP::Aperture: return l->camera.aperture;
        case TP::NearPlane: return l->camera.nearPlane;
        case TP::FarPlane: return l->camera.farPlane;
        case TP::LightIntensity: return l->light.intensity;
        case TP::LightColorR: return l->light.color.x;
        case TP::LightColorG: return l->light.color.y;
        case TP::LightColorB: return l->light.color.z;
        case TP::LightConeAngle: return l->light.coneAngle;
        case TP::LightPenumbra: return l->light.penumbra;
        case TP::TextTracking: return l->text.tracking;
        case TP::AudioVolume: return tr ? tr->staticValue : 1.0;
        default: return tr ? tr->staticValue : 0.0;
    }
}

/// Descritor da propriedade (grupo) que contém a track `t` da camada `l`.
/// `outComponent` = qual componente do grupo a track é.
PropDesc desc_for_track(const Layer* l, LayerId id, const Track& t, f64 fps, u32& outComponent) noexcept {
    PropDesc d;
    d.layer = l;
    d.id = id;
    outComponent = 0;
    if (l && &t == &l->timeRemap) {
        d.kind = 2;
        d.single = &t;
        d.scale = 1.0 / (fps > 0.0 ? fps : 30.0);   // quadros da fonte → segundos
        return d;
    }
    if (!l) { d.kind = 2; d.single = &t; return d; }
    using TP = TrackProperty;
    const u16 p = static_cast<u16>(t.property);
    auto group3 = [&](TP base) {
        const u32 c = p - static_cast<u16>(base);
        d.prop = base;
        outComponent = c;
        d.count = static_cast<u8>(std::max<u32>(l->threeD ? 3u : 2u, c + 1));
    };
    switch (t.property) {
        case TP::PositionX: case TP::PositionY: case TP::PositionZ: group3(TP::PositionX); break;
        case TP::ScaleX: case TP::ScaleY: case TP::ScaleZ: group3(TP::ScaleX); d.scale = 100.0; break;
        case TP::AnchorX: case TP::AnchorY: case TP::AnchorZ: group3(TP::AnchorX); break;
        case TP::LightColorR: case TP::LightColorG: case TP::LightColorB:
            d.prop = TP::LightColorR;
            outComponent = p - static_cast<u16>(TP::LightColorR);
            d.count = 3;
            break;
        case TP::Opacity: d.prop = t.property; d.scale = 100.0; break;
        case TP::AudioVolume: d.prop = t.property; d.scale = 100.0; break;
        case TP::EffectParam: {
            d.kind = 1;
            d.effectIndex = t.effectIndex;
            const u32 pi = t.effectParamIndex / 4;
            outComponent = t.effectParamIndex % 4;
            d.key0 = pi * 4;
            u32 comps = outComponent + 1;
            for (const EffectInstance& e : l->effects) {
                if (e.id != t.effectIndex) continue;
                if (const ParameterRegistry* reg = builtin_effects().params(e.type); reg && pi < reg->count()) {
                    comps = std::max(comps, component_count(reg->at(pi).type));
                }
            }
            d.count = static_cast<u8>(std::clamp<u32>(comps, 1, 4));
            break;
        }
        case TP::TextAnimParam:
            d.kind = 3;
            d.effectIndex = t.effectIndex;
            d.key0 = t.effectParamIndex;
            break;
        default: d.prop = t.property; break;
    }
    return d;
}

PropDesc transform_desc(const Layer* l, LayerId id, TrackProperty base) noexcept {
    PropDesc d;
    d.layer = l;
    d.id = id;
    d.prop = base;
    using TP = TrackProperty;
    if (base == TP::PositionX || base == TP::ScaleX || base == TP::AnchorX) d.count = (l && l->threeD) ? 3 : 2;
    if (base == TP::ScaleX || base == TP::Opacity) d.scale = 100.0;
    return d;
}

bool name_eq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i) {
        const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
        const char y = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
        if (x != y) return false;
    }
    return true;
}

/// Nome em inglês dos controles (compatível com expressões do After Effects).
struct ControlAlias { const char* key; const char* effect; const char* param; };
constexpr ControlAlias kControlAliases[] = {
    {"aurea.control.slider", "Slider Control", "Slider"},
    {"aurea.control.angle", "Angle Control", "Angle"},
    {"aurea.control.checkbox", "Checkbox Control", "Checkbox"},
    {"aurea.control.color", "Color Control", "Color"},
    {"aurea.control.point", "Point Control", "Point"},
};

bool effect_matches(const EffectInstance& e, std::string_view name) noexcept {
    const Effect* fx = builtin_effects().find(e.type);
    if (!fx) return false;
    if (name_eq(name, fx->info().name) || name_eq(name, fx->info().key)) return true;
    for (const ControlAlias& a : kControlAliases) {
        if (std::string_view(fx->info().key) == a.key && name_eq(name, a.effect)) return true;
    }
    return false;
}

// =============================================================================
// Ruído e aleatório (determinísticos)
// =============================================================================
u32 hash32(u32 x) noexcept {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
u32 hash_mix(u32 a, u32 b) noexcept { return hash32(a ^ (hash32(b) + 0x9E3779B9u + (a << 6) + (a >> 2))); }

f64 lattice(u32 seed, i64 i) noexcept {
    const u32 h = hash_mix(seed, static_cast<u32>(i) ^ static_cast<u32>(static_cast<u64>(i) >> 32) * 0x85EBCA6Bu);
    return static_cast<f64>(h) / 4294967295.0 * 2.0 - 1.0;
}

/// Ruído de gradiente 1D em [-1, 1] (prova: |g0·f·(1−w) + g1·(f−1)·w| ≤ ½ com
/// |g| ≤ 1; ×2 dá o intervalo cheio).
f64 noise1(u32 seed, f64 x) noexcept {
    const f64 fl = std::floor(x);
    const i64 i = static_cast<i64>(fl);
    const f64 f = x - fl;
    const f64 g0 = lattice(seed, i) * f;
    const f64 g1 = lattice(seed, i + 1) * (f - 1.0);
    const f64 w = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    return 2.0 * (g0 + (g1 - g0) * w);
}

/// Fractal (oitavas) normalizado pela soma dos pesos: continua em [-1, 1].
f64 fractal(u32 seed, f64 x, u32 octaves, f64 ampMult) noexcept {
    f64 sum = 0.0, w = 1.0, norm = 0.0, fm = 1.0;
    for (u32 o = 0; o < octaves; ++o) {
        sum += w * noise1(hash_mix(seed, o * 1013u + 7u), x * fm + static_cast<f64>(o) * 17.13);
        norm += std::fabs(w);
        w *= ampMult;
        fm *= 2.0;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

/// Ruído 3D de gradiente (Perlin melhorado, 12 direções), limitado a [-1, 1].
f64 noise3(f64 x, f64 y, f64 z) noexcept {
    auto grad = [](u32 h, f64 gx, f64 gy, f64 gz) {
        switch (h % 12u) {
            case 0: return gx + gy;  case 1: return -gx + gy; case 2: return gx - gy;  case 3: return -gx - gy;
            case 4: return gx + gz;  case 5: return -gx + gz; case 6: return gx - gz;  case 7: return -gx - gz;
            case 8: return gy + gz;  case 9: return -gy + gz; case 10: return gy - gz; default: return -gy - gz;
        }
    };
    auto fade = [](f64 t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };
    const f64 fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const i32 ix = static_cast<i32>(fx), iy = static_cast<i32>(fy), iz = static_cast<i32>(fz);
    const f64 dx = x - fx, dy = y - fy, dz = z - fz;
    auto h = [&](i32 a, i32 b, i32 c) {
        return hash_mix(hash_mix(static_cast<u32>(a), static_cast<u32>(b)), static_cast<u32>(c));
    };
    const f64 u = fade(dx), v = fade(dy), w = fade(dz);
    auto lerp = [](f64 a, f64 b, f64 t) { return a + (b - a) * t; };
    const f64 r = lerp(lerp(lerp(grad(h(ix, iy, iz), dx, dy, dz), grad(h(ix + 1, iy, iz), dx - 1, dy, dz), u),
                            lerp(grad(h(ix, iy + 1, iz), dx, dy - 1, dz), grad(h(ix + 1, iy + 1, iz), dx - 1, dy - 1, dz), u), v),
                       lerp(lerp(grad(h(ix, iy, iz + 1), dx, dy, dz - 1), grad(h(ix + 1, iy, iz + 1), dx - 1, dy, dz - 1), u),
                            lerp(grad(h(ix, iy + 1, iz + 1), dx, dy - 1, dz - 1),
                                 grad(h(ix + 1, iy + 1, iz + 1), dx - 1, dy - 1, dz - 1), u), v), w);
    return std::clamp(r, -1.0, 1.0);
}

// =============================================================================
// Ambiente de uma avaliação
// =============================================================================
struct Env {
    const Composition* comp = nullptr;
    const Layer*       layer = nullptr;
    LayerId            layerId{};
    const Track*       track = nullptr;
    PropDesc           self;
    u32                component = 0;
    f64                fps = 30.0;
    f64                localF = 0.0;    ///< quadro local da camada dona
    f64                compF = 0.0;     ///< quadro da composição
    Val                value;           ///< valor pré-expressão (unidade da interface)
    u32                seedBase = 1;    ///< camada + grupo da propriedade
    const f32*         fallback = nullptr;
};

f64 layer_local_from_comp(const Layer* l, f64 compF) noexcept {
    return l ? compF - static_cast<f64>(l->start.value) + static_cast<f64>(l->offset.value) : compF;
}

// =============================================================================
// Avaliador
// =============================================================================
enum class Flow : u8 { None, Return, Break, Continue };

struct Ctx {
    const Program* p = nullptr;
    const Env*     env = nullptr;
    u32            steps = 0;
    bool           failed = false;
    std::string    err;
    u32            errPos = 0;
    Flow           flow = Flow::None;
    Val            ret;
    Val            locals[kMaxLocals];
    bool           defined[kMaxLocals]{};
    std::vector<PropDesc>    props;
    std::vector<Val>         recv;
    std::vector<std::string> rtStrings;
    u64            rng = 0;
    bool           rngTimeless = false;
    u32            rngUserSeed = 0;

    void reset(const Program* prog, const Env* e) {
        p = prog;
        env = e;
        steps = 0;
        failed = false;
        err.clear();
        errPos = 0;
        flow = Flow::None;
        ret = Val{};
        const usize nl = prog ? prog->localNames.size() : 0;
        for (usize i = 0; i < nl; ++i) { locals[i] = Val{}; defined[i] = false; }
        props.clear();
        recv.clear();
        rtStrings.clear();
        rngTimeless = false;
        rngUserSeed = 0;
        seed_rng();
    }

    Val fail(u32 pos, std::string msg) {
        if (!failed) { failed = true; err = std::move(msg); errPos = pos; }
        return Val{};
    }

    // --- aleatório: xorshift64* semeado por camada/propriedade/quadro ---------
    void seed_rng() {
        const i64 f = rngTimeless ? 0 : static_cast<i64>(std::floor(env ? env->compF : 0.0));
        u64 s = (static_cast<u64>(hash_mix(env ? env->seedBase : 1u, rngUserSeed)) << 32)
              ^ hash_mix(static_cast<u32>(f), static_cast<u32>(static_cast<u64>(f) >> 32) ^ 0xA511E9B3u);
        rng = s ? s : 0x9E3779B97F4A7C15ull;
        for (int i = 0; i < 4; ++i) (void)next_u64();
    }
    u64 next_u64() {
        rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
        return rng * 0x2545F4914F6CDD1Dull;
    }
    f64 next01() { return static_cast<f64>(next_u64() >> 11) * (1.0 / 9007199254740992.0); }

    // --- helpers de tipo -----------------------------------------------------
    std::string_view str(const Val& v) const {
        if (v.k != K::Str) return {};
        return v.b ? std::string_view(rtStrings[v.a]) : std::string_view(p->strings[v.a]);
    }
    Val make_str(std::string s) {
        rtStrings.push_back(std::move(s));
        Val r; r.k = K::Str; r.a = static_cast<u32>(rtStrings.size() - 1); r.b = 1;
        return r;
    }
    Val make_prop(const PropDesc& d) {
        props.push_back(d);
        Val r; r.k = K::Prop; r.a = static_cast<u32>(props.size() - 1);
        return r;
    }
    Val make_method(u32 id, const Val& receiver) {
        recv.push_back(receiver);
        Val r; r.k = K::Method; r.id = id; r.a = static_cast<u32>(recv.size() - 1);
        return r;
    }
    Val make_layer(const Layer* l, LayerId id) {
        (void)l;
        Val r; r.k = K::Layer; r.a = id.index; r.b = id.generation;
        return r;
    }
    const Layer* layer_of(const Val& v) const {
        if (!env->comp) return nullptr;
        return env->comp->layer(LayerId{v.a, v.b});
    }

    // --- leitura de propriedade ----------------------------------------------
    /// Componente `c` da propriedade no quadro local fracionário `localF`, na
    /// unidade da interface. `post` = com a expressão da track (a de OUTRA
    /// propriedade); o grupo da própria propriedade sempre lê o valor
    /// pré-expressão (como no After Effects).
    f64 component(const PropDesc& d, u32 c, f64 localF, bool post) {
        if (!d.layer && !d.single) return c < env->value.n ? env->value.v[c] : 0.0;   // avaliação isolada
        const Track* tr = find_track(d, c);
        const bool own = same_group(d, env->self) && env->layer;
        f64 base = static_base(d, c);
        if (own && c == env->component && env->fallback && tr && tr->keys.empty()) base = *env->fallback;
        if (!tr) return base * d.scale;
        const f64 fl = std::floor(localF);
        const f64 k = localF - fl;
        auto at = [&](i64 f) -> f64 {
            if (post && !own && tr->has_expression()) return tr->value_or(FrameIndex{f}, static_cast<f32>(base));
            return tr->keys.empty() ? base : static_cast<f64>(tr->sample_keys(FrameIndex{f}));
        };
        const f64 a = at(static_cast<i64>(fl));
        const f64 v = k > 1e-9 ? a + (at(static_cast<i64>(fl) + 1) - a) * k : a;
        return v * d.scale;
    }

    Val prop_value(const PropDesc& d, f64 compF, bool post, u32 pos) {
        f64 out[4]{};
        const f64 local = layer_local_from_comp(d.layer, compF);
        for (u32 c = 0; c < d.count; ++c) out[c] = component(d, c, local, post);
        if (g_tls.cycle) return fail(pos, g_tls.cycleMsg.empty() ? "dependência circular" : g_tls.cycleMsg);
        return Val::vec(out, d.count);
    }

    Val prop_velocity(const PropDesc& d, f64 compF, u32 pos) {
        const Val a = prop_value(d, compF - 1.0, true, pos);
        const Val b = prop_value(d, compF + 1.0, true, pos);
        if (failed) return Val{};
        f64 out[4]{};
        for (u32 c = 0; c < a.n; ++c) out[c] = (b.v[c] - a.v[c]) * env->fps * 0.5;
        return Val::vec(out, a.n);
    }

    /// Coerção: propriedade vira o valor no instante atual.
    Val deref(const Val& v, u32 pos) {
        if (v.k == K::Prop) return prop_value(props[v.a], env->compF, true, pos);
        return v;
    }

    bool truthy(const Val& v) const {
        switch (v.k) {
            case K::Num: return v.v[0] != 0.0 && !std::isnan(v.v[0]);
            case K::Str: return !str(v).empty();
            case K::Undef: return false;
            default: return true;
        }
    }

    bool want_num(const Val& v, f64& out, u32 pos, const char* what) {
        if (v.k == K::Num) { out = v.v[0]; return true; }
        fail(pos, std::string(what) + " precisa ser um número");
        return false;
    }
    bool want_numvec(const Val& v, u32 pos, const char* what) {
        if (v.k == K::Num || v.k == K::Vec) return true;
        fail(pos, std::string(what) + " precisa ser número ou vetor");
        return false;
    }

    // --- aritmética ------------------------------------------------------------
    Val arith(u8 op, const Val& x, const Val& y, u32 pos) {
        if (x.k == K::Str || y.k == K::Str) {
            if (op == OpEq || op == OpNe) {
                const bool eq = x.k == y.k && str(x) == str(y);
                return Val::num((op == OpEq) == eq ? 1.0 : 0.0);
            }
            return fail(pos, "texto não entra em conta");
        }
        if (!want_numvec(x, pos, "operando") || !want_numvec(y, pos, "operando")) return Val{};
        if (x.k == K::Num && y.k == K::Num) {
            const f64 a = x.v[0], b = y.v[0];
            switch (op) {
                case OpAdd: return Val::num(a + b);
                case OpSub: return Val::num(a - b);
                case OpMul: return Val::num(a * b);
                case OpDiv: return Val::num(a / b);
                case OpMod: return Val::num(std::fmod(a, b));
                case OpPow: return Val::num(std::pow(a, b));
                case OpLt: return Val::num(a < b ? 1.0 : 0.0);
                case OpLe: return Val::num(a <= b ? 1.0 : 0.0);
                case OpGt: return Val::num(a > b ? 1.0 : 0.0);
                case OpGe: return Val::num(a >= b ? 1.0 : 0.0);
                case OpEq: return Val::num(a == b ? 1.0 : 0.0);
                case OpNe: return Val::num(a != b ? 1.0 : 0.0);
                default: return fail(pos, "operador inválido");
            }
        }
        if (op == OpEq || op == OpNe) {
            bool eq = x.n == y.n;
            for (u32 i = 0; eq && i < x.n; ++i) eq = x.v[i] == y.v[i];
            return Val::num((op == OpEq) == eq ? 1.0 : 0.0);
        }
        if (op >= OpLt && op <= OpGe) return fail(pos, "comparação (<, >) só entre números");
        // Vetor: por componente. Soma/subtração completam o menor com zero
        // ([1,2,3] + [10,0] = [11,2,3], como no After Effects); número com
        // vetor se espalha em todos os componentes.
        const u32 n = std::max(x.n, y.n);
        f64 out[4]{};
        for (u32 i = 0; i < n; ++i) {
            const f64 a = x.k == K::Num ? x.v[0] : (i < x.n ? x.v[i] : 0.0);
            const f64 b = y.k == K::Num ? y.v[0] : (i < y.n ? y.v[i] : 0.0);
            switch (op) {
                case OpAdd: out[i] = a + b; break;
                case OpSub: out[i] = a - b; break;
                case OpMul: out[i] = a * b; break;
                case OpDiv: out[i] = a / b; break;
                case OpMod: out[i] = std::fmod(a, b); break;
                case OpPow: out[i] = std::pow(a, b); break;
                default: return fail(pos, "operador inválido");
            }
        }
        Val r = Val::vec(out, n);
        r.k = K::Vec;   // vetor de 1 componente continua vetor
        return r;
    }

    // --- nós ------------------------------------------------------------------
    const Node& N_(i32 i) const { return p->nodes[static_cast<usize>(i)]; }

    bool tick(u32 pos) {
        if (++steps > kMaxInstructions) {
            fail(pos, "limite de " + std::to_string(kMaxInstructions) + " instruções por quadro excedido (laço infinito?)");
            return false;
        }
        return true;
    }

    Val exec(i32 ni, u32 depth);
    Val eval(i32 ni, u32 depth);
    Val call(const Node& n, u32 depth);
    Val member(const Val& obj, std::string_view name, u32 pos);
    Val global(u32 id, u32 pos);
    Val call_fn(u32 id, const Val* args, u32 argc, u32 pos);
    Val call_method(u32 id, const Val& recv, const Val* args, u32 argc, u32 pos);
    Val loop(bool out, const Val* args, u32 argc, bool duration, u32 pos);
    Val lerp_fn(u32 id, const Val* args, u32 argc, u32 pos);
    Val key_of(const PropDesc& d, u32 propIndex, f64 idx, u32 pos);
};

Val Ctx::exec(i32 ni, u32 depth) {
    if (failed || !tick(N_(ni).pos)) return Val{};
    if (depth > kMaxDepth * 2) return fail(N_(ni).pos, "aninhamento profundo demais");
    const Node& n = N_(ni);
    switch (n.kind) {
        case N::Block: {
            Val last;
            for (u32 i = 0; i < n.count; ++i) {
                const Val v = exec(p->kids[n.first + i], depth + 1);
                if (failed) return Val{};
                if (v.k != K::Undef) last = v;
                if (flow != Flow::None) break;
            }
            return last;
        }
        case N::VarDecl: {
            if (n.a >= 0) {
                Val v = eval(n.a, depth + 1);
                if (failed) return Val{};
                locals[n.sym] = v;
            }
            defined[n.sym] = true;
            return Val{};
        }
        case N::If: {
            const Val c = eval(n.a, depth + 1);
            if (failed) return Val{};
            if (truthy(deref(c, n.pos))) return exec(n.b, depth + 1);
            if (n.c >= 0) return exec(n.c, depth + 1);
            return Val{};
        }
        case N::While:
        case N::For: {
            const bool isFor = n.kind == N::For;
            const i32 cond = isFor ? n.b : n.a;
            const i32 body = isFor ? n.d : n.b;
            if (isFor && n.a >= 0) {
                if (N_(n.a).kind == N::Block) (void)exec(n.a, depth + 1);
                else (void)eval(n.a, depth + 1);
                if (failed) return Val{};
            }
            Val last;
            for (;;) {
                if (!tick(n.pos)) return Val{};
                if (cond >= 0) {
                    const Val c = eval(cond, depth + 1);
                    if (failed) return Val{};
                    if (!truthy(deref(c, n.pos))) break;
                }
                const Val v = exec(body, depth + 1);
                if (failed) return Val{};
                if (v.k != K::Undef) last = v;
                if (flow == Flow::Break) { flow = Flow::None; break; }
                if (flow == Flow::Return) return last;
                if (flow == Flow::Continue) flow = Flow::None;
                if (isFor && n.c >= 0) {
                    (void)eval(n.c, depth + 1);
                    if (failed) return Val{};
                }
            }
            return last;
        }
        case N::Return: {
            ret = n.a >= 0 ? deref(eval(n.a, depth + 1), n.pos) : Val{};
            flow = Flow::Return;
            return ret;
        }
        case N::Break: flow = Flow::Break; return Val{};
        case N::Continue: flow = Flow::Continue; return Val{};
        case N::ExprStmt: return eval(n.a, depth + 1);
        case N::Empty: return Val{};
        default: return eval(ni, depth + 1);
    }
}

Val Ctx::eval(i32 ni, u32 depth) {
    if (failed || !tick(N_(ni).pos)) return Val{};
    if (depth > kMaxDepth * 2) return fail(N_(ni).pos, "aninhamento profundo demais");
    const Node& n = N_(ni);
    switch (n.kind) {
        case N::Num: return Val::num(n.num);
        case N::Str: { Val r; r.k = K::Str; r.a = n.sym; return r; }
        case N::Local:
            if (!defined[n.sym]) return fail(n.pos, "variável '" + p->localNames[n.sym] + "' usada antes de receber valor");
            return locals[n.sym];
        case N::Global: return global(n.sym, n.pos);
        case N::Array: {
            f64 out[4]{};
            for (u32 i = 0; i < n.count; ++i) {
                const Val e = deref(eval(p->kids[n.first + i], depth + 1), n.pos);
                if (failed) return Val{};
                if (e.k != K::Num) return fail(N_(p->kids[n.first + i]).pos, "componente de vetor precisa ser número");
                out[i] = e.v[0];
            }
            Val r = Val::vec(out, n.count);
            r.k = K::Vec;
            return r;
        }
        case N::Unary: {
            const Val x = deref(eval(n.a, depth + 1), n.pos);
            if (failed) return Val{};
            if (n.op == OpNot) return Val::num(truthy(x) ? 0.0 : 1.0);
            if (!want_numvec(x, n.pos, "operando")) return Val{};
            if (n.op == OpPos) return x;
            Val r = x;
            for (u32 i = 0; i < r.n; ++i) r.v[i] = -r.v[i];
            return r;
        }
        case N::Binary: {
            const Val x = deref(eval(n.a, depth + 1), n.pos);
            if (failed) return Val{};
            const Val y = deref(eval(n.b, depth + 1), n.pos);
            if (failed) return Val{};
            return arith(n.op, x, y, n.pos);
        }
        case N::And: {
            const Val x = deref(eval(n.a, depth + 1), n.pos);
            if (failed || !truthy(x)) return x;
            return deref(eval(n.b, depth + 1), n.pos);
        }
        case N::Or: {
            const Val x = deref(eval(n.a, depth + 1), n.pos);
            if (failed || truthy(x)) return x;
            return deref(eval(n.b, depth + 1), n.pos);
        }
        case N::Cond: {
            const Val c = deref(eval(n.a, depth + 1), n.pos);
            if (failed) return Val{};
            return eval(truthy(c) ? n.b : n.c, depth + 1);
        }
        case N::Assign: {
            const u32 slot = N_(n.a).sym;
            Val v = deref(eval(n.b, depth + 1), n.pos);
            if (failed) return Val{};
            if (n.op != OpSet) {
                if (!defined[slot]) return fail(n.pos, "variável '" + p->localNames[slot] + "' usada antes de receber valor");
                v = arith(n.op, locals[slot], v, n.pos);
                if (failed) return Val{};
            }
            locals[slot] = v;
            defined[slot] = true;
            return v;
        }
        case N::IncDec: {
            const u32 slot = N_(n.a).sym;
            if (!defined[slot]) return fail(n.pos, "variável '" + p->localNames[slot] + "' usada antes de receber valor");
            const Val old = locals[slot];
            const Val nv = arith(n.op == OpInc ? OpAdd : OpSub, old, Val::num(1.0), n.pos);
            if (failed) return Val{};
            locals[slot] = nv;
            return n.flag ? nv : old;
        }
        case N::Member: {
            const Val obj = eval(n.a, depth + 1);
            if (failed) return Val{};
            return member(obj, p->strings[n.sym], n.pos);
        }
        case N::Index: {
            const Val obj = deref(eval(n.a, depth + 1), n.pos);
            if (failed) return Val{};
            const Val idx = deref(eval(n.b, depth + 1), n.pos);
            if (failed) return Val{};
            f64 i = 0;
            if (!want_num(idx, i, n.pos, "índice")) return Val{};
            if (obj.k == K::Num) {
                if (i == 0.0) return obj;
                return fail(n.pos, "índice fora do vetor");
            }
            if (obj.k != K::Vec) return fail(n.pos, "só dá para indexar vetor");
            const i64 k = static_cast<i64>(std::floor(i));
            if (k < 0 || k >= obj.n) return fail(n.pos, "índice " + std::to_string(k) + " fora do vetor de " + std::to_string(obj.n));
            return Val::num(obj.v[k]);
        }
        case N::Call: return call(n, depth);
        default: return exec(ni, depth + 1);
    }
}

Val Ctx::call(const Node& n, u32 depth) {
    const Val callee = eval(n.a, depth + 1);
    if (failed) return Val{};
    Val args[16];
    for (u32 i = 0; i < n.count; ++i) {
        args[i] = eval(p->kids[n.first + i], depth + 1);
        if (failed) return Val{};
        // Texto e objetos passam como são; propriedades viram valor (exceto
        // para quem pede a propriedade em si — nenhuma função daqui pede).
        args[i] = deref(args[i], n.pos);
        if (failed) return Val{};
    }
    if (callee.k == K::Fn) return call_fn(callee.id, args, n.count, n.pos);
    if (callee.k == K::Method) return call_method(callee.id, recv[callee.a], args, n.count, n.pos);
    if (callee.k == K::Effect) return call_method(MEffectParam, callee, args, n.count, n.pos);
    return fail(n.pos, "isto não é uma função");
}

Val Ctx::global(u32 id, u32 pos) {
    const Env& e = *env;
    if (id >= FWiggle && id < MCompLayer) { Val r; r.k = K::Fn; r.id = id; return r; }
    switch (id) {
        case GTime: return Val::num(e.compF / e.fps);
        case GValue: return e.value;
        case GFps: return Val::num(e.fps);
        case GFrame: return Val::num(std::floor(e.compF));
        case GMath: { Val r; r.k = K::Math; return r; }
        case GThisComp:
            if (!e.comp) return fail(pos, "sem composição (expressão fora de uma camada)");
            { Val r; r.k = K::Comp; return r; }
        default: break;
    }
    if (!e.layer) return fail(pos, "sem camada (expressão fora de uma camada)");
    switch (id) {
        case GIndex: {
            const i32 z = e.comp ? e.comp->z_index_of(e.layerId) : -1;
            return Val::num(z < 0 ? 1.0 : static_cast<f64>(e.comp->order().size() - static_cast<u32>(z)));
        }
        case GThisLayer: return make_layer(e.layer, e.layerId);
        case GThisProperty: return make_prop(e.self);
        case GTransform: { Val r; r.k = K::Transform; r.a = e.layerId.index; r.b = e.layerId.generation; return r; }
        case GPosition: return make_prop(transform_desc(e.layer, e.layerId, TrackProperty::PositionX));
        case GScale: return make_prop(transform_desc(e.layer, e.layerId, TrackProperty::ScaleX));
        case GRotation: return make_prop(transform_desc(e.layer, e.layerId, TrackProperty::RotationZ));
        case GOpacity: return make_prop(transform_desc(e.layer, e.layerId, TrackProperty::Opacity));
        case GAnchor: return make_prop(transform_desc(e.layer, e.layerId, TrackProperty::AnchorX));
        case GVelocity: return prop_velocity(e.self, e.compF, pos);
        case GSpeed: {
            const Val v = prop_velocity(e.self, e.compF, pos);
            f64 s = 0;
            for (u32 i = 0; i < v.n; ++i) s += v.v[i] * v.v[i];
            return Val::num(std::sqrt(s));
        }
        case GNumKeys: {
            const Track* t = find_track(e.self, e.component);
            return Val::num(t ? static_cast<f64>(t->keys.size()) : 0.0);
        }
        case GInPoint: case GOutPoint: case GWidth: case GHeight: {
            static constexpr const char* kNames[] = {"inPoint", "outPoint", "width", "height"};
            return member(make_layer(e.layer, e.layerId), kNames[id - GInPoint], pos);
        }
        default: return fail(pos, "nome não suportado");
    }
}

Val Ctx::member(const Val& obj, std::string_view name, u32 pos) {
    switch (obj.k) {
        case K::Math: {
            if (name == "PI") return Val::num(3.14159265358979323846);
            if (name == "E") return Val::num(2.71828182845904523536);
            static constexpr NameEntry kMath[] = {
                {"sin", FSin}, {"cos", FCos}, {"tan", FTan}, {"asin", FAsin}, {"acos", FAcos}, {"atan", FAtan},
                {"atan2", FAtan2}, {"sqrt", FSqrt}, {"abs", FAbs}, {"floor", FFloor}, {"ceil", FCeil},
                {"round", FRound}, {"min", FMin}, {"max", FMax}, {"pow", FPow}, {"exp", FExp}, {"log", FLog},
                {"random", FRandom},
            };
            for (const NameEntry& m : kMath) if (name == m.name) { Val r; r.k = K::Fn; r.id = m.id; return r; }
            return fail(pos, "Math." + std::string(name) + " não existe");
        }
        case K::Comp: {
            const Composition* c = env->comp;
            if (name == "layer") return make_method(MCompLayer, obj);
            if (name == "width") return Val::num(c->width());
            if (name == "height") return Val::num(c->height());
            if (name == "duration") return Val::num(static_cast<f64>(c->duration().value) / env->fps);
            if (name == "frameDuration") return Val::num(1.0 / env->fps);
            if (name == "numLayers") return Val::num(c->order().size());
            if (name == "name") return make_str(c->name());
            return fail(pos, "thisComp." + std::string(name) + " não existe");
        }
        case K::Layer: {
            const Layer* l = layer_of(obj);
            if (!l) return fail(pos, "a camada não existe mais");
            const LayerId id{obj.a, obj.b};
            if (name == "transform") { Val r = obj; r.k = K::Transform; return r; }
            if (name == "effect") return make_method(MLayerEffect, obj);
            if (name == "name") return make_str(l->name);
            if (name == "index") {
                const i32 z = env->comp->z_index_of(id);
                return Val::num(z < 0 ? 0.0 : static_cast<f64>(env->comp->order().size() - static_cast<u32>(z)));
            }
            if (name == "inPoint") return Val::num(static_cast<f64>(l->start.value) / env->fps);
            if (name == "outPoint") return Val::num(static_cast<f64>(l->end.value) / env->fps);
            if (name == "startTime") return Val::num(static_cast<f64>(l->start.value - l->offset.value) / env->fps);
            if (name == "width" || name == "height") {
                const bool w = name == "width";
                if (l->kind == LayerKind::Shape) return Val::num(w ? l->shape.bounds.w : l->shape.bounds.h);
                if (l->kind == LayerKind::Null) return Val::num(100.0);
                return Val::num(w ? env->comp->width() : env->comp->height());
            }
            if (name == "hasParent") return Val::num(l->parent.valid() ? 1.0 : 0.0);
            if (name == "parent") {
                const Layer* p2 = l->parent.valid() ? env->comp->layer(l->parent) : nullptr;
                if (!p2) return fail(pos, "a camada não tem pai");
                return make_layer(p2, l->parent);
            }
            // layer.position etc. (atalho do transform, como no After Effects).
            Val t = obj;
            t.k = K::Transform;
            return member(t, name, pos);
        }
        case K::Transform: {
            const Layer* l = layer_of(obj);
            if (!l) return fail(pos, "a camada não existe mais");
            const LayerId id{obj.a, obj.b};
            using TP = TrackProperty;
            if (name == "position") return make_prop(transform_desc(l, id, TP::PositionX));
            if (name == "scale") return make_prop(transform_desc(l, id, TP::ScaleX));
            if (name == "anchorPoint") return make_prop(transform_desc(l, id, TP::AnchorX));
            if (name == "rotation" || name == "zRotation") return make_prop(transform_desc(l, id, TP::RotationZ));
            if (name == "xRotation") return make_prop(transform_desc(l, id, TP::RotationX));
            if (name == "yRotation") return make_prop(transform_desc(l, id, TP::RotationY));
            if (name == "opacity") return make_prop(transform_desc(l, id, TP::Opacity));
            return fail(pos, "'" + std::string(name) + "' não é uma propriedade de transformação");
        }
        case K::Effect: {
            if (name == "param") return make_method(MEffectParam, obj);
            if (name == "active" || name == "enabled") {
                const Layer* l = layer_of(obj);
                return Val::num(l && obj.id < l->effects.size() && l->effects[obj.id].enabled ? 1.0 : 0.0);
            }
            if (name == "numProperties") {
                const Layer* l = layer_of(obj);
                const ParameterRegistry* reg = l && obj.id < l->effects.size() ? builtin_effects().params(l->effects[obj.id].type) : nullptr;
                return Val::num(reg ? reg->count() : 0.0);
            }
            // effect("X").Slider — nome do parâmetro direto.
            const Val arg = make_str(std::string(name));
            return call_method(MEffectParam, obj, &arg, 1, pos);
        }
        case K::Prop: {
            const PropDesc d = props[obj.a];
            if (name == "value") return prop_value(d, env->compF, true, pos);
            if (name == "valueAtTime") return make_method(MPropValueAtTime, obj);
            if (name == "velocityAtTime") return make_method(MPropVelocityAtTime, obj);
            if (name == "velocity") return prop_velocity(d, env->compF, pos);
            if (name == "speed") {
                const Val v = prop_velocity(d, env->compF, pos);
                f64 s = 0;
                for (u32 i = 0; i < v.n; ++i) s += v.v[i] * v.v[i];
                return Val::num(std::sqrt(s));
            }
            if (name == "numKeys") {
                const Track* t = find_track(d, 0);
                return Val::num(t ? static_cast<f64>(t->keys.size()) : 0.0);
            }
            if (name == "key") return make_method(MPropKey, obj);
            // Componente por nome não existe no AE; qualquer outro membro é do valor.
            return fail(pos, "propriedade não tem '" + std::string(name) + "'");
        }
        case K::Key: {
            const PropDesc& d = props[obj.a];
            const Track* t = find_track(d, 0);
            if (!t || obj.b >= t->keys.size()) return fail(pos, "keyframe não existe");
            const f64 kf = static_cast<f64>(t->keys[obj.b].time.value);
            const f64 compF = d.layer ? kf + static_cast<f64>(d.layer->start.value - d.layer->offset.value) : kf;
            if (name == "time") return Val::num(compF / env->fps);
            if (name == "index") return Val::num(obj.b + 1.0);
            if (name == "value") return prop_value(d, compF, false, pos);
            return fail(pos, "keyframe não tem '" + std::string(name) + "'");
        }
        case K::Num:
        case K::Vec:
            if (name == "length") return Val::num(obj.n);
            return fail(pos, "número não tem '" + std::string(name) + "'");
        case K::Str:
            if (name == "length") return Val::num(static_cast<f64>(str(obj).size()));
            return fail(pos, "texto não tem '" + std::string(name) + "'");
        default:
            return fail(pos, "'" + std::string(name) + "' não existe aqui");
    }
}

Val Ctx::key_of(const PropDesc& d, u32 propIndex, f64 idx, u32 pos) {
    const Track* t = find_track(d, 0);
    const i64 k = static_cast<i64>(std::floor(idx + 0.5));
    if (!t || k < 1 || k > static_cast<i64>(t->keys.size())) {
        return fail(pos, "key(" + std::to_string(k) + "): a propriedade tem " + std::to_string(t ? t->keys.size() : 0) + " keyframe(s)");
    }
    Val r;
    r.k = K::Key;
    r.a = propIndex;
    r.b = static_cast<u32>(k - 1);
    return r;
}

Val Ctx::call_method(u32 id, const Val& obj, const Val* args, u32 argc, u32 pos) {
    switch (id) {
        case MCompLayer:
        case FLayer: {
            if (!env->comp) return fail(pos, "sem composição");
            if (argc != 1) return fail(pos, "layer() recebe um nome ou um índice");
            const OrderedIds<LayerId>& order = env->comp->order();
            if (args[0].k == K::Num) {
                const i64 i = static_cast<i64>(std::floor(args[0].v[0] + 0.5));
                if (i < 1 || i > static_cast<i64>(order.size())) return fail(pos, "layer(" + std::to_string(i) + "): não existe");
                const LayerId lid = order.at(order.size() - static_cast<u32>(i));
                return make_layer(env->comp->layer(lid), lid);
            }
            if (args[0].k != K::Str) return fail(pos, "layer() recebe um nome ou um índice");
            const std::string_view nm = str(args[0]);
            for (u32 i = order.size(); i-- > 0;) {   // do topo (índice 1) para baixo
                const Layer* l = env->comp->layer(order.at(i));
                if (l && l->name == nm) return make_layer(l, order.at(i));
            }
            return fail(pos, "camada \"" + std::string(nm) + "\" não existe");
        }
        case MLayerEffect:
        case FEffect: {
            const Layer* l = id == FEffect ? env->layer : layer_of(obj);
            if (!l) return fail(pos, "sem camada");
            const LayerId lid = id == FEffect ? env->layerId : LayerId{obj.a, obj.b};
            if (argc != 1) return fail(pos, "effect() recebe um nome ou um índice");
            u32 found = kInvalidIndex;
            if (args[0].k == K::Num) {
                const i64 i = static_cast<i64>(std::floor(args[0].v[0] + 0.5));
                if (i >= 1 && i <= static_cast<i64>(l->effects.size())) found = static_cast<u32>(i - 1);
            } else if (args[0].k == K::Str) {
                std::string_view nm = str(args[0]);
                u32 nth = 1;
                bool exact = false;
                for (const EffectInstance& e : l->effects) if (effect_matches(e, nm)) exact = true;
                if (!exact) {
                    // "Slider Control 2" = a segunda instância daquele tipo.
                    const usize sp = nm.find_last_of(' ');
                    if (sp != std::string_view::npos && sp + 1 < nm.size()) {
                        u32 v = 0;
                        bool digits = true;
                        for (usize k = sp + 1; k < nm.size(); ++k) {
                            if (nm[k] < '0' || nm[k] > '9') { digits = false; break; }
                            v = v * 10 + static_cast<u32>(nm[k] - '0');
                        }
                        if (digits && v > 0) { nth = v; nm = nm.substr(0, sp); }
                    }
                }
                u32 seen = 0;
                for (u32 i = 0; i < l->effects.size(); ++i) {
                    if (effect_matches(l->effects[i], nm) && ++seen == nth) { found = i; break; }
                }
            } else {
                return fail(pos, "effect() recebe um nome ou um índice");
            }
            if (found == kInvalidIndex) {
                return fail(pos, args[0].k == K::Str ? "efeito \"" + std::string(str(args[0])) + "\" não existe na camada"
                                                     : std::string("efeito não existe na camada"));
            }
            Val r;
            r.k = K::Effect;
            r.a = lid.index;
            r.b = lid.generation;
            r.id = found;
            return r;
        }
        case MEffectParam: {
            const Layer* l = layer_of(obj);
            if (!l || obj.id >= l->effects.size()) return fail(pos, "o efeito não existe mais");
            if (argc != 1) return fail(pos, "parâmetro: passe um nome ou um índice");
            const EffectInstance& inst = l->effects[obj.id];
            const ParameterRegistry* reg = builtin_effects().params(inst.type);
            if (!reg) return fail(pos, "efeito desconhecido");
            u32 pi = kInvalidIndex;
            if (args[0].k == K::Num) {
                const i64 i = static_cast<i64>(std::floor(args[0].v[0] + 0.5));
                if (i >= 1 && i <= static_cast<i64>(reg->count())) pi = static_cast<u32>(i - 1);
            } else if (args[0].k == K::Str) {
                const std::string_view nm = str(args[0]);
                for (u32 i = 0; i < reg->count() && pi == kInvalidIndex; ++i) {
                    if (name_eq(nm, reg->at(i).label) || name_eq(nm, reg->at(i).id)) pi = i;
                }
            }
            if (pi == kInvalidIndex || component_count(reg->at(pi).type) == 0) {
                return fail(pos, args[0].k == K::Str ? "parâmetro \"" + std::string(str(args[0])) + "\" não existe no efeito"
                                                     : std::string("parâmetro não existe no efeito"));
            }
            PropDesc d;
            d.layer = l;
            d.id = LayerId{obj.a, obj.b};
            d.kind = 1;
            d.effectIndex = inst.id;
            d.key0 = pi * 4;
            d.count = static_cast<u8>(component_count(reg->at(pi).type));
            return make_prop(d);
        }
        case MPropValueAtTime:
        case MPropVelocityAtTime: {
            f64 t = 0;
            if (argc != 1 || !want_num(args[0], t, pos, "tempo")) return Val{};
            const PropDesc d = props[obj.a];
            if (id == MPropValueAtTime) return prop_value(d, t * env->fps, true, pos);
            return prop_velocity(d, t * env->fps, pos);
        }
        case MPropKey: {
            f64 i = 0;
            if (argc != 1 || !want_num(args[0], i, pos, "índice do keyframe")) return Val{};
            return key_of(props[obj.a], obj.a, i, pos);
        }
        default: return fail(pos, "método desconhecido");
    }
}

/// linear/ease/easeIn/easeOut: (t, tMin, tMax, v1, v2) ou (t, v1, v2) com t em [0, 1].
Val Ctx::lerp_fn(u32 id, const Val* args, u32 argc, u32 pos) {
    f64 t = 0, t0 = 0, t1 = 1;
    Val v1, v2;
    if (argc == 5) {
        if (!want_num(args[0], t, pos, "t") || !want_num(args[1], t0, pos, "tMin") || !want_num(args[2], t1, pos, "tMax")) return Val{};
        v1 = args[3];
        v2 = args[4];
    } else if (argc == 3) {
        if (!want_num(args[0], t, pos, "t")) return Val{};
        v1 = args[1];
        v2 = args[2];
    } else {
        return fail(pos, "use (t, tMin, tMax, valor1, valor2) ou (t, valor1, valor2)");
    }
    if (!want_numvec(v1, pos, "valor1") || !want_numvec(v2, pos, "valor2")) return Val{};
    f64 u = t1 == t0 ? (t >= t1 ? 1.0 : 0.0) : (t - t0) / (t1 - t0);
    if (t1 < t0) u = (t - t1) / (t0 - t1), std::swap(v1, v2);
    u = std::clamp(u, 0.0, 1.0);
    switch (id) {
        case FEase: u = u * u * (3.0 - 2.0 * u); break;
        case FEaseIn: u = u * u; break;
        case FEaseOut: u = 1.0 - (1.0 - u) * (1.0 - u); break;
        default: break;
    }
    return arith(OpAdd, v1, arith(OpMul, arith(OpSub, v2, v1, pos), Val::num(u), pos), pos);
}

/// loopOut/loopIn por componente, sobre os keyframes da própria propriedade
/// (valores pré-expressão, como no After Effects).
Val Ctx::loop(bool out, const Val* args, u32 argc, bool duration, u32 pos) {
    const Env& e = *env;
    std::string_view type = "cycle";
    f64 param = 0;
    if (argc >= 1) {
        if (args[0].k != K::Str) return fail(pos, "o tipo do loop é um texto: \"cycle\", \"pingpong\", \"offset\" ou \"continue\"");
        type = str(args[0]);
    }
    if (argc >= 2 && !want_num(args[1], param, pos, duration ? "duração" : "número de keyframes")) return Val{};
    const u32 mode = type == "cycle" ? 0u : type == "pingpong" ? 1u : type == "offset" ? 2u : type == "continue" ? 3u : 9u;
    if (mode == 9u) return fail(pos, "tipo de loop desconhecido \"" + std::string(type) + "\"");
    const PropDesc& d = e.self;
    f64 res[4]{};
    for (u32 c = 0; c < d.count; ++c) {
        const Track* tr = find_track(d, c);
        const f64 local = e.localF;
        auto raw = [&](f64 f) { return component(d, c, f, false); };
        if (!tr || tr->keys.size() < 2) { res[c] = raw(local); continue; }
        const u32 n = static_cast<u32>(tr->keys.size());
        u32 i0 = 0, i1 = n - 1;
        f64 t0, t1;
        if (out) {
            t1 = static_cast<f64>(tr->keys[i1].time.value);
            if (duration && param > 0) t0 = std::max(static_cast<f64>(tr->keys[0].time.value), t1 - param * e.fps);
            else {
                if (param >= 1) i0 = n - 1 - std::min<u32>(n - 1, static_cast<u32>(param));
                t0 = static_cast<f64>(tr->keys[i0].time.value);
            }
            if (local <= t1) { res[c] = raw(local); continue; }
        } else {
            t0 = static_cast<f64>(tr->keys[0].time.value);
            if (duration && param > 0) t1 = std::min(static_cast<f64>(tr->keys[n - 1].time.value), t0 + param * e.fps);
            else {
                if (param >= 1) i1 = std::min<u32>(n - 1, static_cast<u32>(param));
                t1 = static_cast<f64>(tr->keys[i1].time.value);
            }
            if (local >= t0) { res[c] = raw(local); continue; }
        }
        const f64 span = t1 - t0;
        if (span <= 0.0) { res[c] = raw(local); continue; }
        const f64 dist = out ? local - t0 : t1 - local;       // quanto passou da borda, medido do outro lado
        const f64 cycles = std::floor(dist / span);
        const f64 u = dist - cycles * span;                     // [0, span)
        switch (mode) {
            case 0:   // cycle
                res[c] = out ? raw(t0 + u) : raw(t1 - u);
                break;
            case 1: { // pingpong
                const bool odd = static_cast<i64>(cycles) % 2 != 0;
                res[c] = out ? (odd ? raw(t1 - u) : raw(t0 + u)) : (odd ? raw(t0 + u) : raw(t1 - u));
                break;
            }
            case 2: { // offset: cada ciclo soma a variação do trecho
                const f64 delta = raw(t1) - raw(t0);
                res[c] = out ? raw(t0 + u) + cycles * delta : raw(t1 - u) - cycles * delta;
                break;
            }
            default: { // continue: segue na velocidade da borda
                if (out) {
                    const f64 vel = raw(t1) - raw(t1 - 1.0);
                    res[c] = raw(t1) + vel * (local - t1);
                } else {
                    const f64 vel = raw(t0 + 1.0) - raw(t0);
                    res[c] = raw(t0) - vel * (t0 - local);
                }
                break;
            }
        }
    }
    return Val::vec(res, d.count);
}

Val Ctx::call_fn(u32 id, const Val* args, u32 argc, u32 pos) {
    const Env& e = *env;
    auto need = [&](u32 lo, u32 hi, const char* sig) {
        if (argc < lo || argc > hi) { fail(pos, std::string("uso: ") + sig); return false; }
        return true;
    };
    // Matemática de um argumento: aplica por componente em vetor.
    auto unary = [&](f64 (*fn)(f64), const char* nm) -> Val {
        if (!need(1, 1, nm) || !want_numvec(args[0], pos, "argumento")) return Val{};
        Val r = args[0];
        for (u32 i = 0; i < r.n; ++i) r.v[i] = fn(r.v[i]);
        return r;
    };
    switch (id) {
        case FSin: return unary([](f64 x) { return std::sin(x); }, "sin(x)");
        case FCos: return unary([](f64 x) { return std::cos(x); }, "cos(x)");
        case FTan: return unary([](f64 x) { return std::tan(x); }, "tan(x)");
        case FAsin: return unary([](f64 x) { return std::asin(x); }, "asin(x)");
        case FAcos: return unary([](f64 x) { return std::acos(x); }, "acos(x)");
        case FAtan: return unary([](f64 x) { return std::atan(x); }, "atan(x)");
        case FSqrt: return unary([](f64 x) { return std::sqrt(x); }, "sqrt(x)");
        case FAbs: return unary([](f64 x) { return std::fabs(x); }, "abs(x)");
        case FFloor: return unary([](f64 x) { return std::floor(x); }, "floor(x)");
        case FCeil: return unary([](f64 x) { return std::ceil(x); }, "ceil(x)");
        case FRound: return unary([](f64 x) { return std::floor(x + 0.5); }, "round(x)");
        case FExp: return unary([](f64 x) { return std::exp(x); }, "exp(x)");
        case FLog: return unary([](f64 x) { return std::log(x); }, "log(x)");
        case FDeg2Rad: return unary([](f64 x) { return x * 3.14159265358979323846 / 180.0; }, "degreesToRadians(graus)");
        case FRad2Deg: return unary([](f64 x) { return x * 180.0 / 3.14159265358979323846; }, "radiansToDegrees(rad)");
        case FAtan2: {
            f64 y = 0, x = 0;
            if (!need(2, 2, "atan2(y, x)") || !want_num(args[0], y, pos, "y") || !want_num(args[1], x, pos, "x")) return Val{};
            return Val::num(std::atan2(y, x));
        }
        case FPow: return need(2, 2, "pow(base, expoente)") ? arith(OpPow, args[0], args[1], pos) : Val{};
        case FMin:
        case FMax: {
            if (argc == 0) return fail(pos, "min/max precisam de argumentos");
            f64 r = 0;
            for (u32 i = 0; i < argc; ++i) {
                f64 x = 0;
                if (!want_num(args[i], x, pos, "argumento")) return Val{};
                r = i == 0 ? x : (id == FMin ? std::min(r, x) : std::max(r, x));
            }
            return Val::num(r);
        }
        case FClamp: {
            if (!need(3, 3, "clamp(valor, mínimo, máximo)")) return Val{};
            for (u32 i = 0; i < 3; ++i) if (!want_numvec(args[i], pos, "argumento")) return Val{};
            Val r = args[0];
            for (u32 i = 0; i < r.n; ++i) {
                const f64 lo = args[1].k == K::Num ? args[1].v[0] : (i < args[1].n ? args[1].v[i] : -INFINITY);
                const f64 hi = args[2].k == K::Num ? args[2].v[0] : (i < args[2].n ? args[2].v[i] : INFINITY);
                r.v[i] = std::min(std::max(r.v[i], lo), hi);
            }
            return r;
        }
        case FLinear: case FEase: case FEaseIn: case FEaseOut: return lerp_fn(id, args, argc, pos);
        case FLength: {
            if (!need(1, 2, "length(vetor) ou length(a, b)")) return Val{};
            Val v = args[0];
            if (argc == 2) v = arith(OpSub, args[1], args[0], pos);
            if (failed || !want_numvec(v, pos, "argumento")) return Val{};
            f64 s = 0;
            for (u32 i = 0; i < v.n; ++i) s += v.v[i] * v.v[i];
            return Val::num(std::sqrt(s));
        }
        case FNormalize: {
            if (!need(1, 1, "normalize(vetor)") || !want_numvec(args[0], pos, "argumento")) return Val{};
            Val v = args[0];
            f64 s = 0;
            for (u32 i = 0; i < v.n; ++i) s += v.v[i] * v.v[i];
            s = std::sqrt(s);
            for (u32 i = 0; i < v.n; ++i) v.v[i] = s > 0 ? v.v[i] / s : 0.0;
            return v;
        }
        case FAdd: return need(2, 2, "add(a, b)") ? arith(OpAdd, args[0], args[1], pos) : Val{};
        case FSub: return need(2, 2, "sub(a, b)") ? arith(OpSub, args[0], args[1], pos) : Val{};
        case FMul: return need(2, 2, "mul(vetor, número)") ? arith(OpMul, args[0], args[1], pos) : Val{};
        case FDiv: return need(2, 2, "div(vetor, número)") ? arith(OpDiv, args[0], args[1], pos) : Val{};
        case FDot: {
            if (!need(2, 2, "dot(a, b)") || !want_numvec(args[0], pos, "a") || !want_numvec(args[1], pos, "b")) return Val{};
            f64 s = 0;
            for (u32 i = 0; i < std::min(args[0].n, args[1].n); ++i) s += args[0].v[i] * args[1].v[i];
            return Val::num(s);
        }
        case FCross: {
            if (!need(2, 2, "cross(a, b)") || args[0].k != K::Vec || args[1].k != K::Vec) return fail(pos, "cross precisa de dois vetores");
            const f64* a = args[0].v; const f64* b = args[1].v;
            const f64 az = args[0].n > 2 ? a[2] : 0.0, bz = args[1].n > 2 ? b[2] : 0.0;
            const f64 r[3] = {a[1] * bz - az * b[1], az * b[0] - a[0] * bz, a[0] * b[1] - a[1] * b[0]};
            return Val::vec(r, 3);
        }
        case FFramesToTime: {
            f64 f = 0, fps = e.fps;
            if (!need(1, 2, "framesToTime(quadros[, fps])") || !want_num(args[0], f, pos, "quadros")) return Val{};
            if (argc == 2 && !want_num(args[1], fps, pos, "fps")) return Val{};
            return Val::num(fps > 0 ? f / fps : 0.0);
        }
        case FTimeToFrames: {
            f64 t = e.compF / e.fps, fps = e.fps;
            if (argc > 2) return fail(pos, "uso: timeToFrames([t[, fps]])");
            if (argc >= 1 && !want_num(args[0], t, pos, "t")) return Val{};
            if (argc == 2 && !want_num(args[1], fps, pos, "fps")) return Val{};
            return Val::num(std::floor(t * fps + 1e-6));
        }
        case FSeedRandom: {
            f64 s = 0, tl = 0;
            if (!need(1, 2, "seedRandom(semente[, semTempo])") || !want_num(args[0], s, pos, "semente")) return Val{};
            if (argc == 2 && !want_num(args[1], tl, pos, "semTempo")) return Val{};
            rngUserSeed = static_cast<u32>(static_cast<i64>(s));
            rngTimeless = tl != 0.0;
            seed_rng();
            return Val{};
        }
        case FRandom:
        case FGaussRandom: {
            auto draw = [&]() {
                if (id == FRandom) return next01();
                // Box–Muller, ~[-1, 1] em 90% (como o gaussRandom do AE).
                const f64 u1 = std::max(1e-12, next01()), u2 = next01();
                return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2) / 1.6448536;
            };
            auto scaleTo = [&](f64 r, f64 lo, f64 hi) { return id == FRandom ? lo + r * (hi - lo) : lo + (r * 0.5 + 0.5) * (hi - lo); };
            if (argc == 0) return Val::num(id == FRandom ? draw() : draw() * 0.5 + 0.5);
            for (u32 i = 0; i < argc; ++i) if (!want_numvec(args[i], pos, "argumento")) return Val{};
            if (argc > 2) return fail(pos, "uso: random(), random(máx) ou random(mín, máx)");
            const Val lo = argc == 2 ? args[0] : Val::num(0.0);
            const Val hi = argc == 2 ? args[1] : args[0];
            const u32 n = std::max(lo.n, hi.n);
            f64 r[4]{};
            for (u32 i = 0; i < n; ++i) {
                const f64 a = lo.k == K::Num ? lo.v[0] : (i < lo.n ? lo.v[i] : 0.0);
                const f64 b = hi.k == K::Num ? hi.v[0] : (i < hi.n ? hi.v[i] : 0.0);
                r[i] = scaleTo(draw(), a, b);
            }
            return (lo.k == K::Num && hi.k == K::Num) ? Val::num(r[0]) : Val::vec(r, n);
        }
        case FNoise: {
            if (!need(1, 1, "noise(número ou vetor)") || !want_numvec(args[0], pos, "argumento")) return Val{};
            const Val& v = args[0];
            return Val::num(noise3(v.v[0], v.n > 1 ? v.v[1] : 0.37, v.n > 2 ? v.v[2] : 0.61));
        }
        case FWiggle: {
            f64 freq = 0, oct = 1, mult = 0.5, t = e.compF / e.fps;
            if (!need(2, 5, "wiggle(frequência, amplitude[, oitavas, multiplicador, t])")) return Val{};
            if (!want_num(args[0], freq, pos, "frequência")) return Val{};
            if (!want_numvec(args[1], pos, "amplitude")) return Val{};
            if (argc >= 3 && !want_num(args[2], oct, pos, "oitavas")) return Val{};
            if (argc >= 4 && !want_num(args[3], mult, pos, "multiplicador")) return Val{};
            if (argc >= 5 && !want_num(args[4], t, pos, "t")) return Val{};
            const u32 octaves = static_cast<u32>(std::clamp(oct, 1.0, 10.0));
            Val base = e.value;
            if (base.k != K::Num && base.k != K::Vec) base = Val::num(0.0);
            Val r = base;
            for (u32 c = 0; c < r.n; ++c) {
                const f64 a = args[1].k == K::Num ? args[1].v[0] : (c < args[1].n ? args[1].v[c] : 0.0);
                r.v[c] += a * fractal(hash_mix(e.seedBase, 0x5157u + c), t * freq, octaves, mult);
            }
            return r;
        }
        case FLoopOut: return need(0, 2, "loopOut(tipo[, keyframes])") ? loop(true, args, argc, false, pos) : Val{};
        case FLoopIn: return need(0, 2, "loopIn(tipo[, keyframes])") ? loop(false, args, argc, false, pos) : Val{};
        case FLoopOutDur: return need(0, 2, "loopOutDuration(tipo[, segundos])") ? loop(true, args, argc, true, pos) : Val{};
        case FLoopInDur: return need(0, 2, "loopInDuration(tipo[, segundos])") ? loop(false, args, argc, true, pos) : Val{};
        case FValueAtTime:
        case FVelocityAtTime: {
            f64 t = 0;
            if (!need(1, 1, id == FValueAtTime ? "valueAtTime(t)" : "velocityAtTime(t)") || !want_num(args[0], t, pos, "t")) return Val{};
            if (!e.layer) {
                if (id == FValueAtTime) return e.value;
                return Val::num(0.0);
            }
            if (id == FValueAtTime) return prop_value(e.self, t * e.fps, false, pos);
            return prop_velocity(e.self, t * e.fps, pos);
        }
        case FKey: {
            f64 i = 0;
            if (!need(1, 1, "key(índice)") || !want_num(args[0], i, pos, "índice")) return Val{};
            if (!e.layer) return fail(pos, "sem keyframes (expressão fora de uma camada)");
            const Val pr = make_prop(e.self);
            PropDesc own = e.self;
            // key() da própria track (o componente desta expressão).
            if (own.kind == 0) own.prop = static_cast<TrackProperty>(static_cast<u16>(own.prop) + e.component), own.count = 1;
            else if (own.kind == 1 || own.kind == 3) own.key0 += e.component, own.count = 1;
            props[pr.a] = own;
            return key_of(own, pr.a, i, pos);
        }
        case FLayer: return call_method(FLayer, Val{}, args, argc, pos);
        case FEffect: return call_method(FEffect, Val{}, args, argc, pos);
        default: return fail(pos, "função desconhecida");
    }
}

/// Executa o programa. `out` recebe o valor final (já sem propriedade).
bool run(Ctx& ctx, const Program& prog, const Env& env, Val& out) {
    ctx.reset(&prog, &env);
    Val v = ctx.exec(prog.root, 0);
    if (!ctx.failed && ctx.flow == Flow::Return) v = ctx.ret;
    if (!ctx.failed) v = ctx.deref(v, 0);
    if (ctx.failed) return false;
    if (v.k != K::Num && v.k != K::Vec) {
        ctx.fail(prog.nodes.empty() ? 0 : prog.source.size() > 0 ? static_cast<u32>(prog.source.size() - 1) : 0,
                 v.k == K::Undef ? "a expressão não produziu um valor" : "o resultado precisa ser número ou vetor");
        return false;
    }
    for (u32 i = 0; i < v.n; ++i) {
        if (!std::isfinite(v.v[i])) {
            ctx.fail(0, "o resultado não é um número finito (divisão por zero?)");
            return false;
        }
    }
    out = v;
    return true;
}

/// Um contexto por nível de referência (definido aqui: Ctx precisa estar completo).
thread_local std::vector<std::unique_ptr<Ctx>> g_ctxs;

Ctx& ctx_for_depth(u32 depth) {
    while (g_ctxs.size() <= depth) g_ctxs.push_back(std::make_unique<Ctx>());
    return *g_ctxs[depth];
}

std::string layer_prop_name(const StackEntry& s) {
    std::string n = s.owner.layer ? s.owner.layer->name : std::string("?");
    u32 comp = 0;
    const PropDesc d = desc_for_track(s.owner.layer, s.owner.id, *s.track, 30.0, comp);
    n += ".";
    n += prop_label(d);
    return n;
}

// Cache global de programas por texto.
std::mutex g_cacheMutex;
std::unordered_map<std::string, std::weak_ptr<const Program>> g_programCache;

} // namespace

// =============================================================================
// API
// =============================================================================
Diagnostic TrackExpression::error() const {
    if (!parseError.ok) return parseError;
    if (!hasRuntime_.load(std::memory_order_acquire)) return Diagnostic{};
    std::lock_guard<std::mutex> g(mutex_);
    return runtime_;
}

void TrackExpression::report_runtime(const Diagnostic& d) const {
    std::lock_guard<std::mutex> g(mutex_);
    runtime_ = d;
    hasRuntime_.store(true, std::memory_order_release);
}

void TrackExpression::clear_runtime() const {
    if (!hasRuntime_.load(std::memory_order_acquire)) return;   // caminho quente: sem lock
    std::lock_guard<std::mutex> g(mutex_);
    runtime_ = Diagnostic{};
    hasRuntime_.store(false, std::memory_order_release);
}

std::shared_ptr<const TrackExpression> compile(std::string_view source) {
    auto te = std::make_shared<TrackExpression>();
    te->source = std::string(source);
    {
        std::lock_guard<std::mutex> g(g_cacheMutex);
        const auto it = g_programCache.find(te->source);
        if (it != g_programCache.end()) {
            if (auto p = it->second.lock()) {
                te->program = std::move(p);
                return te;
            }
        }
    }
    Diagnostic diag;
    auto prog = compile_program(source, diag);
    if (!prog) {
        te->parseError = diag;
        return te;
    }
    {
        std::lock_guard<std::mutex> g(g_cacheMutex);
        if (g_programCache.size() > 4096) {
            std::erase_if(g_programCache, [](const auto& kv) { return kv.second.expired(); });
        }
        g_programCache[te->source] = prog;
    }
    te->program = std::move(prog);
    return te;
}

Diagnostic check_syntax(std::string_view source) {
    Diagnostic d;
    (void)compile_program(source, d);
    return d;
}

const EffectRegistry& builtin_effects() {
    static const EffectRegistry reg = [] {
        EffectRegistry r;
        register_builtin_effects(r);
        return r;
    }();
    return reg;
}

Scope::Scope(const Timeline& timeline) noexcept {
    if (!g_tls.scopes.empty() && g_tls.scopes.back()->tl == &timeline) return;   // aninhado: reaproveita
    g_tls.scopes.push_back(g_tls.acquire(&timeline));
    pushed_ = true;
}

Scope::~Scope() {
    if (!pushed_) return;
    g_tls.scopes.pop_back();
    g_tls.release();
}

void register_provider(TimelineProvider fn, void* ctx) {
    std::lock_guard<std::mutex> g(g_providerMutex);
    g_providers.push_back(Provider{fn, ctx});
}

void unregister_provider(void* ctx) {
    std::lock_guard<std::mutex> g(g_providerMutex);
    std::erase_if(g_providers, [ctx](const Provider& p) { return p.ctx == ctx; });
}

f32 evaluate_track(const Track& track, FrameIndex t, const f32* fallback) noexcept {
    const TrackExpression* te = track.expression.get();
    auto raw = [&]() -> f32 {
        return track.keys.empty() ? (fallback ? *fallback : track.staticValue) : track.sample_keys(t);
    };
    if (!te || !te->program) return raw();   // erro de sintaxe: vale o keyframe

    // Ciclo: a mesma track já está sendo avaliada nesta thread.
    Tls& tls = g_tls;
    for (u32 i = 0; i < tls.depth; ++i) {
        if (tls.stack[i].track != &track) continue;
        if (!tls.cycle) {
            tls.cycle = true;
            std::string path = "dependência circular: ";
            for (u32 k = i; k < tls.depth; ++k) path += layer_prop_name(tls.stack[k]) + " → ";
            path += layer_prop_name(tls.stack[i]);
            tls.cycleMsg = std::move(path);
        }
        return raw();
    }
    if (tls.depth >= kMaxRefDepth) {
        if (!tls.cycle) {
            tls.cycle = true;
            tls.cycleMsg = "referências encadeadas demais (mais de " + std::to_string(kMaxRefDepth) + " propriedades)";
        }
        return raw();
    }

    // Camada dona: primeiro o escopo do quadro, depois quem se registrou.
    ScopeData* sd = tls.scopes.empty() ? nullptr : tls.scopes.back();
    Owner owner = sd ? sd->find(&track) : Owner{};
    bool tempScope = false;
    if (!owner.layer) {
        std::vector<Provider> providers;
        {
            std::lock_guard<std::mutex> g(g_providerMutex);
            providers = g_providers;
        }
        for (const Provider& p : providers) {
            const Timeline* tl = p.fn ? p.fn(p.ctx) : nullptr;
            if (!tl || (sd && sd->tl == tl)) continue;
            ScopeData* tmp = tls.acquire(tl);
            owner = tmp->find(&track);
            if (owner.layer) {
                tls.scopes.push_back(tmp);
                sd = tmp;
                tempScope = true;
                break;
            }
            tls.release();
        }
    }
    auto done = [&](f32 v) {
        if (tempScope) { tls.scopes.pop_back(); tls.release(); }
        return v;
    };
    if (!owner.layer) {
        Diagnostic d;
        d.ok = false;
        d.message = "a expressão está numa trilha fora de uma camada da timeline";
        te->report_runtime(d);
        return done(raw());
    }
    if (sd) {
        const auto it = sd->memo.find(MemoKey{&track, t.value});
        if (it != sd->memo.end()) return done(it->second);
    }

    const f64 fps = owner.comp && owner.comp->fps() > 0.0 ? owner.comp->fps() : 30.0;
    Env env;
    env.comp = owner.comp;
    env.layer = owner.layer;
    env.layerId = owner.id;
    env.track = &track;
    env.fallback = fallback;
    env.fps = fps;
    env.self = desc_for_track(owner.layer, owner.id, track, fps, env.component);
    env.localF = static_cast<f64>(t.value);
    env.compF = env.localF + static_cast<f64>(owner.layer->start.value - owner.layer->offset.value);
    env.seedBase = hash_mix(hash_mix(owner.id.index * 2654435761u, static_cast<u32>(env.self.prop) | (env.self.kind << 16)),
                            hash_mix(env.self.effectIndex, env.self.key0));

    const u32 myDepth = tls.depth;
    Ctx& ctx = ctx_for_depth(myDepth);
    ctx.env = &env;
    {
        f64 vals[4]{};
        for (u32 c = 0; c < env.self.count; ++c) vals[c] = ctx.component(env.self, c, env.localF, false);
        env.value = Val::vec(vals, env.self.count);
    }
    const f64 rawDisplay = env.self.count > env.component ? env.value.v[env.component] : 0.0;
    const f32 rawNative = static_cast<f32>(rawDisplay / env.self.scale);

    tls.stack[myDepth] = StackEntry{&track, owner};
    tls.depth = myDepth + 1;
    Val result;
    const bool ok = run(ctx, *te->program, env, result);
    tls.depth = myDepth;
    const bool cycled = tls.cycle;
    std::string cycleMsg = cycled ? tls.cycleMsg : std::string();
    if (myDepth == 0) { tls.cycle = false; tls.cycleMsg.clear(); }

    if (!ok || cycled) {
        te->report_runtime(ok ? make_diag(te->source, 0, cycleMsg)
                              : make_diag(te->source, ctx.errPos, ctx.err));
        return done(rawNative);
    }
    const f64 display = result.k == K::Num ? result.v[0]
                      : (env.component < result.n ? result.v[env.component] : rawDisplay);
    const f32 v = static_cast<f32>(display / env.self.scale);
    te->clear_runtime();
    if (sd && sd->memo.size() < 262144) sd->memo.emplace(MemoKey{&track, t.value}, v);
    return done(v);
}

f32 static_value(const Layer& l, const Track& t) noexcept {
    u32 c = 0;
    const PropDesc d = desc_for_track(&l, LayerId{}, t, 30.0, c);
    return static_cast<f32>(static_base(d, c));
}

StandaloneResult evaluate_standalone(std::string_view source, f64 time, const f64* value, u32 n, f64 fps) {
    StandaloneResult r;
    Diagnostic diag;
    auto prog = compile_program(source, diag);
    if (!prog) { r.diag = diag; return r; }
    Env env;
    env.fps = fps > 0.0 ? fps : 30.0;
    env.compF = time * env.fps;
    env.localF = env.compF;
    env.self.count = static_cast<u8>(std::clamp<u32>(n, 1, 4));
    env.self.kind = 2;
    f64 vals[4]{};
    for (u32 i = 0; i < n && i < 4; ++i) vals[i] = value ? value[i] : 0.0;
    env.value = Val::vec(vals, env.self.count);
    Ctx& ctx = ctx_for_depth(g_tls.depth);
    Val out;
    if (!run(ctx, *prog, env, out)) {
        r.diag = make_diag(source, ctx.errPos, ctx.err);
        return r;
    }
    r.ok = true;
    r.count = out.n;
    for (u32 i = 0; i < out.n; ++i) r.v[i] = out.v[i];
    return r;
}

} // namespace aurea::expr
