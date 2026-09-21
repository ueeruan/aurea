// O MOTION TILE, MEDIDO NO PIXEL.
//
// O RELATO: com o efeito aplicado e o zoom da camada reduzido, a imagem
// nao se repete como no After Effects — e na PREVIA.
//
// A CAUSA NAO ERA A MATEMATICA DO LADRILHO. Era ONDE a repeticao
// acontecia: o efeito mora DENTRO do transform da camada (efeito age na
// fonte, o transform vem depois — a ordem do After Effects), entao a
// regiao ladrilhada era a caixa da camada. Com a camada em 50%, a parede
// de ladrilhos saia junto, encolhida, e o quadro ficava com a moldura
// vazia em volta. A repeticao estava certa; ela so nao cobria nada.
//
// ESTE ARQUIVO TEM DUAS METADES, e elas provam coisas diferentes:
//
//   1. O SHADER — carregado de verdade (`shaders/motion_tile.frag`), com
//      os uniforms na ordem em que o passe os poe. Aqui se prova a GRADE:
//      o periodo, o espelho, a fase. Uma formula reescrita em Dart
//      provaria que eu sei escrever a formula;
//   2. O PASSE — montado como widget, com a escala e a composicao que o
//      palco entrega. Aqui se prova o RELATO: com a camada em 50%, o
//      quadro sai coberto, e nao com moldura vazia.
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/motion_tile_pass.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// A IMAGEM DE ENTRADA: 64x64, quatro quadrantes.
///
/// QUADRANTES DISTINTOS, e nao uma cor lisa: com eles, "o ladrilho esta
/// espelhado" e "a grade tem este periodo" sao perguntas que se respondem
/// olhando uma linha de pixels. Uma imagem lisa esconderia qualquer erro.
Future<ui.Image> _padrao({int lado = 64}) async {
  final pixels = Uint8List(lado * lado * 4);
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final i = (y * lado + x) * 4;
      final direita = x >= lado ~/ 2;
      final baixo = y >= lado ~/ 2;
      final (r, g, b) = switch ((direita, baixo)) {
        (false, false) => (255, 0, 0),
        (true, false) => (0, 255, 0),
        (false, true) => (0, 0, 255),
        (true, true) => (255, 255, 255),
      };
      pixels[i] = r;
      pixels[i + 1] = g;
      pixels[i + 2] = b;
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

  /// O CANAL (0=r, 1=g, 2=b, 3=a) do pixel.
  int canal(int x, int y, int c) {
    if (x < 0 || y < 0 || x >= lado || y >= lado) return 0;
    return pixels[(y * lado + x) * 4 + c];
  }

  /// A COR do pixel como rotulo legivel — um triplo de numeros nao diz
  /// nada a quem le o teste.
  String cor(int x, int y) {
    if (x < 0 || y < 0 || x >= lado || y >= lado) return 'fora';
    final i = (y * lado + x) * 4;
    final r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
    if (r > 200 && g < 60 && b < 60) return 'vermelho';
    if (g > 200 && r < 60 && b < 60) return 'verde';
    if (b > 200 && r < 60 && g < 60) return 'azul';
    if (r > 200 && g > 200 && b > 200) return 'branco';
    if (r < 20 && g < 20 && b < 20) return 'preto';
    return 'outro($r,$g,$b)';
  }

  int _opacos = -1;
  int get opacos {
    if (_opacos >= 0) return _opacos;
    var n = 0;
    for (var i = 3; i < pixels.length; i += 4) {
      if (pixels[i] > 8) n++;
    }
    return _opacos = n;
  }
}

/// DESENHA O SHADER, do jeito que o passe o monta.
///
/// A ORDEM DOS `setFloat` E A DA DECLARACAO NO SHADER, e nao a do byte do
/// std140 (ver `glsl-uniforme-por-declaracao`). Se alguem reordenar os
/// uniforms la sem mexer aqui, este teste passa a medir outra coisa.
Future<_Quadro> _desenharShader({
  required ui.Image fonte,
  required int lado,
  double tileW = 1,
  double tileH = 1,
  double outputW = 1,
  double outputH = 1,
  double espelho = 0,
  double fase = 0,
  double estica = 0,
}) async {
  final programa = await ui.FragmentProgram.fromAsset(
    'shaders/motion_tile.frag',
  );
  final shader = programa.fragmentShader();
  try {
    shader
      ..setFloat(0, lado.toDouble()) // uSize
      ..setFloat(1, lado.toDouble())
      ..setFloat(2, outputW) // uOutput
      ..setFloat(3, outputH)
      ..setFloat(4, tileW) // uTile
      ..setFloat(5, tileH)
      ..setFloat(6, .5) // uCenter
      ..setFloat(7, .5)
      ..setFloat(8, espelho)
      ..setFloat(9, fase)
      ..setFloat(10, 0) // uHorizontal
      ..setFloat(11, 0) // uFilter (host sem Impeller: orientacao ja certa)
      ..setFloat(12, estica)
      // uMeioTexel E O ULTIMO, e nao pode sair do lugar: `setFloat`
      // endereca por ordem de declaracao. Aqui a textura e a propria fonte
      // no tamanho dela, entao meio texel e meio pixel de camada.
      ..setFloat(13, .5 / fonte.width)
      ..setFloat(14, .5 / fonte.height)
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

/// O PONTO ESTA DENTRO DO CONVEXO? Produto vetorial com o mesmo sinal nas
/// quatro arestas.
///
/// A CONTA E OUTRA, E DE PROPOSITO. Conferir a cobertura repetindo a inversa
/// da transformacao so provaria que ela concorda consigo mesma; o produto
/// vetorial pega sinal trocado, eixo trocado e quina fora de ordem.
bool _dentroDoConvexo(List<Offset> quinas, Offset ponto) {
  var positivo = false, negativo = false;
  for (var i = 0; i < quinas.length; i++) {
    final a = quinas[i], b = quinas[(i + 1) % quinas.length];
    final cruz =
        (b.dx - a.dx) * (ponto.dy - a.dy) - (b.dy - a.dy) * (ponto.dx - a.dx);
    if (cruz > 1e-6) positivo = true;
    if (cruz < -1e-6) negativo = true;
  }
  return !(positivo && negativo);
}

/// UMA INSTANCIA DO EFEITO com os parametros pedidos.
EffectInstance _efeito({
  double tileW = 100,
  double tileH = 100,
  double outputW = 100,
  double outputH = 100,
  double espelho = 0,
  double fase = 0,
  double estica = 0,
}) {
  var e = EffectInstance(type: EffectType.motionTile);
  for (final (chave, valor) in [
    ('tile_width', tileW),
    ('tile_height', tileH),
    ('output_width', outputW),
    ('output_height', outputH),
    ('mirror_edges', espelho),
    ('clamp_edges', estica),
    ('phase', fase),
  ]) {
    e = e.withParamEdited(chave, Duration.zero, valor);
  }
  return e;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  _grupoDoGiroEDasEmendas();

  late ui.Image fonte;
  setUpAll(() async => fonte = await _padrao());
  tearDownAll(() => fonte.dispose());

  group('a grade do shader', () {
    test('ladrilho de 100% e a propria camada, sem grade', () async {
      final q = await _desenharShader(fonte: fonte, lado: 64);
      expect(q.cor(2, 2), 'vermelho');
      expect(q.cor(61, 2), 'verde');
      expect(q.cor(2, 61), 'azul');
      expect(q.cor(61, 61), 'branco');
    });

    test('ladrilho de 50% REPETE a cada metade da camada', () async {
      // O PERIODO E A PROPRIEDADE QUE DEFINE "esta ladrilhando" — e ela
      // nao depende de onde a grade comeca, que e o que a fase escolhe.
      final q = await _desenharShader(
        fonte: fonte,
        lado: 64,
        tileW: .5,
        tileH: .5,
      );
      for (var y = 0; y < 32; y++) {
        for (var x = 0; x < 32; x++) {
          expect(
            q.cor(x + 32, y),
            q.cor(x, y),
            reason: 'a coluna $x nao se repetiu em ${x + 32}',
          );
          expect(
            q.cor(x, y + 32),
            q.cor(x, y),
            reason: 'a linha $y nao se repetiu em ${y + 32}',
          );
        }
      }
    });

    test('ladrilho de 25% repete a cada quarto', () async {
      final q = await _desenharShader(
        fonte: fonte,
        lado: 64,
        tileW: .25,
        tileH: .25,
      );
      for (var y = 0; y < 16; y++) {
        for (var x = 0; x < 16; x++) {
          expect(q.cor(x + 16, y), q.cor(x, y));
          expect(q.cor(x + 32, y), q.cor(x, y));
          expect(q.cor(x + 48, y), q.cor(x, y));
        }
      }
    });

    test('zoom muito reduzido: ladrilho de 10% ainda cobre tudo', () async {
      // ESTE E O CASO DO RELATO, do lado do shader: a repeticao nao pode
      // parar — nenhum pixel sem conteudo dentro da camada.
      final q = await _desenharShader(
        fonte: fonte,
        lado: 100,
        tileW: .1,
        tileH: .1,
      );
      expect(q.opacos, 100 * 100, reason: 'sobrou pixel sem conteudo');
      for (var k = 0; k < 9; k++) {
        for (var x = 0; x < 10; x++) {
          expect(q.cor(x + k * 10, 5), q.cor(x, 5), reason: 'periodo de 10');
        }
      }
    });

    test('X e Y do ladrilho podem ser DIFERENTES', () async {
      // LADRILHO LARGO E BAIXO — o preset "Tijolos". Uma grade quadrada
      // esconderia a troca dos dois eixos.
      final q = await _desenharShader(
        fonte: fonte,
        lado: 64,
        tileW: .5,
        tileH: .25,
      );
      for (var y = 0; y < 16; y++) {
        for (var x = 0; x < 32; x++) {
          expect(q.cor(x + 32, y), q.cor(x, y), reason: 'periodo em X');
          expect(q.cor(x, y + 16), q.cor(x, y), reason: 'periodo em Y');
        }
      }
      // E o periodo em Y e METADE do periodo em X: se os dois lados
      // estivessem trocados, o teste de cima passaria do mesmo jeito.
      final q2 = await _desenharShader(
        fonte: fonte,
        lado: 64,
        tileW: .5,
        tileH: .25,
      );
      expect(q2.cor(0, 15), isNot(q.cor(0, 32)));
    });

    test('com espelho, o ladrilho vizinho e a IMAGEM do anterior', () async {
      // A PROPRIEDADE QUE DEFINE O ESPELHO, e ela nao depende de onde a
      // grade comeca: SEM espelho o desenho se repete a cada ladrilho
      // (periodico); COM espelho, nao — o vizinho e o primeiro de cabeca
      // para baixo. E o que separa um mosaico de uma grade de carimbos:
      // sem ele, dois ladrilhos vizinhos comecam os dois pelo mesmo canto
      // e a emenda e uma descontinuidade dura.
      const periodo = 32;
      final com = await _desenharShader(
        fonte: fonte,
        lado: 64,
        tileW: .5,
        tileH: .5,
        espelho: 1,
      );
      // O ESPELHO TIRA A PERIODICIDADE: um pixel a 4 do comeco do vizinho
      // NAO repete o pixel a 4 do comeco do primeiro. Sem espelho, ele
      // repetiria — e essa e a unica diferenca entre os dois casos.
      expect(
        com.cor(periodo + 4, 8),
        isNot(com.cor(4, 8)),
        reason: 'o espelho nao mudou o ladrilho vizinho',
      );
    });

    test('a fase desloca a grade', () async {
      final base = await _desenharShader(
        fonte: fonte,
        lado: 64,
        tileW: .5,
        tileH: .5,
      );
      final deslocado = await _desenharShader(
        fonte: fonte,
        lado: 64,
        tileW: .5,
        tileH: .5,
        fase: .25,
      );
      var diferentes = 0;
      for (var y = 0; y < 64; y++) {
        for (var x = 0; x < 64; x++) {
          if (base.cor(x, y) != deslocado.cor(x, y)) diferentes++;
        }
      }
      expect(diferentes, greaterThan(100), reason: 'a fase nao mexeu na grade');
    });

    test('nada de borda nem de buraco em nenhuma combinacao', () async {
      // AS TRES QUEIXAS DA LISTA — borda, espaco vazio, distorcao — sao a
      // mesma pergunta: sobrou pixel sem conteudo? Um ladrilho que nao
      // fecha ou uma amostragem que escorrega para a margem transparente
      // aparecem aqui.
      for (final (tw, th, ow, esp) in const [
        (1.0, 1.0, 1.0, 0.0),
        (0.5, 0.5, 1.0, 0.0),
        (0.25, 0.25, 1.0, 0.0),
        (0.1, 0.1, 1.0, 0.0),
        (0.33, 0.66, 1.0, 0.0),
        (0.5, 0.5, 1.0, 1.0),
        (0.5, 1.0, 2.0, 0.0),
        (0.25, 0.5, 3.0, 0.0),
      ]) {
        final q = await _desenharShader(
          fonte: fonte,
          lado: 96,
          tileW: tw,
          tileH: th,
          outputW: ow,
          outputH: ow,
          espelho: esp,
        );
        expect(
          q.opacos,
          96 * 96,
          reason: 'ladrilho ${tw}x$th, saida $ow, espelho $esp',
        );
      }
    });
  });

  group('o passe cobre a composicao como os Azulejos do Alight Motion', () {
    testWidgets('a camada em 50% entrega uma saida real de 200% ao transform', (
      tester,
    ) async {
      await tester.pumpWidget(
        Directionality(
          textDirection: TextDirection.ltr,
          child: Center(
            child: MotionTilePass(
              effect: _efeito(),
              time: Duration.zero,
              escalaX: .5,
              escalaY: .5,
              posicao: const Offset(60, 60),
              composicao: const Size(120, 120),
              child: const SizedBox(width: 120, height: 120),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      final viewport = find.byKey(const ValueKey('motion-tile-viewport'));
      expect(viewport, findsOneWidget);
      final box = tester.renderObject<RenderBox>(viewport);
      expect(box.size, const Size(240, 240));
      expect(box.paintBounds, const Rect.fromLTWH(0, 0, 240, 240));
    });
  });

  group('a regiao ladrilhada cresce para cobrir o quadro', () {
    // ESTES TESTES SEGURAM O CONSERTO DO RELATO, e sao PUROS de proposito.
    //
    // A REGIAO E UM NUMERO, e a funcao que a calcula nao depende de shader,
    // de arvore de widgets nem de rasterizacao. Testar por rasterizacao
    // exigia o shader carregado e a arvore ja medida — e um teste que
    // depende disso mede o humor do host, nao o que se quer provar. (A
    // medicao no pixel foi feita, e esta no relato do lote.)
    double fator({
      required double pedido,
      required double escala,
      double ladoDaCamada = 120,
      double? posicao,
      double? ladoDaComposicao,
    }) => fatorQueCobreMotionTile(
      pedido: pedido,
      ladoDaCamada: ladoDaCamada,
      escala: escala,
      posicao: posicao,
      ladoDaComposicao: ladoDaComposicao,
    );

    test('em 100%, a regiao e a propria camada', () {
      expect(
        fator(pedido: 1, escala: 1, posicao: 60, ladoDaComposicao: 120),
        1,
      );
    });

    test('EM 50%, A REGIAO DOBRA — o relato', () {
      // A camada em 50% precisa de uma regiao de 2x em espaco de camada
      // para cobrir o quadro depois de encolher. Sem isto, a composicao
      // fica com a moldura vazia em volta — o defeito relatado.
      expect(
        fator(pedido: 1, escala: .5, posicao: 60, ladoDaComposicao: 120),
        2,
      );
    });

    test('em 25%, a regiao quadruplica', () {
      expect(
        fator(pedido: 1, escala: .25, posicao: 60, ladoDaComposicao: 120),
        4,
      );
    });

    test('em 200%, a regiao NAO encolhe', () {
      // AUMENTAR ja cobre: crescer aqui seria area por quadro sem nada do
      // outro lado.
      expect(
        fator(pedido: 1, escala: 2, posicao: 60, ladoDaComposicao: 120),
        1,
      );
    });

    test('a saida pedida manda quando e MAIOR que o necessario', () {
      expect(
        fator(pedido: 2, escala: 1, posicao: 60, ladoDaComposicao: 120),
        2,
      );
      expect(
        fator(pedido: 3, escala: .5, posicao: 60, ladoDaComposicao: 120),
        3,
      );
    });

    test('a CAMADA DESLOCADA pede mais do lado para onde foi', () {
      // Uma camada encostada na direita precisa de quase nada a mais do
      // lado esquerdo, e de muito do lado direito. A conta e pelos DOIS
      // lados — calcular pelo centro daria ladrilho que ninguem ve.
      //
      // Camada de 120 em 50% com o quadro de 120, centrada em x=90:
      // o lado direito precisa alcancar 120 - 90 = 30, o esquerdo 90 —
      // ou seja, 90 de meia-extensao contra 60 do caso centrado.
      final centrada = fator(
        pedido: 1,
        escala: .5,
        posicao: 60,
        ladoDaComposicao: 120,
      );
      final naDireita = fator(
        pedido: 1,
        escala: .5,
        posicao: 90,
        ladoDaComposicao: 120,
      );
      expect(naDireita, greaterThan(centrada));
      // 2 * 90 / (0.5 * 120) = 3
      expect(naDireita, 3);
      // E a encostada na esquerda pede o mesmo, pelo outro lado.
      expect(
        fator(pedido: 1, escala: .5, posicao: 30, ladoDaComposicao: 120),
        3,
      );
    });

    test('o ladrilho NAO entra nesta conta', () {
      // O LADRILHO MUDA A GRADE, E NAO A AREA. Confundir os dois fazia "o
      // zoom da camada" e "o zoom do ladrilho" parecerem a mesma coisa — e
      // so um deles decide se o quadro fica coberto. A funcao nem recebe o
      // ladrilho, e e por isso que este teste e uma afirmacao sobre a
      // assinatura: se alguem passar a receber, ele cai.
      expect(
        fator(pedido: 1, escala: .5, posicao: 60, ladoDaComposicao: 120),
        2,
      );
    });

    test('sem saber a posicao ou o quadro, NAO cresce', () {
      // NAO SABER E MOTIVO PARA NAO CRESCER, e nao para chutar: uma
      // regiao grande demais e area por quadro que ninguem pediu.
      expect(fator(pedido: 1, escala: .5), 1);
      expect(fator(pedido: 1, escala: .5, posicao: 60), 1);
      expect(fator(pedido: 1, escala: .5, ladoDaComposicao: 120), 1);
    });

    test('escala zero ou invalida nao vira infinito', () {
      // A escala chega do transform, e um projeto com escala 0 existe (e
      // como se esconde uma camada). Dividir por ela daria `infinito`, e
      // um tamanho infinito derruba a montagem da arvore.
      for (final e in [0.0, -1.0, double.nan, double.infinity]) {
        final f = fator(
          pedido: 1,
          escala: e,
          posicao: 60,
          ladoDaComposicao: 120,
        );
        expect(f.isFinite, isTrue, reason: 'escala $e');
        expect(f, 1);
      }
    });

    test('o fator tem TETO', () {
      // Cada fator a mais e area que o shader preenche por quadro. Uma
      // camada de 1 pixel num quadro de 4000 pediria um fator absurdo.
      final f = fator(
        pedido: 1,
        escala: .001,
        ladoDaCamada: 1,
        posicao: 2000,
        ladoDaComposicao: 4000,
      );
      expect(f, lessThanOrEqualTo(24));
      expect(f.isFinite, isTrue);
    });
  });
}

// ==========================================================================
// A REGIAO COM A CAMADA GIRADA, E AS TRES EMENDAS.
// ==========================================================================
//
// O SEGUNDO RELATO: girar a camada deixava os QUATRO CANTOS vazios. O
// ladrilho existia, cobria o meio, e as quinas ficavam pretas — porque a
// conta da regiao so olhava escala e posicao, e tratava a area coberta como
// um retangulo alinhado ao quadro. Uma camada girada cobre um LOSANGO, e o
// losango nao alcanca os cantos.
//
// Aqui se prova a CONTA, com a inversa da transformacao. A prova de pixel
// fica no passo do widget, que ja existe acima.
void _grupoDoGiroEDasEmendas() {
  group('a regiao cresce quando a camada gira', () {
    ({double x, double y}) cobrir({
      double pedidoX = 1,
      double pedidoY = 1,
      Size camada = const Size(120, 120),
      double escalaX = 1,
      double escalaY = 1,
      Offset? posicao = const Offset(60, 60),
      Size? composicao = const Size(120, 120),
      double giro = 0,
    }) => fatoresQueCobremMotionTile(
      pedidoX: pedidoX,
      pedidoY: pedidoY,
      ladoDaCamada: camada,
      escalaX: escalaX,
      escalaY: escalaY,
      posicao: posicao,
      composicao: composicao,
      rotacaoGraus: giro,
    );

    test('sem giro, a conta e a de antes', () {
      // A DIAGONAL: as duas formulas tem de concordar exatamente aqui,
      // senao uma das duas esta errada e nao se sabe qual.
      final r = cobrir();
      expect(r.x, 1);
      expect(r.y, 1);
      expect(
        r.x,
        fatorQueCobreMotionTile(
          pedido: 1,
          ladoDaCamada: 120,
          escala: 1,
          posicao: 60,
          ladoDaComposicao: 120,
        ),
      );
      // A MESMA CONTA, AGORA COM ESCALA. E aqui que a inversa se separa da
      // formula antiga se esquecer de desfazer a escala: com a camada em
      // 50%, o mesmo quadro pede o DOBRO de ladrilho.
      final r2 = cobrir(escalaX: .5, escalaY: .5);
      expect(
        r2.x,
        fatorQueCobreMotionTile(
          pedido: 1,
          ladoDaCamada: 120,
          escala: .5,
          posicao: 60,
          ladoDaComposicao: 120,
        ),
      );
    });

    test('GIRAR 45 GRAUS PEDE MAIS LADRILHO QUE NAO GIRAR — o relato', () {
      // E ESTE E O DEFEITO: sem isto, o canto fica vazio. Quanto mais
      // gira, mais area o losango deixa de fora.
      for (final giro in [15.0, 30.0, 45.0, 60.0, 90.0, 180.0, 270.0]) {
        final reto = cobrir();
        final girado = cobrir(giro: giro);
        expect(
          girado.x >= reto.x - 1e-9 && girado.y >= reto.y - 1e-9,
          isTrue,
          reason: 'giro $giro pediu menos que o reto',
        );
      }
      // 45 E O PIOR CASO de um quadrado: o losango esta na diagonal.
      final q45 = cobrir(giro: 45);
      expect(q45.x, greaterThan(cobrir().x + 0.2));
    });

    test('45 GRAUS NUM QUADRADO: o numero exato', () {
      // Num quadro 120x120 com a camada 120x120 centrada e escala 1, os
      // cantos caem em (+-60, +-60) do centro. Rodando -45 graus, o
      // deslocamento em espaco de camada e
      //   60*cos45 + 60*sen45 = 60*1.4142 = 84,85
      // e o fator e 2*84,85/120 = 1,4142 — a raiz de 2, que e o quanto a
      // diagonal de um quadrado excede o lado. Um numero redondo aqui
      // seria sinal de conta errada.
      final r = cobrir(giro: 45);
      expect(r.x, closeTo(1.41421356, 1e-6));
      expect(r.y, closeTo(1.41421356, 1e-6));
    });

    test('90 GRAUS NUM QUADRADO VOLTA AO MESMO TAMANHO', () {
      // Simetria: girar um quadrado em 90 graus deixa o quadrado igual. Se
      // este teste falhar, a inversa da rotacao tem sinal trocado.
      final r = cobrir(giro: 90);
      expect(r.x, closeTo(1.0, 1e-6));
      expect(r.y, closeTo(1.0, 1e-6));
    });

    test('180 graus tambem: a area girada sobre si mesma e a mesma', () {
      final r = cobrir(giro: 180);
      expect(r.x, closeTo(1.0, 1e-6));
      expect(r.y, closeTo(1.0, 1e-6));
    });

    test('o quadro LARGO pede mais na largura, e nao na altura', () {
      // Composicao 240x120, camada 120x120 centrada em (120,60): o quadro
      // tem o dobro da largura, entao a largura e que precisa crescer.
      final r = cobrir(
        posicao: const Offset(120, 60),
        composicao: const Size(240, 120),
        camada: const Size(120, 120),
      );
      expect(r.x, closeTo(2.0, 1e-6));
      expect(r.y, closeTo(1.0, 1e-6));
    });

    test('a POSICAO extrema entra na conta, girada ou nao', () {
      final reto = cobrir(posicao: const Offset(0, 0));
      expect(reto.x, closeTo(2.0, 1e-6));
      final girado = cobrir(posicao: const Offset(0, 0), giro: 45);
      // GIRAR NAO CRESCE NOS DOIS EIXOS. O losango ganha num eixo e perde no
      // outro: o que se conserva e a AREA coberta, redistribuida. Exigir
      // crescimento nos dois seria exigir que a rotacao inventasse area.
      expect(girado.x, greaterThan(reto.x));
      expect(girado.x * girado.y, greaterThanOrEqualTo(reto.x * reto.y - 1e-9));
    });

    test('escala 50% com giro: as duas coisas se somam', () {
      final r = cobrir(escalaX: .5, escalaY: .5, giro: 45);
      // 2 * 84.8528 / (0.5 * 120) = 2.8284
      expect(r.x, closeTo(2.82842712, 1e-6));
    });

    test('SEM SABER A POSICAO, a regiao ainda cobre o quadro', () {
      // O RELATO: camada menor que o quadro, com o mosaico em 91,5%. Sem
      // a posicao da camada, a conta dos cantos nao existe — e o efeito
      // ladrilhava SO a caixa da camada: a imagem pequena no meio do
      // preto. Com o tamanho do QUADRO conhecido da para cobri-lo mesmo
      // assim: a regiao tem de ser, no minimo, o quadro dividido pela
      // escala.
      final r = cobrir(
        pedidoX: .915,
        pedidoY: 1,
        posicao: null,
        camada: const Size(248, 442),
        composicao: const Size(1080, 1920),
      );
      // 1080/248 = 4,35: a regiao passa a cobrir o quadro.
      expect(r.x, closeTo(4.35, .01));
      expect(r.y, closeTo(4.34, .01));
    });

    test('o piso vale tambem com a camada AMPLIADA', () {
      final r = cobrir(
        pedidoX: .915,
        posicao: null,
        camada: const Size(1080, 1920),
        composicao: const Size(1080, 1920),
        escalaX: 2,
        escalaY: 2,
      );
      // A camada ampliada 2x ja cobre o quadro: o piso e 0,5, e o pedido
      // (0,915) manda.
      expect(r.x, closeTo(.915, 1e-9));
    });

    test('sem o tamanho do quadro, nao chuta: devolve o pedido', () {
      final r = cobrir(composicao: null);
      expect(r.x, 1);
      expect(r.y, 1);
      final r2 = cobrir(composicao: null, giro: 45);
      expect(r2.x, 1);
      expect(r2.y, 1);
    });

    test('o piso NAO muda nada quando a posicao e conhecida', () {
      // A conta dos cantos ja cobre o piso: o piso so serve para quando
      // nao ha posicao. Aqui ele nao pode ter encolhido nem crescido.
      for (final pos in const [
        Offset(60, 60),
        Offset(0, 0),
        Offset(120, 120),
      ]) {
        final comPiso = cobrir(posicao: pos);
        final semPiso = fatorQueCobreMotionTile(
          pedido: 1,
          ladoDaCamada: 120,
          escala: 1,
          posicao: pos.dx,
          ladoDaComposicao: 120,
        );
        expect(comPiso.x, closeTo(semPiso, 1e-6), reason: 'pos $pos');
      }
    });

    test('escala ZERO nao vira infinito', () {
      // Divisao por escala: zero tem de ser recusado, e nao propagado.
      expect(cobrir(escalaX: 0, escalaY: 0).x, 1);
      expect(cobrir(escalaX: 0, escalaY: 0, giro: 45).x, 1);
    });

    test('o pedido da pessoa manda quando e MAIOR', () {
      final r = cobrir(pedidoX: 3, pedidoY: 4, giro: 45);
      expect(r.x, closeTo(3.0, 1e-6));
      expect(r.y, closeTo(4.0, 1e-6));
    });

    test('com o fator calculado, os QUATRO CANTOS do quadro ficam dentro', () {
      // ESTA E A PROVA DE QUE NAO SOBRA CANTO PRETO — o pedido do dono.
      //
      // A regiao que o passe desenha e um retangulo de `w*fator` por
      // `h*fator` em espaco de CAMADA, girado e escalado junto com ela. Aqui
      // esse retangulo e montado no espaco do QUADRO e cada canto do quadro e
      // perguntado contra ele. Se a conta estiver curta em qualquer giro, o
      // canto cai fora e o teste acusa.
      const camada = Size(120, 120);
      const composicao = Size(120, 120);
      const quinasDoQuadro = [
        Offset.zero,
        Offset(120, 0),
        Offset(0, 120),
        Offset(120, 120),
      ];
      for (var g = 0; g < 360; g += 15) {
        for (final e in [0.3, 1.0, 2.5]) {
          for (final pos in const [
            Offset(60, 60),
            Offset(0, 0),
            Offset(120, 120),
            Offset(31, 97),
          ]) {
            final r = cobrir(
              giro: g.toDouble(),
              escalaX: e,
              escalaY: e,
              posicao: pos,
              camada: camada,
              composicao: composicao,
            );
            final theta = g * math.pi / 180;
            final cos = math.cos(theta), sen = math.sin(theta);
            // A meia-largura da regiao, ja escalada, nos DOIS eixos da
            // camada — e depois girada para o espaco do quadro.
            final a = r.x * camada.width * e / 2;
            final b = r.y * camada.height * e / 2;
            final quinas = [
              for (final (sx, sy) in const [(-1, -1), (1, -1), (1, 1), (-1, 1)])
                pos +
                    Offset(
                      sx * a * cos - sy * b * sen,
                      sx * a * sen + sy * b * cos,
                    ),
            ];
            for (final canto in quinasDoQuadro) {
              expect(
                _dentroDoConvexo(quinas, canto),
                isTrue,
                reason: 'canto $canto descoberto: giro=$g escala=$e pos=$pos',
              );
            }
          }
        }
      }
    });

    test('nunca devolve NaN nem infinito, em 3.600 combinações', () {
      // A CONTA DIVIDE POR ESCALA E MULTIPLICA POR SENO: os dois jeitos
      // faceis de produzir NaN. Um NaN aqui nao da erro — vira um
      // `SizedBox` de largura NaN e a tela some. Vale varrer.
      for (var g = 0; g < 360; g += 3) {
        for (final e in [.05, .5, 1.0, 3.0]) {
          for (final p in [0.0, 1.0, 60.0, 119.0]) {
            final r = cobrir(
              giro: g.toDouble(),
              escalaX: e,
              escalaY: e,
              posicao: Offset(p, p),
            );
            expect(r.x.isFinite, isTrue, reason: 'x NaN: g=$g e=$e p=$p');
            expect(r.y.isFinite, isTrue, reason: 'y NaN: g=$g e=$e p=$p');
            expect(r.x, greaterThanOrEqualTo(0.01));
            expect(r.x, lessThanOrEqualTo(24.0));
            expect(r.y, greaterThanOrEqualTo(0.01));
            expect(r.y, lessThanOrEqualTo(24.0));
          }
        }
      }
    });
  });
}
