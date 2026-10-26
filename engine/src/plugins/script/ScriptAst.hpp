// =============================================================================
//  0.3.3 plugins: a árvore do AureaScript (léxico + sintaxe em ScriptParser.cpp,
//  avaliação em ScriptInterp.cpp).
// =============================================================================
#pragma once

#include "aurea/plugins/Script.hpp"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace aurea::plugins::script {

enum class N : u8 {
    // Expressões
    Num, Str, Template, True, False, Null, Undef, Ident, This,
    Array, Object, Func, Unary, Update, Binary, Logical, Cond, Assign,
    Member, Index, Call, New, Seq, Spread, Hole, OptChain,
    Prop,                       ///< chave → valor de um literal de objeto
    // Padrões (declarações e parâmetros)
    PatIdent, PatObject, PatArray, PatRest, PatProp,
    // Comandos
    Var, Decl, ExprStmt, Block, If, For, ForOf, ForIn, While, DoWhile,
    Break, Continue, Return, Throw, Try, Switch, Case, FuncDecl, Empty, Program,
};

enum Op : u16 {
    OpNone = 0,
    // binários
    OpAdd, OpSub, OpMul, OpDiv, OpMod, OpPow,
    OpEq, OpNe, OpStrictEq, OpStrictNe, OpLt, OpGt, OpLe, OpGe,
    OpBitAnd, OpBitOr, OpBitXor, OpShl, OpShr, OpUShr, OpIn, OpInstanceOf,
    // lógicos
    OpAnd, OpOr, OpNullish,
    // unários
    OpNeg, OpPlus, OpNot, OpBitNot, OpTypeof, OpVoid, OpDelete,
};

// flags
inline constexpr u8 kFlagOptional = 1u << 0;   ///< ?. neste elo
inline constexpr u8 kFlagArrow = 1u << 1;
inline constexpr u8 kFlagExprBody = 1u << 2;
inline constexpr u8 kFlagPrefix = 1u << 3;
inline constexpr u8 kFlagComputed = 1u << 4;
inline constexpr u8 kFlagConst = 1u << 5;      ///< const (Var, ForOf/ForIn)
inline constexpr u8 kFlagDeclares = 1u << 6;   ///< ForOf/ForIn com let/const/var
inline constexpr u8 kFlagLogicalAssign = 1u << 7;

struct Node {
    N kind = N::Empty;
    u16 op = OpNone;
    u8 flags = 0;
    u32 line = 0;
    u32 col = 0;
    f64 num = 0.0;
    std::string text;                    ///< Str, chave estática, nome de função
    const std::string* name = nullptr;   ///< Ident, PatIdent, Func (nome)
    std::vector<const Node*> kids;
    const Node* a = nullptr;
    const Node* b = nullptr;
    const Node* c = nullptr;
    const Node* d = nullptr;
};

/// Um arquivo compilado: os nós vivem aqui (endereços estáveis).
struct Program {
    std::deque<Node> nodes;
    const Node* root = nullptr;
    std::string file;
};

/// Compila. Falso = erro de sintaxe (mensagem, linha e coluna em `error`).
bool parse_program(Context& ctx, std::string_view source, std::string_view file, Program& out, ScriptError& error);

} // namespace aurea::plugins::script
