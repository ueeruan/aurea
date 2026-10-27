// =============================================================================
//  0.3.4 plugins: o avaliador do AureaScript (árvore → valores), o heap com a
//  conta de bytes, a coleta de ciclos e as conversões do JavaScript.
//
//  Fluxo de controle: break/continue/return voltam como `Flow` (sem exceção);
//  `throw` do script é um ThrowSignal (C++), e limite/cancelamento um
//  AbortSignal, que o `catch` do script não pega. As funções públicas do
//  Context (`run`, `call`, `call_value`) são a fronteira: nada passa delas.
// =============================================================================
#include "ScriptAst.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace aurea::plugins::script {

// =============================================================================
// Heap: textos, objetos e escopos
// =============================================================================
Str::Str(std::string t, Heap* h) noexcept : text(std::move(t)), heap(h) {
    charged = static_cast<i64>(sizeof(Str) + text.capacity() + 32);
    if (heap) heap->charge(charged);
}
Str::~Str() { if (heap) heap->charge(-charged); }

Object::Object(Heap* h, ObjKind k) noexcept : kind(k), heap(h) {
    charged = static_cast<i64>(sizeof(Object) + 48);
    if (heap) heap->charge(charged);
}
Object::~Object() { if (heap) heap->charge(-charged); }
void Object::grow(i64 bytes) noexcept {
    charged += bytes;
    if (heap) heap->charge(bytes);
}

Env::Env(Heap* h, std::shared_ptr<Env> p) noexcept : heap(h), parent(std::move(p)) {
    charged = static_cast<i64>(sizeof(Env) + 32);
    if (heap) heap->charge(charged);
}
Env::~Env() { if (heap) heap->charge(-charged); }
Binding& Env::declare(const std::string* name, Value v, bool isConst) {
    if (Binding* b = find_local(name)) {
        b->value = std::move(v);
        b->isConst = isConst;
        return *b;
    }
    vars.push_back(Binding{name, std::move(v), isConst});
    const i64 add = static_cast<i64>(sizeof(Binding));
    charged += add;
    if (heap) heap->charge(add);
    return vars.back();
}

bool Value::is_function() const noexcept {
    return type_ == Type::Object && obj_ && (obj_->kind == ObjKind::Function || obj_->kind == ObjKind::Native);
}
bool Value::is_array() const noexcept { return type_ == Type::Object && obj_ && obj_->kind == ObjKind::Array; }
const std::string& Value::as_string() const noexcept {
    static const std::string kEmpty;
    return str_ ? str_->text : kEmpty;
}

// --- PropMap -------------------------------------------------------------------
namespace { constexpr usize kIndexFrom = 12; }

void PropMap::rebuild_index() {
    index_.clear();
    if (entries_.size() < kIndexFrom) return;
    index_.reserve(entries_.size() * 2);
    for (u32 i = 0; i < entries_.size(); ++i) index_.emplace(entries_[i].first, i);
}
Prop* PropMap::find(std::string_view key) noexcept {
    if (!index_.empty()) {
        const auto it = index_.find(std::string(key));
        return it == index_.end() ? nullptr : &entries_[it->second].second;
    }
    for (auto& [k, p] : entries_) if (k == key) return &p;
    return nullptr;
}
const Prop* PropMap::find(std::string_view key) const noexcept {
    return const_cast<PropMap*>(this)->find(key);
}
Prop& PropMap::put(std::string_view key) {
    if (Prop* p = find(key)) return *p;
    entries_.emplace_back(std::string(key), Prop{});
    if (!index_.empty()) index_.emplace(entries_.back().first, static_cast<u32>(entries_.size() - 1));
    else if (entries_.size() >= kIndexFrom) rebuild_index();
    return entries_.back().second;
}
bool PropMap::erase(std::string_view key) {
    for (usize i = 0; i < entries_.size(); ++i) {
        if (entries_[i].first != key) continue;
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        rebuild_index();
        return true;
    }
    return false;
}

std::string ScriptError::text() const {
    std::string out;
    if (!file.empty()) {
        out += file;
        if (line) out += ":" + std::to_string(line) + ":" + std::to_string(column);
        out += ": ";
    }
    out += name.empty() ? "Error" : name;
    out += ": ";
    out += message;
    return out;
}

// =============================================================================
// Utilidades
// =============================================================================
namespace {

usize cp_count(const std::string& s) {
    usize n = 0;
    for (const char c : s) if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

/// Índice canônico de lista ("0", "12"; não "01", "-1", "1.5").
bool array_index(std::string_view key, u32& out) {
    if (key.empty() || key.size() > 10) return false;
    if (key.size() > 1 && key[0] == '0') return false;
    u64 v = 0;
    for (const char c : key) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<u64>(c - '0');
    }
    if (v >= 0xFFFFFFFFull) return false;
    out = static_cast<u32>(v);
    return true;
}

bool number_index(f64 n, u32& out) {
    if (!(n >= 0.0) || n >= 4294967295.0 || std::floor(n) != n) return false;
    out = static_cast<u32>(n);
    return true;
}

i32 to_int32(f64 n) {
    if (!std::isfinite(n)) return 0;
    const f64 t = std::trunc(n);
    const f64 m = std::fmod(t, 4294967296.0);
    u32 u = static_cast<u32>(static_cast<i64>(m < 0 ? m + 4294967296.0 : m));
    return static_cast<i32>(u);
}

enum class Flow : u8 { Normal = 0, Break, Continue, Return };

} // namespace

// =============================================================================
// O avaliador
// =============================================================================
struct Interp {
    Context& c;
    std::shared_ptr<const Program> prog;
    Value ret;
    bool shortCircuit = false;

    using EnvRef = std::shared_ptr<Env>;

    // --- erros ---
    [[noreturn]] void type_error(std::string m) { c.throw_error("TypeError", std::move(m)); }
    [[noreturn]] void ref_error(std::string m) { c.throw_error("ReferenceError", std::move(m)); }

    void at(const Node* n) {
        c.curLine = n->line;
        c.curCol = n->col;
    }

    // --- nomes ---
    Value lookup(const Node* n, const EnvRef& env) {
        for (Env* e = env.get(); e; e = e->parent.get())
            if (Binding* b = e->find_local(n->name)) return b->value;
        ref_error("'" + *n->name + "' nao esta definido");
    }
    void assign_name(const std::string* name, Value v, const EnvRef& env) {
        for (Env* e = env.get(); e; e = e->parent.get()) {
            if (Binding* b = e->find_local(name)) {
                if (b->isConst) type_error("'" + *name + "' e const e nao pode receber outro valor");
                b->value = std::move(v);
                return;
            }
        }
        ref_error("'" + *name + "' nao foi declarado (use let ou const)");
    }
    Value this_of(const EnvRef& env) {
        for (Env* e = env.get(); e; e = e->parent.get())
            if (e->hasThis) return e->thisValue;
        return Value::undefined();
    }

    // --- funções ---
    Value make_closure(const Node* fn, const EnvRef& env) {
        ObjectRef o(new Object(c.heap_ptr(), ObjKind::Function));
        c.track(o);
        o->fn = fn;
        o->program = prog;
        o->closure = env;
        o->nativeName = fn->text;
        o->proto = c.functionProto;
        return Value::object(std::move(o));
    }

    void hoist(const std::vector<const Node*>& kids, const EnvRef& env) {
        for (const Node* k : kids)
            if (k->kind == N::FuncDecl) env->declare(k->name, make_closure(k->a, env), false);
    }
    static bool needs_scope(const std::vector<const Node*>& kids) {
        for (const Node* k : kids) if (k->kind == N::Var || k->kind == N::FuncDecl) return true;
        return false;
    }

    // --- padrões (let {a, b} = ..., parâmetros) ---
    std::vector<Value> iterate(const Value& v, const char* what) {
        std::vector<Value> out;
        if (v.is_array()) return v.as_object()->elements;
        if (v.is_string()) {
            const std::string& s = v.as_string();
            for (usize i = 0; i < s.size();) {
                usize n = 1;
                const unsigned char ch = static_cast<unsigned char>(s[i]);
                if (ch >= 0xF0) n = 4; else if (ch >= 0xE0) n = 3; else if (ch >= 0xC0) n = 2;
                out.push_back(c.str(s.substr(i, n)));
                i += n;
            }
            return out;
        }
        type_error(std::string(what) + ": " + Context::type_name(v) + " nao e uma lista");
    }
    void bind(const Node* pat, Value value, const EnvRef& env, bool declare, bool isConst) {
        switch (pat->kind) {
            case N::PatIdent: {
                if (value.is_undefined() && pat->a) value = eval(pat->a, env);
                if (declare) env->declare(pat->name, std::move(value), isConst);
                else assign_name(pat->name, std::move(value), env);
                return;
            }
            case N::PatObject: {
                if (value.is_undefined() && pat->b) value = eval(pat->b, env);
                if (value.is_nullish()) type_error("nao da para desestruturar " + Context::type_name(value));
                std::vector<std::string> used;
                for (const Node* k : pat->kids) {
                    if (k->kind == N::PatRest) {
                        ObjectRef rest = c.new_object();
                        if (value.is_object()) {
                            for (const auto& [key, p] : value.as_object()->props.entries()) {
                                if (p.hidden || std::find(used.begin(), used.end(), key) != used.end()) continue;
                                std::vector<Value> none;
                                rest->props.put(key).value = p.getter ? p.getter(c, value, none) : p.value;
                                rest->grow(64 + static_cast<i64>(key.size()));
                            }
                        }
                        bind(k->a, Value::object(rest), env, declare, isConst);
                        break;
                    }
                    used.push_back(k->text);
                    bind(k->a, c.get(value, k->text), env, declare, isConst);
                }
                return;
            }
            case N::PatArray: {
                if (value.is_undefined() && pat->b) value = eval(pat->b, env);
                const std::vector<Value> items = iterate(value, "desestruturacao [ ]");
                for (usize i = 0; i < pat->kids.size(); ++i) {
                    const Node* k = pat->kids[i];
                    if (k->kind == N::Hole) continue;
                    if (k->kind == N::PatRest) {
                        std::vector<Value> rest;
                        for (usize j = i; j < items.size(); ++j) rest.push_back(items[j]);
                        bind(k->a, Value::object(c.new_array(std::move(rest))), env, declare, isConst);
                        break;
                    }
                    bind(k, i < items.size() ? items[i] : Value::undefined(), env, declare, isConst);
                }
                return;
            }
            default:
                type_error("padrao invalido");
        }
    }

    Value invoke(const ObjectRef& fnObj, const Value& self, std::vector<Value>& args) {
        const Node* fn = fnObj->fn;
        c.enter_call();
        struct Leave {
            Context& c;
            const Program* program;
            ~Leave() { c.leave_call(); c.curProgram = program; }
        } leave{c, c.curProgram};
        c.curProgram = prog.get();
        EnvRef env = c.new_env(fnObj->closure);
        if (!(fn->flags & kFlagArrow)) {
            env->hasThis = true;
            env->thisValue = self;
            // function nome() {...} usada como expressão: o nome vale lá dentro.
            if (fn->name) env->declare(fn->name, Value::object(fnObj), false);
        }
        for (usize i = 0; i < fn->kids.size(); ++i) {
            const Node* p = fn->kids[i];
            if (p->kind == N::PatRest) {
                std::vector<Value> rest;
                for (usize j = i; j < args.size(); ++j) rest.push_back(args[j]);
                bind(p->a, Value::object(c.new_array(std::move(rest))), env, true, false);
                break;
            }
            bind(p, i < args.size() ? args[i] : Value::undefined(), env, true, false);
        }
        if (fn->flags & kFlagExprBody) return eval(fn->b, env);
        hoist(fn->b->kids, env);
        for (const Node* k : fn->b->kids) {
            const Flow f = exec(k, env);
            if (f == Flow::Return) return ret;
        }
        return Value::undefined();
    }

    std::vector<Value> eval_args(const std::vector<const Node*>& kids, const EnvRef& env) {
        std::vector<Value> args;
        args.reserve(kids.size());
        for (const Node* k : kids) {
            if (k->kind == N::Spread) {
                for (Value& v : iterate(eval(k->a, env), "...")) args.push_back(std::move(v));
            } else {
                args.push_back(eval(k, env));
            }
        }
        return args;
    }

    std::string describe_callee(const Node* n) {
        switch (n->kind) {
            case N::Ident: return *n->name;
            case N::Member: return describe_callee(n->a) + "." + n->text;
            case N::Index: return describe_callee(n->a) + "[...]";
            case N::Call: return describe_callee(n->a) + "(...)";
            case N::This: return "this";
            default: return "a expressao";
        }
    }

    // --- operadores ---
    static bool strict_equal(const Value& a, const Value& b) {
        if (a.type() != b.type()) return false;
        switch (a.type()) {
            case Type::Undefined: case Type::Null: return true;
            case Type::Boolean: return a.as_bool() == b.as_bool();
            case Type::Number: return a.as_number() == b.as_number();
            case Type::String: return a.str_ref() == b.str_ref() || a.as_string() == b.as_string();
            case Type::Object: return a.as_object() == b.as_object();
        }
        return false;
    }
    bool loose_equal(const Value& a, const Value& b) {
        if (a.type() == b.type()) return strict_equal(a, b);
        if (a.is_nullish() && b.is_nullish()) return true;
        if (a.is_nullish() || b.is_nullish()) return false;
        if (a.is_object() || b.is_object()) {
            if (a.is_object() && b.is_object()) return false;
            const Value& prim = a.is_object() ? b : a;
            const Value& obj = a.is_object() ? a : b;
            if (prim.is_string()) return c.to_string(obj) == prim.as_string();
            return c.to_number(obj) == c.to_number(prim);
        }
        return c.to_number(a) == c.to_number(b);
    }
    Value binary(u16 op, const Value& l, const Value& r) {
        switch (op) {
            case OpAdd: {
                if (l.is_number() && r.is_number()) return Value::number(l.as_number() + r.as_number());
                const bool text = l.is_string() || r.is_string() || l.is_object() || r.is_object();
                if (text) {
                    std::string s = c.to_string(l);
                    const std::string t = c.to_string(r);
                    if (s.size() + t.size() > c.limits().maxStringBytes)
                        c.throw_error("RangeError", "texto grande demais (limite " + std::to_string(c.limits().maxStringBytes >> 20) + " MB)");
                    s += t;
                    return c.str(std::move(s));
                }
                return Value::number(c.to_number(l) + c.to_number(r));
            }
            case OpSub: return Value::number(c.to_number(l) - c.to_number(r));
            case OpMul: return Value::number(c.to_number(l) * c.to_number(r));
            case OpDiv: return Value::number(c.to_number(l) / c.to_number(r));
            case OpMod: return Value::number(std::fmod(c.to_number(l), c.to_number(r)));
            case OpPow: return Value::number(std::pow(c.to_number(l), c.to_number(r)));
            case OpEq: return Value::boolean(loose_equal(l, r));
            case OpNe: return Value::boolean(!loose_equal(l, r));
            case OpStrictEq: return Value::boolean(strict_equal(l, r));
            case OpStrictNe: return Value::boolean(!strict_equal(l, r));
            case OpLt: case OpGt: case OpLe: case OpGe: {
                if (l.is_string() && r.is_string()) {
                    const int cmp = l.as_string().compare(r.as_string());
                    return Value::boolean(op == OpLt ? cmp < 0 : op == OpGt ? cmp > 0 : op == OpLe ? cmp <= 0 : cmp >= 0);
                }
                const f64 a = c.to_number(l), b = c.to_number(r);
                if (std::isnan(a) || std::isnan(b)) return Value::boolean(false);
                return Value::boolean(op == OpLt ? a < b : op == OpGt ? a > b : op == OpLe ? a <= b : a >= b);
            }
            case OpBitAnd: return Value::number(to_int32(c.to_number(l)) & to_int32(c.to_number(r)));
            case OpBitOr: return Value::number(to_int32(c.to_number(l)) | to_int32(c.to_number(r)));
            case OpBitXor: return Value::number(to_int32(c.to_number(l)) ^ to_int32(c.to_number(r)));
            case OpShl: return Value::number(static_cast<i32>(static_cast<u32>(to_int32(c.to_number(l))) << (static_cast<u32>(to_int32(c.to_number(r))) & 31u)));
            case OpShr: return Value::number(to_int32(c.to_number(l)) >> (static_cast<u32>(to_int32(c.to_number(r))) & 31u));
            case OpUShr: return Value::number(static_cast<u32>(to_int32(c.to_number(l))) >> (static_cast<u32>(to_int32(c.to_number(r))) & 31u));
            case OpIn: {
                if (!r.is_object()) type_error("'in' precisa de um objeto a direita");
                const ObjectRef& o = r.as_object();
                const std::string key = c.to_string(l);
                u32 idx = 0;
                if (o->kind == ObjKind::Array && (key == "length" || (array_index(key, idx) && idx < o->elements.size()))) return Value::boolean(true);
                for (const Object* p = o.get(); p; p = p->proto.get()) if (p->props.find(key)) return Value::boolean(true);
                return Value::boolean(false);
            }
            case OpInstanceOf: {
                if (!r.is_function()) type_error("o lado direito do instanceof precisa ser uma funcao");
                if (!l.is_object()) return Value::boolean(false);
                const Value protoV = c.get(r, "prototype");
                if (!protoV.is_object()) return Value::boolean(false);
                for (const Object* p = l.as_object()->proto.get(); p; p = p->proto.get())
                    if (p == protoV.as_object().get()) return Value::boolean(true);
                return Value::boolean(false);
            }
            default: break;
        }
        type_error("operador desconhecido");
    }

    // --- acesso com chave avaliada ---
    Value get_index(const Value& base, const Value& key) {
        if (key.is_number()) {
            u32 i = 0;
            if (base.is_array() && number_index(key.as_number(), i)) {
                const auto& el = base.as_object()->elements;
                return i < el.size() ? el[i] : Value::undefined();
            }
            return c.get(base, Context::number_text(key.as_number()));
        }
        return c.get(base, c.to_string(key));
    }
    void set_index(const Value& base, const Value& key, Value v) {
        if (key.is_number()) {
            c.set(base, Context::number_text(key.as_number()), std::move(v));
            return;
        }
        c.set(base, c.to_string(key), std::move(v));
    }

    // --- atribuição a alvo (nome, obj.x, obj[k]) ---
    Value read_target(const Node* t, const EnvRef& env, Value& base, Value& key) {
        if (t->kind == N::Ident) return lookup(t, env);
        base = eval(t->a, env);
        if (t->kind == N::Member) {
            key = c.str(t->text);
            return c.get(base, t->text);
        }
        key = eval(t->b, env);
        return get_index(base, key);
    }
    void write_target(const Node* t, const EnvRef& env, const Value& base, const Value& key, Value v) {
        if (t->kind == N::Ident) { assign_name(t->name, std::move(v), env); return; }
        if (t->kind == N::Member) { c.set(base, t->text, std::move(v)); return; }
        set_index(base, key, std::move(v));
    }

    // =========================================================================
    // Expressões
    // =========================================================================
    Value eval(const Node* n, const EnvRef& env) {
        c.step();
        switch (n->kind) {
            case N::Num: return Value::number(n->num);
            case N::Str: return c.literal(n, n->text);
            case N::Template: {
                std::string out;
                for (const Node* k : n->kids) {
                    if (k->kind == N::Str) out += k->text;
                    else out += c.to_string(eval(k, env));
                    if (out.size() > c.limits().maxStringBytes) c.throw_error("RangeError", "texto grande demais");
                }
                return c.str(std::move(out));
            }
            case N::True: return Value::boolean(true);
            case N::False: return Value::boolean(false);
            case N::Null: return Value::null();
            case N::Undef: return Value::undefined();
            case N::Ident: return lookup(n, env);
            case N::This: return this_of(env);
            case N::Array: {
                std::vector<Value> items;
                items.reserve(n->kids.size());
                for (const Node* k : n->kids) {
                    if (k->kind == N::Hole) items.push_back(Value::undefined());
                    else if (k->kind == N::Spread) for (Value& v : iterate(eval(k->a, env), "...")) items.push_back(std::move(v));
                    else items.push_back(eval(k, env));
                }
                return Value::object(c.new_array(std::move(items)));
            }
            case N::Object: {
                ObjectRef o = c.new_object();
                for (const Node* k : n->kids) {
                    if (k->kind == N::Spread) {
                        const Value src = eval(k->a, env);
                        if (src.is_array()) {
                            const auto& el = src.as_object()->elements;
                            for (usize i = 0; i < el.size(); ++i) { o->props.put(std::to_string(i)).value = el[i]; o->grow(64); }
                        } else if (src.is_object()) {
                            for (const auto& [key, p] : src.as_object()->props.entries()) {
                                if (p.hidden) continue;
                                std::vector<Value> none;
                                o->props.put(key).value = p.getter ? p.getter(c, src, none) : p.value;
                                o->grow(64 + static_cast<i64>(key.size()));
                            }
                        }
                        continue;
                    }
                    std::string key = (k->flags & kFlagComputed) ? c.to_string(eval(k->a, env)) : k->text;
                    Value v = eval(k->b, env);
                    o->grow(64 + static_cast<i64>(key.size()));
                    o->props.put(key).value = std::move(v);
                }
                return Value::object(std::move(o));
            }
            case N::Func: return make_closure(n, env);
            case N::Unary: {
                if (n->op == OpTypeof && n->a->kind == N::Ident) {
                    for (Env* e = env.get(); e; e = e->parent.get())
                        if (Binding* b = e->find_local(n->a->name)) return c.str(Context::type_name(b->value));
                    return c.str("undefined");
                }
                if (n->op == OpDelete) {
                    const Value base = eval(n->a->a, env);
                    if (!base.is_object()) return Value::boolean(true);
                    const ObjectRef& o = base.as_object();
                    if (o->frozen) type_error("objeto congelado");
                    const std::string key = n->a->kind == N::Member ? n->a->text : c.to_string(eval(n->a->b, env));
                    u32 idx = 0;
                    if (o->kind == ObjKind::Array && array_index(key, idx)) {
                        if (idx < o->elements.size()) o->elements[idx] = Value::undefined();
                        return Value::boolean(true);
                    }
                    if (const Prop* p = o->props.find(key); p && (p->readOnly || p->getter)) type_error("'" + key + "' nao pode ser apagado");
                    o->props.erase(key);
                    return Value::boolean(true);
                }
                const Value v = eval(n->a, env);
                switch (n->op) {
                    case OpNeg: return Value::number(-c.to_number(v));
                    case OpPlus: return Value::number(c.to_number(v));
                    case OpNot: return Value::boolean(!Context::truthy(v));
                    case OpBitNot: return Value::number(~to_int32(c.to_number(v)));
                    case OpTypeof: return c.str(Context::type_name(v));
                    case OpVoid: return Value::undefined();
                    default: break;
                }
                type_error("operador desconhecido");
            }
            case N::Update: {
                Value base, key;
                const f64 old = c.to_number(read_target(n->a, env, base, key));
                const f64 now = n->op == 1 ? old + 1.0 : old - 1.0;
                write_target(n->a, env, base, key, Value::number(now));
                return Value::number((n->flags & kFlagPrefix) ? now : old);
            }
            case N::Binary: {
                const Value l = eval(n->a, env);
                const Value r = eval(n->b, env);
                return binary(n->op, l, r);
            }
            case N::Logical: {
                Value l = eval(n->a, env);
                if (n->op == OpAnd) return Context::truthy(l) ? eval(n->b, env) : l;
                if (n->op == OpOr) return Context::truthy(l) ? l : eval(n->b, env);
                return l.is_nullish() ? eval(n->b, env) : l;
            }
            case N::Cond: return Context::truthy(eval(n->a, env)) ? eval(n->b, env) : eval(n->c, env);
            case N::Assign: {
                if (n->op == OpNone) {
                    if (n->a->kind == N::Ident) {
                        Value v = eval(n->b, env);
                        // `const f = () => ...`/`x = function(){}`: a função ganha o nome
                        assign_name(n->a->name, v, env);
                        return v;
                    }
                    Value base = eval(n->a->a, env);
                    Value key = n->a->kind == N::Index ? eval(n->a->b, env) : Value();
                    Value v = eval(n->b, env);
                    write_target(n->a, env, base, key, v);
                    return v;
                }
                Value base, key;
                Value old = read_target(n->a, env, base, key);
                if (n->flags & kFlagLogicalAssign) {
                    const bool keep = n->op == OpAnd ? !Context::truthy(old) : n->op == OpOr ? Context::truthy(old) : !old.is_nullish();
                    if (keep) return old;
                    Value v = eval(n->b, env);
                    write_target(n->a, env, base, key, v);
                    return v;
                }
                const Value r = eval(n->b, env);
                Value v = binary(n->op, old, r);
                write_target(n->a, env, base, key, v);
                return v;
            }
            case N::Member: {
                const Value base = eval(n->a, env);
                if (shortCircuit) return Value::undefined();
                if ((n->flags & kFlagOptional) && base.is_nullish()) { shortCircuit = true; return Value::undefined(); }
                return c.get(base, n->text);
            }
            case N::Index: {
                const Value base = eval(n->a, env);
                if (shortCircuit) return Value::undefined();
                if ((n->flags & kFlagOptional) && base.is_nullish()) { shortCircuit = true; return Value::undefined(); }
                const Value key = eval(n->b, env);
                return get_index(base, key);
            }
            case N::OptChain: {
                shortCircuit = false;
                Value v = eval(n->a, env);
                shortCircuit = false;
                return v;
            }
            case N::Call: {
                Value fn, self;
                const Node* callee = n->a;
                if (callee->kind == N::Member || callee->kind == N::Index) {
                    self = eval(callee->a, env);
                    if (shortCircuit) return Value::undefined();
                    if ((callee->flags & kFlagOptional) && self.is_nullish()) { shortCircuit = true; return Value::undefined(); }
                    if (callee->kind == N::Member) fn = c.get(self, callee->text);
                    else fn = get_index(self, eval(callee->b, env));
                } else {
                    fn = eval(callee, env);
                    if (shortCircuit) return Value::undefined();
                }
                if ((n->flags & kFlagOptional) && fn.is_nullish()) { shortCircuit = true; return Value::undefined(); }
                std::vector<Value> args = eval_args(n->kids, env);
                if (!fn.is_function()) {
                    at(n);
                    type_error(describe_callee(callee) + " nao e uma funcao (e " + Context::type_name(fn) + ")");
                }
                at(n);
                return c.call_function(fn, self, args);
            }
            case N::New: {
                const Value fn = eval(n->a, env);
                std::vector<Value> args = eval_args(n->kids, env);
                at(n);
                if (!fn.is_function()) type_error(describe_callee(n->a) + " nao e um construtor");
                const ObjectRef& f = fn.as_object();
                if (f->kind == ObjKind::Native) {
                    if (!f->constructor) type_error(describe_callee(n->a) + " nao aceita new");
                    struct Flag { Context& c; bool saved; ~Flag() { c.constructing_ = saved; } } flag{c, c.constructing_};
                    c.constructing_ = true;
                    c.step();
                    return f->native(c, Value::undefined(), args);
                }
                if (f->fn && (f->fn->flags & kFlagArrow)) type_error("arrow function nao aceita new");
                Value protoV = c.get(fn, "prototype");
                ObjectRef obj = protoV.is_object() ? c.new_object_with(protoV.as_object()) : c.new_object();
                const Value self = Value::object(obj);
                Value r = c.call_function(fn, self, args);
                return r.is_object() ? r : self;
            }
            case N::Seq: {
                Value v;
                for (const Node* k : n->kids) v = eval(k, env);
                return v;
            }
            default: break;
        }
        c.throw_error("SyntaxError", "expressao nao suportada aqui");
    }

    // =========================================================================
    // Comandos
    // =========================================================================
    Flow run_block(const std::vector<const Node*>& kids, const EnvRef& env) {
        for (const Node* k : kids) {
            const Flow f = exec(k, env);
            if (f != Flow::Normal) return f;
        }
        return Flow::Normal;
    }

    /// Corpo de laço: Break encerra, Continue segue, Return sobe.
    enum class Loop : u8 { Next, Stop, Return };
    Loop loop_body(const Node* body, const EnvRef& env) {
        const Flow f = exec(body, env);
        if (f == Flow::Break) return Loop::Stop;
        if (f == Flow::Return) return Loop::Return;
        return Loop::Next;
    }

    std::vector<std::string> keys_of(const Value& v) {
        std::vector<std::string> keys;
        if (v.is_array()) {
            for (usize i = 0; i < v.as_object()->elements.size(); ++i) keys.push_back(std::to_string(i));
        } else if (v.is_string()) {
            const usize n = cp_count(v.as_string());
            for (usize i = 0; i < n; ++i) keys.push_back(std::to_string(i));
            return keys;
        }
        if (v.is_object())
            for (const auto& [key, p] : v.as_object()->props.entries()) if (!p.hidden) keys.push_back(key);
        return keys;
    }

    Flow exec(const Node* n, const EnvRef& env) {
        c.step();
        at(n);
        switch (n->kind) {
            case N::Program: {
                hoist(n->kids, env);
                return run_block(n->kids, env);
            }
            case N::Block: {
                if (!needs_scope(n->kids)) return run_block(n->kids, env);
                EnvRef scope = c.new_env(env);
                hoist(n->kids, scope);
                return run_block(n->kids, scope);
            }
            case N::Var: {
                const bool isConst = (n->flags & kFlagConst) != 0;
                for (const Node* d : n->kids) bind(d->a, d->b ? eval(d->b, env) : Value::undefined(), env, true, isConst);
                return Flow::Normal;
            }
            case N::ExprStmt: (void)eval(n->a, env); return Flow::Normal;
            case N::If:
                if (Context::truthy(eval(n->a, env))) return exec(n->b, env);
                return n->c ? exec(n->c, env) : Flow::Normal;
            case N::While:
                while (Context::truthy(eval(n->a, env))) {
                    const Loop l = loop_body(n->b, env);
                    if (l == Loop::Stop) break;
                    if (l == Loop::Return) return Flow::Return;
                }
                return Flow::Normal;
            case N::DoWhile:
                do {
                    const Loop l = loop_body(n->b, env);
                    if (l == Loop::Stop) break;
                    if (l == Loop::Return) return Flow::Return;
                } while (Context::truthy(eval(n->a, env)));
                return Flow::Normal;
            case N::For: {
                const bool perIteration = n->a && n->a->kind == N::Var;
                EnvRef cur = perIteration ? c.new_env(env) : env;
                if (n->a) (void)exec(n->a, cur);
                for (;;) {
                    if (n->b && !Context::truthy(eval(n->b, cur))) break;
                    const Loop l = loop_body(n->d, cur);
                    if (l == Loop::Stop) break;
                    if (l == Loop::Return) return Flow::Return;
                    if (perIteration) {
                        // Cada volta tem as próprias variáveis (closures guardam a volta delas).
                        EnvRef copy = c.new_env(env);
                        for (const Binding& b : cur->vars) copy->declare(b.name, b.value, b.isConst);
                        cur = std::move(copy);
                    }
                    if (n->c) (void)eval(n->c, cur);
                }
                return Flow::Normal;
            }
            case N::ForOf: case N::ForIn: {
                const Value src = eval(n->b, env);
                const bool declares = (n->flags & kFlagDeclares) != 0;
                const bool isConst = (n->flags & kFlagConst) != 0;
                auto body = [&](Value item) -> Loop {
                    EnvRef scope = declares ? c.new_env(env) : env;
                    bind(n->a, std::move(item), scope, declares, isConst);
                    return loop_body(n->c, scope);
                };
                if (n->kind == N::ForIn) {
                    if (src.is_nullish()) return Flow::Normal;
                    for (std::string& k : keys_of(src)) {
                        const Loop l = body(c.str(std::move(k)));
                        if (l == Loop::Stop) break;
                        if (l == Loop::Return) return Flow::Return;
                    }
                    return Flow::Normal;
                }
                if (src.is_array()) {
                    // A lista pode crescer durante o laço (como no JavaScript).
                    const ObjectRef arr = src.as_object();
                    for (usize i = 0; i < arr->elements.size(); ++i) {
                        const Loop l = body(arr->elements[i]);
                        if (l == Loop::Stop) break;
                        if (l == Loop::Return) return Flow::Return;
                    }
                    return Flow::Normal;
                }
                if (src.is_object() && !src.is_function()) {
                    type_error("for...of precisa de uma lista ou texto (para objetos use for...in ou Object.keys/entries)");
                }
                for (Value& v : iterate(src, "for...of")) {
                    const Loop l = body(std::move(v));
                    if (l == Loop::Stop) break;
                    if (l == Loop::Return) return Flow::Return;
                }
                return Flow::Normal;
            }
            case N::Break: return Flow::Break;
            case N::Continue: return Flow::Continue;
            case N::Return:
                ret = n->a ? eval(n->a, env) : Value::undefined();
                return Flow::Return;
            case N::Throw: {
                Value v = eval(n->a, env);
                at(n);
                throw ThrowSignal{std::move(v)};
            }
            case N::Try: {
                Flow f = Flow::Normal;
                bool threw = false;
                Value thrown;
                try {
                    f = exec(n->a, env);
                } catch (ThrowSignal& t) {
                    threw = true;
                    thrown = std::move(t.value);
                }
                bool pending = false;
                Value pendingValue;
                if (threw) {
                    if (n->c) {
                        try {
                            EnvRef scope = c.new_env(env);
                            if (n->b) bind(n->b, thrown, scope, true, false);
                            f = exec(n->c, scope);
                        } catch (ThrowSignal& t) {
                            if (!n->d) throw;
                            pending = true;
                            pendingValue = std::move(t.value);
                        }
                    } else {
                        pending = true;
                        pendingValue = std::move(thrown);
                    }
                }
                if (n->d) {
                    const Value savedRet = ret;
                    const Flow ff = exec(n->d, env);
                    if (ff != Flow::Normal) return ff;
                    ret = savedRet;
                }
                if (pending) throw ThrowSignal{std::move(pendingValue)};
                return f;
            }
            case N::Switch: {
                const Value d = eval(n->a, env);
                EnvRef scope = c.new_env(env);
                usize start = n->kids.size();
                usize fallback = n->kids.size();
                for (usize i = 0; i < n->kids.size(); ++i) {
                    const Node* cs = n->kids[i];
                    if (!cs->a) { fallback = i; continue; }
                    if (strict_equal(d, eval(cs->a, scope))) { start = i; break; }
                }
                if (start == n->kids.size()) start = fallback;
                for (usize i = start; i < n->kids.size(); ++i) {
                    hoist(n->kids[i]->kids, scope);
                    const Flow f = run_block(n->kids[i]->kids, scope);
                    if (f == Flow::Break) return Flow::Normal;
                    if (f != Flow::Normal) return f;
                }
                return Flow::Normal;
            }
            case N::FuncDecl: case N::Empty: return Flow::Normal;
            default: break;
        }
        (void)eval(n, env);
        return Flow::Normal;
    }
};

// =============================================================================
// Context
// =============================================================================
Context::Context(Limits limits) : limits_(limits), heap_(std::make_shared<Heap>()) {
    heap_->limit = static_cast<i64>(limits_.maxHeapBytes);
    global_ = new_env(nullptr);
    install_builtins();
}

Context::~Context() {
    // Quebra todo ciclo antes de soltar: closures ↔ escopos ↔ objetos.
    roots_.clear();
    literals_.clear();
    for (auto& w : tracked_) {
        if (ObjectRef o = w.lock()) {
            o->props.clear();
            o->elements.clear();
            o->proto.reset();
            o->closure.reset();
        }
    }
    for (auto& w : trackedEnvs_) {
        if (std::shared_ptr<Env> e = w.lock()) {
            e->vars.clear();
            e->parent.reset();
            e->thisValue = Value();
            e->newTarget = Value();
        }
    }
    objectProto.reset(); arrayProto.reset(); functionProto.reset();
    stringProto.reset(); numberProto.reset(); errorProto.reset();
    global_.reset();
    programs_.clear();
}

void Context::track(const ObjectRef& o) {
    tracked_.push_back(o);
    if (tracked_.size() >= compactObjectsAt_) {
        std::erase_if(tracked_, [](const std::weak_ptr<Object>& w) { return w.expired(); });
        compactObjectsAt_ = std::max<usize>(4096, tracked_.size() * 2);
    }
}
void Context::track_env(const std::shared_ptr<Env>& e) {
    trackedEnvs_.push_back(e);
    if (trackedEnvs_.size() >= compactEnvsAt_) {
        std::erase_if(trackedEnvs_, [](const std::weak_ptr<Env>& w) { return w.expired(); });
        compactEnvsAt_ = std::max<usize>(4096, trackedEnvs_.size() * 2);
    }
}
std::shared_ptr<Env> Context::new_env(const std::shared_ptr<Env>& parent) {
    std::shared_ptr<Env> e(new Env(heap_.get(), parent));
    track_env(e);
    return e;
}

const std::string* Context::intern(std::string_view name) {
    return &*names_.emplace(name).first;
}

Value Context::literal(const Node* n, const std::string& text) {
    const auto it = literals_.find(n);
    if (it != literals_.end()) return it->second;
    Value v = str(text);
    literals_.emplace(n, v);
    return v;
}

void Context::step() {
    ++steps_;
    if (heap_->live > gcAt_ || heap_->live > heap_->limit) {
        (void)collect();
        if (heap_->live > heap_->limit)
            throw AbortSignal{"LimitError", "o script passou do limite de memoria (" + std::to_string(limits_.maxHeapBytes >> 20) + " MB)"};
    }
    if ((steps_ & 1023u) != 0) return;
    if (steps_ > limits_.maxSteps)
        throw AbortSignal{"LimitError", "o script passou do limite de " + std::to_string(limits_.maxSteps / 1000000) + " milhoes de passos numa chamada (laco infinito?)"};
    if (cancel_.load(std::memory_order_relaxed)) throw AbortSignal{"CancelError", "cancelado"};
    if (pauseDepth_ == 0) {
        const auto ran = std::chrono::steady_clock::now() - started_ - paused_;
        if (ran > std::chrono::milliseconds(limits_.maxTimeMs))
            throw AbortSignal{"LimitError", "o script passou do limite de " + std::to_string(limits_.maxTimeMs / 1000) + " s por chamada (laco infinito?)"};
    }
}

void Context::enter_call() {
    if (++depth_ > limits_.maxDepth) {
        --depth_;
        throw_error("RangeError", "funcoes chamando funcoes fundo demais (limite " + std::to_string(limits_.maxDepth) + "): recursao sem fim?");
    }
}

void Context::pause_clock() noexcept {
    if (pauseDepth_++ == 0) pauseAt_ = std::chrono::steady_clock::now();
}
void Context::resume_clock() noexcept {
    if (pauseDepth_ == 0) return;
    if (--pauseDepth_ == 0) paused_ += std::chrono::steady_clock::now() - pauseAt_;
}

void Context::begin_call() noexcept {
    if (callNesting_++ == 0) {
        steps_ = 0;
        depth_ = 0;
        pauseDepth_ = 0;
        paused_ = {};
        started_ = std::chrono::steady_clock::now();
        cancel_.store(false, std::memory_order_relaxed);
    }
}
void Context::end_call() noexcept {
    if (callNesting_ && --callNesting_ == 0) lastSteps_ = steps_;
}

bool Context::guard(const std::function<void()>& body, ScriptError& error) noexcept {
    begin_call();
    error = ScriptError{};
    const std::string file = curProgram ? curProgram->file : std::string();
    try {
        body();
        end_call();
        return true;
    } catch (ThrowSignal& t) {
        error.file = curProgram ? curProgram->file : file;
        error.line = curLine;
        error.column = curCol;
        try {
            if (t.value.is_object() && t.value.as_object()->kind == ObjKind::Error) {
                const ObjectRef& o = t.value.as_object();
                const Prop* name = o->props.find("name");
                const Prop* message = o->props.find("message");
                error.name = name && name->value.is_string() ? name->value.as_string() : "Error";
                error.message = message ? to_string(message->value) : std::string();
                if (o->hostA > 0) { error.line = static_cast<u32>(o->hostA); error.column = static_cast<u32>(o->hostB); }
            } else {
                error.name = "Error";
                error.message = "lancado: " + to_string(t.value);
            }
        } catch (...) {
            error.name = "Error";
            error.message = "erro sem texto";
        }
    } catch (AbortSignal& a) {
        error.name = a.name;
        error.message = std::move(a.message);
        error.file = curProgram ? curProgram->file : file;
        error.line = curLine;
        error.column = curCol;
        error.aborted = true;
    } catch (const std::bad_alloc&) {
        error.name = "LimitError";
        error.message = "sem memoria no aparelho para o script";
        error.aborted = true;
    } catch (...) {
        error.name = "InternalError";
        error.message = "falha interna do interpretador";
        error.aborted = true;
    }
    end_call();
    return false;
}

bool Context::run(std::string_view source, std::string_view fileName, ScriptError& error) noexcept {
    if (source.size() > limits_.maxSourceBytes) {
        error = ScriptError{};
        error.name = "LimitError";
        error.file = std::string(fileName);
        error.message = "script maior que " + std::to_string(limits_.maxSourceBytes / 1024) + " KB";
        return false;
    }
    std::shared_ptr<Program> program;
    try {
        program = std::make_shared<Program>();
    } catch (...) {
        error = ScriptError{};
        error.name = "LimitError";
        error.message = "sem memoria";
        return false;
    }
    if (!parse_program(*this, source, fileName, *program, error)) return false;
    try {
        programs_.push_back(program);
    } catch (...) {
        return false;
    }
    std::shared_ptr<const Program> prog = program;
    return guard([&] {
        curProgram = prog.get();
        Interp in{*this, prog, {}, false};
        (void)in.exec(prog->root, global_);
    }, error);
}

bool check_syntax(std::string_view source, std::string_view fileName, ScriptError& error) noexcept {
    try {
        if (source.size() > Limits{}.maxSourceBytes) {
            error = ScriptError{};
            error.name = "LimitError";
            error.file = std::string(fileName);
            error.message = "script maior que " + std::to_string(Limits{}.maxSourceBytes / 1024) + " KB";
            return false;
        }
        Limits small;
        small.maxHeapBytes = 64ull << 20;
        Context ctx(small);
        Program program;
        return parse_program(ctx, source, fileName, program, error);
    } catch (...) {
        error = ScriptError{};
        error.name = "LimitError";
        error.message = "sem memoria para conferir o script";
        return false;
    }
}

bool Context::has_function(std::string_view name) const noexcept {
    const auto it = names_.find(std::string(name));
    if (it == names_.end() || !global_) return false;
    for (const Binding& b : global_->vars) if (b.name == &*it) return b.value.is_function();
    return false;
}

bool Context::call(std::string_view name, std::vector<Value> args, Value& result, ScriptError& error) noexcept {
    Value fn;
    try {
        fn = global(name);
    } catch (...) {}
    if (!fn.is_function()) {
        error = ScriptError{};
        error.name = "ReferenceError";
        error.message = "o script nao tem a funcao '" + std::string(name) + "'";
        error.file = programs_.empty() ? std::string() : programs_.front()->file;
        return false;
    }
    return call_value(fn, Value::undefined(), std::move(args), result, error);
}

bool Context::call_value(const Value& fn, const Value& self, std::vector<Value> args, Value& result, ScriptError& error) noexcept {
    return guard([&] { result = call_function(fn, self, args); }, error);
}

Value Context::call_function(const Value& fn, const Value& self, std::vector<Value>& args) {
    if (!fn.is_function()) throw_error("TypeError", type_name(fn) + " nao e uma funcao");
    const ObjectRef f = fn.as_object();
    if (f->kind == ObjKind::Native) {
        step();
        struct Flag { Context& c; bool saved; ~Flag() { c.constructing_ = saved; } } flag{*this, constructing_};
        constructing_ = false;
        return f->native(*this, self, args);
    }
    Interp in{*this, f->program, {}, false};
    return in.invoke(f, self, args);
}

i64 Context::collect() noexcept {
    try {
        // Exclusão por tentativa: as referências que um objeto/escopo recebe de
        // DENTRO do heap saem da contagem do shared_ptr; o que sobra veio de
        // fora (globais do Context, raízes, pilha do avaliador, nativas) e é
        // raiz. O que as raízes não alcançam é lixo (ciclos), e é esvaziado.
        std::vector<ObjectRef> objects;
        std::vector<std::shared_ptr<Env>> envs;
        objects.reserve(tracked_.size());
        envs.reserve(trackedEnvs_.size());
        for (auto& w : tracked_) if (ObjectRef o = w.lock()) objects.push_back(std::move(o));
        for (auto& w : trackedEnvs_) if (std::shared_ptr<Env> e = w.lock()) envs.push_back(std::move(e));
        // -1: a cópia em `objects`/`envs` acima.
        for (const ObjectRef& o : objects) { o->gcRefs = o.use_count() - 1; o->marked = false; }
        for (const auto& e : envs) { e->gcRefs = e.use_count() - 1; e->marked = false; }
        auto drop_val = [](const Value& v) { if (v.is_object()) --v.as_object()->gcRefs; };
        for (const ObjectRef& o : objects) {
            if (o->proto) --o->proto->gcRefs;
            for (const auto& [k, p] : o->props.entries()) drop_val(p.value);
            for (const Value& v : o->elements) drop_val(v);
            if (o->closure) --o->closure->gcRefs;
        }
        for (const auto& e : envs) {
            if (e->parent) --e->parent->gcRefs;
            for (const Binding& b : e->vars) drop_val(b.value);
            drop_val(e->thisValue);
            drop_val(e->newTarget);
        }
        std::vector<Object*> objStack;
        std::vector<Env*> envStack;
        auto mark_obj = [&](Object* o) { if (o && !o->marked) { o->marked = true; objStack.push_back(o); } };
        auto mark_val = [&](const Value& v) { if (v.is_object()) mark_obj(v.as_object().get()); };
        auto mark_env = [&](Env* e) { if (e && !e->marked) { e->marked = true; envStack.push_back(e); } };
        for (const ObjectRef& o : objects) if (o->gcRefs > 0) mark_obj(o.get());
        for (const auto& e : envs) if (e->gcRefs > 0) mark_env(e.get());
        while (!objStack.empty() || !envStack.empty()) {
            while (!objStack.empty()) {
                Object* o = objStack.back();
                objStack.pop_back();
                mark_obj(o->proto.get());
                for (const auto& [k, p] : o->props.entries()) mark_val(p.value);
                for (const Value& v : o->elements) mark_val(v);
                mark_env(o->closure.get());
            }
            while (!envStack.empty()) {
                Env* e = envStack.back();
                envStack.pop_back();
                mark_env(e->parent.get());
                for (const Binding& b : e->vars) mark_val(b.value);
                mark_val(e->thisValue);
                mark_val(e->newTarget);
            }
        }
        // Esvazia o lixo (quebra os ciclos); o shared_ptr solta o resto.
        std::vector<Value> dropValues;
        for (const ObjectRef& o : objects) {
            if (o->marked) continue;
            o->props.clear();
            o->elements.clear();
            o->proto.reset();
            o->closure.reset();
        }
        for (const auto& e : envs) {
            if (e->marked) continue;
            e->vars.clear();
            e->parent.reset();
            e->thisValue = Value();
            e->newTarget = Value();
        }
        for (const ObjectRef& o : objects) o->marked = false;
        for (const auto& e : envs) e->marked = false;
        objects.clear();
        envs.clear();
        std::erase_if(tracked_, [](const std::weak_ptr<Object>& w) { return w.expired(); });
        std::erase_if(trackedEnvs_, [](const std::weak_ptr<Env>& w) { return w.expired(); });
        compactObjectsAt_ = std::max<usize>(4096, tracked_.size() * 2);
        compactEnvsAt_ = std::max<usize>(4096, trackedEnvs_.size() * 2);
    } catch (...) {
        // Sem memória para coletar: tenta na próxima.
    }
    gcAt_ = std::clamp<i64>(heap_->live * 2, 4ll << 20, std::max<i64>(heap_->limit, 4ll << 20));
    return heap_->live;
}

// --- objetos -------------------------------------------------------------------
ObjectRef Context::new_object() { return new_object_with(objectProto); }
ObjectRef Context::new_object_with(ObjectRef proto) {
    ObjectRef o(new Object(heap_.get(), ObjKind::Plain));
    o->proto = std::move(proto);
    track(o);
    return o;
}
ObjectRef Context::new_array(std::vector<Value> items) {
    if (items.size() > limits_.maxArrayLength) throw_error("RangeError", "lista grande demais");
    ObjectRef o(new Object(heap_.get(), ObjKind::Array));
    o->proto = arrayProto;
    o->grow(static_cast<i64>(items.size() * sizeof(Value)));
    o->elements = std::move(items);
    track(o);
    return o;
}
Value Context::str(std::string s) {
    return Value::string(std::make_shared<const Str>(std::move(s), heap_.get()));
}
ObjectRef Context::new_native(std::string name, NativeFn fn, bool isConstructor) {
    ObjectRef o(new Object(heap_.get(), ObjKind::Native));
    o->native = std::move(fn);
    o->nativeName = std::move(name);
    o->constructor = isConstructor;
    o->proto = functionProto;
    track(o);
    return o;
}
void Context::method(const ObjectRef& obj, std::string_view name, NativeFn fn) {
    Prop& p = obj->props.put(name);
    p.value = Value::object(new_native(std::string(name), std::move(fn)));
    p.hidden = true;
    p.readOnly = false;
    obj->grow(96);
}
void Context::accessor(const ObjectRef& obj, std::string_view name, NativeFn getter, NativeFn setter) {
    Prop& p = obj->props.put(name);
    p.getter = std::move(getter);
    p.setter = std::move(setter);
    p.readOnly = !p.setter;
    obj->grow(128);
}
void Context::constant(const ObjectRef& obj, std::string_view name, Value v) {
    Prop& p = obj->props.put(name);
    p.value = std::move(v);
    p.readOnly = true;
    obj->grow(64);
}
void Context::set_global(std::string_view name, Value v, bool readOnly) {
    global_->declare(intern(name), std::move(v), readOnly);
}
Value Context::global(std::string_view name) const {
    const auto it = names_.find(std::string(name));
    if (it == names_.end() || !global_) return Value::undefined();
    for (const Binding& b : global_->vars) if (b.name == &*it) return b.value;
    return Value::undefined();
}

Value Context::make_error(std::string_view name, std::string message) {
    ObjectRef o(new Object(heap_.get(), ObjKind::Error));
    o->proto = errorProto;
    if (errorProto) {
        if (const Prop* sub = errorProto->props.find("@" + std::string(name)); sub && sub->value.is_object()) o->proto = sub->value.as_object();
    }
    track(o);
    o->props.put("name").value = str(std::string(name));
    o->props.put("message").value = str(std::move(message));
    o->hostA = curLine;
    o->hostB = curCol;
    std::string where = curProgram ? curProgram->file : std::string();
    if (curLine) where += ":" + std::to_string(curLine) + ":" + std::to_string(curCol);
    Prop& stack = o->props.put("stack");
    stack.value = str("    em " + where);
    stack.hidden = true;
    o->grow(256);
    return Value::object(std::move(o));
}

void Context::throw_error(std::string_view name, std::string message) {
    throw ThrowSignal{make_error(name, std::move(message))};
}

// --- conversões ----------------------------------------------------------------
bool Context::truthy(const Value& v) noexcept {
    switch (v.type()) {
        case Type::Undefined: case Type::Null: return false;
        case Type::Boolean: return v.as_bool();
        case Type::Number: return v.as_number() != 0.0 && !std::isnan(v.as_number());
        case Type::String: return !v.as_string().empty();
        case Type::Object: return true;
    }
    return false;
}

std::string Context::type_name(const Value& v) {
    switch (v.type()) {
        case Type::Undefined: return "undefined";
        case Type::Null: return "object";
        case Type::Boolean: return "boolean";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Object: return v.is_function() ? "function" : "object";
    }
    return "undefined";
}

std::string Context::number_text(f64 n) {
    if (std::isnan(n)) return "NaN";
    if (std::isinf(n)) return n > 0 ? "Infinity" : "-Infinity";
    if (n == 0.0) return "0";
    if (std::fabs(n) < 1e21 && std::floor(n) == n) {
        char b[32];
        std::snprintf(b, sizeof(b), "%.0f", n);
        return b;
    }
    // Dígitos mais curtos que voltam ao mesmo double + as regras do Number#toString.
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), n, std::chars_format::scientific);
    std::string sci(buf, r.ptr);
    const bool neg = sci[0] == '-';
    if (neg) sci.erase(0, 1);
    const usize e = sci.find('e');
    std::string digits;
    for (usize i = 0; i < e; ++i) if (sci[i] != '.') digits += sci[i];
    const int exp10 = std::atoi(sci.c_str() + e + 1);
    const int k = static_cast<int>(digits.size());
    const int point = exp10 + 1;   // posição do ponto decimal
    std::string out;
    if (point >= k && point <= 21) {
        out = digits + std::string(static_cast<usize>(point - k), '0');
    } else if (point > 0 && point <= 21) {
        out = digits.substr(0, static_cast<usize>(point)) + "." + digits.substr(static_cast<usize>(point));
    } else if (point > -6 && point <= 0) {
        out = "0." + std::string(static_cast<usize>(-point), '0') + digits;
    } else {
        out = digits.substr(0, 1);
        if (k > 1) out += "." + digits.substr(1);
        out += "e";
        out += point - 1 >= 0 ? "+" : "-";
        out += std::to_string(std::abs(point - 1));
    }
    return neg ? "-" + out : out;
}

f64 Context::to_number(const Value& v) {
    switch (v.type()) {
        case Type::Undefined: return std::nan("");
        case Type::Null: return 0.0;
        case Type::Boolean: return v.as_bool() ? 1.0 : 0.0;
        case Type::Number: return v.as_number();
        case Type::String: {
            const std::string& s = v.as_string();
            usize a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            if (a == b) return 0.0;
            const std::string t = s.substr(a, b - a);
            if (t == "Infinity" || t == "+Infinity") return INFINITY;
            if (t == "-Infinity") return -INFINITY;
            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
                f64 r = 0.0;
                for (usize i = 2; i < t.size(); ++i) {
                    const unsigned char ch = static_cast<unsigned char>(t[i]);
                    if (!std::isxdigit(ch)) return std::nan("");
                    r = r * 16 + (std::isdigit(ch) ? ch - '0' : std::tolower(ch) - 'a' + 10);
                }
                return r;
            }
            // Só a gramática decimal do JavaScript (strtod aceitaria "inf", "nan", hex).
            usize i = 0;
            if (t[i] == '+' || t[i] == '-') ++i;
            bool digits = false;
            while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) { ++i; digits = true; }
            if (i < t.size() && t[i] == '.') { ++i; while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) { ++i; digits = true; } }
            if (!digits) return std::nan("");
            if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
                ++i;
                if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
                bool ed = false;
                while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) { ++i; ed = true; }
                if (!ed) return std::nan("");
            }
            if (i != t.size()) return std::nan("");
            return std::strtod(t.c_str(), nullptr);
        }
        case Type::Object: {
            if (v.is_array()) {
                const auto& el = v.as_object()->elements;
                if (el.empty()) return 0.0;
                if (el.size() == 1) return to_number(el[0]);
            }
            return std::nan("");
        }
    }
    return std::nan("");
}

std::string Context::to_string(const Value& v) {
    switch (v.type()) {
        case Type::Undefined: return "undefined";
        case Type::Null: return "null";
        case Type::Boolean: return v.as_bool() ? "true" : "false";
        case Type::Number: return number_text(v.as_number());
        case Type::String: return v.as_string();
        case Type::Object: {
            const ObjectRef& o = v.as_object();
            switch (o->kind) {
                case ObjKind::Array: {
                    std::string out;
                    for (usize i = 0; i < o->elements.size(); ++i) {
                        if (i) out += ',';
                        if (!o->elements[i].is_nullish()) {
                            if (o->elements[i].is_array() && o->elements[i].as_object() == o) continue;
                            out += to_string(o->elements[i]);
                        }
                        if (out.size() > limits_.maxStringBytes) throw_error("RangeError", "texto grande demais");
                    }
                    return out;
                }
                case ObjKind::Function: case ObjKind::Native:
                    return "function " + o->nativeName + "() { [codigo] }";
                case ObjKind::Error: {
                    const Prop* name = o->props.find("name");
                    const Prop* message = o->props.find("message");
                    std::string out = name ? to_string(name->value) : "Error";
                    const std::string m = message ? to_string(message->value) : std::string();
                    if (!m.empty()) out += ": " + m;
                    return out;
                }
                case ObjKind::Plain: {
                    // toString nativo do anfitrião (ex.: Date)
                    for (const Object* p = o.get(); p; p = p->proto.get()) {
                        if (const Prop* ts = p->props.find("toString"); ts && ts->value.is_object() && ts->value.as_object()->kind == ObjKind::Native
                            && p != objectProto.get()) {
                            std::vector<Value> none;
                            return to_string(ts->value.as_object()->native(*this, v, none));
                        }
                    }
                    return "[object Object]";
                }
            }
        }
    }
    return std::string();
}

// --- propriedades --------------------------------------------------------------
namespace {

Value read_prop(Context& c, const Prop& p, const Value& self) {
    if (p.getter) {
        std::vector<Value> none;
        return p.getter(c, self, none);
    }
    return p.value;
}

} // namespace

Value Context::get(const Value& base, std::string_view key) {
    switch (base.type()) {
        case Type::Undefined: case Type::Null:
            throw_error("TypeError", "nao da para ler '" + std::string(key) + "' de " + to_string(base));
        case Type::String: {
            const std::string& s = base.as_string();
            if (key == "length") return Value::number(static_cast<f64>(cp_count(s)));
            u32 idx = 0;
            if (array_index(key, idx)) {
                usize n = 0;
                for (usize i = 0; i < s.size();) {
                    usize w = 1;
                    const unsigned char ch = static_cast<unsigned char>(s[i]);
                    if (ch >= 0xF0) w = 4; else if (ch >= 0xE0) w = 3; else if (ch >= 0xC0) w = 2;
                    if (n == idx) return str(s.substr(i, w));
                    ++n;
                    i += w;
                }
                return Value::undefined();
            }
            for (const Object* p = stringProto.get(); p; p = p->proto.get())
                if (const Prop* found = p->props.find(key)) return read_prop(*this, *found, base);
            return Value::undefined();
        }
        case Type::Number: case Type::Boolean: {
            for (const Object* p = numberProto.get(); p; p = p->proto.get())
                if (const Prop* found = p->props.find(key)) return read_prop(*this, *found, base);
            return Value::undefined();
        }
        case Type::Object: break;
    }
    const ObjectRef& o = base.as_object();
    if (o->kind == ObjKind::Array) {
        if (key == "length") return Value::number(static_cast<f64>(o->elements.size()));
        u32 idx = 0;
        if (array_index(key, idx)) return idx < o->elements.size() ? o->elements[idx] : Value::undefined();
    }
    if (o->kind == ObjKind::Function || o->kind == ObjKind::Native) {
        if (key == "name") return str(o->nativeName);
        if (key == "prototype" && o->kind == ObjKind::Function && !o->props.find("prototype")) {
            ObjectRef proto = new_object();
            Prop& p = o->props.put("prototype");
            p.value = Value::object(proto);
            p.hidden = true;
            o->grow(64);
            return p.value;
        }
    }
    for (const Object* p = o.get(); p; p = p->proto.get())
        if (const Prop* found = p->props.find(key)) return read_prop(*this, *found, base);
    return Value::undefined();
}

void Context::set(const Value& base, std::string_view key, Value v) {
    if (base.is_nullish()) throw_error("TypeError", "nao da para gravar '" + std::string(key) + "' em " + to_string(base));
    if (!base.is_object()) return;
    const ObjectRef& o = base.as_object();
    if (o->frozen) throw_error("TypeError", "objeto congelado: '" + std::string(key) + "' nao pode mudar");
    if (o->kind == ObjKind::Array) {
        if (key == "length") {
            const f64 n = to_number(v);
            u32 len = 0;
            if (!number_index(n, len) || len > limits_.maxArrayLength) throw_error("RangeError", "tamanho de lista invalido");
            if (len > o->elements.size()) o->grow(static_cast<i64>((len - o->elements.size()) * sizeof(Value)));
            o->elements.resize(len);
            return;
        }
        u32 idx = 0;
        if (array_index(key, idx)) {
            if (idx >= limits_.maxArrayLength) throw_error("RangeError", "indice de lista grande demais");
            if (idx >= o->elements.size()) {
                o->grow(static_cast<i64>((idx + 1 - o->elements.size()) * sizeof(Value)));
                o->elements.resize(static_cast<usize>(idx) + 1);
            }
            o->elements[idx] = std::move(v);
            return;
        }
    }
    if (Prop* own = o->props.find(key)) {
        if (own->setter) {
            std::vector<Value> args{std::move(v)};
            (void)own->setter(*this, base, args);
            return;
        }
        if (own->readOnly || own->getter) throw_error("TypeError", "'" + std::string(key) + "' e somente leitura");
        own->value = std::move(v);
        return;
    }
    for (const Object* p = o->proto.get(); p; p = p->proto.get()) {
        const Prop* inherited = p->props.find(key);
        if (!inherited) continue;
        if (inherited->setter) {
            std::vector<Value> args{std::move(v)};
            (void)inherited->setter(*this, base, args);
            return;
        }
        if (inherited->getter) throw_error("TypeError", "'" + std::string(key) + "' e somente leitura");
        break;
    }
    o->grow(64 + static_cast<i64>(key.size()));
    o->props.put(key).value = std::move(v);
}

} // namespace aurea::plugins::script
