// A ONDA DO CLIPE NAO PODE SER RECONSTRUIDA A CADA QUADRO.
//
// O RELATO (testador, com musica na linha do tempo): "quando eu coloco a
// musica e dou play, de primeira ele vai normal sem dar lag nenhum, mas
// quando aparece [a onda] na musica ele comeca a dar os lags".
//
// A CAUSA NAO ERA O AUDIO, ERA A ONDA. A linha do tempo rola atras do
// cabecote com um `jumpTo` por quadro, e o conteudo horizontal e um
// `SingleChildScrollView` — nao ha `RepaintBoundary` por clipe como
// haveria num `ListView`. Sem tique nao ha repintura, entao parado tudo
// fica bom; ao dar play, cada quadro repinta a faixa visivel inteira. E a
// onda e a coisa mais cara dali: tres `Float32List` do tamanho da barra e
// DOIS caminhos com milhares de pontos, montados de novo para desenhar
// exatamente a mesma figura.
//
// O QUE ESTE TESTE MEDE, e por que ele mede assim:
//
//   * CONTA AS CHAMADAS, e nao o relogio. Um tempo em milissegundos numa
//     maquina de teste nao diz nada sobre um celular — e a diferenca entre
//     "reconstroi" e "reexecuta" aparece no que chega ao canvas, nao no
//     cronometro;
//   * a PRIMEIRA pintura constroi (3 `drawPath`); da segunda em diante o
//     que chega e UM `drawPicture`. Se alguem tirar o cache, a contagem
//     volta a tres por quadro e o teste cai;
//   * um teste separado prova que o desenho CONTINUA O MESMO: pixels e
//     nao chamadas. Um cache que devolvesse a gravacao errada seria pior
//     do que nao ter cache.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/peak_pyramid.dart';
import 'package:aurea/src/features/editor/presentation/am/clip_preview_painters.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// GRAVA O QUE CHEGA AO CANVAS.
class _Caneta implements Canvas {
  final caminhos = <Rect>[];
  final gravacoes = <ui.Picture>[];

  @override
  void drawPath(ui.Path path, Paint paint) => caminhos.add(path.getBounds());

  @override
  void drawPicture(ui.Picture picture) => gravacoes.add(picture);

  @override
  void drawLine(Offset a, Offset b, Paint paint) {}

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

/// UMA PIRAMIDE COM UM SEGUNDO DE AUDIO, com ataque e silencio.
PeakPyramid _piramide() {
  const taxa = 16000;
  const balde = 64; // o primeiro tamanho de balde da piramide
  final n = taxa ~/ balde;
  final mn = Float32List(n);
  final mx = Float32List(n);
  final rms = Float32List(n);
  for (var i = 0; i < n; i++) {
    // Meio segundo de som, meio de silencio: a onda tem forma, e nao uma
    // linha reta que nao provaria nada.
    final a = i < n ~/ 2 ? (0.6 + 0.4 * (i % 7) / 7) : 0.02;
    mx[i] = a;
    mn[i] = -a;
    rms[i] = a * 0.6;
  }
  return pyramidFromBase(mn, mx, rms, taxa);
}

/// A FONTE: n+1 instantes uniformes na largura.
Float64List _fonte(double segundos) =>
    Float64List.fromList([for (var i = 0; i <= 256; i++) segundos * i / 256]);

ClipWaveformPainter _pintor({double gain = 1, bool muted = false}) =>
    ClipWaveformPainter(
      pyramid: _piramide(),
      fonte: _fonte(1),
      color: const Color(0xFF33E1C0),
      contorno: const Color(0xFFB9FFF0),
      gain: gain,
      muted: muted,
    );

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  setUp(ClipWaveformPainter.limparCache);
  tearDown(ClipWaveformPainter.limparCache);

  group('a onda e construida UMA vez, e nao a cada quadro', () {
    test('a primeira pintura constroi; as seguintes reexecutam', () {
      final pintor = _pintor();
      const tamanho = Size(400, 40);

      final primeira = _Caneta();
      pintor.paint(primeira, tamanho);
      // O QUE CHEGA A CANETA DE FORA E A GRAVACAO, e nao os caminhos: o
      // desenho e montado dentro de um gravador. A prova de que ele NAO
      // foi refeito esta no contador de construcoes, e nao no que a
      // caneta ve.
      expect(primeira.gravacoes, hasLength(1));
      expect(
        ClipWaveformPainter.construcoes,
        1,
        reason: 'a primeira pintura tem de construir uma vez',
      );

      // SESSENTA QUADROS DEPOIS: a gravacao e reexecutada, e a onda NAO
      // e reconstruida nem uma vez.
      for (var quadro = 0; quadro < 60; quadro++) {
        final caneta = _Caneta();
        pintor.paint(caneta, tamanho);
        expect(caneta.gravacoes, hasLength(1), reason: 'quadro $quadro');
      }
      expect(
        ClipWaveformPainter.construcoes,
        1,
        reason: 'a onda foi reconstruida durante a rolagem',
      );
    });

    test('mudar o ganho invalida a gravacao', () {
      // O GANHO MUDA A ALTURA. Uma gravacao reaproveitada entre ganhos
      // diferentes mostraria a onda do ganho antigo.
      const tamanho = Size(400, 40);
      _pintor(gain: 1).paint(_Caneta(), tamanho);
      final outro = _Caneta();
      _pintor(gain: 4).paint(outro, tamanho);
      expect(ClipWaveformPainter.construcoes, 2);
    });

    test('mudar o mudo invalida a gravacao', () {
      const tamanho = Size(400, 40);
      _pintor(muted: false).paint(_Caneta(), tamanho);
      final outro = _Caneta();
      _pintor(muted: true).paint(outro, tamanho);
      expect(ClipWaveformPainter.construcoes, 2);
    });

    test('mudar o TAMANHO invalida a gravacao', () {
      // A barra muda de altura quando a densidade muda; uma gravacao do
      // tamanho antigo sairia esticada.
      _pintor().paint(_Caneta(), const Size(400, 40));
      final outro = _Caneta();
      _pintor().paint(outro, const Size(400, 64));
      expect(ClipWaveformPainter.construcoes, 2);
    });

    test('o cache tem TETO', () {
      // SEM TETO ele guardaria uma gravacao por clipe ja visto na sessao
      // — e uma linha do tempo longa tem centenas.
      for (var i = 0; i < 80; i++) {
        _pintor(gain: 1 + i.toDouble()).paint(_Caneta(), const Size(400, 40));
      }
      expect(
        ClipWaveformPainter.entradasNoCache,
        lessThanOrEqualTo(32),
        reason: 'o cache cresceu sem limite',
      );
    });

    test('a linha do tempo que troca de projeto pode esvaziar o cache', () {
      _pintor().paint(_Caneta(), const Size(400, 40));
      expect(ClipWaveformPainter.entradasNoCache, greaterThan(0));
      ClipWaveformPainter.limparCache();
      expect(ClipWaveformPainter.entradasNoCache, 0);
      // E depois de limpar, a proxima pintura constroi de novo.
      final caneta = _Caneta();
      _pintor().paint(caneta, const Size(400, 40));
      expect(ClipWaveformPainter.construcoes, 1);
      expect(caneta.gravacoes, hasLength(1));
    });
  });

  group('o desenho continua o MESMO', () {
    test('a gravacao pinta exatamente os MESMOS PIXELS', () async {
      // UM CACHE QUE DEVOLVE A GRAVACAO ERRADA E PIOR DO QUE NAO TER
      // CACHE — e a unica prova que nao deixa duvida e comparar PIXEL.
      //
      // `Picture` nao se deixa reexecutar num canvas que a gente grave
      // (nao tem `paint`), entao a comparacao e por RASTERIZACAO: a
      // primeira pintura constroi os caminhos, a segunda reexecuta a
      // gravacao, e as duas imagens tem de sair identicas, byte a byte.
      const tamanho = Size(300, 36);
      final pintor = _pintor();

      Future<Uint8List> rasterizar() async {
        final gravador = ui.PictureRecorder();
        pintor.paint(Canvas(gravador), tamanho);
        final imagem = await gravador.endRecording().toImage(
          tamanho.width.toInt(),
          tamanho.height.toInt(),
        );
        final bytes = await imagem.toByteData(
          format: ui.ImageByteFormat.rawRgba,
        );
        imagem.dispose();
        return bytes!.buffer.asUint8List();
      }

      final primeira = await rasterizar();
      // Da segunda em diante a pintura vem da gravacao.
      final segunda = await rasterizar();
      expect(
        segunda,
        orderedEquals(primeira),
        reason: 'a gravacao nao devolve o mesmo desenho',
      );
      // E a imagem nao e vazia: sem isto, "iguais" poderia ser "os dois
      // em branco".
      expect(
        primeira.where((b) => b != 0).length,
        greaterThan(100),
        reason: 'a onda nao pintou nada',
      );
    });

    test('a onda ocupa a largura e a altura da barra', () async {
      const tamanho = Size(300, 36);
      final pintor = _pintor();
      final gravador = ui.PictureRecorder();
      pintor.paint(Canvas(gravador), tamanho);
      final imagem = await gravador.endRecording().toImage(
        tamanho.width.toInt(),
        tamanho.height.toInt(),
      );
      final bytes = await imagem.toByteData(
        format: ui.ImageByteFormat.rawRgba,
      );
      imagem.dispose();
      final rgba = bytes!.buffer.asUint8List();
      // A PRIMEIRA E A ULTIMA COLUNA tem de estar pintadas — a onda vai
      // de ponta a ponta. Se ela parasse no meio, a barra mostraria som
      // so na metade, que e o defeito que nao da para ver num teste de
      // chamadas.
      int alfa(int x, int y) => rgba[(y * tamanho.width.toInt() + x) * 4 + 3];
      var pintados = 0;
      for (var x = 0; x < tamanho.width.toInt(); x++) {
        if (alfa(x, tamanho.height ~/ 2) > 0) pintados++;
      }
      expect(pintados, greaterThan(tamanho.width * 0.9));
    });

    test('sem audio, nao desenha nada', () {
      // O CAMINHO VAZIO NAO PODE ENTRAR NO CACHE como se fosse desenho: um
      // clipe sem onda nao pinta, e nao ha o que gravar.
      final vazio = ClipWaveformPainter(
        pyramid: PeakPyramid(const [], 16000),
        fonte: _fonte(1),
        color: const Color(0xFF33E1C0),
      );
      final caneta = _Caneta();
      vazio.paint(caneta, const Size(300, 36));
      expect(caneta.gravacoes, isEmpty);
      expect(caneta.caminhos, isEmpty);
      expect(ClipWaveformPainter.entradasNoCache, 0);
      expect(ClipWaveformPainter.construcoes, 0);
    });
  });
}
