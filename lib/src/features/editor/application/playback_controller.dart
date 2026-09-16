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
  int seekRevision = 0;

  /// O proximo tick pode publicar um instante ANTERIOR ao atual (so
  /// depois de um realinhamento real para tras).
  bool _podeVoltar = false;

  void _onTick(Duration elapsed) {
    final t = _base + elapsed;
    final end = durationOf();
    if (t >= end) {
      if (loop.value && end > Duration.zero) {
        // Recomeca de zero no proximo tick: a base passa a ser -elapsed.
        _base = Duration.zero - elapsed;
        seekRevision++;
        time.value = Duration.zero;
        return;
      }
      time.value = end;
      pause();
      return;
    }
    final quantized = _naGrade(t);
    // MONOTONO: a ancoragem na midia pode puxar a base alguns ms para
    // tras, e perto da fronteira de um quadro isso devolvia o quadro
    // anterior — a composicao repetia um quadro, um soluco visivel. Puxar
    // para tras agora so SEGURA o relogio ate a midia alcancar. Seek e
    // loop mudam [time] direto; so o realinhamento real (>1 s) volta.
    if (quantized > time.value || (_podeVoltar && quantized != time.value)) {
      _podeVoltar = false;
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
  static const _zonaMortaUs = 25000;

  void anchorToMedia(Duration mediaTime) {
    if (!playing.value) return;
    final errUs = mediaTime.inMicroseconds - time.value.inMicroseconds;
    if (errUs.abs() > 1000000) {
      // Dessincronia REAL (app em background, midia reiniciada): nao e
      // deriva — realinha de uma vez.
      _base += Duration(microseconds: errUs);
      debugBaseShiftUs = errUs;
      _podeVoltar = errUs < 0;
      return;
    }
    // Slew proporcional. O plugin publica posicao a cada 100 ms (dez
    // amostras por segundo, e nao duas como quando isto foi escrito):
    // 25% com teto de 20 ms por amostra chegava a mexer 20% na velocidade
    // do relogio, e o texto por cima do video acelerava e freava. 10% com
    // teto de 8 ms segura a variacao abaixo de 8%.
    //
    // ZONA MORTA (beta 89, relato "com musica a variancia entre ticks
    // aumenta"): a posicao do AUDIO chega em saltos do tamanho do buffer
    // (20-40 ms), entao o erro medido pula para os dois lados a cada
    // amostra. Corrigir esse ruido mexia no relogio dez vezes por segundo
    // e era a variancia que o diagnostico via, com fps intacto. Abaixo de
    // 25 ms (menos de um quadro a 30 fps) nao ha o que corrigir; a deriva
    // de verdade cresce ate passar dali e entao e absorvida como antes.
    if (errUs.abs() < _zonaMortaUs) {
      debugBaseShiftUs = 0;
      return;
    }
    final step = (errUs * 0.1).round().clamp(-8000, 8000);
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
    if (time.value >= end) {
      seekRevision++;
      time.value = Duration.zero;
    }
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

  /// UM QUADRO PARA A FRENTE OU PARA TRAS.
  ///
  /// Nao existia passo de quadro em lugar nenhum do app: o cabecote so
  /// andava por arrasto e por toque, e as duas coisas param onde o dedo
  /// para. Sem isto nao ha como cravar uma marca no quadro exato — que
  /// e a diferenca entre uma animacao que bate com o corte e uma que
  /// chega um quadro atrasada.
  ///
  /// Anda pelo INDICE do quadro, e nao somando o passo em
  /// microssegundos: a 30 fps o passo nao e inteiro (33333,33 us), e
  /// somar arredondado erra 50 us a cada 150 quadros — o suficiente
  /// para dez passos para a frente e dez para tras nao voltarem ao
  /// mesmo lugar.
  void stepFrame(int passos) {
    if (passos == 0) return;
    pause();
    final f = compositionFps < 1 ? 30 : compositionFps;
    final atual = (time.value.inMicroseconds * f) ~/ 1000000;
    final alvo = atual + passos;
    seek(_instanteDoQuadro(alvo < 0 ? 0 : alvo, f));
  }

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
    return _instanteDoQuadro(quadro, f);
  }

  /// O INSTANTE DO QUADRO [quadro], SEMPRE PARA CIMA.
  ///
  /// A conta de volta (`_naGrade`) usa o CHAO, entao o instante de um
  /// quadro tem de ser o primeiro microssegundo que ja pertence a ele —
  /// senao a ida e a volta nao fecham. O quadro 25 a 24 fps cai em
  /// 1.041.666,67 us: truncar da 1.041.666, que pelo chao ainda e o
  /// quadro 24, e arredondar da o mesmo problema sempre que a fracao
  /// fica abaixo da metade (quadro 55 a 30 fps). Nos dois casos o passo
  /// de quadro ficava preso — pedir "um para a frente" devolvia o mesmo
  /// instante, ou dez passos de ida e dez de volta paravam noutro lugar.
  static Duration _instanteDoQuadro(int quadro, int f) =>
      Duration(microseconds: (quadro * 1000000 / f).ceil());

  void seek(Duration t) {
    seekRevision++;
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
