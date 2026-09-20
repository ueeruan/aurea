// O ESTUDIO DO TEMPO — Time Remap, Speed Ramp e congelar num lugar so,
// no modelo do Graph Editor do After Effects mas desenhado para o dedo.
//
// Duas vistas da MESMA curva fonte-tempo: o grafo de VALOR (que instante
// da fonte aparece) e o grafo de VELOCIDADE (a derivada; 100% = tempo
// real, 0% = congelado, negativo = reverso). As duas saem do nucleo C++
// (aurea_timecore), a mesma conta que escolhe o quadro — o desenho nunca
// discorda da previa.
//
// As licoes do editor antigo (que os testadores reprovaram) valem aqui:
// a folha nao se arrasta (o grafico e dono do gesto vertical), os eixos
// CONGELAM enquanto um dedo esta no vidro (o ponto nao foge), o inspetor
// tem altura fixa, e um arrasto inteiro e UM passo de desfazer.
//
// Abrir nao cria curva: a primeira edicao cria (e, num clipe com o
// Reverso ligado, grava antes a curva que a previa ja toca).
import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'dart:math' as math;

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/utils/time_format.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/cut.dart';
import '../../domain/cut_ops.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import '../../domain/remapear_tempo.dart';
import 'am_colors.dart';
import '../../../../core/ui/am_tick_ruler.dart' show AmArrastoDeValor;
import '../../../../core/ui/tocavel.dart';

/// O NOME DE CADA MODO DE INTERPOLACAO NA TELA, no vocabulario que o dono
/// pediu (Nenhuma / Mistura / Fluxo optico / Fluxo optico (IA)). O enum
/// nao muda — ele e gravado por `name` no projeto —, so o rotulo. Mora
/// aqui porque as tres superficies do tempo (esta, a folha Tempo e o
/// cartao do painel de efeitos) precisam dizer a mesma palavra.
String rotuloDaInterpolacao(InterpolacaoDeQuadros modo) => switch (modo) {
  InterpolacaoDeQuadros.nenhuma => 'Nenhuma',
  InterpolacaoDeQuadros.mesclar => 'Mistura',
  InterpolacaoDeQuadros.movimento => 'Fluxo óptico',
  InterpolacaoDeQuadros.ia => 'Fluxo óptico (IA)',
};

/// A PREVIA JA MISTURA OS QUADROS VIZINHOS? Hoje nao: o palco mostra o
/// quadro mais proximo, e mistura/fluxo optico so existem na exportacao.
/// Quando a previa reduzida (dois quadros do cache + opacidade pela
/// fracao) entrar no palco, esta constante vira `true` e o selo passa a
/// dizer "prévia: mistura" — sem tocar em nenhuma das tres superficies.
const bool kPreviaMisturaQuadros = false;

/// O SELO DA PREVIA, dito na tela para ninguem procurar no palco um
/// resultado que so sai no arquivo. Nulo = nada a avisar.
String? seloDaInterpolacao(InterpolacaoDeQuadros modo) {
  const previa = kPreviaMisturaQuadros
      ? 'prévia: mistura'
      : 'prévia: quadro mais próximo';
  return switch (modo) {
    InterpolacaoDeQuadros.nenhuma => null,
    InterpolacaoDeQuadros.mesclar =>
      kPreviaMisturaQuadros ? null : '$previa · exportação: mistura',
    InterpolacaoDeQuadros.movimento ||
    InterpolacaoDeQuadros.ia => '$previa · exportação: fluxo óptico',
  };
}

/// A folha do estudio: modal sem arrasto proprio (o grafico fica com o
/// gesto), barreira transparente e ~66% da tela — a previa continua a
/// vista mostrando o quadro que o dedo esta remapeando.
Future<void> showEstudioDoTempo(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController? playback,
) async {
  final layer = ref.read(editorControllerProvider).layerById(layerId);
  if (layer is! VideoLayer || !context.mounted) return;
  final altura = MediaQuery.sizeOf(context).height * 0.66;
  await showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    enableDrag: false,
    backgroundColor: AmColors.panel,
    barrierColor: Colors.transparent,
    clipBehavior: Clip.antiAlias,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (_) => SafeArea(
      top: false,
      child: SizedBox(
        height: altura,
        child: EstudioDoTempo(layerId: layerId, playback: playback),
      ),
    ),
  );
}

enum _Aba { valor, velocidade }

enum _Gesto { nada, ponto, alcaEntrada, alcaSaida, velocidade, pan }

class EstudioDoTempo extends ConsumerStatefulWidget {
  const EstudioDoTempo({super.key, required this.layerId, this.playback});

  final String layerId;
  final PlaybackController? playback;

  @override
  ConsumerState<EstudioDoTempo> createState() => _EstudioDoTempoState();
}

class _EstudioDoTempoState extends ConsumerState<EstudioDoTempo> {
  // O AE abre o Time Remap como grafico de valor; o grafico de velocidade
  // fica ao lado para editar rampas e influencia sem sair do editor.
  _Aba _aba = _Aba.valor;
  int? _selecionado;
  bool _grudarNasBatidas = true;

  // A JANELA DO GRAFICO (zoom e pan), em segundos locais e em valor.
  // Congelada enquanto um dedo esta no vidro; `_enquadrar` recalcula.
  double _t0 = 0, _t1 = 1;
  double _v0 = 0, _v1 = 1; // valor (segundos de fonte)
  double _s0 = -1, _s1 = 3; // velocidade (1 = 100%)
  bool _enquadrado = false;

  _Gesto _gesto = _Gesto.nada;
  bool _ladoDaEntrada = false; // qual dos dois pontos do grafo de velocidade
  double _t0DoGesto = 0, _t1DoGesto = 0; // janela no comeco do pinch
  double _v0DoGesto = 0, _v1DoGesto = 0;
  double _s0DoGesto = 0, _s1DoGesto = 0;
  Offset _focoDoGesto = Offset.zero;
  Size _tamanho = Size.zero;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  VideoLayer? get _video {
    final l = ref.read(editorControllerProvider).layerById(widget.layerId);
    return l is VideoLayer ? l : null;
  }

  /// A curva MOSTRADA: a guardada, ou a identidade; num clipe com o
  /// Reverso ligado, o espelho — o que a previa toca de verdade.
  AnimatedDouble _curvaDe(VideoLayer l) {
    final span = videoSourceSpan(l).inMicroseconds / 1e6;
    final t = timeRemapTrackOf(l) ?? curvaIdentidade(l.duration, span);
    return l.reverse ? curvaEspelhada(t, span) : t;
  }

  double _spanDe(VideoLayer l) => videoSourceSpan(l).inMicroseconds / 1e6;

  /// ESCREVE a curva mostrada no projeto. A primeira escrita de um clipe
  /// reverso grava o espelho e desliga o interruptor (nenhum quadro
  /// muda); tudo dentro do gesto ou de um passo unico de desfazer.
  void _escrever(AnimatedDouble nova, {required bool dentroDeGesto}) {
    final l = _video;
    if (l == null) return;
    void corpo() {
      if (l.reverse) _c.assarReversoNaCurva(widget.layerId);
      _c.definirTrilhaDeTempo(widget.layerId, nova);
    }

    if (dentroDeGesto) {
      corpo();
    } else {
      _c.runAsOneUndo(corpo);
    }
    setState(() {});
  }

  // ------------------------------------------------------- geometria

  static const _margemEsq = 44.0, _margemBaixo = 18.0, _margemTopo = 8.0;

  Rect get _quadro => Rect.fromLTRB(
    _margemEsq,
    _margemTopo,
    _tamanho.width - 8,
    _tamanho.height - _margemBaixo,
  );

  double _xDe(double seg) =>
      _quadro.left + (seg - _t0) / math.max(1e-9, _t1 - _t0) * _quadro.width;

  double _segDe(double x) =>
      _t0 + (x - _quadro.left) / math.max(1e-9, _quadro.width) * (_t1 - _t0);

  double _yDoValor(double v) =>
      _quadro.bottom - (v - _v0) / math.max(1e-9, _v1 - _v0) * _quadro.height;

  double _valorDe(double y) =>
      _v0 + (_quadro.bottom - y) / math.max(1e-9, _quadro.height) * (_v1 - _v0);

  double _yDaVelocidade(double s) =>
      _quadro.bottom - (s - _s0) / math.max(1e-9, _s1 - _s0) * _quadro.height;

  double _velocidadeDe(double y) =>
      _s0 + (_quadro.bottom - y) / math.max(1e-9, _quadro.height) * (_s1 - _s0);

  /// Enquadra a janela na curva inteira (o botao Ajustar e o toque duplo).
  void _enquadrar(VideoLayer l) {
    final curva = _curvaDe(l);
    _t0 = 0;
    _t1 = math.max(0.1, l.duration.inMicroseconds / 1e6);
    final (lo, hi) = faixaDaCurva(curva);
    final folga = math.max(0.25, (hi - lo) * 0.12);
    _v0 = lo - folga;
    _v1 = hi + folga;
    var sLo = 0.0, sHi = 1.0;
    for (var i = 0; i <= 48; i++) {
      final s = velocidadeDaCurva(
        curva,
        Duration(microseconds: (_t1 * 1e6 * i / 48).round()),
      );
      if (!s.isFinite) continue;
      sLo = math.min(sLo, s);
      sHi = math.max(sHi, s);
    }
    _s0 = sLo - math.max(0.4, (sHi - sLo) * 0.15);
    _s1 = sHi + math.max(0.4, (sHi - sLo) * 0.15);
    _enquadrado = true;
  }

  // ------------------------------------------------------------ snap

  List<Duration> _guias(VideoLayer l) {
    final projeto = ref.read(editorControllerProvider);
    final locais = <Duration>[];
    void poe(Duration global) {
      final t = global - l.startTime;
      if (t >= Duration.zero && t <= l.duration) locais.add(t);
    }

    final pb = widget.playback;
    if (pb != null) poe(pb.time.value);
    if (_grudarNasBatidas) {
      for (final b in projeto.beats) {
        poe(b);
      }
      for (final m in projeto.markers) {
        poe(m.time);
      }
    }
    return locais;
  }

  Duration _grudar(VideoLayer l, double seg) {
    final projeto = ref.read(editorControllerProvider);
    final bruto = Duration(
      microseconds: (seg.clamp(0, l.duration.inMicroseconds / 1e6) * 1e6)
          .round(),
    );
    final r = ajustarTempoComGrade(
      bruto,
      fps: projeto.fps,
      guias: _guias(l),
      tolerancia: Duration(
        microseconds: ((_t1 - _t0) / math.max(1, _quadro.width) * 8 * 1e6)
            .round(),
      ),
    );
    if (r.naGuia) HapticFeedback.selectionClick();
    return r.tempo;
  }

  // --------------------------------------------------------- gestos

  int? _pontoEm(Offset p, List<PontoDeTempo> pontos, AnimatedDouble curva) {
    int? melhor;
    var menor = 24.0;
    for (final ponto in pontos) {
      final seg = ponto.tempo.inMicroseconds / 1e6;
      final y = _aba == _Aba.valor
          ? _yDoValor(ponto.valor)
          : _yDaVelocidade((ponto.saida ?? ponto.entrada)?.velocidade ?? 0);
      final d = (p - Offset(_xDe(seg), y)).distance;
      if (d < menor) {
        menor = d;
        melhor = ponto.indice;
      }
    }
    return melhor;
  }

  /// As posicoes ABSOLUTAS das alcas do ponto selecionado (grafo de
  /// valor): a de saida no trecho da frente, a de entrada no de tras.
  (Offset?, Offset?) _alcasDe(List<PontoDeTempo> pontos, int indice) {
    final p = pontos[indice];
    final t = p.tempo.inMicroseconds / 1e6;
    Offset? saida, entrada;
    final s = p.saida;
    if (s != null && indice < pontos.length - 1) {
      final dt = (pontos[indice + 1].tempo - p.tempo).inMicroseconds / 1e6;
      final dx = s.influencia * dt;
      saida = Offset(_xDe(t + dx), _yDoValor(p.valor + s.velocidade * dx));
    }
    final e = p.entrada;
    if (e != null && indice > 0) {
      final dt = (p.tempo - pontos[indice - 1].tempo).inMicroseconds / 1e6;
      final dx = e.influencia * dt;
      entrada = Offset(_xDe(t - dx), _yDoValor(p.valor - e.velocidade * dx));
    }
    return (saida, entrada);
  }

  void _comecarGesto(ScaleStartDetails d) {
    final l = _video;
    if (l == null) return;
    final curva = _curvaDe(l);
    final pontos = pontosDaTrilha(curva);
    _gesto = _Gesto.pan;
    _t0DoGesto = _t0;
    _t1DoGesto = _t1;
    _v0DoGesto = _v0;
    _v1DoGesto = _v1;
    _s0DoGesto = _s0;
    _s1DoGesto = _s1;
    _focoDoGesto = d.localFocalPoint;
    if (d.pointerCount > 1) return;
    final p = d.localFocalPoint;

    // Alca primeiro: ela fica perto do ponto e perderia sempre.
    final sel = _selecionado;
    if (_aba == _Aba.valor && sel != null && sel < pontos.length) {
      final (saida, entrada) = _alcasDe(pontos, sel);
      if (saida != null && (p - saida).distance < 28) {
        _gesto = _Gesto.alcaSaida;
      } else if (entrada != null && (p - entrada).distance < 28) {
        _gesto = _Gesto.alcaEntrada;
      }
    }
    if (_gesto == _Gesto.pan) {
      final tocado = _pontoEm(p, pontos, curva);
      if (tocado != null) {
        _selecionado = tocado;
        if (_aba == _Aba.valor) {
          _gesto = _Gesto.ponto;
        } else {
          _gesto = _Gesto.velocidade;
          final ponto = pontos[tocado];
          final ye = ponto.entrada == null
              ? double.infinity
              : (_yDaVelocidade(ponto.entrada!.velocidade) - p.dy).abs();
          final ys = ponto.saida == null
              ? double.infinity
              : (_yDaVelocidade(ponto.saida!.velocidade) - p.dy).abs();
          _ladoDaEntrada = ye < ys || ponto.saida == null;
        }
        HapticFeedback.mediumImpact();
        widget.playback?.pause();
      }
    }
    if (_gesto != _Gesto.pan) _c.beginGesture();
    setState(() {});
  }

  void _moverGesto(ScaleUpdateDetails d) {
    final l = _video;
    if (l == null) return;
    if (d.pointerCount > 1 || _gesto == _Gesto.pan) {
      // PINCH E PAN: zoom por eixo, pan pelo foco. Janela congelada no
      // comeco do gesto — nada se recalcula debaixo do dedo.
      final zx = d.horizontalScale.clamp(0.2, 12.0);
      final zy = d.verticalScale.clamp(0.2, 12.0);
      final duracaoT = (_t1DoGesto - _t0DoGesto) / zx;
      final focoT =
          _t0DoGesto +
          (_focoDoGesto.dx - _quadro.left) /
              math.max(1e-9, _quadro.width) *
              (_t1DoGesto - _t0DoGesto);
      final dxSeg =
          (d.localFocalPoint.dx - _focoDoGesto.dx) /
          math.max(1e-9, _quadro.width) *
          duracaoT;
      final fracFoco =
          (_focoDoGesto.dx - _quadro.left) / math.max(1e-9, _quadro.width);
      _t0 = focoT - duracaoT * fracFoco - dxSeg;
      _t1 = _t0 + duracaoT;
      final dur = math.max(0.1, l.duration.inMicroseconds / 1e6);
      final total = (_t1 - _t0).clamp(dur / 40, dur * 1.5);
      _t0 = _t0.clamp(-dur * 0.25, dur * 1.25 - total);
      _t1 = _t0 + total;

      final (lo0, hi0) = _aba == _Aba.valor
          ? (_v0DoGesto, _v1DoGesto)
          : (_s0DoGesto, _s1DoGesto);
      final faixa = (hi0 - lo0) / zy;
      final fracY =
          (_quadro.bottom - _focoDoGesto.dy) / math.max(1e-9, _quadro.height);
      final focoV = lo0 + fracY * (hi0 - lo0);
      final dyV =
          (d.localFocalPoint.dy - _focoDoGesto.dy) /
          math.max(1e-9, _quadro.height) *
          faixa;
      final novoLo = focoV - faixa * fracY + dyV;
      if (_aba == _Aba.valor) {
        _v0 = novoLo;
        _v1 = novoLo + faixa;
      } else {
        _s0 = novoLo;
        _s1 = novoLo + faixa;
      }
      setState(() {});
      return;
    }

    final curva = _curvaDe(l);
    final pontos = pontosDaTrilha(curva);
    final sel = _selecionado;
    if (sel == null || sel >= pontos.length) return;
    final p = pontos[sel];
    final span = _spanDe(l);
    final pos = d.localFocalPoint;

    switch (_gesto) {
      case _Gesto.ponto:
        // Mover o keyframe: tempo com grade e presos entre os vizinhos;
        // valor preso ao trecho da fonte.
        var t = _grudar(l, _segDe(pos.dx));
        const folga = Duration(milliseconds: 33);
        if (sel > 0) {
          final min = pontos[sel - 1].tempo + folga;
          if (t < min) t = min;
        }
        if (sel < pontos.length - 1) {
          final max = pontos[sel + 1].tempo - folga;
          if (t > max) t = max;
        }
        final v = _valorDe(pos.dy).clamp(0.0, span);
        var nova = curva.comKeyframeMovido(p.tempo, t);
        nova = nova.withKeyframe(t, v, nova.easeAt(t));
        _escrever(nova, dentroDeGesto: true);
      case _Gesto.alcaSaida:
      case _Gesto.alcaEntrada:
        final entrada = _gesto == _Gesto.alcaEntrada;
        final vizinho = entrada ? pontos[sel - 1] : pontos[sel + 1];
        final dtTrecho = ((vizinho.tempo - p.tempo).inMicroseconds / 1e6).abs();
        if (dtTrecho <= 0) return;
        final dxSeg = entrada
            ? p.tempo.inMicroseconds / 1e6 - _segDe(pos.dx)
            : _segDe(pos.dx) - p.tempo.inMicroseconds / 1e6;
        final influencia = (dxSeg / dtTrecho).clamp(0.01, 1.0);
        final dv = entrada
            ? p.valor - _valorDe(pos.dy)
            : _valorDe(pos.dy) - p.valor;
        final velocidade = dxSeg.abs() < 1e-6 ? 0.0 : dv / dxSeg;
        _escrever(
          trilhaComSuavidade(
            curva,
            sel,
            entrada: entrada
                ? SuavidadeTemporal(
                    velocidade: velocidade,
                    influencia: influencia,
                  )
                : null,
            saida: entrada
                ? null
                : SuavidadeTemporal(
                    velocidade: velocidade,
                    influencia: influencia,
                  ),
          ),
          dentroDeGesto: true,
        );
      case _Gesto.velocidade:
        // Grafo de velocidade: vertical muda a velocidade do lado,
        // horizontal muda a influencia dele — como as alcas do AE.
        final lado = _ladoDaEntrada ? p.entrada : p.saida;
        if (lado == null) return;
        final velocidade = _velocidadeDe(pos.dy);
        final vizinho = _ladoDaEntrada
            ? (sel > 0 ? pontos[sel - 1] : null)
            : (sel < pontos.length - 1 ? pontos[sel + 1] : null);
        var influencia = lado.influencia;
        if (vizinho != null) {
          final dtTrecho = ((vizinho.tempo - p.tempo).inMicroseconds / 1e6)
              .abs();
          final dxSeg = (_segDe(pos.dx) - p.tempo.inMicroseconds / 1e6).abs();
          if (dtTrecho > 0 && dxSeg > 0.01 * dtTrecho) {
            influencia = (dxSeg / dtTrecho).clamp(0.01, 1.0);
          }
        }
        final s = SuavidadeTemporal(
          velocidade: velocidade,
          influencia: influencia,
        );
        _escrever(
          trilhaComSuavidade(
            curva,
            sel,
            entrada: _ladoDaEntrada ? s : null,
            saida: _ladoDaEntrada ? null : s,
          ),
          dentroDeGesto: true,
        );
      case _Gesto.pan:
      case _Gesto.nada:
        break;
    }
  }

  void _terminarGesto(ScaleEndDetails d) {
    if (_gesto != _Gesto.pan && _gesto != _Gesto.nada) _c.endGesture();
    _gesto = _Gesto.nada;
    setState(() {});
  }

  void _tocar(TapUpDetails d) {
    final l = _video;
    if (l == null) return;
    final curva = _curvaDe(l);
    final pontos = pontosDaTrilha(curva);
    final tocado = _pontoEm(d.localPosition, pontos, curva);
    if (tocado != null) {
      setState(() => _selecionado = tocado);
      return;
    }
    // TOQUE NA LINHA CRIA PONTO (só no grafo de valor): no instante
    // tocado, com o valor que a curva ja tem ali — o desenho nao pula.
    if (_aba == _Aba.valor && _quadro.contains(d.localPosition)) {
      final t = _grudar(l, _segDe(d.localPosition.dx));
      final naCurva =
          (_yDoValor(valorDaCurva(curva, t)) - d.localPosition.dy).abs() < 26;
      if (naCurva) {
        final v = valorDaCurva(curva, t);
        _escrever(
          curva.withKeyframe(t, v, curva.easeAt(t)),
          dentroDeGesto: false,
        );
        final novos = pontosDaTrilha(_curvaDe(_video!));
        for (final p in novos) {
          if ((p.tempo - t).abs() < kToleranciaDoKeyframe) {
            _selecionado = p.indice;
          }
        }
        HapticFeedback.selectionClick();
        return;
      }
    }
    setState(() => _selecionado = null);
  }

  void _menuDoPonto(int indice) {
    final l = _video;
    if (l == null) return;
    final curva = _curvaDe(l);
    if (indice >= curva.keyframes.length) return;
    final kf = curva.keyframes[indice];
    void aplica(AnimatedDouble nova) => _escrever(nova, dentroDeGesto: false);
    showCupertinoModalPopup<void>(
      context: context,
      builder: (menuContext) => CupertinoActionSheet(
        title: AppTextMoldado('Keyframe · {0}', [formatTime(kf.time)]),
        actions: [
          for (final (rotulo, faz) in <(String, VoidCallback)>[
            (
              'Suavizar (easy ease)',
              () => aplica(trilhaSuavizada(curva, indice)),
            ),
            ('Auto bezier', () => aplica(trilhaAutoBezier(curva, indice))),
            ('Continuo', () => aplica(trilhaContinua(curva, indice))),
            (
              'Linear',
              () => aplica(
                trilhaComTipo(curva, indice, TipoDoPontoDeTempo.linear),
              ),
            ),
            (
              'Manter (congela ate o proximo)',
              () => aplica(
                trilhaComTipo(curva, indice, TipoDoPontoDeTempo.manter),
              ),
            ),
          ])
            CupertinoActionSheetAction(
              onPressed: () {
                Navigator.of(menuContext).pop();
                faz();
              },
              child: AppText(rotulo),
            ),
          CupertinoActionSheetAction(
            isDestructiveAction: true,
            onPressed: () {
              Navigator.of(menuContext).pop();
              if (curva.keyframes.length <= 2) return;
              _selecionado = null;
              aplica(curva.withoutKeyframe(kf.time));
            },
            child: const AppText('Apagar keyframe'),
          ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(menuContext).pop(),
          child: const AppText('Cancelar'),
        ),
      ),
    );
  }

  // ---------------------------------------------------------- acoes

  Duration _localDoCabecote(VideoLayer l) {
    final t = widget.playback?.time.value ?? l.startTime;
    return Duration(
      microseconds: (t - l.startTime).inMicroseconds.clamp(
        0,
        l.duration.inMicroseconds,
      ),
    );
  }

  void _keyframeAqui() {
    final l = _video;
    if (l == null) return;
    final curva = _curvaDe(l);
    final t = _localDoCabecote(l);
    _escrever(
      curva.withKeyframe(t, valorDaCurva(curva, t), curva.easeAt(t)),
      dentroDeGesto: false,
    );
    HapticFeedback.selectionClick();
  }

  void _congelarAqui() {
    final l = _video;
    final pb = widget.playback;
    if (l == null) return;
    final global = pb?.time.value ?? l.startTime;
    final deu = _c.freezeFrame(
      widget.layerId,
      global,
      placement: FreezePlacement.insideClip,
    );
    if (deu) {
      HapticFeedback.mediumImpact();
      setState(() => _enquadrado = false);
    }
  }

  void _reverso() {
    final l = _video;
    if (l == null) return;
    final sel = _selecionado;
    final curva = _curvaDe(l);
    _c.runAsOneUndo(() {
      if (l.reverse) _c.assarReversoNaCurva(widget.layerId);
      final agora = _video;
      if (agora == null) return;
      if (sel != null && sel > 0 && sel < curva.keyframes.length) {
        // Com um ponto do meio selecionado: reverso A PARTIR dele.
        _c.definirTrilhaDeTempo(
          widget.layerId,
          reversoAPartirDe(
            timeRemapTrackOf(agora) ?? curva,
            curva.keyframes[sel].time,
            _spanDe(agora),
          ),
        );
      } else {
        _c.assarReversoNaCurva(widget.layerId);
      }
    });
    setState(() => _enquadrado = false);
  }

  /// Tira a curva e devolve a velocidade constante equivalente (a media
  /// do clipe) — a duracao na timeline nao muda.
  void _removerCurva() {
    _c.ligarCurvaDeTempo(widget.layerId, false);
    HapticFeedback.selectionClick();
    setState(() {
      _selecionado = null;
      _enquadrado = false;
    });
  }

  // ---------------------------------------------------------- build

  @override
  Widget build(BuildContext context) {
    ref.watch(editorControllerProvider);
    final l = _video;
    if (l == null) return const SizedBox.shrink();
    if (!_enquadrado) _enquadrar(l);
    final curva = _curvaDe(l);
    final pontos = pontosDaTrilha(curva);
    if (_selecionado != null && _selecionado! >= pontos.length) {
      _selecionado = null;
    }
    final temCurva = hasTimeRemap(l) || l.reverse;

    return Column(
      children: [
        const SizedBox(height: 10),
        _cabecalho(l),
        const SizedBox(height: 6),
        Expanded(child: _grafico(l, curva, pontos, temCurva)),
        _miniTimeline(l, pontos),
        _inspetor(l, curva, pontos),
        _acoes(l),
        const SizedBox(height: 8),
      ],
    );
  }

  Widget _cabecalho(VideoLayer l) {
    Widget aba(String rotulo, _Aba alvo, String chave) {
      final acesa = _aba == alvo;
      return Tocavel(
        key: ValueKey(chave),
        onTap: () => setState(() => _aba = alvo),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 7),
          decoration: BoxDecoration(
            color: acesa
                ? AmColors.accent.withValues(alpha: .18)
                : AmColors.chip,
            borderRadius: BorderRadius.circular(15),
          ),
          child: AppText(
            rotulo,
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w700,
              color: acesa ? AmColors.accent : AmColors.text,
            ),
          ),
        ),
      );
    }

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 14),
      child: Row(
        children: [
          const Flexible(
            child: AppText(
              'Editor de curva · Time Remap',
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w700,
                color: AmColors.text,
              ),
            ),
          ),
          const Spacer(),
          aba('Valor', _Aba.valor, 'estudio-tempo-aba-valor'),
          const SizedBox(width: 6),
          aba('Velocidade', _Aba.velocidade, 'estudio-tempo-aba-velocidade'),
          const SizedBox(width: 6),
          Tocavel(
            key: const ValueKey('estudio-tempo-enquadrar'),
            onTap: () => setState(() => _enquadrado = false),
            child: const Padding(
              padding: EdgeInsets.all(13),
              child: Icon(
                CupertinoIcons.arrow_up_left_arrow_down_right,
                size: 18,
                color: AmColors.text,
              ),
            ),
          ),
          Tocavel(
            onTap: () => Navigator.of(context).maybePop(),
            child: const Padding(
              padding: EdgeInsets.all(13),
              child: Icon(CupertinoIcons.xmark, size: 18, color: AmColors.text),
            ),
          ),
        ],
      ),
    );
  }

  Widget _grafico(
    VideoLayer l,
    AnimatedDouble curva,
    List<PontoDeTempo> pontos,
    bool temCurva,
  ) {
    final projeto = ref.read(editorControllerProvider);
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 6),
      child: LayoutBuilder(
        builder: (context, caixa) {
          _tamanho = Size(caixa.maxWidth, caixa.maxHeight);
          final alcas = _aba == _Aba.valor && _selecionado != null
              ? _alcasDe(pontos, _selecionado!)
              : (null, null);
          return GestureDetector(
            key: const ValueKey('estudio-tempo-grafico'),
            behavior: HitTestBehavior.opaque,
            onTapUp: _tocar,
            onDoubleTap: () => setState(() => _enquadrado = false),
            onLongPressStart: (d) {
              final tocado = _pontoEm(d.localPosition, pontos, curva);
              if (tocado != null) {
                setState(() => _selecionado = tocado);
                HapticFeedback.mediumImpact();
                _menuDoPonto(tocado);
              }
            },
            onScaleStart: _comecarGesto,
            onScaleUpdate: _moverGesto,
            onScaleEnd: _terminarGesto,
            child: ValueListenableBuilder<Duration>(
              valueListenable:
                  widget.playback?.time ?? ValueNotifier(Duration.zero),
              builder: (context, agora, _) => CustomPaint(
                size: _tamanho,
                painter: _PintorDoGrafico(
                  aba: _aba,
                  curva: curva,
                  pontos: pontos,
                  selecionado: _selecionado,
                  alcaSaida: alcas.$1,
                  alcaEntrada: alcas.$2,
                  quadro: _quadro,
                  t0: _t0,
                  t1: _t1,
                  v0: _aba == _Aba.valor ? _v0 : _s0,
                  v1: _aba == _Aba.valor ? _v1 : _s1,
                  cabecoteSeg: (agora - l.startTime).inMicroseconds / 1e6,
                  batidas: [
                    for (final b in projeto.beats)
                      (b - l.startTime).inMicroseconds / 1e6,
                  ],
                  esboco: !temCurva,
                ),
              ),
            ),
          );
        },
      ),
    );
  }

  Widget _miniTimeline(VideoLayer l, List<PontoDeTempo> pontos) {
    final pb = widget.playback;
    return SizedBox(
      height: 30,
      child: GestureDetector(
        key: const ValueKey('estudio-tempo-minitimeline'),
        behavior: HitTestBehavior.opaque,
        onHorizontalDragStart: (_) => pb?.pause(),
        onHorizontalDragUpdate: (d) {
          if (pb == null) return;
          final w = _tamanho.width - 20;
          final frac = ((d.localPosition.dx - 10) / math.max(1, w)).clamp(
            0.0,
            1.0,
          );
          pb.seek(
            l.startTime +
                Duration(
                  microseconds: (l.duration.inMicroseconds * frac).round(),
                ),
          );
        },
        onTapDown: (d) {
          if (pb == null) return;
          final w = _tamanho.width - 20;
          final frac = ((d.localPosition.dx - 10) / math.max(1, w)).clamp(
            0.0,
            1.0,
          );
          pb.pause();
          pb.seek(
            l.startTime +
                Duration(
                  microseconds: (l.duration.inMicroseconds * frac).round(),
                ),
          );
        },
        child: ValueListenableBuilder<Duration>(
          valueListenable: pb?.time ?? ValueNotifier(Duration.zero),
          builder: (context, agora, _) => CustomPaint(
            size: Size(_tamanho.width, 30),
            painter: _PintorDaMiniTimeline(
              duracao: l.duration,
              pontos: [for (final p in pontos) p.tempo],
              cabecote: agora - l.startTime,
            ),
          ),
        ),
      ),
    );
  }

  Widget _inspetor(
    VideoLayer l,
    AnimatedDouble curva,
    List<PontoDeTempo> pontos,
  ) {
    final sel = _selecionado;
    if (sel == null || sel >= pontos.length) {
      return SizedBox(
        height: 44,
        child: Center(
          child: AppText(
            'Toque num ponto para editar · toque na linha para criar · segure para o menu',
            style: TextStyle(
              fontSize: 10.5,
              color: AmColors.muted.withValues(alpha: .9),
            ),
          ),
        ),
      );
    }
    final p = pontos[sel];
    void muda({
      double? velEntrada,
      double? infEntrada,
      double? velSaida,
      double? infSaida,
    }) {
      final e = p.entrada;
      final s = p.saida;
      _escrever(
        trilhaComSuavidade(
          curva,
          sel,
          entrada: e == null
              ? null
              : SuavidadeTemporal(
                  velocidade: velEntrada ?? e.velocidade,
                  influencia: (infEntrada ?? e.influencia).clamp(0.01, 1.0),
                ),
          saida: s == null
              ? null
              : SuavidadeTemporal(
                  velocidade: velSaida ?? s.velocidade,
                  influencia: (infSaida ?? s.influencia).clamp(0.01, 1.0),
                ),
        ),
        // Arrastando o campo, o gesto ja esta aberto (um desfazer so);
        // digitando, cada valor e o proprio passo.
        dentroDeGesto: _arrastandoCampo,
      );
    }

    return SizedBox(
      height: 44,
      // FittedBox: em tela estreita (e na fonte quadrada dos testes) a
      // fileira ENCOLHE em vez de estourar pela direita.
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            _campo(
              'Entrada',
              p.entrada == null ? null : p.entrada!.velocidade * 100,
              '%',
              (v) => muda(velEntrada: v / 100),
            ),
            _campo(
              'Infl. entrada',
              p.entrada == null ? null : p.entrada!.influencia * 100,
              '%',
              (v) => muda(infEntrada: v / 100),
              min: 1,
              max: 100,
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 8),
              child: AppText(
                switch (p.tipo) {
                  TipoDoPontoDeTempo.manter => 'Manter',
                  TipoDoPontoDeTempo.linear => 'Linear',
                  TipoDoPontoDeTempo.bezier => 'Bezier',
                },
                style: TextStyle(
                  fontSize: 11,
                  fontWeight: FontWeight.w700,
                  color: AmColors.accent,
                ),
              ),
            ),
            _campo(
              'Saída',
              p.saida == null ? null : p.saida!.velocidade * 100,
              '%',
              (v) => muda(velSaida: v / 100),
            ),
            _campo(
              'Infl. saída',
              p.saida == null ? null : p.saida!.influencia * 100,
              '%',
              (v) => muda(infSaida: v / 100),
              min: 1,
              max: 100,
            ),
          ],
        ),
      ),
    );
  }

  // O ARRASTO DE UM CAMPO DO INSPETOR E UM GESTO: abre no primeiro valor
  // entregue (um toque que so abre o teclado nao gasta passo de desfazer)
  // e fecha quando o dedo sai do vidro.
  bool _arrastandoCampo = false;

  void _soltarCampo() {
    if (!_arrastandoCampo) return;
    _arrastandoCampo = false;
    _c.endGesture();
  }

  Widget _campo(
    String rotulo,
    double? valor,
    String sufixo,
    ValueChanged<double> aoMudar, {
    double min = double.negativeInfinity,
    double max = double.infinity,
  }) => Padding(
    padding: const EdgeInsets.symmetric(horizontal: 4),
    child: Opacity(
      opacity: valor == null ? .35 : 1,
      child: _CampoDoEstudio(
        rotulo: rotulo,
        valor: valor ?? 0,
        sufixo: sufixo,
        min: min,
        max: max,
        aoDigitar: valor == null ? null : aoMudar,
        aoArrastar: valor == null
            ? null
            : (v) {
                if (!_arrastandoCampo) {
                  _arrastandoCampo = true;
                  widget.playback?.pause();
                  _c.beginGesture();
                }
                aoMudar(v);
              },
        aoSoltar: _soltarCampo,
      ),
    ),
  );

  Widget _acoes(VideoLayer l) {
    final projeto = ref.read(editorControllerProvider);
    Widget botao(
      String rotulo,
      String chave,
      VoidCallback? faz, {
      bool aceso = false,
    }) {
      return Tocavel(
        key: ValueKey(chave),
        onTap: faz,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 11),
          decoration: BoxDecoration(
            color: aceso ? AmColors.accentDim : AmColors.chip,
            borderRadius: BorderRadius.circular(10),
          ),
          child: AppText(
            rotulo,
            style: TextStyle(
              fontSize: 11.5,
              fontWeight: FontWeight.w600,
              color: faz == null
                  ? AmColors.muted
                  : aceso
                  ? AmColors.accent
                  : AmColors.text,
            ),
          ),
        ),
      );
    }

    return Padding(
      padding: const EdgeInsets.fromLTRB(10, 2, 10, 0),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            height: 44,
            // Fileira EAGER (nada de ListView preguicoso): os chips do fim
            // existem mesmo fora da tela — e os testes os encontram.
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: Row(
                children: [
                  botao('+ Keyframe', 'estudio-tempo-keyframe', _keyframeAqui),
                  const SizedBox(width: 6),
                  botao('Congelar', 'estudio-tempo-congelar', _congelarAqui),
                  const SizedBox(width: 6),
                  botao('Reverso', 'estudio-tempo-reverso', _reverso),
                  const SizedBox(width: 6),
                  // VOLTAR A VELOCIDADE CONSTANTE: sem isto, quem testava
                  // uma rampa so saia dela pelo desfazer.
                  botao(
                    'Remover curva',
                    'estudio-tempo-remover',
                    hasTimeRemap(l) ? _removerCurva : null,
                  ),
                  const SizedBox(width: 6),
                  Tocavel(
                    key: const ValueKey('estudio-tempo-batidas'),
                    onTap: () =>
                        setState(() => _grudarNasBatidas = !_grudarNasBatidas),
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 9,
                        vertical: 8,
                      ),
                      decoration: BoxDecoration(
                        color: _grudarNasBatidas
                            ? AmColors.accent.withValues(alpha: .18)
                            : AmColors.chip,
                        borderRadius: BorderRadius.circular(10),
                      ),
                      child: Icon(
                        CupertinoIcons.metronome,
                        size: 16,
                        color: _grudarNasBatidas
                            ? AmColors.accent
                            : AmColors.text,
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  for (final preset in SpeedRampPreset.values) ...[
                    _ChipDePreset(
                      preset: preset,
                      aoTocar: () {
                        _c.runAsOneUndo(() {
                          if (_video?.reverse ?? false) {
                            _c.assarReversoNaCurva(widget.layerId);
                          }
                          _c.applySpeedRamp(widget.layerId, preset);
                        });
                        setState(() {
                          _selecionado = null;
                          _enquadrado = false;
                        });
                      },
                    ),
                    const SizedBox(width: 6),
                  ],
                ],
              ),
            ),
          ),
          const SizedBox(height: 6),
          SizedBox(
            height: 38,
            child: Row(
              children: [
                const AppText(
                  'Quadros',
                  style: TextStyle(fontSize: 11, color: AmColors.muted),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: SingleChildScrollView(
                    scrollDirection: Axis.horizontal,
                    child: Row(
                      children: [
                        for (final modo in InterpolacaoDeQuadros.values) ...[
                          botao(
                            rotuloDaInterpolacao(modo),
                            'estudio-tempo-interp-${modo.name}',
                            () {
                              _c.setClipInterpolacao(widget.layerId, modo);
                              setState(() {});
                            },
                            aceso: l.interpolacao == modo,
                          ),
                          const SizedBox(width: 6),
                        ],
                      ],
                    ),
                  ),
                ),
                AppText(
                  '${projeto.fps} fps',
                  style: const TextStyle(fontSize: 10, color: AmColors.muted),
                ),
              ],
            ),
          ),
          if (seloDaInterpolacao(l.interpolacao) case final selo?)
            Align(
              alignment: Alignment.centerLeft,
              child: AppText(
                selo,
                key: const ValueKey('estudio-tempo-selo-da-previa'),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(fontSize: 9.5, color: AmColors.muted),
              ),
            ),
        ],
      ),
    );
  }
}

/// Campo compacto do inspetor: toque abre o teclado numerico do sistema
/// num dialogo (o padrao das folhas), arrastar na horizontal ajusta fino.
///
/// O ARRASTO E O [AmArrastoDeValor], o mesmo das linhas de parametro: ele
/// ACUMULA desde o toque (o valor so muda quando o dono reconstroi, e
/// chegam dois ou tres eventos por quadro — somar cada delta em cima do
/// valor do build perdia quase todo o movimento num arrasto rapido) e
/// entrega uma vez por quadro. Mesmo sinal de antes: direita aumenta.
class _CampoDoEstudio extends StatelessWidget {
  const _CampoDoEstudio({
    required this.rotulo,
    required this.valor,
    required this.sufixo,
    required this.aoDigitar,
    required this.aoArrastar,
    required this.aoSoltar,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
  });

  final String rotulo;
  final double valor;
  final String sufixo;
  final double min;
  final double max;
  final ValueChanged<double>? aoDigitar;
  final ValueChanged<double>? aoArrastar;
  final VoidCallback aoSoltar;

  Future<void> _digitar(BuildContext context) async {
    final campo = TextEditingController(text: valor.toStringAsFixed(1));
    final texto = await showCupertinoDialog<String>(
      context: context,
      builder: (dialogContext) => CupertinoAlertDialog(
        title: AppText(rotulo),
        content: Padding(
          padding: const EdgeInsets.only(top: 10),
          child: CupertinoTextField(
            controller: campo,
            autofocus: true,
            keyboardType: const TextInputType.numberWithOptions(
              decimal: true,
              signed: true,
            ),
            onSubmitted: (v) => Navigator.of(dialogContext).pop(v),
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(dialogContext).pop(),
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(dialogContext).pop(campo.text),
            child: const AppText('Aplicar'),
          ),
        ],
      ),
    );
    final novo = double.tryParse((texto ?? '').replaceAll(',', '.'));
    if (novo != null && novo.isFinite) aoDigitar?.call(novo);
  }

  @override
  Widget build(BuildContext context) {
    final visual = Column(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Container(
          width: 64,
          height: 24,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(8),
          ),
          child: Text(
            '${valor.toStringAsFixed(valor.abs() >= 100 ? 0 : 1)}$sufixo',
            style: TextStyle(
              fontSize: 11,
              color: AmColors.text,
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
        ),
        const SizedBox(height: 2),
        AppText(
          rotulo,
          style: TextStyle(fontSize: 8.5, color: AmColors.muted),
        ),
      ],
    );
    final arrastar = aoArrastar;
    if (arrastar == null) return visual;
    // O dedo sai do vidro ANTES de o reconhecedor de arrasto entregar o
    // ultimo valor pendente (o Listener e avisado primeiro, o roteador de
    // gestos depois). Fechar o gesto numa microtarefa deixa esse ultimo
    // valor entrar no MESMO passo de desfazer.
    void soltar() => Future<void>.microtask(aoSoltar);
    return Listener(
      onPointerUp: (_) => soltar(),
      onPointerCancel: (_) => soltar(),
      child: AmArrastoDeValor(
        value: valor,
        min: min,
        max: max,
        // 2 unidades por pixel: a sensibilidade que o campo ja tinha.
        unitsPerPixel: 2,
        onChanged: arrastar,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: () => _digitar(context),
          child: visual,
        ),
      ),
    );
  }
}

/// Chip de preset com a MINIATURA da propria curva dentro.
class _ChipDePreset extends StatelessWidget {
  const _ChipDePreset({required this.preset, required this.aoTocar});

  final SpeedRampPreset preset;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey('estudio-tempo-preset-${preset.name}'),
    onTap: aoTocar,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 9),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          CustomPaint(
            size: const Size(26, 18),
            painter: _PintorDaMiniatura(preset),
          ),
          const SizedBox(width: 6),
          AppText(
            preset.label,
            style: const TextStyle(fontSize: 11, color: AmColors.text),
          ),
        ],
      ),
    ),
  );
}

class _PintorDaMiniatura extends CustomPainter {
  _PintorDaMiniatura(this.preset);

  final SpeedRampPreset preset;

  @override
  void paint(Canvas canvas, Size size) {
    final trilha = speedRampTrack(
      preset,
      const Duration(seconds: 1),
      const Duration(seconds: 1),
    );
    final caminho = Path();
    for (var i = 0; i <= 20; i++) {
      final t = Duration(microseconds: 1000000 * i ~/ 20);
      final v = trilha.valueAt(t).clamp(0.0, 1.0);
      final p = Offset(size.width * i / 20, size.height * (1 - v));
      i == 0 ? caminho.moveTo(p.dx, p.dy) : caminho.lineTo(p.dx, p.dy);
    }
    canvas.drawPath(
      caminho,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.6
        ..color = AmColors.accent,
    );
  }

  @override
  bool shouldRepaint(_PintorDaMiniatura old) => old.preset != preset;
}

class _PintorDoGrafico extends CustomPainter {
  _PintorDoGrafico({
    required this.aba,
    required this.curva,
    required this.pontos,
    required this.selecionado,
    required this.alcaSaida,
    required this.alcaEntrada,
    required this.quadro,
    required this.t0,
    required this.t1,
    required this.v0,
    required this.v1,
    required this.cabecoteSeg,
    required this.batidas,
    required this.esboco,
  });

  final _Aba aba;
  final AnimatedDouble curva;
  final List<PontoDeTempo> pontos;
  final int? selecionado;
  final Offset? alcaSaida, alcaEntrada;
  final Rect quadro;
  final double t0, t1, v0, v1;
  final double cabecoteSeg;
  final List<double> batidas;

  /// Sem curva criada ainda: desenha a identidade tracejada.
  final bool esboco;

  double _x(double seg) =>
      quadro.left + (seg - t0) / math.max(1e-9, t1 - t0) * quadro.width;

  double _y(double v) =>
      quadro.bottom - (v - v0) / math.max(1e-9, v1 - v0) * quadro.height;

  double _amostra(Duration t) =>
      aba == _Aba.valor ? valorDaCurva(curva, t) : velocidadeDaCurva(curva, t);

  @override
  void paint(Canvas canvas, Size size) {
    final fundo = Paint()..color = const Color(0xFF141821);
    canvas.drawRRect(
      RRect.fromRectAndRadius(quadro, const Radius.circular(10)),
      fundo,
    );
    canvas.save();
    canvas.clipRRect(
      RRect.fromRectAndRadius(quadro, const Radius.circular(10)),
    );

    _linhasDeReferencia(canvas);
    _batidas(canvas);
    _curvaPintada(canvas);
    _cabecote(canvas);
    canvas.restore();
    _rotulos(canvas);
    _pontosPintados(canvas);
    if (aba == _Aba.valor) _alcas(canvas);
  }

  void _linhasDeReferencia(Canvas canvas) {
    final tinta = Paint()
      ..strokeWidth = 1
      ..color = const Color(0xFF232936);
    if (aba == _Aba.velocidade) {
      // 0%, 100%, 200%, 500% e -100%: a regua do grafo de velocidade.
      for (final (nivel, forte) in [
        (0.0, true),
        (1.0, true),
        (2.0, false),
        (5.0, false),
        (-1.0, false),
      ]) {
        if (nivel < v0 || nivel > v1) continue;
        final y = _y(nivel);
        canvas.drawLine(
          Offset(quadro.left, y),
          Offset(quadro.right, y),
          forte
              ? (Paint()
                  ..strokeWidth = 1
                  ..color = const Color(0xFF2E3648))
              : tinta,
        );
      }
    } else {
      for (var i = 1; i < 4; i++) {
        final y = quadro.top + quadro.height * i / 4;
        canvas.drawLine(Offset(quadro.left, y), Offset(quadro.right, y), tinta);
      }
    }
    for (var i = 1; i < 6; i++) {
      final x = quadro.left + quadro.width * i / 6;
      canvas.drawLine(Offset(x, quadro.top), Offset(x, quadro.bottom), tinta);
    }
  }

  void _batidas(Canvas canvas) {
    final tinta = Paint()
      ..strokeWidth = 1
      ..color = AureaColors.accent.withValues(alpha: 0.20);
    for (final b in batidas) {
      if (b < t0 || b > t1) continue;
      final x = _x(b);
      canvas.drawLine(Offset(x, quadro.top), Offset(x, quadro.bottom), tinta);
    }
  }

  void _curvaPintada(Canvas canvas) {
    final caminho = Path();
    final passos = math.max(24, quadro.width ~/ 2);
    var comecou = false;
    for (var i = 0; i <= passos; i++) {
      final seg = t0 + (t1 - t0) * i / passos;
      if (seg < 0) continue;
      final v = _amostra(Duration(microseconds: (seg * 1e6).round()));
      if (!v.isFinite) continue;
      final p = Offset(_x(seg), _y(v));
      if (!comecou) {
        caminho.moveTo(p.dx, p.dy);
        comecou = true;
      } else {
        caminho.lineTo(p.dx, p.dy);
      }
    }
    final tinta = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.4
      ..strokeJoin = StrokeJoin.round
      ..color = esboco ? AmColors.muted : AmColors.accent;
    if (esboco) {
      // Tracejada: ainda nao ha curva no clipe; a primeira edicao cria.
      final medida = caminho.computeMetrics();
      final tracejada = Path();
      for (final m in medida) {
        var d = 0.0;
        while (d < m.length) {
          tracejada.addPath(m.extractPath(d, d + 7), Offset.zero);
          d += 13;
        }
      }
      canvas.drawPath(tracejada, tinta);
    } else {
      canvas.drawPath(caminho, tinta);
    }
  }

  void _cabecote(Canvas canvas) {
    if (cabecoteSeg < t0 || cabecoteSeg > t1) return;
    final x = _x(cabecoteSeg);
    canvas.drawLine(
      Offset(x, quadro.top),
      Offset(x, quadro.bottom),
      Paint()
        ..strokeWidth = 1.4
        ..color = AmColors.accent.withValues(alpha: .6),
    );
  }

  void _pontosPintados(Canvas canvas) {
    for (final p in pontos) {
      final seg = p.tempo.inMicroseconds / 1e6;
      if (seg < t0 - 1e-9 || seg > t1 + 1e-9) continue;
      final sel = p.indice == selecionado;
      if (aba == _Aba.valor) {
        _losango(canvas, Offset(_x(seg), _y(p.valor)), sel);
      } else {
        // Dois pontinhos: a velocidade de entrada e a de saida. Quando
        // coincidem (continuo), um so.
        final e = p.entrada, s = p.saida;
        if (e != null &&
            s != null &&
            (e.velocidade - s.velocidade).abs() < 1e-6) {
          _losango(canvas, Offset(_x(seg), _y(s.velocidade)), sel);
        } else {
          if (e != null) {
            _losango(
              canvas,
              Offset(_x(seg) - 5, _y(e.velocidade)),
              sel,
              meio: true,
            );
          }
          if (s != null) {
            _losango(
              canvas,
              Offset(_x(seg) + 5, _y(s.velocidade)),
              sel,
              meio: true,
            );
          }
        }
      }
    }
  }

  void _losango(
    Canvas canvas,
    Offset c,
    bool selecionado, {
    bool meio = false,
  }) {
    final lado = meio ? 5.5 : 7.0;
    final caminho = Path()
      ..moveTo(c.dx, c.dy - lado)
      ..lineTo(c.dx + lado, c.dy)
      ..lineTo(c.dx, c.dy + lado)
      ..lineTo(c.dx - lado, c.dy)
      ..close();
    canvas.drawPath(
      caminho,
      Paint()..color = selecionado ? AmColors.accent : Colors.white,
    );
    if (selecionado) {
      canvas.drawPath(
        caminho,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = Colors.white,
      );
    }
  }

  void _alcas(Canvas canvas) {
    final sel = selecionado;
    if (sel == null || sel >= pontos.length) return;
    final p = pontos[sel];
    final centro = Offset(_x(p.tempo.inMicroseconds / 1e6), _y(p.valor));
    for (final alca in [alcaSaida, alcaEntrada]) {
      if (alca == null) continue;
      canvas.drawLine(
        centro,
        alca,
        Paint()
          ..strokeWidth = 1.2
          ..color = AmColors.accent.withValues(alpha: .7),
      );
      canvas.drawCircle(alca, 6, Paint()..color = const Color(0xFF141821));
      canvas.drawCircle(
        alca,
        6,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = AmColors.accent,
      );
    }
  }

  void _rotulos(Canvas canvas) {
    void texto(String s, Offset onde, {TextAlign align = TextAlign.right}) {
      final tp = TextPainter(
        text: TextSpan(
          text: s,
          style: const TextStyle(fontSize: 8.5, color: AmColors.muted),
        ),
        textDirection: TextDirection.ltr,
        textAlign: align,
      )..layout();
      tp.paint(
        canvas,
        onde - Offset(align == TextAlign.right ? tp.width : 0, tp.height / 2),
      );
    }

    if (aba == _Aba.velocidade) {
      for (final nivel in [0.0, 1.0, 2.0, 5.0, -1.0]) {
        if (nivel < v0 || nivel > v1) continue;
        texto('${(nivel * 100).round()}%', Offset(quadro.left - 4, _y(nivel)));
      }
    } else {
      for (var i = 0; i <= 4; i++) {
        final v = v0 + (v1 - v0) * i / 4;
        texto('${v.toStringAsFixed(1)}s', Offset(quadro.left - 4, _y(v)));
      }
    }
    for (var i = 0; i <= 3; i++) {
      final seg = t0 + (t1 - t0) * i / 3;
      texto(
        formatTime(Duration(microseconds: (seg * 1e6).round())),
        Offset(_x(seg) + 14, quadro.bottom + 9),
      );
    }
  }

  @override
  bool shouldRepaint(_PintorDoGrafico old) =>
      old.aba != aba ||
      !identical(old.curva, curva) ||
      old.selecionado != selecionado ||
      old.t0 != t0 ||
      old.t1 != t1 ||
      old.v0 != v0 ||
      old.v1 != v1 ||
      old.cabecoteSeg != cabecoteSeg ||
      old.alcaSaida != alcaSaida ||
      old.alcaEntrada != alcaEntrada ||
      old.esboco != esboco;
}

class _PintorDaMiniTimeline extends CustomPainter {
  _PintorDaMiniTimeline({
    required this.duracao,
    required this.pontos,
    required this.cabecote,
  });

  final Duration duracao;
  final List<Duration> pontos;
  final Duration cabecote;

  @override
  void paint(Canvas canvas, Size size) {
    final faixa = Rect.fromLTRB(10, 9, size.width - 10, size.height - 9);
    canvas.drawRRect(
      RRect.fromRectAndRadius(faixa, const Radius.circular(6)),
      Paint()..color = AmColors.chip,
    );
    double x(Duration t) =>
        faixa.left +
        faixa.width *
            (t.inMicroseconds / math.max(1, duracao.inMicroseconds)).clamp(
              0.0,
              1.0,
            );
    for (final p in pontos) {
      final c = Offset(x(p), size.height / 2);
      final caminho = Path()
        ..moveTo(c.dx, c.dy - 4)
        ..lineTo(c.dx + 4, c.dy)
        ..lineTo(c.dx, c.dy + 4)
        ..lineTo(c.dx - 4, c.dy)
        ..close();
      canvas.drawPath(caminho, Paint()..color = Colors.white);
    }
    final cx = x(cabecote);
    canvas.drawLine(
      Offset(cx, 2),
      Offset(cx, size.height - 2),
      Paint()
        ..strokeWidth = 2
        ..color = AmColors.accent,
    );
  }

  @override
  bool shouldRepaint(_PintorDaMiniTimeline old) =>
      old.duracao != duracao ||
      old.cabecote != cabecote ||
      old.pontos.length != pontos.length;
}
