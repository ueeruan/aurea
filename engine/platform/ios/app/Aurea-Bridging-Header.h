// =============================================================================
//  Aurea / platform / ios / app / Aurea-Bridging-Header.h
//
//  O que o Swift enxerga do ObjC. UM cabeçalho, e de propósito: a ponte inteira
//  vive atrás dele.
//
//  Ele é compilado como OBJC (não ObjC++), então nada de C++ pode entrar aqui —
//  é exatamente o motivo de `AureaEngine.h` ser ObjC puro e de `AureaBridge.h`
//  (que fala C++) ficar de fora. Incluir o de C++ aqui quebraria a compilação do
//  app inteiro com erros de tipo que não dizem isso.
// =============================================================================
#import "AureaEngine.h"
#import "AureaJSON.h"
