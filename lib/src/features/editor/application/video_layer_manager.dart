import 'optical_flow_preview.dart';

import 'dart:async';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:video_player/video_player.dart';

import '../domain/audio_mix.dart';
import '../domain/cut_ops.dart';
import 'duck_service.dart';
import '../domain/layer.dart';
import 'media_preview_service.dart';
import 'preview_stats.dart';
import 'proxy_service.dart';
import 'audio_render_service.dart';

/// Uma midia da cena: [id] e a camada (ou `id:audiofx`), [key] e o
/// TOCADOR que ela usa — pedacos continuos de um mesmo arquivo dividem
/// a mesma chave (ver [VideoLayerManager.trilhosContinuos]).
typedef _Midia = ({
  String id,
  String key,
  String path,
  double volume,
  Duration offset,
  Layer layer,
});

/// Gerencia os VideoPlayerController das camadas de video e audio e mantem
/// todos sincronizados ao clock mestre (play/pause/seek + correcao de drift).
///
/// TRAVADA NO VIDEO DECUPADO (dono, 14/09/2026): "se o video estiver
/// decupado e cortado, na hora do preview ele fica dando travada". Havia
/// quatro causas somadas, e as quatro estao presas em
/// `test/video_cut_junction_test.dart`:
///   1. cada pedaco tinha tocador proprio, criado e DESCARTADO a cada
///      corte — liberar um decodificador na thread principal e um soluco;
///      agora pedacos continuos dividem um tocador e os outros sao
///      reaproveitados ([_estacionados]);
///   2. o ExoPlayer publica `isPlaying=false` enquanto enche o buffer, e o
///      sync entendia isso como "parou" e mandava seek + play — que
///      esvazia o buffer que ele estava enchendo ([_tocouEm]);
///   3. a primeira amostra de posicao depois do play era a posicao PARADA
///      no alvo do seek, e virava o vies do relogio: o relogio adiantava,
///      o corte chegava cedo e o pre-roll ja nao servia ([_maturidadeMs]);
///   4. um projeto cortado em pedacos curtos pre-rolava todos os pedacos
///      dos proximos dois segundos de uma vez ([_tetoPreRoll]).
class VideoLayerManager {
  /// [relogioMs] so existe para teste: o relogio de parede nao anda junto
  /// com o tempo simulado do teste de widget.
  VideoLayerManager({int Function()? relogioMs}) : _agoraMs = relogioMs {
    AudioRenderService.instance.revision.addListener(_audioChanged);
    OpticalFlowPreview.instance.revision.addListener(_audioChanged);
  }
  final int Function()? _agoraMs;
  int get _msAgora => _agoraMs?.call() ?? _relogio.elapsedMilliseconds;
  Timer? _audioDebounce;
  int _lastAudioRevision = -1;
  void _audioChanged() {
    sync(_lastLayers, _lastT, _lastPlaying, seekRevision: _seekRevision);
    revision.value++;
  }

  void _prepareAudio() {
    _audioDebounce?.cancel();
    _audioDebounce = Timer(const Duration(milliseconds: 300), () {
      final service = AudioRenderService.instance;
      for (final layer in _lastLayers) {
        if (_lastT < layer.startTime - _janelaPreRoll ||
            _lastT >= layer.endTime) {
          continue;
        }
        if (AudioRenderService.needed(layer) &&
            service.ready(layer) == null &&
            !service.busy(layer) &&
            service.error(layer) == null) {
          unawaited(
            service
                .prepare(layer)
                .catchError((Object _) => AudioRenderService.source(layer)),
          );
        }
      }
    });
  }

  // Todos os mapas abaixo sao indexados pela CHAVE DO TOCADOR.
  final Map<String, VideoPlayerController> _controllers = {};
  final Map<String, String> _controllerPath = {};
  final Map<String, Object> _controllerTicket = {};
  final Map<String, Future<void>> _initializing = {};
  final Map<String, DateTime> _lastSeek = {};
  int _lastProxyRevision = -1;
  int _lastFlowRevision = -1;
  final Set<String> _remapProxiesRequested = {};
  int _seekRevision = 0;
  final Map<String, Object> _positioning = {};
  final Set<String> _starting = {};
  final Map<String, ({Duration time, bool play, double rate})>
  _positionTargets = {};

  /// Camada -> chave do tocador (so onde difere do id da camada).
  Map<String, String> _keyOf = const {};

  /// Tocadores REAPROVEITADOS que ainda nao mostraram um quadro deste
  /// pedaco: ate o primeiro seek terminar, a textura ainda tem o quadro de
  /// onde o tocador estava antes.
  final Set<String> _semQuadro = {};

  /// Relogio monotono do gerenciador (ms). DateTime.now pode andar para
  /// tras (ajuste de hora); para medir "ha quanto tempo mandei tocar" nao.
  final Stopwatch _relogio = Stopwatch()..start();

  /// QUANDO o gerenciador mandou cada tocador tocar (ms de [_relogio]).
  ///
  /// Enquanto houver entrada aqui, `isPlaying == false` NAO quer dizer
  /// parado: o ExoPlayer publica isso durante BUFFERING, e o AVPlayer
  /// enquanto espera para tocar. Reposicionar nesse estado era o que
  /// transformava um buffer de 100 ms numa travada no ponto do corte.
  final Map<String, int> _tocouEm = {};

  /// Idade minima do play para uma amostra de posicao valer como
  /// referencia do relogio. O plugin consulta a posicao a cada 100 ms; a
  /// primeira resposta depois do play ainda e a posicao parada no alvo do
  /// seek.
  static const int _maturidadeMs = 350;

  /// Vies maximo de AMOSTRAGEM (us). A amostra nova chega com no maximo um
  /// tique de idade; um erro maior que isso na primeira amostra nao e
  /// vies — e o tocador atrasado de verdade, e isso se corrige, nao se
  /// adota como referencia.
  static const int _tetoDoViesUs = 60000;

  /// Quantos pedacos que ainda NAO comecaram podem ser preparados ao mesmo
  /// tempo. Um video picado em pedacos de meio segundo tinha quatro ou
  /// cinco decodificadores fazendo seek juntos.
  static const int _tetoPreRoll = 2;

  /// A decoder must finish seeking before play. Keep only the newest
  /// requested position while a native seek is pending.
  void _position(
    String id,
    VideoPlayerController c,
    Duration time, {
    bool play = false,
    double rate = 1,
  }) {
    _positionTargets[id] = (time: time, play: play, rate: rate);
    if (play) {
      _starting.add(id);
    } else {
      _starting.remove(id);
    }
    if (_positioning.containsKey(id)) return;
    final ticket = Object();
    _positioning[id] = ticket;
    bool live() =>
        identical(_positioning[id], ticket) && identical(_controllers[id], c);
    // Sincrono, antes do primeiro await: daqui em diante o tocador esta
    // sendo reposicionado, nao "tocando e bufferizando".
    final estavaTocando = _tocouEm.remove(id) != null;
    unawaited(() async {
      try {
        if (estavaTocando || c.value.isPlaying) await c.pause();
        while (live()) {
          final target = _positionTargets.remove(id);
          if (target == null) break;
          await c.seekTo(target.time);
          if (!live()) return;
          if (_semQuadro.remove(id)) revision.value++;
          if (_positionTargets.containsKey(id)) continue;
          _lastPos[id] = null;
          _biasUs.remove(id);
          if (target.play && (_lastPlaying || _scrubbing)) {
            try {
              await c.setPlaybackSpeed(target.rate);
            } catch (_) {
              if (live()) _failedNativeRate[id] = target.rate;
              return;
            }
            if (!live()) return;
            _appliedRate[id] = target.rate;
            if (_positionTargets.containsKey(id)) continue;
            if (_lastPlaying || _scrubbing) {
              _tocouEm[id] = _msAgora;
              await c.play();
            }
          }
        }
      } catch (_) {
        // A removed or failed decoder must not start playback later.
        if (live()) _positionTargets.remove(id);
      } finally {
        if (identical(_positioning[id], ticket)) {
          _positioning.remove(id);
          _starting.remove(id);
        }
      }
    }());
  }

  /// PARA um tocador — inclusive o que a plataforma diz parado mas vai
  /// voltar a tocar sozinho quando o buffer encher.
  void _parar(String key, VideoPlayerController c) {
    final mandado = _tocouEm.remove(key) != null;
    if (mandado || c.value.isPlaying) c.pause();
  }

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
  final List<_Midia> _media = [];

  /// Estruturas do caminho quente, reaproveitadas a cada tick.
  List<bool> _ativo = const [];
  final Map<String, int> _condutor = {};
  final List<int> _candidatos = [];
  final List<String> _mortos = [];

  /// Ultima posicao JA VISTA por camada: o plugin so publica posicao de
  /// tempos em tempos, e ancorar o relogio numa amostra repetida
  /// empurraria a composicao para tras.
  final Map<String, Duration?> _lastPos = {};

  /// Vies constante de amostragem por camada (us): a posicao publicada
  /// pelo plugin sempre chega com atraso. Nao e deriva — e medido uma
  /// vez por reproducao e descontado de todas as amostras seguintes.
  final Map<String, int> _biasUs = {};

  /// PRE-ROLL: pedaco que vai entrar daqui a pouco ja foi posicionado
  /// (seek feito, tocador parado) no offset guardado aqui. Na entrada,
  /// so o play — sem o seek que travava o preview no ponto do corte.
  final Map<String, Duration> _preRolled = {};

  /// LARGADA ANTECIPADA: o pedaco que entra ja esta TOCANDO, mudo, um
  /// pouco antes da juncao. O pre-roll tirou o seek do ponto do corte;
  /// isto tira o PLAY — que nao e gratis: o decodificador leva dezenas
  /// de ms para retomar, e era a ultima travadinha da emenda. O truque:
  /// o pre-roll posiciona a fonte [_avancoDaLargada] ANTES do ponto de
  /// entrada, o play sai mudo quando falta esse tanto, e na juncao o
  /// tocador ja esta exatamente no quadro certo, quente — so o volume
  /// sobe. Emenda de graca.
  final Set<String> _emLargada = {};
  static const Duration _avancoDaLargada = Duration(milliseconds: 300);

  /// Quanto antes do inicio de um pedaco ele e preparado.
  static const Duration _janelaPreRoll = Duration(seconds: 2);

  /// TOCADORES ESTACIONADOS: ja inicializados, parados, esperando o
  /// proximo pedaco do mesmo arquivo.
  ///
  /// Um video decupado em vinte pedacos criava e descartava vinte
  /// tocadores durante a reproducao. Criar e inicializar um decodificador
  /// custa centenas de ms; descartar (release do ExoPlayer) roda na thread
  /// principal. Os dois caiam exatamente no ponto do corte. Agora o
  /// tocador que sai de cena espera aqui e o proximo pedaco do mesmo
  /// arquivo o adota: so um seek, feito no pre-roll.
  final List<({String path, VideoPlayerController c})> _estacionados = [];
  Timer? _faxina;
  static const int _tetoEstacionados = 2;

  /// Acima disto (vivos + estacionados), o estacionado mais antigo e
  /// liberado mesmo durante a reproducao: aparelho sem decodificador livre
  /// e pior que um soluco.
  static const int _tetoDeDecodificadores = 5;

  VideoPlayerController? controllerFor(String layerId) {
    final key = _keyOf[layerId] ?? layerId;
    if (_semQuadro.contains(key)) return null;
    return _controllers[key];
  }

  /// A chave do tocador de uma camada (para teste e diagnostico).
  @visibleForTesting
  String playerKeyFor(String layerId) => _keyOf[layerId] ?? layerId;

  /// Quantos tocadores parados esperam reuso (para teste).
  @visibleForTesting
  int get parkedCount => _estacionados.length;

  /// TRILHO CONTINUO: os pedacos de um DIVIDIR puro compartilham um tocador.
  ///
  /// Dividir um clipe em dois (sem tirar nada do meio) cria duas camadas
  /// que tocam o MESMO arquivo sem salto: o fim de uma e, quadro a quadro,
  /// o comeco da outra. Com um tocador por camada, a juncao trocava de
  /// decodificador — e isso aparecia como soluco na imagem e buraco no som
  /// exatamente onde nao ha corte nenhum. Aqui a segunda camada herda a
  /// chave da primeira: o mesmo tocador segue tocando e a juncao nao custa
  /// nada.
  ///
  /// So encadeia o que e continuo de verdade: mesmo arquivo de reproducao,
  /// a segunda comeca onde a primeira termina (na linha do tempo e na
  /// fonte), mesma velocidade dentro da faixa nativa (0,5..2x), sem
  /// reverso e sem Time Remap. Decupagem (trecho tirado do meio) nao e
  /// continua: essa troca de tocador, com pre-roll e tocador reaproveitado.
  ///
  /// Devolve a chave de TODA camada de midia aceita por [caminhoDe] (a
  /// propria id quando nao encadeia).
  @visibleForTesting
  static Map<String, String> trilhosContinuos(
    List<Layer> layers,
    String? Function(Layer layer) caminhoDe,
  ) {
    bool encadeavel(Layer l) {
      return switch (l) {
        VideoLayer v =>
          !v.reverse && v.speed >= 0.5 && v.speed <= 2.0 && !hasTimeRemap(v),
        AudioLayer a => a.speed >= 0.5 && a.speed <= 2.0,
        _ => false,
      };
    }

    final chave = <String, String>{};
    final pedacos = <({Layer layer, String path})>[];
    for (final l in layers) {
      if (l is! VideoLayer && l is! AudioLayer) continue;
      final path = caminhoDe(l);
      if (path == null) continue;
      chave[l.id] = l.id;
      if (encadeavel(l)) pedacos.add((layer: l, path: path));
    }
    pedacos.sort((a, b) => a.layer.startTime.compareTo(b.layer.startTime));

    const folga = Duration(milliseconds: 1);
    const folgaFonte = Duration(milliseconds: 20);
    final abertos = <({Layer ultimo, String path})>[];
    for (final p in pedacos) {
      final l = p.layer;
      abertos.removeWhere((a) => a.ultimo.endTime < l.startTime - folga);
      var juntou = false;
      for (var i = 0; i < abertos.length; i++) {
        final a = abertos[i];
        if (a.path != p.path || !_continua(a.ultimo, l, folga, folgaFonte)) {
          continue;
        }
        chave[l.id] = chave[a.ultimo.id]!;
        abertos[i] = (ultimo: l, path: p.path);
        juntou = true;
        break;
      }
      if (!juntou) abertos.add((ultimo: l, path: p.path));
    }
    return chave;
  }

  static bool _continua(
    Layer antes,
    Layer depois,
    Duration folga,
    Duration folgaFonte,
  ) {
    if ((depois.startTime - antes.endTime).abs() > folga) return false;
    if (antes is VideoLayer && depois is VideoLayer) {
      return (antes.speed - depois.speed).abs() < 1e-6 &&
          (depois.sourceOffset - (antes.sourceOffset + antes.sourceSpan))
                  .abs() <=
              folgaFonte;
    }
    if (antes is AudioLayer && depois is AudioLayer) {
      return (antes.speed - depois.speed).abs() < 1e-6 &&
          (depois.sourceOffset - (antes.sourceOffset + antes.sourceSpan))
                  .abs() <=
              folgaFonte;
    }
    return false;
  }

  /// Remove tambem um controller que ainda esta inicializando. O Future
  /// nao pode ser cancelado pelo plugin, entao retirar o caminho funciona
  /// como um token de cancelamento: ao terminar, [_ensure] ve que a camada
  /// nao e mais desejada e descarta o controller antes de publica-lo.
  ///
  /// Um controller PRONTO nao e descartado: vai para o estacionamento.
  void _evict(String id) {
    final path = _controllerPath[id];
    _positioning.remove(id);
    _starting.remove(id);
    _positionTargets.remove(id);
    _controllerPath.remove(id);
    _controllerTicket.remove(id);
    _initializing.remove(id);
    _lastSeek.remove(id);
    _appliedVolume.remove(id);
    _appliedRate.remove(id);
    _failedNativeRate.remove(id);
    _lastPos.remove(id);
    _biasUs.remove(id);
    _preRolled.remove(id);
    _emLargada.remove(id);
    _semQuadro.remove(id);
    final mandado = _tocouEm.remove(id) != null;
    final c = _controllers.remove(id);
    if (c == null) return;
    if (path == null || !c.value.isInitialized || c.value.hasError) {
      c.dispose();
      return;
    }
    if (mandado || c.value.isPlaying) c.pause();
    _estacionados.add((path: path, c: c));
    while (_estacionados.length > _tetoEstacionados ||
        (_estacionados.isNotEmpty &&
            _controllers.length + _estacionados.length >
                _tetoDeDecodificadores)) {
      _estacionados.removeAt(0).c.dispose();
    }
    _agendarFaxina();
  }

  /// Libera os estacionados que ninguem adotou — mas nunca no meio da
  /// reproducao (o release do decodificador e justamente o soluco).
  void _agendarFaxina() {
    _faxina?.cancel();
    _faxina = Timer(const Duration(seconds: 5), () {
      if (_lastPlaying) {
        _agendarFaxina();
        return;
      }
      for (final e in _estacionados) {
        e.c.dispose();
      }
      _estacionados.clear();
    });
  }

  VideoPlayerController? _adotarEstacionado(String path) {
    for (var i = _estacionados.length - 1; i >= 0; i--) {
      if (_estacionados[i].path == path) return _estacionados.removeAt(i).c;
    }
    return null;
  }

  void _ensure(String id, String path, double volume) {
    final currentPath = _controllerPath[id];
    if (currentPath != null && currentPath != path) {
      // O proxy (ou o fluxo optico) ficou pronto no MEIO da reproducao:
      // trocar de arquivo agora e um tocador novo no meio do play. A troca
      // espera a pausa — o original toca igual enquanto isso.
      if (_lastPlaying && _controllers.containsKey(id)) return;
      _evict(id);
    }
    if (_controllers.containsKey(id) || _initializing.containsKey(id)) {
      return;
    }
    final adotado = _adotarEstacionado(path);
    if (adotado != null) {
      _controllerPath[id] = path;
      _controllerTicket[id] = Object();
      _controllers[id] = adotado;
      _semQuadro.add(id);
      adotado.setVolume(volume);
      _appliedVolume[id] = volume;
      return;
    }
    final controller = VideoPlayerController.file(
      File(path),
      videoPlayerOptions: VideoPlayerOptions(mixWithOthers: true),
    );
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
          sync(_lastLayers, _lastT, _lastPlaying, seekRevision: _seekRevision);
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
          _parar(id, controller);
        }
      }),
    );
  }

  /// Chamado a cada mudanca relevante do clock. Camadas de AUDIO usam o
  /// mesmo pipeline (ExoPlayer/AVPlayer tocam audio puro sem textura).
  /// QUEM AINDA TOCA SOM, por id.
  ///
  /// O olho da timeline tirava a IMAGEM e deixava o SOM tocando: esconder
  /// uma camada de video continuava com a voz no ar, e numa camada de
  /// audio o olho nao fazia absolutamente nada. Quem sabe quem esta
  /// escondido (ou fora do solo) e o projeto, e nao a lista de camadas —
  /// por isso a resposta vem de fora.
  bool Function(String id)? soaAgora;

  Duration? sync(
    List<Layer> layers,
    Duration t,
    bool isPlaying, {
    int seekRevision = 0,
  }) {
    final jumped = seekRevision != _seekRevision;
    _seekRevision = seekRevision;
    if (jumped) {
      _lastPos.clear();
      _biasUs.clear();
      _preRolled.clear();
      _emLargada.clear();
    }
    if (isPlaying && _scrubbing) {
      _scrubbing = false;
      _scrubTimer?.cancel();
    }
    _lastT = t;
    _lastPlaying = isPlaying;

    // Zero alocacao no caminho quente (travada-periodica C6): a lista de
    // midias so e remontada quando a CENA muda, nunca a cada tick.
    final proxyRevision = ProxyService.instance.revision.value;
    final flowRevision = OpticalFlowPreview.instance.revision.value;
    final audioService = AudioRenderService.instance;
    final audioRevision = audioService.revision.value;
    if (!identical(layers, _lastLayers) ||
        proxyRevision != _lastProxyRevision ||
        flowRevision != _lastFlowRevision ||
        audioRevision != _lastAudioRevision) {
      _lastLayers = layers;
      _lastProxyRevision = proxyRevision;
      _lastFlowRevision = flowRevision;
      _lastAudioRevision = audioRevision;
      _prepareAudio();

      // PROXY quando ha: quadro-chave a cada 6 quadros faz o scrub ficar
      // continuo. Sem proxy, o original — nunca deixa de tocar por falta
      // de cache.
      final caminhos = <String, String>{
        for (final l in layers)
          if (l is AudioLayer)
            l.id: audioService.ready(l) ?? l.sourcePath
          else if (l is VideoLayer)
            l.id:
                OpticalFlowPreview.instance.ready(l) ??
                ProxyService.instance.playbackPath(l.sourcePath),
      };
      final chaves = trilhosContinuos(layers, (l) => caminhos[l.id]);
      _keyOf = {
        for (final e in chaves.entries)
          if (e.key != e.value) e.key: e.value,
      };
      _media
        ..clear()
        // Audio primeiro: ele e o relogio mestre (nunca "pula" frame).
        ..addAll([
          for (final l in layers)
            if (l is AudioLayer)
              (
                id: l.id,
                key: chaves[l.id] ?? l.id,
                path: caminhos[l.id]!,
                volume: l.volume,
                offset: l.sourceOffset,
                layer: l,
              ),
          for (final l in layers)
            if (l is VideoLayer && audioService.ready(l) != null)
              (
                id: '${l.id}:audiofx',
                key: '${l.id}:audiofx',
                path: audioService.ready(l)!,
                volume: l.volume,
                offset: l.sourceOffset,
                layer: l,
              ),
          for (final l in layers)
            if (l is VideoLayer)
              (
                id: l.id,
                key: chaves[l.id] ?? l.id,
                path: caminhos[l.id]!,
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
      final liveKeys = {for (final m in _media) m.key};
      final dead = _controllerPath.keys
          .where((id) => !liveKeys.contains(id))
          .toList();
      for (final id in dead) {
        _evict(id);
      }
    }
    final mediaLayers = _media;
    final n = mediaLayers.length;

    // QUEM APARECE NESTE QUADRO (uma vez por midia, e nao duas).
    if (_ativo.length != n) _ativo = List<bool>.filled(n, false);
    for (var i = 0; i < n; i++) {
      _ativo[i] = mediaLayers[i].layer.activeAt(t);
    }

    // QUEM DIRIGE CADA TOCADOR: o pedaco ativo; senao, o proximo a entrar
    // (pre-roll), com teto. O conjunto de decodificadores vivos e
    // EXATAMENTE isto — camadas longe do playhead nao retem codec (um
    // projeto muito cortado esgotava o limite do aparelho).
    _condutor.clear();
    for (var i = 0; i < n; i++) {
      final key = mediaLayers[i].key;
      if (_ativo[i] && !_condutor.containsKey(key)) _condutor[key] = i;
    }
    _candidatos.clear();
    for (var i = 0; i < n; i++) {
      final m = mediaLayers[i];
      if (_ativo[i] || _condutor.containsKey(m.key)) continue;
      final ate = m.layer.startTime - t;
      if (ate > Duration.zero && ate <= _janelaPreRoll) _candidatos.add(i);
    }
    if (_candidatos.length > 1) {
      _candidatos.sort(
        (a, b) => mediaLayers[a].layer.startTime.compareTo(
          mediaLayers[b].layer.startTime,
        ),
      );
    }
    var vagas = _tetoPreRoll;
    for (final i in _candidatos) {
      final key = mediaLayers[i].key;
      if (_condutor.containsKey(key)) continue;
      if (vagas == 0) break;
      _condutor[key] = i;
      vagas--;
    }

    _mortos.clear();
    for (final id in _controllerPath.keys) {
      if (!_condutor.containsKey(id)) _mortos.add(id);
    }
    for (final id in _mortos) {
      _evict(id);
    }
    for (var i = 0; i < n; i++) {
      final m = mediaLayers[i];
      if (_condutor[m.key] == i) _ensure(m.key, m.path, m.volume);
      final ate = m.layer.startTime - t;
      final perto =
          _ativo[i] || (ate > Duration.zero && ate <= _janelaPreRoll);
      if (!perto) continue;
      if (m.layer is VideoLayer) {
        final video = m.layer as VideoLayer;
        unawaited(OpticalFlowPreview.instance.ensure(video));
        if (hasTimeRemap(video) &&
            _remapProxiesRequested.add(video.sourcePath)) {
          unawaited(
            ProxyService.instance.ensureProxy(video.sourcePath, force: true),
          );
        }
      }
      if (AudioRenderService.needed(m.layer) &&
          audioService.ready(m.layer) == null &&
          !audioService.busy(m.layer) &&
          audioService.error(m.layer) == null &&
          _audioDebounce?.isActive != true) {
        _prepareAudio();
      }
    }

    // Relogio mestre desta passada: a primeira midia ativa que trouxer
    // amostra nova de posicao (audio primeiro — ver ordenacao acima).
    Duration? master;
    final agoraMs = _msAgora;

    for (var i = 0; i < n; i++) {
      final m = mediaLayers[i];
      // Pedaco que nao dirige o proprio tocador neste quadro: ou nao
      // aparece, ou divide o tocador com o pedaco que esta tocando.
      if (_condutor[m.key] != i) continue;
      final key = m.key;
      final layer = m.layer;
      final active = _ativo[i];
      // Condutor que nao aparece so pode ser o pre-roll.
      final vemAi = !active;
      // CONTROLLER SOB DEMANDA: um video decupado em vinte pedacos e
      // o mesmo arquivo vinte vezes. Vinte tocadores preparados de uma
      // vez sao vinte decodificadores vivos — e o preview que engasga.
      final controller = _controllers[key];
      if (controller == null) continue;
      final isAudio = layer is AudioLayer;

      // O GANHO VEM DA MESMA CONTA QUE A EXPORTACAO USA: volume, ganho,
      // mudo, fade e o envelope de ducking ja calculado. Ate aqui o
      // preview tocava so o volume da camada, e o arquivo saia com fade e
      // abaixamento que ninguem tinha ouvido antes de exportar.
      var effectiveVolume = (soaAgora?.call(layer.id) ?? true)
          ? layerAudioGainAt(
              layer,
              t,
              duck: duckEnvelopes[layer.id] ?? DuckEnvelope.neutro,
            )
          // ESCONDIDA NAO SOA. O olho promete tirar do preview, e som e
          // preview tanto quanto imagem.
          : 0.0;
      if (layer is VideoLayer &&
          m.id == layer.id &&
          audioService.ready(layer) != null) {
        effectiveVolume = 0;
      }
      if (((_appliedVolume[key] ?? -1) - effectiveVolume).abs() > 0.001) {
        _appliedVolume[key] = effectiveVolume;
        controller.setVolume(effectiveVolume.clamp(0.0, 1.0));
      }
      // A VELOCIDADE estica a leitura da fonte: um segundo na linha
      // consome [speed] segundos de arquivo.
      final timelineLocal = layer.localTime(t);
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
          ((_failedNativeRate[key] ?? -1) - rate).abs() < 0.001;
      // Reverso e trechos parados exigem decodificacao por source-time.
      // Curvas para frente usam playback continuo com velocidade local,
      // evitando reiniciar o decoder a cada quadro do Time Remap.
      // Fora de 0,5..2x fazemos o mesmo: Android pode limitar extremos e
      // iOS pode rejeita-los; seek por source-time sustenta todo 0,1..10x.
      final frameDriven =
          layer is VideoLayer &&
          (layer.reverse ||
              vel <= 0 ||
              rate < 0.5 ||
              rate > 2.0 ||
              nativeRateFailed);

      if (vemAi && isPlaying) {
        // PRE-ROLL: posiciona o pedaco que vem ai enquanto o atual ainda
        // toca. O seek e o que custa (o decodificador volta ao quadro-
        // chave anterior e avanca ate o ponto); feito agora, na entrada
        // sobra so o play. Era o "trava quando chega onde decupei".
        final entrada = switch (layer) {
          VideoLayer v => videoAbsoluteSourceTimeAt(v, Duration.zero),
          _ => m.offset,
        };
        // A LARGADA comeca ANTES do ponto de entrada na fonte, para o
        // play mudo consumir exatamente a folga e chegar na juncao no
        // quadro certo. So em trecho de leitura simples (sem reverso,
        // sem Time Remap, velocidade nativa) e quando o arquivo tem
        // fonte antes do ponto (decupagem tem; comeco de arquivo nao).
        final avancoNaFonte = Duration(
          microseconds: (_avancoDaLargada.inMicroseconds * rate).round(),
        );
        final podeLargar = !frameDriven && entrada >= avancoNaFonte;
        final prepareAt = podeLargar ? entrada - avancoNaFonte : entrada;
        if (_preRolled[key] != prepareAt) {
          _emLargada.remove(key);
          _parar(key, controller);
          _position(key, controller, prepareAt);
          _preRolled[key] = prepareAt;
        }
        final falta = layer.startTime - t;
        if (podeLargar &&
            falta <= _avancoDaLargada &&
            !_positioning.containsKey(key) &&
            _emLargada.add(key)) {
          // Mudo, na velocidade de entrada, ja rodando: a juncao vira
          // continuacao. O volume real entra no tique em que o pedaco
          // fica ativo, pela mesma conta de sempre.
          _appliedVolume[key] = 0;
          controller.setVolume(0);
          _setNativeRate(key, controller, rate);
          _tocouEm[key] = agoraMs;
          controller.play();
        }
        continue;
      }

      if (active && isPlaying) {
        _emLargada.remove(key);
        if (jumped && !frameDriven) {
          _position(key, controller, local, play: true, rate: rate);
          continue;
        }
        if (_positioning.containsKey(key)) {
          // A pre-roll or paused scrub may still be decoding on play.
          if (!_starting.contains(key) && !frameDriven) {
            _position(key, controller, local, play: true, rate: rate);
          }
          continue;
        }
        if (isAudio && nativeRateFailed) {
          // Nao ha quadro para dirigir manualmente numa faixa somente de
          // audio. Mantemos o clock da composicao independente em vez de
          // repetir uma chamada que a plataforma acabou de rejeitar.
          _parar(key, controller);
          _lastPos[key] = null;
          _biasUs.remove(key);
        } else if (frameDriven) {
          _preRolled.remove(key);
          _parar(key, controller);
          final last = _lastSeek[key];
          if (last == null ||
              DateTime.now().difference(last) >
                  const Duration(milliseconds: 25)) {
            _position(key, controller, local);
            _lastSeek[key] = DateTime.now();
          }
          _lastPos[key] = null;
          _biasUs.remove(key);
        } else if (!controller.value.isPlaying) {
          final mandadoEm = _tocouEm[key];
          if (mandadoEm != null && !controller.value.isCompleted) {
            // JA MANDAMOS TOCAR: parado aqui e buffer enchendo ou a
            // plataforma ainda confirmando. Nada de seek. So se ficar
            // parado tempo demais sem estar em buffer (foco de audio
            // perdido, por exemplo) o play e repetido — sem seek.
            if (agoraMs - mandadoEm > 1500 && !controller.value.isBuffering) {
              _tocouEm[key] = agoraMs;
              controller.play();
            }
            continue;
          }
          // Pre-rolado no ponto certo: nada de seek de novo. O atraso
          // entre o pre-roll e a entrada e de no maximo um tique. Quem
          // largou antecipado nem passa por aqui (ja esta tocando); a
          // folga de 250 ms cobre o alvo deslocado da largada tambem.
          final preparado = _preRolled.remove(key);
          final jaNoLugar =
              preparado != null &&
              (local - preparado).abs() <
                  const Duration(milliseconds: 250) + _avancoDaLargada;
          if (!jaNoLugar) {
            _position(key, controller, local, play: true, rate: rate);
            continue;
          }
          // O tocador tem velocidade propria: usar ela e o que mantem o
          // som continuo em vez de picotado por seeks.
          _setNativeRate(key, controller, rate);
          _tocouEm[key] = agoraMs;
          controller.play();
          _lastPos[key] = null;
          _biasUs.remove(key);
        } else {
          if ((rate - (_appliedRate[key] ?? 0)).abs() > 0.015) {
            _setNativeRate(key, controller, rate);
          }
          // PR-J1: NENHUM seek durante a reproducao. O antigo "se a
          // deriva passar de X, corrige" corrigia em BLOCO, e o bloco
          // (flush do decoder) era a travada periodica. Agora a midia e
          // o RELOGIO MESTRE: cada amostra NOVA de posicao ancora o
          // clock da composicao continuamente, sem acumular deriva.
          final pos = controller.value.position;
          if (_lastPos[key] != pos) {
            _lastPos[key] = pos;
            final mandadoEm = _tocouEm[key];
            // A primeira resposta depois do play ainda e a posicao
            // parada no alvo do seek: nao serve de referencia.
            final madura =
                mandadoEm == null || agoraMs - mandadoEm >= _maturidadeMs;
            // A posicao do plugin so atualiza de tempos em tempos:
            // ancorar com amostra repetida empurraria o relogio para tras.
            if (master == null &&
                madura &&
                !frameDriven &&
                !(layer is VideoLayer && hasTimeRemap(layer))) {
              // A amostra nasce um pouco VELHA. Esse vies e constante e
              // NAO e deriva: ancorar nele puxaria o relogio. Guardamos o
              // vies na primeira amostra madura (com teto: vies grande e
              // atraso de verdade) e corrigimos so o que DERIVOU dali.
              final errUs = (pos - local).inMicroseconds;
              final bias = _biasUs[key] ??= errUs.clamp(
                -_tetoDoViesUs,
                _tetoDoViesUs,
              );
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
        final ultimo = _lastSeek[key];
        if (ultimo == null ||
            agora.difference(ultimo) > const Duration(milliseconds: 90)) {
          _position(key, controller, local, play: true, rate: rate);
          _lastSeek[key] = agora;
        }
      } else {
        _preRolled.remove(key);
        _emLargada.remove(key);
        _parar(key, controller);
        // Scrub pausado: video precisa do seek para MOSTRAR o frame;
        // audio pausado nao tem nada a mostrar — seek so na hora do
        // play. Era o que afogava o preview ao arrastar a timeline.
        if (active && !isAudio) {
          final last = _lastSeek[key];
          if (jumped ||
              last == null ||
              DateTime.now().difference(last) >
                  const Duration(milliseconds: 66)) {
            _position(key, controller, local);
            _lastSeek[key] = DateTime.now();
          }
        }
        _lastPos[key] = null;
        _biasUs.remove(key);
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
      if (!_lastPlaying) pauseAll();
    });
  }

  void pauseAll() {
    for (final e in _controllers.entries) {
      _parar(e.key, e.value);
    }
  }

  void dispose() {
    _audioDebounce?.cancel();
    _faxina?.cancel();
    AudioRenderService.instance.revision.removeListener(_audioChanged);
    OpticalFlowPreview.instance.revision.removeListener(_audioChanged);
    _positioning.clear();
    _starting.clear();
    _positionTargets.clear();
    _scrubTimer?.cancel();
    _controllerPath.clear();
    _controllerTicket.clear();
    _initializing.clear();
    for (final c in _controllers.values) {
      c.dispose();
    }
    _controllers.clear();
    for (final e in _estacionados) {
      e.c.dispose();
    }
    _estacionados.clear();
    _lastSeek.clear();
    _appliedVolume.clear();
    _appliedRate.clear();
    _failedNativeRate.clear();
    _lastPos.clear();
    _biasUs.clear();
    _preRolled.clear();
    _emLargada.clear();
    _tocouEm.clear();
    _semQuadro.clear();
    revision.dispose();
  }
}
