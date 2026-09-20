// A LINHA DO TEMPO SO CONSTROI E SO DESENHA O QUE ESTA NA TELA.
//
// O RELATO (perfil no emulador): rolar a linha do tempo dava 28 fps com
// o projeto em 2D e 27 com uma cena 3D. A meta e 60, e 120 onde o
// aparelho deixar.
//
// A CAUSA NAO ERA O 3D. O eixo do tempo e um `SingleChildScrollView` com
// o conteudo INTEIRO dentro: uma hora a 400 px/s sao 1,44 milhao de
// pixels. Nada ali sabia o que estava visivel —
//
//   * cada pedaco de um video decupado virava uma barra com alcas,
//     previa, onda e losangos, a dez telas do cabecote ou nao;
//   * a regua riscava a duracao toda a cada quadro de rolagem;
//   * a onda montava um caminho da largura do clipe, e entregava os
//     milhares de pontos ao raster em todo quadro.
//
// E cada passo de um arrasto reconstruia a linha do tempo INTEIRA,
// embora uma so camada tivesse mudado.
//
// O QUE ESTE ARQUIVO PRENDE, e por que assim:
//
//   * a JANELA e uma conta pura, entao se testa como conta — sem arvore;
//   * "so a janela nasce" se prova pela AUSENCIA do widget (`findsNothing`
//     na chave da barra), que e o unico jeito de provar que ele nao foi
//     construido;
//   * "o tique nao reconstroi" se prova CONTANDO builds de barra com o
//     contador do Perfil3D, e nao cronometrando;
//   * o arrasto de dois degraus prende a regressao real: sem
//     `findChildIndexCallback` a linha era inflada do zero no primeiro
//     degrau, o `State` da barra morria, e com ele o reconhecedor do
//     toque longo — a camada subia UM degrau e o gesto acabava.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/perfil3d.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/peak_pyramid.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:aurea/src/features/editor/presentation/am/clip_preview_painters.dart';
import 'package:aurea/src/features/editor/presentation/am/janela_da_timeline.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show FontLoader, rootBundle;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// Um projeto com [camadas] formas de tres segundos, uma a cada
/// [passo] — o suficiente para haver barras perto e longe do cabecote.
VideoProject _projeto({
  int camadas = 4,
  Duration passo = const Duration(seconds: 1),
}) => VideoProject(
  name: 'virtualizada',
  createdAt: DateTime(2026, 9, 20),
  layers: [
    for (var i = 0; i < camadas; i++)
      ShapeLayer(
        id: 'c$i',
        name: 'Camada $i',
        startTime: passo * i,
        duration: const Duration(seconds: 3),
        position: AnimatedOffset(const Offset(200, 200)),
        contents: [
          ShapePath(primitive: ShapePrimitive.rectangle),
          ShapeFill(color: const Color(0xFF3DDC97)),
        ],
      ),
  ],
);

class _Casca extends StatefulWidget {
  const _Casca({required this.aoCriar});
  final void Function(PlaybackController) aoCriar;

  @override
  State<_Casca> createState() => _CascaState();
}

class _CascaState extends State<_Casca> with SingleTickerProviderStateMixin {
  late final PlaybackController _playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 600),
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
    height: 320,
    child: AmTimeline(playback: _playback, height: 320),
  );
}

Future<(ProviderContainer, PlaybackController)> _montar(
  WidgetTester tester,
  VideoProject projeto,
) async {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container.read(editorControllerProvider.notifier).openProject(projeto);
  late PlaybackController playback;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Scaffold(body: _Casca(aoCriar: (p) => playback = p)),
      ),
    ),
  );
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 100));
  return (container, playback);
}

/// Uma piramide com um segundo de som, com forma (e nao uma linha reta,
/// que nao provaria nada).
PeakPyramid _piramide() {
  const taxa = 16000;
  const balde = 64;
  final n = taxa ~/ balde;
  final mn = Float32List(n);
  final mx = Float32List(n);
  final rms = Float32List(n);
  for (var i = 0; i < n; i++) {
    final a = i < n ~/ 2 ? (0.6 + 0.4 * (i % 7) / 7) : 0.02;
    mx[i] = a;
    mn[i] = -a;
    rms[i] = a * 0.6;
  }
  return pyramidFromBase(mn, mx, rms, taxa);
}

Float64List _fonte(double segundos) =>
    Float64List.fromList([for (var i = 0; i <= 256; i++) segundos * i / 256]);

void main() {
  setUpAll(() async {
    for (final f in ['Aurea Motion Sans', 'Roboto', 'FlutterTest']) {
      await (FontLoader(f)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  group('a janela e uma conta', () {
    test('anda em baldes, e nao a cada pixel', () {
      // SE ELA MUDASSE A CADA PIXEL, quem a escuta reconstruiria a cada
      // quadro de rolagem — o contrario do que se quer.
      final a = JanelaDaTimeline.de(offset: 1000, viewport: 400, recuo: 200);
      final b = JanelaDaTimeline.de(offset: 1010, viewport: 400, recuo: 200);
      expect(b, a, reason: 'dez pixels nao podem trocar a janela');
      final longe = JanelaDaTimeline.de(
        offset: 1000 + JanelaDaTimeline.balde * 2,
        viewport: 400,
        recuo: 200,
      );
      expect(longe, isNot(a));
    });

    test('cobre a tela inteira mais a folga', () {
      final j = JanelaDaTimeline.de(offset: 1000, viewport: 400, recuo: 200);
      // Conteudo visivel: [800, 1200]. A janela tem de conter isso.
      expect(j.iniPx, lessThanOrEqualTo(800));
      expect(j.fimPx, greaterThanOrEqualTo(1200));
      expect(j.contem(800), isTrue);
      expect(j.contem(1200), isTrue);
      expect(j.cruza(700, 810), isTrue, reason: 'uma ponta ja aparece');
      expect(j.cruza(-5000, -4000), isFalse);
    });

    test('sem janela, tudo aparece', () {
      expect(JanelaDaTimeline.tudo.cruza(-1e9, -1e8), isTrue);
      expect(JanelaDaTimeline.tudo.contem(1e9), isTrue);
    });
  });

  group('a onda so monta a janela', () {
    setUp(ClipWaveformPainter.limparCache);
    tearDown(ClipWaveformPainter.limparCache);

    test('um clipe largo constroi so as colunas visiveis', () {
      // 8000 px de barra: e o que um clipe de tres minutos vira num zoom
      // qualquer. Sem janela sao 8000 colunas, tres `Float32List` e dois
      // caminhos com 16 mil pontos — por passo de zoom, por clipe.
      const largura = 8000.0;
      final janela = ValueNotifier(const JanelaDaTimeline(0, 512));
      addTearDown(janela.dispose);
      final pintor = ClipWaveformPainter(
        pyramid: _piramide(),
        fonte: _fonte(180),
        color: const Color(0xFF33E1C0),
        janela: janela,
      );
      final gravador = ui.PictureRecorder();
      pintor.paint(Canvas(gravador), const Size(largura, 40));
      gravador.endRecording().dispose();
      expect(ClipWaveformPainter.construcoes, 1);
      expect(
        ClipWaveformPainter.colunasDaUltimaConstrucao,
        lessThan(2000),
        reason: 'a onda montou muito mais do que cabe na tela',
      );
      expect(
        ClipWaveformPainter.colunasDaUltimaConstrucao,
        greaterThan(0),
        reason: 'a onda nao montou nada dentro da janela',
      );
    });

    test('sem janela, a onda inteira — o comportamento de sempre', () {
      const largura = 800.0;
      final pintor = ClipWaveformPainter(
        pyramid: _piramide(),
        fonte: _fonte(1),
        color: const Color(0xFF33E1C0),
      );
      final gravador = ui.PictureRecorder();
      pintor.paint(Canvas(gravador), const Size(largura, 40));
      gravador.endRecording().dispose();
      expect(ClipWaveformPainter.colunasDaUltimaConstrucao, largura.toInt());
    });

    test('o clipe inteiro fora da janela nao grava nada', () {
      // A barra selecionada fica na arvore mesmo longe do cabecote (e a
      // unica que pode estar em arrasto). Ela nao pode pagar a onda.
      final janela = ValueNotifier(const JanelaDaTimeline(50000, 51000));
      addTearDown(janela.dispose);
      final pintor = ClipWaveformPainter(
        pyramid: _piramide(),
        fonte: _fonte(1),
        color: const Color(0xFF33E1C0),
        janela: janela,
      );
      final gravador = ui.PictureRecorder();
      pintor.paint(Canvas(gravador), const Size(400, 40));
      gravador.endRecording().dispose();
      expect(ClipWaveformPainter.construcoes, 0);
      expect(ClipWaveformPainter.entradasNoCache, 0);
    });
  });

  testWidgets('so as barras da janela nascem; a de longe aparece ao rolar', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1000, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    // Camadas de 3 s a cada 20 s: a 80 px/s, a ultima comeca a 4800 px do
    // zero — dezenas de telas depois do cabecote.
    final (container, playback) = await _montar(
      tester,
      _projeto(camadas: 4, passo: const Duration(seconds: 20)),
    );
    expect(
      find.byKey(const ValueKey('clip-content-c0')),
      findsOneWidget,
      reason: 'a barra sob o cabecote tem de existir',
    );
    expect(
      find.byKey(const ValueKey('clip-content-c3')),
      findsNothing,
      reason: 'uma barra a 4800 px do cabecote nao pode nascer',
    );

    // O CABECOTE VAI ATE LA: a barra passa a existir, sem nenhuma outra
    // mudanca no projeto.
    playback.seek(const Duration(seconds: 61));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));
    expect(
      find.byKey(const ValueKey('clip-content-c3')),
      findsOneWidget,
      reason: 'a barra sob o cabecote tem de aparecer quando ele chega',
    );
    expect(
      find.byKey(const ValueKey('clip-content-c0')),
      findsNothing,
      reason: 'e a de tras tem de sair',
    );
    expect(container.read(editorControllerProvider).layers, hasLength(4));
  });

  testWidgets('a barra SELECIONADA fica na arvore mesmo longe do cabecote', (
    tester,
  ) async {
    // Ela e a unica que pode estar em arrasto, e o `State` dela guarda o
    // gesto: some-la no meio do movimento mataria o reconhecedor e o
    // dedo perderia o clipe.
    tester.view.physicalSize = const Size(1000, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final (container, _) = await _montar(
      tester,
      _projeto(camadas: 4, passo: const Duration(seconds: 20)),
    );
    container.read(selectedLayerProvider.notifier).state = 'c3';
    await tester.pump();
    expect(find.byKey(const ValueKey('clip-content-c3')), findsOneWidget);
  });

  testWidgets('o tique do relogio NAO reconstroi as linhas', (tester) async {
    tester.view.physicalSize = const Size(1000, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final (_, playback) = await _montar(tester, _projeto(camadas: 6));
    await tester.pump(const Duration(milliseconds: 50));

    Perfil3D.zerar();
    Perfil3D.ligado = true;
    // TRINTA TIQUES dentro do mesmo balde da janela: rolar e so
    // transladar camadas prontas. O unico widget que o tique pode
    // reconstruir e o selo do tempo.
    for (var i = 1; i <= 30; i++) {
      playback.seek(Duration(milliseconds: 16 * i));
      await tester.pump(const Duration(milliseconds: 16));
    }
    Perfil3D.ligado = false;
    final r = Perfil3D.relatorio();
    expect(
      r.contas['build.barra'] ?? 0,
      0,
      reason: 'o relogio reconstruiu barras',
    );
    expect(
      r.fases['build.timeline']?.chamadas ?? 0,
      0,
      reason: 'o relogio reconstruiu a linha do tempo inteira',
    );
  });

  testWidgets('arrastar DOIS degraus num gesto so, e um desfazer devolve', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1000, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final (container, _) = await _montar(tester, _projeto(camadas: 4));
    final controlador = container.read(editorControllerProvider.notifier);
    // A de baixo na pilha e a ULTIMA da lista; ela vai subir dois degraus.
    final ordemInicial = [
      for (final l in container.read(editorControllerProvider).layers) l.id,
    ];
    final alvo = ordemInicial.last;
    container.read(selectedLayerProvider.notifier).state = alvo;
    await tester.pumpAndSettle();

    final barra = find.byKey(ValueKey('clip-content-$alvo'));
    expect(barra, findsOneWidget);
    final dedo = await tester.startGesture(tester.getCenter(barra));
    // `kLongPressTimeout` e 500 ms: o dedo tem de ficar parado antes do
    // primeiro pixel, senao o gesto vai para a rolagem.
    await tester.pump(const Duration(milliseconds: 600));
    // DOIS DEGRAUS NO MESMO GESTO. Era aqui que quebrava: sem
    // `findChildIndexCallback` o primeiro degrau inflava a linha do zero,
    // o `State` da barra morria, e o arrasto acabava no meio.
    for (var i = 1; i <= 18; i++) {
      await dedo.moveBy(const Offset(0, -kAmRowHeight / 6));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await dedo.up();
    await tester.pumpAndSettle();

    final depois = [
      for (final l in container.read(editorControllerProvider).layers) l.id,
    ];
    expect(
      depois.indexOf(alvo),
      lessThanOrEqualTo(ordemInicial.indexOf(alvo) - 2),
      reason: 'a camada subiu menos de dois degraus num gesto so',
    );

    // UM GESTO, UM DESFAZER. Antes cada degrau era estrutural e empilhava
    // um passo proprio: desfazer um arrasto de dois degraus pedia dois
    // toques, e quem arrastou tres linhas tocava tres vezes.
    controlador.undo();
    expect(
      [for (final l in container.read(editorControllerProvider).layers) l.id],
      ordemInicial,
      reason: 'um desfazer tem de devolver o arrasto inteiro',
    );
  });

  testWidgets('arrastar o clipe no tempo: um gesto, um desfazer', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1000, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final (container, _) = await _montar(tester, _projeto(camadas: 3));
    final controlador = container.read(editorControllerProvider.notifier);
    const alvo = 'c1';
    container.read(selectedLayerProvider.notifier).state = alvo;
    await tester.pumpAndSettle();

    Duration inicio() => container
        .read(editorControllerProvider)
        .layers
        .firstWhere((l) => l.id == alvo)
        .startTime;
    final antes = inicio();

    final dedo = await tester.startGesture(
      tester.getCenter(find.byKey(const ValueKey('clip-content-$alvo'))),
    );
    await tester.pump(const Duration(milliseconds: 600));
    await dedo.moveBy(const Offset(40, 0));
    await tester.pump(const Duration(milliseconds: 16));
    for (var i = 0; i < 10; i++) {
      await dedo.moveBy(const Offset(8, 0));
      // UM PASSO DE DEDO NAO E UM QUADRO. O aparelho amostra o toque a
      // 120 ou 240 Hz numa tela de 60: a mutacao e coalescida por quadro,
      // e o alvo e sempre absoluto (origem + acumulado), entao descartar
      // os intermediarios nao perde nada. Com dois eventos por quadro o
      // teste prova justamente isso.
      await dedo.moveBy(const Offset(8, 0));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await dedo.up();
    await tester.pumpAndSettle();

    expect(
      inicio(),
      isNot(antes),
      reason: 'o arrasto nao moveu o clipe — o gesto errou o alvo',
    );
    controlador.undo();
    expect(
      inicio(),
      antes,
      reason: 'um desfazer tem de devolver o arrasto inteiro',
    );
  });
}
