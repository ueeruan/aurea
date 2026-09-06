import 'package:flutter/foundation.dart';
import 'package:flutter/scheduler.dart';

import 'preview_stats.dart';

/// Clock mestre da composicao.
///
/// Um unico [Ticker] alimenta [time] (ValueNotifier); quem depende do tempo
/// escuta o notifier direto, sem setState por frame. Toda a UI (preview,
/// playhead, contador) deriva deste valor.
class PlaybackController {
  PlaybackController({
    required TickerProvider vsync,
    required this.durationOf,
  }) {
    _ticker = vsync.createTicker(_onTick);
  }

  final Duration Function() durationOf;
  late final Ticker _ticker;

  final ValueNotifier<Duration> time = ValueNotifier(Duration.zero);
  final ValueNotifier<bool> playing = ValueNotifier(false);

  /// TOCANDO, visto de qualquer lugar.
  ///
  /// O preview precisa saber disto para desenhar em rascunho enquanto
  /// roda — uma cena 3D inteira nao cabe em 33 ms num celular — e quem
  /// desenha esta longe demais do clock para receber isto por
  /// parametro.
  static final ValueNotifier<bool> tocandoAgora = ValueNotifier(false);

  /// LOOP: ao chegar no fim, volta ao inicio sem parar o relogio.
  final ValueNotifier<bool> loop = ValueNotifier(false);

  /// Taxa da COMPOSICAO (fps do projeto). O ticker roda a cada vsync
  /// (60-120 Hz), mas o clock so notifica quando o FRAME da composicao
  /// muda — num painel de 90 Hz com projeto de 30 fps, isso corta 2/3
  /// das recomposicoes (spec motor-de-preview, C1: compor na taxa da
  /// composicao, nunca na da tela).
  int compositionFps = 30;

  Duration _base = Duration.zero;

  void _onTick(Duration elapsed) {
    final t = _base + elapsed;
    final end = durationOf();
    if (t >= end) {
      if (loop.value && end > Duration.zero) {
        // Recomeca de zero no proximo tick: a base passa a ser -elapsed.
        _base = Duration.zero - elapsed;
        time.value = Duration.zero;
        return;
      }
      time.value = end;
      pause();
      return;
    }
    final quantized = _naGrade(t);
    if (quantized != time.value) {
      // Cadencia (marchas §6): a metrica de suavidade e a VARIANCIA do
      // intervalo entre ticks, nao a media de fps. FrameLog mede no
      // ponto de APRESENTACAO (travada-periodica, PR-J0).
      PreviewStats.clockTick();
      FrameLog.present();
      time.value = quantized;
    }
  }

  /// ANCORAGEM CONTINUA na midia (PR-J1, fim da deriva por construcao).
  ///
  /// O padrao "se a diferenca passar de X, corrige" corrige em BLOCO — e
  /// o bloco E a travada periodica. Aqui o erro medido contra a posicao
  /// real do player e absorvido em fracoes, a cada amostra: a deriva
  /// nunca acumula, entao nunca existe correcao em bloco. Como a imagem
  /// do video vem da textura da plataforma, deslocar o relogio em alguns
  /// ms nao mexe um pixel — ao contrario do seek, que esvazia o decoder.
  void anchorToMedia(Duration mediaTime) {
    if (!playing.value) return;
    final errUs = mediaTime.inMicroseconds - time.value.inMicroseconds;
    if (errUs.abs() > 1000000) {
      // Dessincronia REAL (app em background, midia reiniciada): nao e
      // deriva — realinha de uma vez.
      _base += Duration(microseconds: errUs);
      debugBaseShiftUs = errUs;
      return;
    }
    // Slew proporcional, teto de 20 ms por amostra (~2 amostras/s).
    final step = (errUs * 0.25).round().clamp(-20000, 20000);
    debugBaseShiftUs = step;
    if (step != 0) _base += Duration(microseconds: step);
  }

  /// Ultimo deslocamento aplicado pela ancoragem (us) — so para teste e
  /// diagnostico: mostra que a correcao e fracionada, nunca em bloco.
  int debugBaseShiftUs = 0;

  void play() {
    if (playing.value) return;
    final end = durationOf();
    if (end == Duration.zero) return;
    if (time.value >= end) time.value = Duration.zero;
    _base = time.value;
    _ticker.start();
    playing.value = true;
    tocandoAgora.value = true;
    FrameLog.reset();
  }

  void pause() {
    if (_ticker.isActive) _ticker.stop();
    playing.value = false;
    tocandoAgora.value = false;
    // Intervalo atravessando a pausa nao e jitter.
    PreviewStats.clockReset();
  }

  void toggle() => playing.value ? pause() : play();

  /// Encaixa na grade de quadros da composicao.
  ///
  /// O tick ja fazia isso, e so ele — entao um seek caia entre dois
  /// quadros e o primeiro tick seguinte "voltava" ate a grade. Alguns
  /// microssegundos, invisiveis, mas um retrocesso do cabecote: quem
  /// olha o valor ve o tempo andar para tras.
  Duration _naGrade(Duration t) {
    final f = compositionFps < 1 ? 30 : compositionFps;
    // Pelo INDICE do quadro, nao pelo passo em microssegundos: a 30 fps
    // o passo nao e inteiro (33333,33 us), e arredondar o passo erra
    // 50 us a cada 150 quadros. Cinco segundos deixavam de cair em cinco
    // segundos — pequeno demais para ver, grande o bastante para o
    // relogio nao bater com o quadro exportado.
    final quadro = (t.inMicroseconds * f) ~/ 1000000;
    return Duration(microseconds: (quadro * 1000000) ~/ f);
  }

  void seek(Duration t) {
    final end = durationOf();
    var v = _naGrade(t);
    if (v < Duration.zero) v = Duration.zero;
    if (v > end) v = end;
    if (playing.value) {
      _ticker.stop();
      _base = v;
      time.value = v;
      _ticker.start();
    } else {
      time.value = v;
    }
  }

  void dispose() {
    _ticker.dispose();
    time.dispose();
    playing.dispose();
    loop.dispose();
  }
}
