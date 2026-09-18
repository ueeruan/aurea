// A DOBRA DE PAGINA — a geometria conferida sem GPU, e o desenho lido no
// pixel.
//
// AS REFERENCIAS DO AE estao em `build/qa/ae-novos/R3_pt_*.png`, com um
// gradiente de canto a canto e dois quadrados (a fonte lisa de 48x48 da
// medicao anterior nao tinha o que a dobra cruzasse, e os renders sairam
// chapados).
//
// O QUE A MEDICAO CONFIRMOU, e o que estes testes cobram:
//
//   * o vinco e uma RETA e o lado plano fica INTACTO — a mudanca comeca
//     exatamente na reta, sem um pixel alterado do outro lado;
//   * o rolo COMPRIME o desenho e o projeta de volta por cima do plano. No
//     render, o canto inferior direito do quadrado azul saiu de (167,167)
//     para (163,140): mais perto do vinco do que estava. E a assinatura de
//     um cilindro, e nao de um deslocamento;
//   * ha um REFLEXO correndo pela dobra, e ele nao esta no meio dela.
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/dobra_de_pagina.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:flutter_test/flutter_test.dart';

const _lado = 128;

/// A ENTRADA DA BANCADA: listras verticais de 16 px.
///
/// LISTRAS, e nao uma cor lisa: com elas a compressao do rolo vira um
/// numero — o periodo medido na tela contra o periodo de origem. Uma cor
/// lisa esconderia qualquer erro de mapeamento.
Future<ui.Image> _listras({int lado = _lado, int passo = 16}) async {
  final pixels = Uint8List(lado * lado * 4);
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final i = (y * lado + x) * 4;
      final v = (x ~/ passo) % 2 == 0 ? 60 : 220;
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
  int a(int x, int y) => pixels[(y * lado + x) * 4 + 3];
}

/// DESENHA O SHADER do jeito que o passe o monta.
///
/// A ORDEM DOS `setFloat` E A DA DECLARACAO no `.frag`.
Future<_Quadro> _desenhar({
  required ui.Image fonte,
  required double centroX,
  required double centroY,
  required double angulo,
  required double raio,
  double luz = -60,
  double verso = 0.85,
  double brilho = 1,
  int lado = _lado,
}) async {
  final programa = await ui.FragmentProgram.fromAsset(
    'shaders/dobra_de_pagina.frag',
  );
  final shader = programa.fragmentShader();
  try {
    final normal = normalDaDobra(angulo);
    shader
      ..setFloat(0, lado.toDouble())
      ..setFloat(1, lado.toDouble())
      ..setFloat(2, lado.toDouble()) // uLado
      ..setFloat(3, lado.toDouble())
      ..setFloat(4, centroX * lado) // uCentro
      ..setFloat(5, centroY * lado)
      ..setFloat(6, normal.dx) // uNormal
      ..setFloat(7, normal.dy)
      ..setFloat(8, 0.769) // uCor (cinza-papel do AE)
      ..setFloat(9, 0.769)
      ..setFloat(10, 0.706)
      ..setFloat(11, 1)
      ..setFloat(12, raio)
      ..setFloat(13, luz)
      ..setFloat(14, verso)
      ..setFloat(15, brilho)
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
  group('a normal do vinco', () {
    test('e perpendicular ao angulo pedido', () {
      final n = normalDaDobra(0);
      expect(n.dx, closeTo(-0.0, 1e-9));
      expect(n.dy, closeTo(1, 1e-9));
      final n90 = normalDaDobra(90);
      expect(n90.dx, closeTo(-1, 1e-9));
      expect(n90.dy, closeTo(0, 1e-9));
    });

    test('girar o angulo em 180 inverte o lado que enrola', () {
      final a = normalDaDobra(-60);
      final b = normalDaDobra(120);
      expect(b.dx, closeTo(-a.dx, 1e-9));
      expect(b.dy, closeTo(-a.dy, 1e-9));
    });
  });

  group('a conta da dobra, em Dart', () {
    final centro = Offset.zero;
    final normal = Offset(-math.sin(-math.pi / 3), math.cos(-math.pi / 3));
    const raio = 50.0;
    const junto = 0.5;

    test('o lado plano nao anda um pixel', () {
      for (final p in const [
        Offset(-40, 10),
        Offset(-1, -80),
        Offset(-0.5, junto),
      ]) {
        final q = ondeAparece(
          ponto: p,
          centro: centro,
          normal: normal,
          raio: raio,
        );
        expect(q, p);
      }
    });

    test('o lado do rolo COMPRIME: a folha se aproxima do vinco', () {
      // Um ponto a 20 da dobra aparece mais perto que 20 — e a diferenca
      // entre o arco e a corda do cilindro.
      final q = ondeAparece(
        ponto: normal * 20,
        centro: centro,
        normal: normal,
        raio: raio,
      )!;
      final distancia = q.dx * normal.dx + q.dy * normal.dy;
      expect(distancia, lessThan(20));
      expect(distancia, closeTo(raio * math.sin(20 / raio), 1e-9));
    });

    test('quanto mais longe da dobra, maior a compressao', () {
      double aparente(double u) {
        final q = ondeAparece(
          ponto: normal * u,
          centro: centro,
          normal: normal,
          raio: raio,
        )!;
        return q.dx * normal.dx + q.dy * normal.dy;
      }

      var anterior = 0.0;
      var perda = -1.0;
      for (var u = 5.0; u < raio * math.pi / 2; u += 5) {
        final a = aparente(u);
        expect(a, greaterThan(anterior), reason: 'a ordem tem de se manter');
        final nova = (u - a) / u;
        expect(nova, greaterThan(perda), reason: 'a compressao so cresce');
        perda = nova;
        anterior = a;
      }
    });

    test('o que passou do topo do rolo nao cobre mais nada', () {
      final acima = raio * math.pi / 2 + 1;
      expect(
        ondeAparece(
          ponto: normal * acima,
          centro: centro,
          normal: normal,
          raio: raio,
        ),
        isNull,
      );
      // e exatamente no topo ainda cobre
      expect(
        ondeAparece(
          ponto: normal * (raio * math.pi / 2 - 0.01),
          centro: centro,
          normal: normal,
          raio: raio,
        ),
        isNotNull,
      );
    });

    test('o ponto ao longo do vinco nao se mexe', () {
      // A dobra nao desloca nada na direcao do proprio vinco: so comprime
      // a distancia ate ele.
      final longo = Offset(-normal.dy, normal.dx) * 33;
      final q = ondeAparece(
        ponto: normal * 20 + longo,
        centro: centro,
        normal: normal,
        raio: raio,
      )!;
      final aoLongo = q.dx * (-normal.dy) + q.dy * normal.dx;
      expect(aoLongo, closeTo(33, 1e-9));
    });

    test('as duas contas sao uma o inverso da outra', () {
      for (final u in const [2.0, 10.0, 30.0, 60.0]) {
        final fonte = normal * u;
        final tela = ondeAparece(
          ponto: fonte,
          centro: centro,
          normal: normal,
          raio: raio,
        )!;
        final volta = pontoQueAparece(
          tela: tela,
          centro: centro,
          normal: normal,
          raio: raio,
        )!;
        expect(volta.dx, closeTo(fonte.dx, 1e-6));
        expect(volta.dy, closeTo(fonte.dy, 1e-6));
      }
    });
  });

  group('a ficha no catalogo', () {
    test('esta registrada com o id do After Effects', () {
      final spec = effectSpecs[EffectType.dobraDePagina];
      expect(spec, isNotNull);
      expect(spec!.id, 'cc_page_turn');
      expect(spec.name, 'Dobra de página');
      expect(spec.category, 'Distort');
      expect(spec.hasColor, isTrue);
    });

    test('a cor padrao e o cinza-papel do AE', () {
      final c = effectSpecs[EffectType.dobraDePagina]!.defaultColor;
      expect(c.r, closeTo(0.769, 0.002));
      expect(c.g, closeTo(0.769, 0.002));
      expect(c.b, closeTo(0.706, 0.002));
    });

    test('o raio pequeno demais e identidade', () {
      final normal = normalDaDobra(0);
      for (final r in const [0.0, -3.0, 0.4]) {
        expect(
          ondeAparece(
            ponto: const Offset(90, 10),
            centro: Offset.zero,
            normal: normal,
            raio: r,
          ),
          const Offset(90, 10),
        );
      }
    });
  });

  group('o shader, lido pixel a pixel', () {
    testWidgets('o lado plano sai igual a origem', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _listras();
        final quadro = await _desenhar(
          fonte: fonte,
          // O vinco em x=100 com a normal em (1,0): tudo a esquerda fica
          // plano.
          centroX: 80 / _lado,
          centroY: 0.5,
          angulo: -90,
          raio: 40,
        );
        // O lado plano e x < 80: em 90 o pixel ja caiu DENTRO do rolo.
        for (final x in [0, 20, 45, 70]) {
          expect(quadro.r(x, 64), (x ~/ 16) % 2 == 0 ? 60 : 220,
              reason: 'x=$x esta do lado plano e nao pode mudar');
        }
      });
    });

    testWidgets('o lado do rolo comprime o desenho', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _listras();
        final centro = const Offset(80, 64);
        final normal = normalDaDobra(-90);
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: centro.dx / _lado,
          centroY: 0.5,
          angulo: -90,
          raio: 40,
          // SEM REFLEXO: com ele um pixel escuro pode ser aceso pelo
          // brilho especular e passar por claro.
          brilho: 0,
        );
        // Para uma amostra de pixels do lado do rolo, o shader tem de
        // mostrar o que estava no ponto que a conta em Dart aponta — e
        // esse ponto esta sempre MAIS LONGE do vinco do que o pixel.
        var amostras = 0;
        for (var d = 3.0; d < 39; d += 4) {
          final x = (centro.dx + d).round();
          final y = 64;
          final origem = pontoQueAparece(
            tela: Offset(x.toDouble(), y.toDouble()),
            centro: centro,
            normal: normal,
            raio: 40,
          )!;
          expect(
            origem.dx,
            greaterThan(x.toDouble()),
            reason: 'o ponto de origem tem de estar mais longe do vinco',
          );
          final claro = (origem.dx.round() ~/ 16) % 2 != 0;
          // O SOMBREAMENTO DO CILINDRO escurece o rolo inteiro (0,55 a
          // 1,0) e o valor exato nao serve de prova. O que se cobra e o
          // LADO da listra: sem reflexo, a mais escura das claras fica em
          // 220*0,55 = 121 e a mais clara das escuras em 60 — o corte em
          // 90 separa as duas sem margem para duvida.
          final lido = quadro.r(x, y);
          expect(lido > 90, claro,
              reason: 'em d=$d o shader tem de mostrar a listra de origem '
                  '(origem em ${origem.dx.toStringAsFixed(1)}): $lido');
          amostras++;
        }
        expect(amostras, greaterThan(5));
      });
    });

    testWidgets('alem do raio o pixel fica vazio', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _listras();
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 80 / _lado,
          centroY: 0.5,
          angulo: -90,
          raio: 40,
        );
        for (final x in [121, 125, 127]) {
          expect(quadro.a(x, 64), 0,
              reason: 'x=$x esta fora do rolo e nao pode cobrir nada');
        }
        expect(quadro.a(118, 64), 255, reason: 'dentro do rolo, opaco');
      });
    });

    testWidgets('raio zero nao muda nada', (tester) async {
      await tester.runAsync(() async {
        final fonte = await _listras();
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: -90,
          raio: 0,
        );
        expect(quadro.r(8, 64), 60);
        expect(quadro.r(24, 64), 220);
      });
    });

    testWidgets('a luz acende um lado do rolo e apaga o outro',
        (tester) async {
      await tester.runAsync(() async {
        final fonte = await _listras();
        final normal = normalDaDobra(-90);
        // Sem brilho e com o mesmo papel, o que sobra e a difusa: ela
        // depende do angulo do cilindro, entao os dois extremos do rolo
        // NAO podem ter a mesma luz.
        final quadro = await _desenhar(
          fonte: fonte,
          centroX: 0.5,
          centroY: 0.5,
          angulo: -90,
          raio: 60,
          luz: 0,
          brilho: 0,
        );
        final perto = quadro.r(66, 100); // logo depois do vinco
        final longe = quadro.r(105, 100); // ja perto do topo do rolo
        expect(perto == longe, isFalse,
            reason: 'a difusa do cilindro muda com o angulo: $perto x $longe');
      });
    });
  });
}
