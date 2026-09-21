// A BANCADA DOS PAINEIS DO 3D: um painel montado sozinho, dentro do escopo
// do editor (relogio + gerente de video), sem a casca inteira — o que se
// mede aqui e o painel e o que ele faz no projeto.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// O escopo do editor com relogio de verdade (o painel le `playback.time`).
class BancoDoPainel extends StatefulWidget {
  const BancoDoPainel({
    super.key,
    required this.container,
    required this.construir,
    this.altura = 780,
  });

  final ProviderContainer container;
  final Widget Function(BuildContext context) construir;

  /// Alta de proposito: o painel de verdade tem 200 e rola, e aqui o que
  /// importa e achar as linhas sem rolar.
  final double altura;

  static PlaybackController? relogio;

  @override
  State<BancoDoPainel> createState() => _BancoDoPainelState();
}

class _BancoDoPainelState extends State<BancoDoPainel>
    with TickerProviderStateMixin {
  late final PlaybackController _playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 10),
  );
  final VideoLayerManager _videos = VideoLayerManager();

  @override
  void initState() {
    super.initState();
    BancoDoPainel.relogio = _playback;
  }

  @override
  void dispose() {
    _playback.dispose();
    _videos.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => EscopoDoEditor(
    playback: _playback,
    videos: _videos,
    abrirPainel: (id) =>
        widget.container.read(painelAbertoProvider.notifier).state = id,
    fecharPainel: () =>
        widget.container.read(painelAbertoProvider.notifier).state = null,
    child: Scaffold(
      body: Align(
        alignment: Alignment.bottomCenter,
        child: SizedBox(
          height: widget.altura,
          child: Builder(builder: widget.construir),
        ),
      ),
    ),
  );
}

/// Um projeto novo num container novo.
ProviderContainer containerNovo() {
  final c = ProviderContainer();
  addTearDown(c.dispose);
  c
      .read(editorControllerProvider.notifier)
      .openProject(VideoProject.empty('paineis 3d'));
  return c;
}

EditorController controladorDe(ProviderContainer c) =>
    c.read(editorControllerProvider.notifier);

/// Monta [painel] num telefone de 390 x 844.
Future<void> montar(
  WidgetTester tester,
  ProviderContainer c,
  Widget Function(BuildContext) painel,
) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: BancoDoPainel(container: c, construir: painel),
      ),
    ),
  );
  await tester.pumpAndSettle();
}

/// Cria um TEXTO 3D (a fonte e lida dos assets: fora do relogio falso).
Future<(String cena, String no)> criarTexto3D(
  WidgetTester tester,
  ProviderContainer c, [
  String texto = 'ABCDE',
]) async {
  final no = await tester.runAsync(
    () =>
        controladorDe(c).addTexto3D(Duration.zero, texto, EstiloDoTexto3D.ouro),
  );
  expect(no, isNotNull, reason: 'a fonte empacotada tem de abrir');
  final cena = c
      .read(editorControllerProvider)
      .layers
      .whereType<Scene3DLayer>()
      .firstWhere((l) => l.scene.nodeById(no!) != null);
  return (cena.id, no!);
}

Scene3DLayer cenaDe(ProviderContainer c, String id) =>
    c.read(editorControllerProvider).layerById(id)! as Scene3DLayer;

/// Arrasta a LINHA INTEIRA de uma propriedade (a linha e a superficie de
/// arrasto do `AureaPropertyRow`) — um gesto so, de uma vez.
Future<void> arrastarLinha(WidgetTester tester, String chave, double dx) async {
  final alvo = find.byKey(ValueKey('prop-$chave'));
  expect(alvo, findsOneWidget, reason: 'linha $chave');
  // O PONTO DE PARTIDA e o rotulo: o valor e o losango sao alvos de TOQUE.
  final r = tester.getRect(alvo);
  final g = await tester.startGesture(Offset(r.left + 30, r.center.dy));
  for (var i = 1; i <= 6; i++) {
    await g.moveBy(Offset(dx / 6, 0));
    await tester.pump(const Duration(milliseconds: 16));
  }
  await g.up();
  await tester.pump();
}

/// Deixa a malha do Texto 3D ser refeita: a espera de 140 ms e a
/// reconstrucao (fonte lida dos assets).
Future<void> esperarAMalha(WidgetTester tester) async {
  for (var i = 0; i < 20; i++) {
    await tester.pump(const Duration(milliseconds: 50));
    await tester.runAsync(
      () => Future<void>.delayed(const Duration(milliseconds: 20)),
    );
  }
  await tester.pump();
}

/// Toca na aba [i] do painel [painel] (a fileira de abas rola de lado e e
/// preguicosa: a aba do fim pode estar fora da tela).
Future<void> tocarNaAba(WidgetTester tester, String painel, int i) async {
  final aba = find.byKey(
    ValueKey('painel-$painel-aba-$i'),
    skipOffstage: false,
  );
  expect(aba, findsOneWidget, reason: 'aba $i de $painel');
  await tester.ensureVisible(aba);
  await tester.pumpAndSettle();
  await tester.tap(find.byKey(ValueKey('painel-$painel-aba-$i')));
  await tester.pumpAndSettle();
}
