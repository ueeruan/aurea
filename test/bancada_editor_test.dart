// A BANCADA DO EDITOR: onde o quadro se perde FORA da cena 3D.
//
// O relato foi direto: "o lag tambem acontece fora da Scene 3D... na UI
// da edicao da gargalo". Esta bancada mede o editor inteiro montando e
// desenhando quadros — parado, tocando, arrastando uma camada e rolando
// a linha do tempo — e diz quanto custa cada um deles.
//
// COMO ELA MEDE: num teste de widget o quadro nao vai para a GPU, mas
// tudo o que vem ANTES vai: construir a arvore, medir o layout e gravar
// as chamadas de pintura. E ai que mora o custo de CPU do aplicativo —
// e e ele que aparece como engasgo no aparelho.
//
// Rodar:  flutter test test/bancada_editor_test.dart
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:aurea/src/features/editor/application/perfil3d.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;

/// Um projeto do tamanho de um trabalho de verdade: texto animado,
/// formas, uma camada com efeitos e uma pilha que a composicao tem de
/// resolver quadro a quadro.
VideoProject _projeto({required int camadas}) {
  final lista = <Layer>[];
  for (var i = 0; i < camadas; i++) {
    final atraso = Duration(milliseconds: i * 120);
    if (i % 3 == 0) {
      lista.add(
        TextLayer(
          id: 't$i',
          name: 'Texto $i',
          text: 'AUREA $i',
          startTime: Duration.zero,
          duration: const Duration(seconds: 10),
          fontSize: 64,
          position: AnimatedOffset(const Offset(540, 400))
              .withKeyframe(atraso, const Offset(400, 300))
              .withKeyframe(atraso + const Duration(seconds: 3), const Offset(700, 900)),
          opacity: AnimatedDouble(1)
              .withKeyframe(atraso, 0)
              .withKeyframe(atraso + const Duration(milliseconds: 600), 1),
        ),
      );
    } else {
      lista.add(
        ShapeLayer(
          id: 's$i',
          name: 'Forma $i',
          startTime: Duration.zero,
          duration: const Duration(seconds: 10),
          position: AnimatedOffset(Offset(200.0 + i * 40, 300.0 + i * 25))
              .withKeyframe(Duration.zero, Offset(200.0 + i * 40, 300))
              .withKeyframe(const Duration(seconds: 4), Offset(800.0, 900.0 + i)),
          rotation: AnimatedDouble(0)
              .withKeyframe(Duration.zero, 0)
              .withKeyframe(const Duration(seconds: 5), 180),
          effects: i % 4 == 0
              ? [
                  EffectInstance(
                    type: EffectType.gaussianBlur,
                    params: {'raio': AnimatedDouble(6)},
                  ),
                ]
              : const [],
        ),
      );
    }
  }
  return VideoProject(
    name: 'bancada',
    createdAt: DateTime(2026, 9, 8),
    aspectRatio: 9 / 16,
    resolutionHeight: 1920,
    layers: lista,
  );
}

/// Roda [quadros] quadros e devolve o tempo medio e o pior deles — o
/// pior importa mais que a media: e ele que se sente como engasgo.
Future<({double medio, double pior})> _medir(
  WidgetTester tester,
  Future<void> Function(int i) passo, {
  required int quadros,
}) async {
  var pior = 0.0;
  var total = 0.0;
  for (var i = 0; i < quadros; i++) {
    final relogio = Stopwatch()..start();
    // Cada passo constroi, mede e grava um quadro; o relogio para
    // quando ele termina de verdade.
    await passo(i);
    relogio.stop();
    final ms = relogio.elapsedMicroseconds / 1000.0;
    total += ms;
    if (ms > pior) pior = ms;
  }
  return (medio: total / quadros, pior: pior);
}

/// O palco sozinho: a composicao desenhando o projeto, sem timeline,
/// sem barra de ferramentas, sem transporte.
Future<({double medio, double pior})> _medirSoOPalco(
  WidgetTester tester, {
  required int camadas,
  required int quadros,
}) async {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container
      .read(editorControllerProvider.notifier)
      .openProject(_projeto(camadas: camadas));
  final relogioDaCena = ValueNotifier(Duration.zero);
  addTearDown(relogioDaCena.dispose);
  final videos = VideoLayerManager();
  addTearDown(videos.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Center(
          child: SizedBox(
            width: 360,
            height: 640,
            child: CompositionView(
              time: relogioDaCena,
              videos: videos,
              selectedId: null,
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  return _medir(
    tester,
    (i) async {
      relogioDaCena.value = Duration(milliseconds: 200 + i * 33);
      await tester.pump(const Duration(milliseconds: 16));
    },
    quadros: quadros,
  );
}

/// A LINHA DO TEMPO sozinha, com o relogio andando: as barras, a regua,
/// os losangos e o cabecote.
Future<({double medio, double pior})> _medirSoATimeline(
  WidgetTester tester, {
  required int camadas,
  required int quadros,
  /// Quanto o relogio anda por quadro. Com um passo pequeno demais para
  /// mover a rolagem, a linha do tempo NAO rola — e ai da para ver
  /// quanto custa a rolagem que segue o cabecote.
  int passoMs = 33,
}) async {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container
      .read(editorControllerProvider.notifier)
      .openProject(_projeto(camadas: camadas));
  late PlaybackController playback;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Scaffold(
          body: _CascaDaTimeline(
            aoCriar: (p) => playback = p,
            camadas: camadas,
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  return _medir(
    tester,
    (i) async {
      playback.seek(Duration(milliseconds: 200 + i * passoMs));
      await tester.pump(const Duration(milliseconds: 16));
    },
    quadros: quadros,
  );
}

/// Um hospedeiro com Ticker, que e o que a linha do tempo pede.
class _CascaDaTimeline extends StatefulWidget {
  const _CascaDaTimeline({required this.aoCriar, required this.camadas});

  final void Function(PlaybackController) aoCriar;
  final int camadas;

  @override
  State<_CascaDaTimeline> createState() => _CascaDaTimelineState();
}

class _CascaDaTimelineState extends State<_CascaDaTimeline>
    with TickerProviderStateMixin {
  late final PlaybackController _playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 10),
  );

  @override
  void initState() {
    super.initState();
    widget.aoCriar(_playback);
  }

  @override
  void dispose() {
    _playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => SizedBox(
    height: 300,
    child: TimelineDoEditor(playback: _playback),
  );
}

void main() {
  setUpAll(() async {
    for (final f in ['Aurea Motion Sans', 'Roboto', 'FlutterTest']) {
      await (FontLoader(f)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('bancada: o custo de um quadro do editor', (tester) async {
    final linhas = <String>[
      '',
      'BANCADA DO EDITOR — custo de CPU por quadro (construir + medir + pintar)',
      '',
      'camadas   parado          tocando         arrastando',
      '-----------------------------------------------------------',
    ];

    for (final n in [4, 12, 24]) {
      final c = await openEditor(tester);
      c.read(editorControllerProvider.notifier).openProject(
        _projeto(camadas: n),
      );
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 100));

      final playback = tester
          .widget<PreviewStage>(find.byType(PreviewStage))
          .playback;

      // 1. PARADO: a tela nao muda, mas o app continua reconstruindo.
      final parado = await _medir(
        tester,
        (i) => tester.pump(const Duration(milliseconds: 16)),
        quadros: 20,
      );

      // 2. TOCANDO: o relogio anda, e cada camada e reavaliada.
      final tocando = await _medir(
        tester,
        (i) async {
          playback.seek(Duration(milliseconds: 200 + i * 33));
          await tester.pump(const Duration(milliseconds: 16));
        },
        quadros: 20,
      );

      // 3. ARRASTANDO uma camada no palco: o gesto tem de responder.
      final palco = tester.getRect(find.byType(PreviewStage));
      await tester.tapAt(palco.center);
      await tester.pump(const Duration(milliseconds: 200));
      final arrastando = await _medir(
        tester,
        (i) => tester.dragFrom(palco.center, const Offset(6, 0)),
        quadros: 12,
      );

      linhas.add(
        '${n.toString().padLeft(5)}   '
        '${parado.medio.toStringAsFixed(1).padLeft(5)}/${parado.pior.toStringAsFixed(0).padLeft(3)} ms  '
        '${tocando.medio.toStringAsFixed(1).padLeft(6)}/${tocando.pior.toStringAsFixed(0).padLeft(3)} ms  '
        '${arrastando.medio.toStringAsFixed(1).padLeft(6)}/${arrastando.pior.toStringAsFixed(0).padLeft(3)} ms',
      );
      await tester.pumpWidget(const SizedBox());
      await tester.pump(const Duration(milliseconds: 50));
    }

    linhas
      ..add('')
      ..add('media/pior — 16.6 ms = 60 fps, 33.3 ms = 30 fps');
    // ignore: avoid_print
    print(linhas.join(String.fromCharCode(10)));
  });

  testWidgets('bancada: a linha do tempo sozinha', (tester) async {
    final linhas = <String>[
      '',
      'SO A LINHA DO TEMPO — quadro com o relogio andando',
      '',
      'camadas   timeline',
      '-----------------------',
    ];
    for (final n in [4, 12, 24]) {
      Perfil3D.zerar();
      Perfil3D.ligado = true;
      final m = await _medirSoATimeline(tester, camadas: n, quadros: 20);
      Perfil3D.ligado = false;
      final r = Perfil3D.relatorio();
      linhas.add(
        '${n.toString().padLeft(5)}   '
        '${m.medio.toStringAsFixed(1).padLeft(5)}/${m.pior.toStringAsFixed(0).padLeft(3)} ms',
      );
      for (final fase in ['build.timeline', 'pintar.regua', 'pintar.batidas',
        'pintar.barra.fundo', 'pintar.barra.frente']) {
        final f = r.fases[fase];
        if (f == null) continue;
        linhas.add(
          '          ${fase.padRight(20)} '
          '${(f.ms / 20).toStringAsFixed(2).padLeft(6)} ms/quadro | '
          '${f.chamadas} vezes, '
          '${(f.ms / (f.chamadas == 0 ? 1 : f.chamadas)).toStringAsFixed(1)} ms cada',
        );
      }
      await tester.pumpWidget(const SizedBox());
      await tester.pump(const Duration(milliseconds: 50));
    }

    // A PROVA: o mesmo relogio andando, mas devagar demais para a linha
    // do tempo rolar atras do cabecote.
    final semRolar = await _medirSoATimeline(
      tester,
      camadas: 12,
      quadros: 20,
      passoMs: 0,
    );
    linhas
      ..add('')
      ..add(
        '   12   sem rolar: '
        '${semRolar.medio.toStringAsFixed(1)}/${semRolar.pior.toStringAsFixed(0)} ms',
      );
    await tester.pumpWidget(const SizedBox());
    await tester.pump(const Duration(milliseconds: 50));
    // ignore: avoid_print
    print(linhas.join(String.fromCharCode(10)));
  });

  testWidgets('bancada: o palco sozinho, sem a casca do editor', (
    tester,
  ) async {
    final linhas = <String>[
      '',
      'SO O PALCO (composicao) — quadro com o relogio andando',
      '',
      'camadas   palco',
      '-----------------------',
    ];
    for (final n in [4, 12, 24]) {
      final m = await _medirSoOPalco(tester, camadas: n, quadros: 20);
      linhas.add(
        '${n.toString().padLeft(5)}   '
        '${m.medio.toStringAsFixed(1).padLeft(5)}/${m.pior.toStringAsFixed(0).padLeft(3)} ms',
      );
      await tester.pumpWidget(const SizedBox());
      await tester.pump(const Duration(milliseconds: 50));
    }
    // ignore: avoid_print
    print(linhas.join(String.fromCharCode(10)));
  });
}
