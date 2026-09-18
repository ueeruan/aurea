// A SOMBRA PROJETADA — E A PROVA DE QUE ELA DESENHA.
//
// Tres versoes desta conta morreram porque a sombra nao aparecia, e a
// quarta nao pode se contentar com "compila". Os testes de dominio
// conferem o deslocamento, o sigma e a margem; os de widget LEEM OS
// PIXELS do quadro montado, que e a unica prova de que a sombra saiu.
//
// A bancada: uma tela de 200x200, uma camada branca de 60x60 em (60,60)
// COM UM FURO de 20x20 no meio, e a sombra a 60 px na direcao 135 — o que
// poe o deslocamento em (+42,43, +42,43), longe o bastante para a sombra
// sair de baixo da camada e dar onde amostrar.
//
//   camada   [60, 120]     furo da camada   [80, 100]
//   sombra   [102,162]     furo da sombra   [122,142]
//
// O FURO E O TESTE QUE IMPORTA: ele separa "a sombra e o ALFA da camada"
// de "a sombra e a CAIXA da camada". Uma sombra de caixa pintaria o furo.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/sombra_projetada.dart';
import 'package:aurea/src/features/editor/presentation/widgets/sombra_projetada_pass.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';

const _ladoDaTela = 200.0;

// AS COORDENADAS DA BANCADA, e onde cada amostra cai:
//
//   camada [60,120]      furo da camada [80,100]
//   sombra [102.4,162.4] furo da sombra [122.4,142.4]
//
//   (70,70)   camada, fora do furo          -> branco
//   (90,90)   furo da camada                -> vazio
//   (145,145) sombra solida, FORA da camada -> cor da sombra
//   (131,131) furo da sombra                -> vazio
//
// O (110,110) NAO SERVE para amostrar sombra: ele esta dentro da caixa da
// camada (60..120), e a camada pinta por cima. Foi essa a conta errada da
// primeira versao do teste.
const _pxDaSombraSo = 145.0;
const _pxDoFuroDaSombra = 131.0;
const _pxDaCamada = 70.0;
const _pxDoFuro = 90.0;
const _ladoDaCamada = 60.0;
const _onde = 60.0;
const _ladoDoFuro = 20.0;
const _distancia = 60.0;

/// A instancia do efeito com os parametros que o teste quer.
EffectInstance _efeito({
  double distancia = _distancia,
  double direcao = 135,
  double suavidade = 0,
  double opacidade = 100,
  double somenteSombra = 0,
  Color cor = const Color(0xFF000000),
}) => EffectInstance(
  type: EffectType.sombraProjetada,
  params: {
    'distancia': AnimatedDouble(distancia),
    'direcao': AnimatedDouble(direcao),
    'suavidade': AnimatedDouble(suavidade),
    'opacidade': AnimatedDouble(opacidade),
    'somente_sombra': AnimatedDouble(somenteSombra),
  },
  color: cor,
);

/// Monta o quadro e devolve a imagem, para leitura de pixel.
///
/// A CAMADA E POSICIONADA, e nao esticada: `Positioned.fill` daria
/// restricao justa de 200x200 ao passe, e o `SizedBox` de 60 que devia ser
/// a camada viraria o quadro inteiro — foi o que fez a primeira versao
/// desta bancada achar sombra onde havia branco.
Future<ui.Image> _quadro(WidgetTester tester, Widget passe) async {
  final chave = GlobalKey();
  await tester.pumpWidget(
    Directionality(
      textDirection: TextDirection.ltr,
      child: Center(
        child: RepaintBoundary(
          key: chave,
          child: SizedBox(
            width: _ladoDaTela,
            height: _ladoDaTela,
            child: Stack(
              clipBehavior: Clip.none,
              children: [
                Positioned(
                  left: _onde,
                  top: _onde,
                  width: _ladoDaCamada,
                  height: _ladoDaCamada,
                  child: passe,
                ),
              ],
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
  late ui.Image imagem;
  await tester.runAsync(() async {
    final limite =
        chave.currentContext!.findRenderObject()! as RenderRepaintBoundary;
    imagem = await limite.toImage();
  });
  return imagem;
}

Future<List<int>> _pixel(ui.Image im, double x, double y) async {
  final dados = await im.toByteData(format: ui.ImageByteFormat.rawRgba);
  final bytes = dados!.buffer.asUint8List();
  final i = ((y.round() * im.width) + x.round()) * 4;
  return [bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]];
}

/// A camada do teste: um quadrado cheio com um furo quadrado no meio.
class _ComFuro extends StatelessWidget {
  const _ComFuro();

  @override
  Widget build(BuildContext context) => CustomPaint(
    size: const Size(_ladoDaCamada, _ladoDaCamada),
    painter: _PintorComFuro(),
  );
}

class _PintorComFuro extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final fora = Offset.zero & size;
    final furo = Rect.fromCenter(
      center: size.center(Offset.zero),
      width: _ladoDoFuro,
      height: _ladoDoFuro,
    );
    final caminho = Path()
      ..fillType = PathFillType.evenOdd
      ..addRect(fora)
      ..addRect(furo);
    canvas.drawPath(caminho, Paint()..color = const Color(0xFFFFFFFF));
  }

  @override
  bool shouldRepaint(_PintorComFuro old) => false;
}

void main() {
  group('o deslocamento medido no After Effects', () {
    test('135 graus poe a sombra embaixo a direita: (+21, +21) com 30', () {
      final d = deslocamentoDaSombra(distancia: 30, direcao: 135);
      expect(d.dx, closeTo(21.213, 0.01));
      expect(d.dy, closeTo(21.213, 0.01));
    });

    test('as quatro direcoes cardinais', () {
      // 0 poe a LUZ a direita, entao a sombra cai a ESQUERDA.
      final zero = deslocamentoDaSombra(distancia: 30, direcao: 0);
      expect(zero.dx, closeTo(-30, 0.001));
      expect(zero.dy, closeTo(0, 0.001));

      // 90 poe a luz embaixo, e a sombra sobe.
      final noventa = deslocamentoDaSombra(distancia: 30, direcao: 90);
      expect(noventa.dx, closeTo(0, 0.001));
      expect(noventa.dy, closeTo(30, 0.001));

      final centoEOitenta = deslocamentoDaSombra(distancia: 30, direcao: 180);
      expect(centoEOitenta.dx, closeTo(30, 0.001));
      expect(centoEOitenta.dy, closeTo(0, 0.001));

      final duzentosESetenta = deslocamentoDaSombra(distancia: 30, direcao: 270);
      expect(duzentosESetenta.dx, closeTo(0, 0.001));
      expect(duzentosESetenta.dy, closeTo(-30, 0.001));
    });

    test('os quatro quadrantes mantem a soma dos quadrados', () {
      for (final grau in [45.0, 135.0, 225.0, 315.0]) {
        final d = deslocamentoDaSombra(distancia: 40, direcao: grau);
        expect(
          d.dx * d.dx + d.dy * d.dy,
          closeTo(1600, 0.01),
          reason: 'a distancia e o modulo, em qualquer direcao',
        );
      }
    });

    test('distancia zero nao desloca', () {
      expect(deslocamentoDaSombra(distancia: 0, direcao: 135), Offset.zero);
    });
  });

  group('o desfoque', () {
    test('sigma e suavidade vezes 0,225 — o que o render do AE mostra', () {
      expect(sigmaDaSombra(20), closeTo(4.5, 0.001));
      expect(sigmaDaSombra(100), closeTo(22.5, 0.001));
    });

    test('suavidade zero e borda dura, e nao uma gaussiana de raio zero', () {
      expect(sigmaDaSombra(0), 0);
      expect(sigmaDaSombra(-5), 0);
    });
  });

  group('a margem que a camada precisa abrir', () {
    test('e assimetrica: cada lado leva o que a sombra pede daquele lado', () {
      final m = margemDaSombra(distancia: 30, direcao: 135, suavidade: 0);
      // A sombra cai embaixo a direita, entao e desse lado que a caixa cresce.
      expect(m.esquerda, 0);
      expect(m.topo, 0);
      expect(m.direita, closeTo(21.213, 0.01));
      expect(m.baixo, closeTo(21.213, 0.01));
    });

    test('com desfoque, os quatro lados ganham a cauda de tres sigma', () {
      final m = margemDaSombra(distancia: 30, direcao: 135, suavidade: 20);
      expect(m.esquerda, closeTo(13.5, 0.01));
      expect(m.topo, closeTo(13.5, 0.01));
      expect(m.direita, closeTo(21.213 + 13.5, 0.01));
      expect(m.baixo, closeTo(21.213 + 13.5, 0.01));
    });

    test('sem distancia e sem desfoque a margem e vazia (identidade)', () {
      final m = margemDaSombra(distancia: 0, direcao: 135, suavidade: 0);
      expect(m.vazia, isTrue);
    });

    test('so desfoque ja abre a caixa nos quatro lados', () {
      final m = margemDaSombra(distancia: 0, direcao: 135, suavidade: 20);
      expect(m.vazia, isFalse);
      expect(m.esquerda, closeTo(13.5, 0.01));
      expect(m.direita, closeTo(13.5, 0.01));
    });
  });

  group('o alinhamento da caixa aberta', () {
    // A CONTA DO MOTOR, conferida contra a formula do proprio OverflowBox:
    //   deslocamento = (tamanhoDoPai - tamanhoDoFilho) * (alignment + 1) / 2
    // O que se quer e `deslocamento == -margem`.
    double deslocamento(double pai, double filho, double a) =>
        (pai - filho) * (a + 1) / 2;

    test('a caixa fica onde a camada estava, e nao centrada', () {
      const lado = 60.0;
      final m = margemDaSombra(distancia: 30, direcao: 135, suavidade: 0);
      final a = alinhamentoDaSombra(
        margem: m,
        larguraDaCamada: lado,
        alturaDaCamada: lado,
      );
      expect(deslocamento(lado, lado + m.largura, a.x),
          closeTo(-m.esquerda, 0.001));
      expect(
          deslocamento(lado, lado + m.altura, a.y), closeTo(-m.topo, 0.001));
    });

    test('margem a esquerda empurra a caixa para a esquerda', () {
      // Direcao 0 poe a sombra a esquerda: e o caso em que o sinal se
      // inverte, e onde uma conta de modulo passaria despercebida.
      final m = margemDaSombra(distancia: 40, direcao: 0, suavidade: 0);
      expect(m.esquerda, closeTo(40, 0.01));
      expect(m.direita, 0);
      final a = alinhamentoDaSombra(
        margem: m,
        larguraDaCamada: 100,
        alturaDaCamada: 100,
      );
      expect(a.x, greaterThan(0));
      expect(deslocamento(100, 140, a.x), closeTo(-40, 0.001));
    });

    test('sem caixa a mais nao ha o que alinhar, e nao uma divisao por zero',
        () {
      final a = alinhamentoDaSombra(
        margem: const MargemDaSombra(0, 0, 0, 0),
        larguraDaCamada: 100,
        alturaDaCamada: 100,
      );
      expect(a.x, 0);
      expect(a.y, 0);
    });
  });

  group('a ficha no catalogo', () {
    test('esta registrada com o id do After Effects', () {
      final spec = effectSpecs[EffectType.sombraProjetada];
      expect(spec, isNotNull);
      expect(spec!.id, 'adbe_drop_shadow');
      expect(spec.name, 'Sombra projetada');
      expect(spec.hasColor, isTrue);
    });

    test('tem os cinco parametros do efeito, com os valores do AE', () {
      final p = effectSpecs[EffectType.sombraProjetada]!.params;
      expect(p['distancia']!.initial, 5);
      expect(p['direcao']!.initial, 135);
      expect(p['suavidade']!.initial, 0);
      expect(p['opacidade']!.initial, 100);
      expect(p['opacidade']!.unit, '%');
      expect(p['direcao']!.unit, '°');
    });

    test('os tres numeros que a montagem mostra sao os que a sombra usa', () {
      expect(
        effectSpecs[EffectType.sombraProjetada]!.montar,
        ['distancia', 'direcao', 'suavidade'],
      );
    });
  });

  group('o desenho, lido pixel a pixel', () {
    testWidgets('a sombra SAI da camada — este e o teste que faltava',
        (tester) async {
      final im = await _quadro(
        tester,
        SombraProjetadaPass(
          effect: _efeito(),
          time: Duration.zero,
          child: const _ComFuro(),
        ),
      );
      await tester.runAsync(() async {
        // (145,145) esta DENTRO da caixa da sombra e FORA da camada.
        final sombra = await _pixel(im, _pxDaSombraSo, _pxDaSombraSo);
        expect(sombra[3], greaterThan(200),
            reason: 'a sombra tem de existir e ser opaca aqui: $sombra');
        expect(sombra[0], lessThan(60), reason: 'e preta: $sombra');
        expect(sombra[1], lessThan(60));
        expect(sombra[2], lessThan(60));

        // (70,70) e a camada, por cima da sombra.
        final camada = await _pixel(im, _pxDaCamada, _pxDaCamada);
        expect(camada, [255, 255, 255, 255],
            reason: 'a camada continua inteira por cima');
      });
    });

    testWidgets('a sombra e o ALFA da camada: o furo tambem fura a sombra',
        (tester) async {
      final im = await _quadro(
        tester,
        SombraProjetadaPass(
          effect: _efeito(),
          time: Duration.zero,
          child: const _ComFuro(),
        ),
      );
      await tester.runAsync(() async {
        // (131,131) cai no FURO da sombra, ja fora da camada.
        final furo = await _pixel(im, _pxDoFuroDaSombra, _pxDoFuroDaSombra);
        expect(furo[3], lessThan(30),
            reason: 'uma sombra de CAIXA pintaria aqui; a de alfa nao: $furo');

        // (90,90) e o furo da propria camada: tambem vazio.
        final furoDaCamada = await _pixel(im, _pxDoFuro, _pxDoFuro);
        expect(furoDaCamada[3], lessThan(30));

        // (10,10) esta longe de tudo.
        final longe = await _pixel(im, 10, 10);
        expect(longe[3], lessThan(30));
      });
    });

    testWidgets('sem distancia e sem desfoque o passe nao desenha nada',
        (tester) async {
      final im = await _quadro(
        tester,
        SombraProjetadaPass(
          effect: _efeito(distancia: 0, suavidade: 0),
          time: Duration.zero,
          child: const _ComFuro(),
        ),
      );
      await tester.runAsync(() async {
        // Onde a sombra estaria, nao ha nada.
        final ondeASombraEstaria = await _pixel(im, _pxDaSombraSo, _pxDaSombraSo);
        expect(ondeASombraEstaria[3], lessThan(30));
      });
    });

    testWidgets('a cor da sombra e a pedida, e nao a da camada',
        (tester) async {
      final im = await _quadro(
        tester,
        SombraProjetadaPass(
          effect: _efeito(cor: const Color(0xFFFF0000)),
          time: Duration.zero,
          child: const _ComFuro(),
        ),
      );
      await tester.runAsync(() async {
        final sombra = await _pixel(im, _pxDaSombraSo, _pxDaSombraSo);
        expect(sombra[0], greaterThan(200), reason: 'vermelha: $sombra');
        expect(sombra[1], lessThan(60));
        expect(sombra[2], lessThan(60));
      });
    });

    testWidgets('opacidade 40 deixa a sombra translucida', (tester) async {
      final im = await _quadro(
        tester,
        SombraProjetadaPass(
          effect: _efeito(opacidade: 40),
          time: Duration.zero,
          child: const _ComFuro(),
        ),
      );
      await tester.runAsync(() async {
        final sombra = await _pixel(im, _pxDaSombraSo, _pxDaSombraSo);
        expect(sombra[3], closeTo(102, 8),
            reason: '40% de 255 e 102: $sombra');
      });
    });

    testWidgets('"somente sombra" tira a camada e deixa a sombra',
        (tester) async {
      final im = await _quadro(
        tester,
        SombraProjetadaPass(
          effect: _efeito(somenteSombra: 1),
          time: Duration.zero,
          child: const _ComFuro(),
        ),
      );
      await tester.runAsync(() async {
        final ondeAcamadaEstaria = await _pixel(im, _pxDaCamada, _pxDaCamada);
        expect(ondeAcamadaEstaria[3], lessThan(30),
            reason: 'a camada sai de cena: $ondeAcamadaEstaria');

        final sombra = await _pixel(im, _pxDaSombraSo, _pxDaSombraSo);
        expect(sombra[3], greaterThan(200), reason: 'e a sombra fica: $sombra');
      });
    });

    testWidgets('o desfoque espalha a sombra para fora da borda dura',
        (tester) async {
      // Com suavidade 40 (sigma 9) a sombra ja passou da borda exata do
      // quadrado deslocado. O ponto (98,98) fica FORA da caixa da sombra
      // dura (que comeca em 102) e dentro da cauda desfocada.
      final im = await _quadro(
        tester,
        SombraProjetadaPass(
          effect: _efeito(suavidade: 40, opacidade: 100),
          time: Duration.zero,
          child: const _ComFuro(),
        ),
      );
      await tester.runAsync(() async {
        final cauda = await _pixel(im, 95, 95);
        expect(cauda[3], greaterThan(10),
            reason: 'a cauda do desfoque tem de chegar aqui: $cauda');

        final longe = await _pixel(im, 40, 40);
        expect(longe[3], lessThan(30),
            reason: 'mas ela nao pode invadir o mundo inteiro: $longe');
      });
    });
  });
}
