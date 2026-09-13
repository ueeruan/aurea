// "O PAINEL DE EFEITOS CONTINUA EM PORTUGUES" (hmk, iPad, 13/09/2026).
//
// Os rotulos do painel nao nascem na apresentacao: vem do catalogo em
// `domain/effect.dart` (categoria, parametro, opcao, preset). Traduzir
// so os literais dos widgets deixava o catalogo inteiro em portugues.
// Este teste prende as duas pontas: todo rotulo do catalogo tem entrada
// no dicionario, e cada entrada tem os nove idiomas preenchidos.
import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:aurea/src/core/l10n/translations.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  final idiomas = appLanguages.keys.where((c) => c != 'pt').toList();

  test('todo rotulo do catalogo de efeitos esta no dicionario', () {
    final faltando = <String>{};
    void exige(String s) {
      if (s.trim().isEmpty) return;
      // Numero ou sigla pura nao precisa de traducao.
      if (!RegExp(r'[A-Za-zÀ-ÿ]{2,}').hasMatch(s)) return;
      if (!appTranslations.containsKey(s)) faltando.add(s);
    }

    for (final spec in effectSpecs.values) {
      exige(spec.name);
      exige(spec.category);
      for (final p in spec.params.values) {
        exige(p.label);
        p.options.forEach(exige);
      }
      for (final pronto in spec.presets) {
        exige(pronto.nome);
      }
    }
    expect(
      faltando,
      isEmpty,
      reason: 'rotulos do catalogo sem traducao: ${faltando.join(' | ')}',
    );
  });

  test('cada entrada do dicionario tem os nove idiomas', () {
    final furos = <String>[];
    for (final e in appTranslations.entries) {
      for (final c in idiomas) {
        if ((e.value[c] ?? '').trim().isEmpty) furos.add('${e.key} [$c]');
      }
    }
    expect(furos, isEmpty, reason: furos.take(20).join(' | '));
  });

  test('traduzir muda o rotulo fora do portugues e nao toca o portugues', () {
    expect(translateFor('pt', 'Efeitos'), 'Efeitos');
    expect(translateFor('en', 'Efeitos'), isNot('Efeitos'));
    // O que nao e chave passa intacto: conteudo do usuario nunca vira
    // outra coisa.
    expect(translateFor('en', 'meu texto qualquer 123'), 'meu texto qualquer 123');
  });
}
