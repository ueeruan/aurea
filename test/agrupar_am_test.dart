// AGRUPAR COMO NO ALIGHT MOTION (mapa do agrupamento, 14/09/2026).
//
// Os bugs que o testador sentia ("as vezes buga", "ligo tudo num nulo em
// vez de agrupar") e o que fica preso aqui:
//   * desagrupar pulava: a transformacao do grupo era jogada fora;
//   * vinculo (pai) entre filhos morria fora do grupo, e o criado la
//     dentro sumia ao sair;
//   * video e audio dentro de grupo nao tocavam nem exportavam;
//   * entrar e sair do grupo nao moviam o cabecote;
//   * todo grupo se chamava "Grupo".
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/grupo_ops.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

ShapeLayer _retangulo(
  String id,
  Offset pos,
  Color cor, {
  double w = 20,
  double h = 20,
  double rotacao = 0,
}) => ShapeLayer(
  id: id,
  name: id,
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  position: AnimatedOffset(pos),
  rotation: AnimatedDouble(rotacao),
  contents: [
    ShapePath(primitive: ShapePrimitive.rectangle, width: w, height: h),
    ShapeFill(color: cor),
  ],
);

VideoProject _projeto(List<Layer> camadas) => VideoProject(
  name: 'grupos',
  createdAt: DateTime(2026, 9, 14),
  aspectRatio: 1,
  resolutionHeight: 128,
  fps: 30,
  backgroundColor: const Color(0xFF000000),
  layers: camadas,
);

Future<Uint8List> _quadro(
  WidgetTester tester,
  ProviderContainer container,
  Duration t,
) async {
  final tempo = ValueNotifier(t);
  addTearDown(tempo.dispose);
  final videos = VideoLayerManager();
  addTearDown(videos.dispose);
  final chave = GlobalKey();
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Center(
          child: RepaintBoundary(
            key: chave,
            child: SizedBox(
              width: 128,
              height: 128,
              child: CompositionView(
                time: tempo,
                videos: videos,
                selectedId: null,
                exporting: true,
              ),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  final boundary =
      chave.currentContext!.findRenderObject() as RenderRepaintBoundary;
  final imagem = boundary.toImageSync(pixelRatio: 1);
  final dados = await tester.runAsync(
    () => imagem.toByteData(format: ui.ImageByteFormat.rawStraightRgba),
  );
  imagem.dispose();
  return dados!.buffer.asUint8List();
}

/// Fracao de pixels que mudaram de verdade (borda antisserrilhada nao
/// conta).
double _diferenca(Uint8List a, Uint8List b) {
  var diferentes = 0;
  for (var i = 0; i < a.length; i += 4) {
    final d = (a[i] - b[i]).abs() + (a[i + 1] - b[i + 1]).abs() + (a[i + 2] - b[i + 2]).abs();
    if (d > 120) diferentes++;
  }
  return diferentes / (a.length / 4);
}

int _pintados(Uint8List a) {
  var n = 0;
  for (var i = 0; i < a.length; i += 4) {
    if (a[i] + a[i + 1] + a[i + 2] > 120) n++;
  }
  return n;
}

bool _aceso(Uint8List b, int x, int y) {
  final i = (y * 128 + x) * 4;
  return b[i] + b[i + 1] + b[i + 2] > 200;
}

void _tela(WidgetTester tester) {
  tester.view.physicalSize = const Size(256, 256);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('desagrupar um grupo movido, girado e escalado nao pula', (
    tester,
  ) async {
    _tela(tester);
    final grupo = GroupLayer(
      id: 'g',
      name: 'Grupo 1',
      startTime: const Duration(milliseconds: 200),
      duration: const Duration(seconds: 4),
      position: AnimatedOffset(const Offset(72, 58)),
      rotation: AnimatedDouble(30),
      scaleX: AnimatedDouble(1.3),
      scaleY: AnimatedDouble(1.3),
      opacity: AnimatedDouble(.8),
      children: [
        _retangulo('a', const Offset(40, 40), const Color(0xFFFF3030)),
        _retangulo(
          'b',
          const Offset(84, 76),
          const Color(0xFF30FF60),
          w: 34,
          h: 12,
          rotacao: 20,
        ),
      ],
    );
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final e = container.read(editorControllerProvider.notifier);
    e.openProject(_projeto([grupo]));
    const t = Duration(milliseconds: 900);
    final antes = await _quadro(tester, container, t);
    expect(_pintados(antes), greaterThan(400));

    final avisos = e.ungroupLayer('g');
    expect(avisos, isEmpty);
    final camadas = container.read(editorControllerProvider).layers;
    expect(camadas.map((l) => l.id), ['a', 'b']);
    expect(camadas.first.startTime, const Duration(milliseconds: 200));
    final depois = await _quadro(tester, container, t);
    expect(_diferenca(antes, depois), lessThan(.012));
  });

  testWidgets('desagrupar um grupo ANIMADO acompanha a animacao', (tester) async {
    _tela(tester);
    final grupo = GroupLayer(
      id: 'g',
      name: 'Grupo 1',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      position: AnimatedOffset(const Offset(64, 64), const [
        Keyframe(time: Duration.zero, value: Offset(64, 64)),
        Keyframe(time: Duration(seconds: 1), value: Offset(88, 44)),
      ]),
      rotation: AnimatedDouble(0, const [
        Keyframe(time: Duration.zero, value: 0),
        Keyframe(time: Duration(seconds: 1), value: 40),
      ]),
      children: [
        _retangulo('a', const Offset(44, 50), const Color(0xFFFF3030)),
        _retangulo('b', const Offset(80, 80), const Color(0xFF3060FF), w: 26),
      ],
    );
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final e = container.read(editorControllerProvider.notifier);
    e.openProject(_projeto([grupo]));
    const instantes = [
      Duration(milliseconds: 100),
      Duration(milliseconds: 500),
      Duration(milliseconds: 950),
    ];
    final antes = [for (final t in instantes) await _quadro(tester, container, t)];
    e.ungroupLayer('g');
    for (var i = 0; i < instantes.length; i++) {
      final depois = await _quadro(tester, container, instantes[i]);
      expect(
        _diferenca(antes[i], depois),
        lessThan(.02),
        reason: 'instante ${instantes[i]}',
      );
    }
  });

  testWidgets('filho preso a um nulo do mesmo grupo segue o nulo fora do grupo', (
    tester,
  ) async {
    _tela(tester);
    final nulo = NullLayer(
      id: 'nulo',
      name: 'Nulo',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      position: AnimatedOffset(const Offset(30, 90), const [
        Keyframe(time: Duration.zero, value: Offset(30, 90)),
        Keyframe(time: Duration(seconds: 1), value: Offset(70, 90)),
      ]),
    );
    final grupo = GroupLayer(
      id: 'g',
      name: 'Grupo 1',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      position: AnimatedOffset(const Offset(64, 64)),
      children: [
        _retangulo('filho', const Offset(40, 40), const Color(0xFFFFFFFF)),
        nulo,
      ],
    );
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final e = container.read(editorControllerProvider.notifier);
    e.openProject(_projeto([grupo]));
    // O vinculo e criado LA DENTRO, como a pessoa faz.
    e.enterGroup('g');
    e.linkProperty('filho', LayerProp.parent, 'nulo', Duration.zero);
    e.exitGroup();
    expect(
      container.read(editorControllerProvider).linkFor('filho', LayerProp.parent),
      isNotNull,
      reason: 'o vinculo criado dentro sobrevive a sair do grupo',
    );
    final b = await _quadro(tester, container, const Duration(seconds: 1));
    // O nulo andou 40 px para a direita: o filho, de x = 40 para x = 80.
    expect(_aceso(b, 80, 40), isTrue);
    expect(_aceso(b, 40, 40), isFalse);
  });

  test('midias de dentro dos grupos entram com tempo absoluto e cortadas no fim', () {
    final video = VideoLayer(
      id: 'v',
      name: 'v',
      startTime: const Duration(seconds: 1),
      duration: const Duration(seconds: 5),
      sourcePath: '/v.mp4',
    );
    final audio = AudioLayer(
      id: 'a',
      name: 'a',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      sourcePath: '/a.m4a',
    );
    final interno = GroupLayer(
      id: 'gi',
      name: 'Grupo 2',
      startTime: const Duration(milliseconds: 500),
      duration: const Duration(seconds: 3),
      children: [audio],
    );
    final grupo = GroupLayer(
      id: 'g',
      name: 'Grupo 1',
      startTime: const Duration(seconds: 2),
      duration: const Duration(seconds: 4),
      children: [video, interno],
    );
    final semGrupo = [video];
    expect(identical(midiasAchatadas(semGrupo), semGrupo), isTrue);

    final achatadas = midiasAchatadas([grupo]);
    final v = achatadas.firstWhere((l) => l.id == 'v');
    expect(v.startTime, const Duration(seconds: 3));
    expect(v.endTime, const Duration(seconds: 6), reason: 'cortado no fim do grupo');
    final a = achatadas.firstWhere((l) => l.id == 'a');
    expect(a.startTime, const Duration(milliseconds: 2500));
    expect(a.duration, const Duration(seconds: 2));
  });

  test('nomes numerados, cabecote convertido e desfazer de um passo', () {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final e = container.read(editorControllerProvider.notifier);
    e.openProject(
      _projeto([
        _retangulo('a', const Offset(30, 30), const Color(0xFFFFFFFF)).copyLayer(
          startTime: const Duration(seconds: 2),
        ),
        _retangulo('b', const Offset(60, 60), const Color(0xFFFFFFFF)),
        _retangulo('c', const Offset(90, 90), const Color(0xFFFFFFFF)),
      ]),
    );
    e.groupLayers(['a', 'b']);
    final g1 = container.read(editorControllerProvider).layers.first as GroupLayer;
    expect(g1.name, 'Grupo 1');
    e.groupLayers(['c']);
    final nomes = [
      for (final l in container.read(editorControllerProvider).layers)
        if (l is GroupLayer) l.name,
    ];
    expect(nomes, containsAll(['Grupo 1', 'Grupo 2']));

    final deslocamentos = <Duration>[];
    e.aoMudarDeNivel = deslocamentos.add;
    final comAtraso = GroupLayer(
      id: 'tardio',
      name: 'Tardio',
      startTime: const Duration(seconds: 2),
      duration: const Duration(seconds: 2),
      children: [_retangulo('d', const Offset(10, 10), const Color(0xFFFFFFFF))],
    );
    e.openProject(_projeto([comAtraso]));
    e.enterGroup('tardio');
    e.exitGroup();
    expect(deslocamentos, [
      const Duration(seconds: -2),
      const Duration(seconds: 2),
    ]);
  });
}
