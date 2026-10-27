// =============================================================================
//  0.3.4 plugins: léxico e sintaxe do AureaScript (um subconjunto do ES2020).
//
//  Descida recursiva clássica com precedência por níveis. Toda recusa diz a
//  linha, a coluna e o que faltou, em português (é o que o autor do plugin vê
//  no console do app). O que o JavaScript tem e o AureaScript não (class,
//  async/await, regex, import...) é recusado com uma mensagem própria, nunca
//  com um "token inesperado" genérico.
// =============================================================================
#include "ScriptAst.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace aurea::plugins::script {

namespace {

constexpr int kMaxNesting = 160;    ///< quadros do analisador ativos (parênteses, blocos, funções)
constexpr u32 kMaxAstDepth = 320;   ///< profundidade da árvore (a avaliação é recursiva)

struct ParseFail {
    std::string message;
    u32 line = 0;
    u32 col = 0;
};

enum class T : u8 { End, Num, Str, Template, Ident, Punct };

struct TemplatePart {
    bool isExpr = false;
    std::string text;   ///< texto pronto (cooked) ou o código da expressão
    u32 line = 0;
    u32 col = 0;
};

struct Token {
    T type = T::End;
    std::string text;
    f64 num = 0.0;
    u32 line = 1;
    u32 col = 1;
    bool nl = false;    ///< houve quebra de linha antes
    std::vector<TemplatePart> parts;
};

bool ident_start(unsigned char c) { return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
bool ident_part(unsigned char c) { return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80; }

void append_utf8(std::string& out, u32 cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// -----------------------------------------------------------------------------
// Léxico
// -----------------------------------------------------------------------------
class Lexer {
public:
    Lexer(std::string_view s, u32 line, u32 col) : s_(s), line_(line), col_(col) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        bool nl = false;
        for (;;) {
            nl = skip_space() || nl;
            Token t;
            t.line = line_;
            t.col = col_;
            t.nl = nl;
            nl = false;
            if (i_ >= s_.size()) { t.type = T::End; out.push_back(std::move(t)); return out; }
            const unsigned char c = static_cast<unsigned char>(s_[i_]);
            if (std::isdigit(c) || (c == '.' && i_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_ + 1])))) {
                number(t);
            } else if (c == '"' || c == '\'') {
                string(t, static_cast<char>(c));
            } else if (c == '`') {
                templ(t);
            } else if (ident_start(c)) {
                t.type = T::Ident;
                const usize start = i_;
                while (i_ < s_.size() && ident_part(static_cast<unsigned char>(s_[i_]))) adv();
                t.text.assign(s_.substr(start, i_ - start));
            } else {
                punct(t);
            }
            out.push_back(std::move(t));
            if (out.size() > 2'000'000) fail("arquivo grande demais");
        }
    }

private:
    [[noreturn]] void fail(std::string m) { throw ParseFail{std::move(m), line_, col_}; }
    void adv() {
        if (s_[i_] == '\n') { ++line_; col_ = 1; }
        else ++col_;
        ++i_;
    }
    bool skip_space() {
        bool nl = false;
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == '\n') { nl = true; adv(); }
            else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') adv();
            else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
                while (i_ < s_.size() && s_[i_] != '\n') adv();
            } else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '*') {
                adv(); adv();
                bool closed = false;
                while (i_ < s_.size()) {
                    if (s_[i_] == '*' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') { adv(); adv(); closed = true; break; }
                    if (s_[i_] == '\n') nl = true;
                    adv();
                }
                if (!closed) fail("comentario /* sem */");
            } else if (static_cast<unsigned char>(c) == 0xEF && i_ + 2 < s_.size()
                       && static_cast<unsigned char>(s_[i_ + 1]) == 0xBB && static_cast<unsigned char>(s_[i_ + 2]) == 0xBF) {
                i_ += 3;   // BOM
            } else break;
        }
        return nl;
    }
    void number(Token& t) {
        t.type = T::Num;
        std::string digits;
        auto take_digits = [&](auto ok) {
            while (i_ < s_.size() && (ok(static_cast<unsigned char>(s_[i_])) || s_[i_] == '_')) {
                if (s_[i_] != '_') digits += s_[i_];
                adv();
            }
        };
        if (s_[i_] == '0' && i_ + 1 < s_.size() && std::strchr("xXbBoO", s_[i_ + 1])) {
            const char kind = static_cast<char>(std::tolower(static_cast<unsigned char>(s_[i_ + 1])));
            adv(); adv();
            const int base = kind == 'x' ? 16 : kind == 'b' ? 2 : 8;
            take_digits([&](unsigned char c) { return std::isxdigit(c) != 0; });
            if (digits.empty()) fail("numero incompleto");
            f64 v = 0.0;
            for (const char d : digits) {
                const int x = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : std::tolower(static_cast<unsigned char>(d)) - 'a' + 10;
                if (x >= base) fail("digito invalido no numero");
                v = v * base + x;
            }
            t.num = v;
        } else {
            take_digits([](unsigned char c) { return std::isdigit(c) != 0; });
            if (i_ < s_.size() && s_[i_] == '.') { digits += '.'; adv(); take_digits([](unsigned char c) { return std::isdigit(c) != 0; }); }
            if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
                digits += 'e'; adv();
                if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) { digits += s_[i_]; adv(); }
                const usize before = digits.size();
                take_digits([](unsigned char c) { return std::isdigit(c) != 0; });
                if (digits.size() == before) fail("expoente sem digitos");
            }
            t.num = std::strtod(digits.c_str(), nullptr);
        }
        if (i_ < s_.size() && ident_start(static_cast<unsigned char>(s_[i_]))) {
            if (s_[i_] == 'n') fail("BigInt (123n) nao existe no AureaScript");
            fail("numero colado em um nome");
        }
    }
    u32 hex_digits(usize count) {
        u32 v = 0;
        for (usize k = 0; k < count; ++k) {
            if (i_ >= s_.size() || !std::isxdigit(static_cast<unsigned char>(s_[i_]))) fail("escape \\x/\\u invalido");
            const char d = s_[i_];
            v = v * 16 + static_cast<u32>(std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : std::tolower(static_cast<unsigned char>(d)) - 'a' + 10);
            adv();
        }
        return v;
    }
    /// Um escape depois da barra (o `\` já consumido).
    void escape(std::string& out) {
        if (i_ >= s_.size()) fail("texto sem fim");
        const char e = s_[i_];
        adv();
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'v': out += '\v'; break;
            case '0': out += '\0'; break;
            case '\n': break;   // continuação de linha
            case '\r': if (i_ < s_.size() && s_[i_] == '\n') adv(); break;
            case 'x': append_utf8(out, hex_digits(2)); break;
            case 'u': {
                u32 cp = 0;
                if (i_ < s_.size() && s_[i_] == '{') {
                    adv();
                    while (i_ < s_.size() && s_[i_] != '}') cp = cp * 16 + hex_digits(1);
                    if (i_ >= s_.size()) fail("escape \\u{...} sem }");
                    adv();
                } else {
                    cp = hex_digits(4);
                    // Par substituto UTF-16 escrito como dois \u.
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        adv(); adv();
                        const u32 lo = hex_digits(4);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
                if (cp > 0x10FFFF) fail("codigo \\u fora do Unicode");
                append_utf8(out, cp);
                break;
            }
            default: out += e; break;
        }
    }
    void string(Token& t, char quote) {
        t.type = T::Str;
        adv();
        for (;;) {
            if (i_ >= s_.size() || s_[i_] == '\n') fail("texto sem as aspas de fechar");
            const char c = s_[i_];
            if (c == quote) { adv(); return; }
            if (c == '\\') { adv(); escape(t.text); continue; }
            t.text += c;
            adv();
        }
    }
    void templ(Token& t) {
        t.type = T::Template;
        adv();
        TemplatePart part;
        part.line = line_;
        part.col = col_;
        for (;;) {
            if (i_ >= s_.size()) fail("template `...` sem a crase de fechar");
            const char c = s_[i_];
            if (c == '`') { adv(); t.parts.push_back(std::move(part)); return; }
            if (c == '\\') { adv(); escape(part.text); continue; }
            if (c == '$' && i_ + 1 < s_.size() && s_[i_ + 1] == '{') {
                adv(); adv();
                t.parts.push_back(std::move(part));
                part = TemplatePart{};
                TemplatePart expr;
                expr.isExpr = true;
                expr.line = line_;
                expr.col = col_;
                // Até a } que fecha, pulando textos e chaves aninhadas.
                int depth = 0;
                const usize start = i_;
                for (;;) {
                    if (i_ >= s_.size()) fail("${ sem } no template");
                    const char d = s_[i_];
                    if (d == '"' || d == '\'') {
                        const char q = d;
                        adv();
                        while (i_ < s_.size() && s_[i_] != q) { if (s_[i_] == '\\') adv(); if (i_ < s_.size()) adv(); }
                        if (i_ < s_.size()) adv();
                        continue;
                    }
                    if (d == '`') {
                        // template aninhado: até a crase de fechar (sem ${ ` } dentro de ${})
                        adv();
                        while (i_ < s_.size() && s_[i_] != '`') { if (s_[i_] == '\\') adv(); if (i_ < s_.size()) adv(); }
                        if (i_ < s_.size()) adv();
                        continue;
                    }
                    if (d == '{') ++depth;
                    if (d == '}') {
                        if (depth == 0) break;
                        --depth;
                    }
                    adv();
                }
                expr.text.assign(s_.substr(start, i_ - start));
                adv();   // }
                t.parts.push_back(std::move(expr));
                part.line = line_;
                part.col = col_;
                continue;
            }
            part.text += c;
            adv();
        }
    }
    void punct(Token& t) {
        static const char* const kPuncts[] = {
            ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "?\?=",
            "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=",
            "&=", "|=", "^=", "**", "<<", ">>",
            "{", "}", "(", ")", "[", "]", ";", ",", "<", ">", "+", "-", "*", "/", "%", "&", "|", "^", "!",
            "~", "?", ":", "=", ".", "@", "#",
        };
        for (const char* p : kPuncts) {
            const usize n = std::strlen(p);
            if (s_.substr(i_, n) == p) {
                // `?.5` é o ternário com .5, não encadeamento opcional.
                if (n == 2 && p[0] == '?' && p[1] == '.' && i_ + 2 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_ + 2]))) continue;
                if (p[0] == '@' || p[0] == '#') fail(std::string("caractere '") + p + "' nao e aceito no AureaScript");
                t.type = T::Punct;
                t.text = p;
                for (usize k = 0; k < n; ++k) adv();
                return;
            }
        }
        fail(std::string("caractere inesperado '") + s_[i_] + "'");
    }

    std::string_view s_;
    usize i_ = 0;
    u32 line_;
    u32 col_;
};

// -----------------------------------------------------------------------------
// Sintaxe
// -----------------------------------------------------------------------------
bool reserved(std::string_view w) {
    static const char* const kWords[] = {
        "var", "let", "const", "function", "return", "if", "else", "for", "while", "do", "break", "continue",
        "new", "delete", "typeof", "void", "in", "instanceof", "this", "null", "true", "false", "throw", "try",
        "catch", "finally", "switch", "case", "default", "class", "import", "export", "extends", "super",
        "with", "yield", "await", "debugger", "enum",
    };
    for (const char* k : kWords) if (w == k) return true;
    return false;
}

class Parser {
public:
    Parser(Context& ctx, Program& prog, std::vector<Token> toks) : ctx_(ctx), prog_(prog), toks_(std::move(toks)) {}

    const Node* program() {
        Node* root = make(N::Program, peek());
        while (peek().type != T::End) root->kids.push_back(statement());
        return root;
    }
    const Node* lone_expression() {
        const Node* e = expression();
        if (peek().type != T::End) fail_at(peek(), "sobrou codigo depois da expressao do template");
        return e;
    }

private:
    // --- tokens ---
    const Token& peek(usize k = 0) const { return toks_[std::min(p_ + k, toks_.size() - 1)]; }
    const Token& next() { const Token& t = toks_[p_]; if (p_ + 1 < toks_.size()) ++p_; return t; }
    bool punct(std::string_view s, usize k = 0) const { const Token& t = peek(k); return t.type == T::Punct && t.text == s; }
    bool word(std::string_view s, usize k = 0) const { const Token& t = peek(k); return t.type == T::Ident && t.text == s; }
    bool eat(std::string_view s) { if (punct(s)) { next(); return true; } return false; }
    bool eat_word(std::string_view s) { if (word(s)) { next(); return true; } return false; }
    [[noreturn]] void fail_at(const Token& t, std::string m) { throw ParseFail{std::move(m), t.line, t.col}; }
    std::string describe(const Token& t) const {
        switch (t.type) {
            case T::End: return "fim do arquivo";
            case T::Num: return "numero";
            case T::Str: return "texto";
            case T::Template: return "template";
            default: return "'" + t.text + "'";
        }
    }
    void expect(std::string_view s) {
        if (!eat(s)) fail_at(peek(), "esperava '" + std::string(s) + "' e veio " + describe(peek()));
    }
    void semicolon() {
        if (eat(";")) return;
        if (punct("}") || peek().type == T::End || peek().nl) return;
        fail_at(peek(), "esperava ';' ou fim de linha e veio " + describe(peek()));
    }
    Node* make(N kind, const Token& at) {
        Node& n = prog_.nodes.emplace_back();
        n.kind = kind;
        n.line = at.line;
        n.col = at.col;
        return &n;
    }
    const std::string* ident_name(const Token& t) {
        if (t.type != T::Ident) fail_at(t, "esperava um nome e veio " + describe(t));
        if (reserved(t.text)) unsupported_or(t, "'" + t.text + "' e palavra reservada e nao pode ser nome");
        return ctx_.intern(t.text);
    }
    [[noreturn]] void unsupported_or(const Token& t, std::string fallback) {
        static const char* const kNo[][2] = {
            {"class", "class nao existe no AureaScript: use funcoes e objetos"},
            {"import", "import/export nao existem: o plugin tem um arquivo de script so"},
            {"export", "import/export nao existem: o plugin tem um arquivo de script so"},
            {"async", "async/await nao existem: fetch e as outras funcoes do AUREA ja esperam a resposta"},
            {"await", "async/await nao existem: fetch e as outras funcoes do AUREA ja esperam a resposta"},
            {"yield", "geradores (function*/yield) nao existem no AureaScript"},
            {"with", "with nao existe no AureaScript"},
            {"debugger", "debugger nao existe: use console.log"},
            {"super", "super nao existe no AureaScript"},
            {"extends", "class/extends nao existem no AureaScript"},
        };
        for (const auto& row : kNo) if (t.text == row[0]) fail_at(t, row[1]);
        fail_at(t, std::move(fallback));
    }
    struct Nest {
        Parser& p;
        explicit Nest(Parser& pp, const Token& at) : p(pp) {
            if (++p.nesting_ > kMaxNesting) p.fail_at(at, "codigo aninhado demais (divida em funcoes menores)");
        }
        ~Nest() { --p.nesting_; }
    };

    // --- comandos ---
    const Node* statement() {
        const Token& t = peek();
        Nest guard(*this, t);
        if (t.type == T::Punct) {
            if (t.text == "{") return block();
            if (t.text == ";") { next(); return make(N::Empty, t); }
        }
        if (t.type == T::Ident) {
            const std::string& w = t.text;
            if (w == "var" || w == "let" || w == "const") { const Node* d = var_decl(); semicolon(); return d; }
            if (w == "function") return function(true);
            if (w == "if") return if_stmt();
            if (w == "for") return for_stmt();
            if (w == "while") {
                Node* n = make(N::While, next());
                expect("(");
                n->a = expression();
                expect(")");
                n->b = statement();
                return n;
            }
            if (w == "do") {
                Node* n = make(N::DoWhile, next());
                n->b = statement();
                if (!eat_word("while")) fail_at(peek(), "do { ... } precisa de while (...)");
                expect("(");
                n->a = expression();
                expect(")");
                eat(";");
                return n;
            }
            if (w == "return") {
                Node* n = make(N::Return, next());
                if (!punct(";") && !punct("}") && peek().type != T::End && !peek().nl) n->a = expression();
                semicolon();
                return n;
            }
            if (w == "break" || w == "continue") {
                Node* n = make(w == "break" ? N::Break : N::Continue, next());
                if (peek().type == T::Ident && !peek().nl) fail_at(peek(), "rotulos (break nome) nao existem no AureaScript");
                semicolon();
                return n;
            }
            if (w == "throw") {
                Node* n = make(N::Throw, next());
                if (peek().nl) fail_at(peek(), "throw precisa do valor na mesma linha");
                n->a = expression();
                semicolon();
                return n;
            }
            if (w == "try") return try_stmt();
            if (w == "switch") return switch_stmt();
            if (w == "class" || w == "import" || w == "export" || w == "with" || w == "debugger" || w == "yield")
                unsupported_or(t, "");
            if (w == "async" && (word("function", 1) || punct("(", 1))) unsupported_or(t, "");
            if (peek(1).type == T::Punct && peek(1).text == ":" && !reserved(w))
                fail_at(t, "rotulos (nome:) nao existem no AureaScript");
        }
        Node* n = make(N::ExprStmt, t);
        n->a = expression();
        semicolon();
        return n;
    }
    const Node* block() {
        Node* n = make(N::Block, next());   // {
        while (!punct("}")) {
            if (peek().type == T::End) fail_at(peek(), "bloco { sem }");
            n->kids.push_back(statement());
        }
        next();
        return n;
    }
    /// var/let/const com um ou mais declaradores.
    const Node* var_decl() {
        const Token& kw = next();
        Node* n = make(N::Var, kw);
        if (kw.text == "const") n->flags |= kFlagConst;
        do {
            Node* d = make(N::Decl, peek());
            d->a = pattern(false);
            if (eat("=")) d->b = assignment();
            else if (n->flags & kFlagConst) fail_at(peek(), "const precisa de valor (const x = ...)");
            else if (d->a->kind != N::PatIdent) fail_at(peek(), "desestruturacao precisa de valor (= ...)");
            n->kids.push_back(d);
        } while (eat(","));
        return n;
    }
    /// Nome, {a, b: c, ...r} ou [x, , y = 1, ...r]. `allowDefault`: o `= valor`
    /// logo depois é o padrão do parâmetro/elemento (falso numa declaração,
    /// onde o `=` é o valor da variável).
    const Node* pattern(bool allowDefault = true) {
        const Token& t = peek();
        Nest guard(*this, t);
        if (punct("{")) {
            next();
            Node* n = make(N::PatObject, t);
            while (!punct("}")) {
                if (punct("...")) {
                    Node* r = make(N::PatRest, next());
                    r->a = pattern_ident();
                    n->kids.push_back(r);
                    break;
                }
                Node* prop = make(N::PatProp, peek());
                const Token& key = next();
                if (key.type == T::Ident || key.type == T::Str) prop->text = key.text;
                else if (key.type == T::Num) prop->text = Context::number_text(key.num);
                else fail_at(key, "esperava o nome da propriedade na desestruturacao");
                if (eat(":")) prop->a = pattern();
                else {
                    if (key.type != T::Ident) fail_at(key, "use { \"chave\": nome } para chaves que nao sao nomes");
                    Node* id = make(N::PatIdent, key);
                    id->name = ident_name(key);
                    if (eat("=")) id->a = assignment();
                    prop->a = id;
                }
                n->kids.push_back(prop);
                if (!eat(",")) break;
            }
            expect("}");
            return allowDefault ? with_default(n) : n;
        }
        if (punct("[")) {
            next();
            Node* n = make(N::PatArray, t);
            while (!punct("]")) {
                if (punct(",")) { next(); n->kids.push_back(make(N::Hole, t)); continue; }
                if (punct("...")) {
                    Node* r = make(N::PatRest, next());
                    r->a = pattern();
                    n->kids.push_back(r);
                    break;
                }
                n->kids.push_back(pattern());
                if (!eat(",")) break;
            }
            expect("]");
            return allowDefault ? with_default(n) : n;
        }
        return pattern_ident(allowDefault);
    }
    const Node* with_default(Node* n) {
        if (punct("=") && !punct("==")) {
            // padrão com valor padrão: guardado em `b` (só faz sentido em parâmetros/elementos)
            next();
            n->b = assignment();
        }
        return n;
    }
    Node* pattern_ident(bool allowDefault = true) {
        const Token& t = next();
        Node* id = make(N::PatIdent, t);
        id->name = ident_name(t);
        if (allowDefault && punct("=")) { next(); id->a = assignment(); }
        return id;
    }
    const Node* if_stmt() {
        Node* n = make(N::If, next());
        expect("(");
        n->a = expression();
        expect(")");
        n->b = statement();
        if (eat_word("else")) n->c = statement();
        return n;
    }
    const Node* for_stmt() {
        const Token& kw = next();
        if (word("await")) unsupported_or(peek(), "");
        expect("(");
        // for (let x of/in ...) | for (x of/in ...)
        if ((word("let") || word("const") || word("var")) && (word("of", 2) || word("in", 2) || punct("{", 1) || punct("[", 1))) {
            const usize save = p_;
            const Token& decl = next();
            const Node* pat = pattern(false);
            if (word("of") || word("in")) {
                const bool of = next().text == "of";
                Node* n = make(of ? N::ForOf : N::ForIn, kw);
                n->flags |= kFlagDeclares;
                if (decl.text == "const") n->flags |= kFlagConst;
                n->a = pat;
                n->b = of ? assignment() : expression();
                expect(")");
                n->c = statement();
                return n;
            }
            p_ = save;
        }
        if (peek().type == T::Ident && !reserved(peek().text) && (word("of", 1) || word("in", 1))) {
            const Token& name = next();
            const bool of = next().text == "of";
            Node* n = make(of ? N::ForOf : N::ForIn, kw);
            Node* target = make(N::PatIdent, name);
            target->name = ident_name(name);
            n->a = target;
            n->b = of ? assignment() : expression();
            expect(")");
            n->c = statement();
            return n;
        }
        Node* n = make(N::For, kw);
        if (!punct(";")) {
            const bool savedNoIn = noIn_;
            noIn_ = true;
            if (word("let") || word("const") || word("var")) n->a = var_decl();
            else {
                Node* e = make(N::ExprStmt, peek());
                e->a = expression();
                n->a = e;
            }
            noIn_ = savedNoIn;
        }
        expect(";");
        if (!punct(";")) n->b = expression();
        expect(";");
        if (!punct(")")) n->c = expression();
        expect(")");
        n->d = statement();
        return n;
    }
    const Node* try_stmt() {
        Node* n = make(N::Try, next());
        if (!punct("{")) fail_at(peek(), "try precisa de um bloco { }");
        n->a = block();
        if (eat_word("catch")) {
            if (eat("(")) {
                n->b = pattern(false);
                expect(")");
            }
            if (!punct("{")) fail_at(peek(), "catch precisa de um bloco { }");
            n->c = block();
        }
        if (eat_word("finally")) {
            if (!punct("{")) fail_at(peek(), "finally precisa de um bloco { }");
            n->d = block();
        }
        if (!n->c && !n->d) fail_at(peek(), "try precisa de catch ou finally");
        return n;
    }
    const Node* switch_stmt() {
        Node* n = make(N::Switch, next());
        expect("(");
        n->a = expression();
        expect(")");
        expect("{");
        bool sawDefault = false;
        while (!eat("}")) {
            Node* c = make(N::Case, peek());
            if (eat_word("case")) c->a = expression();
            else if (eat_word("default")) {
                if (sawDefault) fail_at(peek(), "switch com dois default");
                sawDefault = true;
            } else fail_at(peek(), "esperava case ou default e veio " + describe(peek()));
            expect(":");
            while (!word("case") && !word("default") && !punct("}")) {
                if (peek().type == T::End) fail_at(peek(), "switch sem }");
                c->kids.push_back(statement());
            }
            n->kids.push_back(c);
        }
        return n;
    }
    /// function nome(params) { corpo } — declaração ou expressão.
    const Node* function(bool declaration) {
        const Token& kw = next();   // function
        if (punct("*")) {
            Token y;
            y.type = T::Ident;
            y.text = "yield";
            y.line = kw.line;
            y.col = kw.col;
            unsupported_or(y, "");
        }
        Node* fn = make(N::Func, kw);
        if (peek().type == T::Ident && !punct("(")) {
            fn->name = ident_name(next());
            fn->text = *fn->name;
        } else if (declaration) {
            fail_at(peek(), "function precisa de nome");
        }
        params(fn);
        if (!punct("{")) fail_at(peek(), "esperava o corpo { } da funcao");
        fn->b = block();
        if (!declaration) return fn;
        Node* d = make(N::FuncDecl, kw);
        d->a = fn;
        d->name = fn->name;
        return d;
    }
    void params(Node* fn) {
        expect("(");
        while (!punct(")")) {
            if (punct("...")) {
                Node* r = make(N::PatRest, next());
                r->a = pattern();
                fn->kids.push_back(r);
                break;
            }
            fn->kids.push_back(pattern());
            if (!eat(",")) break;
        }
        expect(")");
    }

    // --- expressões ---
    const Node* expression() {
        const Node* e = assignment();
        if (!punct(",")) return e;
        Node* seq = make(N::Seq, peek());
        seq->kids.push_back(e);
        while (eat(",")) seq->kids.push_back(assignment());
        return seq;
    }
    bool arrow_ahead() const {
        // ( ... ) => : acha o ) que fecha e olha o próximo.
        if (!punct("(")) return false;
        int depth = 0;
        for (usize k = p_; k < toks_.size(); ++k) {
            const Token& t = toks_[k];
            if (t.type == T::End) return false;
            if (t.type != T::Punct) continue;
            if (t.text == "(" || t.text == "[" || t.text == "{") ++depth;
            else if (t.text == ")" || t.text == "]" || t.text == "}") {
                if (--depth == 0) {
                    const Token& after = toks_[std::min(k + 1, toks_.size() - 1)];
                    return after.type == T::Punct && after.text == "=>" && !after.nl;
                }
            }
        }
        return false;
    }
    const Node* arrow() {
        const Token& at = peek();
        Node* fn = make(N::Func, at);
        fn->flags |= kFlagArrow;
        if (peek().type == T::Ident) {
            Node* id = make(N::PatIdent, peek());
            id->name = ident_name(next());
            fn->kids.push_back(id);
        } else {
            params(fn);
        }
        expect("=>");
        if (punct("{")) fn->b = block();
        else {
            fn->flags |= kFlagExprBody;
            fn->b = assignment();
        }
        return fn;
    }
    const Node* assignment() {
        const Token& t = peek();
        Nest guard(*this, t);
        if (t.type == T::Ident && word("async") && (punct("(", 1) || peek(1).type == T::Ident)) unsupported_or(t, "");
        if (t.type == T::Ident && !reserved(t.text) && punct("=>", 1) && !peek(1).nl) return arrow();
        if (arrow_ahead()) return arrow();
        const Node* left = conditional();
        static const std::pair<const char*, u16> kAssign[] = {
            {"=", OpNone}, {"+=", OpAdd}, {"-=", OpSub}, {"*=", OpMul}, {"/=", OpDiv}, {"%=", OpMod},
            {"**=", OpPow}, {"<<=", OpShl}, {">>=", OpShr}, {">>>=", OpUShr}, {"&=", OpBitAnd}, {"|=", OpBitOr},
            {"^=", OpBitXor}, {"&&=", OpAnd}, {"||=", OpOr}, {"?\?=", OpNullish},
        };
        if (peek().type != T::Punct) return left;
        for (const auto& [text, op] : kAssign) {
            if (peek().text != text) continue;
            const Token& at = next();
            if (left->kind != N::Ident && left->kind != N::Member && left->kind != N::Index)
                fail_at(at, left->kind == N::Array || left->kind == N::Object
                                ? "atribuicao com desestruturacao so em declaracoes (let [a, b] = ...)"
                                : "o lado esquerdo do " + std::string(text) + " precisa ser um nome ou uma propriedade");
            Node* n = make(N::Assign, at);
            n->op = op;
            if (op == OpAnd || op == OpOr || op == OpNullish) n->flags |= kFlagLogicalAssign;
            n->a = left;
            n->b = assignment();
            return n;
        }
        return left;
    }
    const Node* conditional() {
        const Node* test = binary(0);
        if (!punct("?")) return test;
        Node* n = make(N::Cond, next());
        n->a = test;
        const bool savedNoIn = noIn_;
        noIn_ = false;
        n->b = assignment();
        noIn_ = savedNoIn;
        expect(":");
        n->c = assignment();
        return n;
    }
    struct BinOp { const char* text; u16 op; int prec; bool logical; bool word; };
    const BinOp* binop() const {
        static const BinOp kOps[] = {
            {"??", OpNullish, 1, true, false}, {"||", OpOr, 2, true, false}, {"&&", OpAnd, 3, true, false},
            {"|", OpBitOr, 4, false, false}, {"^", OpBitXor, 5, false, false}, {"&", OpBitAnd, 6, false, false},
            {"==", OpEq, 7, false, false}, {"!=", OpNe, 7, false, false}, {"===", OpStrictEq, 7, false, false},
            {"!==", OpStrictNe, 7, false, false}, {"<", OpLt, 8, false, false}, {">", OpGt, 8, false, false},
            {"<=", OpLe, 8, false, false}, {">=", OpGe, 8, false, false}, {"instanceof", OpInstanceOf, 8, false, true},
            {"in", OpIn, 8, false, true}, {"<<", OpShl, 9, false, false}, {">>", OpShr, 9, false, false},
            {">>>", OpUShr, 9, false, false}, {"+", OpAdd, 10, false, false}, {"-", OpSub, 10, false, false},
            {"*", OpMul, 11, false, false}, {"/", OpDiv, 11, false, false}, {"%", OpMod, 11, false, false},
            {"**", OpPow, 12, false, false},
        };
        const Token& t = peek();
        for (const BinOp& b : kOps) {
            if (b.word ? (t.type == T::Ident && t.text == b.text) : (t.type == T::Punct && t.text == b.text)) {
                if (b.op == OpIn && noIn_) return nullptr;
                return &b;
            }
        }
        return nullptr;
    }
    const Node* binary(int minPrec) {
        const Node* left = unary();
        for (;;) {
            const BinOp* b = binop();
            if (!b || b->prec < minPrec) return left;
            const Token& at = next();
            // ** é associativo à direita
            const Node* right = binary(b->op == OpPow ? b->prec : b->prec + 1);
            Node* n = make(b->logical ? N::Logical : N::Binary, at);
            n->op = b->op;
            n->a = left;
            n->b = right;
            left = n;
        }
    }
    const Node* unary() {
        const Token& t = peek();
        Nest guard(*this, t);
        static const std::pair<const char*, u16> kUnary[] = {
            {"!", OpNot}, {"-", OpNeg}, {"+", OpPlus}, {"~", OpBitNot},
        };
        if (t.type == T::Punct) {
            for (const auto& [text, op] : kUnary) {
                if (t.text != text) continue;
                Node* n = make(N::Unary, next());
                n->op = op;
                n->a = unary();
                return n;
            }
            if (t.text == "++" || t.text == "--") {
                Node* n = make(N::Update, next());
                n->op = t.text == "++" ? 1 : 2;
                n->flags |= kFlagPrefix;
                n->a = unary();
                if (n->a->kind != N::Ident && n->a->kind != N::Member && n->a->kind != N::Index)
                    fail_at(t, t.text + " precisa de um nome ou propriedade");
                return n;
            }
        }
        if (t.type == T::Ident && (t.text == "typeof" || t.text == "void" || t.text == "delete")) {
            Node* n = make(N::Unary, next());
            n->op = t.text == "typeof" ? OpTypeof : t.text == "void" ? OpVoid : OpDelete;
            n->a = unary();
            if (n->op == OpDelete && n->a->kind != N::Member && n->a->kind != N::Index)
                fail_at(t, "delete so apaga propriedades (delete obj.x)");
            return n;
        }
        if (t.type == T::Ident && t.text == "await") unsupported_or(t, "");
        const Node* e = postfix();
        return e;
    }
    const Node* postfix() {
        const Node* e = call_member();
        if ((punct("++") || punct("--")) && !peek().nl) {
            const Token& t = next();
            if (e->kind != N::Ident && e->kind != N::Member && e->kind != N::Index)
                fail_at(t, t.text + " precisa de um nome ou propriedade");
            Node* n = make(N::Update, t);
            n->op = t.text == "++" ? 1 : 2;
            n->a = e;
            return n;
        }
        return e;
    }
    void arguments(Node* call) {
        expect("(");
        while (!punct(")")) {
            if (punct("...")) {
                Node* s = make(N::Spread, next());
                s->a = assignment();
                call->kids.push_back(s);
            } else {
                call->kids.push_back(assignment());
            }
            if (!eat(",")) break;
        }
        expect(")");
    }
    const Node* call_member() {
        const Node* e;
        bool optional = false;
        if (word("new")) {
            const Token& at = next();
            if (punct(".")) fail_at(at, "new.target nao existe no AureaScript");
            Node* n = make(N::New, at);
            // o construtor: primário com acessos (sem chamada)
            const Node* callee = primary();
            for (;;) {
                if (punct(".")) {
                    const Token& dot = next();
                    Node* m = make(N::Member, dot);
                    m->a = callee;
                    m->text = property_name();
                    callee = m;
                } else if (punct("[")) {
                    const Token& br = next();
                    Node* m = make(N::Index, br);
                    m->a = callee;
                    m->b = expression();
                    expect("]");
                    callee = m;
                } else break;
            }
            n->a = callee;
            if (punct("(")) arguments(n);
            e = n;
        } else {
            e = primary();
        }
        for (;;) {
            if (punct(".")) {
                const Token& dot = next();
                Node* m = make(N::Member, dot);
                m->a = e;
                m->text = property_name();
                e = m;
            } else if (punct("?.")) {
                const Token& q = next();
                optional = true;
                if (punct("(")) {
                    Node* c = make(N::Call, q);
                    c->flags |= kFlagOptional;
                    c->a = e;
                    arguments(c);
                    e = c;
                } else if (punct("[")) {
                    next();
                    Node* m = make(N::Index, q);
                    m->flags |= kFlagOptional;
                    m->a = e;
                    m->b = expression();
                    expect("]");
                    e = m;
                } else {
                    Node* m = make(N::Member, q);
                    m->flags |= kFlagOptional;
                    m->a = e;
                    m->text = property_name();
                    e = m;
                }
            } else if (punct("[")) {
                const Token& br = next();
                Node* m = make(N::Index, br);
                m->a = e;
                const bool savedNoIn = noIn_;
                noIn_ = false;
                m->b = expression();
                noIn_ = savedNoIn;
                expect("]");
                e = m;
            } else if (punct("(")) {
                Node* c = make(N::Call, peek());
                c->a = e;
                const bool savedNoIn = noIn_;
                noIn_ = false;
                arguments(c);
                noIn_ = savedNoIn;
                e = c;
            } else if (peek().type == T::Template && !peek().nl) {
                fail_at(peek(), "template marcado (tag`...`) nao existe no AureaScript");
            } else break;
        }
        if (optional) {
            Node* chain = make(N::OptChain, peek());
            chain->a = e;
            return chain;
        }
        return e;
    }
    std::string property_name() {
        const Token& t = next();
        if (t.type == T::Ident) return t.text;   // palavras reservadas valem depois do ponto
        if (t.type == T::Punct && t.text == "#") fail_at(t, "campos privados (#x) nao existem no AureaScript");
        fail_at(t, "esperava o nome da propriedade depois do '.' e veio " + describe(t));
    }
    const Node* primary() {
        const Token& t = peek();
        Nest guard(*this, t);
        switch (t.type) {
            case T::End: fail_at(t, "o arquivo acabou no meio de uma expressao");
            case T::Num: { Node* n = make(N::Num, next()); n->num = t.num; return n; }
            case T::Str: { Node* n = make(N::Str, next()); n->text = t.text; return n; }
            case T::Template: return template_literal(next());
            case T::Ident: {
                const std::string& w = t.text;
                if (w == "true") { next(); return make(N::True, t); }
                if (w == "false") { next(); return make(N::False, t); }
                if (w == "null") { next(); return make(N::Null, t); }
                if (w == "this") { next(); return make(N::This, t); }
                if (w == "function") return function(false);
                if (w == "class" || w == "import" || w == "super" || w == "await" || w == "yield") unsupported_or(t, "");
                Node* n = make(N::Ident, t);
                n->name = ident_name(next());
                return n;
            }
            case T::Punct: break;
        }
        if (t.text == "(") {
            next();
            const bool savedNoIn = noIn_;
            noIn_ = false;
            const Node* e = expression();
            noIn_ = savedNoIn;
            expect(")");
            return e;
        }
        if (t.text == "[") {
            Node* n = make(N::Array, next());
            while (!punct("]")) {
                if (punct(",")) { next(); n->kids.push_back(make(N::Hole, t)); continue; }
                if (punct("...")) {
                    Node* s = make(N::Spread, next());
                    s->a = assignment();
                    n->kids.push_back(s);
                } else {
                    n->kids.push_back(assignment());
                }
                if (!eat(",")) break;
            }
            expect("]");
            return n;
        }
        if (t.text == "{") {
            Node* n = make(N::Object, next());
            while (!punct("}")) {
                if (punct("...")) {
                    Node* s = make(N::Spread, next());
                    s->a = assignment();
                    n->kids.push_back(s);
                    if (!eat(",")) break;
                    continue;
                }
                Node* prop = make(N::Prop, peek());
                const Token& key = next();
                if (key.type == T::Punct && key.text == "[") {
                    prop->flags |= kFlagComputed;
                    prop->a = assignment();
                    expect("]");
                } else if (key.type == T::Ident || key.type == T::Str) {
                    if (key.type == T::Ident && (key.text == "get" || key.text == "set") && peek().type == T::Ident && punct("(", 1))
                        fail_at(key, "get/set em literal de objeto nao existem no AureaScript");
                    prop->text = key.text;
                } else if (key.type == T::Num) {
                    prop->text = Context::number_text(key.num);
                } else {
                    fail_at(key, "esperava o nome da propriedade e veio " + describe(key));
                }
                if (punct("(")) {
                    // método: nome(params) { ... }
                    Node* fn = make(N::Func, key);
                    fn->text = prop->text;
                    params(fn);
                    if (!punct("{")) fail_at(peek(), "esperava o corpo { } do metodo");
                    fn->b = block();
                    prop->b = fn;
                } else if (eat(":")) {
                    prop->b = assignment();
                } else {
                    if (key.type != T::Ident || (prop->flags & kFlagComputed)) fail_at(peek(), "esperava ':' depois da chave");
                    Node* id = make(N::Ident, key);
                    id->name = ident_name(key);
                    prop->b = id;
                }
                n->kids.push_back(prop);
                if (!eat(",")) break;
            }
            expect("}");
            return n;
        }
        if (t.text == "/" || t.text == "/=") fail_at(t, "expressoes regulares (/.../) nao existem no AureaScript: use split, indexOf, includes, replaceAll");
        fail_at(t, "nao esperava " + describe(t) + " aqui");
    }
    const Node* template_literal(const Token& t) {
        Node* n = make(N::Template, t);
        for (const TemplatePart& part : t.parts) {
            if (!part.isExpr) {
                Node* s = make(N::Str, t);
                s->text = part.text;
                n->kids.push_back(s);
                continue;
            }
            Lexer lex(part.text, part.line, part.col);
            Parser sub(ctx_, prog_, lex.run());
            sub.nesting_ = nesting_;
            n->kids.push_back(sub.lone_expression());
        }
        return n;
    }

    Context& ctx_;
    Program& prog_;
    std::vector<Token> toks_;
    usize p_ = 0;
    int nesting_ = 0;
    bool noIn_ = false;
};

/// Profundidade da árvore (a avaliação é recursiva; a pilha das threads é finita).
u32 depth_of(const Node* root) {
    struct Item { const Node* n; u32 d; };
    std::vector<Item> stack{{root, 1}};
    u32 deepest = 0;
    while (!stack.empty()) {
        const Item it = stack.back();
        stack.pop_back();
        if (!it.n) continue;
        deepest = std::max(deepest, it.d);
        if (deepest > kMaxAstDepth) return deepest;
        for (const Node* k : it.n->kids) stack.push_back({k, it.d + 1});
        for (const Node* k : {it.n->a, it.n->b, it.n->c, it.n->d}) if (k) stack.push_back({k, it.d + 1});
    }
    return deepest;
}

} // namespace

bool parse_program(Context& ctx, std::string_view source, std::string_view file, Program& out, ScriptError& error) {
    out.file = std::string(file);
    try {
        Lexer lex(source, 1, 1);
        Parser parser(ctx, out, lex.run());
        out.root = parser.program();
        if (depth_of(out.root) > kMaxAstDepth)
            throw ParseFail{"expressao aninhada demais (encadeie menos operacoes numa linha so)", 1, 1};
        return true;
    } catch (const ParseFail& f) {
        error = ScriptError{};
        error.name = "SyntaxError";
        error.message = f.message;
        error.file = std::string(file);
        error.line = f.line;
        error.column = f.col;
        return false;
    } catch (const std::bad_alloc&) {
        error = ScriptError{};
        error.name = "LimitError";
        error.message = "sem memoria para compilar o script";
        error.file = std::string(file);
        error.aborted = true;
        return false;
    }
}

} // namespace aurea::plugins::script
