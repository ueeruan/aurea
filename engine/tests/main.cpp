#include "TestFramework.hpp"
#include "aurea/core/Log.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    // Silencia o log do motor durante os testes: os avisos esperados (efeito
    // desconhecido, orçamento excedido) são parte do que está sendo testado e
    // poluiriam a saída. O teste verifica o COMPORTAMENTO, não o log.
    ::aurea::set_min_log_level(::aurea::LogLevel::Fatal);
    // Sem buffer: se um teste derrubar o processo, a última linha na saída é
    // a do teste que caiu (com buffer, ficaria para trás e apontaria o errado).
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const char* filter = nullptr;
    if (argc > 1) filter = argv[1];

    return ::aurea::test::run_all(filter);
}
