// "SE O VIDEO ESTIVER DECUPADO E CORTADO, NA HORA DO PREVIEW ELE FICA
// DANDO TRAVADA" (dono, 14/09/2026).
//
// Um tocador falso com VARIOS players (ids proprios, eventos proprios e um
// registro de chamadas) reproduz o que o aparelho fazia no ponto do corte:
// criar e descartar decodificador, seek em cima de buffer, relogio
// ancorado na posicao parada. Cada teste prende uma das causas.
import 'dart:async';
import 'dart:ui';

import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/cut.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:flutter_test/flutter_test.dart';
// Tests replace the native player underneath our video_player dependency.
// ignore: depend_on_referenced_packages
import 'package:video_player_platform_interface/video_player_platform_interface.dart';

class _Player {
  _Player(this.id);
  final int id;
  final eventos = StreamController<VideoEvent>();
  Duration pos = Duration.zero;
  bool tocando = false;
}

class _Plataforma extends VideoPlayerPlatform {
  final players = <int, _Player>{};
  final log = <String>[];
  var _proximo = 1;

  @override
  Future<void> init() async {}
  @override
  Future<void> setMixWithOthers(bool mixWithOthers) async {}
  @override
  Future<int?> createWithOptions(VideoCreationOptions options) async {
    final p = _Player(_proximo++);
    players[p.id] = p;
    log.add('create#${p.id}');
    p.eventos.add(
      VideoEvent(
        eventType: VideoEventType.initialized,
        size: const Size(160, 90),
        duration: const Duration(seconds: 60),
      ),
    );
    return p.id;
  }

  @override
  Stream<VideoEvent> videoEventsFor(int playerId) =>
      players[playerId]!.eventos.stream;
  @override
  Future<void> dispose(int playerId) async => log.add('dispose#$playerId');
  @override
  Future<void> setLooping(int playerId, bool looping) async {}
  @override
  Future<void> setVolume(int playerId, double volume) async {}
  @override
  Future<void> setPlaybackSpeed(int playerId, double speed) async {}
  @override
  Future<void> play(int playerId) async {
    final p = players[playerId]!;
    p.tocando = true;
    log.add('play#$playerId@${p.pos.inMilliseconds}');
  }

  @override
  Future<void> pause(int playerId) async {
    players[playerId]!.tocando = false;
    log.add('pause#$playerId');
  }

  @override
  Future<void> seekTo(int playerId, Duration position) async {
    players[playerId]!.pos = position;
    log.add('seek#$playerId@${position.inMilliseconds}');
  }

  @override
  Future<Duration> getPosition(int playerId) async => players[playerId]!.pos;

  void avancar(Duration d) {
    for (final p in players.values) {
      if (p.tocando) p.pos += d;
    }
  }

  Iterable<String> doTipo(String tipo) => log.where((c) => c.startsWith(tipo));

  /// Sem await: o fechamento so completa quando o evento de fim e
  /// entregue, e dentro do tempo simulado do teste isso nunca acontece.
  void fechar() {
    for (final p in players.values) {
      unawaited(p.eventos.close());
    }
  }
}

VideoLayer _pedaco(
  String id,
  int inicioMs,
  int fimMs,
  int fonteMs, {
  String path = 'mesmo.mp4',
  double speed = 1,
  bool reverse = false,
  ClipTransition? transitionIn,
}) => VideoLayer(
  id: id,
  name: id,
  startTime: Duration(milliseconds: inicioMs),
  duration: Duration(milliseconds: fimMs - inicioMs),
  sourcePath: path,
  sourceOffset: Duration(milliseconds: fonteMs),
  speed: speed,
  reverse: reverse,
  transitionIn: transitionIn,
);

const _quadro = Duration(milliseconds: 33);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('quem divide tocador', () {
    String? caminho(Layer l) => switch (l) {
      VideoLayer v => v.sourcePath,
      AudioLayer a => a.sourcePath,
      _ => null,
    };

    test('divisao pura encadeia; decupagem, velocidade e reverso nao', () {
      final chaves = VideoLayerManager.trilhosContinuos([
        _pedaco('a', 0, 5000, 0),
        _pedaco('b', 5000, 9000, 5000),
        _pedaco('c', 9000, 12000, 9000),
        // decupagem: a fonte salta 3 s
        _pedaco('d', 12000, 14000, 15000),
        // velocidade diferente
        _pedaco('e', 14000, 15000, 17000, speed: 2),
        // reverso
        _pedaco('f', 15000, 16000, 19000, reverse: true),
        // outro arquivo continuo no tempo
        _pedaco('g', 16000, 17000, 0, path: 'outro.mp4'),
      ], caminho);
      expect(chaves['b'], 'a');
      expect(chaves['c'], 'a', reason: 'tres pedacos, um tocador');
      expect(chaves['d'], 'd');
      expect(chaves['e'], 'e');
      expect(chaves['f'], 'f');
      expect(chaves['g'], 'g');
    });

    test('transicao entre os pedacos exige dois tocadores', () {
      final chaves = VideoLayerManager.trilhosContinuos([
        _pedaco('a', 0, 5000, 0),
        _pedaco(
          'b',
          5000,
          9000,
          5000,
          transitionIn: ClipTransition(
            duration: const Duration(milliseconds: 500),
            outgoingLayerId: 'a',
          ),
        ),
      ], caminho);
      expect(chaves['b'], 'b');
    });

    test('duas copias coladas no mesmo ponto: so uma herda o tocador', () {
      final chaves = VideoLayerManager.trilhosContinuos([
        _pedaco('a', 0, 5000, 0),
        _pedaco('b1', 5000, 9000, 5000),
        _pedaco('b2', 5000, 9000, 5000),
      ], caminho);
      expect({chaves['b1'], chaves['b2']}, {'a', 'b2'});
    });
  });

  testWidgets('divisao pura: o mesmo tocador atravessa a juncao sem nada', (
    tester,
  ) async {
    final nativo = _Plataforma();
    VideoPlayerPlatform.instance = nativo;
    var ms = 0;
    final gerente = VideoLayerManager(relogioMs: () => ms);
    final layers = [_pedaco('a', 0, 5000, 0), _pedaco('b', 5000, 10000, 5000)];

    gerente.sync(layers, Duration.zero, false);
    await tester.pumpAndSettle();
    expect(gerente.playerKeyFor('b'), 'a');
    nativo.log.clear();

    for (var t = Duration.zero; t < const Duration(seconds: 7); t += _quadro) {
      gerente.sync(layers, t, true);
      nativo.avancar(_quadro);
      ms += _quadro.inMilliseconds;
      await tester.pump(_quadro);
    }

    expect(nativo.doTipo('create'), isEmpty);
    expect(nativo.doTipo('dispose'), isEmpty);
    expect(nativo.doTipo('pause'), isEmpty, reason: 'o som nao pode abrir buraco');
    expect(
      nativo.doTipo('seek').toList(),
      ['seek#1@0'],
      reason: 'so o seek do play inicial; a juncao nao busca nada',
    );
    expect(identical(gerente.controllerFor('a'), gerente.controllerFor('b')), isTrue);

    gerente.dispose();
    await tester.pump();
    nativo.fechar();
  });

  testWidgets(
    'decupagem: pre-roll no tocador reaproveitado, entrada so com play, nada descartado',
    (tester) async {
      final nativo = _Plataforma();
      VideoPlayerPlatform.instance = nativo;
      var ms = 0;
      final gerente = VideoLayerManager(relogioMs: () => ms);
      final layers = [
        _pedaco('a', 0, 5000, 0),
        _pedaco('b', 5000, 10000, 8000),
        _pedaco('c', 10000, 15000, 20000),
      ];

      gerente.sync(layers, Duration.zero, false);
      await tester.pumpAndSettle();
      nativo.log.clear();

      for (var t = Duration.zero; t < const Duration(seconds: 12); t += _quadro) {
        gerente.sync(layers, t, true);
        nativo.avancar(_quadro);
        ms += _quadro.inMilliseconds;
        await tester.pump(_quadro);
      }

      expect(
        nativo.doTipo('create').toList(),
        ['create#2'],
        reason: 'o terceiro pedaco adota o tocador do primeiro',
      );
      expect(nativo.doTipo('dispose'), isEmpty, reason: 'descartar no play e o soluco');
      expect(
        nativo.doTipo('seek').toList(),
        ['seek#1@0', 'seek#2@8000', 'seek#1@20000'],
        reason: 'um seek por pedaco, sempre ANTES da entrada (pre-roll)',
      );
      expect(nativo.log, contains('play#2@8000'));
      expect(nativo.log, contains('play#1@20000'));
      // A entrada de cada pedaco vem DEPOIS do seek do pre-roll dele, e o
      // tocador que sai e so pausado.
      expect(
        nativo.log.indexOf('seek#2@8000'),
        lessThan(nativo.log.indexOf('play#2@8000')),
      );
      expect(nativo.log, contains('pause#1'));

      gerente.dispose();
      await tester.pump();
      nativo.fechar();
    },
  );

  testWidgets('buffer no meio do pedaco nao vira seek + play', (tester) async {
    final nativo = _Plataforma();
    VideoPlayerPlatform.instance = nativo;
    var ms = 0;
    final gerente = VideoLayerManager(relogioMs: () => ms);
    final layers = [_pedaco('a', 0, 10000, 3000)];

    gerente.sync(layers, Duration.zero, false);
    await tester.pumpAndSettle();

    var t = Duration.zero;
    Future<void> tocar(int quadros) async {
      for (var i = 0; i < quadros; i++) {
        gerente.sync(layers, t, true);
        nativo.avancar(_quadro);
        ms += _quadro.inMilliseconds;
        t += _quadro;
        await tester.pump(_quadro);
      }
    }

    await tocar(20);
    nativo.log.clear();
    // O ExoPlayer entra em BUFFERING: isPlaying=false chega do nativo.
    final p = nativo.players[1]!;
    p.eventos
      ..add(VideoEvent(eventType: VideoEventType.bufferingStart))
      ..add(
        VideoEvent(eventType: VideoEventType.isPlayingStateUpdate, isPlaying: false),
      );
    await tester.pump();
    expect(gerente.controllerFor('a')!.value.isPlaying, isFalse);
    await tocar(24); // ~800 ms parado no buffer
    expect(nativo.doTipo('seek'), isEmpty);
    expect(nativo.doTipo('pause'), isEmpty);
    p.eventos
      ..add(VideoEvent(eventType: VideoEventType.bufferingEnd))
      ..add(
        VideoEvent(eventType: VideoEventType.isPlayingStateUpdate, isPlaying: true),
      );
    await tocar(10);
    expect(nativo.doTipo('seek'), isEmpty);

    // Parado de verdade (sem buffer) por mais de 1,5 s: repete o play, sem
    // seek.
    p.eventos.add(
      VideoEvent(eventType: VideoEventType.isPlayingStateUpdate, isPlaying: false),
    );
    await tester.pump();
    await tocar(50);
    expect(nativo.doTipo('seek'), isEmpty);
    expect(nativo.doTipo('play'), isNotEmpty);

    gerente.dispose();
    await tester.pump();
    nativo.fechar();
  });

  testWidgets('amostra parada logo depois do play nao ancora o relogio', (
    tester,
  ) async {
    final nativo = _Plataforma();
    VideoPlayerPlatform.instance = nativo;
    var ms = 0;
    final gerente = VideoLayerManager(relogioMs: () => ms);
    final layers = [_pedaco('a', 0, 10000, 0)];

    gerente.sync(layers, Duration.zero, false);
    await tester.pumpAndSettle();

    // O decodificador demora 300 ms para comecar a andar depois do play:
    // as primeiras amostras repetem a posicao do seek.
    final mestres = <(int, Duration?)>[];
    var t = Duration.zero;
    for (var i = 0; i < 60; i++) {
      mestres.add((ms, gerente.sync(layers, t, true)));
      if (ms >= 300) nativo.avancar(_quadro);
      ms += _quadro.inMilliseconds;
      t += _quadro;
      await tester.pump(_quadro);
    }
    final cedo = mestres.where((e) => e.$1 < 350 && e.$2 != null);
    expect(cedo, isEmpty, reason: 'antes de maduro, nada de ancorar');
    final ancorados = mestres.where((e) => e.$2 != null).toList();
    expect(ancorados, isNotEmpty);
    // Vies com teto: o atraso de ~300 ms e ERRO (o relogio vai ceder),
    // e nao referencia adotada para sempre.
    final ultimo = ancorados.last;
    final tDoUltimo = Duration(milliseconds: ultimo.$1);
    expect(ultimo.$2!, lessThan(tDoUltimo - const Duration(milliseconds: 150)));

    gerente.dispose();
    await tester.pump();
    nativo.fechar();
  });

  testWidgets('relogio mestre nunca volta um quadro por causa da ancoragem', (
    tester,
  ) async {
    final pc = PlaybackController(
      vsync: tester,
      durationOf: () => const Duration(seconds: 30),
    );
    pc.play();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 34));
    final umQuadro = pc.time.value;
    expect(umQuadro, const Duration(microseconds: 33334));
    // Midia 80 ms atras: a base recua 8 ms e o instante bruto cai no
    // quadro anterior.
    pc.anchorToMedia(umQuadro - const Duration(milliseconds: 80));
    expect(pc.debugBaseShiftUs, -8000);
    await tester.pump(const Duration(milliseconds: 1));
    expect(pc.time.value, umQuadro, reason: 'segura, nao volta');
    await tester.pump(const Duration(milliseconds: 40));
    expect(pc.time.value, greaterThan(umQuadro));
    pc.pause();
    pc.dispose();
  });
}
