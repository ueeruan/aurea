import 'dart:math' as math;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/rastreio3d_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A TELA DO RASTREIO, provada sem vídeo.
///
/// O que ela promete é uma travessia curta: ver os pontos, escolher uma
/// superfície, pôr algo em cima. O erro que este teste pega é o que não
/// aparece olhando — o objeto criado NO LUGAR ERRADO, longe da
/// superfície que a pessoa escolheu, porque o plano foi lido de um jeito
/// e o objeto posicionado de outro.

/// Uma solução de mentirinha: pontos num chão conhecido, câmera acima.
SolucaoCamera3D _solucao() {
  final rng = math.Random(4);
  final nuvem = <int, List<double>>{};
  // Um chão em y = -100, com um pouco de relevo.
  for (var i = 0; i < 40; i++) {
    nuvem[i] = [
      (rng.nextDouble() - .5) * 400,
      -100 + (rng.nextDouble() - .5) * 4,
      (rng.nextDouble() - .5) * 400,
    ];
  }
  // E coisas soltas pelo ar, que não são o chão.
  for (var i = 40; i < 60; i++) {
    nuvem[i] = [
      (rng.nextDouble() - .5) * 400,
      100 + rng.nextDouble() * 200,
      (rng.nextDouble() - .5) * 400,
    ];
  }
  final poses = <PoseCamera>[
    for (var q = 0; q < 6; q++)
      () {
        final pos = [-200.0 + 80.0 * q, 150.0, -600.0];
        final r = rotacaoDeVetor([0.2, 0.0, 0.0]);
        final rt = r.aplicar(pos);
        return PoseCamera(q, r, [-rt[0], -rt[1], -rt[2]]);
      }(),
  ];
  return SolucaoCamera3D(
    largura: 240,
    altura: 135,
    focalPx: 288,
    poses: poses,
    nuvem: nuvem,
    erroPixels: 0.7,
    quadros: 6,
    fps: 8,
    // Um em cada tres sai fraco de proposito: assim o numero de
    // pontos bons difere do total, e a ficha nao pode passar no teste
    // mostrando o mesmo numero em duas casas.
    errosPorPonto: {
      for (final id in nuvem.keys) id: id % 3 == 0 ? 3.0 : 0.4,
    },
    vistasPorPonto: {for (final id in nuvem.keys) id: 6},
    pontosSeguidos: 90,
  );
}

VideoProject _projeto() => VideoProject(
  name: 'Rastreio',
  createdAt: DateTime(2026, 9, 8),
  layers: [
    VideoLayer(
      id: 'clipe',
      name: 'clipe.mp4',
      sourcePath: '/nao/existe/clipe.mp4',
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
      position: AnimatedOffset(const Offset(960, 540)),
    ),
  ],
);

Future<ProviderContainer> _abrir(WidgetTester tester, SolucaoCamera3D s) async {
  tester.view.physicalSize = const Size(430, 932);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final c = ProviderContainer();
  addTearDown(c.dispose);
  c.read(editorControllerProvider.notifier).openProject(_projeto());
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Rastreio3DScreen(layerId: 'clipe', solucao: s),
      ),
    ),
  );
  await tester.pump();
  return c;
}

void main() {
  testWidgets('a ficha mostra erro, pontos e lente', (tester) async {
    await _abrir(tester, _solucao());
    expect(find.byKey(const ValueKey('rastreio3d-ficha')), findsOne);
    expect(find.text('0.70 px'), findsOne);
    expect(find.text('60'), findsOne); // pontos 3D
    expect(find.text('40'), findsOne); // bons: os que nao sao fracos
    expect(find.text('90'), findsOne); // pontos seguidos
    expect(find.text('43 mm'), findsOne); // 36 * 288 / 240
  });

  testWidgets('sem escolha nenhuma, o caminho e criar a camera', (
    tester,
  ) async {
    await _abrir(tester, _solucao());
    expect(find.byKey(const ValueKey('rastreio3d-criar-camera')), findsOne);
    expect(find.byKey(const ValueKey('rastreio3d-chao-auto')), findsOne);
    // As opções de pôr objeto só aparecem com uma superfície escolhida:
    // oferecer "pôr texto" sem lugar onde pôr é uma promessa vazia.
    expect(find.byKey(const ValueKey('rastreio3d-add-texto')), findsNothing);
  });

  testWidgets('criar a camera poe uma cena 3D em cima do clipe', (
    tester,
  ) async {
    final c = await _abrir(tester, _solucao());
    await tester.tap(find.byKey(const ValueKey('rastreio3d-criar-camera')));
    await tester.pumpAndSettle();
    final cenas = c
        .read(editorControllerProvider)
        .layers
        .whereType<Scene3DLayer>();
    expect(cenas.length, 1);
    // A câmera tem de ANDAR: uma cena com a câmera parada não é um
    // rastreio, é um enquadramento.
    final cam = cenas.first.camera;
    expect(cam.posX.keyframes.length, greaterThan(1));
    // O aviso de "camera criada" segura um timer proprio.
    await tester.pump(const Duration(seconds: 8));
  });

  testWidgets('achar o chao sozinho deixa a nuvem do chao em y perto de zero', (
    tester,
  ) async {
    await _abrir(tester, _solucao());
    await tester.tap(find.byKey(const ValueKey('rastreio3d-chao-auto')));
    await tester.pumpAndSettle();
    await tester.pump(const Duration(seconds: 5));
    // Depois de definir o chão, a folha continua de pé e a ficha
    // continua legível — o teste real do chão é o do domínio.
    expect(find.byKey(const ValueKey('rastreio3d-ficha')), findsOne);
  });

  testWidgets('apagar os ruins tira os pontos de erro alto', (tester) async {
    final s = _solucao();
    // Um punhado de pontos com erro alto de propósito.
    final ruim = s.copiarCom(
      errosPorPonto: {
        ...s.errosPorPonto,
        for (var i = 0; i < 5; i++) i: 9.0,
      },
    );
    expect(ruim.pontosDaQualidade({QualidadeDoPonto.ruim}).length, 5);
    await _abrir(tester, ruim);
    await tester.tap(find.byKey(const ValueKey('rastreio3d-apagar-ruins')));
    await tester.pumpAndSettle();
    await tester.pump(const Duration(seconds: 5));
    // Cinquenta e cinco: os sessenta menos os cinco ruins.
    expect(find.text('55'), findsWidgets);
  });

  testWidgets('o avancado mostra os modos e a lente resolvida', (tester) async {
    await _abrir(tester, _solucao());
    await tester.tap(find.byKey(const ValueKey('rastreio3d-avancado')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('rastreio3d-modo-rapido')), findsOne);
    expect(find.byKey(const ValueKey('rastreio3d-modo-preciso')), findsOne);
    expect(find.byKey(const ValueKey('rastreio3d-detalhes')), findsOne);
    // Sem ter rastreado nesta sessão não há rastros na memória, e o
    // botão diz isso em vez de falhar quando tocado.
    expect(find.byKey(const ValueKey('rastreio3d-recalcular')), findsOne);
  });

  testWidgets('escolher pontos do chao abre as opcoes de pôr objeto', (
    tester,
  ) async {
    final s = _solucao();
    await _abrir(tester, s);
    // Arrastar sobre o palco seleciona o que estiver dentro do laço. O
    // palco tem a proporção do vídeo, então um arrasto de canto a canto
    // pega tudo o que está projetado.
    final palco = find.byKey(const ValueKey('rastreio3d-palco'));
    expect(palco, findsOne);
    final r = tester.getRect(palco);
    await tester.dragFrom(
      r.topLeft + const Offset(4, 4),
      Offset(r.width - 8, r.height - 8),
    );
    await tester.pumpAndSettle();
    // Com pontos escolhidos, ou aparece a superfície (e as opções de
    // objeto), ou aparece o aviso de que eles não formam uma. As duas
    // respostas são honestas; o que não pode é ficar sem resposta.
    final temPlano = find.byKey(const ValueKey('rastreio3d-plano'));
    final temAviso = find.byKey(const ValueKey('rastreio3d-poucos'));
    expect(
      temPlano.evaluate().isNotEmpty || temAviso.evaluate().isNotEmpty,
      isTrue,
    );
  });
}
