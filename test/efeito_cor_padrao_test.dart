import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';

/// A COR COM QUE UM EFEITO NASCE.
///
/// Todo efeito guarda uma cor, e a de saida era o mesmo rosa generico
/// para todos. Nos efeitos que MODULAM a imagem com ela isso nao e um
/// detalhe de gosto: uma vinheta recem-criada pintava a borda do quadro
/// de VERMELHO, e os brilhos saiam tingidos de rosa — o usuario tinha de
/// descobrir o seletor e corrigir antes de ver o efeito fazer o que o
/// nome dele diz. A cor de saida tem de ser NEUTRA para a operacao:
/// preto para escurecer, branco para multiplicar.
void main() {
  const rosaGenerico = Color(0xFFFF5566);

  /// Os efeitos que MODULAM a imagem com `effect.color`. O seletor
  /// existe para todos (a cor e escolha de quem edita); o que se cobra
  /// aqui e so que o VALOR DE SAIDA nao altere a imagem sozinho.
  const modulam = [
    EffectType.vignette,
    EffectType.lightGlow,
    EffectType.glowVol,
    EffectType.lightRays,
    EffectType.fractalNoise,
  ];

  test('efeito que modula com a cor nao nasce no rosa generico', () {
    for (final tipo in modulam) {
      final spec = effectSpecs[tipo]!;
      expect(
        EffectInstance(type: tipo).color,
        isNot(rosaGenerico),
        reason: '${spec.id} sai tingido de rosa antes de alguem escolher',
      );
    }
  });

  test('a vinheta nasce preta; brilhos, raios e ruido nascem brancos', () {
    expect(EffectInstance(type: EffectType.vignette).color,
        const Color(0xFF000000));
    for (final tipo in [
      EffectType.lightGlow,
      EffectType.glowVol,
      EffectType.lightRays,
      EffectType.fractalNoise,
    ]) {
      expect(EffectInstance(type: tipo).color, const Color(0xFFFFFFFF),
          reason: effectSpecs[tipo]!.id);
    }
  });

  test('quem pede uma cor continua mandando nela', () {
    const roxo = Color(0xFFE070FF);
    expect(
      EffectInstance(type: EffectType.lightGlow, color: roxo).color,
      roxo,
    );
    expect(
      EffectInstance(type: EffectType.vignette, color: roxo).color,
      roxo,
    );
  });

  test('efeito COM seletor mantem a cor de saida visivel', () {
    expect(EffectInstance(type: EffectType.tint).color, rosaGenerico);
  });
}
