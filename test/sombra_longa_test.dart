// A SOMBRA LONGA — a marcha no dominio, e o desenho lido no pixel.
//
// SEM RENDER DO AE, E DE PROPOSITO: o AE do dono nao tem Long Shadow (nem
// o S_LongShadow do Sapphire). A ficha e nossa, entao o que se cobra aqui
// nao e "igual ao AE" — e a GEOMETRIA da uniao de copias, que e uma conta
// fechada e pode ser conferida amostra por amostra.
//
// A bancada do desenho: tela de 128x128, camada SOLIDA de 40x40 em
// (20,20), sombra de 40 px na direcao 135. O deslocamento unitario e
// (0,7071, 0,7071), entao a sombra cai para baixo e para a direita e a
// ponta do canto chega em (88,3, 88,3).
//
// A CONTA DE CADA AMOSTRA e "existe t em [0,40] com p - t*u dentro do
// quadrado [20,60)x[20,60)?", e ela esta escrita ao lado de cada uma.
//
// POR QUE A LEITURA E NO SHADER CRU, e nao pela arvore de widgets como na
// Sombra projetada: `ui.ImageFilter.isShaderFilterSupported` e FALSE no
// ambiente de teste, entao um passe deste tipo cai na RESERVA, que tira
// uma foto da camada por `SnapshotWidget` — e essa foto e assincrona de
// verdade, coisa que o teste de widget nao deixa completar. O resultado
// era um teste que passava sozinho e falhava em fila, lendo lixo do teste
// anterior. Aqui o shader e chamado direto, com os MESMOS uniformes que o
// passe monta: e a mesma conta, sem a foto no meio.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/sombra_longa.dart';
import 'package:aurea/src/features/editor/domain/sombra_projetada.dart';
import 'package:aurea/src/features/editor/presentation/widgets/sombra_longa_pass.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';

/// O lado da tela da bancada de desenho, em pixels logicos.
const _lado = 128.0;
const _onde = 20.0;
const _ladoDaCamada = 40.0;
const _distancia = 40.0;

/// O fator 0,7071 da direcao 135, que e o que anda cada passo da marcha.
const _meio = 0.7071067811865476;

EffectInstance _efeito({
  double distancia = _distancia,
  double direcao = 135,
  double suavidade = 0,
  double opacidade = 100,
  double queda = 0,
  Color cor = const Color(0xFF2040C0),
}) => EffectInstance(
  type: EffectType.sombraLonga,
  params: {
    'distancia': AnimatedDouble(distancia),
    'direcao': AnimatedDouble(direcao),
    'suavidade': AnimatedDouble(suavidade),
    'opacidade': AnimatedDouble(opacidade),
    'queda': AnimatedDouble(queda),
  },
  color: cor,
);

/// A FONTE DA BANCADA: um quadrado branco de 40x40 em (20,20).
Future<ui.Image> _quadrado() async {
  final pixels = Uint8List(_lado.toInt() * _lado.toInt() * 4);
  for (var y = 20; y < 60; y++) {
    for (var x = 20; x < 60; x++) {
      final i = ((y * _lado.toInt()) + x) * 4;
      pixels[i] = 255;
      pixels[i + 1] = 255;
      pixels[i + 2] = 255;
      pixels[i + 3] = 255;
    }
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(pixels);
  final descritor = ui.ImageDescriptor.raw(
    buffer,
    width: _lado.toInt(),
    height: _lado.toInt(),
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descritor.instantiateCodec();
  return (await codec.getNextFrame()).image;
}

class _Leitura {
  _Leitura(this.bytes, this.largura);
  final Uint8List bytes;
  final int largura;
  List<int> em(double x, double y) {
    final i = ((y.round() * largura) + x.round()) * 4;
    return [bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]];
  }
}

Future<_Leitura> _ler(ui.Image im) async {
  final dados = await im.toByteData(format: ui.ImageByteFormat.rawRgba);
  return _Leitura(dados!.buffer.asUint8List(), im.width);
}

/// A CAMADA DA BANCADA, para os testes de identidade.
Widget _camada() => const ColoredBox(color: Color(0xFFFFFFFF));

/// Monta o passe numa tela de [lado] com a camada posicionada.
Widget _comPasse(double lado, Widget passe) => Directionality(
  textDirection: TextDirection.ltr,
  child: Center(
    child: SizedBox(
      width: lado,
      height: lado,
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
);

void main() {
  group('a marcha', () {
    test('e um passo por pixel enquanto o comprimento couber no teto', () {
      final m = marchaDaSombraLonga(distancia: 40);
      expect(m.passo, 1);
      expect(m.passos, 40);
    });

    test('comprimento longo estoura o teto e o passo cresce junto', () {
      final m = marchaDaSombraLonga(distancia: 2000);
      expect(m.passos, kTetoDePassosDaSombraLonga);
      expect(m.passo, closeTo(2000 / kTetoDePassosDaSombraLonga, 1e-9));
      // A marcha tem de cobrir o comprimento inteiro, e nao parar no meio.
      expect(m.passo * m.passos, greaterThanOrEqualTo(2000));
    });

    test('comprimento zero ou negativo nao marcha', () {
      expect(marchaDaSombraLonga(distancia: 0).passos, 0);
      expect(marchaDaSombraLonga(distancia: -10).passos, 0);
      expect(marchaDaSombraLonga(distancia: double.nan).passos, 0);
    });
  });

  group('a caixa', () {
    test('abre para o lado em que a sombra cai, e nao do outro', () {
      final m = margemDaSombraLonga(
        distancia: 40,
        direcao: 135,
        suavidade: 0,
      );
      // 135 graus: a sombra cai para a direita e para baixo.
      expect(m.direita, closeTo(40 * _meio, 1e-9));
      expect(m.baixo, closeTo(40 * _meio, 1e-9));
      expect(m.esquerda, 0);
      expect(m.topo, 0);
    });

    test('a sombra longa usa a MESMA margem da projetada', () {
      // Nao e economia de codigo: sao a mesma pergunta, e duas contas para
      // ela divergiriam no dia em que uma das duas mudasse.
      for (final d in const [0.0, 12.0, 200.0]) {
        final longa = margemDaSombraLonga(
          distancia: d,
          direcao: 135,
          suavidade: 20,
        );
        final projetada = margemDaSombra(
          distancia: d,
          direcao: 135,
          suavidade: 20,
        );
        expect(longa.esquerda, projetada.esquerda);
        expect(longa.topo, projetada.topo);
        expect(longa.direita, projetada.direita);
        expect(longa.baixo, projetada.baixo);
      }
    });
  });

  group('a ficha no catalogo', () {
    test('esta registrada, com a direcao igual a da sombra projetada', () {
      final spec = effectSpecs[EffectType.sombraLonga];
      expect(spec, isNotNull);
      expect(spec!.id, 'sombra_longa');
      expect(spec.name, 'Sombra longa');
      expect(spec.hasColor, isTrue);
      expect(spec.params['direcao']!.initial, 135);
    });

    test('a cor padrao e preta translucida, e nao preta pura', () {
      final spec = effectSpecs[EffectType.sombraLonga]!;
      expect(spec.defaultColor.a, closeTo(0.7, 0.01));
      expect(spec.defaultColor.r, 0);
    });

    test('os presets usam as chaves que existem na ficha', () {
      final spec = effectSpecs[EffectType.sombraLonga]!;
      for (final pronto in spec.presets) {
        for (final chave in pronto.valores.keys) {
          expect(spec.params.containsKey(chave), isTrue,
              reason: 'preset "${pronto.nome}" mexe em "$chave", que nao existe');
        }
      }
    });
  });

  group('o shader, lido pixel a pixel', () {
    Future<_Leitura> sombra({
      double distancia = _distancia,
      double direcao = 135,
      double opacidade = 100,
      double queda = 0,
      Color cor = const Color(0xFF2040C0),
    }) async {
      final fonte = await _quadrado();
      final marcha = marchaDaSombraLonga(distancia: distancia);
      final unidade = deslocamentoDaSombra(distancia: 1, direcao: direcao);
      final programa = await ui.FragmentProgram.fromAsset(
        'shaders/sombra_longa.frag',
      );
      final shader = programa.fragmentShader();
      shader
        ..setFloat(0, _lado)
        ..setFloat(1, _lado)
        ..setFloat(2, _lado)
        ..setFloat(3, _lado)
        ..setFloat(4, unidade.dx * marcha.passo)
        ..setFloat(5, unidade.dy * marcha.passo)
        ..setFloat(6, marcha.passos.toDouble())
        ..setFloat(7, cor.r)
        ..setFloat(8, cor.g)
        ..setFloat(9, cor.b)
        ..setFloat(10, cor.a * opacidade / 100)
        ..setFloat(11, queda / 100)
        ..setImageSampler(0, fonte);
      final gravador = ui.PictureRecorder();
      ui.Canvas(gravador).drawRect(
        const ui.Rect.fromLTWH(0, 0, _lado, _lado),
        ui.Paint()..shader = shader,
      );
      final imagem = await gravador.endRecording()
          .toImage(_lado.toInt(), _lado.toInt());
      final lida = await _ler(imagem);
      shader.dispose();
      return lida;
    }

    testWidgets('a sombra sai da camada para o lado da direcao',
        (tester) async {
      await tester.runAsync(() async {
        final q = await sombra();
        // (65,65): t de 7,07 a 40 -> dentro (o quadrado comeca em 60).
        expect(q.em(65, 65), [0x20, 0x40, 0xC0, 255]);
        // (75,75): t de 21,2 a 40 -> dentro tambem.
        expect(q.em(75, 75), [0x20, 0x40, 0xC0, 255]);
        // (70,45): y=45,5 pede t de 0 a 36, e ai x fica em 45,5..70,5 — a
        // partir de t=14,8 cai dentro do quadrado (x=60). E sombra mesmo
        // estando FORA da coluna da camada: e o cisalhamento, e nao um
        // borrao.
        expect(q.em(70, 45), [0x20, 0x40, 0xC0, 255]);
      });
    });

    testWidgets('do lado da luz nao ha sombra nenhuma', (tester) async {
      await tester.runAsync(() async {
        final q = await sombra();
        // Acima e a esquerda da camada: a marcha so anda para tras, entao
        // nada ali pode ter sido tocado.
        for (final p in const [
          [5.0, 5.0],
          [50.0, 5.0],
          [5.0, 50.0],
          [18.0, 18.0],
        ]) {
          expect(q.em(p[0], p[1])[3], 0,
              reason: 'em (${p[0]}, ${p[1]}) nao pode haver sombra');
        }
      });
    });

    testWidgets('o comprimento acaba onde foi pedido', (tester) async {
      await tester.runAsync(() async {
        final q = await sombra();
        // A ponta do canto esta em 60 + 40*0,7071 = 88,3.
        // (85,85): t de 35,4 a 40 -> ainda dentro.
        expect(q.em(85, 85)[3], 255, reason: 'pouco antes da ponta');
        // (92,92): t teria de comecar em 45,3, e o comprimento e 40.
        expect(q.em(92, 92)[3], 0, reason: 'depois da ponta');
        expect(q.em(100, 100)[3], 0);
      });
    });

    testWidgets('fora do cisalhamento o pixel fica vazio', (tester) async {
      await tester.runAsync(() async {
        final q = await sombra();
        // (80,30): y=30 pede t ate 14,1, e ai x ficaria em 70..80 — fora
        // do quadrado. O lado do losango e inclinado, nao e uma caixa: e
        // essa a diferenca entre a uniao de copias e um borrao.
        expect(q.em(80, 30)[3], 0);
        expect(q.em(35, 80)[3], 0);
      });
    });

    testWidgets('debaixo da camada a sombra e a silhueta, e nao a cor dela',
        (tester) async {
      await tester.runAsync(() async {
        final q = await sombra(cor: const Color(0xFF000000));
        // (45,45) esta dentro do quadrado branco: o que o shader devolve
        // ali e a SILHUETA chapada na cor da sombra. Quem desenha a
        // camada por cima e o passe, nao o shader.
        expect(q.em(45, 45), [0, 0, 0, 255]);
      });
    });

    testWidgets('a opacidade entra pelo alfa, e nao pelo rgb', (tester) async {
      await tester.runAsync(() async {
        final q = await sombra(opacidade: 40);
        final px = q.em(75, 75);
        expect(px[3], closeTo(102, 3), reason: '40% de 255');
        expect(px[0], 0x20);
        expect(px[1], 0x40);
        expect(px[2], 0xC0);
      });
    });

    testWidgets('a queda desvanece a sombra ate a ponta', (tester) async {
      await tester.runAsync(() async {
        final q = await sombra(queda: 100);
        final perto = q.em(65, 65)[3];
        final longe = q.em(85, 85)[3];
        expect(perto, greaterThan(200), reason: 'o peso ali ainda e alto');
        expect(longe, lessThan(60), reason: 'e aqui ja caiu quase tudo');
        expect(longe, lessThan(perto));
      });
    });

    testWidgets('a direcao gira a sombra', (tester) async {
      await tester.runAsync(() async {
        // 315 graus poe a sombra para cima e para a esquerda.
        final q = await sombra(direcao: 315);
        expect(q.em(12, 12)[3], 255, reason: 'agora a sombra esta aqui');
        expect(q.em(75, 75)[3], 0, reason: 'e nao mais ali');
      });
    });

    testWidgets('comprimento zero nao marcha um passo', (tester) async {
      await tester.runAsync(() async {
        final q = await sombra(distancia: 0);
        // Sem comprimento a marcha nao da passo nenhum, entao nao ha
        // sombra em lugar nenhum — nem debaixo da camada.
        expect(q.em(45, 45)[3], 0);
        expect(q.em(75, 75)[3], 0);
      });
    });
  });

  group('a identidade no passe', () {
    testWidgets('comprimento zero devolve a camada intacta', (tester) async {
      await tester.pumpWidget(
        _comPasse(
          _lado,
          SombraLongaPass(
            effect: _efeito(distancia: 0),
            time: Duration.zero,
            child: _camada(),
          ),
        ),
      );
      await tester.pumpAndSettle();
      // O Passe nao monta caixa nenhuma: a subarvore e a mesma, e o
      // `ColoredBox` da camada continua sendo o unico filho dela.
      expect(find.byType(ColoredBox), findsOneWidget);
      expect(find.byType(OverflowBox), findsNothing);
    });

    testWidgets('opacidade zero tambem', (tester) async {
      await tester.pumpWidget(
        _comPasse(
          _lado,
          SombraLongaPass(
            effect: _efeito(opacidade: 0),
            time: Duration.zero,
            child: _camada(),
          ),
        ),
      );
      await tester.pumpAndSettle();
      expect(find.byType(OverflowBox), findsNothing);
    });
  });
}
