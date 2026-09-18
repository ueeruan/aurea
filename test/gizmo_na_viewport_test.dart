// O GIZMO 3D NO PALCO — desenho e gesto, de ponta a ponta.
//
// A CONTA DESTE TESTE NAO E "o widget existe": e a mesma de quem usa o
// app. Onde o gizmo esta na tela sai da MESMA funcao que o palco usa
// ([gizmoDaCamada]) convertida pelo retangulo real do quadro; o dedo anda
// a partir dali; e o que se confere depois e a PROPRIEDADE da camada. Se o
// desenho e o gesto discordassem de um pixel, o arrasto pegaria o eixo
// errado e este teste cairia.
import 'dart:math' as math;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/gizmo3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

const double _cw = 1920;
const double _ch = 1080;

/// O PROJETO DO TESTE: uma camada 3D no centro e, quando pedida, uma
/// camera ja girada — sem girar a camera o eixo Z nao tem comprimento na
/// tela, e nao haveria o que arrastar.
VideoProject _projeto({
  bool com3D = true,
  double cameraRotY = 0,
  bool bloqueada = false,
}) {
  final camada = ImageLayer(
    name: 'Foto 3D',
    startTime: Duration.zero,
    duration: const Duration(seconds: 5),
    sourcePath: 'a.png',
    is3D: com3D,
    position: AnimatedOffset(const Offset(_cw / 2, _ch / 2)),
    scaleX: AnimatedDouble(1),
    scaleY: AnimatedDouble(1),
  );
  final camadas = <Layer>[camada];
  if (cameraRotY != 0) {
    camadas.add(
      CameraLayer(
        name: 'Camera',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        is3D: true,
        position: AnimatedOffset(const Offset(_cw / 2, _ch / 2)),
        rotationY: AnimatedDouble(cameraRotY),
      ),
    );
  }
  return VideoProject(
    name: 'gizmo no palco',
    createdAt: DateTime(2026),
    resolutionHeight: 1080,
    aspectRatio: 16 / 9,
    layers: camadas,
    meta: bloqueada
        ? {camada.id: const LayerMeta(locked: true)}
        : const <String, LayerMeta>{},
  );
}

/// A GEOMETRIA DO PALCO, lida do widget de verdade: onde o quadro da
/// composicao caiu e por quanto ele esta escalado.
({Offset topLeft, double escala}) _palco(WidgetTester tester) {
  final r = tester.getRect(find.byKey(const ValueKey('composition-frame')));
  return (topLeft: r.topLeft, escala: r.width / _cw);
}

/// O PONTO DA COMPOSICAO [comp] EM COORDENADAS GLOBAIS. O canto do quadro
/// da composicao E o ponto (0,0) da composicao — e por isso que a conta e
/// so multiplicar pelo fator.
Offset _naTela(WidgetTester tester, Offset comp) {
  final p = _palco(tester);
  return p.topLeft + comp * p.escala;
}

Layer _camada(WidgetTester tester, ProviderContainer c) =>
    (c.read(editorControllerProvider)).layers.first;

Offset _posDe(WidgetTester tester, ProviderContainer c) =>
    _camada(tester, c).position.valueAt(Duration.zero);

/// O PONTO DE PARTIDA DO DEDO: NO MEIO DO BRACO, e nao na ponta.
///
/// O reconhecedor de gestos do Flutter so entrega o arrasto DEPOIS de o
/// dedo andar o "slop" (~18 px), e o ponto que chega ao palco ja e o de
/// entao. Comecar na ponta faria esse primeiro passo cair fora do braco,
/// o toque nao pegaria o eixo, e o teste mediria a arena de gestos em vez
/// do gizmo.
Offset _pegaNoMeio(WidgetTester tester, GizmoNaTela g, EixoDoGizmo e) {
  final p = _palco(tester);
  final braco = 86 / p.escala;
  return _naTela(tester, g.origem + g.direcao(e) * (braco * 0.45));
}

/// ANDA [delta] PIXELS DE TELA e devolve o que MUDOU na propriedade, ja
/// descontado o passo que apenas venceu a arena de gestos.
Future<double> _andar(
  WidgetTester tester,
  ProviderContainer c,
  TestGesture gesto,
  Offset delta,
  double Function() ler,
) async {
  // O PRIMEIRO MOVIMENTO E O QUE VENCE A ARENA — ele nao vira arrasto.
  await gesto.moveBy(delta);
  await tester.pump();
  final antes = ler();
  await gesto.moveBy(delta);
  await tester.pump();
  return ler() - antes;
}

void main() {
  testWidgets('o gizmo aparece numa camada 3D e some numa camada plana',
      (tester) async {
    final c = await openEditor(tester);
    c.read(editorControllerProvider.notifier).openProject(_projeto());
    c.read(selectedLayerProvider.notifier).state =
        c.read(editorControllerProvider).layers.first.id;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('gizmo-3d')), findsOneWidget);

    // A MESMA CAMADA com o 3D DESLIGADO: sem gizmo. Um gizmo de
    // profundidade numa camada plana prometeria um eixo que nao existe.
    c.read(editorControllerProvider.notifier).openProject(
      _projeto(com3D: false),
    );
    c.read(selectedLayerProvider.notifier).state =
        c.read(editorControllerProvider).layers.first.id;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('gizmo-3d')), findsNothing);
  });

  testWidgets('arrastar o eixo X move a posicao e NAO a profundidade',
      (tester) async {
    final c = await openEditor(tester);
    c.read(editorControllerProvider.notifier).openProject(_projeto());
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final projeto = c.read(editorControllerProvider);
    final g = gizmoDaCamada(projeto, _camada(tester, c), Duration.zero)!;
    // A PONTA DO BRACO DO X, na tela, com o comprimento que o palco usa.
    final gesto = await tester.startGesture(
      _pegaNoMeio(tester, g, EixoDoGizmo.x),
    );
    await tester.pump();
    // 30 px DE TELA no sentido do eixo. A posicao e medida em pixels da
    // COMPOSICAO, entao o que se espera e 30 dividido pelo fator do palco
    // — e nao 30. E a prova de que a sensibilidade e 1:1 com o dedo, que
    // e o que faz o objeto nao ficar para tras do arrasto.
    final passo = 30 / _palco(tester).escala;
    final andou = await _andar(
      tester, c, gesto, const Offset(30, 0),
      () => _posDe(tester, c).dx,
    );
    await gesto.up();
    await tester.pumpAndSettle();

    expect(andou, closeTo(passo, 1.5));
    final l = _camada(tester, c);
    expect(l.position.valueAt(Duration.zero).dy, closeTo(540, 1e-6));
    expect(l.positionZ.valueAt(Duration.zero), 0);
    expect(l.is3D, isTrue);
  });

  testWidgets('arrastar o eixo Z muda a PROFUNDIDADE e mais nada',
      (tester) async {
    final c = await openEditor(tester);
    c.read(editorControllerProvider.notifier).openProject(
      _projeto(cameraRotY: 90),
    );
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final projeto = c.read(editorControllerProvider);
    final g = gizmoDaCamada(projeto, _camada(tester, c), Duration.zero)!;
    expect(eixoVisivel(g, EixoDoGizmo.z), isTrue,
        reason: 'com a camera a 90 graus o Z tem de aparecer');
    // O Z mede 1 px de composicao por unidade e aponta ao longo do x da
    // tela. O dedo anda 30 px: a profundidade tem de andar 30, e a POSICAO
    // nao pode andar nada — um Z que mexesse na posicao seria 2.5D
    // disfarcado, que e exatamente o que o pedido recusa.
    final passo = Offset(30 * g.z.dx.sign, 0);
    final esperado = 30 / _palco(tester).escala;
    final gesto = await tester.startGesture(
      _pegaNoMeio(tester, g, EixoDoGizmo.z),
    );
    await tester.pump();
    final andou = await _andar(
      tester, c, gesto, passo,
      () => _camada(tester, c).positionZ.valueAt(Duration.zero),
    );
    await gesto.up();
    await tester.pumpAndSettle();

    expect(andou, closeTo(esperado, 1.5));
    final l = _camada(tester, c);
    expect(l.position.valueAt(Duration.zero).dx, closeTo(_cw / 2, 1e-6));
    expect(l.position.valueAt(Duration.zero).dy, closeTo(_ch / 2, 1e-6));
  });

  testWidgets('girar o anel do Z muda a rotacao da camada', (tester) async {
    final c = await openEditor(tester);
    c.read(editorControllerProvider.notifier).openProject(_projeto());
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final projeto = c.read(editorControllerProvider);
    final g = gizmoDaCamada(projeto, _camada(tester, c), Duration.zero)!;
    final p = _palco(tester);
    final raio = 118 / p.escala;
    final centro = g.origem;
    // O ANEL DO Z vive no plano X/Y: com a camada de frente ele e um
    // circulo de raio `raio` na tela. O dedo percorre um QUARTO DE VOLTA
    // por cima dele — de cima para a direita —, em passos que dao o
    // angulo exato de cada evento.
    Offset noAnel(double graus) => _naTela(
      tester,
      centro +
          Offset(
            raio * math.cos(graus * math.pi / 180),
            raio * math.sin(graus * math.pi / 180),
          ),
    );

    final gesto = await tester.startGesture(noAnel(90));
    await tester.pump();
    // O PRIMEIRO PASSO VENCE A ARENA DE GESTOS e nao vira rotacao.
    await gesto.moveTo(noAnel(82.5));
    await tester.pump();
    final antes = _camada(tester, c).rotation.valueAt(Duration.zero);

    // OS 11 PASSOS QUE CONTAM: 82,5 graus de arco, na ordem 90 -> 0.
    for (var i = 2; i <= 12; i++) {
      await gesto.moveTo(noAnel(90 - i * 7.5));
      await tester.pump();
    }
    await gesto.up();
    await tester.pumpAndSettle();

    final depois = _camada(tester, c).rotation.valueAt(Duration.zero);
    // O ARCO ANDADO NA TELA VIRA O GIRO DA CAMADA, com o sinal do eixo:
    // descendo pela direita do anel o angulo de tela cai, e o sinal do Z
    // e +1 — a rotacao tem de cair os mesmos 82,5 graus.
    expect(depois - antes, closeTo(-82.5, 4));
    expect(_posDe(tester, c).dx, closeTo(_cw / 2, 1e-6));
    expect(_camada(tester, c).positionZ.valueAt(Duration.zero), 0);
  });

  testWidgets('camada bloqueada: o gizmo existe mas nao arrasta nada',
      (tester) async {
    final c = await openEditor(tester);
    c.read(editorControllerProvider.notifier).openProject(
      _projeto(bloqueada: true),
    );
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final projeto = c.read(editorControllerProvider);
    final g = gizmoDaCamada(projeto, _camada(tester, c), Duration.zero)!;
    final gesto = await tester.startGesture(
      _pegaNoMeio(tester, g, EixoDoGizmo.x),
    );
    await tester.pump();
    await gesto.moveBy(const Offset(50, 0));
    await tester.pump();
    await gesto.up();
    await tester.pumpAndSettle();

    final l = _camada(tester, c);
    expect(
      l.position.valueAt(Duration.zero).dx,
      closeTo(_cw / 2, 1e-6),
      reason: 'o cadeado tem de fechar a porta do gizmo tambem',
    );
  });
}
