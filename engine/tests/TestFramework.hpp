// =============================================================================
//  Aurea / tests / TestFramework.hpp
//
//  Um framework de teste mínimo, sem dependência externa.
//
//  Por que não GoogleTest: o motor é compilado para Android e iOS com o mesmo
//  código. Uma dependência de teste que só existe no host torna os testes de
//  integração inúteis justamente onde eles importam — no aparelho. Este
//  framework é header-only, sem alocação dinâmica no registro, e roda nos três
//  alvos.
//
//  O que ele NÃO faz: mock, fixture complexa, relatório em XML. Nada disso
//  ajuda a encontrar o bug que os testes existem para encontrar.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace aurea::test {

using TestFn = void (*)();

struct TestCase {
    const char* suite;
    const char* name;
    TestFn fn;
};

class Registry {
public:
    static Registry& instance() noexcept {
        static Registry r;
        return r;
    }

    void add(const char* suite, const char* name, TestFn fn) noexcept {
        if (count_ < kMax) tests_[count_++] = TestCase{suite, name, fn};
    }

    [[nodiscard]] u32 count() const noexcept { return count_; }
    [[nodiscard]] const TestCase& at(u32 i) const noexcept { return tests_[i]; }

    [[nodiscard]] u32 failures() const noexcept { return failures_; }
    void fail() noexcept { ++failures_; }
    void check() noexcept { ++checks_; }
    [[nodiscard]] u32 checks() const noexcept { return checks_; }

private:
    // 1024: a Fase 8 soma suítes em paralelo; acima do teto o `add` descartaria
    // testes EM SILÊNCIO (a contagem final pareceria verde).
    static constexpr u32 kMax = 1024;
    TestCase tests_[kMax]{};
    u32 count_ = 0;
    u32 failures_ = 0;
    u32 checks_ = 0;
};

/// Executa todos os testes registrados. Devolve o número de falhas — é o código
/// de saída do executável, para que o CI não precise interpretar texto.
inline int run_all(const char* filter = nullptr) noexcept {
    Registry& reg = Registry::instance();
    u32 ran = 0;

    std::printf("Aurea Engine — testes\n");
    std::printf("=====================\n");

    const char* lastSuite = nullptr;
    for (u32 i = 0; i < reg.count(); ++i) {
        const TestCase& t = reg.at(i);

        if (filter && *filter) {
            if (!std::strstr(t.suite, filter) && !std::strstr(t.name, filter)) continue;
        }

        if (!lastSuite || std::strcmp(lastSuite, t.suite) != 0) {
            std::printf("\n[%s]\n", t.suite);
            lastSuite = t.suite;
        }

        const u32 before = reg.failures();
        std::printf("  %-52s", t.name);
        std::fflush(stdout);

        t.fn();

        if (reg.failures() == before) {
            std::printf("ok\n");
        } else {
            std::printf("FALHOU\n");
        }
        ++ran;
    }

    std::printf("\n---------------------\n");
    std::printf("%u testes, %u verificacoes, %u falhas\n",
                ran, reg.checks(), reg.failures());
    return static_cast<int>(reg.failures());
}

struct AutoRegister {
    AutoRegister(const char* suite, const char* name, TestFn fn) noexcept {
        Registry::instance().add(suite, name, fn);
    }
};

} // namespace aurea::test

// -----------------------------------------------------------------------------
// Macros
// -----------------------------------------------------------------------------
#define AUREA_TEST(suite, name)                                              \
    static void aurea_test_##suite##_##name();                               \
    static ::aurea::test::AutoRegister aurea_reg_##suite##_##name(           \
        #suite, #name, &aurea_test_##suite##_##name);                        \
    static void aurea_test_##suite##_##name()

#define AUREA_CHECK(expr)                                                    \
    do {                                                                     \
        ::aurea::test::Registry::instance().check();                         \
        if (!(expr)) {                                                       \
            std::printf("\n    FALHA %s:%d: %s\n", __FILE__, __LINE__, #expr);\
            ::aurea::test::Registry::instance().fail();                      \
        }                                                                    \
    } while (0)

#define AUREA_CHECK_MSG(expr, msg)                                           \
    do {                                                                     \
        ::aurea::test::Registry::instance().check();                         \
        if (!(expr)) {                                                       \
            std::printf("\n    FALHA %s:%d: %s — %s\n", __FILE__, __LINE__,  \
                        #expr, (msg));                                       \
            ::aurea::test::Registry::instance().fail();                      \
        }                                                                    \
    } while (0)

#define AUREA_CHECK_EQ(a, b)                                                 \
    do {                                                                     \
        ::aurea::test::Registry::instance().check();                         \
        const auto va_ = (a);                                                \
        const auto vb_ = (b);                                                \
        if (!(va_ == vb_)) {                                                 \
            std::printf("\n    FALHA %s:%d: %s == %s\n", __FILE__, __LINE__,  \
                        #a, #b);                                             \
            ::aurea::test::Registry::instance().fail();                      \
        }                                                                    \
    } while (0)

/// Comparação com tolerância. Ponto flutuante em GPU e em interpolação de curva
/// nunca é exato — exigir igualdade exata produziria testes que falham por
/// ruído e que a equipe aprende a ignorar.
#define AUREA_CHECK_NEAR(a, b, tol)                                          \
    do {                                                                     \
        ::aurea::test::Registry::instance().check();                         \
        const double va_ = static_cast<double>(a);                           \
        const double vb_ = static_cast<double>(b);                           \
        const double diff_ = va_ > vb_ ? va_ - vb_ : vb_ - va_;              \
        if (!(diff_ <= (tol))) {                                             \
            std::printf("\n    FALHA %s:%d: %s ~= %s (%.6f vs %.6f, dif %.6f)\n", \
                        __FILE__, __LINE__, #a, #b, va_, vb_, diff_);        \
            ::aurea::test::Registry::instance().fail();                      \
        }                                                                    \
    } while (0)
