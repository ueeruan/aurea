// O RGB NO TEMPO — a conta, a matriz de cada canal, e a ligacao no palco.
//
// OS PARAMETROS SAO DO PLUGIN, MEDIDOS: os nomes e os valores de fabrica
// (Red Shift Frames 1, Green 0, Blue -1) sairam do S_TimeWarpRGB pelo
// painel do AE. O QUE NAO DEU PARA MEDIR esta escrito na ficha: com o
// efeito ligado o render do AE sai byte a byte igual ao render com ele
// desligado — o Sapphire nao renderiza por `saveFrameToPng` nesta maquina.
// Entao o sinal do deslocamento e convencao nossa, e o teste cobra a
// convencao declarada, nao o plugin.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/time_slice.dart';
import 'package:aurea/src/features/editor/domain/time_warp_rgb.dart';
import 'package:aurea/src/features/editor/presentation/widgets/blend_mask.dart';
import 'package:aurea/src/features/editor/presentation/widgets/time_warp_rgb_pass.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_test/flutter_test.dart';

EffectInstance _efeito({
  double r = 1,
  double g = 0,
  double b = -1,
  double mix = 100,
}) => EffectInstance(
  type: EffectType.timeWarpRgb,
  params: {
    'desloc_r': AnimatedDouble(r),
    'desloc_g': AnimatedDouble(g),
    'desloc_b': AnimatedDouble(b),
    'mix': AnimatedDouble(mix),
  },
);

Layer _camada([Duration duracao = const Duration(seconds: 4)]) => ShapeLayer(
  name: 'bancada',
  startTime: Duration.zero,
  duration: duracao,
  contents: [ShapeFill(color: const Color(0xFFFFFFFF))],
);

void main() {
  group('os deslocamentos', () {
    test('sao os do plugin, em quadros inteiros', () {
      final d = deslocamentosDoTimeWarp(_efeito(), Duration.zero);
      expect(d.r, 1);
      expect(d.g, 0);
      expect(d.b, -1);
    });

    test('arredondam e nao estouram o teto', () {
      final d = deslocamentosDoTimeWarp(
        _efeito(r: 2.4, g: -2.6, b: 900),
        Duration.zero,
      );
      expect(d.r, 2);
      expect(d.g, -3);
      expect(d.b, 240, reason: 'o teto existe para nao pedir 900 quadros');
    });

    test('sem deslocamento nenhum o efeito e identidade', () {
      expect(
        timeWarpEhIdentidade(
          deslocamentosDoTimeWarp(_efeito(r: 0, g: 0, b: 0), Duration.zero),
        ),
        isTrue,
      );
      expect(
        timeWarpEhIdentidade(
          deslocamentosDoTimeWarp(_efeito(), Duration.zero),
        ),
        isFalse,
      );
    });
  });

  group('os instantes que a camada mostra', () {
    test('sao tres, um por canal, e o sinal e o declarado', () {
      final camada = _camada();
      final instantes = instantesDoTimeWarp(
        layer: camada,
        local: const Duration(seconds: 2),
        deslocamentos: (r: 2, g: 0, b: -2),
        fps: 25,
      );
      const quadro = Duration(microseconds: 40000);
      expect(instantes.length, 3);
      expect(instantes, {
        const Duration(seconds: 2) + quadro * 2,
        const Duration(seconds: 2),
        const Duration(seconds: 2) - quadro * 2,
      });
    });

    test('prendem nas pontas da camada, como o Time Slice', () {
      final camada = _camada(const Duration(seconds: 1));
      final instantes = instantesDoTimeWarp(
        layer: camada,
        local: Duration.zero,
        deslocamentos: (r: 0, g: 0, b: -10),
        fps: 30,
      );
      // O azul pediria um tempo negativo: vale o primeiro quadro, e nao
      // um instante fora da camada.
      expect(instantes.every((i) => i >= Duration.zero), isTrue);
      expect(instantes.length, 1, reason: 'as tres contas caem no mesmo 0');
    });

    test('o instante e o da COMPOSICAO, com o inicio da camada somado', () {
      final camada = ShapeLayer(
        name: 'atrasada',
        startTime: const Duration(seconds: 3),
        duration: const Duration(seconds: 4),
        contents: [ShapeFill(color: const Color(0xFFFFFFFF))],
      );
      final instantes = instantesDoTimeWarp(
        layer: camada,
        local: const Duration(seconds: 1),
        deslocamentos: (r: 1, g: 0, b: 0),
        fps: 30,
      );
      // A camada comeca em 3s e o instante local e 1s: tudo o que sai
      // daqui e 3s + 1s (+ o deslocamento), e nunca o instante LOCAL
      // sozinho — quem soma o inicio e esta funcao.
      expect(instantes, {
        const Duration(seconds: 4),
        const Duration(seconds: 4) + const Duration(microseconds: 33333),
      });
    });
  });

  group('a matriz de cada canal', () {
    test('deixa passar um canal so, e o alfa inteiro', () {
      for (final c in [0, 1, 2]) {
        final m = matrizDoCanal(c);
        expect(m.length, 20);
        // A linha do canal c tem de reproduzir o proprio valor...
        expect(m[c * 5 + c], 1, reason: 'canal $c');
        // ...e as outras duas linhas de cor tem de zerar tudo.
        for (final outro in [0, 1, 2]) {
          if (outro == c) continue;
          for (var k = 0; k < 5; k++) {
            expect(m[outro * 5 + k], 0, reason: 'canal $c, linha $outro');
          }
        }
        // A linha do alfa passa o alfa e nao mexe em cor.
        expect(m[15], 0);
        expect(m[16], 0);
        expect(m[17], 0);
        expect(m[18], 1);
        expect(m[19], 0);
      }
    });

    testWidgets('e o motor obedece a matriz: so o canal pedido sobrevive',
        (tester) async {
      // Uma cor de teste com os tres canais BEM diferentes.
      const cor = Color(0xFF3366CC);
      for (final c in [0, 1, 2]) {
        final chave = GlobalKey();
        await tester.pumpWidget(
          Directionality(
            textDirection: TextDirection.ltr,
            child: Center(
              child: RepaintBoundary(
                key: chave,
                child: ColorFiltered(
                  colorFilter: ui.ColorFilter.matrix(matrizDoCanal(c)),
                  child: const SizedBox(
                    width: 20,
                    height: 20,
                    child: ColoredBox(color: cor),
                  ),
                ),
              ),
            ),
          ),
        );
        await tester.pumpAndSettle();
        late List<int> px;
        await tester.runAsync(() async {
          final im = await (chave.currentContext!.findRenderObject()!
                  as RenderRepaintBoundary)
              .toImage();
          final d = await im.toByteData(format: ui.ImageByteFormat.rawRgba);
          final b = d!.buffer.asUint8List();
          px = [b[0], b[1], b[2], b[3]];
        });
        final esperado = [0x33, 0x66, 0xCC];
        for (var k = 0; k < 3; k++) {
          expect(px[k], k == c ? esperado[k] : 0,
              reason: 'canal $c, componente $k');
        }
        expect(px[3], 255, reason: 'o alfa nao pode ser comido pelo filtro');
      }
    });
  });

  group('a ligacao no palco', () {
    testWidgets('monta tres canais e soma dois deles', (tester) async {
      final tempos = <Duration>[];
      await tester.pumpWidget(
        Directionality(
          textDirection: TextDirection.ltr,
          child: TimeWarpRgbPass(
            effect: _efeito(r: 2, g: 0, b: -2),
            time: Duration.zero,
            child: const SizedBox(width: 10, height: 10),
            emTempo: (t) {
              tempos.add(t);
              return const SizedBox(width: 10, height: 10);
            },
            tempoDeslocado: (q) => Duration(milliseconds: q * 40),
          ),
        ),
      );
      await tester.pumpAndSettle();
      // O VERDE nao pede montagem nenhuma: ele e o quadro atual.
      expect(tempos, [const Duration(milliseconds: 80),
                      const Duration(milliseconds: -80)]);
      expect(find.byType(BlendMask), findsNWidgets(2));
      final somas = tester
          .widgetList<BlendMask>(find.byType(BlendMask))
          .map((b) => b.blendMode);
      expect(somas.every((m) => m == BlendMode.plus), isTrue,
          reason: 'canal somado e soma; com srcOver o ultimo taparia os outros');
    });

    testWidgets('sem deslocamento a camada passa intacta', (tester) async {
      var montou = 0;
      await tester.pumpWidget(
        Directionality(
          textDirection: TextDirection.ltr,
          child: TimeWarpRgbPass(
            effect: _efeito(r: 0, g: 0, b: 0),
            time: Duration.zero,
            child: const ColoredBox(color: Color(0xFFFFFFFF)),
            emTempo: (t) {
              montou++;
              return const SizedBox();
            },
            tempoDeslocado: (q) => Duration(milliseconds: q * 40),
          ),
        ),
      );
      // Nenhuma montagem extra, nenhum canal, nenhuma foto: o custo de
      // passar por aqui com o efeito neutro seria tres quadros por nada.
      expect(montou, 0);
      expect(find.byType(BlendMask), findsNothing);
      expect(find.byType(ColorFiltered), findsNothing);
      expect(find.byType(ColoredBox), findsOneWidget);
    });

    testWidgets('mistura zero tambem devolve a camada intacta', (tester) async {
      await tester.pumpWidget(
        Directionality(
          textDirection: TextDirection.ltr,
          child: TimeWarpRgbPass(
            effect: _efeito(mix: 0),
            time: Duration.zero,
            child: const ColoredBox(color: Color(0xFFFFFFFF)),
            emTempo: (t) => const SizedBox(),
            tempoDeslocado: (q) => Duration(milliseconds: q * 40),
          ),
        ),
      );
      expect(find.byType(BlendMask), findsNothing);
    });
  });

  group('a ficha', () {
    test('esta registrada com o id do plugin e os valores de fabrica', () {
      final spec = effectSpecs[EffectType.timeWarpRgb];
      expect(spec, isNotNull);
      expect(spec!.id, 's_timewarp_rgb');
      expect(spec.name, 'RGB no tempo');
      expect(spec.params['desloc_r']!.initial, 1);
      expect(spec.params['desloc_g']!.initial, 0);
      expect(spec.params['desloc_b']!.initial, -1);
    });

    test('TODAS as chaves que o passe le existem na ficha', () {
      const lidas = ['desloc_r', 'desloc_g', 'desloc_b', 'mix'];
      final spec = effectSpecs[EffectType.timeWarpRgb]!;
      for (final chave in lidas) {
        expect(spec.params.containsKey(chave), isTrue,
            reason: 'o passe le "$chave" e a ficha nao tem essa chave');
      }
    });

    test('os presets usam as chaves que existem na ficha', () {
      final spec = effectSpecs[EffectType.timeWarpRgb]!;
      for (final pronto in spec.presets) {
        for (final chave in pronto.valores.keys) {
          expect(spec.params.containsKey(chave), isTrue,
              reason: 'preset "${pronto.nome}" mexe em "$chave"');
        }
      }
    });
  });
}
