// =============================================================================
//  Aurea / project / AeScript.cpp
//
//  Interpretador do subconjunto de ExtendScript (ver AeScript.hpp): léxico,
//  análise (árvore com linha em cada nó), avaliação por árvore com fluxo
//  explícito (sem exceções de C++ — o motor compila sem elas) e a biblioteca
//  do ES3 que scripts de animação usam. O DOM do AE fica em AeScriptDom.cpp.
// =============================================================================
#include "AeScript.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aurea::ae::script {

// =============================================================================
// Árvore
// =============================================================================
enum class N : u8 {
    // expressões
    Num, Str, True, False, Null, Undef, This, Ident, ArrayLit, ObjectLit, FuncExpr, Unary, Update, Binary,
    Logical, Cond, Assign, Member, Index, Call, New, Seq, Regex,
    // comandos
    Block, Var, ExprStmt, If, For, ForIn, While, DoWhile, Return, Break, Continue, Throw, Try, Switch, Case,
    FuncDecl, Empty,
};

enum Op : u8 {
    OAdd = 1, OSub, OMul, ODiv, OMod, OLt, OGt, OLe, OGe, OEq, ONe, OSEq, OSNe, OBitAnd, OBitOr, OBitXor, OShl,
    OShr, OUShr, OIn, OInstanceOf, OAnd, OOr,
    ONeg, OPos, ONot, OBitNot, OTypeof, OVoid, ODelete,
    OPreInc, OPreDec, OPostInc, OPostDec,
    OSet, OAddSet, OSubSet, OMulSet, ODivSet, OModSet, OAndSet, OOrSet, OXorSet, OShlSet, OShrSet, OUShrSet,
};

struct Node {
    N k = N::Empty;
    u8 op = 0;
    u32 line = 0;
    i32 a = -1, b = -1, c = -1, d = -1;
    f64 num = 0.0;
    std::string s;
    std::vector<i32> list;
    std::vector<std::string> names;   ///< ObjectLit: chaves; Var: nomes; Try: nome do catch
};

struct Func {
    std::string name;
    std::vector<std::string> params;
    i32 body = -1;
    u32 line = 0;
    std::vector<std::string> vars;    ///< var içados
    std::vector<i32> decls;           ///< function içadas (índices de FuncDecl)
};

struct Program {
    std::vector<Node> nodes;
    std::vector<Func> funcs;          ///< funcs[0] = o programa
};

// =============================================================================
// Léxico
// =============================================================================
enum class Tk : u8 { End, Num, Str, Ident, Punct, Regex, Error };

struct Token {
    Tk t = Tk::End;
    std::string text;
    f64 num = 0.0;
    u32 line = 1;
    bool nl = false;      ///< houve quebra de linha antes
    usize pos = 0;
};

namespace {

bool ident_start(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || static_cast<u8>(c) >= 0x80;
}
bool ident_part(char c) noexcept { return ident_start(c) || (c >= '0' && c <= '9'); }

void put_utf8(std::string& o, u32 cp) {
    if (cp < 0x80) o.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { o.push_back(static_cast<char>(0xC0 | (cp >> 6))); o.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

int hexv(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class Lexer {
public:
    explicit Lexer(std::string_view src) : s_(src) {}
    std::string error;
    u32 errLine = 0;
    std::vector<std::pair<u32, std::string>> directives;   ///< #include etc.

    Token next() {
        Token t;
        bool nl = false;
        for (;;) {
            if (i_ >= s_.size()) break;
            const char c = s_[i_];
            if (c == '\n') { ++line_; ++i_; nl = true; lineStart_ = true; continue; }
            if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { ++i_; continue; }
            if (static_cast<u8>(c) == 0xEF && i_ + 2 < s_.size() && static_cast<u8>(s_[i_ + 1]) == 0xBB) { i_ += 3; continue; }  // BOM
            if (c == '#' && lineStart_) {   // #target, #include, #script: diretivas do ExtendScript
                const usize e = s_.find('\n', i_);
                directives.emplace_back(line_, std::string(s_.substr(i_, e == std::string_view::npos ? std::string_view::npos : e - i_)));
                i_ = e == std::string_view::npos ? s_.size() : e;
                continue;
            }
            if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
                while (i_ < s_.size() && s_[i_] != '\n') ++i_;
                continue;
            }
            if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '*') {
                i_ += 2;
                while (i_ + 1 < s_.size() && !(s_[i_] == '*' && s_[i_ + 1] == '/')) {
                    if (s_[i_] == '\n') { ++line_; nl = true; }
                    ++i_;
                }
                i_ = std::min(s_.size(), i_ + 2);
                continue;
            }
            break;
        }
        lineStart_ = false;
        t.nl = nl;
        t.line = line_;
        t.pos = i_;
        if (i_ >= s_.size()) { t.t = Tk::End; return t; }
        const char c = s_[i_];
        if (ident_start(c)) {
            const usize b = i_;
            while (i_ < s_.size() && ident_part(s_[i_])) ++i_;
            t.t = Tk::Ident;
            t.text = std::string(s_.substr(b, i_ - b));
            return t;
        }
        if ((c >= '0' && c <= '9') || (c == '.' && i_ + 1 < s_.size() && s_[i_ + 1] >= '0' && s_[i_ + 1] <= '9')) {
            t.t = Tk::Num;
            if (c == '0' && i_ + 1 < s_.size() && (s_[i_ + 1] == 'x' || s_[i_ + 1] == 'X')) {
                i_ += 2;
                f64 v = 0;
                while (i_ < s_.size() && hexv(s_[i_]) >= 0) v = v * 16 + hexv(s_[i_++]);
                t.num = v;
                return t;
            }
            const usize b = i_;
            while (i_ < s_.size() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.')) ++i_;
            if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
                ++i_;
                if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
                while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
            }
            const std::string num(s_.substr(b, i_ - b));
            t.num = std::strtod(num.c_str(), nullptr);
            return t;
        }
        if (c == '"' || c == '\'') {
            const char q = c;
            ++i_;
            t.t = Tk::Str;
            while (i_ < s_.size() && s_[i_] != q) {
                char x = s_[i_];
                if (x == '\n') { fail("texto sem fechar aspas"); t.t = Tk::Error; return t; }
                if (x == '\\' && i_ + 1 < s_.size()) {
                    const char e = s_[i_ + 1];
                    i_ += 2;
                    switch (e) {
                        case 'n': t.text.push_back('\n'); break;
                        case 't': t.text.push_back('\t'); break;
                        case 'r': t.text.push_back('\r'); break;
                        case 'b': t.text.push_back('\b'); break;
                        case 'f': t.text.push_back('\f'); break;
                        case 'v': t.text.push_back('\v'); break;
                        case '0': t.text.push_back('\0'); break;
                        case '\n': ++line_; break;   // continuação de linha
                        case '\r':                   // continuação com CRLF (arquivo do Windows)
                            if (i_ < s_.size() && s_[i_] == '\n') ++i_;
                            ++line_;
                            break;
                        case 'x': {
                            int h1 = i_ < s_.size() ? hexv(s_[i_]) : -1, h2 = i_ + 1 < s_.size() ? hexv(s_[i_ + 1]) : -1;
                            if (h1 >= 0 && h2 >= 0) { put_utf8(t.text, static_cast<u32>(h1 * 16 + h2)); i_ += 2; }
                            break;
                        }
                        case 'u': {
                            u32 cp = 0;
                            bool ok = i_ + 4 <= s_.size();
                            for (int k = 0; ok && k < 4; ++k) {
                                const int h = hexv(s_[i_ + k]);
                                if (h < 0) ok = false; else cp = cp * 16 + static_cast<u32>(h);
                            }
                            if (ok) { put_utf8(t.text, cp); i_ += 4; }
                            break;
                        }
                        default: t.text.push_back(e);
                    }
                    continue;
                }
                t.text.push_back(x);
                ++i_;
            }
            if (i_ >= s_.size()) { fail("texto sem fechar aspas"); t.t = Tk::Error; return t; }
            ++i_;
            return t;
        }
        static const char* const kPunct[] = {">>>=", "===", "!==", ">>>", "<<=", ">>=", "==", "!=", "<=", ">=", "&&", "||",
                                             "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>"};
        for (const char* p : kPunct) {
            const usize n = std::strlen(p);
            if (s_.compare(i_, n, p) == 0) {
                t.t = Tk::Punct;
                t.text = p;
                i_ += n;
                return t;
            }
        }
        if (std::strchr("{}()[];,<>+-*/%&|^!~?:=.", c)) {
            t.t = Tk::Punct;
            t.text = std::string(1, c);
            ++i_;
            return t;
        }
        fail(std::string("caractere inesperado '") + c + "'");
        t.t = Tk::Error;
        return t;
    }

    /// Relê a partir de `pos` como expressão regular (/…/flags).
    Token regex(usize pos, u32 line) {
        Token t;
        t.t = Tk::Regex;
        t.line = line;
        i_ = pos + 1;
        bool cls = false;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '\n') break;
            if (c == '\\') { t.text.push_back(c); if (i_ + 1 < s_.size()) t.text.push_back(s_[i_ + 1]); i_ += 2; continue; }
            if (c == '[') cls = true;
            else if (c == ']') cls = false;
            else if (c == '/' && !cls) break;
            t.text.push_back(c);
            ++i_;
        }
        if (i_ >= s_.size() || s_[i_] != '/') { fail("expressão regular sem fechar"); t.t = Tk::Error; return t; }
        ++i_;
        while (i_ < s_.size() && ident_part(s_[i_])) ++i_;
        return t;
    }

private:
    void fail(std::string m) {
        if (error.empty()) { error = std::move(m); errLine = line_; }
    }
    std::string_view s_;
    usize i_ = 0;
    u32 line_ = 1;
    bool lineStart_ = true;
};

// =============================================================================
// Análise
// =============================================================================
class Parser {
public:
    Parser(std::string_view src, Program& p) : lex_(src), p_(p) {
        volatile char probe = 0;
        stackBase_ = reinterpret_cast<uintptr_t>(&probe);
        advance();
    }
    std::string error;
    u32 errLine = 0;
    std::vector<std::pair<u32, std::string>> directives() const { return lex_.directives; }

    bool parse() {
        Func top;
        top.name = "<script>";
        p_.funcs.push_back(top);
        funcStack_.push_back(0);
        Node block;
        block.k = N::Block;
        block.line = 1;
        while (ok() && tok_.t != Tk::End) block.list.push_back(statement());
        const i32 root = add(std::move(block));
        p_.funcs[0].body = root;
        funcStack_.pop_back();
        if (!lex_.error.empty() && error.empty()) { error = lex_.error; errLine = lex_.errLine; }
        return error.empty();
    }

private:
    Lexer lex_;
    Program& p_;
    Token tok_;
    std::vector<u32> funcStack_;
    u32 depth_ = 0;
    bool noIn_ = false;
    uintptr_t stackBase_ = 0;

    /// Aninhamento: o teto de níveis E o da pilha de C++ (o que vier antes).
    bool too_deep() {
        volatile char probe = 0;
        const uintptr_t here = reinterpret_cast<uintptr_t>(&probe);
        const uintptr_t used = here > stackBase_ ? here - stackBase_ : stackBase_ - here;
        return depth_ > 150 || used > 160u * 1024u;
    }

    bool ok() const noexcept { return error.empty() && lex_.error.empty(); }
    void fail(const std::string& m) {
        if (error.empty()) { error = m; errLine = tok_.line; }
    }
    void advance() { tok_ = lex_.next(); if (tok_.t == Tk::Error && error.empty()) { error = lex_.error; errLine = lex_.errLine; } }
    bool is(const char* p) const noexcept { return tok_.t == Tk::Punct && tok_.text == p; }
    bool kw(const char* k) const noexcept { return tok_.t == Tk::Ident && tok_.text == k; }
    bool eat(const char* p) { if (is(p)) { advance(); return true; } return false; }
    void expect(const char* p) {
        if (!eat(p)) fail(std::string("esperava '") + p + "'" + (tok_.text.empty() ? "" : " perto de '" + tok_.text + "'"));
    }
    i32 add(Node n) {
        p_.nodes.push_back(std::move(n));
        return static_cast<i32>(p_.nodes.size() - 1);
    }
    Node mk(N k) const { Node n; n.k = k; n.line = tok_.line; return n; }
    void semicolon() {
        if (eat(";")) return;
        if (is("}") || tok_.t == Tk::End || tok_.nl) return;
        fail("esperava ';'" + (tok_.text.empty() ? std::string() : " antes de '" + tok_.text + "'"));
    }
    Func& cur() { return p_.funcs[funcStack_.back()]; }

    i32 statement() {
        if (!ok()) return add(mk(N::Empty));
        if (++depth_, too_deep()) { fail("aninhamento demais"); return add(mk(N::Empty)); }
        const i32 r = statement_inner();
        --depth_;
        return r;
    }

    i32 statement_inner() {
        if (is("{")) return block();
        if (is(";")) { advance(); return add(mk(N::Empty)); }
        if (tok_.t == Tk::Ident) {
            const std::string& w = tok_.text;
            if (w == "var" || w == "let" || w == "const") { const i32 v = var_decl(); semicolon(); return v; }
            if (w == "function") return func_decl();
            if (w == "if") {
                Node n = mk(N::If);
                advance(); expect("("); n.a = expression(); expect(")");
                n.b = statement();
                if (kw("else")) { advance(); n.c = statement(); }
                return add(std::move(n));
            }
            if (w == "while") {
                Node n = mk(N::While);
                advance(); expect("("); n.a = expression(); expect(")");
                n.b = statement();
                return add(std::move(n));
            }
            if (w == "do") {
                Node n = mk(N::DoWhile);
                advance(); n.a = statement();
                if (!kw("while")) { fail("esperava 'while'"); return add(std::move(n)); }
                advance(); expect("("); n.b = expression(); expect(")");
                eat(";");
                return add(std::move(n));
            }
            if (w == "for") return for_stmt();
            if (w == "return") {
                Node n = mk(N::Return);
                advance();
                if (!is(";") && !is("}") && tok_.t != Tk::End && !tok_.nl) n.a = expression();
                semicolon();
                return add(std::move(n));
            }
            if (w == "break" || w == "continue") {
                Node n = mk(w == "break" ? N::Break : N::Continue);
                advance();
                if (tok_.t == Tk::Ident && !tok_.nl) { n.s = tok_.text; advance(); }
                semicolon();
                return add(std::move(n));
            }
            if (w == "throw") {
                Node n = mk(N::Throw);
                advance();
                n.a = expression();
                semicolon();
                return add(std::move(n));
            }
            if (w == "try") {
                Node n = mk(N::Try);
                advance();
                n.a = block();
                if (kw("catch")) {
                    advance(); expect("(");
                    if (tok_.t == Tk::Ident) { n.names.push_back(tok_.text); advance(); } else fail("esperava o nome do catch");
                    expect(")");
                    n.b = block();
                }
                if (kw("finally")) { advance(); n.c = block(); }
                if (n.b < 0 && n.c < 0) fail("try sem catch nem finally");
                return add(std::move(n));
            }
            if (w == "switch") {
                Node n = mk(N::Switch);
                advance(); expect("("); n.a = expression(); expect(")"); expect("{");
                while (ok() && !is("}") && tok_.t != Tk::End) {
                    Node c = mk(N::Case);
                    if (kw("case")) { advance(); c.a = expression(); }
                    else if (kw("default")) advance();
                    else { fail("esperava 'case'"); break; }
                    expect(":");
                    while (ok() && !kw("case") && !kw("default") && !is("}") && tok_.t != Tk::End) c.list.push_back(statement());
                    n.list.push_back(add(std::move(c)));
                }
                expect("}");
                return add(std::move(n));
            }
            if (w == "with") { fail("'with' não é suportado"); return add(mk(N::Empty)); }
        }
        // rótulo "nome:" → ignorado (o laço segue sem rótulo)
        if (tok_.t == Tk::Ident) {
            const Token save = tok_;
            (void)save;
        }
        Node n = mk(N::ExprStmt);
        n.a = expression();
        semicolon();
        return add(std::move(n));
    }

    i32 block() {
        Node n = mk(N::Block);
        expect("{");
        while (ok() && !is("}") && tok_.t != Tk::End) n.list.push_back(statement());
        expect("}");
        return add(std::move(n));
    }

    i32 var_decl() {
        Node n = mk(N::Var);
        advance();
        for (;;) {
            if (tok_.t != Tk::Ident) { fail("esperava o nome da variável"); break; }
            n.names.push_back(tok_.text);
            cur().vars.push_back(tok_.text);
            advance();
            i32 init = -1;
            if (eat("=")) init = assignment();
            n.list.push_back(init);
            if (!eat(",")) break;
        }
        return add(std::move(n));
    }

    i32 for_stmt() {
        const u32 line = tok_.line;
        advance();
        expect("(");
        i32 init = -1;
        // for (var x in obj) / for (x in obj)
        if (kw("var") || kw("let") || kw("const")) {
            advance();
            if (tok_.t != Tk::Ident) { fail("esperava o nome da variável"); return add(mk(N::Empty)); }
            const std::string name = tok_.text;
            cur().vars.push_back(name);
            advance();
            if (kw("in")) {
                advance();
                Node n; n.k = N::ForIn; n.line = line; n.s = name;
                n.b = expression(); expect(")");
                n.c = statement();
                return add(std::move(n));
            }
            Node v; v.k = N::Var; v.line = line;
            v.names.push_back(name);
            i32 e = -1;
            noIn_ = true;
            if (eat("=")) e = assignment();
            v.list.push_back(e);
            while (eat(",")) {
                if (tok_.t != Tk::Ident) { fail("esperava o nome da variável"); break; }
                v.names.push_back(tok_.text);
                cur().vars.push_back(tok_.text);
                advance();
                i32 e2 = -1;
                if (eat("=")) e2 = assignment();
                v.list.push_back(e2);
            }
            noIn_ = false;
            init = add(std::move(v));
        } else if (!is(";")) {
            noIn_ = true;
            const i32 e = expression();
            noIn_ = false;
            if (kw("in") && p_.nodes[static_cast<usize>(e)].k == N::Ident) {
                advance();
                Node n; n.k = N::ForIn; n.line = line; n.s = p_.nodes[static_cast<usize>(e)].s;
                n.b = expression(); expect(")");
                n.c = statement();
                return add(std::move(n));
            }
            Node s; s.k = N::ExprStmt; s.line = line; s.a = e;
            init = add(std::move(s));
        }
        expect(";");
        Node n; n.k = N::For; n.line = line;
        n.a = init;
        if (!is(";")) n.b = expression();
        expect(";");
        if (!is(")")) n.c = expression();
        expect(")");
        n.d = statement();
        return add(std::move(n));
    }

    i32 func_common(bool decl) {
        const u32 line = tok_.line;
        advance();   // function
        Func f;
        f.line = line;
        if (tok_.t == Tk::Ident) { f.name = tok_.text; advance(); }
        else if (decl) fail("função sem nome");
        expect("(");
        while (ok() && !is(")")) {
            if (tok_.t != Tk::Ident) { fail("esperava o nome do parâmetro"); break; }
            f.params.push_back(tok_.text);
            advance();
            if (!eat(",")) break;
        }
        expect(")");
        p_.funcs.push_back(f);
        const u32 fi = static_cast<u32>(p_.funcs.size() - 1);
        funcStack_.push_back(fi);
        const bool savedNoIn = noIn_;
        noIn_ = false;
        const i32 body = block();
        noIn_ = savedNoIn;
        funcStack_.pop_back();
        p_.funcs[fi].body = body;
        Node n; n.k = decl ? N::FuncDecl : N::FuncExpr; n.line = line;
        n.a = static_cast<i32>(fi);
        n.s = p_.funcs[fi].name;
        return add(std::move(n));
    }

    i32 func_decl() {
        const i32 n = func_common(true);
        cur().decls.push_back(n);
        return n;
    }

    i32 expression() {
        i32 e = assignment();
        if (is(",")) {
            Node s = mk(N::Seq);
            s.list.push_back(e);
            while (eat(",")) s.list.push_back(assignment());
            e = add(std::move(s));
        }
        return e;
    }

    static u8 assign_op(const std::string& t) noexcept {
        if (t == "=") return OSet;
        if (t == "+=") return OAddSet;
        if (t == "-=") return OSubSet;
        if (t == "*=") return OMulSet;
        if (t == "/=") return ODivSet;
        if (t == "%=") return OModSet;
        if (t == "&=") return OAndSet;
        if (t == "|=") return OOrSet;
        if (t == "^=") return OXorSet;
        if (t == "<<=") return OShlSet;
        if (t == ">>=") return OShrSet;
        if (t == ">>>=") return OUShrSet;
        return 0;
    }

    i32 assignment() {
        if (++depth_, too_deep()) { fail("aninhamento demais"); --depth_; return add(mk(N::Undef)); }
        const i32 lhs = conditional();
        i32 r = lhs;
        if (tok_.t == Tk::Punct) {
            const u8 op = assign_op(tok_.text);
            if (op) {
                const N k = p_.nodes[static_cast<usize>(lhs)].k;
                if (k != N::Ident && k != N::Member && k != N::Index) fail("lado esquerdo da atribuição inválido");
                Node n = mk(N::Assign);
                n.op = op;
                advance();
                n.a = lhs;
                n.b = assignment();
                r = add(std::move(n));
            }
        }
        --depth_;
        return r;
    }

    i32 conditional() {
        const i32 c = binary(1);
        if (!is("?")) return c;
        Node n = mk(N::Cond);
        advance();
        n.a = c;
        const bool saved = noIn_;
        noIn_ = false;
        n.b = assignment();
        noIn_ = saved;
        expect(":");
        n.c = assignment();
        return add(std::move(n));
    }

    int prec(u8& op, bool& logical) const noexcept {
        logical = false;
        if (tok_.t == Tk::Ident) {
            if (tok_.text == "instanceof") { op = OInstanceOf; return 7; }
            if (tok_.text == "in" && !noIn_) { op = OIn; return 7; }
            return 0;
        }
        if (tok_.t != Tk::Punct) return 0;
        const std::string& t = tok_.text;
        if (t == "||") { op = OOr; logical = true; return 1; }
        if (t == "&&") { op = OAnd; logical = true; return 2; }
        if (t == "|") { op = OBitOr; return 3; }
        if (t == "^") { op = OBitXor; return 4; }
        if (t == "&") { op = OBitAnd; return 5; }
        if (t == "==") { op = OEq; return 6; }
        if (t == "!=") { op = ONe; return 6; }
        if (t == "===") { op = OSEq; return 6; }
        if (t == "!==") { op = OSNe; return 6; }
        if (t == "<") { op = OLt; return 7; }
        if (t == ">") { op = OGt; return 7; }
        if (t == "<=") { op = OLe; return 7; }
        if (t == ">=") { op = OGe; return 7; }
        if (t == "<<") { op = OShl; return 8; }
        if (t == ">>") { op = OShr; return 8; }
        if (t == ">>>") { op = OUShr; return 8; }
        if (t == "+") { op = OAdd; return 9; }
        if (t == "-") { op = OSub; return 9; }
        if (t == "*") { op = OMul; return 10; }
        if (t == "/") { op = ODiv; return 10; }
        if (t == "%") { op = OMod; return 10; }
        return 0;
    }

    i32 binary(int minPrec) {
        i32 lhs = unary();
        for (;;) {
            u8 op = 0;
            bool logical = false;
            const int p = prec(op, logical);
            if (p == 0 || p < minPrec || !ok()) break;
            Node n = mk(logical ? N::Logical : N::Binary);
            n.op = op;
            advance();
            n.a = lhs;
            n.b = binary(p + 1);
            lhs = add(std::move(n));
        }
        return lhs;
    }

    i32 unary() {
        if (++depth_, too_deep()) { fail("aninhamento demais"); --depth_; return add(mk(N::Undef)); }
        i32 r;
        if (tok_.t == Tk::Punct && (tok_.text == "!" || tok_.text == "-" || tok_.text == "+" || tok_.text == "~")) {
            Node n = mk(N::Unary);
            n.op = tok_.text == "!" ? ONot : tok_.text == "-" ? ONeg : tok_.text == "+" ? OPos : OBitNot;
            advance();
            n.a = unary();
            r = add(std::move(n));
        } else if (tok_.t == Tk::Punct && (tok_.text == "++" || tok_.text == "--")) {
            Node n = mk(N::Update);
            n.op = tok_.text == "++" ? OPreInc : OPreDec;
            advance();
            n.a = unary();
            r = add(std::move(n));
        } else if (kw("typeof") || kw("void") || kw("delete")) {
            Node n = mk(N::Unary);
            n.op = kw("typeof") ? OTypeof : kw("void") ? OVoid : ODelete;
            advance();
            n.a = unary();
            r = add(std::move(n));
        } else {
            r = postfix();
        }
        --depth_;
        return r;
    }

    i32 postfix() {
        const i32 e = call_member();
        if (tok_.t == Tk::Punct && !tok_.nl && (tok_.text == "++" || tok_.text == "--")) {
            Node n = mk(N::Update);
            n.op = tok_.text == "++" ? OPostInc : OPostDec;
            advance();
            n.a = e;
            return add(std::move(n));
        }
        return e;
    }

    std::vector<i32> arguments() {
        std::vector<i32> args;
        expect("(");
        const bool saved = noIn_;
        noIn_ = false;
        while (ok() && !is(")")) {
            args.push_back(assignment());
            if (!eat(",")) break;
        }
        noIn_ = saved;
        expect(")");
        return args;
    }

    i32 call_member() {
        i32 e;
        if (kw("new")) {
            Node n = mk(N::New);
            advance();
            // new X.Y.Z(args) — o alvo sem chamadas
            i32 callee = primary();
            for (;;) {
                if (is(".")) {
                    advance();
                    Node m = mk(N::Member);
                    m.a = callee;
                    if (tok_.t != Tk::Ident) { fail("esperava um nome depois de '.'"); break; }
                    m.s = tok_.text;
                    advance();
                    callee = add(std::move(m));
                } else if (is("[")) {
                    advance();
                    Node m = mk(N::Index);
                    m.a = callee;
                    m.b = expression();
                    expect("]");
                    callee = add(std::move(m));
                } else break;
            }
            n.a = callee;
            if (is("(")) n.list = arguments();
            e = add(std::move(n));
        } else {
            e = primary();
        }
        for (;;) {
            if (!ok()) break;
            if (is(".")) {
                advance();
                Node m = mk(N::Member);
                m.a = e;
                if (tok_.t != Tk::Ident) { fail("esperava um nome depois de '.'"); break; }
                m.s = tok_.text;
                advance();
                e = add(std::move(m));
            } else if (is("[")) {
                advance();
                Node m = mk(N::Index);
                m.a = e;
                const bool saved = noIn_;
                noIn_ = false;
                m.b = expression();
                noIn_ = saved;
                expect("]");
                e = add(std::move(m));
            } else if (is("(")) {
                Node c = mk(N::Call);
                c.a = e;
                c.list = arguments();
                e = add(std::move(c));
            } else break;
        }
        return e;
    }

    i32 primary() {
        if (!ok()) return add(mk(N::Undef));
        Node n = mk(N::Undef);
        switch (tok_.t) {
            case Tk::Num: n.k = N::Num; n.num = tok_.num; advance(); return add(std::move(n));
            case Tk::Str: n.k = N::Str; n.s = tok_.text; advance(); return add(std::move(n));
            case Tk::Ident: {
                const std::string w = tok_.text;
                if (w == "function") return func_common(false);
                if (w == "true") { n.k = N::True; advance(); return add(std::move(n)); }
                if (w == "false") { n.k = N::False; advance(); return add(std::move(n)); }
                if (w == "null") { n.k = N::Null; advance(); return add(std::move(n)); }
                if (w == "this") { n.k = N::This; advance(); return add(std::move(n)); }
                n.k = N::Ident;
                n.s = w;
                advance();
                return add(std::move(n));
            }
            case Tk::Punct: {
                if (is("(")) {
                    advance();
                    const bool saved = noIn_;
                    noIn_ = false;
                    const i32 e = expression();
                    noIn_ = saved;
                    expect(")");
                    return e;
                }
                if (is("[")) {
                    advance();
                    n.k = N::ArrayLit;
                    while (ok() && !is("]")) {
                        if (is(",")) { advance(); n.list.push_back(add(mk(N::Undef))); continue; }
                        n.list.push_back(assignment());
                        if (!eat(",")) break;
                    }
                    expect("]");
                    return add(std::move(n));
                }
                if (is("{")) {
                    advance();
                    n.k = N::ObjectLit;
                    while (ok() && !is("}")) {
                        std::string key;
                        if (tok_.t == Tk::Ident || tok_.t == Tk::Str) key = tok_.text;
                        else if (tok_.t == Tk::Num) {
                            char buf[64];
                            std::snprintf(buf, sizeof buf, "%.17g", tok_.num);
                            key = buf;
                        } else { fail("chave de objeto inválida"); break; }
                        advance();
                        expect(":");
                        n.names.push_back(key);
                        n.list.push_back(assignment());
                        if (!eat(",")) break;
                    }
                    expect("}");
                    return add(std::move(n));
                }
                if (is("/") || is("/=")) {
                    const Token r = lex_.regex(tok_.pos, tok_.line);
                    if (r.t == Tk::Error) { fail(lex_.error); return add(std::move(n)); }
                    n.k = N::Regex;
                    n.s = r.text;
                    advance();
                    return add(std::move(n));
                }
                fail("expressão inesperada '" + tok_.text + "'");
                return add(std::move(n));
            }
            case Tk::End: fail("o script terminou no meio de uma expressão"); return add(std::move(n));
            default: fail("expressão inválida"); return add(std::move(n));
        }
    }
};

// =============================================================================
// Números ↔ texto (como o JS)
// =============================================================================
std::string num_to_str(f64 n) {
    if (std::isnan(n)) return "NaN";
    if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
    if (n == 0) return "0";
    if (std::fabs(n) < 1e21 && n == std::floor(n)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", n);
        return buf;
    }
    char buf[40];
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*g", p, n);
        if (std::strtod(buf, nullptr) == n) break;
    }
    // "1e+21" → "1e+21" (JS); "1.5e-07" → "1.5e-7"
    std::string s = buf;
    const usize e = s.find('e');
    if (e != std::string::npos && e + 2 < s.size() && s[e + 2] == '0') s.erase(e + 2, 1);
    return s;
}

f64 str_to_num(const std::string& s) {
    usize b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b == e) return 0.0;
    const std::string t = s.substr(b, e - b);
    if (t == "Infinity" || t == "+Infinity") return INFINITY;
    if (t == "-Infinity") return -INFINITY;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        f64 v = 0;
        for (usize i = 2; i < t.size(); ++i) {
            const int h = hexv(t[i]);
            if (h < 0) return NAN;
            v = v * 16 + h;
        }
        return v;
    }
    char* end = nullptr;
    const f64 v = std::strtod(t.c_str(), &end);
    return end == t.c_str() + t.size() ? v : NAN;
}

i32 to_i32(f64 v) noexcept {
    if (!std::isfinite(v)) return 0;
    const f64 m = std::fmod(std::trunc(v), 4294967296.0);
    const u32 u = static_cast<u32>(static_cast<i64>(m < 0 ? m + 4294967296.0 : m));
    return static_cast<i32>(u);
}
u32 to_u32(f64 v) noexcept { return static_cast<u32>(to_i32(v)); }

// --- ids das funções nativas ----------------------------------------------------
enum Native : u32 {
    NoNative = 0,
    // globais
    NParseInt, NParseFloat, NIsNaN, NIsFinite, NString, NNumber, NBoolean, NArray, NObject, NError, NFunctionCtor,
    NAlert, NWriteln, NWrite, NSleep, NDate, NRegExp, NEval, NEncodeURI, NDecodeURI, NConfirm, NPrompt,
    // Math
    MAbs, MAcos, MAsin, MAtan, MAtan2, MCeil, MCos, MExp, MFloor, MLog, MMax, MMin, MPow, MRandom, MRound, MSin, MSqrt, MTan,
    // String.prototype
    SCharAt, SCharCodeAt, SIndexOf, SLastIndexOf, SSubstring, SSubstr, SSlice, SSplit, SReplace, SToUpper, SToLower,
    SConcat, STrim, SMatch, SSearch, SToString, SFromCharCode, SLocaleCompare,
    // Number.prototype
    NumToFixed, NumToString, NumToPrecision,
    // Array.prototype
    APush, APop, AShift, AUnshift, ASlice, ASplice, AConcat, AJoin, AReverse, ASort, AIndexOf, ALastIndexOf, AToString,
    // Object.prototype / Function.prototype
    OHasOwn, OToString, FCall, FApply,
    // Date
    DGetTime, DZero,
    // RegExp (só relatado), JSON
    RxTest, RxExec, JStringify, JParse,
};

/// Corta o texto em até `n` bytes sem partir um caractere UTF-8 no meio (o
/// relatório atravessa JNI/NSString, que recusam UTF-8 quebrado).
std::string cut_utf8(const std::string& s, usize n) {
    if (s.size() <= n) return s;
    usize e = n;
    while (e > 0 && (static_cast<u8>(s[e]) & 0xC0) == 0x80) --e;
    return s.substr(0, e);
}

// Métodos embutidos por tipo (também achados por Array.prototype.x etc.).
constexpr std::pair<const char*, u32> kStringMethods[] = {
    {"charAt", SCharAt}, {"charCodeAt", SCharCodeAt}, {"indexOf", SIndexOf}, {"lastIndexOf", SLastIndexOf},
    {"substring", SSubstring}, {"substr", SSubstr}, {"slice", SSlice}, {"split", SSplit}, {"replace", SReplace},
    {"toUpperCase", SToUpper}, {"toLowerCase", SToLower}, {"concat", SConcat}, {"trim", STrim},
    {"match", SMatch}, {"search", SSearch}, {"toString", SToString}, {"valueOf", SToString},
    {"localeCompare", SLocaleCompare}};
constexpr std::pair<const char*, u32> kArrayMethods[] = {
    {"push", APush}, {"pop", APop}, {"shift", AShift}, {"unshift", AUnshift}, {"slice", ASlice},
    {"splice", ASplice}, {"concat", AConcat}, {"join", AJoin}, {"reverse", AReverse}, {"sort", ASort},
    {"indexOf", AIndexOf}, {"lastIndexOf", ALastIndexOf}, {"toString", AToString}};
constexpr std::pair<const char*, u32> kNumberMethods[] = {
    {"toFixed", NumToFixed}, {"toString", NumToString}, {"toPrecision", NumToPrecision}, {"valueOf", NumToString}};
constexpr std::pair<const char*, u32> kObjectMethods[] = {{"hasOwnProperty", OHasOwn}, {"toString", OToString}};
constexpr std::pair<const char*, u32> kFunctionMethods[] = {{"call", FCall}, {"apply", FApply}};

/// Método embutido do protótipo do construtor `ctor` ("Array", "String"...); 0 = nenhum.
u32 builtin_method(std::string_view ctor, const std::string& name) noexcept {
    auto find = [&](const auto& table) -> u32 {
        for (const auto& e : table) if (name == e.first) return e.second;
        return 0;
    };
    if (ctor == "String") return find(kStringMethods);
    if (ctor == "Array") return find(kArrayMethods);
    if (ctor == "Number") return find(kNumberMethods);
    if (ctor == "Function") return find(kFunctionMethods);
    if (ctor == "Object") return find(kObjectMethods);
    return 0;
}

} // namespace

// =============================================================================
// Interp: heap e utilidades
// =============================================================================

Interp::Interp(Host& host, ImportReport& report, const ScriptLimits& limits)
    : host_(host), report_(report), limits_(limits) {
    objects_.push_back(std::make_unique<Object>());   // 0: reservado ("sem protótipo")
    global_ = new_object(OK::Plain, "global");
    envs_.push_back(Env{0, {}});
}

Interp::~Interp() = default;

void Interp::account(u64 bytes) {
    heapBytes_ += bytes;
    if (heapBytes_ > limits_.maxHeapBytes && flow_ < 5) abort("memória do script esgotada (limite do sandbox)");
}

u32 Interp::new_object(OK kind, std::string cls) {
    auto o = std::make_unique<Object>();
    o->kind = kind;
    o->cls = std::move(cls);
    objects_.push_back(std::move(o));
    account(sizeof(Object) + 32);
    return static_cast<u32>(objects_.size() - 1);
}

Value Interp::str(std::string s) {
    account(s.size() + sizeof(std::string));
    strings_.push_back(std::move(s));
    Value v;
    v.t = T::Str;
    v.r = static_cast<u32>(strings_.size() - 1);
    return v;
}

Value Interp::array(std::vector<Value> elems) {
    const u32 a = new_object(OK::Array, "Array");
    account(elems.size() * sizeof(Value));
    o(a).elems = std::move(elems);
    return obj(a);
}

Value Interp::plain(std::string cls) { return obj(new_object(OK::Plain, std::move(cls))); }

Value Interp::native_fn(u32 id, std::string cls) {
    const u32 f = new_object(OK::Native, std::move(cls));
    o(f).native = id;
    return obj(f);
}

void Interp::define_global(const std::string& name, const Value& v) { own_set(global_, name, v); }

Value Interp::own(u32 objIndex, const std::string& name, bool* found) const {
    const Object& ob = *objects_[objIndex];
    for (const auto& p : ob.props)
        if (p.first == name) { if (found) *found = true; return p.second; }
    if (found) *found = false;
    return Value::undef();
}

void Interp::own_set(u32 objIndex, const std::string& name, const Value& v) {
    Object& ob = *objects_[objIndex];
    for (auto& p : ob.props)
        if (p.first == name) { p.second = v; return; }
    account(name.size() + sizeof(Value) + 16);
    ob.props.emplace_back(name, v);
}

bool Interp::truthy(const Value& v) const noexcept {
    switch (v.t) {
        case T::Undef: case T::Null: return false;
        case T::Bool: return v.b;
        case T::Num: return v.n != 0 && !std::isnan(v.n);
        case T::Str: return !strings_[v.r].empty();
        case T::Obj: return true;
    }
    return false;
}

f64 Interp::to_num(const Value& v) {
    switch (v.t) {
        case T::Undef: return NAN;
        case T::Null: return 0.0;
        case T::Bool: return v.b ? 1.0 : 0.0;
        case T::Num: return v.n;
        case T::Str: return str_to_num(strings_[v.r]);
        case T::Obj: {
            const Object& ob = o(v);
            if (ob.kind == OK::Array && ob.elems.size() == 1) return to_num(ob.elems[0]);
            if (ob.kind == OK::Array && ob.elems.empty()) return 0.0;
            return NAN;
        }
    }
    return NAN;
}

std::string Interp::to_str(const Value& v) {
    switch (v.t) {
        case T::Undef: return "undefined";
        case T::Null: return "null";
        case T::Bool: return v.b ? "true" : "false";
        case T::Num: return num_to_str(v.n);
        case T::Str: return strings_[v.r];
        case T::Obj: {
            Object& ob = o(v);
            if (ob.kind == OK::Array) {
                std::string s;
                for (usize i = 0; i < ob.elems.size(); ++i) {
                    if (i) s += ",";
                    if (!ob.elems[i].nullish()) s += to_str(ob.elems[i]);
                    if (s.size() > (1u << 20)) break;
                }
                return s;
            }
            if (ob.kind == OK::Error) {
                const Value m = own(v.r, "message");
                const Value n = own(v.r, "name");
                return (n.is_str() ? s(n) : std::string("Error")) + ": " + (m.nullish() ? std::string() : to_str(m));
            }
            if (ob.kind == OK::Function || ob.kind == OK::Native || ob.kind == OK::BoundHost) return "function () { [code] }";
            bool found = false;
            const Value ts = own(v.r, "toString", &found);
            (void)ts;
            return "[object " + ob.cls + "]";
        }
    }
    return {};
}

std::vector<f64> Interp::to_vec(const Value& v) {
    std::vector<f64> out;
    if (v.is_obj() && o(v).kind == OK::Array) {
        for (const Value& e : o(v).elems) out.push_back(to_num(e));
    } else if (!v.nullish()) {
        out.push_back(to_num(v));
    }
    return out;
}

Value Interp::throw_error(const std::string& msg) {
    if (flow_ >= 4) return Value::undef();
    const u32 e = new_object(OK::Error, "Error");
    own_set(e, "message", str(msg));
    own_set(e, "name", str("Error"));
    own_set(e, "line", Value::num(line_));
    thrown_ = obj(e);
    flow_ = 4;
    return Value::undef();
}

void Interp::abort(const std::string& msg) {
    if (flow_ == 5) return;
    report_.skip("script", "sandbox", msg, line_);
    flow_ = 5;
}

void Interp::blocked(const std::string& what) {
    report_.skip("chamada", what, "bloqueado pelo sandbox (arquivo, rede ou sistema): não foi executado", line_);
}
void Interp::unsupported(const std::string& what, const std::string& why) { report_.skip("chamada", what, why, line_); }
void Interp::approx(const std::string& what, const std::string& why) { report_.note("chamada", what, why, line_); }

// =============================================================================
// Avaliador
// =============================================================================
struct Evaluator {
    Interp& in;
    Program& p;

    Object& O(const Value& v) { return in.o(v); }
    bool stop() const noexcept { return in.flow_ != 0; }
    bool fatal() const noexcept { return in.flow_ >= 4; }

    bool tick(u32 line) {
        in.line_ = line;
        volatile char probe = 0;
        const uintptr_t here = reinterpret_cast<uintptr_t>(&probe);
        const uintptr_t used = here > in.stackBase_ ? here - in.stackBase_ : in.stackBase_ - here;
        if (in.stackBase_ && used > Interp::kStackBudget) {
            in.abort("recursão profunda demais (a pilha do sandbox acabou) e o script foi parado");
            return false;
        }
        if (++in.steps_ > in.limits_.maxSteps) {
            in.abort("o script passou do limite de " + std::to_string(in.limits_.maxSteps) +
                     " passos (laço infinito?) e foi parado");
            return false;
        }
        return true;
    }

    // --- ambiente ------------------------------------------------------------------
    Value* lookup(u32 env, const std::string& name) {
        for (u32 e = env;;) {
            for (auto& v : in.envs_[e].vars) if (v.first == name) return &v.second;
            if (e == 0) break;
            e = in.envs_[e].parent;
        }
        return nullptr;
    }
    void declare(u32 env, const std::string& name, const Value& v) {
        for (auto& x : in.envs_[env].vars) if (x.first == name) { x.second = v; return; }
        in.account(name.size() + sizeof(Value) + 16);
        in.envs_[env].vars.emplace_back(name, v);
    }
    bool has_var(u32 env, const std::string& name) {
        for (auto& x : in.envs_[env].vars) if (x.first == name) return true;
        return false;
    }

    // --- objetos -------------------------------------------------------------------
    Value proto_lookup(u32 objIndex, const std::string& name, bool& found) {
        u32 cur = objIndex;
        for (int guard = 0; cur && guard < 64; ++guard) {
            const Value v = in.own(cur, name, &found);
            if (found) return v;
            cur = in.o(cur).proto;
        }
        found = false;
        return Value::undef();
    }

    /// Método que o script pendurou em <ctor>.prototype (ou em Object.prototype)
    /// de um construtor embutido: `String.prototype.trim = function () {...}`.
    Value script_proto(const char* ctor, const std::string& name, bool* hit = nullptr) {
        if (hit) *hit = false;
        for (const char* c : {ctor, "Object"}) {
            bool g = false;
            const Value fn = in.own(in.global_, c, &g);
            if (!g || !fn.is_obj() || O(fn).kind != OK::Native) continue;
            bool pf = false;
            const Value pr = in.own(fn.r, "prototype", &pf);
            if (!pf || !pr.is_obj()) continue;
            bool found = false;
            const Value v = in.own(pr.r, name, &found);
            if (found) { if (hit) *hit = true; return v; }
        }
        return Value::undef();
    }

    Value make_function(i32 fi, u32 env) {
        const u32 f = in.new_object(OK::Function, "Function");
        in.o(f).fn = fi;
        in.o(f).env = env;
        return Interp::obj(f);
    }

    Value bound(u32 id, const Value& self) {
        const Value f = in.native_fn(id);
        O(f).self = self;
        return f;
    }

    Value get(const Value& base, const std::string& name) {
        switch (base.t) {
            case T::Undef: case T::Null:
                return in.throw_error("não dá para ler '" + name + "' de " + in.to_str(base));
            case T::Str: {
                const std::string& s = in.s(base);
                if (name == "length") {
                    // comprimento em caracteres (pontos de código), como o texto do AE
                    u32 n = 0;
                    for (const char c : s) if ((static_cast<u8>(c) & 0xC0) != 0x80) ++n;
                    return Value::num(n);
                }
                for (const auto& e : kStringMethods) if (name == e.first) return bound(e.second, base);
                return script_proto("String", name);
            }
            case T::Num: {
                for (const auto& e : kNumberMethods) if (name == e.first) return bound(e.second, base);
                return script_proto("Number", name);
            }
            case T::Bool:
                if (name == "toString") return bound(SToString, base);
                return script_proto("Boolean", name);
            case T::Obj: break;
        }
        Object& ob = O(base);
        if (ob.kind == OK::Host || ob.kind == OK::BoundHost) {
            bool handled = false;
            const Value v = in.dom ? in.dom->get(in, base.r, name, handled) : Value::undef();
            if (handled || stop()) return v;
        }
        if (ob.kind == OK::Array) {
            if (name == "length") return Value::num(static_cast<f64>(ob.elems.size()));
            const f64 idx = str_to_num(name);
            if (std::isfinite(idx) && idx >= 0 && idx == std::floor(idx) && !name.empty() && name[0] != ' ') {
                const usize i = static_cast<usize>(idx);
                return i < ob.elems.size() ? ob.elems[i] : Value::undef();
            }
            for (const auto& e : kArrayMethods) if (name == e.first) return bound(e.second, base);
        }
        if (ob.kind == OK::Regex && (name == "test" || name == "exec")) return bound(name == "test" ? RxTest : RxExec, base);
        bool found = false;
        const Value v = proto_lookup(base.r, name, found);
        if (found) return v;
        if (ob.kind == OK::Function || ob.kind == OK::Native) {
            if (name == "call") return bound(FCall, base);
            if (name == "apply") return bound(FApply, base);
            if (name == "prototype" && (ob.kind == OK::Function || ob.self.t == T::Undef)) {
                // Construtor embutido (Array, String, Date...): o protótipo é um
                // objeto onde o script pode pendurar métodos (polyfills como
                // `if (!Array.prototype.indexOf) {...}` e o json2); os métodos
                // embutidos também são achados por ele (Array.prototype.slice.call).
                const Value pr = ob.kind == OK::Function ? in.plain() : in.plain("proto:" + ob.cls);
                in.own_set(base.r, "prototype", pr);
                return pr;
            }
            if (name == "length" && ob.kind == OK::Function && ob.fn >= 0)
                return Value::num(static_cast<f64>(p.funcs[static_cast<usize>(ob.fn)].params.size()));
        }
        if (ob.kind == OK::Plain && ob.cls.size() > 6 && ob.cls.compare(0, 6, "proto:") == 0) {
            if (const u32 id = builtin_method(std::string_view(ob.cls).substr(6), name)) return in.native_fn(id);
        }
        if (ob.kind == OK::Plain && ob.cls == "Date" && name == "getTime") return bound(DGetTime, base);
        if (ob.kind == OK::Plain && ob.cls == "Date" && name.size() > 3 && name.compare(0, 3, "get") == 0) return bound(DZero, base);
        {
            // Métodos que o script pendurou no protótipo do construtor embutido.
            const char* ctor = ob.kind == OK::Array ? "Array"
                             : ob.kind == OK::Function || ob.kind == OK::Native || ob.kind == OK::BoundHost ? "Function"
                             : ob.kind == OK::Error ? "Error"
                             : ob.kind == OK::Regex ? "RegExp"
                             : ob.kind == OK::Plain && ob.cls == "Date" ? "Date" : "Object";
            bool hit = false;
            const Value sv = script_proto(ctor, name, &hit);
            if (hit) return sv;
        }
        if (name == "hasOwnProperty") return bound(OHasOwn, base);
        if (name == "toString") return bound(OToString, base);
        return Value::undef();
    }

    void put(const Value& base, const std::string& name, const Value& v) {
        if (!base.is_obj()) {
            if (base.nullish()) in.throw_error("não dá para escrever '" + name + "' em " + in.to_str(base));
            return;   // primitivo: ignorado (como no JS)
        }
        Object& ob = O(base);
        if (ob.kind == OK::Host) {
            if (in.dom && in.dom->set(in, base.r, name, v)) return;
            if (stop()) return;
        }
        if (ob.kind == OK::Array) {
            if (name == "length") {
                const f64 n = in.to_num(v);
                if (n >= 0 && n < 1e7) { in.account(sizeof(Value) * static_cast<u64>(n)); ob.elems.resize(static_cast<usize>(n)); }
                return;
            }
            const f64 idx = str_to_num(name);
            if (std::isfinite(idx) && idx >= 0 && idx == std::floor(idx)) {
                if (idx > 1e7) { in.abort("índice de array grande demais"); return; }
                const usize i = static_cast<usize>(idx);
                if (i >= ob.elems.size()) { in.account(sizeof(Value) * (i + 1 - ob.elems.size())); ob.elems.resize(i + 1); }
                ob.elems[i] = v;
                return;
            }
        }
        in.own_set(base.r, name, v);
    }

    std::string key_of(const Value& k) { return in.to_str(k); }

    Value index_get(const Value& base, const Value& key) {
        if (base.is_obj()) {
            Object& ob = O(base);
            if (ob.kind == OK::Array && key.is_num()) {
                const f64 i = key.n;
                if (i >= 0 && i == std::floor(i)) return static_cast<usize>(i) < ob.elems.size() ? ob.elems[static_cast<usize>(i)] : Value::undef();
            }
            if (ob.kind == OK::Host && in.dom) {
                bool handled = false;
                const Value v = in.dom->index(in, base.r, key, handled);
                if (handled || stop()) return v;
            }
        }
        if (base.is_str() && key.is_num()) {
            // caractere (ponto de código) na posição
            const std::string& s = in.s(base);
            i64 want = static_cast<i64>(key.n), cpi = 0;
            for (usize i = 0; i < s.size();) {
                const u8 c = static_cast<u8>(s[i]);
                const usize n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
                if (cpi == want) return in.str(s.substr(i, n));
                i += n;
                ++cpi;
            }
            return Value::undef();
        }
        return get(base, key_of(key));
    }

    void index_put(const Value& base, const Value& key, const Value& v) {
        if (base.is_obj() && O(base).kind == OK::Array && key.is_num()) {
            const f64 i = key.n;
            if (i >= 0 && i == std::floor(i)) {
                if (i > 1e7) { in.abort("índice de array grande demais"); return; }
                Object& ob = O(base);
                const usize k = static_cast<usize>(i);
                if (k >= ob.elems.size()) { in.account(sizeof(Value) * (k + 1 - ob.elems.size())); ob.elems.resize(k + 1); }
                ob.elems[k] = v;
                return;
            }
        }
        put(base, key_of(key), v);
    }

    // --- operadores ------------------------------------------------------------------
    bool loose_eq(const Value& a, const Value& b) {
        if (a.t == b.t) return strict_eq(a, b);
        if (a.nullish() && b.nullish()) return true;
        if (a.nullish() || b.nullish()) return false;
        if (a.t == T::Obj || b.t == T::Obj) {
            if (a.t == T::Obj && b.t == T::Obj) return a.r == b.r;
            const Value& prim = a.t == T::Obj ? b : a;
            const Value& objv = a.t == T::Obj ? a : b;
            if (prim.is_str()) return in.to_str(objv) == in.s(prim);
            return in.to_num(objv) == in.to_num(prim);
        }
        return in.to_num(a) == in.to_num(b);
    }
    bool strict_eq(const Value& a, const Value& b) {
        if (a.t != b.t) return false;
        switch (a.t) {
            case T::Undef: case T::Null: return true;
            case T::Bool: return a.b == b.b;
            case T::Num: return a.n == b.n;
            case T::Str: return in.s(a) == in.s(b);
            case T::Obj: return a.r == b.r;
        }
        return false;
    }

    std::string type_of(const Value& v) {
        switch (v.t) {
            case T::Undef: return "undefined";
            case T::Null: return "object";
            case T::Bool: return "boolean";
            case T::Num: return "number";
            case T::Str: return "string";
            case T::Obj: {
                const OK k = O(v).kind;
                return k == OK::Function || k == OK::Native || k == OK::BoundHost ? "function" : "object";
            }
        }
        return "undefined";
    }

    bool instance_of(const Value& v, const Value& ctor) {
        if (!ctor.is_obj()) { in.throw_error("instanceof precisa de um construtor"); return false; }
        if (!v.is_obj()) return false;
        const Object& c = O(ctor);
        const Object& x = O(v);
        if (c.kind == OK::Native) {
            if (c.cls == "Object") return true;
            if (c.cls == "Array") return x.kind == OK::Array;
            if (c.cls == "Error") return x.kind == OK::Error;
            if (c.cls == "Function") return x.kind == OK::Function || x.kind == OK::Native;
            return x.cls == c.cls || (c.cls == "AVLayer" && (x.cls == "TextLayer" || x.cls == "ShapeLayer"))
                || (c.cls == "Layer" && x.cls.find("Layer") != std::string::npos)
                || (c.cls == "PropertyBase" && (x.cls == "Property" || x.cls == "PropertyGroup"));
        }
        if (c.kind == OK::Function) {
            bool found = false;
            const Value pr = in.own(ctor.r, "prototype", &found);
            if (!found || !pr.is_obj()) return false;
            u32 cur = x.proto;
            for (int g = 0; cur && g < 64; ++g) {
                if (cur == pr.r) return true;
                cur = in.o(cur).proto;
            }
        }
        return false;
    }

    Value arith(u8 op, const Value& a, const Value& b) {
        if (op == OAdd) {
            const bool sa = a.is_str() || (a.is_obj() && O(a).kind != OK::Plain ? false : a.is_obj());
            if (a.is_str() || b.is_str() || (a.is_obj() && !b.is_num()) || (b.is_obj() && !a.is_num()) || sa) {
                if (a.is_str() || b.is_str() || a.is_obj() || b.is_obj()) return in.str(in.to_str(a) + in.to_str(b));
            }
            return Value::num(in.to_num(a) + in.to_num(b));
        }
        const f64 x = in.to_num(a), y = in.to_num(b);
        switch (op) {
            case OSub: return Value::num(x - y);
            case OMul: return Value::num(x * y);
            case ODiv: return Value::num(x / y);
            case OMod: return Value::num(std::fmod(x, y));
            case OBitAnd: return Value::num(to_i32(x) & to_i32(y));
            case OBitOr: return Value::num(to_i32(x) | to_i32(y));
            case OBitXor: return Value::num(to_i32(x) ^ to_i32(y));
            case OShl: return Value::num(static_cast<i32>(to_u32(x) << (to_u32(y) & 31)));
            case OShr: return Value::num(to_i32(x) >> (to_u32(y) & 31));
            case OUShr: return Value::num(static_cast<f64>(to_u32(x) >> (to_u32(y) & 31)));
            default: return Value::num(NAN);
        }
    }

    Value compare(u8 op, const Value& a, const Value& b) {
        if (a.is_str() && b.is_str()) {
            const int c = in.s(a).compare(in.s(b));
            switch (op) {
                case OLt: return Value::boolean(c < 0);
                case OGt: return Value::boolean(c > 0);
                case OLe: return Value::boolean(c <= 0);
                default: return Value::boolean(c >= 0);
            }
        }
        const f64 x = in.to_num(a), y = in.to_num(b);
        switch (op) {
            case OLt: return Value::boolean(x < y);
            case OGt: return Value::boolean(x > y);
            case OLe: return Value::boolean(x <= y);
            default: return Value::boolean(x >= y);
        }
    }

    Value binary(u8 op, const Value& a, const Value& b) {
        switch (op) {
            case OLt: case OGt: case OLe: case OGe: return compare(op, a, b);
            case OEq: return Value::boolean(loose_eq(a, b));
            case ONe: return Value::boolean(!loose_eq(a, b));
            case OSEq: return Value::boolean(strict_eq(a, b));
            case OSNe: return Value::boolean(!strict_eq(a, b));
            case OInstanceOf: return Value::boolean(instance_of(a, b));
            case OIn: {
                if (!b.is_obj()) return in.throw_error("'in' precisa de um objeto");
                const std::string k = in.to_str(a);
                if (O(b).kind == OK::Array) {
                    const f64 i = str_to_num(k);
                    if (std::isfinite(i) && i >= 0 && static_cast<usize>(i) < O(b).elems.size()) return Value::boolean(true);
                    if (k == "length") return Value::boolean(true);
                }
                bool found = false;
                (void)proto_lookup(b.r, k, found);
                return Value::boolean(found);
            }
            default: return arith(op, a, b);
        }
    }

    // --- chamadas -----------------------------------------------------------------
    Value call_value(const Value& fn, const Value& self, const std::vector<Value>& args, bool construct = false) {
        if (!fn.is_obj()) return in.throw_error("isto não é uma função");
        Object& f = O(fn);
        if (f.kind == OK::Host && in.dom) {
            bool handled = false;
            const Value v = in.dom->call(in, fn.r, "()", args, handled);
            if (handled || stop()) return v;
        }
        if (f.kind == OK::Native) {
            if (f.native >= Interp::kDomNative && in.dom) {
                bool handled = false;
                const Value v = construct ? in.dom->construct(in, f.cls, args, handled)
                                          : in.dom->call(in, fn.r, "()", args, handled);
                if (handled || stop()) return v;
                return in.throw_error("chamada não suportada");
            }
            return native(f.native, f.self.t == T::Undef ? self : f.self, args, construct, fn);
        }
        if (f.kind == OK::BoundHost) {
            bool handled = false;
            const Value v = in.dom ? in.dom->call(in, static_cast<u32>(f.h0), f.method, args, handled) : Value::undef();
            if (handled || stop()) return v;
            in.unsupported(f.method + "()", "método do After Effects sem equivalente no Aurea");
            return Value::undef();
        }
        if (f.kind != OK::Function || f.fn < 0) return in.throw_error("isto não é uma função");
        if (in.depth_ >= in.limits_.maxDepth) {
            in.abort("recursão profunda demais (mais de " + std::to_string(in.limits_.maxDepth) + " chamadas aninhadas)");
            return Value::undef();
        }
        const Func& fd = p.funcs[static_cast<usize>(f.fn)];
        in.envs_.push_back(Interp::Env{f.env, {}});
        in.account(sizeof(Interp::Env) + 64);
        const u32 env = static_cast<u32>(in.envs_.size() - 1);
        for (usize i = 0; i < fd.params.size(); ++i) declare(env, fd.params[i], i < args.size() ? args[i] : Value::undef());
        declare(env, "arguments", in.array(args));
        declare(env, "this", self);
        hoist(fd, env);
        ++in.depth_;
        const u32 savedLine = in.line_;
        exec(fd.body, env);
        --in.depth_;
        in.line_ = savedLine;
        Value r = Value::undef();
        if (in.flow_ == 1) { r = retval_; in.flow_ = 0; retval_ = Value::undef(); }
        else if (in.flow_ == 2 || in.flow_ == 3) in.flow_ = 0;
        return r;
    }

    void hoist(const Func& f, u32 env) {
        for (const std::string& v : f.vars) if (!has_var(env, v)) declare(env, v, Value::undef());
        for (const i32 d : f.decls) {
            const Node& n = p.nodes[static_cast<usize>(d)];
            declare(env, n.s, make_function(n.a, env));
        }
    }

    Value construct(const Value& fn, const std::vector<Value>& args) {
        if (!fn.is_obj()) return in.throw_error("isto não é um construtor");
        Object& f = O(fn);
        if (f.kind == OK::Native) return call_value(fn, Value::undef(), args, true);
        if (f.kind == OK::Host && in.dom) {   // new File(...) etc.: o DOM decide (bloqueia)
            bool handled = false;
            const Value v = in.dom->call(in, fn.r, "()", args, handled);
            if (handled || stop()) return v;
        }
        if (f.kind != OK::Function) return in.throw_error("isto não é um construtor");
        const Value protoV = get(fn, "prototype");
        const Value self = in.plain();
        if (protoV.is_obj()) O(self).proto = protoV.r;
        const Value r = call_value(fn, self, args);
        return r.is_obj() ? r : self;
    }

    // --- biblioteca nativa ----------------------------------------------------------
    f64 arg_num(const std::vector<Value>& a, usize i, f64 def = NAN) { return i < a.size() ? in.to_num(a[i]) : def; }

    static usize utf8_offset(const std::string& s, i64 cp) {
        usize i = 0;
        for (i64 k = 0; k < cp && i < s.size(); ++k) {
            const u8 c = static_cast<u8>(s[i]);
            i += c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        }
        return std::min(i, s.size());
    }
    static i64 utf8_len(const std::string& s) {
        i64 n = 0;
        for (const char c : s) if ((static_cast<u8>(c) & 0xC0) != 0x80) ++n;
        return n;
    }
    static i64 clamp_index(f64 v, i64 len, bool negFromEnd) {
        if (std::isnan(v)) return 0;
        i64 i = static_cast<i64>(std::trunc(std::clamp(v, -1e15, 1e15)));
        if (i < 0) i = negFromEnd ? std::max<i64>(0, len + i) : 0;
        return std::min(i, len);
    }

    // --- JSON (o mínimo para valores comuns: os scripts usam no lugar do json2) ---------
    static void json_quote(std::string& out, const std::string& s) {
        out.push_back('"');
        for (const char ch : s) {
            const u8 c = static_cast<u8>(ch);
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20) {
                        char b[8];
                        std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(c));
                        out += b;
                    } else {
                        out.push_back(ch);
                    }
            }
        }
        out.push_back('"');
    }

    /// Falso = o valor não entra (undefined, função): some do objeto, vira null no array.
    bool json_write(std::string& out, const Value& v, const std::string& gap, const std::string& indent, u32 depth) {
        if (stop()) return false;
        if (depth > 64) { in.throw_error("JSON.stringify: estrutura circular ou funda demais"); return false; }
        if (out.size() > (8u << 20)) { in.abort("texto grande demais"); return false; }
        switch (v.t) {
            case T::Undef: return false;
            case T::Null: out += "null"; return true;
            case T::Bool: out += v.b ? "true" : "false"; return true;
            case T::Num: out += std::isfinite(v.n) ? num_to_str(v.n) : std::string("null"); return true;
            case T::Str: json_quote(out, in.s(v)); return true;
            case T::Obj: break;
        }
        const OK kind = O(v).kind;
        if (kind == OK::Function || kind == OK::Native || kind == OK::BoundHost) return false;
        if (kind == OK::Host) { out += "{}"; return true; }   // objeto do AE: sem campos serializáveis
        if (kind == OK::Plain && O(v).cls == "Date") {
            // As datas do sandbox não têm relógio (getTime() = 0).
            bool hit = false;
            (void)script_proto("Date", "toJSON", &hit);
            if (!hit) { out += "\"1970-01-01T00:00:00.000Z\""; return true; }
        }
        if (kind != OK::Array) {
            // toJSON do script (o dos polyfills de Date, por exemplo).
            const Value tj = get(v, "toJSON");
            if (stop()) return false;
            if (tj.is_obj() && (O(tj).kind == OK::Function || O(tj).kind == OK::Native)) {
                const Value r = call_value(tj, v, {in.str("")});
                if (stop()) return false;
                if (!r.is_obj() || r.r != v.r) return json_write(out, r, gap, indent, depth + 1);
            }
        }
        const std::string inner = indent + gap;
        if (kind == OK::Array) {
            const std::vector<Value> el = O(v).elems;   // cópia: o toJSON pode mexer no array
            if (el.empty()) { out += "[]"; return true; }
            out.push_back('[');
            for (usize i = 0; i < el.size(); ++i) {
                if (i) out.push_back(',');
                if (!gap.empty()) { out.push_back('\n'); out += inner; }
                if (!json_write(out, el[i], gap, inner, depth + 1)) {
                    if (stop()) return false;
                    out += "null";
                }
            }
            if (!gap.empty()) { out.push_back('\n'); out += indent; }
            out.push_back(']');
            return true;
        }
        const auto props = O(v).props;
        out.push_back('{');
        bool any = false;
        for (const auto& pr : props) {
            const usize mark = out.size();
            if (any) out.push_back(',');
            if (!gap.empty()) { out.push_back('\n'); out += inner; }
            json_quote(out, pr.first);
            out += gap.empty() ? ":" : ": ";
            if (!json_write(out, pr.second, gap, inner, depth + 1)) {
                if (stop()) return false;
                out.resize(mark);
                continue;
            }
            any = true;
        }
        if (any && !gap.empty()) { out.push_back('\n'); out += indent; }
        out.push_back('}');
        return true;
    }

    static void json_ws(const std::string& s, usize& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }

    static bool json_hex4(const std::string& s, usize i, u32& cp) {
        if (i + 4 > s.size()) return false;
        cp = 0;
        for (usize k = 0; k < 4; ++k) {
            const int h = hexv(s[i + k]);
            if (h < 0) return false;
            cp = cp * 16 + static_cast<u32>(h);
        }
        return true;
    }

    bool json_string(const std::string& s, usize& i, std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size() && s[i] != '"') {
            const char c = s[i];
            if (static_cast<u8>(c) < 0x20) return false;
            if (c != '\\') { out.push_back(c); ++i; continue; }
            if (i + 1 >= s.size()) return false;
            const char e = s[i + 1];
            i += 2;
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    u32 cp = 0;
                    if (!json_hex4(s, i, cp)) return false;
                    i += 4;
                    // Par substituto do UTF-16 (😀) → um caractere só.
                    u32 lo = 0;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u' &&
                        json_hex4(s, i + 2, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    }

    Value json_value(const std::string& s, usize& i, u32 depth, bool& bad) {
        json_ws(s, i);
        if (bad || stop() || i >= s.size() || depth > 256) { bad = true; return Value::undef(); }
        if (!tick(in.line_)) return Value::undef();
        const char c = s[i];
        if (c == '{') {
            const Value ob = in.plain();
            ++i;
            json_ws(s, i);
            if (i < s.size() && s[i] == '}') { ++i; return ob; }
            for (;;) {
                json_ws(s, i);
                std::string key;
                if (!json_string(s, i, key)) { bad = true; return Value::undef(); }
                json_ws(s, i);
                if (i >= s.size() || s[i] != ':') { bad = true; return Value::undef(); }
                ++i;
                const Value v = json_value(s, i, depth + 1, bad);
                if (bad || stop()) return Value::undef();
                in.own_set(ob.r, key, v);
                json_ws(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return ob; }
                bad = true;
                return Value::undef();
            }
        }
        if (c == '[') {
            std::vector<Value> el;
            ++i;
            json_ws(s, i);
            if (i < s.size() && s[i] == ']') { ++i; return in.array(std::move(el)); }
            for (;;) {
                el.push_back(json_value(s, i, depth + 1, bad));
                if (bad || stop()) return Value::undef();
                json_ws(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return in.array(std::move(el)); }
                bad = true;
                return Value::undef();
            }
        }
        if (c == '"') {
            std::string out;
            if (!json_string(s, i, out)) { bad = true; return Value::undef(); }
            return in.str(std::move(out));
        }
        if (s.compare(i, 4, "true") == 0) { i += 4; return Value::boolean(true); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return Value::boolean(false); }
        if (s.compare(i, 4, "null") == 0) { i += 4; return Value::null(); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const usize b = i;
            while (i < s.size() && (s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
                                    (s[i] >= '0' && s[i] <= '9')))
                ++i;
            const std::string num = s.substr(b, i - b);
            char* end = nullptr;
            const f64 v = std::strtod(num.c_str(), &end);
            if (!end || *end != '\0') { bad = true; return Value::undef(); }
            return Value::num(v);
        }
        bad = true;
        return Value::undef();
    }

    Value native(u32 id, const Value& self, const std::vector<Value>& a, bool construct, const Value& callee) {
        (void)callee;
        switch (id) {
            case NParseInt: {
                const std::string s = a.empty() ? std::string() : in.to_str(a[0]);
                const int radix = a.size() > 1 ? static_cast<int>(in.to_num(a[1])) : 10;
                char* end = nullptr;
                const long long v = std::strtoll(s.c_str(), &end, radix >= 2 && radix <= 36 ? radix : 10);
                return end == s.c_str() ? Value::num(NAN) : Value::num(static_cast<f64>(v));
            }
            case NParseFloat: {
                const std::string s = a.empty() ? std::string() : in.to_str(a[0]);
                char* end = nullptr;
                const f64 v = std::strtod(s.c_str(), &end);
                return end == s.c_str() ? Value::num(NAN) : Value::num(v);
            }
            case NIsNaN: return Value::boolean(std::isnan(arg_num(a, 0)));
            case NIsFinite: return Value::boolean(std::isfinite(arg_num(a, 0)));
            case NString: return in.str(a.empty() ? std::string() : in.to_str(a[0]));
            case NNumber: return Value::num(a.empty() ? 0.0 : in.to_num(a[0]));
            case NBoolean: return Value::boolean(!a.empty() && in.truthy(a[0]));
            case NArray: {
                if (a.size() == 1 && a[0].is_num()) {
                    const f64 n = a[0].n;
                    if (n < 0 || n > 1e7 || n != std::floor(n)) return in.throw_error("tamanho de array inválido");
                    return in.array(std::vector<Value>(static_cast<usize>(n)));
                }
                return in.array(a);
            }
            case NObject: return a.empty() || !a[0].is_obj() ? in.plain() : a[0];
            case NError: {
                const u32 e = in.new_object(OK::Error, "Error");
                in.own_set(e, "message", in.str(a.empty() ? std::string() : in.to_str(a[0])));
                in.own_set(e, "name", in.str("Error"));
                in.own_set(e, "line", Value::num(in.line_));
                return Interp::obj(e);
            }
            case NFunctionCtor: case NEval:
                in.unsupported(id == NEval ? "eval()" : "new Function()", "código montado em texto não é executado");
                return Value::undef();
            case NAlert: case NConfirm: case NPrompt: {
                const std::string msg = a.empty() ? std::string() : in.to_str(a[0]);
                in.report_.note("script", id == NAlert ? "alert" : id == NConfirm ? "confirm" : "prompt",
                                "mensagem do script: " + cut_utf8(msg, 200), in.line_);
                if (id == NConfirm) return Value::boolean(true);
                if (id == NPrompt) return a.size() > 1 ? a[1] : in.str("");
                return Value::undef();
            }
            case NWriteln: case NWrite: case NSleep: return Value::undef();
            case NDate: {
                const Value d = in.plain("Date");
                if (!construct) return in.str("Thu Jan 1 1970 00:00:00 GMT");
                return d;
            }
            case DGetTime: case DZero: return Value::num(0);
            case NRegExp:
                in.unsupported("RegExp", "expressões regulares não são suportadas");
                return in.plain("RegExp");
            case NEncodeURI: case NDecodeURI: return a.empty() ? in.str("") : in.str(in.to_str(a[0]));
            // Math
            case MAbs: return Value::num(std::fabs(arg_num(a, 0)));
            case MAcos: return Value::num(std::acos(arg_num(a, 0)));
            case MAsin: return Value::num(std::asin(arg_num(a, 0)));
            case MAtan: return Value::num(std::atan(arg_num(a, 0)));
            case MAtan2: return Value::num(std::atan2(arg_num(a, 0), arg_num(a, 1)));
            case MCeil: return Value::num(std::ceil(arg_num(a, 0)));
            case MCos: return Value::num(std::cos(arg_num(a, 0)));
            case MExp: return Value::num(std::exp(arg_num(a, 0)));
            case MFloor: return Value::num(std::floor(arg_num(a, 0)));
            case MLog: return Value::num(std::log(arg_num(a, 0)));
            case MMax: case MMin: {
                f64 r = id == MMax ? -INFINITY : INFINITY;
                for (const Value& v : a) {
                    const f64 x = in.to_num(v);
                    if (std::isnan(x)) return Value::num(NAN);
                    r = id == MMax ? std::max(r, x) : std::min(r, x);
                }
                return Value::num(r);
            }
            case MPow: return Value::num(std::pow(arg_num(a, 0), arg_num(a, 1)));
            case MRandom: {
                // xorshift64* com semente fixa: o mesmo script dá o mesmo resultado.
                u64 x = in.rng_;
                x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
                in.rng_ = x;
                return Value::num(static_cast<f64>((x * 2685821657736338717ull) >> 11) / 9007199254740992.0);
            }
            case MRound: return Value::num(std::floor(arg_num(a, 0) + 0.5));
            case MSin: return Value::num(std::sin(arg_num(a, 0)));
            case MSqrt: return Value::num(std::sqrt(arg_num(a, 0)));
            case MTan: return Value::num(std::tan(arg_num(a, 0)));
            default: break;
        }
        // --- métodos de texto ---
        if (id >= SCharAt && id <= SLocaleCompare) {
            if (id == SFromCharCode) {
                std::string s;
                for (const Value& v : a) put_utf8(s, static_cast<u32>(in.to_num(v)) & 0x1FFFFF);
                return in.str(s);
            }
            const std::string s = in.to_str(self);
            const i64 len = utf8_len(s);
            switch (id) {
                case SCharAt: {
                    const i64 i = static_cast<i64>(arg_num(a, 0, 0));
                    if (i < 0 || i >= len) return in.str("");
                    const usize b = utf8_offset(s, i), e = utf8_offset(s, i + 1);
                    return in.str(s.substr(b, e - b));
                }
                case SCharCodeAt: {
                    const i64 i = static_cast<i64>(arg_num(a, 0, 0));
                    if (i < 0 || i >= len) return Value::num(NAN);
                    const usize b = utf8_offset(s, i);
                    const u8 c = static_cast<u8>(s[b]);
                    u32 cp = c;
                    if (c >= 0xC0 && b + 1 < s.size()) {
                        const usize n = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
                        cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
                        for (usize k = 1; k < n && b + k < s.size(); ++k) cp = (cp << 6) | (static_cast<u8>(s[b + k]) & 0x3F);
                    }
                    return Value::num(cp);
                }
                case SIndexOf: case SLastIndexOf: {
                    const std::string needle = a.empty() ? std::string("undefined") : in.to_str(a[0]);
                    usize r;
                    if (id == SIndexOf) r = s.find(needle, utf8_offset(s, static_cast<i64>(arg_num(a, 1, 0))));
                    else r = s.rfind(needle);
                    if (r == std::string::npos) return Value::num(-1);
                    return Value::num(static_cast<f64>(utf8_len(s.substr(0, r))));
                }
                case SSubstring: {
                    i64 b = clamp_index(arg_num(a, 0, 0), len, false), e = a.size() > 1 && !a[1].nullish() ? clamp_index(arg_num(a, 1), len, false) : len;
                    if (b > e) std::swap(b, e);
                    return in.str(s.substr(utf8_offset(s, b), utf8_offset(s, e) - utf8_offset(s, b)));
                }
                case SSubstr: {
                    const i64 b = clamp_index(arg_num(a, 0, 0), len, true);
                    const i64 n = a.size() > 1 && !a[1].nullish() ? std::max<i64>(0, static_cast<i64>(arg_num(a, 1))) : len;
                    const i64 e = std::min(len, b + n);
                    return in.str(s.substr(utf8_offset(s, b), utf8_offset(s, e) - utf8_offset(s, b)));
                }
                case SSlice: {
                    const i64 b = clamp_index(arg_num(a, 0, 0), len, true);
                    const i64 e = a.size() > 1 && !a[1].nullish() ? clamp_index(arg_num(a, 1), len, true) : len;
                    if (e <= b) return in.str("");
                    return in.str(s.substr(utf8_offset(s, b), utf8_offset(s, e) - utf8_offset(s, b)));
                }
                case SSplit: {
                    std::vector<Value> out;
                    if (a.empty() || a[0].nullish()) { out.push_back(in.str(s)); return in.array(out); }
                    if (a[0].is_obj() && O(a[0]).cls == "RegExp") {
                        in.unsupported("split(/regex/)", "expressões regulares não são suportadas");
                        out.push_back(in.str(s));
                        return in.array(out);
                    }
                    const std::string sep = in.to_str(a[0]);
                    if (sep.empty()) {
                        for (i64 i = 0; i < len; ++i) {
                            const usize b = utf8_offset(s, i), e = utf8_offset(s, i + 1);
                            out.push_back(in.str(s.substr(b, e - b)));
                        }
                    } else {
                        usize pos = 0;
                        for (;;) {
                            const usize f = s.find(sep, pos);
                            out.push_back(in.str(s.substr(pos, f == std::string::npos ? std::string::npos : f - pos)));
                            if (f == std::string::npos || out.size() > 1000000) break;
                            pos = f + sep.size();
                        }
                    }
                    return in.array(out);
                }
                case SReplace: {
                    if (!a.empty() && a[0].is_obj() && O(a[0]).cls == "RegExp") {
                        in.unsupported("replace(/regex/)", "expressões regulares não são suportadas: o texto ficou igual");
                        return in.str(s);
                    }
                    const std::string from = a.empty() ? std::string("undefined") : in.to_str(a[0]);
                    std::string to;
                    if (a.size() > 1 && a[1].is_obj() && (O(a[1]).kind == OK::Function || O(a[1]).kind == OK::Native)) {
                        const usize f = s.find(from);
                        if (f == std::string::npos) return in.str(s);
                        to = in.to_str(call_value(a[1], Value::undef(), {in.str(from), Value::num(static_cast<f64>(f)), in.str(s)}));
                        return in.str(s.substr(0, f) + to + s.substr(f + from.size()));
                    }
                    to = a.size() > 1 ? in.to_str(a[1]) : std::string("undefined");
                    const usize f = s.find(from);
                    if (f == std::string::npos) return in.str(s);
                    return in.str(s.substr(0, f) + to + s.substr(f + from.size()));
                }
                case SToUpper: case SToLower: {
                    std::string r = s;
                    for (char& c : r) {
                        if (id == SToUpper && c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
                        if (id == SToLower && c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
                    }
                    return in.str(r);
                }
                case SConcat: {
                    std::string r = s;
                    for (const Value& v : a) r += in.to_str(v);
                    return in.str(r);
                }
                case STrim: {
                    usize b = 0, e = s.size();
                    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
                    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
                    return in.str(s.substr(b, e - b));
                }
                case SMatch: case SSearch:
                    in.unsupported(id == SMatch ? "match()" : "search()", "expressões regulares não são suportadas");
                    return id == SMatch ? Value::null() : Value::num(-1);
                case SToString: return self.is_str() ? self : in.str(s);
                case SLocaleCompare: {
                    const std::string o2 = a.empty() ? std::string() : in.to_str(a[0]);
                    return Value::num(s < o2 ? -1 : s > o2 ? 1 : 0);
                }
                default: break;
            }
        }
        // --- números ---
        if (id == NumToFixed || id == NumToString || id == NumToPrecision) {
            const f64 x = in.to_num(self);
            if (id == NumToFixed) {
                const int d = std::clamp(static_cast<int>(arg_num(a, 0, 0)), 0, 20);
                char buf[64];
                std::snprintf(buf, sizeof buf, "%.*f", d, x);
                return in.str(buf);
            }
            if (id == NumToPrecision) {
                const int d = std::clamp(static_cast<int>(arg_num(a, 0, 6)), 1, 21);
                char buf[64];
                std::snprintf(buf, sizeof buf, "%.*g", d, x);
                return in.str(buf);
            }
            const int radix = a.empty() ? 10 : static_cast<int>(arg_num(a, 0, 10));
            if (radix == 16 || radix == 2 || radix == 8) {
                u64 v = static_cast<u64>(std::fabs(std::trunc(x)));
                std::string r;
                do { r.insert(r.begin(), "0123456789abcdef"[v % static_cast<u64>(radix)]); v /= static_cast<u64>(radix); } while (v);
                if (x < 0) r.insert(r.begin(), '-');
                return in.str(r);
            }
            return in.str(num_to_str(x));
        }
        // --- arrays ---
        if (id >= APush && id <= AToString) {
            if (!self.is_obj() || O(self).kind != OK::Array) return in.throw_error("método de array em algo que não é array");
            auto& el = O(self).elems;
            switch (id) {
                case APush:
                    in.account(sizeof(Value) * a.size());
                    for (const Value& v : a) el.push_back(v);
                    return Value::num(static_cast<f64>(el.size()));
                case APop: {
                    if (el.empty()) return Value::undef();
                    const Value v = el.back();
                    el.pop_back();
                    return v;
                }
                case AShift: {
                    if (el.empty()) return Value::undef();
                    const Value v = el.front();
                    el.erase(el.begin());
                    return v;
                }
                case AUnshift:
                    in.account(sizeof(Value) * a.size());
                    el.insert(el.begin(), a.begin(), a.end());
                    return Value::num(static_cast<f64>(el.size()));
                case ASlice: {
                    const i64 len = static_cast<i64>(el.size());
                    const i64 b = clamp_index(arg_num(a, 0, 0), len, true);
                    const i64 e = a.size() > 1 && !a[1].nullish() ? clamp_index(arg_num(a, 1), len, true) : len;
                    std::vector<Value> out;
                    for (i64 i = b; i < e; ++i) out.push_back(el[static_cast<usize>(i)]);
                    return in.array(out);
                }
                case ASplice: {
                    const i64 len = static_cast<i64>(el.size());
                    const i64 b = clamp_index(arg_num(a, 0, 0), len, true);
                    const i64 n = a.size() > 1 ? std::clamp<i64>(static_cast<i64>(arg_num(a, 1, 0)), 0, len - b) : len - b;
                    std::vector<Value> removed(el.begin() + b, el.begin() + b + n);
                    el.erase(el.begin() + b, el.begin() + b + n);
                    if (a.size() > 2) { in.account(sizeof(Value) * (a.size() - 2)); el.insert(el.begin() + b, a.begin() + 2, a.end()); }
                    return in.array(removed);
                }
                case AConcat: {
                    std::vector<Value> out = el;
                    for (const Value& v : a) {
                        if (v.is_obj() && O(v).kind == OK::Array) { const auto& x = O(v).elems; out.insert(out.end(), x.begin(), x.end()); }
                        else out.push_back(v);
                    }
                    return in.array(out);
                }
                case AJoin: case AToString: {
                    const std::string sep = id == AJoin && !a.empty() && !a[0].nullish() ? in.to_str(a[0]) : std::string(",");
                    std::string r;
                    for (usize i = 0; i < el.size(); ++i) {
                        if (i) r += sep;
                        if (!el[i].nullish()) r += in.to_str(el[i]);
                        if (r.size() > (4u << 20)) { in.abort("texto grande demais"); break; }
                    }
                    return in.str(r);
                }
                case AReverse: std::reverse(el.begin(), el.end()); return self;
                case ASort: {
                    const Value cmp = a.empty() ? Value::undef() : a[0];
                    std::vector<Value> copy = el;
                    // Inserção estável (pequeno e sem exceção; o limite de passos vale no comparador).
                    for (usize i = 1; i < copy.size() && !stop(); ++i) {
                        Value x = copy[i];
                        usize j = i;
                        while (j > 0 && !stop()) {
                            bool greater;
                            if (cmp.is_obj()) greater = in.to_num(call_value(cmp, Value::undef(), {copy[j - 1], x})) > 0;
                            else if (copy[j - 1].is_num() && x.is_num() && false) greater = copy[j - 1].n > x.n;
                            else greater = in.to_str(copy[j - 1]) > in.to_str(x);
                            if (!greater) break;
                            copy[j] = copy[j - 1];
                            --j;
                            if (!tick(in.line_)) break;
                        }
                        copy[j] = x;
                    }
                    if (!stop()) O(self).elems = copy;
                    return self;
                }
                case AIndexOf: case ALastIndexOf: {
                    const Value want = a.empty() ? Value::undef() : a[0];
                    if (id == AIndexOf) {
                        for (usize i = 0; i < el.size(); ++i) if (strict_eq(el[i], want)) return Value::num(static_cast<f64>(i));
                    } else {
                        for (usize i = el.size(); i-- > 0;) if (strict_eq(el[i], want)) return Value::num(static_cast<f64>(i));
                    }
                    return Value::num(-1);
                }
                default: break;
            }
        }
        if (id == RxTest || id == RxExec) {
            const Value src = self.is_obj() ? in.own(self.r, "source") : Value::undef();
            in.unsupported("/" + (src.is_str() ? cut_utf8(in.s(src), 60) : std::string()) + "/." + (id == RxTest ? "test()" : "exec()"),
                           "expressões regulares não são suportadas");
            return id == RxTest ? Value::boolean(false) : Value::null();
        }
        if (id == JStringify) {
            if (a.size() > 1 && a[1].is_obj()) in.approx("JSON.stringify()", "o filtro (replacer) do JSON.stringify foi ignorado");
            std::string gap;
            if (a.size() > 2 && a[2].is_num()) gap.assign(static_cast<usize>(std::clamp(a[2].n, 0.0, 10.0)), ' ');
            else if (a.size() > 2 && a[2].is_str()) gap = cut_utf8(in.s(a[2]), 10);
            std::string out;
            if (a.empty() || !json_write(out, a[0], gap, std::string(), 0)) return Value::undef();
            return in.str(std::move(out));
        }
        if (id == JParse) {
            if (a.size() > 1 && a[1].is_obj()) in.approx("JSON.parse()", "a função de reconstrução (reviver) do JSON.parse foi ignorada");
            const std::string text = a.empty() ? std::string("undefined") : in.to_str(a[0]);
            usize i = 0;
            bool bad = false;
            const Value v = json_value(text, i, 0, bad);
            if (stop()) return Value::undef();
            json_ws(text, i);
            if (bad || i != text.size()) return in.throw_error("JSON.parse: texto inválido na posição " + std::to_string(i));
            return v;
        }
        if (id == OHasOwn) {
            if (!self.is_obj() || a.empty()) return Value::boolean(false);
            const std::string k = in.to_str(a[0]);
            if (O(self).kind == OK::Array) {
                const f64 i = str_to_num(k);
                if (std::isfinite(i) && i >= 0 && static_cast<usize>(i) < O(self).elems.size()) return Value::boolean(true);
            }
            bool found = false;
            (void)in.own(self.r, k, &found);
            return Value::boolean(found);
        }
        if (id == OToString) return in.str(self.is_obj() ? "[object " + O(self).cls + "]" : in.to_str(self));
        if (id == FCall || id == FApply) {
            const Value thisArg = a.empty() ? Value::undef() : a[0];
            std::vector<Value> rest;
            if (id == FCall) rest.assign(a.begin() + (a.empty() ? 0 : 1), a.end());
            else if (a.size() > 1 && a[1].is_obj() && O(a[1]).kind == OK::Array) rest = O(a[1]).elems;
            return call_value(self, thisArg, rest);
        }
        return in.throw_error("função embutida desconhecida");
    }

    // --- expressões ------------------------------------------------------------------
    Value retval_;

    Value eval(i32 ni, u32 env) {
        if (ni < 0) return Value::undef();
        const Node& n = p.nodes[static_cast<usize>(ni)];
        if (!tick(n.line) || stop()) return Value::undef();
        switch (n.k) {
            case N::Num: return Value::num(n.num);
            case N::Str: return in.str(n.s);
            case N::True: return Value::boolean(true);
            case N::False: return Value::boolean(false);
            case N::Null: return Value::null();
            case N::Undef: return Value::undef();
            case N::This: {
                if (Value* v = lookup(env, "this")) return *v;
                return in.global_object();
            }
            case N::Ident: {
                if (Value* v = lookup(env, n.s)) return *v;
                bool found = false;
                const Value g = in.own(in.global_, n.s, &found);
                if (found) return g;
                if (n.s == "undefined") return Value::undef();
                if (n.s == "NaN") return Value::num(NAN);
                if (n.s == "Infinity") return Value::num(INFINITY);
                return in.throw_error("'" + n.s + "' não está definido");
            }
            case N::Regex: {
                // Só é relatada quando USADA (test/exec/replace/split/match):
                // bibliotecas como o json2 declaram várias e não usam.
                const Value r = in.plain("RegExp");
                O(r).kind = OK::Regex;
                in.own_set(r.r, "source", in.str(n.s));
                return r;
            }
            case N::ArrayLit: {
                std::vector<Value> el;
                for (const i32 c : n.list) { el.push_back(eval(c, env)); if (stop()) return Value::undef(); }
                return in.array(std::move(el));
            }
            case N::ObjectLit: {
                const Value ob = in.plain();
                for (usize i = 0; i < n.list.size(); ++i) {
                    const Value v = eval(n.list[i], env);
                    if (stop()) return Value::undef();
                    in.own_set(ob.r, n.names[i], v);
                }
                return ob;
            }
            case N::FuncExpr: {
                const Value f = make_function(n.a, env);
                if (!n.s.empty()) {
                    // função nomeada: o nome vale dentro dela
                    in.envs_.push_back(Interp::Env{env, {}});
                    const u32 e2 = static_cast<u32>(in.envs_.size() - 1);
                    declare(e2, n.s, f);
                    O(f).env = e2;
                }
                return f;
            }
            case N::Unary: {
                if (n.op == OTypeof && p.nodes[static_cast<usize>(n.a)].k == N::Ident) {
                    const std::string& nm = p.nodes[static_cast<usize>(n.a)].s;
                    if (Value* v = lookup(env, nm)) return in.str(type_of(*v));
                    bool found = false;
                    const Value g = in.own(in.global_, nm, &found);
                    return in.str(found ? type_of(g) : "undefined");
                }
                if (n.op == ODelete) {
                    const Node& t = p.nodes[static_cast<usize>(n.a)];
                    if (t.k == N::Member || t.k == N::Index) {
                        const Value base = eval(t.a, env);
                        const std::string key = t.k == N::Member ? t.s : in.to_str(eval(t.b, env));
                        if (base.is_obj()) {
                            auto& props = O(base).props;
                            props.erase(std::remove_if(props.begin(), props.end(), [&](const auto& x) { return x.first == key; }), props.end());
                        }
                    }
                    return Value::boolean(true);
                }
                const Value v = eval(n.a, env);
                if (stop()) return Value::undef();
                switch (n.op) {
                    case ONeg: return Value::num(-in.to_num(v));
                    case OPos: return Value::num(in.to_num(v));
                    case ONot: return Value::boolean(!in.truthy(v));
                    case OBitNot: return Value::num(~to_i32(in.to_num(v)));
                    case OTypeof: return in.str(type_of(v));
                    case OVoid: return Value::undef();
                    default: return Value::undef();
                }
            }
            case N::Update: {
                const Value old = eval(n.a, env);
                if (stop()) return Value::undef();
                const f64 x = in.to_num(old);
                const f64 nv = (n.op == OPreInc || n.op == OPostInc) ? x + 1 : x - 1;
                assign_to(n.a, Value::num(nv), env);
                return Value::num(n.op == OPreInc || n.op == OPreDec ? nv : x);
            }
            case N::Binary: {
                const Value a = eval(n.a, env);
                if (stop()) return Value::undef();
                const Value b = eval(n.b, env);
                if (stop()) return Value::undef();
                return binary(n.op, a, b);
            }
            case N::Logical: {
                const Value a = eval(n.a, env);
                if (stop()) return Value::undef();
                if (n.op == OAnd) return in.truthy(a) ? eval(n.b, env) : a;
                return in.truthy(a) ? a : eval(n.b, env);
            }
            case N::Cond: {
                const Value c = eval(n.a, env);
                if (stop()) return Value::undef();
                return eval(in.truthy(c) ? n.b : n.c, env);
            }
            case N::Assign: {
                if (n.op == OSet) {
                    const Value v = eval(n.b, env);
                    if (stop()) return Value::undef();
                    assign_to(n.a, v, env);
                    return v;
                }
                const Value cur = eval(n.a, env);
                if (stop()) return Value::undef();
                const Value rhs = eval(n.b, env);
                if (stop()) return Value::undef();
                static const u8 base[] = {0, OAdd, OSub, OMul, ODiv, OMod, OBitAnd, OBitOr, OBitXor, OShl, OShr, OUShr};
                const u8 op = base[std::min<usize>(n.op - OSet, 11)];
                const Value v = arith(op, cur, rhs);
                assign_to(n.a, v, env);
                return v;
            }
            case N::Member: {
                const Value base = eval(n.a, env);
                if (stop()) return Value::undef();
                return get(base, n.s);
            }
            case N::Index: {
                const Value base = eval(n.a, env);
                if (stop()) return Value::undef();
                const Value key = eval(n.b, env);
                if (stop()) return Value::undef();
                return index_get(base, key);
            }
            case N::Call: return eval_call(n, env);
            case N::New: {
                const Value f = eval(n.a, env);
                if (stop()) return Value::undef();
                std::vector<Value> args;
                for (const i32 c : n.list) { args.push_back(eval(c, env)); if (stop()) return Value::undef(); }
                in.line_ = n.line;
                return construct(f, args);
            }
            case N::Seq: {
                Value v;
                for (const i32 c : n.list) { v = eval(c, env); if (stop()) break; }
                return v;
            }
            default: return Value::undef();
        }
    }

    Value eval_call(const Node& n, u32 env) {
        const Node& callee = p.nodes[static_cast<usize>(n.a)];
        Value self = Value::undef();
        Value fn;
        std::string name;
        bool hostMethod = false;
        Value hostObj;
        if (callee.k == N::Member || callee.k == N::Index) {
            self = eval(callee.a, env);
            if (stop()) return Value::undef();
            if (callee.k == N::Member) name = callee.s;
            else {
                const Value k = eval(callee.b, env);
                if (stop()) return Value::undef();
                name = in.to_str(k);
            }
            if (self.is_obj() && O(self).kind == OK::Host) { hostMethod = true; hostObj = self; }
            else fn = get(self, name);
        } else {
            fn = eval(n.a, env);
            name = callee.k == N::Ident ? callee.s : std::string("função");
        }
        if (stop()) return Value::undef();
        std::vector<Value> args;
        for (const i32 c : n.list) { args.push_back(eval(c, env)); if (stop()) return Value::undef(); }
        in.line_ = n.line;
        if (hostMethod) {
            bool handled = false;
            const Value v = in.dom ? in.dom->call(in, hostObj.r, name, args, handled) : Value::undef();
            if (handled || stop()) return v;
            // atributo que guarda uma função comum
            fn = get(hostObj, name);
            if (stop()) return Value::undef();
            if (!fn.is_obj()) {
                in.unsupported(O(hostObj).cls + "." + name + "()", "método do After Effects sem equivalente no Aurea");
                return Value::undef();
            }
        }
        if (!fn.is_obj()) return in.throw_error("'" + name + "' não é uma função");
        return call_value(fn, self, args);
    }

    void assign_to(i32 target, const Value& v, u32 env) {
        const Node& t = p.nodes[static_cast<usize>(target)];
        if (t.k == N::Ident) {
            if (Value* slot = lookup(env, t.s)) { *slot = v; return; }
            in.own_set(in.global_, t.s, v);   // variável implícita (sem var): global
            return;
        }
        if (t.k == N::Member) {
            const Value base = eval(t.a, env);
            if (stop()) return;
            put(base, t.s, v);
            return;
        }
        if (t.k == N::Index) {
            const Value base = eval(t.a, env);
            if (stop()) return;
            const Value key = eval(t.b, env);
            if (stop()) return;
            index_put(base, key, v);
        }
    }

    // --- comandos ---------------------------------------------------------------------
    void exec(i32 si, u32 env) {
        if (si < 0 || stop()) return;
        const Node& n = p.nodes[static_cast<usize>(si)];
        if (!tick(n.line)) return;
        switch (n.k) {
            case N::Block:
                for (const i32 c : n.list) { exec(c, env); if (stop()) return; }
                return;
            case N::Var:
                for (usize i = 0; i < n.names.size(); ++i) {
                    if (n.list[i] < 0) { if (!has_var(env, n.names[i]) && !lookup(env, n.names[i])) declare(env, n.names[i], Value::undef()); continue; }
                    const Value v = eval(n.list[i], env);
                    if (stop()) return;
                    if (Value* slot = lookup(env, n.names[i])) *slot = v;
                    else declare(env, n.names[i], v);
                }
                return;
            case N::ExprStmt: (void)eval(n.a, env); return;
            case N::If: {
                const Value c = eval(n.a, env);
                if (stop()) return;
                exec(in.truthy(c) ? n.b : n.c, env);
                return;
            }
            case N::While:
                for (;;) {
                    const Value c = eval(n.a, env);
                    if (stop() || !in.truthy(c)) return;
                    exec(n.b, env);
                    if (!loop_flow()) return;
                }
            case N::DoWhile:
                for (;;) {
                    exec(n.a, env);
                    if (!loop_flow()) return;
                    const Value c = eval(n.b, env);
                    if (stop() || !in.truthy(c)) return;
                }
            case N::For:
                exec(n.a, env);
                for (;;) {
                    if (stop()) return;
                    if (n.b >= 0) {
                        const Value c = eval(n.b, env);
                        if (stop() || !in.truthy(c)) return;
                    }
                    exec(n.d, env);
                    if (!loop_flow()) return;
                    (void)eval(n.c, env);
                }
            case N::ForIn: {
                const Value ob = eval(n.b, env);
                if (stop() || !ob.is_obj()) return;
                std::vector<std::string> keys;
                Object& o = O(ob);
                if (o.kind == OK::Array) for (usize i = 0; i < o.elems.size(); ++i) keys.push_back(std::to_string(i));
                for (const auto& pr : o.props) keys.push_back(pr.first);
                for (const std::string& k : keys) {
                    const Value kv = in.str(k);
                    if (Value* slot = lookup(env, n.s)) *slot = kv; else declare(env, n.s, kv);
                    exec(n.c, env);
                    if (!loop_flow()) return;
                }
                return;
            }
            case N::Return:
                retval_ = n.a >= 0 ? eval(n.a, env) : Value::undef();
                if (!fatal()) in.flow_ = 1;
                return;
            case N::Break: in.flow_ = 2; return;
            case N::Continue: in.flow_ = 3; return;
            case N::Throw: {
                const Value v = eval(n.a, env);
                if (stop()) return;
                in.thrown_ = v;
                in.flow_ = 4;
                return;
            }
            case N::Try: {
                exec(n.a, env);
                if (in.flow_ == 4 && n.b >= 0) {
                    const Value err = in.thrown_;
                    in.flow_ = 0;
                    in.thrown_ = Value::undef();
                    in.envs_.push_back(Interp::Env{env, {}});
                    const u32 ce = static_cast<u32>(in.envs_.size() - 1);
                    if (!n.names.empty()) declare(ce, n.names[0], err);
                    exec(n.b, ce);
                }
                if (n.c >= 0 && in.flow_ != 5) {
                    const u8 saved = in.flow_;
                    const Value savedThrown = in.thrown_;
                    const Value savedRet = retval_;
                    in.flow_ = 0;
                    exec(n.c, env);
                    if (in.flow_ == 0) { in.flow_ = saved; in.thrown_ = savedThrown; retval_ = savedRet; }
                }
                return;
            }
            case N::Switch: {
                const Value d = eval(n.a, env);
                if (stop()) return;
                i32 start = -1, def = -1;
                for (usize i = 0; i < n.list.size() && start < 0; ++i) {
                    const Node& c = p.nodes[static_cast<usize>(n.list[i])];
                    if (c.a < 0) { def = static_cast<i32>(i); continue; }
                    const Value v = eval(c.a, env);
                    if (stop()) return;
                    if (strict_eq(d, v)) start = static_cast<i32>(i);
                }
                if (start < 0) start = def;
                if (start < 0) return;
                for (usize i = static_cast<usize>(start); i < n.list.size(); ++i) {
                    const Node& c = p.nodes[static_cast<usize>(n.list[i])];
                    for (const i32 s : c.list) {
                        exec(s, env);
                        if (stop()) {
                            if (in.flow_ == 2) in.flow_ = 0;
                            return;
                        }
                    }
                }
                return;
            }
            case N::FuncDecl: return;   // içada na entrada da função
            case N::Empty: return;
            default: (void)eval(si, env); return;
        }
    }

    /// Depois do corpo de um laço: true = continua o laço.
    bool loop_flow() {
        if (in.flow_ == 2) { in.flow_ = 0; return false; }
        if (in.flow_ == 3) { in.flow_ = 0; return true; }
        return in.flow_ == 0;
    }
};

// =============================================================================
// Execução
// =============================================================================

Value Interp::get(const Value& objv, const std::string& name) {
    Evaluator ev{*this, *program_};
    return ev.get(objv, name);
}

void Interp::put(const Value& objv, const std::string& name, const Value& v) {
    Evaluator ev{*this, *program_};
    ev.put(objv, name, v);
}

Value Interp::call(const Value& fn, const Value& self, const std::vector<Value>& args) {
    Evaluator ev{*this, *program_};
    return ev.call_value(fn, self, args);
}

bool Interp::run(std::string_view source, Dom& d) {
    dom = &d;
    if (source.size() > limits_.maxSourceBytes) {
        report_.skip("script", "fonte", "script grande demais para o sandbox");
        return false;
    }
    program_ = std::make_unique<Program>();
    Parser parser(source, *program_);
    const bool parsed = parser.parse();
    for (const auto& dir : parser.directives()) {
        if (dir.second.find("#include") == 0 || dir.second.find("#includepath") == 0)
            report_.skip("chamada", cut_utf8(dir.second, 120), "bloqueado pelo sandbox (inclui outro arquivo): não foi executado", dir.first);
    }
    if (!parsed) {
        report_.skip("script", "sintaxe", parser.error, parser.errLine);
        report_.message = "Erro de sintaxe no script (linha " + std::to_string(parser.errLine) + "): " + parser.error;
        return false;
    }
    // Globais da linguagem.
    struct G { const char* name; u32 id; const char* cls; };
    static const G kGlobals[] = {
        {"parseInt", NParseInt, "Function"}, {"parseFloat", NParseFloat, "Function"}, {"isNaN", NIsNaN, "Function"},
        {"isFinite", NIsFinite, "Function"}, {"String", NString, "String"}, {"Number", NNumber, "Number"},
        {"Boolean", NBoolean, "Boolean"}, {"Array", NArray, "Array"}, {"Object", NObject, "Object"},
        {"Error", NError, "Error"}, {"TypeError", NError, "Error"}, {"RangeError", NError, "Error"},
        {"Function", NFunctionCtor, "Function"}, {"alert", NAlert, "Function"}, {"confirm", NConfirm, "Function"},
        {"prompt", NPrompt, "Function"}, {"Date", NDate, "Date"}, {"RegExp", NRegExp, "RegExp"}, {"eval", NEval, "Function"},
        {"encodeURI", NEncodeURI, "Function"}, {"decodeURI", NDecodeURI, "Function"},
        {"encodeURIComponent", NEncodeURI, "Function"}, {"decodeURIComponent", NDecodeURI, "Function"},
    };
    for (const G& g : kGlobals) define_global(g.name, native_fn(g.id, g.cls));
    {
        const Value math = plain("Math");
        static const std::pair<const char*, u32> kMath[] = {
            {"abs", MAbs}, {"acos", MAcos}, {"asin", MAsin}, {"atan", MAtan}, {"atan2", MAtan2}, {"ceil", MCeil},
            {"cos", MCos}, {"exp", MExp}, {"floor", MFloor}, {"log", MLog}, {"max", MMax}, {"min", MMin}, {"pow", MPow},
            {"random", MRandom}, {"round", MRound}, {"sin", MSin}, {"sqrt", MSqrt}, {"tan", MTan}};
        for (const auto& m : kMath) own_set(math.r, m.first, native_fn(m.second));
        own_set(math.r, "PI", Value::num(3.14159265358979323846));
        own_set(math.r, "E", Value::num(2.71828182845904523536));
        own_set(math.r, "LN2", Value::num(0.69314718055994530942));
        own_set(math.r, "LN10", Value::num(2.30258509299404568402));
        own_set(math.r, "LOG2E", Value::num(1.44269504088896340736));
        own_set(math.r, "LOG10E", Value::num(0.43429448190325182765));
        own_set(math.r, "SQRT2", Value::num(1.41421356237309504880));
        own_set(math.r, "SQRT1_2", Value::num(0.70710678118654752440));
        define_global("Math", math);
        const Value str = get(global_object(), "String");
        own_set(str.r, "fromCharCode", native_fn(SFromCharCode));
        // JSON do ES5 (o ExtendScript não tem; os scripts trazem o json2, que
        // pula a própria definição quando JSON.stringify já existe).
        const Value json = plain("JSON");
        own_set(json.r, "stringify", native_fn(JStringify));
        own_set(json.r, "parse", native_fn(JParse));
        define_global("JSON", json);
    }
    d.install(*this);
    Evaluator ev{*this, *program_};
    volatile char probe = 0;
    stackBase_ = reinterpret_cast<uintptr_t>(&probe);
    ev.hoist(program_->funcs[0], 0);
    ev.declare(0, "this", global_object());
    ev.exec(program_->funcs[0].body, 0);
    bool ok = true;
    if (flow_ == 4) {
        const std::string msg = to_str(thrown_);
        u32 errLine = line_;
        if (thrown_.is_obj()) {
            bool found = false;
            const Value l = own(thrown_.r, "line", &found);
            if (found && l.is_num()) errLine = static_cast<u32>(l.n);
        }
        report_.skip("script", "erro", "o script parou com um erro não tratado: " + cut_utf8(msg, 300), errLine);
        report_.message = "O script parou na linha " + std::to_string(errLine) + ": " + cut_utf8(msg, 300);
        ok = false;
    } else if (flow_ == 5) {
        report_.message = "O script foi parado pelo sandbox na linha " + std::to_string(line_) + ".";
        ok = false;
    }
    const u8 savedFlow = flow_;
    flow_ = 0;   // o DOM ainda grava as expressões pendentes
    d.finish(*this);
    flow_ = savedFlow;
    return ok;
}

} // namespace aurea::ae::script
