import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';

/// INVENTARIO DOS EFEITOS.
///
/// A pergunta que o inventario responde nao e "o efeito e bonito" — e
/// "o efeito esta ligado nos proprios controles". Efeito com parametro
/// que a conta nunca le tem um controle morto na tela: a pessoa mexe, o
/// numero muda, a imagem nao. Isso e defeito, nao calibracao, e nenhum
/// teste de imagem pega — a saida esta "certa" para os dois valores.
void main() {
  group('catalogo', () {
    test('todo tipo tem ficha', () {
      for (final t in EffectType.values) {
        expect(effectSpecs[t], isNotNull, reason: 'sem ficha: $t');
      }
    });

    test('o identificador e unico', () {
      final vistos = <String, EffectType>{};
      for (final e in effectSpecs.entries) {
        final antigo = vistos[e.value.id];
        expect(
          antigo,
          isNull,
          reason: 'id "${e.value.id}" repetido em ${e.key} e $antigo',
        );
        vistos[e.value.id] = e.key;
      }
    });

    test('o valor inicial cabe na faixa', () {
      for (final e in effectSpecs.entries) {
        for (final p in e.value.params.entries) {
          final v = p.value;
          expect(
            v.min,
            lessThan(v.max),
            reason: '${e.key}.${p.key}: min >= max',
          );
          expect(
            v.initial,
            greaterThanOrEqualTo(v.min),
            reason: '${e.key}.${p.key}: inicial abaixo do minimo',
          );
          expect(
            v.initial,
            lessThanOrEqualTo(v.max),
            reason: '${e.key}.${p.key}: inicial acima do maximo',
          );
        }
      }
    });

    test('lista de opcoes cobre a faixa da escolha', () {
      for (final e in effectSpecs.entries) {
        for (final p in e.value.params.entries) {
          if (p.value.kind != ParamKind.choice) continue;
          final esperado = (p.value.max - p.value.min).round() + 1;
          expect(
            p.value.options.length,
            esperado,
            reason:
                '${e.key}.${p.key}: '
                '${p.value.options.length} rotulos para $esperado opcoes',
          );
        }
      }
    });

    test('o interruptor vai de 0 a 1', () {
      for (final e in effectSpecs.entries) {
        for (final p in e.value.params.entries) {
          if (p.value.kind != ParamKind.toggle) continue;
          expect(p.value.min, 0, reason: '${e.key}.${p.key}');
          expect(p.value.max, 1, reason: '${e.key}.${p.key}');
        }
      }
    });
  });

  group('controles vivos', () {
    test('todo parametro lido pela conta existe na ficha', () {
      // POR QUE ISTO E UM TESTE DE FONTE: `paramAt` de uma chave que nao
      // existe devolve zero em silencio. O efeito continua compilando,
      // continua renderizando, e o controle na tela deixa de fazer
      // qualquer coisa. Foi assim que o limiar do Unmult passou
      // despercebido — estava na tela, nao estava na conta.
      final f = File(
        'lib/src/features/editor/presentation/widgets/'
        'preview_stage.dart',
      );
      expect(f.existsSync(), isTrue, reason: 'rode a partir da raiz');
      final fonte = f.readAsStringSync();

      final casos = RegExp(r"\n        case EffectType\.(\w+):")
          .allMatches(fonte)
          .toList();
      expect(
        casos.length,
        greaterThan(30),
        reason: 'nao achei os casos de efeito',
      );

      final leitura = RegExp(r"paramAt\(\s*'([A-Za-z_0-9]+)'");
      final problemas = <String>[];

      for (var i = 0; i < casos.length; i++) {
        final nome = casos[i].group(1)!;
        final fim = i + 1 < casos.length
            ? casos[i + 1].start
            : casos[i].start + 4000;
        final corpo = fonte.substring(
          casos[i].end,
          fim.clamp(casos[i].end, fonte.length),
        );

        final tipo = EffectType.values.where((t) => t.name == nome).firstOrNull;
        if (tipo == null) continue;
        final ficha = effectSpecs[tipo];
        if (ficha == null) continue;

        for (final m in leitura.allMatches(corpo)) {
          final chave = m.group(1)!;
          final resolvida = resolveParamKey(tipo, chave);
          if (!ficha.params.containsKey(resolvida)) {
            problemas.add('$nome le "$chave", que nao existe na ficha');
          }
        }
      }

      expect(problemas, isEmpty, reason: problemas.join('\n'));
    });

    test('todo parametro da ficha e lido pela conta', () {
      // O DEFEITO INVERSO, e o que de fato aconteceu: o limiar do Unmult
      // estava na tela, com nome e faixa, e a conta nunca o lia. Mexer
      // nele nao mudava um pixel. Nenhum teste de imagem pega isso — a
      // saida esta "certa" para qualquer valor do controle morto.
      final fonte = File(
        'lib/src/features/editor/presentation/widgets/'
        'preview_stage.dart',
      ).readAsStringSync();
      final casos = RegExp(r"\n        case EffectType\.(\w+):")
          .allMatches(fonte)
          .toList();

      // Efeitos que NAO sao aplicados neste switch: eco e desfoque de
      // movimento re-renderizam a camada inteira em _buildLayers, e o
      // remapeamento de tempo mexe no relogio da camada. A lista e
      // explicita para que mover um efeito de lugar exija mexer aqui.
      const foraDoSwitch = {'echo', 'forceMotionBlur', 'timeRemap'};

      final mortos = <String>[];
      for (var i = 0; i < casos.length; i++) {
        final nome = casos[i].group(1)!;
        if (foraDoSwitch.contains(nome)) continue;
        final tipo = EffectType.values.where((t) => t.name == nome).firstOrNull;
        final ficha = tipo == null ? null : effectSpecs[tipo];
        if (tipo == null || ficha == null) continue;

        final fim = i + 1 < casos.length
            ? casos[i + 1].start
            : casos[i].start + 4000;
        final corpo = fonte.substring(
          casos[i].end,
          fim.clamp(casos[i].end, fonte.length),
        );
        // Alguns efeitos leem parte dos parametros FORA do switch: os do
        // Blob Tracker que descrevem a DETECCAO sao consumidos pela
        // analise, nao pelo desenho. A lista e explicita para nao virar
        // uma busca frouxa que deixa de acusar controle morto.
        var texto = corpo;
        if (nome == 'blobTracker') {
          texto += File(
            'lib/src/features/editor/application/'
            'editor_controller.dart',
          ).readAsStringSync();
        }

        final lidos = {
          for (final m in RegExp(
            r"(?:paramAt|track)\(\s*'([A-Za-z_0-9]+)'",
          ).allMatches(texto))
            resolveParamKey(tipo, m.group(1)!),
        };

        // CHAVE MONTADA NA HORA. O Shake le os quatro eixos com o mesmo
        // codigo, trocando so o prefixo: '${eixo}_wave_amplitude'. Uma
        // busca por texto literal nao ve nenhuma dessas chaves e acusaria
        // vinte controles vivos de mortos.
        final sufixos = {
          for (final m in RegExp(
            r"(?:paramAt|track)\(\s*'\$\{\w+\}_"
            r"([A-Za-z_0-9]+)'",
          ).allMatches(texto))
            m.group(1)!,
        };

        for (final chave in ficha.params.keys) {
          final porSufixo = sufixos.any((suf) => chave.endsWith('_$suf'));
          if (!lidos.contains(chave) && !porSufixo) {
            mortos.add('$nome expoe "$chave" e nunca le');
          }
        }
      }

      // DIVIDA CONHECIDA, escrita para nao crescer em silencio.
      //
      // Sao controles que existem na ficha e ainda nao existem na conta.
      // Ficam listados um a um: assim a lista e uma lista de tarefas
      // visivel, e qualquer controle morto NOVO quebra o teste em vez de
      // se juntar a estes sem ninguem notar.
      const divida = <String>{};

      final novos = mortos.where((m) => !divida.contains(m)).toList();
      expect(novos, isEmpty, reason: novos.join('\n'));

      // A divida tambem nao pode ENVELHECER: item que ja foi resolvido
      // sai da lista, senao ela vira folclore.
      final resolvidos = divida.where((d) => !mortos.contains(d)).toList();
      expect(
        resolvidos,
        isEmpty,
        reason:
            'ja funciona, tire da divida:\n'
            '${resolvidos.join('\n')}',
      );
    });
  });
}
