// A LINHA DO TEMPO SO CONSTROI E SO DESENHA O QUE ESTA NA TELA.
//
// O RELATO (perfil no emulador): rolar a linha do tempo dava 28 fps com
// o projeto em 2D e 27 com uma cena 3D. A meta e 60, e 120 onde o
// aparelho deixar.
//
// A CAUSA NAO ERA O 3D. Nada no eixo do tempo sabia o que estava
// visivel —
//
//   * a regua riscava a duracao toda a cada quadro de rolagem;
//   * a onda montava um caminho da largura do clipe, e entregava os
//     milhares de pontos ao raster em todo quadro.
//
// O QUE ESTE ARQUIVO PRENDE, e por que assim:
//
//   * a JANELA e uma conta pura, entao se testa como conta — sem arvore;
//   * a ONDA so monta as colunas da janela, contadas pelo proprio pintor.
//
// "So as linhas da tela nascem", "o tique nao reconstroi" e "um arrasto,
// um desfazer" na timeline nova estao em test/ui/timeline/timeline_test.dart.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/peak_pyramid.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/pintores_do_clipe.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/janela_da_timeline.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// Uma piramide com um segundo de som, com forma (e nao uma linha reta,
/// que nao provaria nada).
PeakPyramid _piramide() {
  const taxa = 16000;
  const balde = 64;
  final n = taxa ~/ balde;
  final mn = Float32List(n);
  final mx = Float32List(n);
  final rms = Float32List(n);
  for (var i = 0; i < n; i++) {
    final a = i < n ~/ 2 ? (0.6 + 0.4 * (i % 7) / 7) : 0.02;
    mx[i] = a;
    mn[i] = -a;
    rms[i] = a * 0.6;
  }
  return pyramidFromBase(mn, mx, rms, taxa);
}

Float64List _fonte(double segundos) =>
    Float64List.fromList([for (var i = 0; i <= 256; i++) segundos * i / 256]);

void main() {
  group('a janela e uma conta', () {
    test('anda em baldes, e nao a cada pixel', () {
      // SE ELA MUDASSE A CADA PIXEL, quem a escuta reconstruiria a cada
      // quadro de rolagem — o contrario do que se quer.
      final a = JanelaDaTimeline.de(offset: 1000, viewport: 400, recuo: 200);
      final b = JanelaDaTimeline.de(offset: 1010, viewport: 400, recuo: 200);
      expect(b, a, reason: 'dez pixels nao podem trocar a janela');
      final longe = JanelaDaTimeline.de(
        offset: 1000 + JanelaDaTimeline.balde * 2,
        viewport: 400,
        recuo: 200,
      );
      expect(longe, isNot(a));
    });

    test('cobre a tela inteira mais a folga', () {
      final j = JanelaDaTimeline.de(offset: 1000, viewport: 400, recuo: 200);
      // Conteudo visivel: [800, 1200]. A janela tem de conter isso.
      expect(j.iniPx, lessThanOrEqualTo(800));
      expect(j.fimPx, greaterThanOrEqualTo(1200));
      expect(j.contem(800), isTrue);
      expect(j.contem(1200), isTrue);
      expect(j.cruza(700, 810), isTrue, reason: 'uma ponta ja aparece');
      expect(j.cruza(-5000, -4000), isFalse);
    });

    test('sem janela, tudo aparece', () {
      expect(JanelaDaTimeline.tudo.cruza(-1e9, -1e8), isTrue);
      expect(JanelaDaTimeline.tudo.contem(1e9), isTrue);
    });
  });

  group('a onda so monta a janela', () {
    setUp(ClipWaveformPainter.limparCache);
    tearDown(ClipWaveformPainter.limparCache);

    test('um clipe largo constroi so as colunas visiveis', () {
      // 8000 px de barra: e o que um clipe de tres minutos vira num zoom
      // qualquer. Sem janela sao 8000 colunas, tres `Float32List` e dois
      // caminhos com 16 mil pontos — por passo de zoom, por clipe.
      const largura = 8000.0;
      final janela = ValueNotifier(const JanelaDaTimeline(0, 512));
      addTearDown(janela.dispose);
      final pintor = ClipWaveformPainter(
        pyramid: _piramide(),
        fonte: _fonte(180),
        color: const Color(0xFF33E1C0),
        janela: janela,
      );
      final gravador = ui.PictureRecorder();
      pintor.paint(Canvas(gravador), const Size(largura, 40));
      gravador.endRecording().dispose();
      expect(ClipWaveformPainter.construcoes, 1);
      expect(
        ClipWaveformPainter.colunasDaUltimaConstrucao,
        lessThan(2000),
        reason: 'a onda montou muito mais do que cabe na tela',
      );
      expect(
        ClipWaveformPainter.colunasDaUltimaConstrucao,
        greaterThan(0),
        reason: 'a onda nao montou nada dentro da janela',
      );
    });

    test('sem janela, a onda inteira — o comportamento de sempre', () {
      const largura = 800.0;
      final pintor = ClipWaveformPainter(
        pyramid: _piramide(),
        fonte: _fonte(1),
        color: const Color(0xFF33E1C0),
      );
      final gravador = ui.PictureRecorder();
      pintor.paint(Canvas(gravador), const Size(largura, 40));
      gravador.endRecording().dispose();
      expect(ClipWaveformPainter.colunasDaUltimaConstrucao, largura.toInt());
    });

    test('o clipe inteiro fora da janela nao grava nada', () {
      // A barra selecionada fica na arvore mesmo longe do cabecote (e a
      // unica que pode estar em arrasto). Ela nao pode pagar a onda.
      final janela = ValueNotifier(const JanelaDaTimeline(50000, 51000));
      addTearDown(janela.dispose);
      final pintor = ClipWaveformPainter(
        pyramid: _piramide(),
        fonte: _fonte(1),
        color: const Color(0xFF33E1C0),
        janela: janela,
      );
      final gravador = ui.PictureRecorder();
      pintor.paint(Canvas(gravador), const Size(400, 40));
      gravador.endRecording().dispose();
      expect(ClipWaveformPainter.construcoes, 0);
      expect(ClipWaveformPainter.entradasNoCache, 0);
    });
  });
}
