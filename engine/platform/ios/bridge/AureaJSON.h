// =============================================================================
//  Aurea / platform / ios / bridge / AureaJSON.h
//
//  Escrever JSON sem poder derrubar o app.
//
//  `NSJSONSerialization` LEVANTA `NSException` quando o objeto tem um valor que
//  não é JSON — um tipo Swift sem ponte para Foundation, um número não finito
//  (NaN/infinito), uma chave que não é String. E exceção de ObjC **não é**
//  `Error` do Swift: `try?`, `if let` e `do/catch` não pegam. A exceção sobe,
//  ninguém a segura e o processo MORRE.
//
//  Foi assim que a cena `home-scroll` da paridade derrubou o app no primeiro
//  run em que ela existiu:
//
//      *** Terminating app due to uncaught exception 'NSInvalidArgumentException',
//      reason: 'Invalid type in JSON write (__SwiftValue)'
//
//  O `try? JSONSerialization.data(withJSONObject:)` que estava lá não tinha
//  como pegar — o crash não era do `try?` estar faltando, era de a API ser de
//  exceção e não de erro.
//
//  Este invólucro troca o crash por um `nil`, e mais: ANTES de tentar, ele
//  percorre o objeto e diz QUAL caminho e QUAL tipo estão errados. Sem isso o
//  diagnóstico seria "algum valor do relatório", que não se depura.
//
//  `AureaJSONData` é ObjC; `AureaJSONDataDescription` (a varredura) é C++
//  puro, para poder ser testada no host sem Objective-C.
// =============================================================================
#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// `nil` quando o objeto não é JSON válido — e o motivo vai para o stderr.
/// `pretty` = indentado (e chaves ordenadas), para relatório que humano lê.
FOUNDATION_EXPORT NSData* _Nullable AureaJSONData(id object, BOOL pretty);

/// Varre o objeto e devolve uma frase dizendo o primeiro valor que não é JSON
/// (`nil` = pode serializar). O caminho sai como `bars.tab.measuredSigma`, e o
/// tipo como o nome Swift, para o log apontar o culpado em vez de "algum campo".
/// Está aqui, e não no Swift, para o mesmo código valer no Android/host.
FOUNDATION_EXPORT NSString* _Nullable AureaJSONDescribeFailure(id object);

NS_ASSUME_NONNULL_END
