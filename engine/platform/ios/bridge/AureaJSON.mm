// =============================================================================
//  Aurea / platform / ios / bridge / AureaJSON.mm
//
//  Ver o cabeçalho: por que existe e o crash que o motivou.
//
//  A varredura (`scan`) é o que dá valor a isto. `isValidJSONObject:`
//  responde "não" e para aí; num relatório de paridade com dezenas de campos
//  isso não se depura. Aqui o log sai com o CAMINHO e o TIPO:
//
//      [AureaJSON] home-backdrop.json invalido em bars.tab.scale: CGFloat
//
//  Tipos aceitos (os mesmos de `NSJSONSerialization`): NSString, NSNumber,
//  NSArray, NSDictionary, NSNull. `NSNumber` ainda precisa ser FINITO — NaN e
//  infinito são NSNumber de verdade e mesmo assim fazem a API levantar, que é o
//  caso mais traiçoeiro dos três. Por isso a varredura roda SEMPRE, e não só
//  quando `isValidJSONObject:` diz que não: há versão de Foundation em que ele
//  responde "sim" para NaN e a exceção vem depois.
//
//  `static_cast<NSNumber*>(x)` NÃO compila dentro de uma mensagem ObjC
//  (`[static_cast<NSNumber*>x objCType]` dá "expected '(' after 'static_cast'",
//  foi o que quebrou os dois jobs do run 35929352896). Cast de C serve, ou —
//  como aqui — uma variável local do tipo certo, que é o que se lê melhor.
// =============================================================================
#import "AureaJSON.h"

#include <cmath>

namespace {

/// O nome do tipo do jeito que quem lê o log reconhece.
NSString* type_name(id value) {
    if ([value isKindOfClass:[NSNumber class]]) {
        // Um NSNumber guarda o tipo de origem: `objCType` distingue um BOOL de
        // um char, e um double de um CGFloat (que no iOS arm64 é double).
        NSNumber* number = (NSNumber*)value;
        const char* t = number.objCType;
        if (t && (t[0] == 'c' || t[0] == 'B')) return @"BOOL";
        if (t && t[0] == 'f') return @"Float";
        return @"Double";
    }
    return NSStringFromClass([value class]) ?: @"?";
}

/// `nil` = pode escrever. Preenche `why` com o primeiro problema.
NSString* scan(id value, NSString* path, NSUInteger depth) {
    if (depth > 32) return [NSString stringWithFormat:@"%@: aninhado demais", path];
    if (value == nil || value == (id)[NSNull null]) return nil;
    if ([value isKindOfClass:[NSString class]]) return nil;
    if ([value isKindOfClass:[NSNumber class]]) {
        NSNumber* number = (NSNumber*)value;
        const double d = number.doubleValue;
        if (!std::isfinite(d)) {
            return [NSString stringWithFormat:@"%@: %@ nao finito (%g)", path, type_name(value), d];
        }
        return nil;
    }
    if ([value isKindOfClass:[NSArray class]]) {
        NSArray* array = (NSArray*)value;
        for (NSUInteger i = 0; i < array.count; ++i) {
            NSString* sub = [NSString stringWithFormat:@"%@[%lu]", path, (unsigned long)i];
            if (NSString* why = scan(array[i], sub, depth + 1)) return why;
        }
        return nil;
    }
    if ([value isKindOfClass:[NSDictionary class]]) {
        NSDictionary* dict = (NSDictionary*)value;
        for (id key in dict) {
            // Chave tem de ser String: um dicionário com chave de outro tipo é
            // aceito por `isValidJSONObject:` e levanta na hora de escrever.
            if (![key isKindOfClass:[NSString class]]) {
                return [NSString stringWithFormat:@"%@: chave %@ (tipo %@), tem de ser String",
                        path, key, type_name(key)];
            }
            NSString* sub = path.length ? [path stringByAppendingFormat:@".%@", key] : (NSString*)key;
            if (NSString* why = scan(dict[key], sub, depth + 1)) return why;
        }
        return nil;
    }
    // O `__SwiftValue` do crash cai aqui: não é nenhum dos cinco tipos, e
    // `NSStringFromClass` devolve o nome que aparece na exceção original.
    return [NSString stringWithFormat:@"%@: %@ (nao e um tipo JSON)", path, type_name(value)];
}

/// O serializer só aceita Array ou Dictionary no topo (não pedimos
/// `NSJSONWritingFragmentsAllowed`). Uma raiz de outro tipo também levanta.
NSString* describe_root(id object) {
    if (object == nil) return @"objeto nulo";
    if ([object isKindOfClass:[NSArray class]] || [object isKindOfClass:[NSDictionary class]]) {
        return scan(object, @"", 0);
    }
    return [NSString stringWithFormat:@"raiz e %@, tem de ser Array ou Dictionary",
            type_name(object)];
}

} // namespace

NSString* AureaJSONDescribeFailure(id object) {
    return describe_root(object);
}

NSData* AureaJSONData(id object, BOOL pretty) {
    if (object == nil) return nil;
    // A varredura cobre as TRÊS causas que fazem a API levantar em vez de
    // devolver erro: valor não finito, chave que não é String e tipo sem ponte
    // para Foundation (`__SwiftValue`). Roda sempre, sem consultar
    // `isValidJSONObject:` antes — ver o comentário do topo do arquivo.
    //
    // Não há @try/@catch de propósito: este alvo compila com `-fno-exceptions`,
    // e um `@try` aqui dependeria de a flag de exceções de ObjC sobreviver a
    // ela. Trocar um crash raro por um build que não compila seria pior.
    if (NSString* why = describe_root(object)) {
        NSLog(@"[AureaJSON] objeto invalido em %@", why);
        return nil;
    }
    NSJSONWritingOptions options = 0;
    if (pretty) options |= NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys;
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:object options:options error:&error];
    if (!data && error) NSLog(@"[AureaJSON] falha do serializador: %@", error);
    return data;
}
