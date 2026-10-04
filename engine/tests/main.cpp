#include "TestFramework.hpp"
#include "aurea/core/Log.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

namespace {
LONG WINAPI report_native_crash(EXCEPTION_POINTERS* fault) {
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT context = *fault->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
    std::fprintf(stderr, "\nNative exception 0x%08lx at %p\n", fault->ExceptionRecord->ExceptionCode,
        fault->ExceptionRecord->ExceptionAddress);
    if (fault->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        && fault->ExceptionRecord->NumberParameters >= 2) {
        std::fprintf(stderr, "  access=%s address=0x%llx\n",
            fault->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" :
            fault->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "execute",
            static_cast<unsigned long long>(fault->ExceptionRecord->ExceptionInformation[1]));
    }
    for (unsigned i = 0; i < 32 && frame.AddrPC.Offset; ++i) {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            std::fprintf(stderr, "  %s+0x%llx\n", symbol->Name, static_cast<unsigned long long>(displacement));
        else std::fprintf(stderr, "  0x%llx\n", static_cast<unsigned long long>(frame.AddrPC.Offset));
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
            SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
    }
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
}
#endif

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(report_native_crash);
#endif
    for (const char* dir : {"build/prompt03", "build/prompt04", "build/effects-packages"}) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    // Silencia o log do motor durante os testes: os avisos esperados (efeito
    // desconhecido, orçamento excedido) são parte do que está sendo testado e
    // poluiriam a saída. O teste verifica o COMPORTAMENTO, não o log.
    ::aurea::set_min_log_level(std::getenv("AUREA_TEST_VERBOSE") ? ::aurea::LogLevel::Info :
        std::getenv("AUREA_TEST_ERRORS") ? ::aurea::LogLevel::Error : ::aurea::LogLevel::Fatal);
    // Sem buffer: se um teste derrubar o processo, a última linha na saída é
    // a do teste que caiu (com buffer, ficaria para trás e apontaria o errado).
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const char* filter = nullptr;
    if (argc > 1) filter = argv[1];

    return ::aurea::test::run_all(filter);
}
