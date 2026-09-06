import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:video_player/video_player.dart';

import '../domain/audio_mix.dart';
import '../domain/cut_ops.dart';
import 'duck_service.dart';
import '../domain/layer.dart';
import 'media_preview_service.dart';
import 'preview_stats.dart';
import 'proxy_service.dart';

/// Gerencia um VideoPlayerController por camada de video e mantem todos
/// sincronizados ao clock mestre (play/pause/seek + correcao de drift).
class VideoLayerManager {
  final Map<String, VideoPlayerController> _controllers = {};
  final Map<String, String> _controllerPath = {};
  final Map<String, Object> _controllerTicket = {};
  final Map<String, Future<void>> _initializing = {};
  final Map<String, DateTime> _lastSeek = {};
  int _lastProxyRevision = -1;

  /// Ultimo volume APLICADO por camada: setVolume e uma chamada de
  /// plataforma — repeti-la a cada tick (30x/s por camada) derruba o
  /// preview. So chama quando o valor realmente muda.
  final Map<String, double> _appliedVolume = {};

  /// OS ENVELOPES DE DUCKING, calculados fora e entregues prontos. Quem
  /// os monta e quem tem os picos da voz na mao; aqui so se le. E o mesmo
  /// mapa que vai para a exportacao — e por isso que o arquivo sai com o
  /// abaixamento que se ouviu no preview.
  Map<String, DuckEnvelope> duckEnvelopes = const {};
  final Map<String, double> _appliedRate = {};
  final Map<String, double> _failedNativeRate = {};

  /// Notifica quando um controller termina de inicializar (para o preview
  /// trocar o placeholder pelo video).
  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Ultimo estado visto pelo [sync]: quando um controller termina de
  /// inicializar, o sync roda DE NOVO com ele — sem isso, um clip
  /// CORTADO (sourceOffset != 0) nascia mostrando o frame 0 do arquivo
  /// ate alguem mexer no clock ("nao decupa nada").
  List<Layer> _lastLayers = const [];
  Duration _lastT = Duration.zero;
  bool _lastPlaying = false;

  /// Midias da cena, remontadas so quando a cena muda (zero alocacao por
  /// tick). Audio vem antes de video: o audio e o relogio mestre.
  final List<
    ({String id, String path, double volume, Duration offset, Layer layer})
  >
  _media = [];

  /// Ultima posicao JA VISTA por camada: o plugin so publica posicao a
  /// cada ~500 ms, e ancorar o relogio numa amostra repetida empurraria
  /// a composicao para tras.
  final Map<String, Duration?> _lastPos = {};

  /// Vies constante de amostragem por camada (us): a posicao publicada
  /// pelo plugin sempre chega com atraso. Nao e deriva — e medido uma
  /// vez por reproducao e descontado de todas as amostras seguintes.
  final Map<String, int> _biasUs = {};

  /// PRE-ROLL: pedaco que vai entrar daqui a pouco ja foi posicionado
  /// (seek feito, tocador parado) no offset guardado aqui. Na entrada,
  /// so o play — sem o seek que travava o preview no ponto do corte.
  final Map<String, Duration> _preRolled = {};

  /// Quanto antes do inicio de um pedaco ele e preparado.
  static const Duration _janelaPreRoll = Duration(seconds: 2);

  VideoPlayerController? controllerFor(String layerId) => _controllers[layerId];

  /// Remove tambem um controller que ainda esta inicializando. O Future
  /// nao pode ser cancelado pelo plugin, entao retirar o caminho funciona
  /// como um token de cancelamento: ao terminar, [_ensure] ve que a camada
  /// nao e mais desejada e descarta o controller antes de publica-lo.
  void _evict(String id) {
    _controllerPath.remove(id);
    _controllerTicket.remove(id);
    _initializing.remove(id);
    _controllers.remove(id)?.dispose();
    _lastSeek.remove(id);
    _appliedVolume.remove(id);
    _appliedRate.remove(id);
    _failedNativeRate.remove(id);
    _lastPos.remove(id);
    _biasUs.remove(id);
    _preRolled.remove(id);
  }

  void _ensure(String id, String path, double volume) {
    final currentPath = _controllerPath[id];
    if (currentPath != null && currentPath != path) {
      _evict(id);
    }
    if (_controllers.containsKey(id) || _initializing.containsKey(id)) {
      return;
    }
    final controller = VideoPlayerController.file(File(path));
    final ticket = Object();
    _controllerPath[id] = path;
    _controllerTicket[id] = ticket;
    _initializing[id] = controller
        .initialize()
        .then((_) {
          if (_controllerPath[id] != path ||
              !identical(_controllerTicket[id], ticket)) {
            controller.dispose();
            return;
          }
          _controllers[id] = controller;
          _initializing.remove(id);
          controller.setVolume(volume);
          _appliedVolume[id] = volume;
          revision.value++;
          // Seek inicial no instante atual do clock (decupagem correta).
          sync(_lastLayers, _lastT, _lastPlaying);
        })
        .catchError((_) {
          if (_controllerPath[id] == path &&
              identical(_controllerTicket[id], ticket)) {
            _initializing.remove(id);
            _controllerPath.remove(id);
            _controllerTicket.remove(id);
          }
          controller.dispose();
        });
  }

  void _setNativeRate(
    String id,
    VideoPlayerController controller,
    double rate,
  ) {
    _appliedRate[id] = rate;
    _failedNativeRate.remove(id);
    unawaited(
      controller.setPlaybackSpeed(rate).catchError((Object _) {
        if (identical(_controllers[id], controller) &&
            _appliedRate[id] == rate) {
          _failedNativeRate[id] = rate;
          if (controller.value.isPlaying) controller.pause();
        }
      }),
    );
  }

  /// Chamado a cada mudanca relevante do clock. Camadas de AUDIO usam o
  /// mesmo pipeline (ExoPlayer/AVPlayer tocam audio puro sem textura).
  Duration? sync(List<Layer> layers, Duration t, bool isPlaying) {
    _lastT = t;
    _lastPlaying = isPlaying;

    // Zero alocacao no caminho quente (travada-periodica C6): a lista de
    // midias so e remontada quando a CENA muda, nunca a cada tick.
    final proxyRevision = ProxyService.instance.revision.value;
    if (!identical(layers, _lastLayers) ||
        proxyRevision != _lastProxyRevision) {
      _lastLayers = layers;
      _lastProxyRevision = proxyRevision;
      _media
        ..clear()
        // Audio primeiro: ele e o relogio mestre (nunca "pula" frame).
        ..addAll([
          for (final l in layers)
            if (l is AudioLayer)
              (
                id: l.id,
                path: l.sourcePath,
                volume: l.volume,
                offset: l.sourceOffset,
                layer: l,
              ),
          for (final l in layers)
            if (l is VideoLayer)
              (
                id: l.id,
                // PROXY quando ha: quadro-chave a cada 6 quadros faz o
                // scrub ficar continuo. Sem proxy, o original — nunca
                // deixa de tocar por falta de cache.
                path: ProxyService.instance.playbackPath(l.sourcePath),
                volume: l.volume,
                offset: l.sourceOffset,
                layer: l,
              ),
        ]);

      // O envelope de abaixamento e PRE-CALCULADO aqui, uma vez por
      // mudanca de cena — nunca por tique. Decidir em tempo real gastaria
      // CPU na reproducao e daria um resultado por execucao.
      duckEnvelopes = buildProjectDuckEnvelopes(
        layers,
        MediaPreviewService.instance.peaksOf,
      );

      // Descarta controllers de camadas removidas.
      final liveIds = {for (final m in _media) m.id};
      final dead = _controllerPath.keys
          .where((id) => !liveIds.contains(id))
          .toList();
      for (final id in dead) {
        _evict(id);
      }
    }
    final mediaLayers = _media;
    final transitions = transitionContextsAt(layers, t);

    // O conjunto de decodificadores vivos e EXATAMENTE o que aparece
    // neste quadro mais o pre-roll imediato. Camadas que continuam no
    // projeto, mas estao longe do playhead, nao podem reter codecs para
    // sempre (um projeto muito cortado esgotava o limite do aparelho).
    final wanted = <String>{};
    for (final m in mediaLayers) {
      final active = visibleForCut(layers, m.layer, t, contexts: transitions);
      final untilStart = m.layer.startTime - t;
      final preRoll =
          !active && untilStart > Duration.zero && untilStart <= _janelaPreRoll;
      if (active || preRoll) wanted.add(m.id);
    }
    for (final id in _controllerPath.keys.toList()) {
      if (!wanted.contains(id)) _evict(id);
    }
    for (final m in mediaLayers) {
      if (wanted.contains(m.id)) _ensure(m.id, m.path, m.volume);
    }

    // Relogio mestre desta passada: a primeira midia ativa que trouxer
    // amostra nova de posicao (audio primeiro — ver ordenacao acima).
    Duration? master;

    for (final m in mediaLayers) {
      final layer = m.layer;
      final active = visibleForCut(layers, layer, t, contexts: transitions);
      final ateComecar = layer.startTime - t;
      final vemAi =
          !active && ateComecar > Duration.zero && ateComecar <= _janelaPreRoll;
      // CONTROLLER SOB DEMANDA: um video decupado em vinte pedacos e
      // o mesmo arquivo vinte vezes. Vinte tocadores preparados de uma
      // vez sao vinte decodificadores vivos — e o preview que engasga.
      // So o pedaco ativo (e o que vem ai) ganha tocador; os outros
      // ganham o deles quando o cabecote chegar perto.
      final controller = _controllers[m.id];
      if (controller == null) continue;
      final isAudio = layer is AudioLayer;

      final layerTransitions = transitionContextsForLayer(
        layers,
        layer.id,
        t,
        contexts: transitions,
      );
      // O GANHO VEM DA MESMA CONTA QUE A EXPORTACAO USA: volume, ganho,
      // mudo, fade e o envelope de ducking ja calculado. Ate aqui o
      // preview tocava so o volume da camada, e o arquivo saia com fade e
      // abaixamento que ninguem tinha ouvido antes de exportar.
      var effectiveVolume = layerAudioGainAt(
        layer,
        t,
        duck: duckEnvelopes[layer.id] ?? DuckEnvelope.neutro,
      );
      for (final transition in layerTransitions) {
        if (!transition.transition.crossfadeAudio) continue;
        final angle = transition.progress * math.pi / 2;
        effectiveVolume *= layer.id == transition.incoming.id
            ? math.sin(angle)
            : math.cos(angle);
      }
      if (((_appliedVolume[m.id] ?? -1) - effectiveVolume).abs() > 0.001) {
        _appliedVolume[m.id] = effectiveVolume;
        controller.setVolume(effectiveVolume.clamp(0.0, 1.0));
      }
      // A VELOCIDADE estica a leitura da fonte: um segundo na linha
      // consome [speed] segundos de arquivo.
      final timelineLocal = localTimeForCutContexts(layer, t, layerTransitions);
      final vel = switch (layer) {
        VideoLayer v => videoPlaybackRateAt(v, timelineLocal),
        AudioLayer a => a.speed,
        _ => 1.0,
      };
      final local = switch (layer) {
        VideoLayer v => videoAbsoluteSourceTimeAt(v, timelineLocal),
        _ =>
          m.offset +
              Duration(
                microseconds: (timelineLocal.inMicroseconds * vel).round(),
              ),
      };
      final rate = vel.abs().clamp(0.1, 10.0).toDouble();
      final nativeRateFailed =
          ((_failedNativeRate[m.id] ?? -1) - rate).abs() < 0.001;
      // Reverso e Time Remap nao possuem um unico clock de playback: o
      // quadro correto e uma funcao pura do tempo da composicao. Nesses
      // modos o player vira apenas decoder e recebe o source-time exato.
      // Fora de 0,5..2x fazemos o mesmo: Android pode limitar extremos e
      // iOS pode rejeita-los; seek por source-time sustenta todo 0,1..10x.
      final frameDriven =
          layer is VideoLayer &&
          (layer.reverse ||
              hasTimeRemap(layer) ||
              rate < 0.5 ||
              rate > 2.0 ||
              nativeRateFailed);

      if (vemAi && isPlaying) {
        // PRE-ROLL: posiciona o pedaco que vem ai enquanto o atual ainda
        // toca. O seek e o que custa (o decodificador volta ao quadro-
        // chave anterior e avanca ate o ponto); feito agora, na entrada
        // sobra so o play. Era o "trava quando chega onde decupei".
        final prepareAt = switch (layer) {
          VideoLayer v => videoAbsoluteSourceTimeAt(v, Duration.zero),
          _ => m.offset,
        };
        if (_preRolled[m.id] != prepareAt) {
          if (controller.value.isPlaying) controller.pause();
          controller.seekTo(prepareAt);
          _preRolled[m.id] = prepareAt;
        }
        continue;
      }

      if (active && isPlaying) {
        if (isAudio && nativeRateFailed) {
          // Nao ha quadro para dirigir manualmente numa faixa somente de
          // audio. Mantemos o clock da composicao independente em vez de
          // repetir uma chamada que a plataforma acabou de rejeitar.
          if (controller.value.isPlaying) controller.pause();
          _lastPos[m.id] = null;
          _biasUs.remove(m.id);
        } else if (frameDriven) {
          _preRolled.remove(m.id);
          if (controller.value.isPlaying) controller.pause();
          final last = _lastSeek[layer.id];
          if (last == null ||
              DateTime.now().difference(last) >
                  const Duration(milliseconds: 25)) {
            controller.seekTo(local);
            _lastSeek[layer.id] = DateTime.now();
          }
          _lastPos[m.id] = null;
          _biasUs.remove(m.id);
        } else if (!controller.value.isPlaying) {
          // Pre-rolado no ponto certo: nada de seek de novo. O atraso
          // entre o pre-roll e a entrada e de no maximo um tique.
          final preparado = _preRolled.remove(m.id);
          final jaNoLugar =
              preparado != null &&
              (local - preparado).abs() < const Duration(milliseconds: 250);
          if (!jaNoLugar) controller.seekTo(local);
          // O tocador tem velocidade propria: usar ela e o que mantem o
          // som continuo em vez de picotado por seeks.
          _setNativeRate(m.id, controller, rate);
          controller.play();
          _lastPos[m.id] = null;
          _biasUs.remove(m.id);
        } else {
          if ((rate - (_appliedRate[m.id] ?? 0)).abs() > 0.015) {
            _setNativeRate(m.id, controller, rate);
          }
          // PR-J1: NENHUM seek durante a reproducao. O antigo "se a
          // deriva passar de X, corrige" corrigia em BLOCO, e o bloco
          // (flush do decoder) era a travada periodica. Agora a midia e
          // o RELOGIO MESTRE: cada amostra NOVA de posicao ancora o
          // clock da composicao continuamente, sem acumular deriva.
          final pos = controller.value.position;
          if (_lastPos[m.id] != pos) {
            _lastPos[m.id] = pos;
            // A posicao do plugin so atualiza a cada ~500 ms: ancorar
            // com amostra repetida empurraria o relogio para tras.
            if (master == null && !frameDriven) {
              // A amostra nasce VELHA (idade media ~250 ms). Esse vies e
              // constante e NAO e deriva: ancorar nele puxaria o relogio
              // para tras. Guardamos o vies na primeira amostra e
              // corrigimos so o que DERIVOU a partir dali.
              final errUs = (pos - local).inMicroseconds;
              final bias = _biasUs[m.id] ??= errUs;
              final sourceErrorUs = errUs - bias;
              // [pos-local] esta no relogio da FONTE. A composicao anda
              // 1/rate desse valor: sem a divisao, 10x corrigia dez vezes
              // demais e 0,1x corrigia dez vezes de menos.
              final timelineErrorUs =
                  (sourceErrorUs / vel.abs().clamp(0.1, 10.0).toDouble())
                      .round();
              master = t + Duration(microseconds: timelineErrorUs);
              FrameLog.reportDrift(-timelineErrorUs / 1000.0);
            }
          }
        }
      } else if (_scrubbing && active && effectiveVolume > 0.001) {
        // SCRUB DE AUDIO: enquanto a pessoa arrasta a regua, o som toca
        // em lasquinhas. Ouvir onde se esta e o que torna a decupagem
        // rapida — procurar a silaba no olho, na forma de onda, e muito
        // mais lento do que ouvir.
        //
        // Sem esta saida, o proprio sync pausaria no tique seguinte e o
        // scrub nao sairia do lugar.
        final agora = DateTime.now();
        final ultimo = _lastSeek[layer.id];
        if (ultimo == null ||
            agora.difference(ultimo) > const Duration(milliseconds: 90)) {
          controller.seekTo(local);
          _lastSeek[layer.id] = agora;
          if (!controller.value.isPlaying) controller.play();
        }
      } else {
        _preRolled.remove(m.id);
        if (controller.value.isPlaying) controller.pause();
        // Scrub pausado: video precisa do seek para MOSTRAR o frame;
        // audio pausado nao tem nada a mostrar — seek so na hora do
        // play. Era o que afogava o preview ao arrastar a timeline.
        if (active && !isAudio) {
          final last = _lastSeek[layer.id];
          if (last == null ||
              DateTime.now().difference(last) >
                  const Duration(milliseconds: 66)) {
            controller.seekTo(local);
            _lastSeek[layer.id] = DateTime.now();
          }
        }
        _lastPos[m.id] = null;
        _biasUs.remove(m.id);
      }
    }
    return master;
  }

  bool _scrubbing = false;
  Timer? _scrubTimer;

  /// SCRUB: liga o modo "tocar em lasquinhas" por um instante.
  ///
  /// Chamado a cada movimento do dedo na regua; o temporizador desliga
  /// sozinho quando o dedo para. Sem o desligamento automatico, o audio
  /// continuaria tocando depois que a pessoa soltasse.
  void scrub() {
    _scrubbing = true;
    _scrubTimer?.cancel();
    _scrubTimer = Timer(const Duration(milliseconds: 160), () {
      _scrubbing = false;
      pauseAll();
    });
  }

  void pauseAll() {
    for (final c in _controllers.values) {
      if (c.value.isPlaying) c.pause();
    }
  }

  void dispose() {
    _scrubTimer?.cancel();
    _controllerPath.clear();
    _controllerTicket.clear();
    _initializing.clear();
    for (final c in _controllers.values) {
      c.dispose();
    }
    _controllers.clear();
    _lastSeek.clear();
    _appliedVolume.clear();
    _appliedRate.clear();
    _failedNativeRate.clear();
    _lastPos.clear();
    _biasUs.clear();
    _preRolled.clear();
    revision.dispose();
  }
}
