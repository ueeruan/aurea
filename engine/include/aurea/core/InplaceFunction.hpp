// =============================================================================
//  Aurea / core / InplaceFunction.hpp
//
//  `std::function` sem heap.
//
//  O FrameGraph é remontado a cada frame, e cada passe carrega uma lambda com o
//  que precisa (handles, parâmetros resolvidos). `std::function` aloca quando a
//  captura passa do buffer interno — que no MSVC e na libc++ é pequeno — e 60
//  alocações por passe por segundo durante o playback é exatamente o tipo de
//  custo que aparece como engasgo aleatório no alocador.
//
//  Aqui a captura mora DENTRO do objeto, num buffer de tamanho fixo. Captura
//  maior que o buffer é erro de COMPILAÇÃO, não alocação escondida.
// =============================================================================
#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace aurea {

template <typename Signature, std::size_t Capacity = 128>
class InplaceFunction;

template <typename R, typename... Args, std::size_t Capacity>
class InplaceFunction<R(Args...), Capacity> {
public:
    InplaceFunction() noexcept = default;

    template <typename F,
              typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, InplaceFunction>>>
    InplaceFunction(F&& f) noexcept {   // NOLINT: conversão implícita como std::function
        using Fn = std::decay_t<F>;
        static_assert(sizeof(Fn) <= Capacity,
                      "captura grande demais para InplaceFunction: aumente Capacity "
                      "ou capture um ponteiro para os dados");
        static_assert(alignof(Fn) <= alignof(std::max_align_t),
                      "alinhamento de captura nao suportado");
        static_assert(std::is_nothrow_move_constructible_v<Fn>,
                      "a captura precisa ser movivel sem excecao");
        ::new (static_cast<void*>(storage_)) Fn(std::forward<F>(f));
        invoke_ = [](void* s, Args... args) -> R {
            return (*static_cast<Fn*>(s))(std::forward<Args>(args)...);
        };
        manage_ = [](void* dst, void* src, bool destroyOnly) noexcept {
            if (destroyOnly) {
                static_cast<Fn*>(src)->~Fn();
                return;
            }
            ::new (dst) Fn(std::move(*static_cast<Fn*>(src)));
            static_cast<Fn*>(src)->~Fn();
        };
    }

    InplaceFunction(InplaceFunction&& o) noexcept { move_from(o); }
    InplaceFunction& operator=(InplaceFunction&& o) noexcept {
        if (this != &o) {
            reset();
            move_from(o);
        }
        return *this;
    }

    InplaceFunction(const InplaceFunction&) = delete;
    InplaceFunction& operator=(const InplaceFunction&) = delete;

    ~InplaceFunction() { reset(); }

    void reset() noexcept {
        if (manage_) manage_(nullptr, storage_, true);
        invoke_ = nullptr;
        manage_ = nullptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return invoke_ != nullptr; }

    R operator()(Args... args) const {
        return invoke_(const_cast<unsigned char*>(storage_), std::forward<Args>(args)...);
    }

private:
    void move_from(InplaceFunction& o) noexcept {
        if (!o.invoke_) return;
        o.manage_(storage_, o.storage_, false);
        invoke_ = o.invoke_;
        manage_ = o.manage_;
        o.invoke_ = nullptr;
        o.manage_ = nullptr;
    }

    alignas(std::max_align_t) unsigned char storage_[Capacity]{};
    R (*invoke_)(void*, Args...) = nullptr;
    void (*manage_)(void*, void*, bool) noexcept = nullptr;
};

} // namespace aurea
