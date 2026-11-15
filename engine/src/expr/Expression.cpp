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
#include "aurea/timeline/LayerAnimator.hpp"
#include "aurea/timeline/Timeline.hpp"
#include "aurea/timeline/LayerWorld.hpp"   // AE animation: toComp/fromComp/toWorld/fromWorld
#include "aurea/render/MaskRaster.hpp"     // AE expressions: caminho da máscara no instante
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace aurea::expr {

// =============================================================================
// Programa
// =============================================================================
enum class N : u8 {
    Num, Str, Name, Local, Global, Array, Unary, Binary, And, Or, Cond, Call, Member, Index,
    Assign, IncDec, VarDecl, Block, If, For, While, Return, Break, Continue, ExprStmt, Empty,
    // AE expressions (lote E): funções da pessoa, objetos, o resto do JavaScript do dia a dia.
    Null, Undefined, Object, Func, FuncDecl, Spread, Seq, Try, Throw, DoWhile, ForOf, Switch, Case,
    TypeOf, Coalesce,
};

struct Node {
    N   kind = N::Empty;
    u8  op = 0;         ///< operador (Unary/Binary/Assign/IncDec)
    u8  flag = 0;       ///< IncDec: 1 = prefixo; ForOf: 1 = "in"
    u8  hops = 0;       ///< Local: quantas funções para fora a variável mora
    u16 fn = 0;         ///< a função (Program::funcs) em que o nó foi escrito
    u32 pos = 0;        ///< offset no fonte (erro aponta aqui)
    i32 a = -1, b = -1, c = -1, d = -1;
    u32 first = 0, count = 0;   ///< filhos em Program::kids (Local: `first` = nome em strings)
    f64 num = 0.0;
    u32 sym = 0;        ///< Str/Name/Member: índice em strings; Local/VarDecl/FuncDecl: slot; Global: id
};

/// Função (a 0 é o programa). Variáveis têm escopo de função, como o `var`;
/// uma função de dentro lê as de fora pela cadeia `parent` (Node::hops).
struct Func {
    i32 parent = -1;
    i32 body = -1;                 ///< bloco, ou expressão (seta sem chaves)
    bool exprBody = false;
    u32 nparams = 0;               ///< os primeiros slots
    i32 rest = -1;                 ///< slot do parâmetro "...resto" (−1 = não tem)
    u32 pos = 0;
    std::string name;
    std::vector<std::string> locals;                 ///< slot → nome
    std::vector<i32> defaults;                       ///< por parâmetro: nó do valor padrão (−1 = não tem)
    std::vector<std::pair<u32, u32>> decls;          ///< `function f(){}` içadas: (slot, função)
};

class Program {
public:
    std::string              source;
    std::vector<Node>        nodes;
    std::vector<i32>         kids;
    std::vector<std::string> strings;
    std::vector<std::string> localNames;
    std::vector<Func>        funcs;
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
    GOutPoint, GWidth, GHeight, GMath, GTextIndex, GTextTotal, GLocalTime,
    GTimeMs, GMarker, GSelectorValue,
    // Funções (valor K::Fn quando citadas sem chamar).
    FWiggle = 100, FLoopOut, FLoopIn, FLoopOutDur, FLoopInDur, FLinear, FEase, FEaseIn, FEaseOut,
    FClamp, FRandom, FGaussRandom, FSeedRandom, FNoise, FSin, FCos, FTan, FAsin, FAcos, FAtan,
    FAtan2, FSqrt, FAbs, FFloor, FCeil, FRound, FMin, FMax, FPow, FExp, FLog, FDeg2Rad, FRad2Deg,
    FLength, FNormalize, FAdd, FSub, FMul, FDiv, FDot, FCross, FValueAtTime, FVelocityAtTime,
    FKey, FLayer, FEffect, FFramesToTime, FTimeToFrames,
    // Ritmo e tremor (tremor com decaimento, marcas, tempo em degraus).
    FShake, FTemporalWiggle, FSpeedAtTime, FTimeSinceMarker, FPosterizeTime, FSmooth, FNearestKey,
    // Métodos (valor K::Method: função + objeto receptor).
    MCompLayer = 200, MLayerEffect, MEffectParam, MPropValueAtTime, MPropVelocityAtTime, MPropKey,
    MPropSpeedAtTime, MPropNearestKey, MMarkerKey, MMarkerNearestKey,
};

// AE animation (2026-11-11): espaço da camada, mira, caixa do conteúdo e outras
// composições. Números próprios (longe do fim das listas acima) para não
// colidir com o que outros acrescentam lá.
enum AeAnimId : u32 {
    // toComp, fromComp, toWorld, fromWorld e as versões *Vec (direção, sem a
    // translação), na MESMA ordem dos métodos MLayerSpace + k.
    FToComp = 180, FFromComp, FToWorld, FFromWorld, FToCompVec, FFromCompVec, FToWorldVec, FFromWorldVec,
    FLookAt, FComp, FSourceRect,
    MLayerSpace = 260,          ///< + 0..7: thisLayer.toComp(...) …
    MLayerSourceRect = 270,     ///< thisLayer.sourceRectAtTime(...)
};
// v70, expressão no texto (o Texto-fonte do AE): funções e métodos de texto.
// Funções ficam entre FWiggle e MCompLayer (o que vira K::Fn); métodos, longe.
enum TextExprId : u32 {
    GText = 90,                 ///< `text` (o grupo Texto da própria camada)
    FString = 165, FNumber, FParseInt, FParseFloat, FTimeToTimecode, FTimeToCurrentFormat,
    MStrUpper = 300, MStrLower, MStrSubstr, MStrSubstring, MStrSlice, MStrCharAt, MStrIndexOf, MStrLastIndexOf,
    MStrReplace, MStrTrim, MStrPadStart, MStrPadEnd, MStrRepeat, MStrToString, MStrIncludes, MStrStartsWith,
    MStrEndsWith, MStrCharCodeAt, MNumToFixed, MNumToString, MStrReplaceAll,
};
constexpr const char* kSpaceNames[8] = {"toComp", "fromComp", "toWorld", "fromWorld",
                                        "toCompVec", "fromCompVec", "toWorldVec", "fromWorldVec"};
// AE expressions (lote E, 2026-11-12): cor, footage(), caminhos, sampleImage,
// vetores longos, objetos e os métodos que faltavam. Funções (K::Fn) em
// 1000..1999; métodos em 2000+ (longe das listas de cima).
enum AeExprId : u32 {
    GArrayNs = 40, GObjectNs, GNumberNs,
    FRgbToHsl = 1000, FHslToRgb, FHexToRgb, FFootage, FCreatePath, FSampleImage, FIsNaN, FIsFinite, FMask, FContent,
    FSign, FTrunc, FHypot, FCbrt, FLog10, FLog2, FIsArray, FObjKeys, FObjValues, FObjEntries, FFromCharCode,
    FNumIsInteger, FExprFnEnd,
    MArrPush = 2000, MArrPop, MArrShift, MArrUnshift, MArrSlice, MArrSplice, MArrConcat, MArrJoin, MArrIndexOf,
    MArrLastIndexOf, MArrIncludes, MArrReverse, MArrSort, MArrMap, MArrFilter, MArrReduce, MArrForEach, MArrSome,
    MArrEvery, MArrFind, MArrFindIndex, MArrFill, MArrToString, MArrFlat, MArrAt,
    MStrSplit = 2100, MStrConcat, MStrTrimStart, MStrTrimEnd, MStrAt,
    MPathPoints = 2200, MPathInTangents, MPathOutTangents, MPathIsClosed, MPathPointOn, MPathTangentOn, MPathNormalOn,
    MPathCreate,
    MLayerMask = 2300, MLayerContent, MGroupContent, MLayerSample, MObjHasOwn,
};
/// Funções que recebem vetores como vetores (o resto vê [a, b, c] como número/vetor de até 4).
constexpr bool wants_arrays(u32 id) noexcept {
    return id == FCreatePath || id == FIsArray || id == FObjKeys || id == FObjValues || id == FObjEntries
        || id == FString || id == MPathCreate || (id >= MArrPush && id <= MArrAt) || id == MObjHasOwn
        || id == FMin || id == FMax || id == FHypot;
}

struct NameEntry { const char* name; u32 id; };

constexpr NameEntry kGlobals[] = {
    {"time", GTime}, {"value", GValue}, {"index", GIndex}, {"fps", GFps}, {"frame", GFrame},
    {"thisLayer", GThisLayer}, {"thisComp", GThisComp}, {"thisProperty", GThisProperty},
    {"transform", GTransform}, {"position", GPosition}, {"scale", GScale}, {"rotation", GRotation},
    {"opacity", GOpacity}, {"anchorPoint", GAnchor}, {"velocity", GVelocity}, {"speed", GSpeed},
    {"numKeys", GNumKeys}, {"inPoint", GInPoint}, {"outPoint", GOutPoint}, {"width", GWidth},
    {"height", GHeight}, {"Math", GMath},
    {"textIndex", GTextIndex}, {"textTotal", GTextTotal}, {"localTime", GLocalTime},
    // Seletor de expressão do AE: o valor dos seletores anteriores. No Aurea o
    // animador tem um seletor só, então é a seleção cheia (100 %).
    {"selectorValue", GSelectorValue},
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
    {"timeMs", GTimeMs}, {"marker", GMarker},
    {"shake", FShake}, {"temporalWiggle", FTemporalWiggle}, {"speedAtTime", FSpeedAtTime},
    {"timeSinceMarker", FTimeSinceMarker}, {"posterizeTime", FPosterizeTime}, {"smooth", FSmooth},
    {"nearestKey", FNearestKey},
    // AE animation: no corpo da expressão valem para a própria camada.
    {"toComp", FToComp}, {"fromComp", FFromComp}, {"toWorld", FToWorld}, {"fromWorld", FFromWorld},
    {"toCompVec", FToCompVec}, {"fromCompVec", FFromCompVec}, {"toWorldVec", FToWorldVec},
    {"fromWorldVec", FFromWorldVec}, {"lookAt", FLookAt}, {"comp", FComp}, {"sourceRectAtTime", FSourceRect},
    // v70: texto.
    {"text", GText}, {"String", FString}, {"Number", FNumber}, {"parseInt", FParseInt}, {"parseFloat", FParseFloat},
    {"timeToTimecode", FTimeToTimecode}, {"timeToCurrentFormat", FTimeToCurrentFormat},
    // AE expressions (lote E).
    {"rgbToHsl", FRgbToHsl}, {"hslToRgb", FHslToRgb}, {"hexToRgb", FHexToRgb}, {"footage", FFootage},
    {"createPath", FCreatePath}, {"sampleImage", FSampleImage}, {"isNaN", FIsNaN}, {"isFinite", FIsFinite},
    {"mask", FMask}, {"content", FContent}, {"fromCompToSurface", FFromComp},
    {"Array", GArrayNs}, {"Object", GObjectNs},
};
/// Nomes que entraram no lote E: uma expressão antiga que atribui a um deles
/// sem declarar (`mask = 3`) continua valendo (vira variável, como antes).
constexpr bool shadowable_global(u32 id) noexcept { return id >= FRgbToHsl || (id >= GArrayNs && id <= GNumberNs); }

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
    u32 nest = 0;       ///< `${ }` dentro de `${ }` (texto com crase)

    bool fail(u32 pos, std::string msg) {
        if (error.empty()) { error = std::move(msg); errPos = pos; }
        return false;
    }

    /// Texto com crase (template literal): `a ${x} b` vira os tokens de
    /// ("a " + (x) + " b"). O primeiro pedaço é sempre texto, então o "+" junta.
    bool lex_template(usize& i, u32 pos, bool nl) {
        const usize n = src.size();
        auto punct = [&](const char* p, usize at, bool nlb = false) {
            Token t; t.kind = Tok::Punct; t.pos = static_cast<u32>(at); t.text = p; t.nl = nlb; out.push_back(std::move(t));
        };
        std::string chunk;
        bool first = true;
        auto flush = [&](usize at) {
            if (!first) punct("+", at);
            Token t; t.kind = Tok::Str; t.pos = static_cast<u32>(at); t.text = std::move(chunk); out.push_back(std::move(t));
            chunk.clear();
            first = false;
        };
        punct("(", pos, nl);
        usize j = i + 1;
        for (;;) {
            if (j >= n) return fail(pos, "texto com crase sem fechar");
            const char ch = src[j];
            if (ch == '`') { flush(j); ++j; break; }
            if (ch == '\\' && j + 1 < n) {
                const char e = src[j + 1];
                chunk.push_back(e == 'n' || e == 'r' ? '\n' : e == 't' ? '\t' : e);
                j += 2;
                continue;
            }
            if (ch == '$' && j + 1 < n && src[j + 1] == '{') {
                flush(j);
                usize k = j + 2;
                int depth = 1;
                while (k < n) {
                    const char q = src[k];
                    if (q == '{') ++depth;
                    else if (q == '}') { if (--depth == 0) break; }
                    else if (q == '"' || q == '\'' || q == '`') {
                        ++k;
                        while (k < n && src[k] != q) { if (src[k] == '\\') ++k; ++k; }
                    }
                    ++k;
                }
                if (k >= n) return fail(static_cast<u32>(j), "${ sem fechar");
                if (nest >= 4) return fail(static_cast<u32>(j), "texto com crase aninhado demais");
                Lexer sub{src.substr(j + 2, k - (j + 2)), {}, {}, 0, nest + 1};
                if (!sub.run()) return fail(static_cast<u32>(j + 2) + sub.errPos, sub.error);
                if (sub.out.size() <= 1) return fail(static_cast<u32>(j), "${ } vazio");
                punct("+", j);
                punct("(", j);
                for (usize m = 0; m + 1 < sub.out.size(); ++m) {
                    Token t = sub.out[m];
                    t.pos += static_cast<u32>(j + 2);
                    t.nl = false;
                    out.push_back(std::move(t));
                }
                punct(")", k);
                j = k + 1;
                continue;
            }
            chunk.push_back(ch);
            if (chunk.size() > kMaxStringBytes) return fail(pos, "texto longo demais");
            ++j;
        }
        punct(")", j - 1);
        i = j;
        return true;
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
            if (c == '`') {
                if (!lex_template(i, static_cast<u32>(i), nl)) return false;
                nl = false;
                continue;
            }
            Token t;
            t.pos = static_cast<u32>(i);
            t.nl = nl;
            nl = false;
            if (c == '0' && i + 2 < n && (src[i + 1] == 'x' || src[i + 1] == 'X') && std::isxdigit(static_cast<unsigned char>(src[i + 2]))) {
                // Hexadecimal (0xFF8800), comum em cor.
                usize j = i + 2;
                f64 v = 0.0;
                while (j < n && std::isxdigit(static_cast<unsigned char>(src[j]))) {
                    const char h = src[j];
                    v = v * 16.0 + static_cast<f64>(h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
                    ++j;
                }
                t.num = v;
                t.kind = Tok::Num;
                i = j;
            } else if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < n && src[i + 1] >= '0' && src[i + 1] <= '9')) {
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
                        // "\r" quebra a linha, como no texto-fonte do After Effects.
                        s.push_back(e == 'n' || e == 'r' ? '\n' : e == 't' ? '\t' : e);
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
                    "===", "!==", "...", "=>", "??", "%=",
                    "**", "==", "!=", "<=", ">=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=",
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
    u32 curFn = 0;      ///< a função sendo lida (0 = o programa)
    u32 fnNest = 0;
    u32 hidden = 0;     ///< contador das variáveis internas (desestruturação)

    Parser(const std::vector<Token>& t, Program& p) : toks(t), prog(p) { prog.funcs.emplace_back(); }

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
        n.fn = static_cast<u16>(curFn);
        prog.nodes.push_back(n);
        return static_cast<i32>(prog.nodes.size() - 1);
    }
    bool is_ident(const char* w, usize k = 0) const { const Token& t = peek(k); return t.kind == Tok::Ident && t.text == w; }

    /// `function nome(a, b = 1, ...resto) { }`, `function (…) { }`, `(a, b) => expr`,
    /// `x => { }`. Quem chama já consumiu `function` e o nome. Devolve o índice
    /// da função em Program::funcs (−1 = erro).
    i32 function_rest(u32 pos, const std::string& name, bool arrow, bool bareParam) {
        if (prog.funcs.size() >= kMaxFunctions) { fail(pos, "funções demais na expressão"); return -1; }
        if (fnNest >= 12) { fail(pos, "funções aninhadas demais"); return -1; }
        const u32 fi = static_cast<u32>(prog.funcs.size());
        prog.funcs.emplace_back();
        prog.funcs[fi].parent = static_cast<i32>(curFn);
        prog.funcs[fi].name = name;
        prog.funcs[fi].pos = pos;
        const u32 saved = curFn;
        curFn = fi;
        ++fnNest;
        struct Restore { Parser& p; u32 fn; ~Restore() { p.curFn = fn; --p.fnNest; } } restore{*this, saved};
        auto add_param = [&](const Token& t) -> bool {
            for (const std::string& s : prog.funcs[fi].locals) if (s == t.text) { fail(t.pos, "parâmetro '" + t.text + "' repetido"); return false; }
            if (prog.funcs[fi].locals.size() >= kMaxParams) { fail(t.pos, "parâmetros demais"); return false; }
            prog.funcs[fi].locals.push_back(t.text);
            prog.funcs[fi].defaults.push_back(-1);
            prog.funcs[fi].nparams = static_cast<u32>(prog.funcs[fi].locals.size());
            return true;
        };
        if (bareParam) {
            const Token& t = peek();
            ++at;
            if (!add_param(t)) return -1;
        } else {
            if (!expect("(")) return -1;
            if (!is(")")) {
                do {
                    const bool rest = accept("...");
                    const Token& t = peek();
                    if (t.kind != Tok::Ident) { fail(t.pos, "esperava o nome do parâmetro" + describe_found()); return -1; }
                    ++at;
                    if (!add_param(t)) return -1;
                    if (rest) { prog.funcs[fi].rest = static_cast<i32>(prog.funcs[fi].nparams - 1); break; }
                    if (accept("=")) {
                        const i32 d = expression(PAssign);
                        if (d < 0) return -1;
                        prog.funcs[fi].defaults.back() = d;
                    }
                } while (accept(","));
            }
            if (!expect(")")) return -1;
        }
        i32 body = -1;
        if (arrow) {
            if (!expect("=>")) return -1;
            if (is("{")) body = statement();
            else { body = expression(PAssign); prog.funcs[fi].exprBody = true; }
        } else {
            if (!is("{")) { fail(peek().pos, "esperava '{' no corpo da função" + describe_found()); return -1; }
            body = statement();
        }
        if (body < 0) return -1;
        prog.funcs[fi].body = body;
        return static_cast<i32>(fi);
    }

    /// Em `(`: é a lista de parâmetros de uma seta? (olha até o `)` que fecha e o `=>` depois)
    bool arrow_ahead() const {
        usize k = at;
        int level = 0;
        for (; k < toks.size(); ++k) {
            const Token& t = toks[k];
            if (t.kind == Tok::End) return false;
            if (t.kind != Tok::Punct) continue;
            if (t.text == "(" || t.text == "[" || t.text == "{") ++level;
            else if (t.text == ")" || t.text == "]" || t.text == "}") { if (--level == 0) break; }
        }
        return k + 1 < toks.size() && toks[k + 1].kind == Tok::Punct && toks[k + 1].text == "=>";
    }

    i32 var_node(const std::string& name, u32 pos, i32 init) {
        const i32 d = node(N::VarDecl, pos);
        nd(d).sym = intern(name);
        nd(d).a = init;
        return d;
    }
    i32 name_node(const std::string& name, u32 pos) {
        const i32 n = node(N::Name, pos);
        nd(n).sym = intern(name);
        return n;
    }
    i32 str_node(const std::string& s, u32 pos) {
        const i32 n = node(N::Str, pos);
        nd(n).sym = intern(s);
        return n;
    }
    i32 num_node(f64 v, u32 pos) {
        const i32 n = node(N::Num, pos);
        nd(n).num = v;
        return n;
    }
    /// `a, b` (só no início e no passo do for): vale o último.
    i32 expr_seq() {
        const u32 pos = peek().pos;
        const i32 first = expression(PAssign);
        if (first < 0 || !is(",")) return first;
        std::vector<i32> list{first};
        while (accept(",")) {
            const i32 e = expression(PAssign);
            if (e < 0) return -1;
            list.push_back(e);
        }
        const i32 s = node(N::Seq, pos);
        set_kids(s, list);
        return s;
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
                if (!expect("(")) return -1;
                // for (const x of lista) / for (k in objeto)
                {
                    const bool decl = is_kw("var") || is_kw("let") || is_kw("const");
                    const usize k = decl ? 1 : 0;
                    if (peek(k).kind == Tok::Ident && (is_ident("of", k + 1) || is_ident("in", k + 1))) {
                        const Token& name = peek(k);
                        const bool in = is_ident("in", k + 1);
                        at += k + 2;
                        const i32 n = node(N::ForOf, t.pos);
                        const i32 target = decl ? var_node(name.text, name.pos, -1) : name_node(name.text, name.pos);
                        const i32 it = expression(PAssign);
                        if (it < 0 || !expect(")")) return -1;
                        const i32 body = statement();
                        if (body < 0) return -1;
                        nd(n).a = it; nd(n).b = body; nd(n).c = target; nd(n).flag = in ? 1 : 0;
                        return n;
                    }
                }
                const i32 n = node(N::For, t.pos);
                i32 init = -1, cond = -1, step = -1;
                if (!is(";")) {
                    if (is_kw("var") || is_kw("let") || is_kw("const")) {
                        const u32 p = peek().pos;
                        ++at;
                        init = var_decl(p);
                    } else {
                        init = expr_seq();
                    }
                    if (init < 0) return -1;
                }
                if (!expect(";")) return -1;
                if (!is(";")) { cond = expression(PAssign); if (cond < 0) return -1; }
                if (!expect(";")) return -1;
                if (!is(")")) { step = expr_seq(); if (step < 0) return -1; }
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
            if (t.text == "function" && peek(1).kind == Tok::Ident) {
                // Declaração: içada (vale no escopo inteiro, antes ou depois da linha).
                const Token& name = peek(1);
                at += 2;
                const i32 fi = function_rest(t.pos, name.text, false, false);
                if (fi < 0) return -1;
                const i32 n = node(N::FuncDecl, name.pos);
                nd(n).sym = intern(name.text);
                nd(n).b = fi;
                accept(";");
                return n;
            }
            if (t.text == "do") {
                ++at;
                const i32 n = node(N::DoWhile, t.pos);
                const i32 body = statement();
                if (body < 0) return -1;
                if (!is_kw("while")) return fail(peek().pos, "esperava 'while' depois do 'do'" + describe_found());
                ++at;
                if (!expect("(")) return -1;
                const i32 c = expression(PAssign);
                if (c < 0 || !expect(")")) return -1;
                accept(";");
                nd(n).a = c; nd(n).b = body;
                return n;
            }
            if (t.text == "try") {
                ++at;
                const i32 n = node(N::Try, t.pos);
                if (!is("{")) return fail(peek().pos, "esperava '{' depois do 'try'" + describe_found());
                const i32 body = statement();
                if (body < 0) return -1;
                i32 handler = -1, binding = -1, finally = -1;
                if (is_kw("catch")) {
                    ++at;
                    if (accept("(")) {
                        const Token& name = peek();
                        if (name.kind != Tok::Ident) return fail(name.pos, "esperava o nome do erro" + describe_found());
                        ++at;
                        binding = var_node(name.text, name.pos, -1);
                        if (!expect(")")) return -1;
                    }
                    if (!is("{")) return fail(peek().pos, "esperava '{' depois do 'catch'" + describe_found());
                    handler = statement();
                    if (handler < 0) return -1;
                }
                if (is_kw("finally")) {
                    ++at;
                    if (!is("{")) return fail(peek().pos, "esperava '{' depois do 'finally'" + describe_found());
                    finally = statement();
                    if (finally < 0) return -1;
                }
                if (handler < 0 && finally < 0) return fail(peek().pos, "o 'try' precisa de 'catch' ou 'finally'");
                nd(n).a = body; nd(n).b = handler; nd(n).c = finally; nd(n).d = binding;
                return n;
            }
            if (t.text == "throw") {
                ++at;
                const i32 n = node(N::Throw, t.pos);
                const i32 e = expression(PAssign);
                if (e < 0) return -1;
                nd(n).a = e;
                accept(";");
                return n;
            }
            if (t.text == "switch") {
                ++at;
                const i32 n = node(N::Switch, t.pos);
                if (!expect("(")) return -1;
                const i32 disc = expression(PAssign);
                if (disc < 0 || !expect(")") || !expect("{")) return -1;
                std::vector<i32> cases;
                while (!failed() && !is("}")) {
                    const Token& ct = peek();
                    i32 test = -1;
                    if (is_kw("case")) {
                        ++at;
                        test = expression(PAssign);
                        if (test < 0) return -1;
                    } else if (is_kw("default")) {
                        ++at;
                    } else {
                        return fail(ct.pos, "esperava 'case' ou 'default'" + describe_found());
                    }
                    if (!expect(":")) return -1;
                    const i32 c = node(N::Case, ct.pos);
                    std::vector<i32> body;
                    while (!failed() && !is("}") && !is_kw("case") && !is_kw("default")) {
                        if (peek().kind == Tok::End) return fail(peek().pos, "'switch' sem fechar");
                        const i32 s = statement();
                        if (s < 0) return -1;
                        body.push_back(s);
                    }
                    nd(c).a = test;
                    set_kids(c, body);
                    cases.push_back(c);
                    if (cases.size() > 256) return fail(ct.pos, "casos demais no 'switch'");
                }
                if (!expect("}")) return -1;
                nd(n).a = disc;
                set_kids(n, cases);
                return n;
            }
            if (t.text == "class" || t.text == "new" || t.text == "eval"
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
            if (is("[") || is("{")) {
                // Desestruturação: const [x, y] = position;  const {width, height} = thisComp;
                const bool arr = is("[");
                ++at;
                struct Part { std::string name, key; u32 pos; u32 index; };
                std::vector<Part> parts;
                u32 index = 0;
                while (!is(arr ? "]" : "}")) {
                    if (arr && is(",")) { ++at; ++index; continue; }   // [, y] pula um
                    const Token& pn = peek();
                    if (pn.kind != Tok::Ident) return fail(pn.pos, "esperava um nome" + describe_found());
                    ++at;
                    Part part{pn.text, pn.text, pn.pos, index};
                    if (!arr && accept(":")) {
                        const Token& alias = peek();
                        if (alias.kind != Tok::Ident) return fail(alias.pos, "esperava um nome" + describe_found());
                        ++at;
                        part.name = alias.text;
                    }
                    ++index;
                    parts.push_back(std::move(part));
                    if (parts.size() > 32) return fail(pn.pos, "nomes demais");
                    if (!accept(",")) break;
                }
                if (!expect(arr ? "]" : "}") || !expect("=")) return -1;
                const i32 e = expression(PAssign);
                if (e < 0) return -1;
                const std::string tmp = std::string(1, '\x01') + "d" + std::to_string(hidden++);
                list.push_back(var_node(tmp, name.pos, e));
                for (const Part& part : parts) {
                    i32 get;
                    if (arr) {
                        get = node(N::Index, part.pos);
                        const i32 base = name_node(tmp, part.pos);
                        const i32 idx = num_node(static_cast<f64>(part.index), part.pos);
                        nd(get).a = base; nd(get).b = idx;
                    } else {
                        get = node(N::Member, part.pos);
                        const i32 base = name_node(tmp, part.pos);
                        nd(get).a = base;
                        nd(get).sym = intern(part.key);
                    }
                    list.push_back(var_node(part.name, part.pos, get));
                }
                continue;
            }
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
        if (s == "=" || s == "+=" || s == "-=" || s == "*=" || s == "/=" || s == "%=") return PAssign;
        if (s == "?") return PCond;
        if (s == "||" || s == "??") return POr;
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
                const N lk = nd(left).kind;
                if (lk != N::Name && lk != N::Index && lk != N::Member)
                    return fail(t.pos, "só dá para atribuir a uma variável, a um item de vetor ou a uma propriedade de objeto");
                const i32 rhs = expression(PAssign);   // associa à direita
                if (rhs < 0) return -1;
                const i32 n = node(N::Assign, t.pos);
                nd(n).a = left; nd(n).b = rhs;
                nd(n).op = s == "=" ? OpSet : s == "+=" ? OpAdd : s == "-=" ? OpSub : s == "*=" ? OpMul
                         : s == "%=" ? OpMod : OpDiv;
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
            } else if (s == "??") {
                n = node(N::Coalesce, t.pos);
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
        if (t.kind == Tok::Ident && t.text == "typeof") {
            ++at;
            const i32 e = unary();
            if (e < 0) return -1;
            const i32 n = node(N::TypeOf, t.pos);
            nd(n).a = e;
            return n;
        }
        if (t.kind == Tok::Punct && (t.text == "++" || t.text == "--")) {
            ++at;
            const i32 e = unary();
            if (e < 0) return -1;
            if (nd(e).kind != N::Name && nd(e).kind != N::Index && nd(e).kind != N::Member) return fail(t.pos, "++/-- só em variável");
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
                        i32 a;
                        if (is("...")) {   // f(...lista)
                            const u32 sp = peek().pos;
                            ++at;
                            const i32 inner = expression(PAssign);
                            if (inner < 0) return -1;
                            a = node(N::Spread, sp);
                            nd(a).a = inner;
                        } else {
                            a = expression(PAssign);
                        }
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
                if (nd(e).kind != N::Name && nd(e).kind != N::Index && nd(e).kind != N::Member) return fail(t.pos, "++/-- só em variável");
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
                if (t.text == "null") return node(N::Null, t.pos);
                if (t.text == "undefined") return node(N::Undefined, t.pos);
                if (t.text == "NaN") return num_node(std::nan(""), t.pos);
                if (t.text == "Infinity") return num_node(INFINITY, t.pos);
                if (t.text == "function") {
                    // Função como valor: function (a, b) { ... } (o nome, se vier, é só enfeite).
                    std::string name;
                    if (peek().kind == Tok::Ident) { name = peek().text; ++at; }
                    const i32 fi = function_rest(t.pos, name, false, false);
                    if (fi < 0) return -1;
                    const i32 n = node(N::Func, t.pos);
                    nd(n).sym = static_cast<u32>(fi);
                    return n;
                }
                if (is("=>")) {   // x => x * 2
                    --at;
                    const i32 fi = function_rest(t.pos, {}, true, true);
                    if (fi < 0) return -1;
                    const i32 n = node(N::Func, t.pos);
                    nd(n).sym = static_cast<u32>(fi);
                    return n;
                }
                static constexpr const char* kReserved[] = {"var", "let", "const", "if", "else", "for", "while",
                                                           "return", "break", "continue", "new", "try", "catch",
                                                           "finally", "throw", "switch", "case", "default", "do",
                                                           "typeof", "class"};
                for (const char* r : kReserved) {
                    if (t.text == r) return fail(t.pos, "'" + t.text + "' fora do lugar");
                }
                const i32 n = node(N::Name, t.pos);
                if (n >= 0) nd(n).sym = intern(t.text);
                return n;
            }
            case Tok::Punct: {
                if (t.text == "(") {
                    if (arrow_ahead()) {   // (a, b) => a + b
                        const i32 fi = function_rest(t.pos, {}, true, false);
                        if (fi < 0) return -1;
                        const i32 n = node(N::Func, t.pos);
                        nd(n).sym = static_cast<u32>(fi);
                        return n;
                    }
                    ++at;
                    const i32 e = expression(PAssign);
                    if (e < 0 || !expect(")")) return -1;
                    return e;
                }
                if (t.text == "[") {
                    ++at;
                    const i32 n = node(N::Array, t.pos);
                    std::vector<i32> elems;
                    while (!is("]")) {
                        i32 e;
                        if (is("...")) {   // [...a, ...b]
                            const u32 sp = peek().pos;
                            ++at;
                            const i32 inner = expression(PAssign);
                            if (inner < 0) return -1;
                            e = node(N::Spread, sp);
                            nd(e).a = inner;
                        } else {
                            e = expression(PAssign);
                        }
                        if (e < 0) return -1;
                        elems.push_back(e);
                        if (elems.size() > 4096) return fail(t.pos, "vetor literal com itens demais");
                        if (!accept(",")) break;
                    }
                    if (!expect("]")) return -1;
                    set_kids(n, elems);
                    return n;
                }
                if (t.text == "{") {
                    // Objeto: { chave: valor, "outra": 2, abreviada, [calculada]: 3 }
                    ++at;
                    const i32 n = node(N::Object, t.pos);
                    std::vector<i32> pairs;
                    while (!is("}")) {
                        const Token& kt = peek();
                        i32 key = -1, value = -1;
                        if (kt.kind == Tok::Ident || kt.kind == Tok::Str) {
                            ++at;
                            key = str_node(kt.text, kt.pos);
                            if (kt.kind == Tok::Ident && !is(":")) {
                                if (is("(")) {   // método abreviado: nome(a) { ... }
                                    const i32 fi = function_rest(kt.pos, kt.text, false, false);
                                    if (fi < 0) return -1;
                                    value = node(N::Func, kt.pos);
                                    nd(value).sym = static_cast<u32>(fi);
                                } else {
                                    value = name_node(kt.text, kt.pos);   // { x } = { x: x }
                                }
                            }
                        } else if (kt.kind == Tok::Num) {
                            ++at;
                            char buf[40];
                            std::snprintf(buf, sizeof(buf), "%.17g", kt.num);
                            key = str_node(buf, kt.pos);
                        } else if (is("[")) {
                            ++at;
                            key = expression(PAssign);
                            if (key < 0 || !expect("]")) return -1;
                        } else {
                            return fail(kt.pos, "esperava o nome da propriedade" + describe_found());
                        }
                        if (value < 0) {
                            if (!expect(":")) return -1;
                            value = expression(PAssign);
                            if (value < 0) return -1;
                        }
                        pairs.push_back(key);
                        pairs.push_back(value);
                        if (pairs.size() > 1024) return fail(t.pos, "objeto literal com propriedades demais");
                        if (!accept(",")) break;
                    }
                    if (!expect("}")) return -1;
                    set_kids(n, pairs);
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
/// AE expressions (lote E): com funções, cada função tem as suas variáveis
/// (parâmetros primeiro) e enxerga as das funções de fora (Node::hops).
bool resolve_names(Program& p, std::string& error, u32& errPos) {
    auto slot_in = [&](u32 f, const std::string& s) -> i32 {
        const std::vector<std::string>& locals = p.funcs[f].locals;
        for (u32 i = 0; i < locals.size(); ++i) if (locals[i] == s) return static_cast<i32>(i);
        return -1;
    };
    auto declare = [&](u32 f, const std::string& s) {
        if (slot_in(f, s) < 0) p.funcs[f].locals.push_back(s);
    };
    auto lookup = [&](u32 f, const std::string& s, u32& hops) -> i32 {
        hops = 0;
        for (i32 cur = static_cast<i32>(f); cur >= 0; cur = p.funcs[static_cast<usize>(cur)].parent, ++hops) {
            const i32 slot = slot_in(static_cast<u32>(cur), s);
            if (slot >= 0) return slot;
        }
        return -1;
    };
    // 1) declarações explícitas (podem sombrear um global).
    for (const Node& n : p.nodes) {
        if (n.kind == N::VarDecl || n.kind == N::FuncDecl) declare(n.fn, p.strings[n.sym]);
    }
    // 2) atribuição a nome não declarado: vira variável do programa — a menos que seja global.
    for (const Node& n : p.nodes) {
        i32 ti = -1;
        if (n.kind == N::Assign || n.kind == N::IncDec) ti = n.a;
        else if (n.kind == N::ForOf) ti = n.c;
        if (ti < 0) continue;
        const Node& target = p.nodes[static_cast<usize>(ti)];
        if (target.kind != N::Name) continue;
        const std::string& name = p.strings[target.sym];
        u32 hops = 0;
        if (lookup(target.fn, name, hops) >= 0) continue;
        if (const u32 g = find_global(name); g && !shadowable_global(g)) {
            error = "não dá para atribuir a '" + name + "'";
            errPos = target.pos;
            return false;
        }
        declare(0, name);
    }
    for (const Func& f : p.funcs) {
        if (f.locals.size() > kMaxLocals) {
            error = "variáveis demais";
            errPos = f.pos;
            return false;
        }
    }
    for (Node& n : p.nodes) {
        if (n.kind == N::VarDecl) {
            n.sym = static_cast<u32>(slot_in(n.fn, p.strings[n.sym]));
        } else if (n.kind == N::FuncDecl) {
            const i32 slot = slot_in(n.fn, p.strings[n.sym]);
            n.sym = static_cast<u32>(slot);
            p.funcs[n.fn].decls.emplace_back(static_cast<u32>(slot), static_cast<u32>(n.b));
        } else if (n.kind == N::Name) {
            const std::string& name = p.strings[n.sym];
            u32 hops = 0;
            const i32 s = lookup(n.fn, name, hops);
            if (s >= 0) {
                n.kind = N::Local;
                n.first = n.sym;          // o nome, para as mensagens
                n.sym = static_cast<u32>(s);
                n.hops = static_cast<u8>(hops);
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
    p.localNames = p.funcs[0].locals;
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
    Lexer lx{source, {}, {}, 0, 0};
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
/// Markers = as marcas da composição da camada (`marker`, `thisComp.marker`);
/// MarkerKey = uma delas (`a` = índice na lista, em ordem de tempo).
/// Rect = a caixa de `sourceRectAtTime` (v = top, left, width, height). Comp e
/// Layer guardam em v[3] de qual composição são (0 = a da expressão; k = a
/// k-ésima da timeline, ver Ctx::comp_of).
enum class K : u8 { Undef, Num, Vec, Str, Fn, Method, Layer, Comp, Transform, Effect, Prop, Key, Math, Markers, MarkerKey, Rect,
                    Text,  ///< v70: layer.text (o grupo Texto do AE; .sourceText)
                    // AE expressions (lote E). Arr/Obj/Path moram em listas do contexto (`a` = índice):
                    // são referências, como no JavaScript. Vetor de 1 a 4 números continua Vec (valor).
                    Null, Arr, Obj,
                    UFn,       ///< função da pessoa: id = função, a/b = quadro em que nasceu (índice, série)
                    Path,      ///< caminho pronto (createPath, ou um caminho lido num instante)
                    PathProp,  ///< caminho de máscara/forma como propriedade (a = Ctx::pathRefs)
                    Mask,      ///< layer.mask("x"): a/b = camada, id = Mask::id
                    VGroup,    ///< layer.content("Grupo"): a/b = camada, id = índice do grupo
                    Footage,   ///< footage("nome") / layer.source: a = Ctx::footages
                    Ns         ///< Array / Object (id = GArrayNs / GObjectNs)
};

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
    f64           offset = 0.0;   ///< somado depois da escala (Menu suspenso: índice 0.. → 1.., como no AE)
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
    f64          f;
    u64          textContext = 0;
    bool operator==(const MemoKey&) const noexcept = default;
};
struct MemoHash {
    usize operator()(const MemoKey& k) const noexcept {
        return std::hash<const void*>()(k.t) ^ (std::hash<f64>()(k.f) * 0x9E3779B97F4A7C15ull)
             ^ (std::hash<u64>()(k.textContext) * 0x85EBCA77u);
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
        layersBuilt = false;
        layerOwners.clear();
        masksBuilt = false;
        maskOwners.clear();
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
    /// v70: a composição e o id de uma camada (a expressão do texto-fonte).
    std::unordered_map<const Layer*, Owner> layerOwners;
    bool layersBuilt = false;
    Owner find_layer(const Layer* layer) {
        if (!layersBuilt && tl) {
            layersBuilt = true;
            tl->for_each_composition([&](CompositionId, const Composition& c) {
                c.layers().for_each([&](LayerId id, const Layer& l) { layerOwners[&l] = Owner{&c, &l, id}; });
            });
        }
        const auto it = layerOwners.find(layer);
        return it == layerOwners.end() ? Owner{} : it->second;
    }
    /// AE expressions: a camada de uma máscara com expressão no caminho.
    std::unordered_map<const Mask*, Owner> maskOwners;
    bool masksBuilt = false;
    Owner find_mask(const Mask* mask) {
        if (!masksBuilt && tl) {
            masksBuilt = true;
            tl->for_each_composition([&](CompositionId, const Composition& c) {
                c.layers().for_each([&](LayerId id, const Layer& l) {
                    for (const Mask& m : l.masks) if (m.pathExpression) maskOwners[&m] = Owner{&c, &l, id};
                });
            });
        }
        const auto it = maskOwners.find(mask);
        return it == maskOwners.end() ? Owner{} : it->second;
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
thread_local u64 g_textContext = 0;
thread_local Vec3 g_textSelector{100.0f, 100.0f, 100.0f};

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
    if (d.kind == 4) return "animador de camada";
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
    const f32* ref = text::animator_param_ref(a, p);
    return ref ? *ref : 0.0f;
}

const Track* find_track(const PropDesc& d, u32 c) noexcept {
    if (!d.layer) return d.single;
    switch (d.kind) {
        case 0: return d.layer->tracks.find(static_cast<TrackProperty>(static_cast<u16>(d.prop) + c));
        case 1: return d.layer->tracks.find(TrackProperty::EffectParam, d.effectIndex, d.key0 + c);
        case 2: return d.single;
        case 3: return d.layer->tracks.find(TrackProperty::TextAnimParam, d.effectIndex, d.key0 + c);
        case 4: return d.layer->tracks.find(TrackProperty::LayerAnimParam, d.effectIndex, d.key0 + c);
        case 5: return d.layer->tracks.find(TrackProperty::MaskParam, d.effectIndex, d.key0 + c);
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
    if (d.kind == 4) {
        return d.effectIndex < l->layerAnimators.size()
                 ? layeranim::param_static(l->layerAnimators[d.effectIndex], d.key0 + c) : 0.0;
    }
    if (d.kind == 2) return tr ? tr->staticValue : 0.0;
    if (d.kind == 5) {
        for (const Mask& mask : l->masks) if (mask.id == d.effectIndex) {
            switch (d.key0 + c) { case 0: return mask.feather; case 1: return mask.expansion; case 2: return mask.opacity; default: return 0; }
        }
        return 0;
    }
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
        case TP::TextPathFirstMargin: return l->text.pathOffset;
        case TP::TextPathLastMargin: return l->text.pathLastMargin;
        case TP::AudioVolume: return tr ? tr->staticValue : 1.0;
        case TP::Speed: return l->speed;
        case TP::CameraPoiX: return l->camera.poi.x;   // AE animation: Ponto de Interesse
        case TP::CameraPoiY: return l->camera.poi.y;
        case TP::CameraPoiZ: return l->camera.poi.z;
        default: return tr ? tr->staticValue : 0.0;
    }
}

/// AE expressions (lote E): o Menu suspenso (Dropdown Menu Control).
bool is_dropdown_control(EffectTypeId type) noexcept {
    static const EffectTypeId id = effect_type_id(effect_keys::kDropdownControl);
    return type == id;
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
        case TP::CameraPoiX: case TP::CameraPoiY: case TP::CameraPoiZ:   // AE animation: [x, y, z]
            group3(TP::CameraPoiX);
            d.count = 3;
            break;
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
                if (pi == 0 && is_dropdown_control(e.type)) d.offset = 1.0;   // Menu suspenso: 1.., como no AE
            }
            d.count = static_cast<u8>(std::clamp<u32>(comps, 1, 4));
            break;
        }
        case TP::TextAnimParam: {
            d.kind = 3;
            d.effectIndex = t.effectIndex;
            d.key0 = t.effectParamIndex;
            // Propriedades vetoriais do animador (posição XYZ, escala XY, cores
            // RGB): a expressão devolve o vetor inteiro, como no After Effects,
            // e cada trilha pega o SEU componente (antes todas pegavam o [0]).
            const u32 p = t.effectParamIndex;
            auto group = [&](u32 first, u32 n) {
                d.key0 = first;
                d.count = static_cast<u8>(n);
                outComponent = p - first;
            };
            if (p >= text::kPosX && p <= text::kPosZ) group(text::kPosX, 3);
            else if (p >= text::kScaleX && p <= text::kScaleY) group(text::kScaleX, 2);
            else if (p >= text::kFillR && p <= text::kFillB) group(text::kFillR, 3);
            else if (p >= text::kStrokeR && p <= text::kStrokeB) group(text::kStrokeR, 3);
            break;
        }
        case TP::MaskParam:
            d.kind = 5; d.effectIndex = t.effectIndex; d.key0 = t.effectParamIndex;
            break;
        case TP::LayerAnimParam:   // animador de camada: um valor por trilha
            d.kind = 4;
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
    // AE expressions (lote E).
    {"aurea.control.dropdown", "Dropdown Menu Control", "Menu"},
    {"aurea.control.layer", "Layer Control", "Layer"},
    {"aurea.control.point3d", "3D Point Control", "3D Point"},
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

/// Ruído de VALOR 1D em [-1, 1]: sorteios da grade interpolados (suave). O de
/// gradiente zera nos inteiros — e o tremor na taxa do projeto (30 Hz a 30
/// fps) cai num inteiro a cada quadro; este não.
f64 vnoise1(u32 seed, f64 x) noexcept {
    const f64 fl = std::floor(x);
    const i64 i = static_cast<i64>(fl);
    const f64 f = x - fl;
    const f64 w = f * f * (3.0 - 2.0 * f);
    const f64 a = lattice(seed, i), b = lattice(seed, i + 1);
    return a + (b - a) * w;
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
// --- v70: texto por letra (UTF-8 ↔ códigos) -----------------------------------
std::vector<u32> utf8_codepoints(std::string_view s) {
    std::vector<u32> out;
    out.reserve(s.size());
    for (usize i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        u32 cp = 0xFFFD;
        usize n = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 0x6 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<u8>(s[i + 1]) & 0x3Fu); n = 2; }
        else if ((c >> 4) == 0xE && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<u8>(s[i + 2]) & 0x3Fu);
            n = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 12) | ((static_cast<u8>(s[i + 2]) & 0x3Fu) << 6)
               | (static_cast<u8>(s[i + 3]) & 0x3Fu);
            n = 4;
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}
std::string utf8_encode(const u32* cps, usize n) {
    std::string out;
    out.reserve(n);
    for (usize i = 0; i < n; ++i) {
        const u32 cp = cps[i];
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    return out;
}
/// Maiúscula/minúscula: ASCII, acentos do Latin-1 (á → Á) e cirílico.
u32 cp_case(u32 c, bool upper) noexcept {
    if (upper) {
        if (c >= 'a' && c <= 'z') return c - 32;
        if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
        if (c >= 0x430 && c <= 0x44F) return c - 32;
        if (c >= 0x450 && c <= 0x45F) return c - 80;
    } else {
        if (c >= 'A' && c <= 'Z') return c + 32;
        if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
        if (c >= 0x410 && c <= 0x42F) return c + 32;
        if (c >= 0x400 && c <= 0x40F) return c + 80;
    }
    return c;
}

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
    const Timeline*    tl = nullptr;        ///< AE animation: comp("Nome") e camadas de outras composições
    const Mask*        maskSelf = nullptr;  ///< AE expressions: a máscara dona, quando a expressão é a do caminho
    /// v70: expressão do texto-fonte — o texto antes dela (`value`, `text.sourceText`).
    const std::string* textValue = nullptr;
};

f64 layer_local_from_comp(const Layer* l, f64 compF) noexcept {
    return l ? compF - static_cast<f64>(l->start.value) + static_cast<f64>(l->offset.value) : compF;
}

// =============================================================================
// Avaliador
// =============================================================================
enum class Flow : u8 { None, Return, Break, Continue };

/// Um caminho lido como propriedade: de quem é e qual.
struct PathRef {
    Val owner;          ///< a camada (K::Layer, com a composição em v[3])
    u8  kind = 0;       ///< 0 máscara (a = Mask::id), 1 forma (a = grupo, b = caminho), 2 o caminho de entrada da expressão
    u32 a = 0, b = 0;
};
struct ObjEntry { std::string key; Val v; };
struct Frame { u32 base = 0, fn = 0, parent = 0, serial = 0; };

struct Ctx {
    const Program* p = nullptr;
    const Env*     env = nullptr;
    u32            steps = 0;
    bool           failed = false;
    bool           hard = false;      ///< estouro de limite: `catch` não segura
    std::string    err;
    u32            errPos = 0;
    Flow           flow = Flow::None;
    Val            ret;
    // Variáveis: uma pilha de quadros (o 0 é o programa; cada chamada de função abre um).
    std::vector<Val>   stack;
    std::vector<u8>    defd;
    std::vector<Frame> frames;
    u32            cur = 0;
    u32            serialGen = 0;
    u32            curDepth = 0;      ///< profundidade nativa na última chamada (para as funções de retorno)
    u64            heap = 0;          ///< bytes de texto/vetor/objeto criados nesta avaliação
    bool           hasThrown = false;
    Val            thrown;
    std::vector<PropDesc>    props;
    std::vector<Val>         recv;
    std::vector<std::string> rtStrings;
    std::vector<std::vector<Val>>      arrays;   u32 arraysUsed = 0;
    std::vector<std::vector<ObjEntry>> objects;  u32 objectsUsed = 0;
    std::vector<BezierPath>            paths;    u32 pathsUsed = 0;
    std::vector<PathRef>               pathRefs;
    std::vector<FootageInfo>           footages;
    const std::vector<MaskPoint>*      inPath = nullptr;   ///< expressão de caminho: o caminho de antes
    bool                               inPathClosed = true;
    u64            rng = 0;
    bool           rngTimeless = false;
    u32            rngUserSeed = 0;
    /// "Agora" desta avaliação, em quadros da composição. Começa no quadro de
    /// verdade; `posterizeTime(n)` o prende em degraus de 1/n s — e aí `time`,
    /// `value`, o aleatório e os padrões de wiggle/shake passam a ler o degrau.
    f64            nowF = 0.0;
    Val            nowValue;

    /// Quadro LOCAL da camada no "agora" (posterizado ou não).
    [[nodiscard]] f64 now_local() const { return env ? env->localF + (nowF - env->compF) : nowF; }

    void reset(const Program* prog, const Env* e) {
        p = prog;
        env = e;
        nowF = e ? e->compF : 0.0;
        nowValue = e ? e->value : Val{};
        steps = 0;
        failed = false;
        err.clear();
        errPos = 0;
        flow = Flow::None;
        ret = Val{};
        hard = false;
        heap = 0;
        hasThrown = false;
        thrown = Val{};
        stack.clear();
        defd.clear();
        frames.clear();
        cur = 0;
        serialGen = 0;
        curDepth = 0;
        props.clear();
        recv.clear();
        rtStrings.clear();
        arraysUsed = objectsUsed = pathsUsed = 0;
        pathRefs.clear();
        footages.clear();
        rngTimeless = false;
        rngUserSeed = 0;
        seed_rng();
        if (prog && !prog->funcs.empty()) push_frame(0, 0);
    }

    Val fail(u32 pos, std::string msg) {
        if (!failed) { failed = true; err = std::move(msg); errPos = pos; }
        return Val{};
    }
    /// Estouro de limite (instruções, memória, recursão): nem `try` segura.
    Val fail_hard(u32 pos, std::string msg) {
        if (!failed) { failed = true; err = std::move(msg); errPos = pos; }
        hard = true;
        return Val{};
    }

    // --- quadros (variáveis) --------------------------------------------------
    void push_frame(u32 fn, u32 parent) {
        Frame fr;
        fr.base = static_cast<u32>(stack.size());
        fr.fn = fn;
        fr.parent = parent;
        fr.serial = ++serialGen;
        const usize nl = p->funcs[fn].locals.size();
        stack.resize(fr.base + nl);
        defd.resize(fr.base + nl, 0);
        frames.push_back(fr);
        cur = static_cast<u32>(frames.size() - 1);
        // `function f(){}` vale no escopo inteiro (içada).
        for (const auto& d : p->funcs[fn].decls) {
            Val c;
            c.k = K::UFn; c.id = d.second; c.a = cur; c.b = fr.serial;
            stack[fr.base + d.first] = c;
            defd[fr.base + d.first] = 1;
        }
    }
    void pop_frame(u32 back) {
        const u32 base = frames.back().base;
        frames.pop_back();
        stack.resize(base);
        defd.resize(base);
        cur = back;
    }
    [[nodiscard]] u32 local_index(const Node& n) const {
        u32 f = cur;
        for (u32 h = n.hops; h > 0; --h) f = frames[f].parent;
        return frames[f].base + n.sym;
    }
    [[nodiscard]] const std::string& local_name(const Node& n) const {
        if (n.kind == N::Local) return p->strings[n.first];
        return p->funcs[n.fn].locals[n.sym];
    }

    // --- memória da avaliação ---------------------------------------------------
    bool charge(u64 bytes) {
        heap += bytes;
        if (heap <= kMaxHeapBytes) return true;
        fail_hard(curPos, "a expressão usou memória demais (mais de 8 MB de texto e vetores num quadro)");
        return false;
    }
    Val make_arr() {
        if (arraysUsed == arrays.size()) arrays.emplace_back();
        arrays[arraysUsed].clear();
        Val r; r.k = K::Arr; r.a = arraysUsed++;
        (void)charge(32);
        return r;
    }
    Val make_obj() {
        if (objectsUsed == objects.size()) objects.emplace_back();
        objects[objectsUsed].clear();
        Val r; r.k = K::Obj; r.a = objectsUsed++;
        (void)charge(32);
        return r;
    }
    Val make_path(BezierPath path) {
        (void)charge(64 + path.v.size() * sizeof(BezierVertex));
        if (pathsUsed == paths.size()) paths.emplace_back();
        paths[pathsUsed] = std::move(path);
        Val r; r.k = K::Path; r.a = pathsUsed++;
        return r;
    }
    bool arr_push(u32 arrIndex, const Val& v) {
        if (arrays[arrIndex].size() >= kMaxArrayLength) { fail_hard(curPos, "vetor grande demais (mais de 65536 itens)"); return false; }
        if (!charge(sizeof(Val))) return false;
        arrays[arrIndex].push_back(v);
        return true;
    }
    /// Vetor de 1 a 4 números → Vec (valor), o resto fica como está.
    Val norm(const Val& v) const {
        if (v.k != K::Arr) return v;
        const std::vector<Val>& a = arrays[v.a];
        if (a.empty() || a.size() > 4) return v;
        f64 out[4]{};
        for (usize i = 0; i < a.size(); ++i) {
            if (a[i].k != K::Num) return v;
            out[i] = a[i].v[0];
        }
        Val r = Val::vec(out, static_cast<u32>(a.size()));
        r.k = K::Vec;
        return r;
    }
    /// Vec/Num → vetor de verdade (para push, map...). Arr devolve o próprio.
    Val to_arr(const Val& v) {
        if (v.k == K::Arr) return v;
        const Val r = make_arr();
        if (v.k == K::Vec || v.k == K::Num) {
            for (u32 i = 0; i < v.n; ++i) if (!arr_push(r.a, Val::num(v.v[i]))) break;
        }
        return r;
    }
    Val obj_get(const Val& o, std::string_view key) const {
        for (const ObjEntry& e : objects[o.a]) if (e.key == key) return e.v;
        return Val{};
    }
    bool obj_set(const Val& o, std::string_view key, const Val& v) {
        for (ObjEntry& e : objects[o.a]) if (e.key == key) { e.v = v; return true; }
        if (objects[o.a].size() >= 4096) { fail_hard(curPos, "objeto com propriedades demais"); return false; }
        if (!charge(sizeof(ObjEntry) + key.size())) return false;
        objects[o.a].push_back(ObjEntry{std::string(key), v});
        return true;
    }
    u32 curPos = 0;

    // --- aleatório: xorshift64* semeado por camada/propriedade/quadro ---------
    void seed_rng() {
        const i64 f = rngTimeless ? 0 : static_cast<i64>(std::floor(nowF));
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
        (void)charge(s.size() + 24);
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
        const Composition* c = comp_of(v);
        if (!c) return nullptr;
        return c->layer(LayerId{v.a, v.b});
    }
    /// AE animation: a composição de um Comp/Layer/Transform/Effect (v[3]: 0 =
    /// a da expressão; k = a k-ésima da timeline, a ordem de `comp("Nome")`).
    const Composition* comp_of(const Val& v) const {
        const u32 tag = v.v[3] > 0.0 ? static_cast<u32>(v.v[3]) : 0u;
        if (tag == 0) return env->comp;
        if (!env->tl) return nullptr;
        const Composition* found = nullptr;
        u32 i = 0;
        env->tl->for_each_composition([&](CompositionId, const Composition& c) { if (++i == tag) found = &c; });
        return found;
    }
    Val layer_space(u32 which, const Val& layerVal, const Val* args, u32 argc, u32 pos);
    Val look_at(const Val* args, u32 argc, u32 pos);
    Val comp_fn(const Val* args, u32 argc, u32 pos);
    Val source_rect(const Val& layerVal, const Val* args, u32 argc, u32 pos);

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
        if (!tr) return base * d.scale + d.offset;
        const f64 v = post && !own && tr->has_expression()
            ? tr->value_or_f(localF, static_cast<f32>(base))
            : (tr->keys.empty() ? base : static_cast<f64>(tr->sample_keys_f(localF)));
        return v * d.scale + d.offset;
    }

    Val prop_value(const PropDesc& d, f64 compF, bool post, u32 pos) {
        f64 out[4]{};
        const f64 local = layer_local_from_comp(d.layer, compF);
        for (u32 c = 0; c < d.count; ++c) out[c] = component(d, c, local, post);
        if (g_tls.cycle) return fail(pos, g_tls.cycleMsg.empty() ? "dependência circular" : g_tls.cycleMsg);
        return Val::vec(out, d.count);
    }

    /// Derivada no instante (por segundo), como a velocity do AE: diferença
    /// central de ±1/100 de quadro. Com ±1 quadro, perto de um key a conta
    /// misturava os dois trechos — velocityAtTime(key.time − .001) dos bounces
    /// clássicos saía com a METADE da velocidade de chegada.
    Val prop_velocity(const PropDesc& d, f64 compF, u32 pos) {
        constexpr f64 kHalfStep = 0.01;   // quadros
        const Val a = prop_value(d, compF - kHalfStep, true, pos);
        const Val b = prop_value(d, compF + kHalfStep, true, pos);
        if (failed) return Val{};
        f64 out[4]{};
        for (u32 c = 0; c < a.n; ++c) out[c] = (b.v[c] - a.v[c]) * env->fps * (0.5 / kHalfStep);
        return Val::vec(out, a.n);
    }

    /// Coerção: propriedade vira o valor no instante atual.
    Val deref(const Val& v, u32 pos) {
        if (v.k == K::Prop) return prop_value(props[v.a], nowF, true, pos);
        return v;
    }

    bool truthy(const Val& v) const {
        switch (v.k) {
            case K::Num: return v.v[0] != 0.0 && !std::isnan(v.v[0]);
            case K::Str: return !str(v).empty();
            case K::Undef: case K::Null: return false;
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

    /// v70: o valor como texto, do jeito do JavaScript (2 → "2", 0.5 → "0.5",
    /// [1, 2] → "1,2"). Objetos viram o nome do tipo.
    std::string to_text(const Val& v, u32 nest = 0) {
        if (nest > 12) return {};   // vetor dentro de vetor dentro de…
        switch (v.k) {
            case K::Str: return std::string(str(v));
            case K::Num: return number_text(v.v[0]);
            case K::Vec: {
                std::string s;
                for (u32 i = 0; i < v.n; ++i) { if (i) s += ','; s += number_text(v.v[i]); }
                return s;
            }
            case K::Undef: return "undefined";
            case K::Null: return "null";
            case K::Arr: {
                std::string s;
                const usize count = arrays[v.a].size();
                for (usize i = 0; i < count && s.size() < 64 * 1024; ++i) {
                    if (i) s += ',';
                    const Val item = arrays[v.a][i];
                    if (item.k == K::Arr && item.a == v.a) continue;   // vetor dentro dele mesmo
                    if (item.k != K::Undef && item.k != K::Null) s += to_text(item, nest + 1);
                }
                return s;
            }
            case K::Obj: {
                const Val m = obj_get(v, "message");   // o erro de um catch (e)
                if (m.k == K::Str) return "Error: " + std::string(str(m));
                return "[object Object]";
            }
            case K::Fn: case K::Method: case K::UFn: return "function";
            default: return "[object]";
        }
    }
    static std::string number_text(f64 x) {
        if (std::isnan(x)) return "NaN";
        if (std::isinf(x)) return x > 0 ? "Infinity" : "-Infinity";
        if (x == 0.0) return "0";
        char buf[40];
        if (std::fabs(x) < 1e21 && x == std::floor(x)) {
            std::snprintf(buf, sizeof(buf), "%.0f", x);
            return buf;
        }
        // O menor número de casas que volta ao mesmo valor (até 17), como o JS.
        for (int prec = 1; prec <= 17; ++prec) {
            std::snprintf(buf, sizeof(buf), "%.*g", prec, x);
            if (std::strtod(buf, nullptr) == x) break;
        }
        return buf;
    }

    // --- aritmética ------------------------------------------------------------
    Val arith(u8 op, const Val& x0, const Val& y0, u32 pos) {
        if (op == OpEq || op == OpNe) return Val::num((op == OpEq) == loose_equal(x0, y0) ? 1.0 : 0.0);
        const Val x = norm(x0), y = norm(y0);   // vetor de verdade com 1 a 4 números entra na conta como vetor
        if (x.k == K::Str || y.k == K::Str) {
            if (op == OpEq || op == OpNe) {
                const bool eq = x.k == y.k && str(x) == str(y);
                return Val::num((op == OpEq) == eq ? 1.0 : 0.0);
            }
            // v70: "+" com texto junta (como no JavaScript do AE).
            if (op == OpAdd) {
                std::string s = to_text(x);
                s += to_text(y);
                if (s.size() > 64 * 1024) return fail(pos, "texto longo demais (mais de 64 KB)");
                return make_str(std::move(s));
            }
            if (x.k == K::Str && y.k == K::Str && (op == OpLt || op == OpLe || op == OpGt || op == OpGe)) {
                const int c = str(x).compare(str(y));
                const bool r = op == OpLt ? c < 0 : op == OpLe ? c <= 0 : op == OpGt ? c > 0 : c >= 0;
                return Val::num(r ? 1.0 : 0.0);
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
        curPos = pos;
        if (++steps > kMaxInstructions) {
            fail_hard(pos, "limite de " + std::to_string(kMaxInstructions) + " instruções por quadro excedido (laço infinito?)");
            return false;
        }
        return true;
    }

    Val exec(i32 ni, u32 depth);
    Val eval(i32 ni, u32 depth);
    Val call(const Node& n, u32 depth);
    // AE expressions (lote E).
    Val exec_forof(const Node& n, u32 depth);
    Val exec_switch(const Node& n, u32 depth);
    Val exec_try(const Node& n, u32 depth);
    Val eval_array(const Node& n, u32 depth);
    Val eval_object(const Node& n, u32 depth);
    Val eval_index(const Node& n, u32 depth);
    Val eval_assign(const Node& n, u32 depth);
    Val eval_incdec(const Node& n, u32 depth);
    Val* lref(i32 ni, u32 depth);
    Val store(i32 target, const Val& v, u8 op, u32 pos, u32 depth);
    Val invoke(const Val& callee, Val* args, u32 argc, u32 pos, u32 depth);
    Val call_user(const Val& fn, const Val* args, u32 argc, u32 pos, u32 depth);
    Val call_array(u32 id, const Val& self, const Val* args, u32 argc, u32 pos);
    Val group_call(const Val& obj, const Val* args, u32 argc, u32 pos);
    Val color_fn(u32 id, const Val* args, u32 argc, u32 pos);
    Val footage_of(std::string_view name, u64 asset, u32 pos);
    Val layer_mask(const Val& layerVal, const Val* args, u32 argc, u32 pos);
    Val layer_content(const Val& layerVal, const Val* args, u32 argc, u32 pos);
    Val group_content(const Val& group, const Val* args, u32 argc, u32 pos);
    bool resolve_path(const Val& v, f64 compF, BezierPath& out, u32 pos);
    Val path_method(u32 id, const Val& self, const Val* args, u32 argc, u32 pos);
    Val create_path(const Val* args, u32 argc, u32 pos);
    Val sample_image(const Val& layerVal, const Val* args, u32 argc, u32 pos);
    Val array_member(const Val& arr, std::string_view name, u32 pos);
    [[nodiscard]] u32 comp_key(const Val& v) const;
    static bool mutating_array_method(std::string_view name) noexcept {
        return name == "push" || name == "pop" || name == "shift" || name == "unshift" || name == "splice"
            || name == "reverse" || name == "sort" || name == "fill";
    }
    /// Igualdade (== e ===): números e vetores por valor, texto por texto,
    /// null com undefined; vetor de verdade, objeto e camada por identidade.
    bool loose_equal(const Val& x0, const Val& y0) const {
        const Val x = norm(x0), y = norm(y0);
        const bool xn = x.k == K::Undef || x.k == K::Null, yn = y.k == K::Undef || y.k == K::Null;
        if (xn || yn) return xn && yn;
        if (x.k == K::Str || y.k == K::Str) return x.k == y.k && str(x) == str(y);
        const bool xv = x.k == K::Num || x.k == K::Vec, yv = y.k == K::Num || y.k == K::Vec;
        if (xv && yv) {
            if (x.k == K::Num && y.k == K::Num) return x.v[0] == y.v[0];
            bool eq = x.n == y.n;
            for (u32 i = 0; eq && i < x.n; ++i) eq = x.v[i] == y.v[i];
            return eq;
        }
        if (x.k != y.k) return false;
        return x.a == y.a && x.b == y.b && x.id == y.id && x.v[3] == y.v[3];
    }
    Val member(const Val& obj, std::string_view name, u32 pos);
    Val global(u32 id, u32 pos);
    Val call_fn(u32 id, const Val* args, u32 argc, u32 pos);
    Val call_method(u32 id, const Val& recv, const Val* args, u32 argc, u32 pos);
    Val loop(bool out, const Val* args, u32 argc, bool duration, u32 pos);
    Val lerp_fn(u32 id, const Val* args, u32 argc, u32 pos);
    Val key_of(const PropDesc& d, u32 propIndex, f64 idx, u32 pos);
    Val nearest_key(const PropDesc& d, u32 propIndex, f64 compF, u32 pos);
    Val marker_key(const Val& obj, const Val* args, u32 argc, bool nearest, u32 pos);
    /// Timeline do AE: a lista de um Markers/MarkerKey — `n` 1 = marcas da
    /// camada `a`/`b` (tempo local), senão as da composição — e quanto somar
    /// ao quadro dela para chegar ao quadro da composição.
    const std::vector<Marker>* marker_list(const Val& v, i64& shift) const {
        shift = 0;
        if (v.n != 1) return env->comp ? &env->comp->markers() : nullptr;
        const Layer* l = layer_of(v);
        if (!l) return nullptr;
        shift = l->start.value - l->offset.value;
        return &l->markers;
    }
    Val shake(const Val* args, u32 argc, u32 pos);
    Val time_since_marker(const Val* args, u32 argc, u32 pos);

    /// Módulo de um vetor de velocidade (speed/speedAtTime).
    static Val magnitude(const Val& v) {
        f64 s = 0;
        for (u32 i = 0; i < v.n; ++i) s += v.v[i] * v.v[i];
        return Val::num(std::sqrt(s));
    }
    /// A própria trilha (o componente desta expressão), como `key()` a lê.
    PropDesc own_track() const {
        PropDesc own = env->self;
        if (own.kind == 0) own.prop = static_cast<TrackProperty>(static_cast<u16>(own.prop) + env->component), own.count = 1;
        else if (own.kind == 1 || own.kind == 3 || own.kind == 4) own.key0 += env->component, own.count = 1;
        return own;
    }
};

Val Ctx::exec(i32 ni, u32 depth) {
    if (failed || !tick(N_(ni).pos)) return Val{};
    if (depth > kMaxEvalDepth) return fail_hard(N_(ni).pos, "aninhamento profundo demais (recursão sem fim?)");
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
                stack[frames[cur].base + n.sym] = v;
            }
            defd[frames[cur].base + n.sym] = 1;
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
        case N::DoWhile: {
            Val last;
            for (;;) {
                if (!tick(n.pos)) return Val{};
                const Val v = exec(n.b, depth + 1);
                if (failed) return Val{};
                if (v.k != K::Undef) last = v;
                if (flow == Flow::Break) { flow = Flow::None; break; }
                if (flow == Flow::Return) return last;
                if (flow == Flow::Continue) flow = Flow::None;
                const Val c = eval(n.a, depth + 1);
                if (failed) return Val{};
                if (!truthy(deref(c, n.pos))) break;
            }
            return last;
        }
        case N::ForOf: return exec_forof(n, depth);
        case N::Switch: return exec_switch(n, depth);
        case N::Try: return exec_try(n, depth);
        case N::Throw: {
            const Val v = deref(eval(n.a, depth + 1), n.pos);
            if (failed) return Val{};
            thrown = v;
            hasThrown = true;
            std::string msg = v.k == K::Obj ? to_text(obj_get(v, "message")) : to_text(v);
            return fail(n.pos, msg.empty() ? std::string("erro") : msg);
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
        case N::FuncDecl: return Val{};   // içada na entrada do escopo (push_frame)
        case N::Case: return Val{};
        default: return eval(ni, depth + 1);
    }
}

/// for (x of lista) / for (k in objeto). O vetor é percorrido pelo tamanho do
/// início (quem cresce a lista dentro do laço não fica preso nele).
Val Ctx::exec_forof(const Node& n, u32 depth) {
    const Val it = deref(eval(n.a, depth + 1), n.pos);
    if (failed) return Val{};
    const Node& target = N_(n.c);
    std::vector<u32> cps;
    u32 count = 0;
    switch (it.k) {
        case K::Arr: count = static_cast<u32>(arrays[it.a].size()); break;
        case K::Vec: case K::Num: count = it.n; break;
        case K::Str: cps = utf8_codepoints(str(it)); count = static_cast<u32>(cps.size()); break;
        case K::Obj:
            if (!n.flag) return fail(n.pos, "for...of não percorre objeto (use for...in ou Object.keys)");
            count = static_cast<u32>(objects[it.a].size());
            break;
        default: return fail(n.pos, "o 'for' precisa de um vetor, um texto ou um objeto");
    }
    Val last;
    for (u32 i = 0; i < count; ++i) {
        if (!tick(n.pos)) return Val{};
        Val item;
        if (n.flag) {
            if (it.k == K::Obj) {
                if (i >= objects[it.a].size()) break;
                item = make_str(std::string(objects[it.a][i].key));
            } else {
                item = Val::num(static_cast<f64>(i));
            }
        } else if (it.k == K::Arr) {
            if (i >= arrays[it.a].size()) break;
            item = arrays[it.a][i];
        } else if (it.k == K::Str) {
            item = make_str(utf8_encode(&cps[i], 1));
        } else {
            item = Val::num(it.v[i]);
        }
        if (failed) return Val{};
        const u32 idx = target.kind == N::VarDecl ? frames[cur].base + target.sym : local_index(target);
        stack[idx] = item;
        defd[idx] = 1;
        const Val v = exec(n.b, depth + 1);
        if (failed) return Val{};
        if (v.k != K::Undef) last = v;
        if (flow == Flow::Break) { flow = Flow::None; break; }
        if (flow == Flow::Return) return last;
        if (flow == Flow::Continue) flow = Flow::None;
    }
    return last;
}

Val Ctx::exec_switch(const Node& n, u32 depth) {
    const Val d = deref(eval(n.a, depth + 1), n.pos);
    if (failed) return Val{};
    i32 start = -1, def = -1;
    for (u32 i = 0; i < n.count && start < 0; ++i) {
        const Node& c = N_(p->kids[n.first + i]);
        if (c.a < 0) { def = static_cast<i32>(i); continue; }
        const Val t = deref(eval(c.a, depth + 1), c.pos);
        if (failed) return Val{};
        if (loose_equal(d, t)) start = static_cast<i32>(i);
    }
    if (start < 0) start = def;
    if (start < 0) return Val{};
    Val last;
    for (u32 i = static_cast<u32>(start); i < n.count; ++i) {
        const Node& c = N_(p->kids[n.first + i]);
        for (u32 k = 0; k < c.count; ++k) {
            const Val v = exec(p->kids[c.first + k], depth + 1);
            if (failed) return Val{};
            if (v.k != K::Undef) last = v;
            if (flow == Flow::Break) { flow = Flow::None; return last; }
            if (flow != Flow::None) return last;   // return e continue sobem
        }
    }
    return last;
}

/// try / catch / finally. O `catch` segura erro de execução da expressão
/// (camada que não existe, índice fora…) e `throw`; estouro de limite não.
Val Ctx::exec_try(const Node& n, u32 depth) {
    const usize frameCount = frames.size();
    const u32 frameCur = cur;
    Val result = exec(n.a, depth + 1);
    if (failed && !hard && n.b >= 0) {
        while (frames.size() > frameCount) pop_frame(frameCur);   // por garantia: as chamadas já fecharam os delas
        cur = frameCur;
        const std::string message = err;
        const bool hadThrown = hasThrown;
        const Val thrownVal = thrown;
        failed = false;
        err.clear();
        errPos = 0;
        hasThrown = false;
        flow = Flow::None;
        Val e = thrownVal;
        if (!hadThrown) {
            e = make_obj();
            (void)obj_set(e, "message", make_str(message));
            (void)obj_set(e, "name", make_str("Error"));
        }
        if (failed) return Val{};
        if (n.d >= 0) {
            const u32 idx = frames[cur].base + N_(n.d).sym;
            stack[idx] = e;
            defd[idx] = 1;
        }
        result = exec(n.b, depth + 1);
    }
    if (n.c >= 0 && !hard) {
        // finally roda sempre; o erro ou o `return` pendente continuam valendo depois dele.
        const bool wasFailed = failed;
        const std::string savedErr = err;
        const u32 savedPos = errPos;
        const Flow savedFlow = flow;
        const Val savedRet = ret;
        failed = false;
        flow = Flow::None;
        (void)exec(n.c, depth + 1);
        if (!failed && flow == Flow::None) {
            failed = wasFailed;
            err = savedErr;
            errPos = savedPos;
            flow = savedFlow;
            ret = savedRet;
        }
    }
    return failed ? Val{} : result;
}

Val Ctx::eval(i32 ni, u32 depth) {
    if (failed || !tick(N_(ni).pos)) return Val{};
    if (depth > kMaxEvalDepth) return fail_hard(N_(ni).pos, "aninhamento profundo demais (recursão sem fim?)");
    const Node& n = N_(ni);
    switch (n.kind) {
        case N::Num: return Val::num(n.num);
        case N::Str: { Val r; r.k = K::Str; r.a = n.sym; return r; }
        case N::Null: { Val r; r.k = K::Null; return r; }
        case N::Undefined: return Val{};
        case N::Local: {
            const u32 idx = local_index(n);
            if (!defd[idx]) return fail(n.pos, "variável '" + local_name(n) + "' usada antes de receber valor");
            return stack[idx];
        }
        case N::Global: return global(n.sym, n.pos);
        case N::Array: return eval_array(n, depth);
        case N::Object: return eval_object(n, depth);
        case N::Func: {
            Val c;
            c.k = K::UFn; c.id = n.sym; c.a = cur; c.b = frames[cur].serial;
            return c;
        }
        case N::Unary: {
            const Val x = norm(deref(eval(n.a, depth + 1), n.pos));
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
        case N::Coalesce: {
            const Val x = deref(eval(n.a, depth + 1), n.pos);
            if (failed || (x.k != K::Undef && x.k != K::Null)) return x;
            return deref(eval(n.b, depth + 1), n.pos);
        }
        case N::Cond: {
            const Val c = deref(eval(n.a, depth + 1), n.pos);
            if (failed) return Val{};
            return eval(truthy(c) ? n.b : n.c, depth + 1);
        }
        case N::TypeOf: {
            const Val x = eval(n.a, depth + 1);
            if (failed) return Val{};
            switch (x.k) {
                case K::Undef: return make_str("undefined");
                case K::Num: return make_str("number");
                case K::Str: return make_str("string");
                case K::Fn: case K::Method: case K::UFn: return make_str("function");
                default: return make_str("object");
            }
        }
        case N::Assign: return eval_assign(n, depth);
        case N::IncDec: return eval_incdec(n, depth);
        case N::Member: {
            const Val obj = eval(n.a, depth + 1);
            if (failed) return Val{};
            return member(obj, p->strings[n.sym], n.pos);
        }
        case N::Index: return eval_index(n, depth);
        case N::Call: return call(n, depth);
        case N::Spread: return fail(n.pos, "'...' só vale dentro de [ ] ou dos argumentos de uma chamada");
        case N::Seq: {
            Val last;
            for (u32 i = 0; i < n.count; ++i) {
                last = eval(p->kids[n.first + i], depth + 1);
                if (failed) return Val{};
            }
            return last;
        }
        default: return exec(ni, depth + 1);
    }
}

/// [a, b, c]: de 1 a 4 números é o vetor de sempre (valor); qualquer outra
/// coisa (vazio, mais de 4, texto, vetor dentro de vetor, ...espalhado) é um
/// vetor de verdade (referência, com push/map/…).
Val Ctx::eval_array(const Node& n, u32 depth) {
    Val head[4];
    u32 got = 0;
    if (n.count >= 1 && n.count <= 4) {
        bool plain = true;
        f64 out[4]{};
        for (u32 i = 0; i < n.count; ++i) {
            const i32 ki = p->kids[n.first + i];
            if (N_(ki).kind == N::Spread) { plain = false; break; }
            head[i] = deref(eval(ki, depth + 1), n.pos);
            if (failed) return Val{};
            got = i + 1;
            if (head[i].k != K::Num) { plain = false; break; }
            out[i] = head[i].v[0];
        }
        if (plain) {
            Val r = Val::vec(out, n.count);
            r.k = K::Vec;
            return r;
        }
    }
    const Val arr = make_arr();
    for (u32 i = 0; i < got; ++i) if (!arr_push(arr.a, head[i])) return Val{};
    for (u32 i = got; i < n.count; ++i) {
        const i32 ki = p->kids[n.first + i];
        const bool spread = N_(ki).kind == N::Spread;
        const Val e = deref(eval(spread ? N_(ki).a : ki, depth + 1), n.pos);
        if (failed) return Val{};
        if (!spread) {
            if (!arr_push(arr.a, e)) return Val{};
            continue;
        }
        if (e.k == K::Arr) {
            const usize count = arrays[e.a].size();
            for (usize k = 0; k < count; ++k) if (!arr_push(arr.a, arrays[e.a][k])) return Val{};
        } else if (e.k == K::Vec || e.k == K::Num) {
            for (u32 k = 0; k < e.n; ++k) if (!arr_push(arr.a, Val::num(e.v[k]))) return Val{};
        } else if (e.k == K::Str) {
            const std::vector<u32> cps = utf8_codepoints(str(e));
            for (const u32& cp : cps) if (!arr_push(arr.a, make_str(utf8_encode(&cp, 1)))) return Val{};
        } else {
            return fail(N_(ki).pos, "'...' precisa de um vetor");
        }
    }
    return failed ? Val{} : arr;
}

Val Ctx::eval_object(const Node& n, u32 depth) {
    const Val o = make_obj();
    for (u32 i = 0; i + 1 < n.count; i += 2) {
        const Val k = deref(eval(p->kids[n.first + i], depth + 1), n.pos);
        if (failed) return Val{};
        const Val v = deref(eval(p->kids[n.first + i + 1], depth + 1), n.pos);
        if (failed) return Val{};
        const std::string key = to_text(k);
        if (!obj_set(o, key, v)) return Val{};
    }
    return failed ? Val{} : o;
}

Val Ctx::eval_index(const Node& n, u32 depth) {
    const Val obj = deref(eval(n.a, depth + 1), n.pos);
    if (failed) return Val{};
    const Val idx = deref(eval(n.b, depth + 1), n.pos);
    if (failed) return Val{};
    if (idx.k == K::Str) {
        const std::string key(str(idx));
        if (obj.k == K::Obj) return obj_get(obj, key);
        return member(obj, key, n.pos);   // thisComp["width"]
    }
    f64 i = 0;
    if (!want_num(idx, i, n.pos, "índice")) return Val{};
    const i64 k = static_cast<i64>(std::floor(i));
    switch (obj.k) {
        case K::Num:
            if (i == 0.0) return obj;
            return fail(n.pos, "índice fora do vetor");
        case K::Vec:
            if (k < 0 || k >= obj.n) return fail(n.pos, "índice " + std::to_string(k) + " fora do vetor de " + std::to_string(obj.n));
            return Val::num(obj.v[k]);
        case K::Arr: {
            const std::vector<Val>& a = arrays[obj.a];
            if (k < 0 || static_cast<usize>(k) >= a.size()) return Val{};   // undefined, como no JavaScript
            return a[static_cast<usize>(k)];
        }
        case K::Str: {
            const std::vector<u32> cps = utf8_codepoints(str(obj));
            if (k < 0 || static_cast<usize>(k) >= cps.size()) return Val{};
            return make_str(utf8_encode(&cps[static_cast<usize>(k)], 1));
        }
        case K::Obj: return obj_get(obj, number_text(i));
        default: return fail(n.pos, "só dá para indexar vetor, texto ou objeto");
    }
}

Val Ctx::eval_assign(const Node& n, u32 depth) {
    Val v = deref(eval(n.b, depth + 1), n.pos);
    if (failed) return Val{};
    const Node& t = N_(n.a);
    if (t.kind != N::Local) return store(n.a, v, n.op, n.pos, depth);
    const u32 idx = local_index(t);
    if (n.op != OpSet) {
        if (!defd[idx]) return fail(n.pos, "variável '" + local_name(t) + "' usada antes de receber valor");
        const Val old = deref(stack[idx], n.pos);
        if (failed) return Val{};
        v = arith(n.op, old, v, n.pos);
        if (failed) return Val{};
    }
    stack[idx] = v;
    defd[idx] = 1;
    return v;
}

Val Ctx::eval_incdec(const Node& n, u32 depth) {
    const Node& t = N_(n.a);
    const u8 op = n.op == OpInc ? OpAdd : OpSub;
    if (t.kind != N::Local) {
        const Val nv = store(n.a, Val::num(1.0), op, n.pos, depth);
        if (failed) return Val{};
        return n.flag ? nv : arith(n.op == OpInc ? OpSub : OpAdd, nv, Val::num(1.0), n.pos);
    }
    const u32 idx = local_index(t);
    if (!defd[idx]) return fail(n.pos, "variável '" + local_name(t) + "' usada antes de receber valor");
    const Val old = stack[idx];
    const Val nv = arith(op, old, Val::num(1.0), n.pos);
    if (failed) return Val{};
    stack[idx] = nv;
    return n.flag ? nv : old;
}

/// Onde mora o valor de `a`, `a[i]`, `o.k` (para trocar no lugar). Nulo = não
/// é um lugar (resultado de função, propriedade do projeto…).
Val* Ctx::lref(i32 ni, u32 depth) {
    if (depth > kMaxEvalDepth) { fail_hard(N_(ni).pos, "aninhamento profundo demais"); return nullptr; }
    const Node& n = N_(ni);
    if (n.kind == N::Local) {
        const u32 idx = local_index(n);
        if (!defd[idx]) { fail(n.pos, "variável '" + local_name(n) + "' usada antes de receber valor"); return nullptr; }
        return &stack[idx];
    }
    if (n.kind == N::Index) {
        const Val idx = deref(eval(n.b, depth + 1), n.pos);
        if (failed) return nullptr;
        Val* base = lref(n.a, depth + 1);
        if (failed || !base) return nullptr;
        if (idx.k == K::Str) {
            if (base->k != K::Obj) return nullptr;
            const std::string key(str(idx));
            for (ObjEntry& e : objects[base->a]) if (e.key == key) return &e.v;
            return nullptr;
        }
        if (idx.k != K::Num || base->k != K::Arr) return nullptr;
        const i64 k = static_cast<i64>(std::floor(idx.v[0]));
        if (k < 0 || static_cast<usize>(k) >= arrays[base->a].size()) return nullptr;
        return &arrays[base->a][static_cast<usize>(k)];
    }
    if (n.kind == N::Member) {
        Val* base = lref(n.a, depth + 1);
        if (failed || !base || base->k != K::Obj) return nullptr;
        const std::string& key = p->strings[n.sym];
        for (ObjEntry& e : objects[base->a]) if (e.key == key) return &e.v;
        return nullptr;
    }
    return nullptr;
}

/// a[i] = v, o.k = v, o["k"] += v. Num vetor-valor (até 4 números) troca o
/// componente; passando disso (ou com item que não é número) ele vira vetor
/// de verdade.
Val Ctx::store(i32 target, const Val& v0, u8 op, u32 pos, u32 depth) {
    const Node& t = N_(target);
    Val v = v0;
    std::string key;
    bool byKey = t.kind == N::Member;
    f64 fi = 0;
    if (byKey) {
        key = p->strings[t.sym];
    } else {
        const Val idx = deref(eval(t.b, depth + 1), pos);
        if (failed) return Val{};
        if (idx.k == K::Str) { key = std::string(str(idx)); byKey = true; }
        else if (!want_num(idx, fi, pos, "índice")) return Val{};
    }
    Val tmp;
    Val* cont = lref(t.a, depth + 1);
    if (failed) return Val{};
    if (!cont) {
        tmp = deref(eval(t.a, depth + 1), pos);
        if (failed) return Val{};
        cont = &tmp;
    }
    if (byKey) {
        if (cont->k != K::Obj) return fail(pos, "só dá para criar ou trocar propriedade num objeto { }");
        const Val o = *cont;
        if (op != OpSet) {
            v = arith(op, obj_get(o, key), v, pos);
            if (failed) return Val{};
        }
        if (!obj_set(o, key, v)) return Val{};
        return v;
    }
    const i64 k = static_cast<i64>(std::floor(fi));
    if (k < 0 || k >= static_cast<i64>(kMaxArrayLength)) return fail(pos, "índice " + std::to_string(k) + " inválido");
    if (cont->k == K::Vec || cont->k == K::Num) {
        const bool inside = k < cont->n;
        if (v.k == K::Num && k < 4 && (inside || (k == cont->n && op == OpSet))) {
            if (op != OpSet) {
                v = arith(op, Val::num(cont->v[k]), v, pos);
                if (failed) return Val{};
            }
            cont->v[k] = v.v[0];
            if (!inside) cont->n = static_cast<u8>(k + 1);
            if (cont->n > 1) cont->k = K::Vec;
            return v;
        }
        const Val promoted = to_arr(*cont);
        if (failed) return Val{};
        *cont = promoted;
    }
    if (cont->k != K::Arr) return fail(pos, "só dá para trocar item de vetor");
    const u32 ai = cont->a;
    const usize uk = static_cast<usize>(k);
    if (uk >= arrays[ai].size()) {
        if (op != OpSet) return fail(pos, "índice " + std::to_string(k) + " fora do vetor");
        while (arrays[ai].size() <= uk) if (!arr_push(ai, Val{})) return Val{};
    }
    if (op != OpSet) {
        v = arith(op, deref(arrays[ai][uk], pos), v, pos);
        if (failed) return Val{};
    }
    arrays[ai][uk] = v;
    return v;
}

Val Ctx::call(const Node& n, u32 depth) {
    const Node& cn = N_(n.a);
    Val callee;
    if (cn.kind == N::Member) {
        Val obj = eval(cn.a, depth + 1);
        if (failed) return Val{};
        const std::string& name = p->strings[cn.sym];
        if ((obj.k == K::Vec || obj.k == K::Num) && mutating_array_method(name)) {
            // v = [1, 2]; v.push(3): a variável deixa de ser vetor-valor e vira vetor de verdade.
            Val* st = lref(cn.a, depth + 1);
            if (failed) return Val{};
            if (st && (st->k == K::Vec || st->k == K::Num)) {
                const Val promoted = to_arr(*st);
                *st = promoted;
                obj = promoted;
            } else {
                obj = to_arr(obj);
            }
            if (failed) return Val{};
        }
        callee = member(obj, name, cn.pos);
    } else {
        callee = eval(n.a, depth + 1);
    }
    if (failed) return Val{};
    Val args[kMaxParams];
    u32 argc = 0;
    for (u32 i = 0; i < n.count; ++i) {
        const i32 ki = p->kids[n.first + i];
        if (N_(ki).kind == N::Spread) {
            const Val s = deref(eval(N_(ki).a, depth + 1), n.pos);
            if (failed) return Val{};
            if (s.k != K::Arr && s.k != K::Vec && s.k != K::Num) return fail(N_(ki).pos, "'...' precisa de um vetor");
            const u32 count = s.k == K::Arr ? static_cast<u32>(arrays[s.a].size()) : s.n;
            if (argc + count > kMaxParams) return fail(n.pos, "argumentos demais (no máximo 16)");
            for (u32 k = 0; k < count; ++k) args[argc++] = s.k == K::Arr ? arrays[s.a][k] : Val::num(s.v[k]);
            continue;
        }
        if (argc >= kMaxParams) return fail(n.pos, "argumentos demais (no máximo 16)");
        // Texto e objetos passam como são; propriedades viram valor (exceto
        // para quem pede a propriedade em si — nenhuma função daqui pede).
        Val a = eval(ki, depth + 1);
        if (failed) return Val{};
        a = deref(a, n.pos);
        if (failed) return Val{};
        args[argc++] = a;
    }
    curDepth = depth;
    return invoke(callee, args, argc, n.pos, depth);
}

/// Chama um valor: função embutida, método, função da pessoa — é por aqui que
/// map/filter/reduce chamam a função de retorno.
Val Ctx::invoke(const Val& callee, Val* args, u32 argc, u32 pos, u32 depth) {
    switch (callee.k) {
        case K::UFn: return call_user(callee, args, argc, pos, depth);
        case K::Fn:
            if (!wants_arrays(callee.id)) for (u32 i = 0; i < argc; ++i) args[i] = norm(args[i]);
            return call_fn(callee.id, args, argc, pos);
        case K::Method: {
            if (!wants_arrays(callee.id)) for (u32 i = 0; i < argc; ++i) args[i] = norm(args[i]);
            const Val receiver = recv[callee.a];
            return call_method(callee.id, receiver, args, argc, pos);
        }
        case K::Effect: return call_method(MEffectParam, callee, args, argc, pos);
        case K::Layer: case K::Transform: case K::Mask: case K::VGroup: case K::Text: case K::PathProp:
            return group_call(callee, args, argc, pos);
        default: return fail(pos, "isto não é uma função");
    }
}

Val Ctx::call_user(const Val& fnv, const Val* args, u32 argc, u32 pos, u32 depth) {
    if (fnv.id >= p->funcs.size()) return fail(pos, "função inválida");
    const Func& f = p->funcs[fnv.id];
    if (frames.size() >= kMaxCallDepth) return fail_hard(pos, "funções chamando funções fundo demais (recursão sem fim?)");
    if (fnv.a >= frames.size() || frames[fnv.a].serial != fnv.b)
        return fail(pos, "a função" + (f.name.empty() ? std::string() : " '" + f.name + "'") + " saiu do escopo em que foi criada");
    if (!tick(pos)) return Val{};
    const u32 back = cur;
    push_frame(fnv.id, fnv.a);
    const u32 me = cur;
    for (u32 i = 0; i < f.nparams && !failed; ++i) {
        const u32 slot = frames[me].base + i;
        if (static_cast<i32>(i) == f.rest) {
            const Val restArr = make_arr();
            for (u32 k = i; k < argc; ++k) if (!arr_push(restArr.a, args[k])) break;
            stack[slot] = restArr;
        } else if (i < argc && args[i].k != K::Undef) {
            stack[slot] = args[i];
        } else if (f.defaults[i] >= 0) {
            const Val d = deref(eval(f.defaults[i], depth + 1), pos);
            stack[slot] = d;
        }
        defd[slot] = 1;
    }
    Val result;
    if (!failed) {
        if (f.exprBody) {
            result = deref(eval(f.body, depth + 1), pos);
        } else {
            (void)exec(f.body, depth + 1);
            if (!failed && flow == Flow::Return) result = ret;
        }
    }
    flow = Flow::None;
    while (frames.size() > static_cast<usize>(me) + 1) pop_frame(me);   // por garantia
    pop_frame(back);
    return failed ? Val{} : result;
}

Val Ctx::global(u32 id, u32 pos) {
    const Env& e = *env;
    if ((id >= FWiggle && id < MCompLayer) || (id >= FRgbToHsl && id < FExprFnEnd)) { Val r; r.k = K::Fn; r.id = id; return r; }
    if (id == GArrayNs || id == GObjectNs) { Val r; r.k = K::Ns; r.id = id; return r; }
    if (e.maskSelf && (id == GValue || id == GThisProperty)) {
        // Expressão no caminho da máscara: `value` e `thisProperty` são o caminho de antes dela.
        PathRef ref;
        ref.kind = 2;
        pathRefs.push_back(ref);
        Val r; r.k = K::PathProp; r.a = static_cast<u32>(pathRefs.size() - 1);
        return r;
    }
    switch (id) {
        case GTime: return Val::num(nowF / e.fps);
        case GTimeMs: return Val::num(nowF / e.fps * 1000.0);
        case GLocalTime: return Val::num(now_local() / e.fps);
        case GTextIndex: return Val::num(static_cast<u32>(g_textContext));
        case GTextTotal: return Val::num(static_cast<u32>(g_textContext >> 32));
        case GSelectorValue: {
            const f64 selected[3] = {g_textSelector.x, g_textSelector.y, g_textSelector.z};
            return Val::vec(selected, 3);
        }
        case GValue: return e.textValue ? make_str(*e.textValue) : nowValue;
        case GText: {
            if (!e.layer || e.layer->kind != LayerKind::Text) return fail(pos, "'text' só existe numa camada de texto");
            Val r = make_layer(e.layer, e.layerId);
            r.k = K::Text;
            return r;
        }
        case GFps: return Val::num(e.fps);
        case GFrame: return Val::num(std::floor(nowF));
        case GMath: { Val r; r.k = K::Math; return r; }
        case GThisComp:
            if (!e.comp) return fail(pos, "sem composição (expressão fora de uma camada)");
            { Val r; r.k = K::Comp; return r; }
        case GMarker:
            // As marcas são da composição (a régua); `marker` e `thisComp.marker` são as mesmas.
            if (!e.comp) return fail(pos, "sem marcas (expressão fora de uma camada)");
            { Val r; r.k = K::Markers; return r; }
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
        case GVelocity: return prop_velocity(e.self, nowF, pos);
        case GSpeed: {
            const Val v = prop_velocity(e.self, nowF, pos);
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
            if (name == "SQRT2") return Val::num(1.41421356237309504880);
            if (name == "SQRT1_2") return Val::num(0.70710678118654752440);
            if (name == "LN2") return Val::num(0.69314718055994530942);
            if (name == "LN10") return Val::num(2.30258509299404568402);
            if (name == "LOG2E") return Val::num(1.44269504088896340736);
            if (name == "LOG10E") return Val::num(0.43429448190325182765);
            static constexpr NameEntry kMath[] = {
                {"sin", FSin}, {"cos", FCos}, {"tan", FTan}, {"asin", FAsin}, {"acos", FAcos}, {"atan", FAtan},
                {"atan2", FAtan2}, {"sqrt", FSqrt}, {"abs", FAbs}, {"floor", FFloor}, {"ceil", FCeil},
                {"round", FRound}, {"min", FMin}, {"max", FMax}, {"pow", FPow}, {"exp", FExp}, {"log", FLog},
                {"random", FRandom},
                {"sign", FSign}, {"trunc", FTrunc}, {"hypot", FHypot}, {"cbrt", FCbrt}, {"log10", FLog10}, {"log2", FLog2},
            };
            for (const NameEntry& m : kMath) if (name == m.name) { Val r; r.k = K::Fn; r.id = m.id; return r; }
            return fail(pos, "Math." + std::string(name) + " não existe");
        }
        case K::Comp: {
            const Composition* c = comp_of(obj);
            if (!c) return fail(pos, "a composição não existe mais");
            if (name == "layer") return make_method(MCompLayer, obj);
            if (name == "width") return Val::num(c->width());
            if (name == "height") return Val::num(c->height());
            // A taxa é a DA composição lida (comp("Outra") pode ter outra taxa).
            const f64 cfps = c->fps() > 0.0 ? c->fps() : env->fps;
            if (name == "duration") return Val::num(static_cast<f64>(c->duration().value) / cfps);
            if (name == "frameDuration") return Val::num(1.0 / cfps);
            if (name == "numLayers") return Val::num(c->order().size());
            if (name == "pixelAspect") return Val::num(1.0);
            if (name == "displayStartTime") return Val::num(0.0);
            if (name == "name") return make_str(c->name());
            if (name == "marker") { Val r; r.k = K::Markers; return r; }
            return fail(pos, "thisComp." + std::string(name) + " não existe");
        }
        case K::Markers: {
            i64 shift = 0;
            const std::vector<Marker>* list = marker_list(obj, shift);
            if (!list) return fail(pos, "a camada não existe mais");
            if (name == "numKeys") return Val::num(static_cast<f64>(list->size()));
            if (name == "key") return make_method(MMarkerKey, obj);
            if (name == "nearestKey") return make_method(MMarkerNearestKey, obj);
            return fail(pos, "marker." + std::string(name) + " não existe (use key, nearestKey ou numKeys)");
        }
        case K::MarkerKey: {
            i64 shift = 0;
            const std::vector<Marker>* list = marker_list(obj, shift);
            if (!list || obj.id >= list->size()) return fail(pos, "a marca não existe mais");
            const Marker& m = (*list)[obj.id];
            // Tempo da COMPOSIÇÃO também na marca da camada (como no AE).
            if (name == "time") return Val::num(static_cast<f64>(m.frame.value + shift) / env->fps);
            if (name == "index") return Val::num(obj.id + 1.0);
            if (name == "frame") return Val::num(static_cast<f64>(m.frame.value + shift));
            if (name == "name") return make_str(m.label);
            // Sem comentário (marca de antes da v67), o nome responde, como antes.
            if (name == "comment") return make_str(m.comment.empty() ? m.label : m.comment);
            if (name == "duration") return Val::num(static_cast<f64>(m.duration) / env->fps);
            return fail(pos, "a marca não tem '" + std::string(name) + "'");
        }
        case K::Layer: {
            const Layer* l = layer_of(obj);
            if (!l) return fail(pos, "a camada não existe mais");
            const LayerId id{obj.a, obj.b};
            if (name == "transform") { Val r = obj; r.k = K::Transform; return r; }
            if (name == "effect") return make_method(MLayerEffect, obj);
            // AE animation: espaço da camada e a caixa do conteúdo.
            for (u32 k = 0; k < 8; ++k) if (name == kSpaceNames[k]) return make_method(MLayerSpace + k, obj);
            if (name == "sourceRectAtTime") return make_method(MLayerSourceRect, obj);
            // AE expressions (lote E).
            if (name == "fromCompToSurface") return make_method(MLayerSpace + 1, obj);
            if (name == "mask") return make_method(MLayerMask, obj);
            if (name == "content") return make_method(MLayerContent, obj);
            if (name == "sampleImage") return make_method(MLayerSample, obj);
            if (name == "source") {
                if (l->kind == LayerKind::Composition) {
                    // A pré-composição: o mesmo objeto de comp("Nome").
                    u32 i = 0, tag = 0;
                    if (env->tl) env->tl->for_each_composition([&](CompositionId cid, const Composition&) { ++i; if (cid == l->nested.composition) tag = i; });
                    if (!tag) return fail(pos, "a pré-composição não existe mais");
                    Val r; r.k = K::Comp; r.v[3] = static_cast<f64>(tag);
                    return r;
                }
                if (!l->source.valid()) return fail(pos, "a camada \"" + l->name + "\" não tem arquivo de origem");
                return footage_of({}, l->source.pack(), pos);
            }
            const Composition* lc = comp_of(obj);
            if (name == "name") return make_str(l->name);
            if (name == "index") {
                const i32 z = lc->z_index_of(id);
                return Val::num(z < 0 ? 0.0 : static_cast<f64>(lc->order().size() - static_cast<u32>(z)));
            }
            if (name == "inPoint") return Val::num(static_cast<f64>(l->start.value) / env->fps);
            if (name == "outPoint") return Val::num(static_cast<f64>(l->end.value) / env->fps);
            if (name == "startTime") return Val::num(static_cast<f64>(l->start.value - l->offset.value) / env->fps);
            if (name == "width" || name == "height") {
                const bool w = name == "width";
                if (l->kind == LayerKind::Shape) return Val::num(w ? l->shape.bounds.w : l->shape.bounds.h);
                if (l->kind == LayerKind::Null) return Val::num(100.0);
                return Val::num(w ? lc->width() : lc->height());
            }
            if (name == "marker") { Val r = obj; r.k = K::Markers; r.n = 1; return r; }   // thisLayer.marker
            if (name == "text") {   // v70: o grupo Texto (só camada de texto)
                if (l->kind != LayerKind::Text) return fail(pos, "a camada \"" + l->name + "\" não é de texto");
                Val r = obj; r.k = K::Text; return r;
            }
            if (name == "hasParent") return Val::num(l->parent.valid() ? 1.0 : 0.0);
            if (name == "parent") {
                const Layer* p2 = l->parent.valid() ? lc->layer(l->parent) : nullptr;
                if (!p2) return fail(pos, "a camada não tem pai");
                Val r = make_layer(p2, l->parent);
                r.v[3] = obj.v[3];   // o pai mora na mesma composição
                return r;
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
            if (name == "value") return prop_value(d, nowF, true, pos);
            if (name == "valueAtTime") return make_method(MPropValueAtTime, obj);
            if (name == "velocityAtTime") return make_method(MPropVelocityAtTime, obj);
            if (name == "speedAtTime") return make_method(MPropSpeedAtTime, obj);
            if (name == "nearestKey") return make_method(MPropNearestKey, obj);
            if (name == "velocity") return prop_velocity(d, nowF, pos);
            if (name == "speed") {
                const Val v = prop_velocity(d, nowF, pos);
                f64 s = 0;
                for (u32 i = 0; i < v.n; ++i) s += v.v[i] * v.v[i];
                return Val::num(std::sqrt(s));
            }
            if (name == "numKeys") {
                const Track* t = find_track(d, 0);
                return Val::num(t ? static_cast<f64>(t->keys.size()) : 0.0);
            }
            if (name == "key") return make_method(MPropKey, obj);
            if (name == "points" || name == "pointOnPath" || name == "createPath" || name == "isClosed")
                return fail(pos, "'" + std::string(name) + "' é de caminho (máscara ou forma), e esta propriedade é " + prop_label(d));
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
        case K::Rect: {
            // sourceRectAtTime(): {top, left, width, height}, px da camada.
            if (name == "top") return Val::num(obj.v[0]);
            if (name == "left") return Val::num(obj.v[1]);
            if (name == "width") return Val::num(obj.v[2]);
            if (name == "height") return Val::num(obj.v[3]);
            return fail(pos, "a caixa tem top, left, width e height (não '" + std::string(name) + "')");
        }
        case K::Num:
        case K::Vec:
            if (name == "length") return Val::num(obj.n);
            if (obj.k == K::Num && name == "toFixed") return make_method(MNumToFixed, obj);
            if (name == "toString") return make_method(MNumToString, obj);
            if (obj.k == K::Vec) {
                // map/filter/join… num vetor-valor: valem sobre uma cópia.
                const Val copy = to_arr(obj);
                if (failed) return Val{};
                const Val m = array_member(copy, name, pos);
                if (m.k != K::Undef || failed) return m;
            }
            return fail(pos, "número não tem '" + std::string(name) + "'");
        case K::Arr: {
            if (name == "length") return Val::num(static_cast<f64>(arrays[obj.a].size()));
            const Val m = array_member(obj, name, pos);
            if (m.k != K::Undef || failed) return m;
            return fail(pos, "vetor não tem '" + std::string(name) + "'");
        }
        case K::Obj: {
            for (const ObjEntry& e : objects[obj.a]) if (e.key == name) return e.v;
            if (name == "hasOwnProperty") return make_method(MObjHasOwn, obj);
            return Val{};   // propriedade que não existe = undefined
        }
        case K::Ns: {
            static constexpr NameEntry kArray[] = {{"isArray", FIsArray}};
            static constexpr NameEntry kObject[] = {{"keys", FObjKeys}, {"values", FObjValues}, {"entries", FObjEntries}};
            if (obj.id == GArrayNs) { for (const NameEntry& m : kArray) if (name == m.name) { Val r; r.k = K::Fn; r.id = m.id; return r; } }
            else { for (const NameEntry& m : kObject) if (name == m.name) { Val r; r.k = K::Fn; r.id = m.id; return r; } }
            return fail(pos, std::string(obj.id == GArrayNs ? "Array." : "Object.") + std::string(name) + " não existe");
        }
        case K::Fn: {
            // String.fromCharCode, Number.isFinite…
            static constexpr NameEntry kString[] = {{"fromCharCode", FFromCharCode}, {"fromCodePoint", FFromCharCode}};
            static constexpr NameEntry kNumber[] = {{"isFinite", FIsFinite}, {"isNaN", FIsNaN}, {"isInteger", FNumIsInteger},
                                                    {"parseFloat", FParseFloat}, {"parseInt", FParseInt}};
            if (obj.id == FString) for (const NameEntry& m : kString) if (name == m.name) { Val r; r.k = K::Fn; r.id = m.id; return r; }
            if (obj.id == FNumber) {
                for (const NameEntry& m : kNumber) if (name == m.name) { Val r; r.k = K::Fn; r.id = m.id; return r; }
                if (name == "MAX_VALUE") return Val::num(1.7976931348623157e308);
                if (name == "MIN_VALUE") return Val::num(5e-324);
                if (name == "EPSILON") return Val::num(2.220446049250313e-16);
                if (name == "MAX_SAFE_INTEGER") return Val::num(9007199254740991.0);
            }
            return fail(pos, "'" + std::string(name) + "' não existe aqui");
        }
        case K::Undef:
        case K::Null:
            return fail(pos, "não dá para ler '" + std::string(name) + "' de " + (obj.k == K::Null ? "null" : "undefined"));
        case K::Path:
        case K::PathProp: {
            if (name == "path" || name == "maskPath" || name == "value") return obj;
            static constexpr NameEntry kPath[] = {
                {"points", MPathPoints}, {"inTangents", MPathInTangents}, {"outTangents", MPathOutTangents},
                {"isClosed", MPathIsClosed}, {"pointOnPath", MPathPointOn}, {"tangentOnPath", MPathTangentOn},
                {"normalOnPath", MPathNormalOn}, {"createPath", MPathCreate},
            };
            for (const NameEntry& m : kPath) if (name == m.name) return make_method(m.id, obj);
            return fail(pos, "caminho não tem '" + std::string(name) + "' (tem points, inTangents, outTangents, isClosed, pointOnPath, tangentOnPath, normalOnPath, createPath)");
        }
        case K::Mask: {
            const Layer* l = layer_of(obj);
            const Mask* m = nullptr;
            if (l) for (const Mask& x : l->masks) if (x.id == obj.id) m = &x;
            if (!m) return fail(pos, "a máscara não existe mais");
            if (name == "maskPath" || name == "path") {
                PathRef ref;
                ref.owner = obj; ref.owner.k = K::Layer; ref.owner.id = 0;
                ref.kind = 0; ref.a = obj.id;
                pathRefs.push_back(ref);
                Val r; r.k = K::PathProp; r.a = static_cast<u32>(pathRefs.size() - 1);
                return r;
            }
            if (name == "name") return make_str(m->name);
            if (name == "invert" || name == "inverted") return Val::num(m->inverted ? 1.0 : 0.0);
            const u32 which = name == "maskFeather" ? 0u : name == "maskExpansion" ? 1u : name == "maskOpacity" ? 2u : 9u;
            if (which == 9u) return fail(pos, "máscara não tem '" + std::string(name) + "' (tem maskPath, maskFeather, maskExpansion, maskOpacity)");
            PropDesc d;
            d.layer = l;
            d.id = LayerId{obj.a, obj.b};
            d.kind = 5;
            d.effectIndex = obj.id;
            d.key0 = which;
            d.count = 1;
            if (which == 2u) d.scale = 100.0;   // opacidade em %
            return make_prop(d);
        }
        case K::VGroup: {
            const Layer* l = layer_of(obj);
            if (!l || l->kind != LayerKind::Shape || obj.id >= l->shape.vector.groups.size()) return fail(pos, "o grupo não existe mais");
            const VectorGroup& g = l->shape.vector.groups[obj.id];
            if (name == "content") return make_method(MGroupContent, obj);
            if (name == "name") return make_str(g.name);
            if (name == "numProperties") return Val::num(static_cast<f64>(g.paths.size()));
            if (name == "path") {
                if (g.paths.empty()) return fail(pos, "o grupo \"" + g.name + "\" não tem caminho");
                const Val one = Val::num(1.0);
                return group_content(obj, &one, 1, pos);
            }
            return fail(pos, "grupo não tem '" + std::string(name) + "' (tem content, path, name)");
        }
        case K::Footage: {
            if (obj.a >= footages.size()) return fail(pos, "arquivo inválido");
            const FootageInfo& f = footages[obj.a];
            if (name == "width") return Val::num(f.width);
            if (name == "height") return Val::num(f.height);
            if (name == "duration") return Val::num(f.duration);
            if (name == "frameDuration") return Val::num(f.frameDuration > 0.0 ? f.frameDuration : 1.0 / env->fps);
            if (name == "name") return make_str(f.name);
            if (name == "pixelAspect") return Val::num(1.0);
            if (name == "ntscDropFrame") return Val::num(0.0);
            return fail(pos, "arquivo não tem '" + std::string(name) + "' (tem width, height, duration, frameDuration, name)");
        }
        case K::Str: {
            // v70: texto (letras contadas como letras, não bytes).
            if (name == "length") return Val::num(static_cast<f64>(utf8_codepoints(str(obj)).size()));
            if (name == "value" || name == "text") return obj;   // sourceText.value / .text, como no AE
            static constexpr NameEntry kStrMethods[] = {
                {"split", MStrSplit}, {"concat", MStrConcat}, {"trimStart", MStrTrimStart}, {"trimEnd", MStrTrimEnd},
                {"trimLeft", MStrTrimStart}, {"trimRight", MStrTrimEnd}, {"at", MStrAt},
                {"toUpperCase", MStrUpper}, {"toLowerCase", MStrLower}, {"toLocaleUpperCase", MStrUpper},
                {"toLocaleLowerCase", MStrLower}, {"substr", MStrSubstr}, {"substring", MStrSubstring},
                {"slice", MStrSlice}, {"charAt", MStrCharAt}, {"indexOf", MStrIndexOf}, {"lastIndexOf", MStrLastIndexOf},
                {"replace", MStrReplace}, {"replaceAll", MStrReplaceAll}, {"trim", MStrTrim}, {"padStart", MStrPadStart},
                {"padEnd", MStrPadEnd}, {"repeat", MStrRepeat}, {"toString", MStrToString}, {"includes", MStrIncludes},
                {"startsWith", MStrStartsWith}, {"endsWith", MStrEndsWith}, {"charCodeAt", MStrCharCodeAt},
            };
            for (const NameEntry& m : kStrMethods) if (name == m.name) return make_method(m.id, obj);
            return fail(pos, "texto não tem '" + std::string(name) + "'");
        }
        case K::Text: {
            const Layer* l = layer_of(obj);
            if (!l) return fail(pos, "a camada não existe mais");
            if (name == "sourceText") {
                // O próprio texto, antes da expressão; o de outra camada, com o key no instante.
                if (env->textValue && l == env->layer) return make_str(*env->textValue);
                TextData shown = l->text;
                (void)text::apply_source_text(l->text, static_cast<i64>(std::floor(layer_local_from_comp(l, nowF))), shown);
                return make_str(shown.content);
            }
            return fail(pos, "o grupo Texto tem sourceText (não '" + std::string(name) + "')");
        }
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

/// O keyframe mais perto do quadro da composição `compF` (empate: o anterior).
Val Ctx::nearest_key(const PropDesc& d, u32 propIndex, f64 compF, u32 pos) {
    const Track* t = find_track(d, 0);
    if (!t || t->keys.empty()) return fail(pos, "nearestKey: a propriedade não tem keyframes");
    const f64 local = layer_local_from_comp(d.layer, compF);
    u32 best = 0;
    f64 bestD = std::fabs(static_cast<f64>(t->keys[0].time.value) - local);
    for (u32 i = 1; i < t->keys.size(); ++i) {
        const f64 dist = std::fabs(static_cast<f64>(t->keys[i].time.value) - local);
        if (dist < bestD) { bestD = dist; best = i; }
    }
    Val r;
    r.k = K::Key;
    r.a = propIndex;
    r.b = best;
    return r;
}

/// `marker.key(índice | "nome")` e `marker.nearestKey(t)`. O nome compara
/// sem diferenciar maiúsculas (a pessoa digita "Drop", a marca é "drop").
Val Ctx::marker_key(const Val& obj, const Val* args, u32 argc, bool nearest, u32 pos) {
    i64 shift = 0;
    const std::vector<Marker>* list = marker_list(obj, shift);
    if (!list) return fail(pos, "a camada não existe mais");
    const std::vector<Marker>& ms = *list;
    if (argc != 1) return fail(pos, nearest ? "uso: marker.nearestKey(t)" : "uso: marker.key(índice ou \"nome\")");
    if (ms.empty()) return fail(pos, obj.n == 1 ? "a camada não tem marcas" : "a composição não tem marcas");
    Val r = obj;   // `n`/`a`/`b` dizem de quem é a lista
    r.k = K::MarkerKey;
    if (nearest) {
        f64 t = 0;
        if (!want_num(args[0], t, pos, "tempo")) return Val{};
        const f64 f = t * env->fps - static_cast<f64>(shift);
        u32 best = 0;
        for (u32 i = 1; i < ms.size(); ++i) {
            if (std::fabs(static_cast<f64>(ms[i].frame.value) - f) < std::fabs(static_cast<f64>(ms[best].frame.value) - f)) best = i;
        }
        r.id = best;
        return r;
    }
    if (args[0].k == K::Str) {
        // O nome (rótulo) e, como no AE, também o comentário.
        const std::string_view nm = str(args[0]);
        for (u32 i = 0; i < ms.size(); ++i) if (ms[i].label == nm || ms[i].comment == nm) { r.id = i; return r; }
        for (u32 i = 0; i < ms.size(); ++i) if (name_eq(ms[i].label, nm) || name_eq(ms[i].comment, nm)) { r.id = i; return r; }
        return fail(pos, "marca \"" + std::string(nm) + "\" não existe");
    }
    f64 idx = 0;
    if (!want_num(args[0], idx, pos, "índice da marca")) return Val{};
    const i64 k = static_cast<i64>(std::floor(idx + 0.5));
    if (k < 1 || k > static_cast<i64>(ms.size())) {
        return fail(pos, "marker.key(" + std::to_string(k) + "): " + (obj.n == 1 ? "a camada" : "a composição")
                         + " tem " + std::to_string(ms.size()) + " marca(s)");
    }
    r.id = static_cast<u32>(k - 1);
    return r;
}

/// shake(frequência = 30, amplitude = 8, aspereza = 0.4, decaimento = 0, início = 0)
///
/// Tremor de câmera: ruído de valor em oitavas (1 a 4, pela aspereza 0..1),
/// cada componente com o seu sorteio. Com `decaimento` > 0 (segundos), é um
/// IMPACTO: nada antes de `início` (segundos da composição — `inPoint` para
/// começar com a camada) e a força cai como e^(−3·Δt/decaimento), ~5% depois
/// de `decaimento`. A amplitude está na unidade da propriedade (px, graus, %),
/// como no wiggle.
Val Ctx::shake(const Val* args, u32 argc, u32 pos) {
    const Env& e = *env;
    if (argc > 5) return fail(pos, "uso: shake(frequência, amplitude[, aspereza, decaimento, início])");
    f64 freq = 30.0, rough = 0.4, decay = 0.0, start = 0.0;
    if (argc >= 1 && !want_num(args[0], freq, pos, "frequência")) return Val{};
    if (argc >= 2 && !want_numvec(args[1], pos, "amplitude")) return Val{};
    if (argc >= 3 && !want_num(args[2], rough, pos, "aspereza")) return Val{};
    if (argc >= 4 && !want_num(args[3], decay, pos, "decaimento")) return Val{};
    if (argc >= 5 && !want_num(args[4], start, pos, "início")) return Val{};
    const f64 t = nowF / e.fps;
    f64 env01 = 1.0;
    if (decay > 0.0) {
        const f64 dt = t - start;
        env01 = dt < 0.0 ? 0.0 : std::exp(-3.0 * dt / decay);
    }
    rough = std::clamp(rough, 0.0, 1.0);
    const u32 octaves = static_cast<u32>(std::llround(3.0 * rough)) + 1u;
    const f64 persistence = rough * 0.4 + 0.3;
    Val base = nowValue;
    if (base.k != K::Num && base.k != K::Vec) base = Val::num(0.0);
    Val r = base;
    for (u32 c = 0; c < r.n; ++c) {
        const f64 a = argc < 2 ? 8.0 : (args[1].k == K::Num ? args[1].v[0] : (c < args[1].n ? args[1].v[c] : 0.0));
        if (a == 0.0 || freq == 0.0 || env01 == 0.0) continue;
        f64 sum = 0.0, w = 1.0, norm = 0.0, fm = freq;
        for (u32 o = 0; o < octaves; ++o) {
            sum += w * vnoise1(hash_mix(e.seedBase, 0x5A4Bu + c * 131u + o * 7919u), t * fm + static_cast<f64>(o) * 17.13);
            norm += w;
            w *= persistence;
            fm *= 2.0;
        }
        r.v[c] += a * env01 * (norm > 0.0 ? sum / norm : 0.0);
    }
    return r;
}

/// timeSinceMarker() | ("nome") | (t) | ("nome", t): segundos desde a última
/// marca em t ou antes (a de nome igual, se pedido). Sem marca antes, é
/// infinito — `ease(timeSinceMarker(), 0, 0.3, 120, 100)` fica no fim.
Val Ctx::time_since_marker(const Val* args, u32 argc, u32 pos) {
    if (!env->comp) return fail(pos, "sem marcas (expressão fora de uma camada)");
    if (argc > 2) return fail(pos, "uso: timeSinceMarker([\"nome\"][, t])");
    std::string_view nm;
    bool named = false;
    f64 t = nowF / env->fps;
    u32 ti = 0;
    if (argc >= 1 && args[0].k == K::Str) { nm = str(args[0]); named = true; ti = 1; }
    if (argc > ti && !want_num(args[ti], t, pos, "t")) return Val{};
    if (argc > ti + 1) return fail(pos, "uso: timeSinceMarker([\"nome\"][, t])");
    const f64 f = t * env->fps;
    const std::vector<Marker>& ms = env->comp->markers();
    for (usize i = ms.size(); i-- > 0;) {
        if (static_cast<f64>(ms[i].frame.value) > f + 1e-9) continue;
        if (named && !name_eq(ms[i].label, nm)) continue;
        return Val::num(t - static_cast<f64>(ms[i].frame.value) / env->fps);
    }
    return Val::num(INFINITY);
}

Val Ctx::call_method(u32 id, const Val& obj, const Val* args, u32 argc, u32 pos) {
    switch (id) {
        case MCompLayer:
        case FLayer: {
            // AE animation: comp("Nome").layer(...) procura na OUTRA composição.
            const Composition* cc = id == MCompLayer ? comp_of(obj) : env->comp;
            const f64 tag = id == MCompLayer ? obj.v[3] : 0.0;
            if (!cc) return fail(pos, "sem composição");
            if (argc != 1) return fail(pos, "layer() recebe um nome ou um índice");
            const OrderedIds<LayerId>& order = cc->order();
            if (args[0].k == K::Num) {
                const i64 i = static_cast<i64>(std::floor(args[0].v[0] + 0.5));
                if (i < 1 || i > static_cast<i64>(order.size())) return fail(pos, "layer(" + std::to_string(i) + "): não existe");
                const LayerId lid = order.at(order.size() - static_cast<u32>(i));
                Val r = make_layer(cc->layer(lid), lid);
                r.v[3] = tag;
                return r;
            }
            if (args[0].k != K::Str) return fail(pos, "layer() recebe um nome ou um índice");
            const std::string_view nm = str(args[0]);
            for (u32 i = order.size(); i-- > 0;) {   // do topo (índice 1) para baixo
                const Layer* l = cc->layer(order.at(i));
                if (l && l->name == nm) {
                    Val r = make_layer(l, order.at(i));
                    r.v[3] = tag;
                    return r;
                }
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
            r.v[3] = id == FEffect ? 0.0 : obj.v[3];   // AE animation: camada de outra composição
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
                // O nome do After Effects do parâmetro do controle ("3D Point", "Menu", "Layer").
                if (pi == kInvalidIndex && reg->count() > 0) {
                    const Effect* fx = builtin_effects().find(inst.type);
                    for (const ControlAlias& a : kControlAliases) {
                        if (fx && std::string_view(fx->info().key) == a.key && name_eq(nm, a.param)) pi = 0;
                    }
                }
            }
            // AE expressions: Layer Control — o parâmetro É a camada escolhida.
            if (pi != kInvalidIndex && reg->at(pi).type == ParamType::LayerReference) {
                const u64 ref = pi < inst.params.size() ? inst.params[pi].constant.ref : 0;
                const Composition* lc = comp_of(obj);
                const LayerId target = ref ? LayerId::unpack(ref) : LayerId{};
                const Layer* tl = lc && target.valid() ? lc->layer(target) : nullptr;
                if (!tl) return fail(pos, "o controle de camada não tem uma camada escolhida");
                Val r = make_layer(tl, target);
                r.v[3] = obj.v[3];
                return r;
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
            if (pi == 0 && is_dropdown_control(inst.type)) d.offset = 1.0;   // Menu suspenso: 1.., como no AE
            return make_prop(d);
        }
        // --- AE expressions (lote E) ------------------------------------------------
        case MLayerMask: return layer_mask(obj, args, argc, pos);
        case MLayerContent: return layer_content(obj, args, argc, pos);
        case MGroupContent: return group_content(obj, args, argc, pos);
        case MLayerSample: return sample_image(obj, args, argc, pos);
        case MPathPoints: case MPathInTangents: case MPathOutTangents: case MPathIsClosed:
        case MPathPointOn: case MPathTangentOn: case MPathNormalOn:
            return path_method(id, obj, args, argc, pos);
        case MPathCreate: return create_path(args, argc, pos);
        case MObjHasOwn: {
            if (argc != 1) return fail(pos, "uso: hasOwnProperty(\"nome\")");
            const std::string key = to_text(args[0]);
            for (const ObjEntry& e : objects[obj.a]) if (e.key == key) return Val::num(1.0);
            return Val::num(0.0);
        }
        case MStrSplit: {
            // split("sep"[, limite]); sem separador = o texto inteiro; "" = letra por letra.
            if (argc > 2) return fail(pos, "uso: split(separador[, limite])");
            const std::string s(str(obj));
            f64 limit = static_cast<f64>(kMaxArrayLength);
            if (argc == 2 && !want_num(args[1], limit, pos, "limite")) return Val{};
            const usize maxParts = static_cast<usize>(std::clamp(limit, 0.0, static_cast<f64>(kMaxArrayLength)));
            const Val out = make_arr();
            if (argc == 0 || args[0].k == K::Undef) {
                if (maxParts > 0) (void)arr_push(out.a, make_str(s));
                return failed ? Val{} : out;
            }
            const std::string sep = to_text(args[0]);
            if (sep.empty()) {
                const std::vector<u32> cps = utf8_codepoints(s);
                for (usize i = 0; i < cps.size() && i < maxParts; ++i) if (!arr_push(out.a, make_str(utf8_encode(&cps[i], 1)))) break;
                return failed ? Val{} : out;
            }
            usize from = 0;
            while (arrays[out.a].size() < maxParts) {
                const usize at = s.find(sep, from);
                if (at == std::string::npos) { (void)arr_push(out.a, make_str(s.substr(from))); break; }
                if (!arr_push(out.a, make_str(s.substr(from, at - from)))) break;
                from = at + sep.size();
            }
            return failed ? Val{} : out;
        }
        case MStrConcat: {
            std::string s(str(obj));
            for (u32 i = 0; i < argc; ++i) {
                s += to_text(args[i]);
                if (s.size() > 64 * 1024) return fail(pos, "texto longo demais (mais de 64 KB)");
            }
            return make_str(std::move(s));
        }
        case MStrTrimStart: case MStrTrimEnd: {
            const std::string_view s = str(obj);
            if (id == MStrTrimStart) {
                const usize a = s.find_first_not_of(" \t\r\n");
                return make_str(a == std::string_view::npos ? std::string() : std::string(s.substr(a)));
            }
            const usize b = s.find_last_not_of(" \t\r\n");
            return make_str(b == std::string_view::npos ? std::string() : std::string(s.substr(0, b + 1)));
        }
        case MStrAt: {
            f64 i = 0;
            if (argc != 1 || !want_num(args[0], i, pos, "índice")) return Val{};
            const std::vector<u32> cps = utf8_codepoints(str(obj));
            const f64 k = i < 0 ? static_cast<f64>(cps.size()) + std::trunc(i) : std::trunc(i);
            if (k < 0 || k >= static_cast<f64>(cps.size())) return Val{};
            return make_str(utf8_encode(&cps[static_cast<usize>(k)], 1));
        }
        case MPropValueAtTime:
        case MPropVelocityAtTime: {
            f64 t = 0;
            if (argc != 1 || !want_num(args[0], t, pos, "tempo")) return Val{};
            const PropDesc d = props[obj.a];
            if (id == MPropValueAtTime) return prop_value(d, t * env->fps, true, pos);
            return prop_velocity(d, t * env->fps, pos);
        }
        case MPropSpeedAtTime: {
            f64 t = 0;
            if (argc != 1 || !want_num(args[0], t, pos, "tempo")) return Val{};
            const Val v = prop_velocity(props[obj.a], t * env->fps, pos);
            return failed ? Val{} : magnitude(v);
        }
        case MPropNearestKey: {
            f64 t = 0;
            if (argc != 1 || !want_num(args[0], t, pos, "tempo")) return Val{};
            return nearest_key(props[obj.a], obj.a, t * env->fps, pos);
        }
        case MMarkerKey: return marker_key(obj, args, argc, false, pos);
        case MMarkerNearestKey: return marker_key(obj, args, argc, true, pos);
        case MPropKey: {
            f64 i = 0;
            if (argc != 1 || !want_num(args[0], i, pos, "índice do keyframe")) return Val{};
            return key_of(props[obj.a], obj.a, i, pos);
        }
        case MLayerSpace + 0: case MLayerSpace + 1: case MLayerSpace + 2: case MLayerSpace + 3:
        case MLayerSpace + 4: case MLayerSpace + 5: case MLayerSpace + 6: case MLayerSpace + 7:
            return layer_space(id - MLayerSpace, obj, args, argc, pos);
        case MLayerSourceRect: return source_rect(obj, args, argc, pos);
        // --- v70: métodos de texto (por letra, como o JavaScript) -------------------
        case MNumToFixed: {
            f64 d = 0;
            if (argc > 1 || (argc == 1 && !want_num(args[0], d, pos, "casas"))) return Val{};
            const int decimals = static_cast<int>(std::clamp(std::floor(d), 0.0, 20.0));
            char buf[64];
            const f64 x = obj.v[0];
            if (!std::isfinite(x)) return make_str(number_text(x));
            std::snprintf(buf, sizeof(buf), "%.*f", decimals, x);
            std::string s = buf;
            if (s.size() > 1 && s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);   // sem "-0.00"
            return make_str(std::move(s));
        }
        case MNumToString: return make_str(to_text(obj));
        case MStrToString: return obj;
        case MStrUpper: case MStrLower: {
            std::vector<u32> cps = utf8_codepoints(str(obj));
            for (u32& c : cps) c = cp_case(c, id == MStrUpper);
            return make_str(utf8_encode(cps.data(), cps.size()));
        }
        case MStrTrim: {
            const std::string_view s = str(obj);
            const usize a = s.find_first_not_of(" \t\r\n");
            if (a == std::string_view::npos) return make_str({});
            const usize b = s.find_last_not_of(" \t\r\n");
            return make_str(std::string(s.substr(a, b - a + 1)));
        }
        case MStrSubstr: case MStrSubstring: case MStrSlice: case MStrCharAt: case MStrCharCodeAt: {
            const std::vector<u32> cps = utf8_codepoints(str(obj));
            const f64 n = static_cast<f64>(cps.size());
            f64 a = 0, b = n;
            if (argc >= 1 && !want_num(args[0], a, pos, "início")) return Val{};
            if (argc >= 2 && !want_num(args[1], b, pos, id == MStrSubstr ? "quantidade" : "fim")) return Val{};
            if (argc > 2) return fail(pos, "recebe no máximo dois números");
            a = std::isnan(a) ? 0.0 : std::trunc(a);
            b = std::isnan(b) ? 0.0 : std::trunc(b);
            f64 from = 0, to = 0;
            if (id == MStrCharAt || id == MStrCharCodeAt) {
                if (a < 0 || a >= n) return id == MStrCharAt ? make_str({}) : Val::num(std::nan(""));
                if (id == MStrCharCodeAt) return Val::num(static_cast<f64>(cps[static_cast<usize>(a)]));
                return make_str(utf8_encode(&cps[static_cast<usize>(a)], 1));
            }
            if (id == MStrSubstr) {          // (início, quantidade); início negativo conta do fim
                from = a < 0 ? std::max(0.0, n + a) : std::min(a, n);
                to = argc >= 2 ? std::clamp(from + std::max(0.0, b), from, n) : n;
            } else if (id == MStrSlice) {    // (início, fim); negativos contam do fim
                from = a < 0 ? std::max(0.0, n + a) : std::min(a, n);
                to = b < 0 ? std::max(0.0, n + b) : std::min(b, n);
            } else {                          // substring: negativo = 0, troca se invertido
                from = std::clamp(a, 0.0, n);
                to = std::clamp(b, 0.0, n);
                if (from > to) std::swap(from, to);
            }
            if (to <= from) return make_str({});
            return make_str(utf8_encode(cps.data() + static_cast<usize>(from), static_cast<usize>(to - from)));
        }
        case MStrIndexOf: case MStrLastIndexOf: case MStrIncludes: case MStrStartsWith: case MStrEndsWith: {
            if (argc < 1 || argc > 2) return fail(pos, "uso: indexOf(texto)");
            const std::string needle = to_text(args[0]);
            const std::string_view s = str(obj);
            usize at = std::string_view::npos;
            if (id == MStrLastIndexOf) at = s.rfind(needle);
            else if (id == MStrEndsWith) return Val::num(s.size() >= needle.size() && s.substr(s.size() - needle.size()) == needle ? 1.0 : 0.0);
            else at = s.find(needle);
            if (id == MStrIncludes) return Val::num(at != std::string_view::npos ? 1.0 : 0.0);
            if (id == MStrStartsWith) return Val::num(at == 0 ? 1.0 : 0.0);
            if (at == std::string_view::npos) return Val::num(-1.0);
            return Val::num(static_cast<f64>(utf8_codepoints(s.substr(0, at)).size()));   // em letras
        }
        case MStrReplace: case MStrReplaceAll: {
            // replace troca a primeira ocorrência; replaceAll, todas (texto, sem regex).
            if (argc != 2) return fail(pos, "uso: replace(\"de\", \"para\")");
            const std::string from = to_text(args[0]), to = to_text(args[1]);
            std::string s(str(obj));
            if (from.empty()) return make_str(std::move(s));
            for (usize at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
                s.replace(at, from.size(), to);
                if (s.size() > 64 * 1024) return fail(pos, "texto longo demais");
                if (id == MStrReplace) break;
            }
            return make_str(std::move(s));
        }
        case MStrPadStart: case MStrPadEnd: case MStrRepeat: {
            f64 count = 0;
            if (argc < 1 || !want_num(args[0], count, pos, id == MStrRepeat ? "vezes" : "tamanho")) return Val{};
            const std::string s(str(obj));
            if (id == MStrRepeat) {
                if (count < 0 || count * static_cast<f64>(s.size()) > 64 * 1024) return fail(pos, "repeat: vezes demais");
                std::string out;
                for (i64 i = 0; i < static_cast<i64>(count); ++i) out += s;
                return make_str(std::move(out));
            }
            const std::string fill = argc >= 2 ? to_text(args[1]) : std::string(" ");
            const usize have = utf8_codepoints(s).size();
            if (fill.empty() || count <= static_cast<f64>(have) || count > 4096) return make_str(std::string(s));
            const std::vector<u32> fc = utf8_codepoints(fill);
            std::vector<u32> pad;
            for (usize i = 0; pad.size() + have < static_cast<usize>(count); ++i) pad.push_back(fc[i % fc.size()]);
            const std::string ps = utf8_encode(pad.data(), pad.size());
            return make_str(id == MStrPadStart ? ps + s : s + ps);
        }
        default:
            if (id >= MArrPush && id <= MArrAt) return call_array(id, obj, args, argc, pos);
            return fail(pos, "método desconhecido");
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
        const f64 local = now_local();
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
        // --- v70: texto ----------------------------------------------------------------
        case FString:
            if (!need(0, 1, "String(valor)")) return Val{};
            return make_str(argc ? to_text(args[0]) : std::string());
        case FNumber: case FParseFloat: case FParseInt: {
            if (!need(1, 2, id == FParseInt ? "parseInt(texto)" : "Number(texto)")) return Val{};
            if (args[0].k == K::Num) return Val::num(id == FParseInt ? std::trunc(args[0].v[0]) : args[0].v[0]);
            const std::string s = to_text(args[0]);
            const char* b = s.c_str();
            while (*b == ' ' || *b == '\t') ++b;
            char* end = nullptr;
            f64 v = id == FParseInt ? static_cast<f64>(std::strtoll(b, &end, argc >= 2 ? std::clamp(static_cast<int>(args[1].v[0]), 2, 36) : 10))
                                    : std::strtod(b, &end);
            if (end == b) return Val::num(id == FNumber && *b == 0 ? 0.0 : std::nan(""));
            if (id == FNumber) { while (*end == ' ' || *end == '\t') ++end; if (*end) return Val::num(std::nan("")); }
            return Val::num(v);
        }
        case FTimeToTimecode: case FTimeToCurrentFormat: {
            // timeToTimecode([t[, base]]): HH:MM:SS:QQ (base = quadros por segundo).
            f64 tt = nowF / e.fps, base = std::round(e.fps);
            if (argc >= 1 && !want_num(args[0], tt, pos, "t")) return Val{};
            if (argc >= 2 && !want_num(args[1], base, pos, "base")) return Val{};
            if (argc > 3) return fail(pos, "uso: timeToTimecode([t[, base]])");
            base = std::max(1.0, std::round(base));
            const bool neg = tt < 0;
            const i64 total = static_cast<i64>(std::floor(std::fabs(tt) * base + 1e-6));
            const i64 b = static_cast<i64>(base);
            const i64 secs = total / b;
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s%02lld:%02lld:%02lld:%02lld", neg ? "-" : "", static_cast<long long>(secs / 3600),
                          static_cast<long long>(secs / 60 % 60), static_cast<long long>(secs % 60), static_cast<long long>(total % b));
            return make_str(buf);
        }
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
        case FMax:
        case FHypot: {
            if (argc == 0) return fail(pos, "min/max precisam de argumentos");
            // Um vetor de verdade no lugar dos números também vale: Math.max(lista).
            const bool list = argc == 1 && args[0].k == K::Arr;
            if (argc == 1 && args[0].k == K::Vec && id != FHypot) return fail(pos, "argumento precisa ser um número");
            const u32 count = list ? static_cast<u32>(arrays[args[0].a].size()) : argc;
            if (count == 0) return Val::num(id == FMin ? INFINITY : id == FMax ? -INFINITY : 0.0);
            f64 r = 0;
            for (u32 i = 0; i < count; ++i) {
                f64 x = 0;
                const Val item = list ? arrays[args[0].a][i] : args[i];
                if (id == FHypot && item.k == K::Vec) { for (u32 c = 0; c < item.n; ++c) r += item.v[c] * item.v[c]; continue; }
                if (!want_num(item, x, pos, "argumento")) return Val{};
                if (id == FHypot) r += x * x;
                else r = i == 0 ? x : (id == FMin ? std::min(r, x) : std::max(r, x));
            }
            return Val::num(id == FHypot ? std::sqrt(r) : r);
        }
        // --- AE expressions (lote E) ------------------------------------------------
        case FSign: return unary([](f64 x) { return x > 0.0 ? 1.0 : x < 0.0 ? -1.0 : x; }, "sign(x)");
        case FTrunc: return unary([](f64 x) { return std::trunc(x); }, "trunc(x)");
        case FCbrt: return unary([](f64 x) { return std::cbrt(x); }, "cbrt(x)");
        case FLog10: return unary([](f64 x) { return std::log10(x); }, "log10(x)");
        case FLog2: return unary([](f64 x) { return std::log2(x); }, "log2(x)");
        case FIsNaN: case FIsFinite: case FNumIsInteger: {
            if (!need(1, 1, "isNaN(valor)")) return Val{};
            const bool num = args[0].k == K::Num;
            const f64 x = num ? args[0].v[0] : std::nan("");
            if (id == FIsNaN) return Val::num(std::isnan(x) ? 1.0 : 0.0);
            if (id == FIsFinite) return Val::num(std::isfinite(x) ? 1.0 : 0.0);
            return Val::num(std::isfinite(x) && x == std::floor(x) ? 1.0 : 0.0);
        }
        case FIsArray:
            if (!need(1, 1, "Array.isArray(valor)")) return Val{};
            return Val::num(args[0].k == K::Arr || args[0].k == K::Vec ? 1.0 : 0.0);
        case FObjKeys: case FObjValues: case FObjEntries: {
            if (!need(1, 1, "Object.keys(objeto)")) return Val{};
            const Val src = args[0];
            const Val out = make_arr();
            if (src.k == K::Obj) {
                const usize count = objects[src.a].size();
                for (usize i = 0; i < count && !failed; ++i) {
                    if (id == FObjKeys) { (void)arr_push(out.a, make_str(std::string(objects[src.a][i].key))); continue; }
                    if (id == FObjValues) { (void)arr_push(out.a, objects[src.a][i].v); continue; }
                    const Val pair = make_arr();
                    (void)arr_push(pair.a, make_str(std::string(objects[src.a][i].key)));
                    (void)arr_push(pair.a, objects[src.a][i].v);
                    (void)arr_push(out.a, pair);
                }
            } else if (src.k == K::Arr || src.k == K::Vec || src.k == K::Num) {
                const Val list = to_arr(src);
                const usize count = arrays[list.a].size();
                for (usize i = 0; i < count && !failed; ++i) {
                    if (id == FObjKeys) { (void)arr_push(out.a, make_str(std::to_string(i))); continue; }
                    if (id == FObjValues) { (void)arr_push(out.a, arrays[list.a][i]); continue; }
                    const Val pair = make_arr();
                    (void)arr_push(pair.a, make_str(std::to_string(i)));
                    (void)arr_push(pair.a, arrays[list.a][i]);
                    (void)arr_push(out.a, pair);
                }
            } else {
                return fail(pos, "Object.keys precisa de um objeto { }");
            }
            return failed ? Val{} : out;
        }
        case FFromCharCode: {
            std::vector<u32> cps;
            for (u32 i = 0; i < argc; ++i) {
                f64 c = 0;
                if (!want_num(args[i], c, pos, "código")) return Val{};
                cps.push_back(c >= 0 && c <= 1114111.0 ? static_cast<u32>(c) : 0xFFFDu);
            }
            return make_str(utf8_encode(cps.data(), cps.size()));
        }
        case FRgbToHsl: case FHslToRgb: case FHexToRgb: return color_fn(id, args, argc, pos);
        case FFootage:
            if (!need(1, 1, "footage(\"nome do arquivo\")") || args[0].k != K::Str) return fail(pos, "uso: footage(\"nome do arquivo\")");
            return footage_of(str(args[0]), 0, pos);
        case FCreatePath: return create_path(args, argc, pos);
        case FSampleImage:
            if (!e.layer) return fail(pos, "sem camada (expressão fora de uma camada)");
            return sample_image(make_layer(e.layer, e.layerId), args, argc, pos);
        case FMask:
            if (!e.layer) return fail(pos, "sem camada (expressão fora de uma camada)");
            return layer_mask(make_layer(e.layer, e.layerId), args, argc, pos);
        case FContent:
            if (!e.layer) return fail(pos, "sem camada (expressão fora de uma camada)");
            return layer_content(make_layer(e.layer, e.layerId), args, argc, pos);
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
            f64 t = nowF / e.fps, fps = e.fps;
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
            f64 freq = 0, oct = 1, mult = 0.5, t = nowF / e.fps;
            if (!need(2, 5, "wiggle(frequência, amplitude[, oitavas, multiplicador, t])")) return Val{};
            if (!want_num(args[0], freq, pos, "frequência")) return Val{};
            if (!want_numvec(args[1], pos, "amplitude")) return Val{};
            if (argc >= 3 && !want_num(args[2], oct, pos, "oitavas")) return Val{};
            if (argc >= 4 && !want_num(args[3], mult, pos, "multiplicador")) return Val{};
            if (argc >= 5 && !want_num(args[4], t, pos, "t")) return Val{};
            const u32 octaves = static_cast<u32>(std::clamp(oct, 1.0, 10.0));
            Val base = nowValue;
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
                if (id == FValueAtTime) return nowValue;
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
            else if (own.kind == 1 || own.kind == 3 || own.kind == 4) own.key0 += e.component, own.count = 1;
            props[pr.a] = own;
            return key_of(own, pr.a, i, pos);
        }
        case FShake: return shake(args, argc, pos);
        case FTimeSinceMarker: return time_since_marker(args, argc, pos);
        case FSpeedAtTime: {
            f64 t = 0;
            if (!need(1, 1, "speedAtTime(t)") || !want_num(args[0], t, pos, "t")) return Val{};
            if (!e.layer) return Val::num(0.0);
            const Val v = prop_velocity(e.self, t * e.fps, pos);
            return failed ? Val{} : magnitude(v);
        }
        case FTemporalWiggle: {
            // Lê a PRÓPRIA propriedade num instante que oscila: t + ruído × amplitude (segundos).
            f64 freq = 0, amp = 0, oct = 1, mult = 0.5, t = nowF / e.fps;
            if (!need(2, 5, "temporalWiggle(frequência, amplitude[, oitavas, multiplicador, t])")) return Val{};
            if (!want_num(args[0], freq, pos, "frequência") || !want_num(args[1], amp, pos, "amplitude")) return Val{};
            if (argc >= 3 && !want_num(args[2], oct, pos, "oitavas")) return Val{};
            if (argc >= 4 && !want_num(args[3], mult, pos, "multiplicador")) return Val{};
            if (argc >= 5 && !want_num(args[4], t, pos, "t")) return Val{};
            const u32 octaves = static_cast<u32>(std::clamp(oct, 1.0, 10.0));
            const f64 when = t + amp * fractal(hash_mix(e.seedBase, 0x7E3Au), t * freq + 0.2718, octaves, mult);
            if (!e.layer) return nowValue;
            return prop_value(e.self, when * e.fps, false, pos);
        }
        case FSmooth: {
            // Média de `amostras` leituras da própria propriedade numa janela de `largura` s em volta de t.
            f64 width = 0.2, samples = 5, t = nowF / e.fps;
            if (!need(0, 3, "smooth([largura, amostras, t])")) return Val{};
            if (argc >= 1 && !want_num(args[0], width, pos, "largura")) return Val{};
            if (argc >= 2 && !want_num(args[1], samples, pos, "amostras")) return Val{};
            if (argc >= 3 && !want_num(args[2], t, pos, "t")) return Val{};
            if (!e.layer) return nowValue;
            const u32 n = static_cast<u32>(std::clamp(std::floor(samples), 1.0, 64.0));
            if (n == 1 || width <= 0.0) return prop_value(e.self, t * e.fps, false, pos);
            f64 acc[4]{};
            u32 comps = 1;
            for (u32 i = 0; i < n; ++i) {
                const f64 at = t + (static_cast<f64>(i) / static_cast<f64>(n - 1) - 0.5) * width;
                const Val v = prop_value(e.self, at * e.fps, false, pos);
                if (failed) return Val{};
                comps = v.n;
                for (u32 c = 0; c < v.n; ++c) acc[c] += v.v[c];
            }
            for (u32 c = 0; c < comps; ++c) acc[c] /= static_cast<f64>(n);
            return Val::vec(acc, comps);
        }
        case FPosterizeTime: {
            // Daqui em diante, o "agora" anda em degraus de 1/n s (n ≤ 0: parado no início).
            f64 rate = 0;
            if (!need(1, 1, "posterizeTime(quadros por segundo)") || !want_num(args[0], rate, pos, "quadros por segundo")) return Val{};
            const f64 secs = e.compF / e.fps;
            const f64 stepped = rate > 0.0 ? std::floor(secs * rate + 1e-9) / rate : 0.0;
            nowF = stepped * e.fps;
            if (e.layer) {
                f64 vals[4]{};
                for (u32 c = 0; c < e.self.count; ++c) vals[c] = component(e.self, c, now_local(), false);
                nowValue = Val::vec(vals, e.self.count);
            }
            seed_rng();
            return Val{};   // instrução, como seedRandom: sozinha não vira valor
        }
        case FNearestKey: {
            f64 t = 0;
            if (!need(1, 1, "nearestKey(t)") || !want_num(args[0], t, pos, "t")) return Val{};
            if (!e.layer) return fail(pos, "sem keyframes (expressão fora de uma camada)");
            const Val pr = make_prop(e.self);
            const PropDesc own = own_track();
            props[pr.a] = own;
            return nearest_key(own, pr.a, t * e.fps, pos);
        }
        case FLayer: return call_method(FLayer, Val{}, args, argc, pos);
        case FEffect: return call_method(FEffect, Val{}, args, argc, pos);
        // AE animation: sem camada na frente, valem para a própria camada.
        case FToComp: case FFromComp: case FToWorld: case FFromWorld:
        case FToCompVec: case FFromCompVec: case FToWorldVec: case FFromWorldVec:
            if (!e.layer) return fail(pos, "sem camada (expressão fora de uma camada)");
            return layer_space(id - FToComp, make_layer(e.layer, e.layerId), args, argc, pos);
        case FSourceRect:
            if (!e.layer) return fail(pos, "sem camada (expressão fora de uma camada)");
            return source_rect(make_layer(e.layer, e.layerId), args, argc, pos);
        case FLookAt: return look_at(args, argc, pos);
        case FComp: return comp_fn(args, argc, pos);
        default: return fail(pos, "função desconhecida");
    }
}

// =============================================================================
// AE animation (2026-11-11): espaço da camada, mira, caixa e outras composições.
// As matrizes são as do render (timeline/LayerWorld): a expressão vê a camada
// exatamente onde ela é desenhada.
// =============================================================================
namespace {
Vec3 mul_point(const Mat4& m, Vec3 p, f32 w) noexcept {
    const Vec4 r = m * Vec4{p.x, p.y, p.z, w};
    return Vec3{r.x, r.y, r.z};
}
} // namespace

/// toComp/fromComp/toWorld/fromWorld (+ *Vec): (ponto[, t]). Mundo = o espaço
/// 3D da cena (no 2D, igual à composição). Na camada 3D, toComp projeta pela
/// câmera ativa e fromComp acha o ponto do PLANO da camada sob o pixel.
Val Ctx::layer_space(u32 which, const Val& layerVal, const Val* args, u32 argc, u32 pos) {
    const char* name = kSpaceNames[which & 7u];
    const Layer* l = layer_of(layerVal);
    const Composition* c = comp_of(layerVal);
    if (!l || !c) return fail(pos, "a camada não existe mais");
    if (argc < 1 || argc > 2) return fail(pos, std::string("uso: ") + name + "(ponto[, t])");
    if (args[0].k != K::Vec || args[0].n < 2) return fail(pos, std::string(name) + ": o ponto precisa ser [x, y] ou [x, y, z]");
    f64 t = nowF / env->fps;
    if (argc == 2 && !want_num(args[1], t, pos, "t")) return Val{};
    const f64 cf = c->fps() > 0.0 ? c->fps() : 30.0;
    const f64 frame = t * cf;
    if (!std::isfinite(frame) || std::fabs(frame) > 1.0e12) return fail(pos, std::string(name) + ": tempo inválido");
    const bool threeD = world::wants_3d(*c, *l, FrameIndex{static_cast<i64>(std::floor(frame))});
    const Mat4 m = threeD ? world::world_3d_frac(*c, *l, frame) : world::world_2d_frac(*c, *l, frame);
    if (g_tls.cycle) return fail(pos, g_tls.cycleMsg.empty() ? "dependência circular" : g_tls.cycleMsg);
    const u32 n = args[0].n;
    const Vec3 p{static_cast<f32>(args[0].v[0]), static_cast<f32>(args[0].v[1]), n > 2 ? static_cast<f32>(args[0].v[2]) : 0.0f};
    Mat4 inv;
    const bool invertible = world::invert(m, inv);
    const Mat4 a = threeD ? world::comp_from_world(*c, frame) * m : m;   // composição ← camada
    auto toComp = [&](Vec3 q) -> Vec3 {
        if (!threeD) return mul_point(m, q, 1.0f);
        const Vec4 h = a * Vec4{q.x, q.y, q.z, 1.0f};
        const f32 w = std::fabs(h.w) > 1e-9f ? h.w : 1e-9f;
        return Vec3{h.x / w, h.y / w, 0.0f};
    };
    auto fromComp = [&](Vec3 q) -> Vec3 {
        if (!threeD) return mul_point(inv, q, 1.0f);
        // Plano da camada (z = 0): a(x, y, 0, 1) projeta em q. Duas equações lineares.
        const f64 cx = q.x, cy = q.y;
        const f64 a00 = a.col[0].x - cx * a.col[0].w, a01 = a.col[1].x - cx * a.col[1].w, b0 = cx * a.col[3].w - a.col[3].x;
        const f64 a10 = a.col[0].y - cy * a.col[0].w, a11 = a.col[1].y - cy * a.col[1].w, b1 = cy * a.col[3].w - a.col[3].y;
        const f64 det = a00 * a11 - a01 * a10;
        if (std::fabs(det) < 1e-12) return Vec3{0, 0, 0};
        return Vec3{static_cast<f32>((b0 * a11 - a01 * b1) / det), static_cast<f32>((a00 * b1 - b0 * a10) / det), 0.0f};
    };
    if ((which == 1 || which == 3 || which == 5 || which == 7) && !invertible && !threeD)
        return fail(pos, std::string(name) + ": a camada está achatada (escala zero)");
    Vec3 r;
    switch (which) {
        case 0: r = toComp(p); break;
        case 1: r = fromComp(p); break;
        case 2: r = mul_point(m, p, 1.0f); break;
        case 3: r = mul_point(inv, p, 1.0f); break;
        case 4: r = toComp(p) - toComp(Vec3{0, 0, 0}); break;
        case 5: r = fromComp(p) - fromComp(Vec3{0, 0, 0}); break;
        case 6: r = mul_point(m, p, 0.0f); break;
        default: r = mul_point(inv, p, 0.0f); break;
    }
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z)) return fail(pos, std::string(name) + ": resultado inválido");
    const f64 out[3] = {r.x, r.y, r.z};
    return Val::vec(out, std::min<u32>(n, 3u));
}

/// lookAt(de, para): a orientação [x, y, 0] (graus) que aponta o eixo Z da
/// camada de `de` para `para` — para usar na rotação X/Y de uma camada 3D.
Val Ctx::look_at(const Val* args, u32 argc, u32 pos) {
    if (argc != 2 || args[0].k != K::Vec || args[1].k != K::Vec)
        return fail(pos, "uso: lookAt(de, para) com pontos [x, y, z]");
    const f64 dx = args[1].v[0] - args[0].v[0], dy = args[1].v[1] - args[0].v[1];
    const f64 dz = (args[1].n > 2 ? args[1].v[2] : 0.0) - (args[0].n > 2 ? args[0].v[2] : 0.0);
    const f64 flat = std::sqrt(dx * dx + dz * dz);
    const f64 out[3] = {flat + std::fabs(dy) < 1e-12 ? 0.0 : std::atan2(-dy, flat) * 180.0 / 3.14159265358979323846,
                        flat < 1e-12 ? 0.0 : std::atan2(dx, dz) * 180.0 / 3.14159265358979323846, 0.0};
    return Val::vec(out, 3);
}

/// comp("Nome"): outra composição do projeto (nome exato; senão sem caixa).
Val Ctx::comp_fn(const Val* args, u32 argc, u32 pos) {
    if (argc != 1 || args[0].k != K::Str) return fail(pos, "uso: comp(\"Nome\")");
    if (!env->tl) return fail(pos, "sem projeto (expressão fora de uma camada)");
    const std::string_view nm = str(args[0]);
    u32 i = 0, exact = 0, loose = 0;
    const Composition* exactComp = nullptr;
    const Composition* looseComp = nullptr;
    env->tl->for_each_composition([&](CompositionId, const Composition& c) {
        ++i;
        if (!exact && c.name() == nm) { exact = i; exactComp = &c; }
        else if (!loose && name_eq(c.name(), nm)) { loose = i; looseComp = &c; }
    });
    const u32 tag = exact ? exact : loose;
    const Composition* found = exact ? exactComp : looseComp;
    if (!tag) return fail(pos, "composição \"" + std::string(nm) + "\" não existe");
    Val r;
    r.k = K::Comp;
    r.v[3] = found == env->comp ? 0.0 : static_cast<f64>(tag);
    return r;
}

/// sourceRectAtTime([t[, includeExtents]]): a caixa do conteúdo no instante,
/// em px da camada — texto (com o key do texto-fonte), forma, vetor,
/// pré-composição; o resto, a caixa da camada.
Val Ctx::source_rect(const Val& layerVal, const Val* args, u32 argc, u32 pos) {
    const Layer* l = layer_of(layerVal);
    const Composition* c = comp_of(layerVal);
    if (!l || !c) return fail(pos, "a camada não existe mais");
    if (argc > 2) return fail(pos, "uso: sourceRectAtTime([t[, incluirExtensões]])");
    f64 t = nowF / env->fps;
    if (argc >= 1 && !want_num(args[0], t, pos, "t")) return Val{};
    const bool extents = argc >= 2 && truthy(args[1]);
    const f64 cf = c->fps() > 0.0 ? c->fps() : 30.0;
    const f64 local = layer_local_from_comp(l, t * cf);
    if (!std::isfinite(local)) return fail(pos, "sourceRectAtTime: tempo inválido");
    f64 top = 0, left = 0, width = 0, height = 0;
    switch (l->kind) {
        case LayerKind::Text: {
            TextData shown = l->text;
            const bool keyed = text::apply_source_text(l->text, static_cast<i64>(std::floor(local)), shown);
            if (const Track* tr = l->tracks.find(TrackProperty::TextTracking); tr && tr->driven())
                shown.tracking = std::clamp(tr->value_or_f(local, shown.tracking), -1000.0f, 1000.0f);
            const auto font = text::FontManager::instance().font_for(shown);
            if (!font) return fail(pos, "sourceRectAtTime: sem fonte para o texto");
            const text::TextExtent ext = text::measure(*font, shown);
            const TextData& base = l->text;
            // A mesma origem do render: a margem do texto de base e, com key, o
            // ponto de alinhamento do key sobre o do texto de base.
            const f64 pad = base.strokeWidth > 0.0f ? base.strokeWidth + 2.0f : 2.0f;
            left = top = pad;
            if (keyed && shown.boxMode == 0) {
                const auto baseFont = text::FontManager::instance().font_for(base);
                const f64 wb = baseFont ? text::measure(*baseFont, base).width : 0.0;
                auto at = [](u32 align, f64 w) { return align == 1 ? w * 0.5 : align == 2 ? w : 0.0; };
                left += at(base.alignment, wb) - at(shown.alignment, ext.width);
            }
            width = ext.width;
            height = ext.height;
            if (extents && shown.strokeWidth > 0.0f) {
                left -= shown.strokeWidth; top -= shown.strokeWidth;
                width += 2.0 * shown.strokeWidth; height += 2.0 * shown.strokeWidth;
            }
            break;
        }
        case LayerKind::Shape: {
            if (l->shape.shapeType == kShapeVector) {
                std::vector<VectorGroup> ev;
                ev.reserve(l->shape.vector.groups.size());
                for (u32 gi = 0; gi < l->shape.vector.groups.size(); ++gi)
                    ev.push_back(vector::evaluate_group(l->shape.vector.groups[gi], l->tracks, gi, local, cf));
                Vec2 mn, mx;
                if (vector::bounds_of(ev, local, mn, mx)) { left = mn.x; top = mn.y; width = mx.x - mn.x; height = mx.y - mn.y; }
                break;
            }
            const Rect& b = l->shape.bounds;
            auto param = [&](u32 p, f32 fallback) {
                const Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, p);
                return tr && !tr->keys.empty() ? static_cast<f64>(tr->value_or_f(local, fallback)) : static_cast<f64>(fallback);
            };
            left = b.x; top = b.y;
            width = std::max(0.0, param(5, b.w));
            height = std::max(0.0, param(6, b.h));
            if (extents && l->shape.strokeWidth > 0.0f) {
                const f64 s = l->shape.strokeWidth * 0.5;
                left -= s; top -= s; width += 2.0 * s; height += 2.0 * s;
            }
            break;
        }
        case LayerKind::Null: width = height = 100.0; break;
        case LayerKind::Composition: {
            const Composition* child = env->tl ? env->tl->composition(l->nested.composition) : nullptr;
            width = child ? child->width() : c->width();
            height = child ? child->height() : c->height();
            break;
        }
        default: width = c->width(); height = c->height(); break;
    }
    Val r;
    r.k = K::Rect;
    r.n = 4;
    r.v[0] = top; r.v[1] = left; r.v[2] = width; r.v[3] = height;
    return r;
}

// =============================================================================
// AE expressions (lote E, 2026-11-12): vetores de verdade, objetos, cor,
// arquivos do projeto, caminhos (máscara e forma) e sampleImage.
// =============================================================================
namespace {
struct FootageEntry { FootageProvider fn; void* ctx; };
std::vector<FootageEntry> g_footageProviders;     // sob g_providerMutex

struct ImageKey {
    const Timeline* tl = nullptr;
    u32  comp = 0;
    u64  layer = 0;
    bool post = true;
    bool operator==(const ImageKey&) const noexcept = default;
};
struct ImageKeyHash {
    usize operator()(const ImageKey& k) const noexcept {
        return std::hash<const void*>()(k.tl) ^ (std::hash<u64>()(k.layer) * 0x9E3779B97F4A7C15ull)
             ^ (static_cast<usize>(k.comp) * 0x85EBCA77u) ^ (k.post ? 0x5bd1e995u : 0u);
    }
};
struct SourceEntry { ImageSource fn; void* ctx; };
std::mutex g_imageMutex;
std::unordered_map<ImageKey, std::shared_ptr<const LayerPixels>, ImageKeyHash> g_images;         // publicadas
std::vector<ImageRequest> g_imageRequests;
std::vector<SourceEntry> g_imageSources;

/// Máscaras cujo caminho está sendo avaliado nesta thread (A lê B que lê A).
thread_local std::vector<const Mask*> g_maskEval;

bool callable(const Val& v) noexcept { return v.k == K::UFn || v.k == K::Fn || v.k == K::Method; }
} // namespace

Val Ctx::array_member(const Val& arr, std::string_view name, u32 pos) {
    (void)pos;
    static constexpr NameEntry kArr[] = {
        {"push", MArrPush}, {"pop", MArrPop}, {"shift", MArrShift}, {"unshift", MArrUnshift}, {"slice", MArrSlice},
        {"splice", MArrSplice}, {"concat", MArrConcat}, {"join", MArrJoin}, {"indexOf", MArrIndexOf},
        {"lastIndexOf", MArrLastIndexOf}, {"includes", MArrIncludes}, {"reverse", MArrReverse}, {"sort", MArrSort},
        {"map", MArrMap}, {"filter", MArrFilter}, {"reduce", MArrReduce}, {"forEach", MArrForEach}, {"some", MArrSome},
        {"every", MArrEvery}, {"find", MArrFind}, {"findIndex", MArrFindIndex}, {"fill", MArrFill},
        {"toString", MArrToString}, {"flat", MArrFlat}, {"at", MArrAt},
    };
    for (const NameEntry& m : kArr) if (name == m.name) return make_method(m.id, arr);
    return Val{};
}

/// Métodos de vetor. Tudo que anda o vetor inteiro cobra passos pelo tamanho
/// (um laço de shift() num vetor grande não vira minutos de trabalho).
Val Ctx::call_array(u32 id, const Val& self, const Val* args, u32 argc, u32 pos) {
    if (self.k != K::Arr) return fail(pos, "isto não é um vetor");
    const u32 ai = self.a;
    const u32 depth = curDepth + 1;
    auto size = [&]() -> usize { return arrays[ai].size(); };
    auto work = [&](usize n) -> bool { steps += static_cast<u32>(n / 16); return tick(pos); };
    // Índice relativo (negativo conta do fim), preso em [0, n].
    auto rel = [](const Val& v, f64 def, f64 n) -> f64 {
        if (v.k != K::Num || std::isnan(v.v[0])) return def;
        const f64 x = std::trunc(v.v[0]);
        return x < 0 ? std::max(0.0, n + x) : std::min(x, n);
    };
    switch (id) {
        case MArrPush: {
            for (u32 i = 0; i < argc; ++i) if (!arr_push(ai, args[i])) return Val{};
            return Val::num(static_cast<f64>(size()));
        }
        case MArrPop: {
            if (arrays[ai].empty()) return Val{};
            const Val v = arrays[ai].back();
            arrays[ai].pop_back();
            return v;
        }
        case MArrShift: {
            if (arrays[ai].empty() || !work(size())) return Val{};
            const Val v = arrays[ai].front();
            arrays[ai].erase(arrays[ai].begin());
            return v;
        }
        case MArrUnshift: {
            if (size() + argc > kMaxArrayLength) return fail_hard(pos, "vetor grande demais (mais de 65536 itens)");
            if (!charge(argc * sizeof(Val)) || !work(size())) return Val{};
            arrays[ai].insert(arrays[ai].begin(), args, args + argc);
            return Val::num(static_cast<f64>(size()));
        }
        case MArrSlice: {
            const f64 n = static_cast<f64>(size());
            const usize a = static_cast<usize>(argc >= 1 ? rel(args[0], 0.0, n) : 0.0);
            const usize b = static_cast<usize>(argc >= 2 ? rel(args[1], n, n) : n);
            const Val out = make_arr();
            if (!work(b > a ? b - a : 0)) return Val{};
            for (usize i = a; i < b && i < size(); ++i) if (!arr_push(out.a, arrays[ai][i])) return Val{};
            return out;
        }
        case MArrSplice: {
            const f64 n = static_cast<f64>(size());
            const usize start = static_cast<usize>(argc >= 1 ? rel(args[0], 0.0, n) : 0.0);
            f64 del = argc == 0 ? 0.0 : n - static_cast<f64>(start);
            if (argc >= 2) {
                if (!want_num(args[1], del, pos, "quantidade")) return Val{};
                del = std::isnan(del) ? 0.0 : std::clamp(std::trunc(del), 0.0, n - static_cast<f64>(start));
            }
            const usize count = static_cast<usize>(del);
            const u32 ins = argc > 2 ? argc - 2 : 0;
            if (size() - count + ins > kMaxArrayLength) return fail_hard(pos, "vetor grande demais (mais de 65536 itens)");
            const Val out = make_arr();
            for (usize i = 0; i < count; ++i) if (!arr_push(out.a, arrays[ai][start + i])) return Val{};
            if (!charge(ins * sizeof(Val)) || !work(size())) return Val{};
            arrays[ai].erase(arrays[ai].begin() + static_cast<std::ptrdiff_t>(start),
                             arrays[ai].begin() + static_cast<std::ptrdiff_t>(start + count));
            if (ins) arrays[ai].insert(arrays[ai].begin() + static_cast<std::ptrdiff_t>(start), args + 2, args + 2 + ins);
            return out;
        }
        case MArrConcat: {
            const Val out = make_arr();
            if (!work(size())) return Val{};
            for (usize i = 0; i < size(); ++i) if (!arr_push(out.a, arrays[ai][i])) return Val{};
            for (u32 k = 0; k < argc; ++k) {
                const Val a = args[k];
                if (a.k == K::Arr) {
                    const usize count = arrays[a.a].size();
                    if (!work(count)) return Val{};
                    for (usize i = 0; i < count; ++i) if (!arr_push(out.a, arrays[a.a][i])) return Val{};
                } else if (a.k == K::Vec) {
                    for (u32 i = 0; i < a.n; ++i) if (!arr_push(out.a, Val::num(a.v[i]))) return Val{};
                } else if (!arr_push(out.a, a)) {
                    return Val{};
                }
            }
            return out;
        }
        case MArrJoin: case MArrToString: {
            std::string sep = ",";
            if (id == MArrJoin && argc >= 1 && args[0].k != K::Undef) sep = to_text(args[0]);
            if (!work(size())) return Val{};
            std::string s;
            for (usize i = 0; i < size(); ++i) {
                if (i) s += sep;
                const Val item = arrays[ai][i];
                if (item.k != K::Undef && item.k != K::Null) s += to_text(item, 1);
                if (s.size() > 64 * 1024) return fail(pos, "texto longo demais (mais de 64 KB)");
            }
            return make_str(std::move(s));
        }
        case MArrIndexOf: case MArrLastIndexOf: case MArrIncludes: {
            if (argc < 1) return fail(pos, "uso: indexOf(valor)");
            if (!work(size())) return Val{};
            f64 found = -1.0;
            for (usize i = 0; i < size(); ++i) {
                if (!loose_equal(arrays[ai][i], args[0])) continue;
                found = static_cast<f64>(i);
                if (id != MArrLastIndexOf) break;
            }
            if (id == MArrIncludes) return Val::num(found >= 0.0 ? 1.0 : 0.0);
            return Val::num(found);
        }
        case MArrReverse: {
            if (!work(size())) return Val{};
            std::reverse(arrays[ai].begin(), arrays[ai].end());
            return self;
        }
        case MArrFill: {
            if (argc < 1) return fail(pos, "uso: fill(valor[, início, fim])");
            const f64 n = static_cast<f64>(size());
            const usize a = static_cast<usize>(argc >= 2 ? rel(args[1], 0.0, n) : 0.0);
            const usize b = static_cast<usize>(argc >= 3 ? rel(args[2], n, n) : n);
            if (!work(size())) return Val{};
            for (usize i = a; i < b && i < size(); ++i) arrays[ai][i] = args[0];
            return self;
        }
        case MArrAt: {
            f64 i = 0;
            if (argc != 1 || !want_num(args[0], i, pos, "índice")) return Val{};
            const f64 k = i < 0 ? static_cast<f64>(size()) + std::trunc(i) : std::trunc(i);
            if (k < 0 || k >= static_cast<f64>(size())) return Val{};
            return arrays[ai][static_cast<usize>(k)];
        }
        case MArrFlat: {
            const Val out = make_arr();
            if (!work(size())) return Val{};
            for (usize i = 0; i < size(); ++i) {
                const Val item = arrays[ai][i];
                if (item.k == K::Arr) {
                    const usize count = arrays[item.a].size();
                    if (!work(count)) return Val{};
                    for (usize k = 0; k < count; ++k) if (!arr_push(out.a, arrays[item.a][k])) return Val{};
                } else if (item.k == K::Vec) {
                    for (u32 k = 0; k < item.n; ++k) if (!arr_push(out.a, Val::num(item.v[k]))) return Val{};
                } else if (!arr_push(out.a, item)) {
                    return Val{};
                }
            }
            return out;
        }
        case MArrSort: {
            // Sem função: pela forma em TEXTO, como o JavaScript ([10, 9, 1] vira [1, 10, 9]);
            // com função (a, b) => a - b, pelo sinal dela. Merge sort estável próprio:
            // uma função de comparação torta não derruba nada.
            const bool hasFn = argc >= 1 && callable(args[0]);
            const usize n = size();
            if (n > 8192) return fail(pos, "sort: vetor grande demais (mais de 8192 itens)");
            if (!work(n)) return Val{};
            const std::vector<Val> a = arrays[ai];
            std::vector<std::string> keys;
            if (!hasFn) {
                keys.reserve(n);
                for (const Val& v : a) keys.push_back(to_text(v, 1));
            }
            std::vector<u32> idx(n), tmp(n);
            for (usize i = 0; i < n; ++i) idx[i] = static_cast<u32>(i);
            const Val fn = hasFn ? args[0] : Val{};
            auto before = [&](u32 x, u32 y) -> bool {   // a[x] vem antes de a[y]?
                if (!hasFn) return keys[x] < keys[y];
                if (failed || !tick(pos)) return false;
                Val cb[2] = {a[x], a[y]};
                const Val r = deref(invoke(fn, cb, 2, pos, depth), pos);
                return !failed && r.k == K::Num && r.v[0] < 0.0;
            };
            for (usize width = 1; width < n && !failed; width *= 2) {
                for (usize lo = 0; lo < n; lo += 2 * width) {
                    const usize mid = std::min(lo + width, n), hi = std::min(lo + 2 * width, n);
                    usize i = lo, j = mid, k = lo;
                    while (i < mid && j < hi) tmp[k++] = before(idx[j], idx[i]) ? idx[j++] : idx[i++];
                    while (i < mid) tmp[k++] = idx[i++];
                    while (j < hi) tmp[k++] = idx[j++];
                }
                idx.swap(tmp);
            }
            if (failed) return Val{};
            std::vector<Val> sorted(n);
            for (usize i = 0; i < n; ++i) sorted[i] = a[idx[i]];
            arrays[ai] = std::move(sorted);
            return self;
        }
        case MArrMap: case MArrFilter: case MArrForEach: case MArrSome: case MArrEvery: case MArrFind: case MArrFindIndex: {
            if (argc < 1 || !callable(args[0])) return fail(pos, "passe uma função: lista.map(x => x * 2)");
            const Val fn = args[0];
            Val out;
            if (id == MArrMap || id == MArrFilter) out = make_arr();
            for (usize i = 0; i < size(); ++i) {
                if (!tick(pos)) return Val{};
                const Val item = arrays[ai][i];
                Val cb[3] = {item, Val::num(static_cast<f64>(i)), self};
                const Val r = deref(invoke(fn, cb, fn.k == K::UFn ? 3u : 1u, pos, depth), pos);
                if (failed) return Val{};
                switch (id) {
                    case MArrMap: if (!arr_push(out.a, r)) return Val{}; break;
                    case MArrFilter: if (truthy(r) && !arr_push(out.a, item)) return Val{}; break;
                    case MArrSome: if (truthy(r)) return Val::num(1.0); break;
                    case MArrEvery: if (!truthy(r)) return Val::num(0.0); break;
                    case MArrFind: if (truthy(r)) return item; break;
                    case MArrFindIndex: if (truthy(r)) return Val::num(static_cast<f64>(i)); break;
                    default: break;
                }
            }
            switch (id) {
                case MArrMap: case MArrFilter: return out;
                case MArrSome: return Val::num(0.0);
                case MArrEvery: return Val::num(1.0);
                case MArrFindIndex: return Val::num(-1.0);
                default: return Val{};
            }
        }
        case MArrReduce: {
            if (argc < 1 || !callable(args[0])) return fail(pos, "passe uma função: lista.reduce((soma, x) => soma + x, 0)");
            const Val fn = args[0];
            usize i = 0;
            Val acc;
            if (argc >= 2) {
                acc = args[1];
            } else {
                if (arrays[ai].empty()) return fail(pos, "reduce num vetor vazio precisa do valor inicial");
                acc = arrays[ai][0];
                i = 1;
            }
            for (; i < size(); ++i) {
                if (!tick(pos)) return Val{};
                Val cb[4] = {acc, arrays[ai][i], Val::num(static_cast<f64>(i)), self};
                acc = deref(invoke(fn, cb, fn.k == K::UFn ? 4u : 2u, pos, depth), pos);
                if (failed) return Val{};
            }
            return acc;
        }
        default: return fail(pos, "método de vetor desconhecido");
    }
}

/// A forma de chamar grupo de propriedade do After Effects:
/// thisLayer("Transform")("Position"), layer("Masks")("Mask 1")("Mask Path"),
/// com o nome da interface em inglês ou o match name ("ADBE …").
Val Ctx::group_call(const Val& obj, const Val* args, u32 argc, u32 pos) {
    if (argc != 1) return fail(pos, "isto não é uma função");
    if (args[0].k == K::Num && obj.k == K::VGroup) return group_content(obj, args, argc, pos);
    if (args[0].k != K::Str) return fail(pos, "passe o nome do grupo ou da propriedade");
    const std::string nm(str(args[0]));
    auto is = [&](const char* a, const char* b) { return name_eq(nm, a) || name_eq(nm, b); };
    switch (obj.k) {
        case K::Layer:
            if (is("Transform", "ADBE Transform Group")) { Val r = obj; r.k = K::Transform; return r; }
            if (is("Effects", "ADBE Effect Parade")) return make_method(MLayerEffect, obj);
            if (is("Masks", "ADBE Mask Parade")) return make_method(MLayerMask, obj);
            if (is("Contents", "ADBE Root Vectors Group")) return make_method(MLayerContent, obj);
            if (is("Text", "ADBE Text Properties")) return member(obj, "text", pos);
            if (is("Marker", "ADBE Marker")) return member(obj, "marker", pos);
            break;
        case K::Transform: {
            static constexpr struct { const char* ui; const char* match; const char* member; } kT[] = {
                {"Anchor Point", "ADBE Anchor Point", "anchorPoint"}, {"Position", "ADBE Position", "position"},
                {"Scale", "ADBE Scale", "scale"}, {"Rotation", "ADBE Rotate Z", "rotation"},
                {"Z Rotation", "ADBE Rotate Z", "rotation"}, {"X Rotation", "ADBE Rotate X", "xRotation"},
                {"Y Rotation", "ADBE Rotate Y", "yRotation"}, {"Opacity", "ADBE Opacity", "opacity"},
            };
            for (const auto& t : kT) if (is(t.ui, t.match)) return member(obj, t.member, pos);
            break;
        }
        case K::Mask:
            if (is("Mask Path", "ADBE Mask Shape")) return member(obj, "maskPath", pos);
            if (is("Mask Feather", "ADBE Mask Feather")) return member(obj, "maskFeather", pos);
            if (is("Mask Opacity", "ADBE Mask Opacity")) return member(obj, "maskOpacity", pos);
            if (is("Mask Expansion", "ADBE Mask Offset")) return member(obj, "maskExpansion", pos);
            break;
        case K::VGroup:
            if (is("Contents", "ADBE Vectors Group")) return make_method(MGroupContent, obj);
            if (is("Path", "ADBE Vector Shape")) return member(obj, "path", pos);
            return group_content(obj, args, argc, pos);
        case K::PathProp:
            if (is("Path", "ADBE Vector Shape")) return obj;
            break;
        case K::Text:
            if (is("Source Text", "ADBE Text Document")) return member(obj, "sourceText", pos);
            break;
        default: break;
    }
    return fail(pos, "\"" + nm + "\" não existe aqui");
}

/// rgbToHsl([r, g, b, a]) / hslToRgb([h, s, l, a]) / hexToRgb("FF8800"): tudo
/// em 0..1, 4 componentes (alfa 1 quando não vem), como no After Effects.
Val Ctx::color_fn(u32 id, const Val* args, u32 argc, u32 pos) {
    if (id == FHexToRgb) {
        if (argc != 1) return fail(pos, "uso: hexToRgb(\"FF8800\")");
        std::string hex;
        if (args[0].k == K::Str) {
            hex = std::string(str(args[0]));
        } else if (args[0].k == K::Num && std::isfinite(args[0].v[0])) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%06X", static_cast<unsigned>(std::clamp(args[0].v[0], 0.0, 16777215.0)));
            hex = buf;
        } else {
            return fail(pos, "uso: hexToRgb(\"FF8800\")");
        }
        usize b = hex.find_first_not_of(" \t");
        usize e = hex.find_last_not_of(" \t");
        if (b == std::string::npos) return fail(pos, "hexToRgb: o texto está vazio");
        if (hex[b] == '#') ++b;
        else if (b + 1 <= e && hex[b] == '0' && (hex[b + 1] == 'x' || hex[b + 1] == 'X')) b += 2;
        const std::string h = b <= e ? hex.substr(b, e - b + 1) : std::string();
        for (const char c : h) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return fail(pos, "hexToRgb: \"" + hex + "\" não é uma cor em hexadecimal");
        }
        auto nib = [](char c) -> f64 { return static_cast<f64>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10); };
        f64 out[4] = {0.0, 0.0, 0.0, 1.0};
        if (h.size() == 3 || h.size() == 4) {
            for (usize i = 0; i < h.size(); ++i) out[i] = nib(h[i]) * 17.0 / 255.0;
        } else if (h.size() == 6 || h.size() == 8) {
            for (usize i = 0; i < h.size() / 2; ++i) out[i] = (nib(h[i * 2]) * 16.0 + nib(h[i * 2 + 1])) / 255.0;
        } else {
            return fail(pos, "hexToRgb: use 3, 4, 6 ou 8 dígitos (\"F80\", \"FF8800\", \"FF8800CC\")");
        }
        return Val::vec(out, 4);
    }
    if (argc != 1 || args[0].k != K::Vec || args[0].n < 3)
        return fail(pos, id == FRgbToHsl ? "uso: rgbToHsl([r, g, b, a])" : "uso: hslToRgb([h, s, l, a])");
    const f64* c = args[0].v;
    const f64 alpha = args[0].n > 3 ? c[3] : 1.0;
    if (id == FRgbToHsl) {
        const f64 r = c[0], g = c[1], b = c[2];
        const f64 mx = std::max({r, g, b}), mn = std::min({r, g, b});
        const f64 l = (mx + mn) * 0.5;
        f64 h = 0.0, s = 0.0;
        const f64 d = mx - mn;
        if (d > 1e-12) {
            const f64 den = l > 0.5 ? 2.0 - mx - mn : mx + mn;
            s = std::fabs(den) > 1e-12 ? d / den : 0.0;
            if (mx == r) h = (g - b) / d + (g < b ? 6.0 : 0.0);
            else if (mx == g) h = (b - r) / d + 2.0;
            else h = (r - g) / d + 4.0;
            h /= 6.0;
        }
        const f64 out[4] = {h, s, l, alpha};
        return Val::vec(out, 4);
    }
    const f64 h = c[0] - std::floor(c[0]), s = c[1], l = c[2];
    f64 out[4] = {l, l, l, alpha};
    if (std::fabs(s) > 1e-12) {
        const f64 q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
        const f64 pp = 2.0 * l - q;
        auto hue = [&](f64 t) {
            if (t < 0.0) t += 1.0;
            if (t > 1.0) t -= 1.0;
            if (t < 1.0 / 6.0) return pp + (q - pp) * 6.0 * t;
            if (t < 0.5) return q;
            if (t < 2.0 / 3.0) return pp + (q - pp) * (2.0 / 3.0 - t) * 6.0;
            return pp;
        };
        out[0] = hue(h + 1.0 / 3.0);
        out[1] = hue(h);
        out[2] = hue(h - 1.0 / 3.0);
    }
    return Val::vec(out, 4);
}

/// footage("nome") / layer.source: quem conhece os arquivos (o Engine) responde.
Val Ctx::footage_of(std::string_view name, u64 asset, u32 pos) {
    const std::string nm(name);
    std::vector<FootageEntry> providers;
    {
        std::lock_guard<std::mutex> g(g_providerMutex);
        providers = g_footageProviders;
    }
    for (const FootageEntry& pr : providers) {
        FootageInfo info;
        if (!pr.fn || !pr.fn(pr.ctx, env->tl, nm, asset, info)) continue;
        if (footages.size() >= 256) return fail(pos, "arquivos demais na expressão");
        footages.push_back(std::move(info));
        Val r;
        r.k = K::Footage;
        r.a = static_cast<u32>(footages.size() - 1);
        return r;
    }
    return fail(pos, nm.empty() ? std::string("o arquivo de origem da camada não foi encontrado")
                                : "arquivo \"" + nm + "\" não existe no projeto");
}

/// O número (1..) da composição de um valor na ordem da timeline (o `comp` de
/// publish_layer_image / ImageRequest).
u32 Ctx::comp_key(const Val& v) const {
    if (v.v[3] > 0.0) return static_cast<u32>(v.v[3]);
    u32 i = 0, found = 0;
    if (env->tl) env->tl->for_each_composition([&](CompositionId, const Composition& c) { ++i; if (&c == env->comp) found = i; });
    return found;
}

/// mask("Mask 1") / mask(1): a máscara da camada (1 = a primeira da pilha).
Val Ctx::layer_mask(const Val& layerVal, const Val* args, u32 argc, u32 pos) {
    const Layer* l = layer_of(layerVal);
    if (!l) return fail(pos, "a camada não existe mais");
    if (argc != 1) return fail(pos, "mask() recebe um nome ou um índice");
    const Mask* found = nullptr;
    if (args[0].k == K::Num) {
        const i64 i = static_cast<i64>(std::floor(args[0].v[0] + 0.5));
        if (i >= 1 && i <= static_cast<i64>(l->masks.size())) found = &l->masks[static_cast<usize>(i - 1)];
        if (!found) return fail(pos, "mask(" + std::to_string(i) + "): a camada \"" + l->name + "\" tem " + std::to_string(l->masks.size()) + " máscara(s)");
    } else if (args[0].k == K::Str) {
        const std::string_view nm = str(args[0]);
        for (const Mask& m : l->masks) if (!found && m.name == nm) found = &m;
        for (const Mask& m : l->masks) if (!found && name_eq(m.name, nm)) found = &m;
        if (!found) return fail(pos, "máscara \"" + std::string(nm) + "\" não existe na camada \"" + l->name + "\"");
    } else {
        return fail(pos, "mask() recebe um nome ou um índice");
    }
    Val r = layerVal;
    r.k = K::Mask;
    r.id = found->id;
    return r;
}

/// content("Grupo") / content(1): o grupo da camada de forma vetorial (1 = o
/// de cima, como no After Effects).
Val Ctx::layer_content(const Val& layerVal, const Val* args, u32 argc, u32 pos) {
    const Layer* l = layer_of(layerVal);
    if (!l) return fail(pos, "a camada não existe mais");
    if (argc != 1) return fail(pos, "content() recebe um nome ou um índice");
    if (l->kind != LayerKind::Shape || l->shape.shapeType != kShapeVector)
        return fail(pos, "a camada \"" + l->name + "\" não é uma forma vetorial (content só existe nela)");
    const std::vector<VectorGroup>& groups = l->shape.vector.groups;
    u32 found = kInvalidIndex;
    if (args[0].k == K::Num) {
        const i64 i = static_cast<i64>(std::floor(args[0].v[0] + 0.5));
        if (i >= 1 && i <= static_cast<i64>(groups.size())) found = static_cast<u32>(groups.size() - static_cast<usize>(i));
        if (found == kInvalidIndex) return fail(pos, "content(" + std::to_string(i) + "): a forma tem " + std::to_string(groups.size()) + " grupo(s)");
    } else if (args[0].k == K::Str) {
        const std::string_view nm = str(args[0]);
        for (u32 i = static_cast<u32>(groups.size()); i-- > 0;) if (found == kInvalidIndex && groups[i].name == nm) found = i;
        for (u32 i = static_cast<u32>(groups.size()); i-- > 0;) if (found == kInvalidIndex && name_eq(groups[i].name, nm)) found = i;
        if (found == kInvalidIndex) return fail(pos, "grupo \"" + std::string(nm) + "\" não existe na forma \"" + l->name + "\"");
    } else {
        return fail(pos, "content() recebe um nome ou um índice");
    }
    Val r = layerVal;
    r.k = K::VGroup;
    r.id = found;
    return r;
}

/// content("Grupo").content("Path 1") / .content(1): um caminho do grupo. Os
/// caminhos do Aurea não têm nome: vale o número ("Path 2", "Caminho 2" → o 2º).
Val Ctx::group_content(const Val& group, const Val* args, u32 argc, u32 pos) {
    const Layer* l = layer_of(group);
    if (!l || l->kind != LayerKind::Shape || group.id >= l->shape.vector.groups.size()) return fail(pos, "o grupo não existe mais");
    const VectorGroup& g = l->shape.vector.groups[group.id];
    if (argc != 1) return fail(pos, "content() recebe um nome ou um índice");
    i64 index = 0;
    if (args[0].k == K::Num) {
        index = static_cast<i64>(std::floor(args[0].v[0] + 0.5));
    } else if (args[0].k == K::Str) {
        const std::string_view nm = str(args[0]);
        usize digits = nm.size();
        while (digits > 0 && nm[digits - 1] >= '0' && nm[digits - 1] <= '9') --digits;
        index = 1;
        if (digits < nm.size()) {
            index = 0;
            for (usize k = digits; k < nm.size() && index < 100000; ++k) index = index * 10 + (nm[k] - '0');
        }
    } else {
        return fail(pos, "content() recebe um nome ou um índice");
    }
    if (index < 1 || index > static_cast<i64>(g.paths.size()))
        return fail(pos, "o grupo \"" + g.name + "\" tem " + std::to_string(g.paths.size()) + " caminho(s)");
    PathRef ref;
    ref.owner = group;
    ref.owner.k = K::Layer;
    ref.owner.id = 0;
    ref.kind = 1;
    ref.a = group.id;
    ref.b = static_cast<u32>(index - 1);
    pathRefs.push_back(ref);
    Val r;
    r.k = K::PathProp;
    r.a = static_cast<u32>(pathRefs.size() - 1);
    return r;
}

/// O caminho bezier de um valor no instante `seconds` (tempo da composição).
bool Ctx::resolve_path(const Val& v, f64 seconds, BezierPath& out, u32 pos) {
    if (v.k == K::Path) {
        if (v.a >= pathsUsed) { fail(pos, "caminho inválido"); return false; }
        out = paths[v.a];
        return true;
    }
    if (v.k != K::PathProp || v.a >= pathRefs.size()) {
        fail(pos, "isto não é um caminho (use mask(\"x\").maskPath, content(\"Grupo\").path ou createPath)");
        return false;
    }
    if (!std::isfinite(seconds) || std::fabs(seconds) > 1.0e9) { fail(pos, "tempo inválido"); return false; }
    const PathRef ref = pathRefs[v.a];
    auto from_points = [&](const std::vector<MaskPoint>& pts, bool closed) {
        out.v.clear();
        out.closed = closed;
        out.v.reserve(pts.size());
        for (const MaskPoint& q : pts) out.v.push_back(BezierVertex{q.position, q.inTangent, q.outTangent});
    };
    if (ref.kind == 2) {
        // O caminho de antes da expressão: no instante dela é o que entrou; noutro, o dos keys.
        if (!inPath || !env->maskSelf) { fail(pos, "sem caminho aqui"); return false; }
        if (std::fabs(seconds * env->fps - env->compF) > 1e-6) {
            std::vector<MaskPoint> pts;
            mask::evaluate_path_keys(*env->maskSelf, layer_local_from_comp(env->layer, seconds * env->fps), pts);
            from_points(pts, env->maskSelf->closed);
        } else {
            from_points(*inPath, inPathClosed);
        }
        return true;
    }
    const Layer* l = layer_of(ref.owner);
    const Composition* c = comp_of(ref.owner);
    if (!l || !c) { fail(pos, "a camada não existe mais"); return false; }
    const f64 cf = c->fps() > 0.0 ? c->fps() : 30.0;
    const f64 local = layer_local_from_comp(l, seconds * cf);
    if (ref.kind == 0) {
        const Mask* m = nullptr;
        for (const Mask& x : l->masks) if (x.id == ref.a) m = &x;
        if (!m) { fail(pos, "a máscara não existe mais"); return false; }
        std::vector<MaskPoint> pts;
        if (m == env->maskSelf) mask::evaluate_path_keys(*m, local, pts);   // a própria: antes da expressão
        else mask::evaluate_path(*m, local, pts);
        if (g_tls.cycle) { fail(pos, g_tls.cycleMsg.empty() ? "dependência circular" : g_tls.cycleMsg); return false; }
        from_points(pts, m->closed);
        return true;
    }
    if (l->kind != LayerKind::Shape || ref.a >= l->shape.vector.groups.size()
        || ref.b >= l->shape.vector.groups[ref.a].paths.size()) {
        fail(pos, "o caminho não existe mais");
        return false;
    }
    out = vector::path_at(l->shape.vector.groups[ref.a].paths[ref.b], local);
    return true;
}

/// points(t) / inTangents(t) / outTangents(t) / isClosed() /
/// pointOnPath(porcentagem = 0.5, t) / tangentOnPath / normalOnPath.
/// Pontos em px da camada (máscara) ou do grupo (forma); tangentes relativas
/// ao ponto; a porcentagem anda pelo COMPRIMENTO do caminho (0..1).
Val Ctx::path_method(u32 id, const Val& self, const Val* args, u32 argc, u32 pos) {
    const bool sampled = id == MPathPointOn || id == MPathTangentOn || id == MPathNormalOn;
    f64 pct = 0.5, t = nowF / env->fps;
    const u32 ti = sampled ? 1u : 0u;
    if (argc > ti + 1) return fail(pos, sampled ? "uso: pointOnPath(porcentagem[, t])" : "uso: points([t])");
    if (sampled && argc >= 1 && !want_num(args[0], pct, pos, "porcentagem")) return Val{};
    if (argc > ti && !want_num(args[ti], t, pos, "t")) return Val{};
    BezierPath path;
    if (!resolve_path(self, t, path, pos)) return Val{};
    switch (id) {
        case MPathIsClosed: return Val::num(path.closed ? 1.0 : 0.0);
        case MPathPoints: case MPathInTangents: case MPathOutTangents: {
            const Val out = make_arr();
            for (const BezierVertex& bv : path.v) {
                const Vec2 q = id == MPathPoints ? bv.p : id == MPathInTangents ? bv.in : bv.out;
                const f64 xy[2] = {q.x, q.y};
                Val item = Val::vec(xy, 2);
                item.k = K::Vec;
                if (!arr_push(out.a, item)) return Val{};
            }
            return out;
        }
        default: {
            if (path.v.empty()) return fail(pos, "o caminho está vazio");
            steps += static_cast<u32>(path.v.size());   // achatar custa pelo número de pontos
            if (!tick(pos)) return Val{};
            vector::Contour c;
            vector::flatten(path, 0.1f, c);
            c.closed = path.closed;
            const f64 len = c.pts.size() >= 2 ? vector::length_of(c) : 0.0;
            Vec2 pt = path.v[0].p, tan{1.0f, 0.0f};
            if (len > 1e-9) {
                pct = std::isnan(pct) ? 0.0 : std::clamp(pct, 0.0, 1.0);
                (void)vector::sample_at(c, pct * len, pt, tan);
            }
            const f64 point[2] = {pt.x, pt.y};
            const f64 tangent[2] = {tan.x, tan.y};
            // Normal: perpendicular à tangente, apontando para FORA num caminho no
            // sentido horário da tela (em cima de um quadrado horário: para cima).
            // Sentido inferido do uso comum no AE (ponto + normal · d afasta do
            // caminho horário); não conferido contra o After Effects.
            const f64 normal[2] = {tan.y, -tan.x};
            Val r = Val::vec(id == MPathPointOn ? point : id == MPathTangentOn ? tangent : normal, 2);
            r.k = K::Vec;
            return r;
        }
    }
}

/// createPath(pontos, tangentesDeEntrada = [], tangentesDeSaída = [], fechado = true)
Val Ctx::create_path(const Val* args, u32 argc, u32 pos) {
    if (argc > 4) return fail(pos, "uso: createPath(pontos, tangentesDeEntrada, tangentesDeSaída, fechado)");
    BezierPath path;
    path.closed = argc >= 4 ? truthy(args[3]) : true;
    auto read = [&](const Val& list, u32 which, const char* what) -> bool {
        if (list.k == K::Undef || list.k == K::Null) return which != 0;
        if (list.k != K::Arr) {
            fail(pos, std::string("createPath: ") + what + " precisa ser uma lista de pontos [[x, y], [x, y], …]");
            return false;
        }
        const usize count = arrays[list.a].size();
        if (which == 0) {
            if (count == 0 || count > 4096) { fail(pos, "createPath: use de 1 a 4096 pontos"); return false; }
            path.v.resize(count);
        } else if (count != 0 && count != path.v.size()) {
            fail(pos, std::string("createPath: ") + what + " precisa ter um item por ponto (ou ser [])");
            return false;
        }
        for (usize i = 0; i < count; ++i) {
            const Val item = norm(arrays[list.a][i]);
            if (item.k != K::Vec || item.n < 2 || !std::isfinite(item.v[0]) || !std::isfinite(item.v[1])) {
                fail(pos, std::string("createPath: ") + what + "[" + std::to_string(i) + "] precisa ser [x, y]");
                return false;
            }
            const Vec2 q{static_cast<f32>(item.v[0]), static_cast<f32>(item.v[1])};
            if (which == 0) path.v[i].p = q;
            else if (which == 1) path.v[i].in = q;
            else path.v[i].out = q;
        }
        return true;
    };
    if (argc == 0) {
        // O padrão do After Effects: o quadrado de 100 px.
        path.v = {BezierVertex{Vec2{0.0f, 0.0f}, {}, {}}, BezierVertex{Vec2{100.0f, 0.0f}, {}, {}},
                  BezierVertex{Vec2{100.0f, 100.0f}, {}, {}}, BezierVertex{Vec2{0.0f, 100.0f}, {}, {}}};
    } else if (!read(args[0], 0, "pontos")) {
        if (!failed) fail(pos, "createPath: faltam os pontos");
        return Val{};
    }
    if (argc >= 2 && !read(args[1], 1, "tangentes de entrada")) return Val{};
    if (argc >= 3 && !read(args[2], 2, "tangentes de saída")) return Val{};
    return make_path(std::move(path));
}

/// sampleImage(ponto, raio = [0.5, 0.5], depoisDosEfeitos = true, t = time):
/// [r, g, b, a] em 0..1, média ponderada pelo alfa da caixa de ±raio em volta
/// do ponto (px da camada). A avaliação não lê a GPU; ver Expression.hpp para
/// de onde vêm os pixels e o atraso de um quadro.
Val Ctx::sample_image(const Val& layerVal, const Val* args, u32 argc, u32 pos) {
    const Layer* l = layer_of(layerVal);
    const Composition* c = comp_of(layerVal);
    if (!l || !c) return fail(pos, "a camada não existe mais");
    if (argc < 1 || argc > 4) return fail(pos, "uso: sampleImage(ponto[, raio, depoisDosEfeitos, t])");
    if (args[0].k != K::Vec || args[0].n < 2) return fail(pos, "sampleImage: o ponto precisa ser [x, y]");
    f64 rx = 0.5, ry = 0.5;
    if (argc >= 2) {
        if (args[1].k == K::Num) rx = ry = args[1].v[0];
        else if (args[1].k == K::Vec && args[1].n >= 2) { rx = args[1].v[0]; ry = args[1].v[1]; }
        else return fail(pos, "sampleImage: o raio precisa ser um número ou [x, y]");
    }
    const bool post = argc >= 3 ? truthy(args[2]) : true;
    f64 t = nowF / env->fps;
    if (argc >= 4 && !want_num(args[3], t, pos, "t")) return Val{};
    const f64 px = args[0].v[0], py = args[0].v[1];
    if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(t))
        return fail(pos, "sampleImage: valor inválido");
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const f64 cf = c->fps() > 0.0 ? c->fps() : 30.0;
    const f64 compFrame = t * cf;
    const f64 local = layer_local_from_comp(l, compFrame);
    const ImageKey key{env->tl, comp_key(layerVal), LayerId{layerVal.a, layerVal.b}.pack(), post};
    std::shared_ptr<const LayerPixels> img;
    std::vector<SourceEntry> sources;
    {
        std::lock_guard<std::mutex> g(g_imageMutex);
        auto it = g_images.find(key);
        if (it == g_images.end()) { ImageKey other = key; other.post = !post; it = g_images.find(other); }
        if (it != g_images.end()) {
            img = it->second;
        } else {
            bool queued = false;
            for (const ImageRequest& r : g_imageRequests) queued |= r.timeline == key.tl && r.comp == key.comp && r.layer == key.layer && r.postEffect == post;
            if (!queued && g_imageRequests.size() < 256) g_imageRequests.push_back(ImageRequest{key.tl, key.comp, key.layer, post, compFrame});
            sources = g_imageSources;
        }
    }
    // Os pixels da FONTE (sem efeitos), de quem os tem prontos na CPU.
    for (usize i = 0; !img && i < sources.size(); ++i) {
        if (!sources[i].fn) continue;
        std::shared_ptr<const LayerPixels> pixels = sources[i].fn(sources[i].ctx, env->tl, *l, local);
        if (!pixels || !pixels->width || !pixels->height
            || pixels->rgba.size() < static_cast<usize>(pixels->width) * pixels->height * 4) continue;
        img = std::move(pixels);
    }
    f64 out[4]{};
    if (img && img->layerWidth > 0.0f && img->layerHeight > 0.0f) {
        const f64 sx = static_cast<f64>(img->width) / img->layerWidth, sy = static_cast<f64>(img->height) / img->layerHeight;
        f64 x0 = (px - rx) * sx, x1 = (px + rx) * sx, y0 = (py - ry) * sy, y1 = (py + ry) * sy;
        // Caixa menor que um texel da imagem pequena: o texel sob o ponto.
        if (x1 - x0 < 1e-6) { x0 = std::floor(px * sx); x1 = x0 + 1.0; }
        if (y1 - y0 < 1e-6) { y0 = std::floor(py * sy); y1 = y0 + 1.0; }
        const f64 area = (x1 - x0) * (y1 - y0);
        const i64 ix0 = std::max<i64>(0, static_cast<i64>(std::floor(x0))), ix1 = std::min<i64>(img->width, static_cast<i64>(std::ceil(x1)));
        const i64 iy0 = std::max<i64>(0, static_cast<i64>(std::floor(y0))), iy1 = std::min<i64>(img->height, static_cast<i64>(std::ceil(y1)));
        f64 sumA = 0.0, sum[3]{};
        for (i64 y = iy0; y < iy1; ++y) {
            const f64 wy = std::min<f64>(y1, static_cast<f64>(y + 1)) - std::max<f64>(y0, static_cast<f64>(y));
            if (wy <= 0.0) continue;
            for (i64 x = ix0; x < ix1; ++x) {
                const f64 wx = std::min<f64>(x1, static_cast<f64>(x + 1)) - std::max<f64>(x0, static_cast<f64>(x));
                if (wx <= 0.0) continue;
                const u8* q = &img->rgba[(static_cast<usize>(y) * img->width + static_cast<usize>(x)) * 4];
                const f64 a = q[3] / 255.0 * wx * wy;
                sumA += a;
                for (u32 k = 0; k < 3; ++k) sum[k] += a * q[k] / 255.0;
            }
        }
        if (sumA > 1e-12) for (u32 k = 0; k < 3; ++k) out[k] = sum[k] / sumA;
        out[3] = area > 0.0 ? std::clamp(sumA / area, 0.0, 1.0) : 0.0;
    } else if (l->kind == LayerKind::Shape && l->shape.shapeType != kShapeVector && l->shape.filled) {
        // Sem imagem: a cor chapada que o modelo conhece (sólido/forma), dentro da caixa da forma.
        const Rect& b = l->shape.bounds;
        if (px >= b.x && px <= b.x + b.w && py >= b.y && py <= b.y + b.h) {
            out[0] = l->shape.fillColor.x; out[1] = l->shape.fillColor.y; out[2] = l->shape.fillColor.z; out[3] = l->shape.fillColor.w;
        }
    }
    return Val::vec(out, 4);
}

/// Executa o programa. `out` recebe o valor final (já sem propriedade).
/// `text` (v70, texto-fonte): o resultado pode ser texto (fica válido no `ctx`
/// até o próximo run) e número não finito vira "NaN"/"Infinity", como no AE.
/// `mode`: 0 número/vetor (trilha), 1 texto (v70, texto-fonte), 2 caminho (AE
/// expressions: createPath ou um caminho lido).
bool run(Ctx& ctx, const Program& prog, const Env& env, Val& out, u32 mode = 0) {
    const bool text = mode == 1;
    ctx.reset(&prog, &env);
    Val v = ctx.exec(prog.root, 0);
    if (!ctx.failed && ctx.flow == Flow::Return) v = ctx.ret;
    if (!ctx.failed) v = ctx.norm(ctx.deref(v, 0));
    if (ctx.failed) return false;
    if (mode == 2) {
        if (v.k == K::Path || v.k == K::PathProp) { out = v; return true; }
        ctx.fail(prog.source.empty() ? 0 : static_cast<u32>(prog.source.size() - 1),
                 "a expressão do caminho precisa devolver um caminho: createPath(pontos, …) ou outro caminho");
        return false;
    }
    if (text && (v.k == K::Str || v.k == K::Num || v.k == K::Vec || v.k == K::Arr)) { out = v; return true; }
    if (v.k == K::Arr && ctx.arrays[v.a].size() > 4) {
        ctx.fail(prog.source.empty() ? 0 : static_cast<u32>(prog.source.size() - 1),
                 "o resultado é um vetor de " + std::to_string(ctx.arrays[v.a].size()) + " itens; a propriedade aceita de 1 a 4 números");
        return false;
    }
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
    if (!s.track) return n + ".texto-fonte";   // v70: a expressão do texto não mora numa trilha
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
    // O registro não se copia nem se move (posições fixas, leitura sem lock).
    static const EffectRegistry& reg = *[] {
        auto* r = new EffectRegistry();
        register_builtin_effects(*r);
        return r;
    }();
    return reg;
}

TextScope::TextScope(u32 index, u32 total, const Vec3* selector) noexcept : previous_(g_textContext), previousSelector_(g_textSelector) {
    g_textSelector = selector ? *selector : Vec3{100.0f, 100.0f, 100.0f};
    g_textContext = static_cast<u64>(index) | (static_cast<u64>(total) << 32);
}
TextScope::~TextScope() { g_textContext = previous_; g_textSelector = previousSelector_; }

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

// Lote M (timeline/PathRig): as timelines que esta thread enxerga agora, pela
// mesma regra das expressões — a do Scope de cima primeiro, depois as vivas.
u32 live_timelines(const Timeline** out, u32 capacity) noexcept {
    u32 n = 0;
    if (!out || capacity == 0) return 0;
    if (!g_tls.scopes.empty() && g_tls.scopes.back()->tl) out[n++] = g_tls.scopes.back()->tl;
    std::vector<Provider> providers;
    {
        std::lock_guard<std::mutex> g(g_providerMutex);
        providers = g_providers;
    }
    for (usize i = 0; i < providers.size() && n < capacity; ++i) {
        const Timeline* tl = providers[i].fn ? providers[i].fn(providers[i].ctx) : nullptr;
        if (!tl) continue;
        bool seen = false;
        for (u32 k = 0; k < n; ++k) seen = seen || out[k] == tl;
        if (!seen) out[n++] = tl;
    }
    return n;
}

f32 evaluate_track(const Track& track, FrameIndex t, const f32* fallback) noexcept {
    return evaluate_track_f(track, static_cast<f64>(t.value), fallback);
}

f32 evaluate_track_f(const Track& track, f64 t, const f32* fallback) noexcept {
    if (!std::isfinite(t)) return fallback ? *fallback : track.staticValue;
    const TrackExpression* te = track.expression.get();
    auto raw = [&]() -> f32 {
        return track.keys.empty() ? (fallback ? *fallback : track.staticValue) : track.sample_keys_f(t);
    };
    // Beyond the exact-integer range of a double there is no subframe clock.
    // Do not send hostile valueAtTime inputs into integer noise/random seeds.
    constexpr f64 maxFrame = 9007199254740991.0;
    if (std::fabs(t) > maxFrame) return raw();
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
        const auto it = sd->memo.find(MemoKey{&track, t, g_textContext});
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
    env.tl = sd ? sd->tl : nullptr;
    env.self = desc_for_track(owner.layer, owner.id, track, fps, env.component);
    env.localF = t;
    env.compF = env.localF + static_cast<f64>(owner.layer->start.value) - static_cast<f64>(owner.layer->offset.value);
    if (!std::isfinite(env.compF) || std::fabs(env.compF) > maxFrame) return done(raw());
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
    const f32 rawNative = static_cast<f32>((rawDisplay - env.self.offset) / env.self.scale);

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
    const f32 v = static_cast<f32>((display - env.self.offset) / env.self.scale);
    te->clear_runtime();
    if (sd && sd->memo.size() < 262144) sd->memo.emplace(MemoKey{&track, t, g_textContext}, v);
    return done(v);
}

// --- AE expressions (lote E): caminho da máscara, arquivos e sampleImage --------
bool apply_mask_path_expression(const Mask& mask, f64 localFrame, std::vector<MaskPoint>& points, bool* closedOut) noexcept {
    const TrackExpression* te = mask.pathExpression.get();
    if (!te || !mask.pathExpressionEnabled || !te->program) return false;
    constexpr f64 maxFrame = 9007199254740991.0;
    if (!std::isfinite(localFrame) || std::fabs(localFrame) > maxFrame) return false;
    Tls& tls = g_tls;
    if (tls.depth >= kMaxRefDepth) return false;
    for (const Mask* m : g_maskEval) {
        if (m != &mask) continue;
        // A máscara lê outra que lê esta de volta: vale o caminho de antes.
        if (!tls.cycle) { tls.cycle = true; tls.cycleMsg = "dependência circular entre caminhos de máscara"; }
        return false;
    }
    ScopeData* sd = tls.scopes.empty() ? nullptr : tls.scopes.back();
    Owner owner = sd ? sd->find_mask(&mask) : Owner{};
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
            owner = tmp->find_mask(&mask);
            if (owner.layer) { tls.scopes.push_back(tmp); sd = tmp; tempScope = true; break; }
            tls.release();
        }
    }
    struct PopScope {
        Tls& tls; bool on;
        ~PopScope() { if (on) { tls.scopes.pop_back(); tls.release(); } }
    } popScope{tls, tempScope};
    if (!owner.layer) {
        te->report_runtime(make_diag(te->source, 0, "a expressão está numa máscara fora de uma camada da timeline"));
        return false;
    }
    Env env;
    env.comp = owner.comp;
    env.layer = owner.layer;
    env.layerId = owner.id;
    env.fps = owner.comp && owner.comp->fps() > 0.0 ? owner.comp->fps() : 30.0;
    env.tl = sd ? sd->tl : nullptr;
    env.self.count = 1;
    env.self.kind = 2;                  // sem trilha: numKeys/key() não se aplicam ao caminho
    env.value = Val::num(0.0);
    env.maskSelf = &mask;
    env.localF = localFrame;
    env.compF = localFrame + static_cast<f64>(owner.layer->start.value) - static_cast<f64>(owner.layer->offset.value);
    if (!std::isfinite(env.compF) || std::fabs(env.compF) > maxFrame) return false;
    env.seedBase = hash_mix(owner.id.index * 2654435761u, 0x9A7B0000u ^ mask.id);
    const u32 myDepth = tls.depth;
    Ctx& ctx = ctx_for_depth(myDepth);
    tls.stack[myDepth] = StackEntry{nullptr, owner};
    tls.depth = myDepth + 1;
    g_maskEval.push_back(&mask);
    ctx.inPath = &points;
    ctx.inPathClosed = mask.closed;
    Val out;
    bool ok = run(ctx, *te->program, env, out, 2);
    BezierPath path;
    if (ok) ok = ctx.resolve_path(out, env.compF / env.fps, path, 0);
    ctx.inPath = nullptr;
    g_maskEval.pop_back();
    tls.depth = myDepth;
    const bool cycled = tls.cycle;
    std::string cycleMsg = cycled ? tls.cycleMsg : std::string();
    if (myDepth == 0) { tls.cycle = false; tls.cycleMsg.clear(); }
    if (!ok || cycled) {
        te->report_runtime(ok ? make_diag(te->source, 0, cycleMsg) : make_diag(te->source, ctx.errPos, ctx.err));
        return false;
    }
    if (path.v.empty() || path.v.size() > 4096) {
        te->report_runtime(make_diag(te->source, 0, "o caminho precisa ter de 1 a 4096 pontos"));
        return false;
    }
    std::vector<MaskPoint> result(path.v.size());
    for (usize i = 0; i < result.size(); ++i) {
        const BezierVertex& bv = path.v[i];
        if (!std::isfinite(bv.p.x) || !std::isfinite(bv.p.y) || !std::isfinite(bv.in.x) || !std::isfinite(bv.in.y)
            || !std::isfinite(bv.out.x) || !std::isfinite(bv.out.y)) {
            te->report_runtime(make_diag(te->source, 0, "o caminho tem um ponto que não é um número finito"));
            return false;
        }
        result[i].position = bv.p;
        result[i].inTangent = bv.in;
        result[i].outTangent = bv.out;
    }
    te->clear_runtime();
    if (closedOut) *closedOut = path.closed;
    points = std::move(result);
    return true;
}

bool is_menu_control(u32 effectType) noexcept { return is_dropdown_control(effectType); }

std::vector<std::string> menu_items(const EffectInstance& effect) {
    if (!effect.menuItems.empty()) return effect.menuItems;
    return {"Item 1", "Item 2", "Item 3"};
}

void register_footage_provider(FootageProvider fn, void* ctx) {
    std::lock_guard<std::mutex> g(g_providerMutex);
    g_footageProviders.push_back(FootageEntry{fn, ctx});
}

void unregister_footage_provider(void* ctx) {
    std::lock_guard<std::mutex> g(g_providerMutex);
    std::erase_if(g_footageProviders, [ctx](const FootageEntry& p) { return p.ctx == ctx; });
}

void publish_layer_image(const Timeline* timeline, u32 compKey, u64 layerKey, bool postEffect, LayerPixels pixels) {
    if (!pixels.width || !pixels.height || pixels.rgba.size() < static_cast<usize>(pixels.width) * pixels.height * 4) return;
    auto shared = std::make_shared<const LayerPixels>(std::move(pixels));
    std::lock_guard<std::mutex> g(g_imageMutex);
    if (g_images.size() >= 256) g_images.clear();
    g_images[ImageKey{timeline, compKey, layerKey, postEffect}] = std::move(shared);
}

void clear_layer_images(const Timeline* timeline) noexcept {
    std::lock_guard<std::mutex> g(g_imageMutex);
    std::erase_if(g_images, [timeline](const auto& kv) { return !timeline || kv.first.tl == timeline; });
    std::erase_if(g_imageRequests, [timeline](const ImageRequest& r) { return !timeline || r.timeline == timeline; });
}

u32 take_image_requests(std::vector<ImageRequest>& out) {
    std::lock_guard<std::mutex> g(g_imageMutex);
    const u32 n = static_cast<u32>(g_imageRequests.size());
    out.insert(out.end(), g_imageRequests.begin(), g_imageRequests.end());
    g_imageRequests.clear();
    return n;
}

void register_image_source(ImageSource fn, void* ctx) {
    std::lock_guard<std::mutex> g(g_imageMutex);
    g_imageSources.push_back(SourceEntry{fn, ctx});
}

void unregister_image_source(void* ctx) {
    std::lock_guard<std::mutex> g(g_imageMutex);
    std::erase_if(g_imageSources, [ctx](const SourceEntry& s) { return s.ctx == ctx; });
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

bool apply_source_expression(const Layer& layer, f64 localFrame, TextData& t) noexcept {
    const TextData& own = layer.text;
    if (!own.sourceExpression || !own.sourceExpressionEnabled || !own.sourceExpression->program) return false;
    TextResult r = evaluate_text(*own.sourceExpression, layer, t.content, localFrame);
    if (!r.ok || r.text == t.content) return false;
    t.content = std::move(r.text);
    t.spans.clear();   // os trechos estilizados apontavam para o texto de antes
    return true;
}

TextResult evaluate_text(const TrackExpression& te, const Layer& layer, std::string_view value, f64 localFrame) noexcept {
    TextResult r;
    r.text = std::string(value);
    if (!te.program) { r.diag = te.parseError; return r; }
    constexpr f64 maxFrame = 9007199254740991.0;
    if (!std::isfinite(localFrame) || std::fabs(localFrame) > maxFrame) return r;
    Tls& tls = g_tls;
    if (tls.depth >= kMaxRefDepth) return r;
    // A camada dona: o escopo do quadro ou quem se registrou (como a trilha).
    ScopeData* sd = tls.scopes.empty() ? nullptr : tls.scopes.back();
    Owner owner = sd ? sd->find_layer(&layer) : Owner{};
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
            owner = tmp->find_layer(&layer);
            if (owner.layer) { tls.scopes.push_back(tmp); sd = tmp; tempScope = true; break; }
            tls.release();
        }
    }
    struct PopScope {
        Tls& tls; bool on;
        ~PopScope() { if (on) { tls.scopes.pop_back(); tls.release(); } }
    } popScope{tls, tempScope};
    if (!owner.layer) {
        r.diag = make_diag(te.source, 0, "a expressão está num texto fora de uma camada da timeline");
        te.report_runtime(r.diag);
        return r;
    }
    const std::string text(value);
    Env env;
    env.comp = owner.comp;
    env.layer = owner.layer;
    env.layerId = owner.id;
    env.fps = owner.comp && owner.comp->fps() > 0.0 ? owner.comp->fps() : 30.0;
    env.tl = sd ? sd->tl : nullptr;
    env.self.count = 1;
    env.self.kind = 2;                  // sem trilha: numKeys/key() não se aplicam ao texto
    env.value = Val::num(0.0);
    env.textValue = &text;
    env.localF = localFrame;
    env.compF = localFrame + static_cast<f64>(owner.layer->start.value) - static_cast<f64>(owner.layer->offset.value);
    env.seedBase = hash_mix(owner.id.index * 2654435761u, 0x7E57u);
    const u32 myDepth = tls.depth;
    Ctx& ctx = ctx_for_depth(myDepth);
    tls.stack[myDepth] = StackEntry{nullptr, owner};
    tls.depth = myDepth + 1;
    Val out;
    const bool ok = run(ctx, *te.program, env, out, 1);
    tls.depth = myDepth;
    const bool cycled = tls.cycle;
    std::string cycleMsg = cycled ? tls.cycleMsg : std::string();
    if (myDepth == 0) { tls.cycle = false; tls.cycleMsg.clear(); }
    if (!ok || cycled) {
        r.diag = ok ? make_diag(te.source, 0, cycleMsg) : make_diag(te.source, ctx.errPos, ctx.err);
        te.report_runtime(r.diag);
        return r;
    }
    std::string shown = ctx.to_text(out);
    if (shown.size() > 64 * 1024) {
        r.diag = make_diag(te.source, 0, "texto longo demais (mais de 64 KB)");
        te.report_runtime(r.diag);
        return r;
    }
    te.clear_runtime();
    r.ok = true;
    r.text = std::move(shown);
    return r;
}

} // namespace aurea::expr
