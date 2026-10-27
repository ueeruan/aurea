// =============================================================================
//  Aurea / plugins / Script.hpp
//
//  0.3.4 plugins: AureaScript, o interpretador dos scripts de plugin.
//
//  Um subconjunto do JavaScript (ES2020) avaliado por árvore, inteiro em C++,
//  igual no Android e no iOS. É um SANDBOX por construção: a linguagem não tem
//  nenhuma função que toque disco, rede, relógio do sistema ou processo — o que
//  o script alcança é só o que o anfitrião (o motor) registra como global.
//
//  Limites por chamada (Limits): passos de avaliação, tempo de CPU do script
//  (o tempo parado esperando o anfitrião não conta), profundidade de chamada e
//  bytes vivos no heap. Estourar um limite ABORTA a chamada (o `try/catch` do
//  script não pega) e devolve um erro com o motivo; o contexto continua usável.
//
//  Memória: valores com contagem de referência (shared_ptr) e coleta dos
//  CICLOS (closures ↔ escopos, objetos que se apontam) entre chamadas, a partir
//  dos globais. Cada objeto e texto conta seus bytes no Heap do contexto.
//
//  Exceções: o interpretador usa exceções C++ por dentro (o `throw` do script e
//  os abortos). As funções públicas do Context nunca lançam: devolvem false e
//  um ScriptError. Funções nativas registradas pelo anfitrião podem lançar com
//  `Context::throw_error` (o arquivo delas precisa compilar com exceções; os
//  do sistema de plugins estão em AUREA_IMPORT_BOUNDARIES no CMakeLists).
//
//  A linguagem aceita, as diferenças para o JavaScript e a API dos objetos do
//  AUREA estão em docs/plugins/README.md ("Scripts").
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aurea::plugins::script {

struct Limits {
    u64 maxHeapBytes = 32ull << 20;      ///< bytes vivos no heap do contexto
    u64 maxSteps = 20'000'000;           ///< nós avaliados por chamada
    u32 maxTimeMs = 5000;                ///< CPU do script por chamada
    u32 maxDepth = 64;                   ///< chamadas de função aninhadas
    usize maxSourceBytes = 512u * 1024u; ///< tamanho do arquivo de script
    usize maxStringBytes = 8u << 20;     ///< maior texto
    u32 maxArrayLength = 1u << 22;       ///< maior lista
};

/// Contador de bytes do contexto. Um texto/objeto soma ao nascer e tira ao morrer.
struct Heap {
    i64 live = 0;
    i64 peak = 0;
    i64 limit = 32ll << 20;
    void charge(i64 bytes) noexcept {
        live += bytes;
        if (live > peak) peak = live;
    }
    [[nodiscard]] bool over() const noexcept { return live > limit; }
};

class Context;
struct Object;
struct Env;
struct Node;
struct Program;
class Value;

using ObjectRef = std::shared_ptr<Object>;
/// Função nativa: `self` é o `this` da chamada, `args` os argumentos (pode mexer).
using NativeFn = std::function<Value(Context& ctx, const Value& self, std::vector<Value>& args)>;

/// Texto imutável com a conta de bytes no heap.
struct Str {
    std::string text;
    Heap* heap = nullptr;
    i64 charged = 0;
    Str(std::string t, Heap* h) noexcept;
    ~Str();
    Str(const Str&) = delete;
    Str& operator=(const Str&) = delete;
};

enum class Type : u8 { Undefined = 0, Null, Boolean, Number, String, Object };

class Value {
public:
    Value() noexcept = default;
    [[nodiscard]] static Value undefined() noexcept { return Value(); }
    [[nodiscard]] static Value null() noexcept { Value v; v.type_ = Type::Null; return v; }
    [[nodiscard]] static Value boolean(bool b) noexcept { Value v; v.type_ = Type::Boolean; v.num_ = b ? 1.0 : 0.0; return v; }
    [[nodiscard]] static Value number(f64 n) noexcept { Value v; v.type_ = Type::Number; v.num_ = n; return v; }
    [[nodiscard]] static Value string(std::shared_ptr<const Str> s) noexcept {
        Value v; v.type_ = Type::String; v.str_ = std::move(s); return v;
    }
    [[nodiscard]] static Value object(ObjectRef o) noexcept {
        Value v;
        if (!o) return null();
        v.type_ = Type::Object; v.obj_ = std::move(o); return v;
    }

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_undefined() const noexcept { return type_ == Type::Undefined; }
    [[nodiscard]] bool is_null() const noexcept { return type_ == Type::Null; }
    [[nodiscard]] bool is_nullish() const noexcept { return type_ == Type::Undefined || type_ == Type::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return type_ == Type::Boolean; }
    [[nodiscard]] bool is_number() const noexcept { return type_ == Type::Number; }
    [[nodiscard]] bool is_string() const noexcept { return type_ == Type::String; }
    [[nodiscard]] bool is_object() const noexcept { return type_ == Type::Object; }
    [[nodiscard]] bool is_function() const noexcept;
    [[nodiscard]] bool is_array() const noexcept;

    [[nodiscard]] bool as_bool() const noexcept { return num_ != 0.0; }
    [[nodiscard]] f64 as_number() const noexcept { return num_; }
    [[nodiscard]] const std::string& as_string() const noexcept;
    [[nodiscard]] const std::shared_ptr<const Str>& str_ref() const noexcept { return str_; }
    [[nodiscard]] const ObjectRef& as_object() const noexcept { return obj_; }

private:
    Type type_ = Type::Undefined;
    f64 num_ = 0.0;
    std::shared_ptr<const Str> str_;
    ObjectRef obj_;
};

enum class ObjKind : u8 { Plain = 0, Array, Function, Native, Error };

/// Uma propriedade: valor simples ou acessor nativo (get/set).
struct Prop {
    Value value;
    NativeFn getter;
    NativeFn setter;
    bool readOnly = false;
    bool hidden = false;     ///< fora de Object.keys/for..in/JSON (métodos dos protótipos)
};

class PropMap {
public:
    [[nodiscard]] Prop* find(std::string_view key) noexcept;
    [[nodiscard]] const Prop* find(std::string_view key) const noexcept;
    Prop& put(std::string_view key);   ///< cria se não houver (no fim da ordem)
    bool erase(std::string_view key);
    [[nodiscard]] usize size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<std::pair<std::string, Prop>>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::vector<std::pair<std::string, Prop>>& entries() noexcept { return entries_; }
    void clear() noexcept { entries_.clear(); index_.clear(); }

private:
    void rebuild_index();
    std::vector<std::pair<std::string, Prop>> entries_;
    std::unordered_map<std::string, u32> index_;   ///< só com muitas chaves
};

struct Object : std::enable_shared_from_this<Object> {
    explicit Object(Heap* h, ObjKind k) noexcept;
    ~Object();
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    ObjKind kind = ObjKind::Plain;
    Heap* heap = nullptr;
    i64 charged = 0;
    i64 gcRefs = 0;          ///< coleta: referências vindas de fora do heap
    bool marked = false;
    bool frozen = false;
    ObjectRef proto;
    PropMap props;
    std::vector<Value> elements;            ///< Array

    // Função do script
    const Node* fn = nullptr;
    std::shared_ptr<const Program> program;
    std::shared_ptr<Env> closure;
    // Função nativa
    NativeFn native;
    std::string nativeName;
    bool constructor = false;               ///< aceita `new`

    // Dados do anfitrião (camada, propriedade, quadro...): o motor decide o sentido.
    u32 hostTag = 0;
    u64 hostId = 0;
    i64 hostA = 0;
    i64 hostB = 0;
    std::shared_ptr<void> hostData;

    void grow(i64 bytes) noexcept;          ///< soma ao heap e ao próprio total
};

struct ScriptError {
    std::string name;      ///< "SyntaxError", "TypeError", "PermissionError", "LimitError"...
    std::string message;
    std::string file;
    u32 line = 0;
    u32 column = 0;
    bool aborted = false;  ///< limite estourado ou cancelado (o script não pôde pegar)
    [[nodiscard]] std::string text() const;   ///< "main.js:12:5: TypeError: ..."
};

/// O que se lança por dentro (o `throw` do script). Público para as nativas.
struct ThrowSignal { Value value; };
/// Limite estourado/cancelado: atravessa o try/catch do script.
struct AbortSignal { std::string name; std::string message; };

/// Só confere a sintaxe (instalação do plugin): nada roda. Nunca lança.
[[nodiscard]] bool check_syntax(std::string_view source, std::string_view fileName, ScriptError& error) noexcept;

class Context {
public:
    explicit Context(Limits limits = {});
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // --- Execução (nunca lançam) --------------------------------------------
    /// Compila e roda o código do topo do arquivo (define as funções globais).
    bool run(std::string_view source, std::string_view fileName, ScriptError& error) noexcept;
    /// Chama a função global `name` com `args`.
    bool call(std::string_view name, std::vector<Value> args, Value& result, ScriptError& error) noexcept;
    /// Chama um valor-função.
    bool call_value(const Value& fn, const Value& self, std::vector<Value> args, Value& result, ScriptError& error) noexcept;
    [[nodiscard]] bool has_function(std::string_view name) const noexcept;

    /// Pede para parar a chamada em andamento (qualquer thread). Vale até o fim dela.
    void request_cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }
    /// Em volta de uma espera do anfitrião (rede, seletor): o relógio do limite para.
    void pause_clock() noexcept;
    void resume_clock() noexcept;
    /// Coleta os ciclos. Pode rodar no meio de uma chamada: o que o C++ segura
    /// (pilha do avaliador, nativas) conta como raiz pela contagem de
    /// referências (exclusão por tentativa, como a do CPython). Devolve os bytes vivos.
    i64 collect() noexcept;
    [[nodiscard]] const Heap& heap() const noexcept { return *heap_; }
    [[nodiscard]] const Limits& limits() const noexcept { return limits_; }
    [[nodiscard]] u64 steps_last_call() const noexcept { return lastSteps_; }

    /// Saída do `console` (nível 0 log, 1 info, 2 warn, 3 error).
    std::function<void(int level, const std::string& text)> onConsole;
    /// Semente do Math.random (determinístico por contexto).
    void seed(u64 s) noexcept { rng_ = s ? s : 0x9E3779B97F4A7C15ull; }

    // --- Globais e objetos do anfitrião --------------------------------------
    void set_global(std::string_view name, Value v, bool readOnly = true);
    [[nodiscard]] Value global(std::string_view name) const;
    [[nodiscard]] ObjectRef new_object();
    [[nodiscard]] ObjectRef new_object_with(ObjectRef proto);
    [[nodiscard]] ObjectRef new_array(std::vector<Value> items = {});
    [[nodiscard]] Value str(std::string s);
    [[nodiscard]] ObjectRef new_native(std::string name, NativeFn fn, bool constructor = false);
    void method(const ObjectRef& obj, std::string_view name, NativeFn fn);
    void accessor(const ObjectRef& obj, std::string_view name, NativeFn getter, NativeFn setter = nullptr);
    void constant(const ObjectRef& obj, std::string_view name, Value v);
    /// Um Error com nome e mensagem (o que `new Error(...)` cria).
    [[nodiscard]] Value make_error(std::string_view name, std::string message);
    /// Lança no script (só dentro de uma nativa). Mensagem para a pessoa.
    [[noreturn]] void throw_error(std::string_view name, std::string message);

    // --- Conversões e acesso (para as nativas) -------------------------------
    [[nodiscard]] std::string to_string(const Value& v);
    [[nodiscard]] f64 to_number(const Value& v);
    [[nodiscard]] static bool truthy(const Value& v) noexcept;
    [[nodiscard]] Value get(const Value& base, std::string_view key);
    void set(const Value& base, std::string_view key, Value v);
    [[nodiscard]] Value call_function(const Value& fn, const Value& self, std::vector<Value>& args);
    [[nodiscard]] std::string json_stringify(const Value& v, std::string_view indent = {});
    [[nodiscard]] Value json_parse(std::string_view text);   ///< lança SyntaxError
    [[nodiscard]] static std::string number_text(f64 n);
    [[nodiscard]] static std::string type_name(const Value& v);

    /// Mantém vivo um valor guardado pelo anfitrião em C++ (protótipos, caches):
    /// a coleta de ciclos só enxerga o que sai dos globais e destas raízes.
    void add_root(const Value& v) { roots_.push_back(v); }
    /// Dentro de uma nativa: a chamada veio de `new`.
    [[nodiscard]] bool constructing() const noexcept { return constructing_; }

    // --- Interno (o avaliador) -------------------------------------------------
    const std::string* intern(std::string_view name);
    [[nodiscard]] Value literal(const Node* n, const std::string& text);
    u32 curLine = 0;
    u32 curCol = 0;
    const Program* curProgram = nullptr;
    bool constructing_ = false;
    [[nodiscard]] std::shared_ptr<Env> globals() const noexcept { return global_; }
    void step();                         ///< conta um passo; aborta por limite
    void enter_call();
    void leave_call() noexcept { if (depth_) --depth_; }
    ObjectRef objectProto, arrayProto, functionProto, stringProto, numberProto, errorProto;
    u64 rng_ = 0x9E3779B97F4A7C15ull;

private:
    friend struct Interp;
    void begin_call() noexcept;
    void end_call() noexcept;
    bool guard(const std::function<void()>& body, ScriptError& error) noexcept;
    void install_builtins();

    Limits limits_;
    std::shared_ptr<Heap> heap_;
    std::shared_ptr<Env> global_;
    std::unordered_set<std::string> names_;
    std::vector<std::shared_ptr<const Program>> programs_;
    std::vector<std::weak_ptr<Object>> tracked_;
    std::vector<std::weak_ptr<Env>> trackedEnvs_;
    i64 gcAt_ = 4ll << 20;          ///< bytes vivos que disparam a coleta no meio da chamada
    usize compactObjectsAt_ = 4096;
    usize compactEnvsAt_ = 4096;
    std::vector<Value> roots_;
    std::unordered_map<const Node*, Value> literals_;
    std::atomic<bool> cancel_{false};
    u64 steps_ = 0;
    u64 lastSteps_ = 0;
    u32 depth_ = 0;
    u32 callNesting_ = 0;
    std::chrono::steady_clock::time_point started_{};
    std::chrono::steady_clock::duration paused_{};
    std::chrono::steady_clock::time_point pauseAt_{};
    u32 pauseDepth_ = 0;
    friend struct Object;
    friend struct Env;
public:
    // Registro de objetos/escopos para a coleta de ciclos (o avaliador usa).
    void track(const ObjectRef& o);
    void track_env(const std::shared_ptr<Env>& e);
    Heap* heap_ptr() noexcept { return heap_.get(); }
    [[nodiscard]] std::shared_ptr<Env> new_env(const std::shared_ptr<Env>& parent);
};

/// Um escopo: nomes internados (ponteiro do Context) → valores.
struct Binding {
    const std::string* name = nullptr;
    Value value;
    bool isConst = false;
};
struct Env {
    explicit Env(Heap* h, std::shared_ptr<Env> p) noexcept;
    ~Env();
    Env(const Env&) = delete;
    Env& operator=(const Env&) = delete;
    Heap* heap = nullptr;
    i64 charged = 0;
    i64 gcRefs = 0;
    bool marked = false;
    std::shared_ptr<Env> parent;
    std::vector<Binding> vars;
    bool hasThis = false;
    Value thisValue;
    Value newTarget;
    [[nodiscard]] Binding* find_local(const std::string* name) noexcept {
        for (Binding& b : vars) if (b.name == name) return &b;
        return nullptr;
    }
    Binding& declare(const std::string* name, Value v, bool isConst);
};

} // namespace aurea::plugins::script
