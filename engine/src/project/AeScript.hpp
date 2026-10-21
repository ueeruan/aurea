// =============================================================================
//  Aurea / project / AeScript.hpp   (interno: interpretador do .jsx)
//
//  Um interpretador PRÓPRIO de um subconjunto do ExtendScript (ES3): var,
//  function (com closures), objetos, arrays, strings, if/for/for-in/while/
//  do/switch/try/throw, operadores do ES3, Math, String/Array/Number. O DOM do
//  After Effects (app, project, comp, camadas, propriedades) vive em
//  AeScriptDom.cpp e entra como objetos "host".
//
//  Por que não reaproveitar o motor de expressões (expr/Expression.hpp): ele é
//  numérico/vetorial, avaliado por quadro do render, sem strings como valor,
//  sem objetos, sem funções do usuário e com o sandbox medido por avaliação.
//  Um script é o contrário — imperativo, cheio de objetos e strings, roda uma
//  vez. Misturar os dois poria risco no caminho quente do render para ganhar
//  pouco; os dois só se encontram na expressão que o script grava (texto
//  compilado pelo motor de expressões, como qualquer outra).
//
//  Sandbox: o interpretador não conhece arquivo, rede, processo nem relógio.
//  Limites: passos do avaliador, profundidade de chamada, bytes vivos no
//  heap, tamanho do fonte; estourar qualquer um PARA o script (erro que o
//  try/catch do script não pega). Math.random tem semente fixa.
// =============================================================================
#pragma once

#include "aurea/project/AeImport.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace aurea::ae::script {

enum class T : u8 { Undef, Null, Bool, Num, Str, Obj };

struct Value {
    T   t = T::Undef;
    bool b = false;
    f64 n = 0.0;
    u32 r = 0;   ///< Str: índice do texto; Obj: índice do objeto

    static Value undef() noexcept { return {}; }
    static Value null() noexcept { Value v; v.t = T::Null; return v; }
    static Value boolean(bool x) noexcept { Value v; v.t = T::Bool; v.b = x; return v; }
    static Value num(f64 x) noexcept { Value v; v.t = T::Num; v.n = x; return v; }
    [[nodiscard]] bool is_obj() const noexcept { return t == T::Obj; }
    [[nodiscard]] bool is_str() const noexcept { return t == T::Str; }
    [[nodiscard]] bool is_num() const noexcept { return t == T::Num; }
    [[nodiscard]] bool nullish() const noexcept { return t == T::Undef || t == T::Null; }
};

enum class OK : u8 { Plain, Array, Function, Native, Host, BoundHost, Error, Regex };

struct Object {
    OK kind = OK::Plain;
    std::string cls = "Object";   ///< "Array", "TextDocument", "KeyframeEase", "Shape", "Error"...
    u32 proto = 0;                ///< protótipo (0 = nenhum; o objeto 0 é reservado)
    std::vector<std::pair<std::string, Value>> props;
    std::vector<Value> elems;     ///< Array
    i32 fn = -1;                  ///< Function: índice da função no programa
    u32 env = 0;                  ///< Function: ambiente capturado
    u32 native = 0;               ///< Native: id da função embutida
    Value self;                   ///< Native: receptor ligado (método de string/array)
    u32 hostKind = 0;             ///< Host: tipo do objeto do DOM
    u64 h0 = 0, h1 = 0, h2 = 0;   ///< Host: dados (camada, referência de propriedade...)
    std::string method;           ///< BoundHost: nome do método
};

class Interp;

/// Ganchos do DOM do After Effects (AeScriptDom.cpp).
struct Dom {
    virtual ~Dom() = default;
    /// Globais: app, KeyframeEase, KeyframeInterpolationType, TextDocument...
    virtual void install(Interp& in) = 0;
    /// Leitura de atributo de objeto host. `handled` = o DOM respondeu.
    virtual Value get(Interp& in, u32 obj, const std::string& name, bool& handled) = 0;
    virtual bool set(Interp& in, u32 obj, const std::string& name, const Value& v) = 0;
    /// Chamada de método de objeto host (obj.method(args)).
    virtual Value call(Interp& in, u32 obj, const std::string& method, const std::vector<Value>& args, bool& handled) = 0;
    /// obj[i] em coleções do host (camadas, efeitos...).
    virtual Value index(Interp& in, u32 obj, const Value& key, bool& handled) = 0;
    /// `new X(args)` de classes do DOM (KeyframeEase, TextDocument, Shape...).
    virtual Value construct(Interp& in, const std::string& cls, const std::vector<Value>& args, bool& handled) = 0;
    /// Fim do script (com ou sem erro): expressões pendentes, contagens.
    virtual void finish(Interp& in) = 0;
};

struct Program;   ///< árvore compilada (AeScript.cpp)

class Interp {
public:
    Interp(Host& host, ImportReport& report, const ScriptLimits& limits);
    ~Interp();
    Interp(const Interp&) = delete;
    Interp& operator=(const Interp&) = delete;

    /// Compila e roda. Falso = erro de sintaxe, exceção não tratada ou limite.
    bool run(std::string_view source, Dom& dom);

    // --- heap -------------------------------------------------------------------
    [[nodiscard]] u32 new_object(OK kind, std::string cls);
    [[nodiscard]] Value str(std::string s);
    [[nodiscard]] static Value obj(u32 i) noexcept { Value v; v.t = T::Obj; v.r = i; return v; }
    [[nodiscard]] Object& o(u32 i) noexcept { return *objects_[i]; }
    [[nodiscard]] Object& o(const Value& v) noexcept { return *objects_[v.r]; }
    [[nodiscard]] const std::string& s(const Value& v) const noexcept { return strings_[v.r]; }
    [[nodiscard]] Value array(std::vector<Value> elems);
    [[nodiscard]] Value plain(std::string cls = "Object");

    // --- conversões ---------------------------------------------------------------
    [[nodiscard]] f64 to_num(const Value& v);
    [[nodiscard]] std::string to_str(const Value& v);
    [[nodiscard]] bool truthy(const Value& v) const noexcept;
    [[nodiscard]] std::vector<f64> to_vec(const Value& v);   ///< número ou array de números

    // --- propriedades de objetos comuns --------------------------------------------
    [[nodiscard]] Value get(const Value& objv, const std::string& name);
    void put(const Value& objv, const std::string& name, const Value& v);
    [[nodiscard]] Value own(u32 obj, const std::string& name, bool* found = nullptr) const;
    void own_set(u32 obj, const std::string& name, const Value& v);

    /// Chama uma função do script (callback de sort, etc.).
    [[nodiscard]] Value call(const Value& fn, const Value& self, const std::vector<Value>& args);

    // --- erros e relatório ----------------------------------------------------------
    /// Erro que o script pode pegar (throw new Error).
    Value throw_error(const std::string& msg);
    /// Para o script (limites do sandbox; o catch do script não pega).
    void abort(const std::string& msg);
    /// Chamada bloqueada pelo sandbox (relatada, não executada).
    void blocked(const std::string& what);
    void unsupported(const std::string& what, const std::string& why);
    void approx(const std::string& what, const std::string& why);
    [[nodiscard]] u32 line() const noexcept { return line_; }
    [[nodiscard]] ImportReport& report() noexcept { return report_; }
    [[nodiscard]] Host& host() noexcept { return host_; }
    [[nodiscard]] bool failed() const noexcept { return flow_ >= 4; }
    [[nodiscard]] Value global_object() const noexcept { return obj(global_); }
    void define_global(const std::string& name, const Value& v);
    [[nodiscard]] Value native_fn(u32 id, std::string cls = "Function");

    /// Ids de funções nativas do DOM começam aqui (AeScriptDom.cpp chama de volta).
    static constexpr u32 kDomNative = 10000;
    Dom* dom = nullptr;

private:
    friend struct Evaluator;
    Host& host_;
    ImportReport& report_;
    ScriptLimits limits_;
    std::vector<std::unique_ptr<Object>> objects_;
    std::vector<std::string> strings_;
    u64 heapBytes_ = 0;
    u32 global_ = 0;
    u32 line_ = 0;
    u8 flow_ = 0;              ///< 0 normal, 1 return, 2 break, 3 continue, 4 throw, 5 abort
    Value thrown_;
    std::string label_;        ///< break/continue com rótulo (não suportado: vazio)
    std::unique_ptr<Program> program_;
    u64 steps_ = 0;
    u32 depth_ = 0;
    /// Pilha de C++ usada pelo avaliador (recursão de verdade): mede-se a
    /// distância do endereço de uma variável local até o do começo do run().
    uintptr_t stackBase_ = 0;
    static constexpr usize kStackBudget = 192u * 1024u;   ///< cabe na pilha de 512 KB das threads do iOS
    u64 rng_ = 0x9E3779B97F4A7C15ull;
    struct Env { u32 parent; std::vector<std::pair<std::string, Value>> vars; };
    std::vector<Env> envs_;

    void account(u64 bytes);
};

} // namespace aurea::ae::script
