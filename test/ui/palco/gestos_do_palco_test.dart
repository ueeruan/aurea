// OS GESTOS DO PALCO DE PONTA A PONTA, no PreviewStage de verdade.
//
// O pedido do dono, ao pe da letra: "tento pincar -> objeto move; tento
// selecionar -> playhead pula" nao pode mais acontecer. Cada teste mede o
// que a pessoa ve depois do gesto: QUAL camada ficou escolhida, ONDE ela
// esta, de que TAMANHO, quanto vale o ZOOM da vista e em que INSTANTE o
// relogio parou.
import 'dart:math' as math;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/gizmo3d.dart';
import 'package:aurea/src/features/editor/domain/gizmo_da_cena3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/shell/cromo_editor.dart'
    show zoomDoPalcoProvider;
import 'package:aurea/src/features/editor/presentation/ui/palco/alcas_do_palco.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gizmo_da_cena_overlay.dart'
    show noAtivoDaCena;
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// O PALCO MONTADO SOZINHO (400 x 700), com um relogio de verdade.
class _Palco {
  _Palco(this.c, this.playback);

  final ProviderContainer c;
  final PlaybackController playback;

  EditorController get e => c.read(editorControllerProvider.notifier);
  VideoProject get projeto => c.read(editorControllerProvider);
  String? get escolhida => c.read(selectedLayerProvider);
  Layer camada(String id) => projeto.layerById(id)!;
  Offset pos(String id) => camada(id).position.valueAt(Duration.zero);
  double escala(String id) => camada(id).scaleX.valueAt(Duration.zero);
  double get zoom => c.read(zoomDoPalcoProvider);
}

Future<_Palco> _montar(
  WidgetTester tester,
  void Function(EditorController e, ProviderContainer c) preparar,
) async {
  final c = ProviderContainer();
  addTearDown(c.dispose);
  final videos = VideoLayerManager();
  addTearDown(videos.dispose);
  final e = c.read(editorControllerProvider.notifier);
  e.openProject(VideoProject.empty('palco'));
  preparar(e, c);
  // O PROJETO REABERTO: desfazer vazio e nada escolhido — o que se conta
  // depois e so o que o gesto fez.
  e.openProject(c.read(editorControllerProvider));
  late PlaybackController playback;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: _Host(
          builder: (p) {
            playback = p;
            return PreviewStage(playback: p, videos: videos);
          },
        ),
      ),
    ),
  );
  await tester.pump();
  return _Palco(c, playback);
}

/// O ponto [comp] da COMPOSICAO na tela, pelo retangulo real do quadro.
Offset _naTela(WidgetTester tester, _Palco p, Offset comp) {
  final r = tester.getRect(find.byKey(const ValueKey('composition-frame')));
  return r.topLeft + comp * (r.width / p.projeto.outputWidth);
}

/// DUAS FORMAS: "Fundo" a esquerda do centro e "Topo" um pouco a direita,
/// sobrepostas no meio. A de cima e a ultima criada.
void _duasFormas(EditorController e, ProviderContainer _) {
  e.addShapeLayer(Duration.zero, name: 'Fundo');
  e.addShapeLayer(Duration.zero, name: 'Topo');
}

({String topo, String fundo}) _ids(_Palco p) {
  final ls = p.projeto.layers;
  return (
    topo: ls.firstWhere((l) => l.name.startsWith('Topo')).id,
    fundo: ls.firstWhere((l) => l.name.startsWith('Fundo')).id,
  );
}

void main() {
  testWidgets('tocar no objeto escolhe a camada de CIMA; no vazio tira', (
    tester,
  ) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    final centro = Offset(
      p.projeto.outputWidth / 2,
      p.projeto.outputHeight / 2,
    );
    // Afasta o fundo para a esquerda: no meio as duas se sobrepoem, e a
    // esquerda so o fundo aparece.
    final largura = p.e.layerBoxSize(p.camada(ids.fundo), Duration.zero).width;
    p.e.editPosition(
      ids.fundo,
      Duration.zero,
      centro - Offset(largura * .8, 0),
    );
    await tester.pump();

    await tester.tapAt(_naTela(tester, p, centro));
    await tester.pump();
    expect(p.escolhida, ids.topo, reason: 'onde as duas estao, a de cima');

    final soOFundo = centro - Offset(largura * 1.2, 0);
    await tester.tapAt(_naTela(tester, p, soOFundo));
    await tester.pump();
    expect(p.escolhida, ids.fundo, reason: 'onde so o fundo aparece');

    await tester.tapAt(_naTela(tester, p, const Offset(20, 20)));
    await tester.pump(const Duration(milliseconds: 400));
    expect(p.escolhida, isNull, reason: 'o vazio tira a selecao');
  });

  testWidgets('camada invisivel no instante nao pega o toque', (tester) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    // O topo transparente: quem se ve no meio e o fundo.
    p.e.editOpacity(ids.topo, Duration.zero, 0);
    await tester.pump();
    final centro = Offset(
      p.projeto.outputWidth / 2,
      p.projeto.outputHeight / 2,
    );
    await tester.tapAt(_naTela(tester, p, centro));
    await tester.pump();
    expect(p.escolhida, ids.fundo);
  });

  testWidgets('o toque de escolher nao mexe no relogio', (tester) async {
    final p = await _montar(tester, _duasFormas);
    p.playback.seek(const Duration(milliseconds: 1500));
    await tester.pump();
    final centro = Offset(
      p.projeto.outputWidth / 2,
      p.projeto.outputHeight / 2,
    );

    await tester.tapAt(_naTela(tester, p, centro));
    await tester.pump();
    expect(p.escolhida, isNotNull);
    expect(p.playback.time.value, const Duration(milliseconds: 1500));
    expect(p.playback.playing.value, isFalse);

    await tester.tapAt(_naTela(tester, p, const Offset(20, 20)));
    await tester.pump(const Duration(milliseconds: 400));
    expect(p.escolhida, isNull);
    expect(p.playback.time.value, const Duration(milliseconds: 1500));
  });

  testWidgets('tremer o dedo ao escolher nao move o objeto', (tester) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    final antes = p.pos(ids.topo);
    final centro = Offset(
      p.projeto.outputWidth / 2,
      p.projeto.outputHeight / 2,
    );
    final g = await tester.startGesture(_naTela(tester, p, centro));
    await tester.pump();
    await g.moveBy(const Offset(6, 4));
    await tester.pump();
    await g.moveBy(const Offset(-3, 5));
    await tester.pump();
    await g.up();
    await tester.pump();
    expect(p.escolhida, ids.topo);
    expect(p.pos(ids.topo), antes, reason: 'escolher nao e arrastar');
    expect(p.e.canUndo, isFalse, reason: 'nenhum passo de desfazer vazio');
  });

  testWidgets('arrastar move a camada e vira UM passo de desfazer', (
    tester,
  ) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    final antes = p.pos(ids.topo);
    final centro = Offset(
      p.projeto.outputWidth / 2,
      p.projeto.outputHeight / 2,
    );
    final g = await tester.startGesture(_naTela(tester, p, centro));
    await tester.pump();
    // PAUSAS DE VERDADE entre os passos, maiores que a janela de 450 ms
    // que juntava edicoes: sem o gesto aberto, cada pausa viraria um passo.
    for (var i = 0; i < 3; i++) {
      await g.moveBy(const Offset(40, 25));
      await tester.pump();
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 500)),
      );
    }
    await g.up();
    await tester.pump();

    expect(p.escolhida, ids.topo, reason: 'arrastar escolheu no comeco');
    final depois = p.pos(ids.topo);
    expect(depois.dx, greaterThan(antes.dx + 100));
    expect(depois.dy, greaterThan(antes.dy + 60));

    p.e.undo();
    await tester.pump();
    expect(p.pos(ids.topo), antes, reason: 'um desfazer volta o arrasto todo');
    expect(p.e.canUndo, isFalse, reason: 'e so havia um passo');
  });

  testWidgets('arrastar nunca troca de camada no meio do caminho', (
    tester,
  ) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    final centro = Offset(
      p.projeto.outputWidth / 2,
      p.projeto.outputHeight / 2,
    );
    final largura = p.e.layerBoxSize(p.camada(ids.fundo), Duration.zero).width;
    // Fundo bem a esquerda, sem sobrepor.
    p.e.editPosition(
      ids.fundo,
      Duration.zero,
      centro - Offset(largura * 1.5, 0),
    );
    await tester.pump();
    final fundoAntes = p.pos(ids.fundo);
    // Arrasta o TOPO por cima do fundo e alem dele.
    final g = await tester.startGesture(_naTela(tester, p, centro));
    await tester.pump();
    for (var i = 0; i < 6; i++) {
      await g.moveBy(const Offset(-30, 0));
      await tester.pump();
    }
    expect(p.escolhida, ids.topo);
    await g.up();
    await tester.pump();
    expect(p.escolhida, ids.topo);
    expect(p.pos(ids.fundo), fundoAntes, reason: 'o fundo nao foi tocado');
  });

  testWidgets('pinca no objeto escolhido escala e NAO move', (tester) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    p.c.read(selectedLayerProvider.notifier).state = ids.topo;
    await tester.pump();
    final posAntes = p.pos(ids.topo);
    final escalaAntes = p.escala(ids.topo);
    final meio = _naTela(
      tester,
      p,
      Offset(p.projeto.outputWidth / 2, p.projeto.outputHeight / 2),
    );

    final a = await tester.startGesture(meio - const Offset(12, 0));
    await tester.pump();
    // O primeiro dedo treme antes do segundo chegar: nao pode arrastar.
    await a.moveBy(const Offset(5, 3));
    await tester.pump();
    final b = await tester.startGesture(meio + const Offset(12, 0));
    await tester.pump();
    // Abrem E andam juntos para baixo: o meio dos dedos anda 30 px.
    for (var i = 0; i < 4; i++) {
      await a.moveBy(const Offset(-8, 8));
      await b.moveBy(const Offset(8, 8));
      await tester.pump();
    }
    await a.up();
    await tester.pump();
    // O dedo que sobrou continua andando: nada pode mover.
    await b.moveBy(const Offset(40, 40));
    await tester.pump();
    await b.up();
    await tester.pump();

    expect(p.escala(ids.topo), greaterThan(escalaAntes * 1.5));
    expect(p.pos(ids.topo), posAntes, reason: 'a pinca nunca move a camada');
    expect(p.escolhida, ids.topo, reason: 'nem troca a escolhida');
    expect(p.zoom, 1.0, reason: 'a pinca era da camada, e nao da vista');
    // Um gesto, um passo.
    p.e.undo();
    await tester.pump();
    expect(p.escala(ids.topo), closeTo(escalaAntes, 1e-9));
  });

  testWidgets('pinca fora do objeto: zoom da VISTA, o projeto nao muda', (
    tester,
  ) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    p.c.read(selectedLayerProvider.notifier).state = ids.topo;
    await tester.pump();
    final projetoAntes = p.projeto;
    final quadro = tester.getRect(
      find.byKey(const ValueKey('composition-frame')),
    );
    // Os dois dedos no canto de cima e a esquerda do quadro: vazio.
    final a = await tester.startGesture(quadro.topLeft + const Offset(10, 20));
    await tester.pump();
    final b = await tester.startGesture(quadro.topLeft + const Offset(50, 30));
    await tester.pump();
    for (var i = 0; i < 4; i++) {
      await a.moveBy(const Offset(-6, -3));
      await b.moveBy(const Offset(10, 4));
      await tester.pump();
    }
    await a.up();
    await b.up();
    await tester.pump();

    expect(p.zoom, greaterThan(1.3), reason: 'a vista aproximou');
    expect(
      identical(p.projeto, projetoAntes),
      isTrue,
      reason: 'zoom da vista nao e edicao do projeto',
    );
    expect(p.escolhida, ids.topo, reason: 'e nao mexe na selecao');
    expect(p.e.canUndo, isFalse);
  });

  testWidgets('sem selecao, pinca em cima do objeto tambem e da vista', (
    tester,
  ) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    final escalaAntes = p.escala(ids.topo);
    final meio = _naTela(
      tester,
      p,
      Offset(p.projeto.outputWidth / 2, p.projeto.outputHeight / 2),
    );
    final a = await tester.startGesture(meio - const Offset(10, 0));
    final b = await tester.startGesture(meio + const Offset(10, 0));
    await tester.pump();
    for (var i = 0; i < 3; i++) {
      await a.moveBy(const Offset(-10, 0));
      await b.moveBy(const Offset(10, 0));
      await tester.pump();
    }
    await a.up();
    await b.up();
    await tester.pump();
    expect(p.zoom, greaterThan(1.3));
    expect(p.escala(ids.topo), escalaAntes);
    expect(p.escolhida, isNull, reason: 'pincar nao escolhe ninguem');
  });

  testWidgets('dois toques no vazio reenquadram a vista', (tester) async {
    final p = await _montar(tester, _duasFormas);
    p.c.read(zoomDoPalcoProvider.notifier).state = 2.0;
    await tester.pump();
    final palco = tester.getRect(find.byType(PreviewStage));
    final canto = palco.topLeft + const Offset(8, 8);
    await tester.tapAt(canto);
    await tester.pump(const Duration(milliseconds: 60));
    await tester.tapAt(canto);
    await tester.pump(const Duration(milliseconds: 400));
    expect(p.zoom, 1.0);
  });

  testWidgets('a alca de giro gira pelo quanto o dedo varreu, sem salto', (
    tester,
  ) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    p.c.read(selectedLayerProvider.notifier).state = ids.topo;
    await tester.pump();
    final posAntes = p.pos(ids.topo);
    final alca = tester.getCenter(find.byKey(const ValueKey('alca-giro')));
    final g = await tester.startGesture(alca);
    await tester.pump();
    // Um passo pequeno: a rotacao acompanha pouco, e nao salta 45 graus.
    await g.moveBy(const Offset(0, 6));
    await tester.pump();
    final pouco = p.camada(ids.topo).rotation.valueAt(Duration.zero);
    expect(pouco.abs(), lessThan(10));
    for (var i = 0; i < 5; i++) {
      await g.moveBy(const Offset(0, 14));
      await tester.pump();
    }
    await g.up();
    await tester.pump();
    final muito = p.camada(ids.topo).rotation.valueAt(Duration.zero);
    expect(muito, greaterThan(pouco + 15), reason: 'descer pela direita gira');
    expect(p.pos(ids.topo), posAntes, reason: 'girar nao move');
  });

  testWidgets('encaixar no centro da um toque leve no dedo', (tester) async {
    final p = await _montar(tester, _duasFormas);
    final ids = _ids(p);
    final hapticos = <Object?>[];
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform,
      (call) async {
        if (call.method == 'HapticFeedback.vibrate') {
          hapticos.add(call.arguments);
        }
        return null;
      },
    );
    addTearDown(
      () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        null,
      ),
    );
    final centro = Offset(
      p.projeto.outputWidth / 2,
      p.projeto.outputHeight / 2,
    );
    final g = await tester.startGesture(_naTela(tester, p, centro));
    await tester.pump();
    await g.moveBy(const Offset(30, 2));
    await tester.pump();
    expect(hapticos, isEmpty, reason: 'passando depressa nao encaixa');
    await g.moveBy(const Offset(4, 0));
    await tester.pump();
    await g.up();
    await tester.pump();
    expect(p.escolhida, ids.topo);
    expect(hapticos, contains('HapticFeedbackType.lightImpact'));
  });

  testWidgets(
    'camada de cena 3D escolhida: o gizmo do objeto recebe o arrasto',
    (tester) async {
      late String cenaId;
      final p = await _montar(tester, (e, c) {
        e.addScene3DLayer(Duration.zero);
        cenaId = c.read(editorControllerProvider).layers.single.id;
        e.addSceneNode(cenaId, Element3DKind.cube);
      });
      p.c.read(selectedLayerProvider.notifier).state = cenaId;
      await tester.pump();
      final cena = p.camada(cenaId) as Scene3DLayer;
      final posAntes = p.pos(cenaId);

      // O GIZMO PELA MESMA CONTA DA SOBREPOSICAO (gizmo_da_cena_overlay).
      const t = Duration.zero;
      final local = cena.localTime(t);
      final cenaViva = cenaComNulosDaComposicao(p.projeto, cena, local, t);
      final camera = orbitarCamera(
        cameraDaCena(p.projeto, cena, local, t) ?? cena.cameraAt(local),
        -cena.rotationX.valueAt(local),
        -cena.rotationY.valueAt(local),
      );
      final noId = noAtivoDaCena(cenaViva, null)!;
      final g = gizmoDoNo(
        p.projeto,
        cena,
        noId,
        t,
        Size(
          p.projeto.outputWidth.toDouble(),
          p.projeto.outputHeight.toDouble(),
        ),
        cena: cenaViva,
        camera: camera,
      )!;
      final quadro = tester.getRect(
        find.byKey(const ValueKey('composition-frame')),
      );
      final fator = quadro.width / p.projeto.outputWidth;
      double xDoNo() => p.e.sceneNodeValueAt(
        (p.camada(cenaId) as Scene3DLayer).scene.nodeById(noId)!,
        PropDoNo.x,
        local,
      );
      final xAntes = xDoNo();

      // No meio do braco X: a ponta sai da mesma conta do gizmo (86 px DE
      // TELA, em pixels de composicao), e o dedo desce na metade do caminho.
      final braco = 86 / fator;
      final ponta = pontaDoEixo(g, EixoDoGizmo.x, braco);
      final inicio =
          quadro.topLeft + (g.origem + (ponta - g.origem) * .45) * fator;
      final dir = ponta - g.origem;
      final passo = dir / math.max(1e-9, dir.distance) * 20;
      final gesto = await tester.startGesture(inicio);
      await tester.pump();
      for (var i = 0; i < 4; i++) {
        await gesto.moveBy(passo);
        await tester.pump();
      }
      await gesto.up();
      await tester.pump();

      expect(
        xDoNo(),
        isNot(closeTo(xAntes, 1e-6)),
        reason: 'o objeto andou em X',
      );
      expect(p.pos(cenaId), posAntes, reason: 'a camada 2D nao foi arrastada');
      expect(p.escolhida, cenaId);
    },
  );

  test('o pintor da selecao nao abre camada, filtro nem sombra', () {
    final geo = GeometriaDaSelecao(
      contorno: const [
        Offset(10, 10),
        Offset(110, 10),
        Offset(110, 60),
        Offset(10, 60),
      ],
      centro: const Offset(60, 35),
      pivo: const Offset(60, 35),
      escala: const Offset(110, 60),
      giro: const Offset(110, 10),
      supEsq: const Offset(10, 10),
      infEsq: const Offset(10, 60),
    );
    final ativa = ValueNotifier<String?>('escala');
    addTearDown(ativa.dispose);
    final espiao = _CanvasEspiao();
    PintorDaSelecao(
      desenho: DesenhoDaSelecao(
        principal: geo,
        outras: [geo.contorno],
        formas: const [(chave: 'sizeX', ponto: Offset(60, 10))],
      ),
      alcaAtiva: ativa,
    ).paint(espiao, const Size(200, 100));
    expect(espiao.camadas, 0, reason: 'nenhum saveLayer');
    expect(espiao.filtros, 0, reason: 'nenhum MaskFilter/ImageFilter');
    expect(espiao.tracos, contains(2.0), reason: 'traco de selecao do DS');
    // As alcas de canto (3) com a escolhida no raio 6, e a da forma no 5.
    expect(espiao.raios.where((r) => r == 6).length, 3);
    expect(espiao.raios, contains(5.0));
  });
}

class _CanvasEspiao implements Canvas {
  int camadas = 0;
  int filtros = 0;
  final tracos = <double>[];
  final raios = <double>[];

  void _paint(Paint p) {
    if (p.maskFilter != null || p.imageFilter != null) filtros++;
  }

  @override
  void saveLayer(Rect? bounds, Paint paint) => camadas++;

  @override
  void drawPath(Path path, Paint paint) {
    _paint(paint);
    if (paint.style == PaintingStyle.stroke) tracos.add(paint.strokeWidth);
  }

  @override
  void drawCircle(Offset c, double radius, Paint paint) {
    _paint(paint);
    if (paint.style == PaintingStyle.fill) raios.add(radius);
  }

  @override
  void drawArc(Rect rect, double s, double w, bool useCenter, Paint paint) =>
      _paint(paint);

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

class _Host extends StatefulWidget {
  const _Host({required this.builder});

  final Widget Function(PlaybackController) builder;

  @override
  State<_Host> createState() => _HostState();
}

class _HostState extends State<_Host> with SingleTickerProviderStateMixin {
  late final PlaybackController playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 10),
  );

  @override
  void dispose() {
    playback.pause();
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    body: SizedBox(width: 400, height: 700, child: widget.builder(playback)),
  );
}
