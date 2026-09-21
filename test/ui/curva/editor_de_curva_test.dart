// O EDITOR DE CURVA DA UI NOVA (ui/curva/).
//
// O que ele promete, na ordem dos testes:
//
//   1. abre ENTRE DOIS keyframes da propriedade e mostra a curva daquele
//      trecho — fora deles, um aviso (a regra da referencia);
//   2. os presets da faixa gravam a curva do trecho, cada toque um passo
//      de desfazer;
//   3. arrastar uma alca muda a bezier e e UM desfazer, por mais passos
//      que o arrasto tenha;
//   4. o grafico de velocidade edita a mesma bezier; a pinca da zoom e o
//      toque duplo reenquadra;
//   5. Hold segura o valor; "Selecionados" aplica em todas as marcas
//      escolhidas na timeline;
//   6. ‹ › levam o cabecote a marca anterior/proxima;
//   7. nenhum gesto grava numero nao finito;
//   8. as outras portas (efeito, Time Remap, no 3D, letra do Texto 3D)
//      falam com o controlador pelo mesmo contrato.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/keyframe_clipboard.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/curva.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/grafico_da_curva.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

Duration _s(num x) => Duration(microseconds: (x * 1000000).round());

const _grafico = ValueKey('curva-grafico');

/// Uma camada de forma com marcas de POSICAO em [posicao] (segundos) e de
/// OPACIDADE em [opacidade].
({ProviderContainer c, EditorController e, String id}) _bancada({
  List<num> posicao = const [0, 1],
  List<num> opacidade = const [],
}) {
  final c = ProviderContainer();
  addTearDown(c.dispose);
  final e = c.read(editorControllerProvider.notifier);
  e.addShapeLayer(Duration.zero, name: 'Alvo');
  final id = c.read(editorControllerProvider).layers.single.id;
  for (final t in posicao) {
    e.toggleKeyframe(id, _s(t), LayerProp.position);
  }
  for (final t in opacidade) {
    e.toggleKeyframe(id, _s(t), LayerProp.opacity);
  }
  return (c: c, e: e, id: id);
}

Layer _camada(ProviderContainer c, String id) =>
    c.read(editorControllerProvider).layerById(id)!;

Easing _curvaDaPosicao(ProviderContainer c, String id, num t) =>
    _camada(c, id).position.easeAt(_s(t));

/// A tela do teste: o palco vazio em cima e o que [construir] devolver.
class _Palco extends StatefulWidget {
  const _Palco({super.key, required this.construir, required this.aoCriar});

  final Widget Function(BuildContext context, PlaybackController p) construir;
  final ValueChanged<PlaybackController> aoCriar;

  @override
  State<_Palco> createState() => _PalcoState();
}

class _PalcoState extends State<_Palco> with SingleTickerProviderStateMixin {
  late final PlaybackController playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 5),
  );

  @override
  void initState() {
    super.initState();
    widget.aoCriar(playback);
  }

  @override
  void dispose() {
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) =>
      Scaffold(body: widget.construir(context, playback));
}

void _telaDeCelular(WidgetTester tester) {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
}

/// Monta o editor EMBUTIDO (316 de altura, a do painel grande sem o
/// respiro da folha) sobre um palco vazio. Devolve o relogio.
Future<PlaybackController> _montar(
  WidgetTester tester,
  ProviderContainer c,
  String id, {
  TrilhaDaCurva? trilha,
  Duration tempo = const Duration(milliseconds: 500),
  Duration? cabecote,
}) async {
  _telaDeCelular(tester);
  late PlaybackController relogio;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: _Palco(
          // Cada montagem e uma tela nova (e um relogio novo), mesmo duas
          // no mesmo teste.
          key: UniqueKey(),
          aoCriar: (p) {
            relogio = p;
            if (cabecote != null) p.seek(cabecote);
          },
          construir: (context, p) => Column(
            children: [
              const Expanded(child: SizedBox.expand()),
              SizedBox(
                height: 316,
                child: EditorDeCurva(
                  layerId: id,
                  trilha: trilha ?? TrilhaDaCurva.transformacao(LayerProp.position),
                  tempoInicial: tempo,
                  playback: p,
                  aoFechar: () {},
                ),
              ),
            ],
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  return relogio;
}

GraficoDaCurvaState _estadoDoGrafico(WidgetTester tester) =>
    tester.state<GraficoDaCurvaState>(find.byKey(_grafico));

/// O centro da alca [i] na TELA.
Offset _alcaNaTela(WidgetTester tester, int i) =>
    tester.getTopLeft(find.byKey(_grafico)) +
    _estadoDoGrafico(tester).centroDaAlca(i)!;

void _finita(Easing e) {
  for (final v in [e.x1, e.y1, e.x2, e.y2, e.intensity, e.smooth]) {
    expect(v.isFinite, isTrue, reason: 'a curva guardou $v');
  }
  for (var i = 0; i <= 20; i++) {
    expect(e.transform(i / 20).isFinite, isTrue);
    expect(e.speedAt(i / 20).isNaN, isFalse);
  }
}

void main() {
  group('abrir', () {
    testWidgets('abre entre dois keyframes e mostra a curva, pela porta publica', (
      tester,
    ) async {
      final b = _bancada();
      _telaDeCelular(tester);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: b.c,
          child: MaterialApp(
            home: _Palco(
              aoCriar: (_) {},
              construir: (context, p) => Consumer(
                builder: (context, ref, _) => Center(
                  child: GestureDetector(
                    key: const ValueKey('abrir'),
                    behavior: HitTestBehavior.opaque,
                    onTap: () => abrirEditorDeCurva(
                      context,
                      ref,
                      layerId: b.id,
                      trilha: TrilhaDaCurva.transformacao(LayerProp.position),
                      tempo: _s(.5),
                      playback: p,
                    ),
                    child: const SizedBox(width: 80, height: 80),
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.tap(find.byKey(const ValueKey('abrir')));
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('curva')), findsOneWidget);
      expect(find.byKey(_grafico), findsOneWidget);
      expect(find.byKey(const ValueKey('curva-aviso')), findsNothing);
      expect(find.text('Linear · Trecho 1 de 1'), findsOneWidget);
      // A folha tem a altura do painel grande: 326 = timeline + transporte,
      // e o palco fica a vista em cima.
      final painel = tester.getRect(find.byKey(const ValueKey('curva')));
      expect(painel.height, closeTo(316, .5));
      expect(painel.top, greaterThan(844 - 326 - 40));
      // As sete fichas, na ordem da referencia.
      for (final p in presetsDoEditorDeCurva) {
        expect(find.byKey(ValueKey(chaveDoPreset(p))), findsOneWidget);
      }
      expect(
        [for (final p in presetsDoEditorDeCurva) p.nome],
        ['Linear', 'Ease', 'Ease in', 'Ease out', 'Ease in-out', 'Bézier', 'Hold'],
      );

      await tester.tap(find.byKey(const ValueKey('curva-fechar')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('curva')), findsNothing);
    });

    testWidgets('fora de dois keyframes mostra o aviso, e nao uma curva', (
      tester,
    ) async {
      // O losango fora das marcas E o cabecote fora delas: nao ha trecho.
      final b = _bancada();
      await _montar(tester, b.c, b.id, tempo: _s(3), cabecote: _s(3));
      expect(find.byKey(const ValueKey('curva-aviso')), findsOneWidget);
      expect(find.byKey(_grafico), findsNothing);

      final so1 = _bancada(posicao: const [0]);
      await _montar(tester, so1.c, so1.id);
      expect(
        find.text('Crie dois keyframes nesta propriedade para editar a curva.'),
        findsOneWidget,
      );
    });

    testWidgets('o toque longo na ULTIMA marca abre o trecho que chega nela', (
      tester,
    ) async {
      final b = _bancada(posicao: const [0, 1, 2]);
      await _montar(tester, b.c, b.id, tempo: _s(2));
      expect(find.text('Linear · Trecho 2 de 2'), findsOneWidget);
    });
  });

  group('presets', () {
    testWidgets('Ease In muda o easing do trecho, e e um desfazer', (
      tester,
    ) async {
      final b = _bancada();
      await _montar(tester, b.c, b.id);
      await tester.tap(find.byKey(const ValueKey('curva-preset-ease-in')));
      await tester.pump();
      expect(_curvaDaPosicao(b.c, b.id, 0).mesmoPresetQue(Easing.easeIn), isTrue);
      expect(find.text('Ease in · Trecho 1 de 1'), findsOneWidget);

      // O "Ease" e a curva ease padrao, vinda do catalogo.
      await tester.tap(find.byKey(const ValueKey('curva-preset-ease')));
      await tester.pump();
      expect(
        _curvaDaPosicao(b.c, b.id, 0).mesmoPresetQue(Easing.appleStandard),
        isTrue,
      );

      b.e.undo();
      expect(_curvaDaPosicao(b.c, b.id, 0).mesmoPresetQue(Easing.easeIn), isTrue);
      b.e.undo();
      expect(_curvaDaPosicao(b.c, b.id, 0).isLinear, isTrue);
    });

    testWidgets('Hold segura o valor ate o proximo keyframe', (tester) async {
      final b = _bancada(posicao: const [], opacidade: const [0, 1]);
      b.e.editOpacity(b.id, _s(0), 1);
      b.e.editOpacity(b.id, _s(1), 0);
      await _montar(
        tester,
        b.c,
        b.id,
        trilha: TrilhaDaCurva.transformacao(LayerProp.opacity),
      );
      // A setima ficha pode estar fora da vista: a faixa rola.
      await tester.ensureVisible(find.byKey(const ValueKey('curva-preset-hold')));
      await tester.tap(find.byKey(const ValueKey('curva-preset-hold')));
      await tester.pump();

      final l = _camada(b.c, b.id);
      expect(l.opacity.easeAt(_s(0)).type, EasingType.hold);
      expect(l.opacity.valueAt(_s(.5)), 1);
      expect(l.opacity.valueAt(_s(.99)), 1);
      expect(l.opacity.valueAt(_s(1)), 0);
      expect(
        find.text('Segura o valor até o próximo keyframe.'),
        findsOneWidget,
      );
    });
  });

  group('alcas', () {
    testWidgets('arrastar uma alca altera a bezier e e UM desfazer', (
      tester,
    ) async {
      final b = _bancada();
      b.e.setSegmentEase(b.id, LayerProp.position, Duration.zero, Easing.easeInOut);
      await _montar(tester, b.c, b.id);

      final inicio = _alcaNaTela(tester, 0);
      final gesto = await tester.startGesture(inicio);
      // Passos separados por MAIS que a janela de 450 ms do controlador:
      // sem o gesto aberto, cada um viraria um passo de desfazer.
      for (var i = 1; i <= 6; i++) {
        await gesto.moveTo(inicio + Offset(8.0 * i, -6.0 * i));
        await tester.pump(const Duration(milliseconds: 500));
      }
      await gesto.up();
      await tester.pump();

      final depois = _curvaDaPosicao(b.c, b.id, 0);
      expect(depois.x1, greaterThan(.5), reason: 'a alca nao andou');
      expect(depois.y1, greaterThan(.05));
      expect(depois.x2, closeTo(.58, 1e-9), reason: 'mexeu na outra alca');
      expect(depois.y2, closeTo(1, 1e-9));
      expect(find.byKey(const ValueKey('curva-leitura')), findsOneWidget);

      b.e.undo();
      final volta = _curvaDaPosicao(b.c, b.id, 0);
      expect(volta.x1, closeTo(.42, 1e-9), reason: 'um desfazer nao voltou tudo');
      expect(volta.y1, closeTo(0, 1e-9));
    });

    testWidgets('um toque na alca, sem arrastar, nao gasta desfazer', (
      tester,
    ) async {
      final b = _bancada();
      // Dois passos de desfazer bem separados: ease out, depois ease in-out.
      b.e.runAsOneUndo(
        () => b.e.setSegmentEase(
          b.id,
          LayerProp.position,
          Duration.zero,
          Easing.easeOut,
        ),
      );
      b.e.runAsOneUndo(
        () => b.e.setSegmentEase(
          b.id,
          LayerProp.position,
          Duration.zero,
          Easing.easeInOut,
        ),
      );
      await _montar(tester, b.c, b.id);
      final gesto = await tester.startGesture(_alcaNaTela(tester, 1));
      await tester.pump(const Duration(milliseconds: 100));
      await gesto.up();
      await tester.pump();
      expect(
        _curvaDaPosicao(b.c, b.id, 0).mesmoPresetQue(Easing.easeInOut),
        isTrue,
      );
      b.e.undo();
      // Se o toque tivesse aberto um gesto, este undo voltaria ao proprio
      // ease in-out (nada mudou no "gesto") em vez de desfazer o ultimo
      // passo de verdade.
      expect(
        _curvaDaPosicao(b.c, b.id, 0).mesmoPresetQue(Easing.easeOut),
        isTrue,
      );
    });

    testWidgets('o ima gruda a alca em 1 e na diagonal (linear)', (
      tester,
    ) async {
      final r = moverAlcaDaCurva(
        Easing.easeInOut,
        ModoDoGrafico.valor,
        1,
        .6,
        .995,
        tolX: .02,
        tolY: .02,
      );
      expect(r.curva.y2, 1);
      expect(r.ima.y, isTrue);
      final l = moverAlcaDaCurva(
        Easing.easeInOut,
        ModoDoGrafico.valor,
        0,
        .40,
        .41,
        tolX: .02,
        tolY: .02,
      );
      expect(l.ima.linear, isTrue);
      expect(l.curva.x1, l.curva.y1);
    });
  });

  group('graficos', () {
    testWidgets('alternar para velocidade edita a mesma bezier', (
      tester,
    ) async {
      final b = _bancada();
      b.e.setSegmentEase(b.id, LayerProp.position, Duration.zero, Easing.easeInOut);
      await _montar(tester, b.c, b.id);
      expect(
        tester.widget<GraficoDaCurva>(find.byKey(_grafico)).modo,
        ModoDoGrafico.valor,
      );

      await tester.tap(find.byKey(const ValueKey('curva-modo-velocidade')));
      await tester.pump();
      expect(
        tester.widget<GraficoDaCurva>(find.byKey(_grafico)).modo,
        ModoDoGrafico.velocidade,
      );
      final leitura = tester.widget<Text>(
        find.byKey(const ValueKey('curva-leitura')),
      );
      expect(leitura.data, startsWith('Saída 0× · 42%'));

      // A velocidade de SAIDA sobe: a alca 0 vai para cima.
      final inicio = _alcaNaTela(tester, 0);
      final gesto = await tester.startGesture(inicio);
      for (var i = 1; i <= 5; i++) {
        await gesto.moveTo(inicio + Offset(0, -12.0 * i));
        await tester.pump(const Duration(milliseconds: 16));
      }
      await gesto.up();
      await tester.pump();
      final depois = _curvaDaPosicao(b.c, b.id, 0);
      expect(depois.y1, greaterThan(0), reason: 'a velocidade nao mudou');
      expect(depois.speedAt(0), greaterThan(.3));
      _finita(depois);

      await tester.tap(find.byKey(const ValueKey('curva-modo-valor')));
      await tester.pump();
      expect(
        tester.widget<GraficoDaCurva>(find.byKey(_grafico)).modo,
        ModoDoGrafico.valor,
      );
    });

    testWidgets('pinca muda o zoom e o toque duplo reenquadra', (tester) async {
      final b = _bancada();
      await _montar(tester, b.c, b.id);
      final antes = _estadoDoGrafico(tester).vista;
      final centro = tester.getCenter(find.byKey(_grafico));

      final a = await tester.startGesture(centro - const Offset(20, 0));
      final d = await tester.startGesture(
        centro + const Offset(20, 0),
        pointer: 7,
      );
      for (var i = 1; i <= 4; i++) {
        await a.moveTo(centro - Offset(20.0 + 15 * i, 0));
        await d.moveTo(centro + Offset(20.0 + 15 * i, 0));
        await tester.pump();
      }
      final perto = _estadoDoGrafico(tester).vista;
      expect(perto.largura, lessThan(antes.largura * .5),
          reason: 'afastar os dedos tem de aproximar');
      // Dois dedos andando juntos deslocam.
      for (var i = 1; i <= 3; i++) {
        await a.moveBy(const Offset(10, 0));
        await d.moveBy(const Offset(10, 0));
        await tester.pump();
      }
      expect(_estadoDoGrafico(tester).vista.x0, lessThan(perto.x0));
      await a.up();
      await d.up();
      await tester.pump();
      // A pinca nao mexe na curva.
      expect(_curvaDaPosicao(b.c, b.id, 0).isLinear, isTrue);

      await tester.tap(find.byKey(_grafico));
      await tester.pump(const Duration(milliseconds: 60));
      await tester.tap(find.byKey(_grafico));
      await tester.pumpAndSettle();
      expect(_estadoDoGrafico(tester).vista, antes);
    });
  });

  group('selecionados', () {
    testWidgets('aplica nas marcas selecionadas, num desfazer so', (
      tester,
    ) async {
      final b = _bancada(posicao: const [0, 1, 2, 3], opacidade: const [0, 1]);
      b.c.read(keyframesSelecionadosProvider.notifier).state = {
        (layerId: b.id, prop: LayerProp.position, tempo: _s(1)),
        (layerId: b.id, prop: LayerProp.opacity, tempo: _s(0)),
      };
      await _montar(tester, b.c, b.id);
      expect(find.text('Selecionados (2)'), findsOneWidget);

      await tester.tap(find.byKey(const ValueKey('curva-selecionados')));
      await tester.pump();
      await tester.tap(find.byKey(const ValueKey('curva-preset-ease-out')));
      await tester.pump();

      final l = _camada(b.c, b.id);
      expect(l.position.easeAt(_s(0)).mesmoPresetQue(Easing.easeOut), isTrue,
          reason: 'o trecho mostrado');
      expect(l.position.easeAt(_s(1)).mesmoPresetQue(Easing.easeOut), isTrue,
          reason: 'a marca selecionada da mesma propriedade');
      expect(l.opacity.easeAt(_s(0)).mesmoPresetQue(Easing.easeOut), isTrue,
          reason: 'a marca selecionada de outra propriedade');
      expect(l.position.easeAt(_s(2)).isLinear, isTrue,
          reason: 'marca que ninguem escolheu');

      b.e.undo();
      final volta = _camada(b.c, b.id);
      expect(volta.position.easeAt(_s(0)).isLinear, isTrue);
      expect(volta.position.easeAt(_s(1)).isLinear, isTrue);
      expect(volta.opacity.easeAt(_s(0)).isLinear, isTrue);
    });

    testWidgets('desligado, grava so o trecho mostrado', (tester) async {
      final b = _bancada(posicao: const [0, 1, 2]);
      b.c.read(keyframesSelecionadosProvider.notifier).state = {
        (layerId: b.id, prop: LayerProp.position, tempo: _s(1)),
      };
      await _montar(tester, b.c, b.id);
      await tester.tap(find.byKey(const ValueKey('curva-preset-ease-out')));
      await tester.pump();
      expect(_curvaDaPosicao(b.c, b.id, 0).mesmoPresetQue(Easing.easeOut), isTrue);
      expect(_curvaDaPosicao(b.c, b.id, 1).isLinear, isTrue);
    });
  });

  group('navegacao', () {
    testWidgets('‹ › movem o cabecote para a marca anterior/proxima', (
      tester,
    ) async {
      final b = _bancada(posicao: const [0, 1, 2]);
      final relogio = await _montar(tester, b.c, b.id, cabecote: _s(.5));
      expect(relogio.time.value, _s(.5));

      await tester.tap(find.byKey(const ValueKey('curva-proximo')));
      await tester.pump();
      expect(relogio.time.value, _s(1));
      // O editor seguiu o cabecote para o trecho que sai da marca.
      expect(find.text('Linear · Trecho 2 de 2'), findsOneWidget);

      await tester.tap(find.byKey(const ValueKey('curva-proximo')));
      await tester.pump();
      expect(relogio.time.value, _s(2));

      // Na ultima nao ha proxima: o toque nao move nada.
      await tester.tap(find.byKey(const ValueKey('curva-proximo')));
      await tester.pump();
      expect(relogio.time.value, _s(2));

      await tester.tap(find.byKey(const ValueKey('curva-anterior')));
      await tester.pump();
      expect(relogio.time.value, _s(1));
      await tester.tap(find.byKey(const ValueKey('curva-anterior')));
      await tester.pump();
      expect(relogio.time.value, Duration.zero);
      expect(find.text('Linear · Trecho 1 de 2'), findsOneWidget);
    });

    test('a marca vizinha e a do design system (tolerancia inclusa)', () {
      final marcas = [_s(0), _s(1), _s(2)];
      expect(
        marcaVizinha(marcasLocais: marcas, agoraLocal: _s(1), direcao: -1),
        _s(0),
      );
      expect(
        marcaVizinha(marcasLocais: marcas, agoraLocal: _s(1), direcao: 1),
        _s(2),
      );
      // Em cima da marca (a menos de 8 ms) ela nao conta como vizinha.
      expect(
        marcaVizinha(marcasLocais: marcas, agoraLocal: _s(1.004), direcao: -1),
        _s(0),
      );
      expect(
        marcaVizinha(marcasLocais: marcas, agoraLocal: _s(2), direcao: 1),
        isNull,
      );
    });

    test('trechoEm: dentro, em cima da marca, na ultima e fora', () {
      final marcas = [_s(0), _s(1), _s(2)];
      expect(trechoEm(marcas, _s(.5))?.indice, 0);
      expect(trechoEm(marcas, _s(1))?.indice, 1);
      expect(trechoEm(marcas, _s(.9995))?.indice, 1,
          reason: 'o seek que para um pouco antes da marca');
      expect(trechoEm(marcas, _s(2))?.indice, 1);
      expect(trechoEm(marcas, _s(2.5)), isNull);
      expect(trechoEm([_s(0)], _s(0)), isNull);
    });
  });

  group('valores sempre finitos', () {
    test('passo de alca com lixo nao grava lixo', () {
      final curvas = [
        Easing.easeInOut,
        const Easing(x1: 0, y1: 0, x2: 0, y2: 0),
        const Easing(x1: 1, y1: 1, x2: 1, y2: 1),
        Easing.elastic,
        Easing.bounce,
        Easing.cyclic,
        Easing.hold,
      ];
      const lixo = [
        double.nan,
        double.infinity,
        double.negativeInfinity,
        1e12,
        -1e12,
        0.0,
      ];
      for (final e in curvas) {
        for (final modo in ModoDoGrafico.values) {
          for (final alca in const [0, 1]) {
            for (final x in lixo) {
              for (final y in lixo) {
                for (final overshoot in const [false, true]) {
                  final r = moverAlcaDaCurva(
                    e,
                    modo,
                    alca,
                    x,
                    y,
                    tolX: .02,
                    tolY: .02,
                    overshoot: overshoot,
                  );
                  _finita(r.curva);
                }
              }
            }
          }
        }
      }
    });

    testWidgets('arrastar para muito longe do grafico fica finito e no limite', (
      tester,
    ) async {
      final b = _bancada();
      b.e.setSegmentEase(b.id, LayerProp.position, Duration.zero, Easing.easeInOut);
      await _montar(tester, b.c, b.id);
      for (final modo in ['curva-modo-valor', 'curva-modo-velocidade']) {
        await tester.tap(find.byKey(ValueKey(modo)));
        await tester.pump();
        for (final destino in const [
          Offset(5000, -5000),
          Offset(-5000, 5000),
          Offset(3000, 3000),
        ]) {
          final inicio = _alcaNaTela(tester, 1);
          final gesto = await tester.startGesture(inicio);
          await gesto.moveBy(const Offset(4, 4));
          await gesto.moveBy(destino);
          await tester.pump();
          await gesto.up();
          await tester.pump();
          final e = _curvaDaPosicao(b.c, b.id, 0);
          _finita(e);
          expect(e.x1, inInclusiveRange(0, 1));
          expect(e.x2, inInclusiveRange(0, 1));
          if (modo == 'curva-modo-valor') {
            // Sem overshoot, o valor fica dentro de 0..1.
            expect(e.y2, inInclusiveRange(0, 1));
          }
        }
      }
      await tester.pumpAndSettle();
    });
  });

  group('as outras portas', () {
    test('parametro de efeito', () {
      final b = _bancada(posicao: const []);
      b.e.addEffect(b.id, EffectType.exposure);
      final efeito = _camada(b.c, b.id).effects.last;
      b.e.toggleEffectKeyframe(b.id, efeito.id, _s(0));
      b.e.toggleEffectKeyframe(b.id, efeito.id, _s(1));
      final trilha = TrilhaDaCurva.efeito(efeito.id);
      final l = _camada(b.c, b.id);
      expect(trilha.marcasDe(b.e, l), [_s(0), _s(1)]);
      trilha.gravar(b.e, l, _s(0), Easing.easeIn);
      final depois = _camada(b.c, b.id).effects.last;
      final animada = depois.params.values.firstWhere((p) => p.isAnimated);
      expect(animada.easeAt(_s(0)).mesmoPresetQue(Easing.easeIn), isTrue);
      expect(
        trilha.curvaDe(b.e, _camada(b.c, b.id), _s(0)).mesmoPresetQue(
          Easing.easeIn,
        ),
        isTrue,
      );
    });

    testWidgets('trilha do Time Remap, no relogio cru', (tester) async {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      final id = e.addVideoLayer(Duration.zero, '/nao/existe.mp4', 'Clipe', _s(3));
      e.definirTrilhaDeTempo(
        id,
        AnimatedDouble(0, [
          Keyframe(time: _s(0), value: 0),
          Keyframe(time: _s(2), value: 2),
        ]),
      );
      final trilha = TrilhaDaCurva.timeRemap();
      expect(trilha.marcasDe(e, _camada(c, id)), [_s(0), _s(2)]);

      await _montar(tester, c, id, trilha: trilha, tempo: _s(1));
      expect(find.byKey(_grafico), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('curva-preset-ease-in')));
      await tester.pump();
      final l = _camada(c, id) as VideoLayer;
      expect(l.timeRemap!.easeAt(_s(0)).mesmoPresetQue(Easing.easeIn), isTrue);
      // O grafico de velocidade E o Speed da trilha.
      await tester.tap(find.byKey(const ValueKey('curva-modo-velocidade')));
      await tester.pump();
      expect(
        tester.widget<GraficoDaCurva>(find.byKey(_grafico)).modo,
        ModoDoGrafico.velocidade,
      );
    });

    test('propriedade de objeto da cena 3D', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addScene3DLayer(Duration.zero);
      final id = c.read(editorControllerProvider).layers.single.id;
      e.addSceneNode(id, Element3DKind.cube);
      final no = (_camada(c, id) as Scene3DLayer).scene.nodes.last.id;
      e.toggleSceneNodeKeyframe(id, no, PropDoNo.giroY, _s(0));
      e.toggleSceneNodeKeyframe(id, no, PropDoNo.giroY, _s(1));
      final trilha = TrilhaDaCurva.noDaCena(no, PropDoNo.giroY);
      expect(trilha.marcasDe(e, _camada(c, id)), [_s(0), _s(1)]);
      trilha.gravar(e, _camada(c, id), _s(0), Easing.easeOut);
      expect(
        trilha.curvaDe(e, _camada(c, id), _s(0)).mesmoPresetQue(Easing.easeOut),
        isTrue,
      );
    });

    test('medida de caractere do Texto 3D', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addScene3DLayer(Duration.zero);
      final id = c.read(editorControllerProvider).layers.single.id;
      e.addSceneNode(id, Element3DKind.cube);
      final no = (_camada(c, id) as Scene3DLayer).scene.nodes.last.id;
      e.updateSceneNode(id, no, (n) => n.copyWith(texto3d: const Texto3D()));
      e.ajustarCaracteresDoTexto3D(id, no, [
        AjusteDeCaracteres(
          trilhas: {
            MedidaDoCaractere.z: AnimatedDouble(0, [
              Keyframe(time: _s(0), value: 0),
              Keyframe(time: _s(1), value: 50),
            ]),
          },
        ),
      ]);
      final trilha = TrilhaDaCurva.caractereDoTexto3D(
        nodeId: no,
        inicio: 0,
        fim: -1,
        medida: MedidaDoCaractere.z,
      );
      expect(trilha.marcasDe(e, _camada(c, id)), [_s(0), _s(1)]);
      trilha.gravar(e, _camada(c, id), _s(0), Easing.easeInOut);
      expect(
        trilha
            .curvaDe(e, _camada(c, id), _s(0))
            .mesmoPresetQue(Easing.easeInOut),
        isTrue,
      );
    });
  });
}
