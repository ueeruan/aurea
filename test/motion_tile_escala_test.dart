// ==========================================================================
// O MOTION TILE NAO PODE ENCOLHER A IMAGEM. MEDIDO, EM 1024x1024.
// ==========================================================================
//
// O RELATO DO DONO, com a imagem na mao: "aplico Motion Tile numa imagem
// 1:1 e a imagem COMPRIME, parece voltar, DIMINUI DE TAMANHO e PERDE
// QUALIDADE".
//
// A REGRA: o efeito so repete a textura para FORA dos limites. A copia do
// meio continua sendo a imagem original — mesmo tamanho, mesma proporcao,
// mesmo lugar, mesma resolucao aparente. Centro X/Y DESLIZAM a grade;
// Largura/Altura da saida aumentam a AREA repetida. Nenhum dos dois
// escala coisa nenhuma.
//
// ESTE ARQUIVO MEDE AS DUAS METADES DO RELATO:
//
//   1. O TAMANHO — a caixa da copia central, ANTES e DEPOIS do efeito,
//      lida pelo MESMO instrumento nas duas imagens. Uma imagem de
//      1024x1024 tem de continuar com 1024x1024 de caixa, na mesma
//      posicao;
//   2. A QUALIDADE — a diferenca media por canal entre os pixels da copia
//      central e os da imagem original. Uma reamostragem escondida no
//      meio do caminho aparece aqui como um numero grande.
//
// E O TESTE MORDE. O ultimo caso alimenta o mesmo desenho com uma textura
// reduzida a 49% (que e exatamente o que a conta antiga fazia num quadro
// 4K) e exige que a medida de qualidade ACUSE. Um teste que passa com a
// textura estragada nao estaria medindo nada.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/motion_tile.dart';
import 'package:aurea/src/features/editor/presentation/widgets/motion_tile_pass.dart';
import 'package:flutter/painting.dart';
import 'package:flutter_test/flutter_test.dart';

/// O LADO DA IMAGEM DO RELATO.
const int kLado = 1024;

/// A COMPOSICAO do relato: retrato, com a imagem 1:1 no meio.
const Size kComposicao = Size(1080, 1920);

/// A CAIXA DA CAMADA, em px da composicao.
const Size _ladoDaCamada = Size(1024, 1024);

/// A IMAGEM DE ENTRADA, 1024x1024, COM DUAS MARCAS E MUITO DETALHE FINO.
///
/// AS MARCAS sao uma coluna/linha MAGENTA na borda de cima e da esquerda e
/// uma coluna/linha CIANO na de baixo e da direita — um pixel cada. Sao
/// elas que dizem onde uma copia COMECA e onde ela TERMINA, e por isso a
/// caixa e uma medida, e nao um palpite. O miolo nunca chega perto dessas
/// duas cores (o verde e o azul ficam entre 40 e 190), entao nao ha como
/// confundir marca com conteudo.
///
/// O DETALHE FINO e um xadrez de 2 px. Ele existe para a medida de
/// qualidade ter do que reclamar: um xadrez de 2 px e a primeira coisa que
/// morre quando alguem reamostra a textura pelo caminho. Um degrade liso
/// sobreviveria a quase tudo e o teste passaria sem ver nada.
Future<ui.Image> _fonte({int lado = kLado}) async {
  final px = Uint8List(lado * lado * 4);
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final i = (y * lado + x) * 4;
      final inicio = x == 0 || y == 0;
      final fim = x == lado - 1 || y == lado - 1;
      int r, g, b;
      if (inicio) {
        (r, g, b) = (255, 0, 255); // magenta: aqui a copia comeca
      } else if (fim) {
        (r, g, b) = (0, 255, 255); // ciano: aqui a copia termina
      } else {
        final xadrez = ((x ~/ 2) + (y ~/ 2)).isEven;
        r = xadrez ? 220 : 40;
        g = 40 + (x * 150) ~/ (lado - 1);
        b = 40 + (y * 150) ~/ (lado - 1);
      }
      px[i] = r;
      px[i + 1] = g;
      px[i + 2] = b;
      px[i + 3] = 255;
    }
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(px);
  final descritor = ui.ImageDescriptor.raw(
    buffer,
    width: lado,
    height: lado,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  return (await (await descritor.instantiateCodec()).getNextFrame()).image;
}

/// UM QUADRO LIDO NO PIXEL.
class _Quadro {
  _Quadro(this.pixels, this.largura, this.altura);

  final Uint8List pixels;
  final int largura, altura;

  int canal(int x, int y, int c) => pixels[((y * largura) + x) * 4 + c];

  bool _magenta(int x, int y) =>
      canal(x, y, 0) > 200 && canal(x, y, 1) < 60 && canal(x, y, 2) > 200;
  bool _ciano(int x, int y) =>
      canal(x, y, 0) < 60 && canal(x, y, 1) > 200 && canal(x, y, 2) > 200;

  List<int> colunasDeInicio(int y) => [
    for (var x = 0; x < largura; x++)
      if (_magenta(x, y)) x,
  ];
  List<int> colunasDeFim(int y) => [
    for (var x = 0; x < largura; x++)
      if (_ciano(x, y)) x,
  ];
  List<int> linhasDeInicio(int x) => [
    for (var y = 0; y < altura; y++)
      if (_magenta(x, y)) y,
  ];
  List<int> linhasDeFim(int x) => [
    for (var y = 0; y < altura; y++)
      if (_ciano(x, y)) y,
  ];
}

/// A CAIXA DA COPIA QUE CONTEM O CENTRO DO QUADRO.
///
/// O inicio e a ultima marca de comeco ANTES do centro; o fim e a primeira
/// marca de termino DEPOIS dele. Com uma copia so (a imagem sem efeito) ou
/// com a parede inteira de copias, o instrumento e o mesmo — e por isso o
/// "antes" e o "depois" sao comparaveis.
Rect? _caixaCentral(_Quadro q) {
  final meioX = q.largura ~/ 2, meioY = q.altura ~/ 2;
  // A varredura foge das proprias marcas: numa linha de marca tudo e
  // magenta e a leitura nao diria nada.
  final linha = meioY, coluna = meioX;
  final iniX = q.colunasDeInicio(linha).where((x) => x <= meioX);
  final fimX = q.colunasDeFim(linha).where((x) => x >= meioX);
  final iniY = q.linhasDeInicio(coluna).where((y) => y <= meioY);
  final fimY = q.linhasDeFim(coluna).where((y) => y >= meioY);
  if (iniX.isEmpty || fimX.isEmpty || iniY.isEmpty || fimY.isEmpty) return null;
  return Rect.fromLTRB(
    iniX.last.toDouble(),
    iniY.last.toDouble(),
    fimX.first + 1,
    fimY.first + 1,
  );
}

/// TODAS AS LARGURAS DE COPIA VISIVEIS numa linha: de cada marca de comeco
/// ate a primeira marca de termino a sua direita.
List<int> _largurasDeCopia(_Quadro q, int linha) {
  final inicios = q.colunasDeInicio(linha);
  final fins = q.colunasDeFim(linha);
  final larguras = <int>[];
  for (final i in inicios) {
    final f = fins.where((x) => x > i);
    if (f.isEmpty) continue;
    larguras.add(f.first - i + 1);
  }
  return larguras;
}

Future<_Quadro> _lerPixels(ui.Image img) async {
  final dados = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
  return _Quadro(dados!.buffer.asUint8List(), img.width, img.height);
}

/// A FOTO DA CAMADA, reamostrada no fator pedido — e o que
/// `_TileRender.paint` faz com `Layer.toImageSync`, sem precisar do
/// rasterizador de verdade (que num teste de widget nunca devolve).
Future<ui.Image> _foto(ui.Image fonte, double fator) async {
  if (fator == 1) return fonte;
  final w = (fonte.width * fator).round(), h = (fonte.height * fator).round();
  final gravador = ui.PictureRecorder();
  ui.Canvas(gravador).drawImageRect(
    fonte,
    Rect.fromLTWH(0, 0, fonte.width.toDouble(), fonte.height.toDouble()),
    Rect.fromLTWH(0, 0, w.toDouble(), h.toDouble()),
    ui.Paint()..filterQuality = FilterQuality.medium,
  );
  return gravador.endRecording().toImage(w, h);
}

/// ANTES DO EFEITO: a imagem no lugar dela, sem passar por ladrilho nenhum.
Future<_Quadro> _semEfeito(ui.Image fonte, Size quadro) async {
  final gravador = ui.PictureRecorder();
  final canvas = ui.Canvas(gravador);
  canvas.drawImage(
    fonte,
    Offset(
      (quadro.width - fonte.width) / 2,
      (quadro.height - fonte.height) / 2,
    ),
    ui.Paint(),
  );
  final img = await gravador.endRecording().toImage(
    quadro.width.round(),
    quadro.height.round(),
  );
  final q = await _lerPixels(img);
  img.dispose();
  return q;
}

/// DEPOIS DO EFEITO: o mesmo quadro, agora desenhado pelo passe.
Future<_Quadro> _comEfeito({
  required ui.FragmentProgram programa,
  required ui.Image textura,
  required Size fonte,
  required Size saida,
  ParametrosDoMotionTile parametros = const ParametrosDoMotionTile(),
}) async {
  final shader = programa.fragmentShader();
  try {
    final gravador = ui.PictureRecorder();
    pintarMotionTile(
      canvas: ui.Canvas(gravador),
      shader: shader,
      textura: textura,
      fonte: fonte,
      saida: saida,
      parametros: parametros,
    );
    final img = await gravador.endRecording().toImage(
      saida.width.round(),
      saida.height.round(),
    );
    final q = await _lerPixels(img);
    img.dispose();
    return q;
  } finally {
    shader.dispose();
  }
}

/// A DIFERENCA MEDIA POR CANAL entre a copia central e a imagem original.
///
/// O miolo, sem as marcas: elas sao um pixel de cor chapada e nao dizem
/// nada sobre reamostragem.
double _diferencaMedia(_Quadro depois, _Quadro fonte, Rect caixa) {
  var soma = 0.0;
  var n = 0;
  for (var y = 1; y < fonte.altura - 1; y++) {
    for (var x = 1; x < fonte.largura - 1; x++) {
      final ox = caixa.left.round() + x, oy = caixa.top.round() + y;
      if (ox < 0 || oy < 0 || ox >= depois.largura || oy >= depois.altura) {
        continue;
      }
      for (var c = 0; c < 3; c++) {
        soma += (depois.canal(ox, oy, c) - fonte.canal(x, y, c)).abs();
        n++;
      }
    }
  }
  return n == 0 ? double.infinity : soma / n;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late ui.FragmentProgram programa;
  late ui.Image fonte;
  late _Quadro pixelsDaFonte;

  setUpAll(() async {
    programa = await ui.FragmentProgram.fromAsset('shaders/motion_tile.frag');
    fonte = await _fonte();
    pixelsDaFonte = await _lerPixels(fonte);
  });
  tearDownAll(() => fonte.dispose());

  /// A REGIAO LADRILHADA do relato, calculada pela conta de verdade do
  /// passe — nao por um numero escrito a mao aqui.
  ({double x, double y}) cobertura({double pedidoX = 1, double pedidoY = 1}) =>
      fatoresQueCobremMotionTile(
        pedidoX: pedidoX,
        pedidoY: pedidoY,
        ladoDaCamada: _ladoDaCamada,
        escalaX: 1,
        escalaY: 1,
        posicao: Offset(kComposicao.width / 2, kComposicao.height / 2),
        composicao: kComposicao,
      );

  group('a imagem 1:1 nao encolhe nem borra (o relato do dono)', () {
    test('A CAIXA DA COPIA CENTRAL E A MESMA, ANTES E DEPOIS', () async {
      final c = cobertura();
      final saida = Size(kLado * c.x, kLado * c.y);
      expect(saida, kComposicao, reason: 'a regiao ladrilhada mudou de tamanho');

      final antes = await _semEfeito(fonte, saida);
      final caixaAntes = _caixaCentral(antes);
      expect(caixaAntes, isNotNull, reason: 'nao achei a imagem no quadro');
      expect(caixaAntes!.width, kLado.toDouble());
      expect(caixaAntes.height, kLado.toDouble());

      final ratio = ratioDaCapturaMotionTile(
        camada: _ladoDaCamada,
        pixelRatio: 1,
      );
      final textura = await _foto(fonte, ratio);
      final depois = await _comEfeito(
        programa: programa,
        textura: textura,
        fonte: _ladoDaCamada,
        saida: saida,
      );
      if (!identical(textura, fonte)) textura.dispose();

      final caixaDepois = _caixaCentral(depois);
      expect(caixaDepois, isNotNull, reason: 'a copia central sumiu');
      // A DIFERENCA TEM DE SER PRATICAMENTE ZERO: um pixel de folga cobre
      // o arredondamento da amostragem, e nada alem disso.
      expect(
        (caixaDepois!.width - caixaAntes.width).abs(),
        lessThanOrEqualTo(1),
        reason: 'a copia central mudou de LARGURA: $caixaDepois vs $caixaAntes',
      );
      expect(
        (caixaDepois.height - caixaAntes.height).abs(),
        lessThanOrEqualTo(1),
        reason: 'a copia central mudou de ALTURA: $caixaDepois vs $caixaAntes',
      );
      expect(
        (caixaDepois.left - caixaAntes.left).abs(),
        lessThanOrEqualTo(1),
        reason: 'a copia central SAIU DO LUGAR',
      );
      expect(
        (caixaDepois.top - caixaAntes.top).abs(),
        lessThanOrEqualTo(1),
        reason: 'a copia central SAIU DO LUGAR',
      );
      // E A PROPORCAO CONTINUA 1:1 — o relato fala em "comprime".
      expect(caixaDepois.width / caixaDepois.height, closeTo(1, .002));
    });

    test('A QUALIDADE DA COPIA CENTRAL NAO CAI', () async {
      final c = cobertura();
      final saida = Size(kLado * c.x, kLado * c.y);
      final ratio = ratioDaCapturaMotionTile(
        camada: _ladoDaCamada,
        pixelRatio: 1,
      );
      final textura = await _foto(fonte, ratio);
      final depois = await _comEfeito(
        programa: programa,
        textura: textura,
        fonte: _ladoDaCamada,
        saida: saida,
      );
      if (!identical(textura, fonte)) textura.dispose();

      final caixa = _caixaCentral(depois)!;
      final erro = _diferencaMedia(depois, pixelsDaFonte, caixa);
      // UM CANAL DE 0 A 255. A copia central e uma copia, e nao uma
      // aproximacao: o que sobra e arredondamento de amostragem.
      expect(
        erro,
        lessThan(1.0),
        reason: 'a copia central perdeu qualidade: erro medio $erro por canal',
      );
    });

    test('O TESTE MORDE: com a textura reduzida, a medida ACUSA', () async {
      // A CONTA ANTIGA era `min(pixelRatio, sqrt(2e6/area))`, e num quadro
      // 4K ela dava 0,49 — a textura saia com METADE do lado. Aqui essa
      // textura e alimentada de proposito: se o teste de qualidade acima
      // passasse com ela tambem, ele nao estaria medindo nada.
      final c = cobertura();
      final saida = Size(kLado * c.x, kLado * c.y);
      final textura = await _foto(fonte, .49);
      final depois = await _comEfeito(
        programa: programa,
        textura: textura,
        fonte: _ladoDaCamada,
        saida: saida,
      );
      textura.dispose();
      final caixa = _caixaCentral(depois);
      final erro = caixa == null
          ? double.infinity
          : _diferencaMedia(depois, pixelsDaFonte, caixa);
      expect(
        erro,
        greaterThan(5.0),
        reason: 'a medida de qualidade nao viu uma textura pela metade',
      );
    });
  });

  group('os controles mudam a AREA, e nunca a escala', () {
    test('LARGURA/ALTURA DA SAIDA em 200% nao encolhem a copia', () async {
      final c = cobertura(pedidoX: 2, pedidoY: 2);
      expect(c.x, 2.0);
      expect(c.y, 2.0);
      final saida = Size(kLado * c.x, kLado * c.y);
      final depois = await _comEfeito(
        programa: programa,
        textura: fonte,
        fonte: _ladoDaCamada,
        saida: saida,
      );
      final caixa = _caixaCentral(depois);
      expect(caixa, isNotNull);
      expect(
        caixa!.width,
        kLado.toDouble(),
        reason: 'dobrar a saida encolheu a copia: $caixa',
      );
      expect(caixa.height, kLado.toDouble());
      // E A AREA CRESCEU DE VERDADE: mais de uma copia por linha.
      expect(
        _largurasDeCopia(depois, depois.altura ~/ 2).length,
        greaterThanOrEqualTo(1),
      );
      expect(
        depois.colunasDeInicio(depois.altura ~/ 2).length,
        greaterThan(1),
        reason: 'a saida em 200% nao acrescentou ladrilho nenhum',
      );
    });

    test('CENTRO X DESLIZA a grade, sem mexer no tamanho', () async {
      final c = cobertura(pedidoX: 2, pedidoY: 2);
      final saida = Size(kLado * c.x, kLado * c.y);
      Future<_Quadro> com(double centro) => _comEfeito(
        programa: programa,
        textura: fonte,
        fonte: _ladoDaCamada,
        saida: saida,
        parametros: ParametrosDoMotionTile(centroX: centro),
      );
      final meio = await com(.5);
      final movido = await com(.6);
      final linha = meio.altura ~/ 2;

      // TODA COPIA INTEIRA VISIVEL continua com 1024 de largura, com a
      // grade no lugar e com a grade deslizada.
      for (final q in [meio, movido]) {
        final larguras = _largurasDeCopia(q, linha);
        expect(larguras, isNotEmpty);
        for (final l in larguras) {
          expect(l, kLado, reason: 'copia com largura $l');
        }
      }
      // E ELA REALMENTE DESLIZOU: 0,1 da camada = 102,4 px.
      final a = meio.colunasDeInicio(linha).first;
      final b = movido.colunasDeInicio(linha).first;
      expect((b - a).toDouble(), closeTo(102.4, 1.5));
    });
  });

  group('o mosaico volta a poder CRESCER', () {
    test('a ficha deixa o mosaico chegar a 300%', () {
      final spec = efeitosMotionTile[EffectType.motionTile]!;
      // O TETO DE 100% era o que prendia a pessoa no "so encolhe".
      expect(spec.params['tile_width']!.max, 300);
      expect(spec.params['tile_height']!.max, 300);
      expect(spec.params['tile_width']!.initial, 100);
    });

    test('MOSAICO EM 200% desenha um ladrilho do DOBRO — medido', () async {
      // Com a saida em 300% cabe uma copia inteira no quadro, e a medida
      // e a mesma do resto do arquivo: da marca de comeco a de termino.
      final saida = Size(kLado * 3, kLado * 3);
      Future<int> larguraCom(double mosaicoEmPorcento) async {
        final q = await _comEfeito(
          programa: programa,
          textura: fonte,
          fonte: _ladoDaCamada,
          saida: saida,
          parametros: ParametrosDoMotionTile(
            ladrilhoX: mosaicoEmPorcento / 100,
            ladrilhoY: mosaicoEmPorcento / 100,
          ),
        );
        final larguras = _largurasDeCopia(q, q.altura ~/ 2);
        expect(larguras, isNotEmpty, reason: 'nenhuma copia inteira no quadro');
        return larguras.first;
      }

      expect(await larguraCom(100), closeTo(kLado, 4));
      expect(
        await larguraCom(200),
        closeTo(kLado * 2, 4),
        reason: 'o mosaico em 200% nao ficou maior que a camada',
      );
    });
  });

  group('a foto da camada nunca tem menos pixel que a camada', () {
    test('NENHUM TAMANHO DE CAMADA cai abaixo de 1', () {
      for (final camada in const [
        Size(1024, 1024),
        Size(1080, 1920),
        Size(1920, 1920),
        Size(3840, 2160),
        Size(7680, 4320),
        Size(1, 1),
      ]) {
        for (final dpr in const [1.0, 1.5, 2.0, 3.0, 4.0]) {
          final r = ratioDaCapturaMotionTile(camada: camada, pixelRatio: dpr);
          expect(
            r,
            greaterThanOrEqualTo(1.0),
            reason: 'camada $camada em $dpr devolveu $r — perda permanente',
          );
          expect(r, lessThanOrEqualTo(3.0));
          expect(r.isFinite, isTrue);
        }
      }
    });

    test('O QUADRO 4K EXPORTA INTEIRO — era a metade', () {
      // A conta antiga: sqrt(2e6 / (3840*2160)) = 0,49.
      expect(
        ratioDaCapturaMotionTile(
          camada: const Size(3840, 2160),
          pixelRatio: 1,
        ),
        1.0,
      );
    });

    test('numero podre nao vira NaN nem zero', () {
      for (final camada in const [Size(0, 0), Size(-4, 10), Size.zero]) {
        expect(
          ratioDaCapturaMotionTile(camada: camada, pixelRatio: 3),
          greaterThanOrEqualTo(1.0),
        );
      }
      for (final dpr in const [0.0, -2.0, double.nan, double.infinity]) {
        final r = ratioDaCapturaMotionTile(
          camada: const Size(1024, 1024),
          pixelRatio: dpr,
        );
        expect(r.isFinite, isTrue, reason: 'dpr $dpr');
        expect(r, greaterThanOrEqualTo(1.0));
      }
    });

    test('a camada pequena ainda pode gastar o aparelho inteiro', () {
      // Um teto que nunca sobe de 1 jogaria fora a nitidez que o celular
      // tem de graca numa camada pequena.
      expect(
        ratioDaCapturaMotionTile(camada: const Size(256, 256), pixelRatio: 3),
        3.0,
      );
    });
  });
}
