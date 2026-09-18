import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';

/// A DIVERGENCIA ZERO — o avaliador C++ contra o avaliador Dart.
///
/// O RENDERCORE NAO PODE INVENTAR A ANIMACAO. Se o C++ avaliar diferente
/// do Dart, o preview (C++) e a exportacao (que ainda e Dart) discordam —
/// e a discordancia so aparece no arquivo final, depois de uma hora de
/// render. Este teste e o portao do estagio S2 do plano (docs/nucleo-cpp.md):
/// mesma pilha, mesmo instante, mesmo numero.
///
/// AS CURVAS AINDA NAO COPIADAS SAO COBRADAS COMO NAO COPIADAS. O
/// avaliador responde se espelhou a curva; exigir `espelhado == false`
/// onde ele nao copiou e o que impede uma curva nova no Dart de entrar
/// silenciosamente como "linear" no C++.
void main() {
  /// A LISTA DE VALORES QUE O TESTE VARREDURA. Pontas, meio e instantes
  /// que caem exatamente em cima de um keyframe — o caso em que a busca
  /// binaria do C++ e do Dart precisa concordar sobre QUAL trecho vale.
  const instantes = <double>[
    0.0, 0.0001, 0.05, 0.1, 0.149, 0.15, 0.2, 0.33, 0.5,
    0.66, 0.75, 0.9, 0.999, 1.0, 1.2, 1.5, 2.0, 3.7,
  ];

  CurvaDeRender daDart(Easing e) => CurvaDeRender(
    tipo: EasingType.values.indexOf(e.type),
    x1: e.x1,
    y1: e.y1,
    x2: e.x2,
    y2: e.y2,
    contagem: e.count,
    suavidade: e.smooth,
    intensidade: e.intensity,
    resposta: e.response,
    amortecimento: e.damping,
    velocidadeInicial: e.initialVelocity,
  );

  /// COMPARA UMA PILHA INTEIRA, INSTANTE A INSTANTE.
  void compararPilha(String nome, List<Keyframe<double>> quadros) {
    final animado = AnimatedDouble(0, quadros);
    final paraOCpp = [
      for (final k in quadros)
        KeyframeDeRender(
          tempoS: k.time.inMicroseconds / 1e6,
          valor: k.value,
          curva: daDart(k.ease),
        ),
    ];

    for (final t in instantes) {
      final esperado = animado.valueAt(
        Duration(microseconds: (t * 1e6).round()),
      );
      final r = avaliarNoNucleo(paraOCpp, t);
      if (!r.espelhado) continue;
      expect(
        r.valor,
        closeTo(esperado, 1e-9),
        reason: '$nome em t=$t',
      );
    }
  }

  test('o C++ e o Dart dao o mesmo numero em toda curva espelhada', () {
    // UMA CURVA DE CADA TIPO ESPELHADO, no meio de uma pilha com valores
    // e espacamentos diferentes — uma curva so nao pega o erro de
    // segmento, e um espacamento so nao pega o erro de fracao.
    final curvas = <String, Easing>{
      'linear': Easing.linear,
      'bezier suave': const Easing(
        type: EasingType.cubicBezier,
        x1: 0.42,
        y1: 0,
        x2: 0.58,
        y2: 1,
      ),
      'bezier assimetrica': const Easing(
        type: EasingType.cubicBezier,
        x1: 0.9,
        y1: 0.1,
        x2: 0.1,
        y2: 0.9,
      ),
      'quicar': const Easing(type: EasingType.bounce, count: 3, intensity: 0.7),
      'elastico': const Easing(
        type: EasingType.elastic,
        count: 4,
        intensity: 0.4,
      ),
      'ciclico': const Easing(type: EasingType.cyclic, count: 3, smooth: 0.8),
      'aleatorio': const Easing(type: EasingType.random, intensity: 0.9),
      'degraus': const Easing(type: EasingType.steps, count: 5),
      'mola': const Easing(
        type: EasingType.spring,
        response: 0.6,
        damping: 0.7,
      ),
      'segurar': const Easing(type: EasingType.hold),
    };

    curvas.forEach((nome, curva) {
      compararPilha('$nome, tres keyframes', [
        const Keyframe(time: Duration.zero, value: 10, ease: Easing.linear),
        Keyframe(
          time: const Duration(milliseconds: 700),
          value: -4.5,
          ease: curva,
        ),
        const Keyframe(time: Duration(milliseconds: 1500), value: 120),
      ]);
      compararPilha('$nome, dois keyframes', [
        const Keyframe(time: Duration.zero, value: 0, ease: Easing.linear),
        Keyframe(
          time: const Duration(milliseconds: 1000),
          value: 1,
          ease: curva,
        ),
      ]);
    });
  });

  test('as curvas que ainda nao foram copiadas se dizem nao copiadas', () {
    // A CONTRAPARTE DA REGRA. Se alguem "consertar" o C++ fazendo-o
    // devolver linear em silencio para estas, este teste cai — e o
    // portao S2 nunca fecha com uma curva disfarcada.
    final naoEspelhadas = <EasingType>[
      EasingType.elasticSteps,
      EasingType.bounceIn,
      EasingType.elasticIn,
      EasingType.stepsRandom,
      EasingType.oscillate,
      EasingType.repeat,
      EasingType.sawtooth,
    ];
    for (final tipo in naoEspelhadas) {
      // A CURVA DO TRECHO E A DO KEYFRAME DA ESQUERDA — a mesma regra do
      // Dart (`_segmentAt` usa `a.ease`). Poe-la no keyframe da direita
      // avaliaria a curva padrao, e o teste passaria sem testar nada.
      final r = avaliarNoNucleo([
        KeyframeDeRender(
          tempoS: 0,
          valor: 0,
          curva: CurvaDeRender(tipo: EasingType.values.indexOf(tipo)),
        ),
        const KeyframeDeRender(tempoS: 1, valor: 10),
      ], 0.5);
      expect(
        r.espelhado,
        isFalse,
        reason: '${tipo.name} nao esta copiada e tem de dizer isso',
      );
    }
  });

  test('o valor de fora do trecho e a ponta, como no Dart', () {
    final quadros = [
      const KeyframeDeRender(tempoS: 0.2, valor: 5),
      const KeyframeDeRender(tempoS: 0.8, valor: 9),
    ];
    expect(avaliarNoNucleo(quadros, 0.0).valor, 5);
    expect(avaliarNoNucleo(quadros, 0.2).valor, 5);
    expect(avaliarNoNucleo(quadros, 0.8).valor, 9);
    expect(avaliarNoNucleo(quadros, 5.0).valor, 9);
    // SEM KEYFRAME NENHUM, VALE A BASE — e a mesma regra do `AnimatedDouble`.
    expect(avaliarNoNucleo(const [], 0.5, base: 42).valor, 42);
    expect(avaliarNoNucleo([quadros.first], 0.5).valor, 5);
  });

  test('uma curva do C++ bate com o transform do Dart, uma a uma', () {
    // A CURVA SEPARADA, e nao dentro de uma interpolacao: aqui um erro de
    // 1% aparece na hora, e dentro de um trecho de 10 unidades ele sumiria
    // no arredondamento do `closeTo`.
    final curvas = [
      Easing.linear,
      const Easing(type: EasingType.cubicBezier, x1: 0.25, y1: 0.1, x2: 0.25, y2: 1),
      const Easing(type: EasingType.bounce, count: 2, intensity: 0.3),
      const Easing(type: EasingType.elastic, count: 2, intensity: 0.6),
      const Easing(type: EasingType.cyclic, count: 2, smooth: 0.3),
      const Easing(type: EasingType.random, intensity: 0.5),
      const Easing(type: EasingType.steps, count: 4),
      const Easing(type: EasingType.spring, response: 0.9, damping: 0.6),
      const Easing(type: EasingType.hold),
    ];
    for (final c in curvas) {
      for (var i = 0; i <= 40; i++) {
        final t = i / 40;
        expect(
          transformarCurvaNoNucleo(daDart(c), t),
          closeTo(c.transform(t), 1e-9),
          reason: '${c.type.name} em t=$t',
        );
      }
    }
  });

  test('o avaliador aguenta um corpus grande sem escorregar', () {
    // UMA VARREDURA MAIOR, com valores e instantes sorteados de forma
    // REPETIVEL (semente fixa): se um dia divergir, o teste diz o
    // instante exato e o mesmo numero aparece na proxima execucao.
    final sorteio = math.Random(20260917);
    for (var caso = 0; caso < 40; caso++) {
      final n = 2 + sorteio.nextInt(5);
      final quadros = <Keyframe<double>>[];
      var t = 0;
      for (var i = 0; i < n; i++) {
        t += 1 + sorteio.nextInt(900);
        quadros.add(
          Keyframe(
            time: Duration(milliseconds: t),
            value: (sorteio.nextDouble() - 0.5) * 500,
            ease: Easing(
              type: EasingType.values[sorteio.nextInt(9)],
              x1: sorteio.nextDouble(),
              y1: sorteio.nextDouble(),
              x2: sorteio.nextDouble(),
              y2: sorteio.nextDouble(),
              count: 1 + sorteio.nextInt(6),
              smooth: sorteio.nextDouble(),
              intensity: sorteio.nextDouble(),
              response: 0.05 + sorteio.nextDouble() * 5,
              damping: sorteio.nextDouble() * 4,
            ),
          ),
        );
      }
      compararPilha('caso $caso', quadros);
    }
  });
}
