import 'dart:math' as math;

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/playback_controller.dart';
import 'janela_da_timeline.dart';
import 'reordenar.dart';

// ===========================================================================
// O ESTADO DE VISTA DA TIMELINE
// ===========================================================================
//
// Tudo o que muda SEM reconstruir widget mora aqui, em notificadores: o
// instante sob o cabecote, o zoom, a guia do ima, o destino do reordenar.
// Os pintores escutam estes notificadores pelo `repaint:`; as linhas so se
// reconstroem quando a CAMADA delas muda. E isso que deixa o tique do
// relogio barato: ele move o conteudo (repintura), nunca a arvore.

/// O ESTADO DE VISTA, um por timeline montada.
class EstadoDaTimeline {
  EstadoDaTimeline({required this.playback, required TickerProvider vsync}) {
    vistaUs.value = playback.time.value.inMicroseconds.toDouble();
    playback.time.addListener(_seguirRelogio);
    autoRolagem = AutoRolagem(vsync: vsync, estado: this);
  }

  final PlaybackController playback;

  /// A cada passo de scrub (o gerente de video toca lasquinhas de som e
  /// segura os decodificadores no modo de busca rapida). Trocado pela
  /// timeline a cada montagem.
  VoidCallback? aoScrub;

  /// Toque num clipe, depois de escolher a camada (a casca pode reagir).
  ValueChanged<String>? aoTocarNaCamada;

  /// O reordenar em curso (a timeline o cria; as linhas o acionam).
  ControleDeReordenar? reordenar;

  /// O zoom tem teto e piso: abaixo de 2 dp/s uma hora cabe em 7200 dp e
  /// o risco some; acima de 800 um quadro a 30 fps passa de 26 dp.
  static const double ppsMinimo = 2;
  static const double ppsMaximo = 800;

  /// O INSTANTE SOB O CABECOTE, em µs.
  ///
  /// DOUBLE, e nao o `Duration` do relogio: o relogio anda na grade de
  /// quadros, e a pinca e o arrasto andam em fracao de quadro. Se a vista
  /// fosse o relogio, o instante sob os dedos pularia um quadro inteiro a
  /// cada passo da pinca (a 600 dp/s, 20 dp) — o "zoom que pula".
  final ValueNotifier<double> vistaUs = ValueNotifier<double>(0);

  /// O zoom, em dp por segundo (base: [AureaDims.dpPorSegundo]).
  final ValueNotifier<double> pps = ValueNotifier<double>(
    AureaDims.dpPorSegundo,
  );

  /// O que os pintores escutam: andar no tempo ou mudar o zoom.
  late final Listenable vista = Listenable.merge([vistaUs, pps]);

  /// A GUIA DO IMA: o instante do projeto (µs) em que o arrasto grudou.
  /// Nulo = sem guia.
  final ValueNotifier<int?> guiaUs = ValueNotifier<int?>(null);

  /// O TRACO DE DESTINO do reordenar: `y` no conteudo da lista de linhas
  /// (0 = topo da primeira linha). Nulo = sem reordenar em curso.
  final ValueNotifier<double?> destinoDoReordenar = ValueNotifier<double?>(
    null,
  );

  /// A camada que esta sendo reordenada (a linha dela fica "levantada").
  final ValueNotifier<String?> emReordenacao = ValueNotifier<String?>(null);

  late final AutoRolagem autoRolagem;

  /// A largura da timeline, medida na ultima montagem.
  double largura = 0;

  /// O CABECOTE FICA NO CENTRO da timeline inteira (e nao da area de
  /// tempo a direita dos cabecalhos): e onde o olho procura.
  double get centro => largura / 2;

  // ------------------------------------------------------------ geometria

  double xDoTempo(num us) => centro + (us - vistaUs.value) / 1e6 * pps.value;

  double tempoDoX(double x) => vistaUs.value + (x - centro) / pps.value * 1e6;

  double usPorPx(double px) => px / pps.value * 1e6;

  /// O PEDACO VISIVEL em pixels de CONTEUDO (zero = instante zero do
  /// projeto) — a moeda que a tira de miniaturas e a onda entendem.
  JanelaDaTimeline get janela => JanelaDaTimeline.de(
    offset: vistaUs.value / 1e6 * pps.value,
    viewport: largura,
    recuo: centro,
  );

  // --------------------------------------------------- quem manda na vista

  int _seguram = 0;

  /// UM GESTO SEGURA A VISTA: enquanto ele dura, o relogio nao a puxa de
  /// volta para a grade de quadros. O seek cai num quadro, e o quadro nao
  /// coincide com o pixel sob o dedo — sem a trava, cada passo do scrub
  /// voltaria ate meio quadro e o conteudo tremeria sob o dedo.
  void segurarVista() => _seguram++;

  /// Fim do gesto: a vista se acomoda no quadro em que o relogio caiu.
  void soltarVista() {
    if (_seguram > 0) _seguram--;
    if (_seguram == 0) _seguirRelogio();
  }

  bool get vistaPresa => _seguram > 0;

  void _seguirRelogio() {
    if (_seguram > 0) return;
    final us = playback.time.value.inMicroseconds.toDouble();
    if (vistaUs.value != us) vistaUs.value = us;
  }

  /// LEVA A VISTA (e o relogio) A [us]: o scrub. O palco atualiza na hora
  /// (o seek com o relogio parado ja marca a interacao, e o palco desenha
  /// em rascunho enquanto o dedo anda).
  void irPara(double us) {
    final fim = playback.durationOf().inMicroseconds.toDouble();
    final v = us.clamp(0.0, math.max(0.0, fim)).toDouble();
    if (vistaUs.value != v) vistaUs.value = v;
    // O GERENTE DE VIDEO PRECISA SABER QUE E SCRUB ANTES do seek publicar
    // o tempo: o ouvinte do relogio e sincrono, e na ordem inversa cada
    // passo parecia um seek isolado que esvaziava o decodificador.
    aoScrub?.call();
    playback.seek(Duration(microseconds: v.round()));
  }

  /// Zoom para [novo] dp/s, respeitando teto e piso.
  void zoom(double novo) {
    final v = novo.clamp(ppsMinimo, ppsMaximo);
    if (pps.value != v) pps.value = v;
  }

  void dispose() {
    playback.time.removeListener(_seguirRelogio);
    autoRolagem.dispose();
    vistaUs.dispose();
    pps.dispose();
    guiaUs.dispose();
    destinoDoReordenar.dispose();
    emReordenacao.dispose();
  }
}

/// O ESTADO DE VISTA para as linhas, sem reconstruir ninguem: a instancia
/// e a mesma pela vida da timeline.
class EscopoDaTimeline extends InheritedWidget {
  const EscopoDaTimeline({
    super.key,
    required this.estado,
    required super.child,
  });

  final EstadoDaTimeline estado;

  static EstadoDaTimeline de(BuildContext context) {
    final e = context
        .dependOnInheritedWidgetOfExactType<EscopoDaTimeline>()
        ?.estado;
    assert(e != null, 'Linha da timeline montada fora da timeline.');
    return e!;
  }

  @override
  bool updateShouldNotify(EscopoDaTimeline old) => old.estado != estado;
}

/// A AUTO-ROLAGEM: arrastar perto da borda anda a timeline sozinha
/// ([AureaDims.velocidadeDeAutoRolagem] dp/s a [AureaDims.bordaDeAutoRolagem]
/// da borda). Horizontal para clipe, alca e losango (o tempo corre sob o
/// cabecote); vertical para o reordenar.
class AutoRolagem {
  AutoRolagem({required TickerProvider vsync, required this.estado}) {
    _ticker = vsync.createTicker(_tique);
  }

  final EstadoDaTimeline estado;
  late final Ticker _ticker;
  Duration _ultimo = Duration.zero;

  int _dirX = 0;
  int _dirY = 0;
  VoidCallback? _aoRolarX;
  VoidCallback? _aoRolarY;
  ScrollController? _lista;

  bool get ativa => _ticker.isActive;

  /// Quanto o dedo precisa andar em direcao a borda para valer a rolagem.
  static const double _intencao = 4;

  /// O dedo esta em [x] (coordenada da timeline). Perto da borda da area de
  /// tempo, rola; [aoRolar] reaplica o arrasto no ponto em que o dedo ficou.
  ///
  /// So rola para o lado a que o dedo FOI desde [desde] (onde o gesto
  /// comecou): pegar um clipe ja perto da borda nao pode sair rolando
  /// antes de a pessoa mexer.
  void horizontal(double x, VoidCallback aoRolar, {required double desde}) {
    final esquerda = AureaDims.cabecalhoDaCamada + AureaDims.bordaDeAutoRolagem;
    final direita = estado.largura - AureaDims.bordaDeAutoRolagem;
    _dirX = x < esquerda && x < desde - _intencao
        ? -1
        : (x > direita && x > desde + _intencao ? 1 : 0);
    _aoRolarX = aoRolar;
    _ligarOuDesligar();
  }

  /// O dedo esta em [y] (coordenada da janela da lista, de altura
  /// [altura]). Perto do topo ou da base, rola a [lista].
  void vertical(
    double y,
    double altura,
    ScrollController lista,
    VoidCallback aoRolar, {
    required double desde,
  }) {
    const borda = AureaDims.bordaDeAutoRolagem;
    _dirY = y < borda && y < desde - _intencao
        ? -1
        : (y > altura - borda && y > desde + _intencao ? 1 : 0);
    _lista = lista;
    _aoRolarY = aoRolar;
    _ligarOuDesligar();
  }

  void parar() {
    _dirX = 0;
    _dirY = 0;
    _aoRolarX = null;
    _aoRolarY = null;
    _lista = null;
    if (_ticker.isActive) _ticker.stop();
  }

  void _ligarOuDesligar() {
    final precisa = _dirX != 0 || _dirY != 0;
    if (precisa && !_ticker.isActive) {
      _ultimo = Duration.zero;
      _ticker.start();
    } else if (!precisa && _ticker.isActive) {
      _ticker.stop();
    }
  }

  void _tique(Duration agora) {
    final dt = (agora - _ultimo).inMicroseconds / 1e6;
    _ultimo = agora;
    if (dt <= 0) return;
    final passo = AureaDims.velocidadeDeAutoRolagem * dt;
    if (_dirX != 0) {
      final antes = estado.vistaUs.value;
      estado.irPara(antes + _dirX * estado.usPorPx(passo));
      if (estado.vistaUs.value != antes) _aoRolarX?.call();
    }
    final lista = _lista;
    if (_dirY != 0 && lista != null && lista.hasClients) {
      final p = lista.position;
      final alvo = (p.pixels + _dirY * passo).clamp(
        p.minScrollExtent,
        p.maxScrollExtent,
      );
      if (alvo != p.pixels) {
        p.jumpTo(alvo);
        _aoRolarY?.call();
      }
    }
  }

  void dispose() {
    _ticker.dispose();
  }
}

/// SONDAS DE TESTE: quantas vezes as pecas se reconstruiram. E a medida de
/// "o tique do relogio nao reconstroi as linhas" — sem ela, a promessa
/// seria uma afirmacao sem numero.
abstract final class SondaDaTimeline {
  static int buildsDeLinha = 0;

  static int buildsDaTimeline = 0;

  static void zerar() {
    buildsDeLinha = 0;
    buildsDaTimeline = 0;
  }
}
