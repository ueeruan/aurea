// =============================================================================
//  Aurea / core / Version.hpp
//
//  Versão do MOTOR (não do app: o app tem a sua, para a loja).
//
//  Estes números vão gravados no cabeçalho de cada .aurea e no rodapé de cada
//  exportação. É como um projeto gravado por uma versão mais nova é recusado em
//  vez de aberto pela metade.
//
//  O CMake define os três via `target_compile_definitions`. Os valores de
//  reserva existem para que um arquivo possa ser compilado fora do CMake — um
//  teste isolado, uma ferramenta de linha de comando — sem que o autor precise
//  lembrar de passar três `-D`. Sem eles, o erro apareceria como "identificador
//  não declarado" no meio de um arquivo de serialização, que é o pior lugar
//  possível para descobrir isso.
// =============================================================================
#pragma once

#if !defined(AUREA_VERSION_MAJOR)
    #define AUREA_VERSION_MAJOR 2
#endif
#if !defined(AUREA_VERSION_MINOR)
    #define AUREA_VERSION_MINOR 0
#endif
#if !defined(AUREA_VERSION_PATCH)
    #define AUREA_VERSION_PATCH 0
#endif

/// Versão do FORMATO do arquivo .aurea. Não muda quando o app muda de versão —
/// muda quando o layout dos bytes muda.
#if !defined(AUREA_PROJECT_FORMAT_VERSION)
    #define AUREA_PROJECT_FORMAT_VERSION 1
#endif

#define AUREA_VERSION_STRINGIZE_(x) #x
#define AUREA_VERSION_STRINGIZE(x) AUREA_VERSION_STRINGIZE_(x)
#define AUREA_VERSION_LABEL \
    AUREA_VERSION_STRINGIZE(AUREA_VERSION_MAJOR) "." \
    AUREA_VERSION_STRINGIZE(AUREA_VERSION_MINOR) "." \
    AUREA_VERSION_STRINGIZE(AUREA_VERSION_PATCH)
