// A VARREDURA DE LUZ, MEDIDA NO PIXEL E CONTRA O RENDER DO AE.
//
// As referencias estao em `build/qa/ae-novos/R3_ls_*.png` — quatro
// variantes sobre um gradiente de canto a canto com dois quadrados. A
// medicao anterior tinha saido chapada porque a fonte era um solido liso
// de 48x48: nao havia o que a luz atravessasse.
//
// O QUE A MEDICAO DO AE DISSE, e o que estes testes cobram:
//
//   * a luz SOMA um valor absoluto — o mesmo +63 sobre cinza 102 e sobre
//     cinza 146, e nao um fator da cor de origem;
//   * o valor somado e `255 * Intensidade/100`;
//   * a faixa e uma RETA: dois pontos a mesma distancia perpendicular
//     recebem a mesma luz, por mais longe que estejam um do outro ao
//     longo da faixa;
//   * o perfil e QUADRATICO e cai de 1 no centro ate 0 em 2 vezes a
//     Largura: `(1 - d/semi)^2`, ajustado em 22 mil pixels de dois
//     renders com erro medio de 0,6 e 2,1 niveis num pico de 63.
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:flutter_test/flutter_test.dart';

const _lado = 128;

/// A ENTRADA DA BANCADA: metade de CIMA num cinza, a de baixo noutro.
///
/// DUAS BASES DIFERENTES de proposito: e o unico jeito de separar "soma
/// um valor" de "clareia proporcionalmente". Com uma cor so, as duas
/// hipoteses dao o mesmo numero, e o teste nao provaria nada.
///
/// A DIVISAO E HORIZONTAL porque a faixa do padrao e VERTICAL: dois pontos
/// de mesma altura estao a mesma distancia da faixa, e um em cima e outro
/// embaixo estao sobre bases diferentes — que e exatamente o par que
/// separa as duas hipoteses.
Future<ui.Image> _duasBases({
  int esquerda = 102,
  int direita = 146,
  int lado = _lado,
}) async {
  final pixels = Uint8List(lado * lado * 4);
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final i = (y * lado + x) * 4;
      final v = y < lado ~/ 2 ? esquerda : direita;
      pixels[i] = v;
      pixels[i + 1] = v;
      pixels[i + 2] = v;
      pixels[i + 3] = 255;
    }
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(pixels);
  final descritor = ui.ImageDescriptor.raw(
    buffer,
    width: lado,
    height: lado,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descritor.instantiateCodec();
  return (await codec.getNextFrame()).image;
}

class _Quadro {
  _Quadro(this.pixels, this.lado);
  final Uint8List pixels;
  final int lado;
  int r(int x, int y) => pixels[(y * lado + x) * 4];
}

/// DESENHA O SHADER do jeito que o passe o monta.
///
/// A ORDEM DOS `setFloat` E A DA DECLARACAO no `.frag`. Reordenar os
/// uniforms la sem mexer aqui faz este teste medir outra coisa.
Future<_Quadro> _desenhar({
  required ui.Image fonte,
  required double centroX,
  required double centroY,
  required double angulo,
  required double largura,
  required double intensidade,
  double recepcao = 0,
  double r = 1,
  double g = 0.98,
  double b = 0.941,
  int lado = _lado,
}) async {
  final programa = await ui.FragmentProgram.fromAsset(
    'shaders/luz_na_faixa.frag',
  );
  final shader = programa.fragmentShader();
  try {
    final rad = angulo * math.pi / 180;
    shader
      ..setFloat(0, lado.toDouble())
      ..setFloat(1, lado.toDouble())
      ..setFloat(2, lado.toDouble()) // uLado
      ..setFloat(3, lado.toDouble())
      ..setFloat(4, centroX * lado) // uCentro
      ..setFloat(5, centroY * lado)
      ..setFloat(6, math.cos(rad)) // uNormal
      ..setFloat(7, math.sin(rad))
      ..setFloat(8, r) // uCor
      ..setFloat(9, g)
      ..setFloat(10, b)
      ..setFloat(11, 1)
      ..setFloat(12, largura)
      ..setFloat(13, intensidade)
      ..setFloat(14, recepcao)
      ..setImageSampler(0, fonte);
    final gravador = ui.PictureRecorder();
    ui.Canvas(gravador).drawRect(
      ui.Rect.fromLTWH(0, 0, lado.toDouble(), lado.toDouble()),
      ui.Paint()..shader = shader,
    );
    final imagem = await gravador.endRecording().toImage(lado, lado);
    final dados = await imagem.toByteData(format: ui.ImageByteFormat.rawRgba);
    imagem.dispose();
    return _Quadro(dados!.buffer.asUint8List(), lado);
  } finally {
    shader.dispose();
  }
}

void main() {
  group('a ficha no catalogo', () {
    test('esta registrada com o id do After Effects', () {
      final spec = effectSpecs[EffectType.lightSweep];
      expect(spec, isNotNull);
      expect(spec!.id, 'cc_light_sweep');
      expect(spec.name, 'Varredura de luz');
      expect(spec.category, 'Light');
      expect(spec.hasColor, isTrue);
    });

    test('a cor padrao e o branco quente do AE, e nao branco puro', () {
      final c = effectSpecs[EffectType.lightSweep]!.defaultColor;
      expect(c.r, closeTo(1.0, 0.001));
      expect(c.g, closeTo(0.9804, 0.001));
      expect(c.b, closeTo(0.9412, 0.001));
    });

    test('a intensidade nasce em 25%, como no AE', () {
      expect(effectSpecs[EffectType.lightSweep]!.params['intensidade']!.initial, 25);
    });
  });

  group('o shader, lido pixel a pixel', () {
    // A FAIXA DO PADRAO E VERTICAL: com a direcao em 0 a normal e (1,0), e
    // o que decide a luz de um pixel e a distancia dele em X ate o centro.
    // Foi o que o render do AE mostrou — a mesma luz em toda a altura.
    testWidgets('a luz soma o MESMO valor sobre duas bases diferentes',
        (tester) async {
      await tester.runAsync(() async {
        final fonte = await _duasBases();
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 0,
          largura: 50,
          intensidade: 0.25,
        );
        // (20,20) esta na base 102 e (20,100) na base 146 — mesma coluna,
        // logo a mesma distancia ate a faixa, e bases diferentes.
        final sobre102 = quadro.r(20, 20) - 102;
        final sobre146 = quadro.r(20, 100) - 146;
        expect(sobre102, sobre146,
            reason: 'a luz tem de somar o mesmo valor nas duas bases');
        // semi-extensao 50*2 = 100; o pixel 20 tem centro em 20,5, a 43,5
        // do centro em 64.
        expect(sobre102, closeTo(63.75 * math.pow(1 - 43.5 / 100, 2), 4));
      });
    });

    testWidgets('a faixa e uma RETA, e nao uma mancha', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _duasBases(esquerda: 102, direita: 102);
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 0,
          largura: 50,
          intensidade: 0.25,
        );
        // Mesma coluna, alturas bem diferentes: numa mancha redonda o
        // valor cairia com a distancia ao centro; numa reta, nao.
        final a = quadro.r(64, 4);
        final b = quadro.r(64, 60);
        final c = quadro.r(64, 124);
        expect(a, b);
        expect(b, c);
        expect(a - 102, closeTo(64, 5));
      });
    });

    testWidgets('o perfil cai ate zero em 2 vez a Largura', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _duasBases(esquerda: 102, direita: 102);
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 0,
          largura: 20,
          intensidade: 0.25,
        );
        // Largura 20 -> semi-extensao 40 px em volta de x=64.
        expect(quadro.r(64, 20) - 102, closeTo(62, 3), reason: 'no centro');
        expect(quadro.r(74, 20) - 102, closeTo(35, 3), reason: 'no quarto');
        expect(quadro.r(94, 20) - 102, closeTo(4, 3), reason: 'na borda');
        expect(quadro.r(110, 20) - 102, lessThan(2), reason: 'fora');
      });
    });

    testWidgets('a direcao gira a faixa', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _duasBases(esquerda: 102, direita: 102);
        // Com a direcao em 90 a normal vira (0,1) e a faixa fica
        // HORIZONTAL: o que decide a luz passa a ser y, e x deixa de
        // importar.
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 90,
          largura: 20,
          intensidade: 0.25,
        );
        expect(quadro.r(4, 64), quadro.r(60, 64));
        expect(quadro.r(60, 64), quadro.r(124, 64));
        expect(quadro.r(64, 4) - 102, lessThan(3),
            reason: 'e o que estava claro na vertical agora esta fora');
      });
    });

    testWidgets('intensidade zero nao muda um pixel', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _duasBases();
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 0,
          largura: 50,
          intensidade: 0,
        );
        expect(quadro.r(64, 10), 102);
        expect(quadro.r(64, 100), 146);
      });
    });

    testWidgets('o alfa da camada atravessa o efeito', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _duasBases();
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 0,
          largura: 50,
          intensidade: 0.25,
        );
        expect(quadro.pixels[(64 * _lado + 64) * 4 + 3], 255);
      });
    });

    testWidgets('recepcao em Tela nao estoura onde a base ja e clara',
        (tester) async {
      await tester.runAsync(() async {
        final fonte = await _duasBases(esquerda: 240, direita: 240);
        final somar = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 0,
          largura: 50,
          intensidade: 0.25,
        );
        final tela = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: 0,
          largura: 50,
          intensidade: 0.25,
          recepcao: 1,
        );
        // Somar parte de 240 e bate no teto de 255; tela preserva o que
        // sobrou da cor de baixo.
        expect(somar.r(64, 20), 255);
        expect(tela.r(64, 20), lessThan(255));
        expect(tela.r(64, 20), greaterThan(240));
      });
    });
  });
}
