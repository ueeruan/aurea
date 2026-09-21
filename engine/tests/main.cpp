#include "TestFramework.hpp"
#include "aurea/core/Log.hpp"

int main(int argc, char** argv) {
    // Silencia o log do motor durante os testes: os avisos esperados (efeito
    // desconhecido, orçamento excedido) são parte do que está sendo testado e
    // poluiriam a saída. O teste verifica o COMPORTAMENTO, não o log.
    ::aurea::set_min_log_level(::aurea::LogLevel::Fatal);

    const char* filter = nullptr;
    if (argc > 1) filter = argv[1];

    return ::aurea::test::run_all(filter);
}
