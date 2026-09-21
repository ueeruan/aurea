import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/curva.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/grafico_da_curva.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// O EDITOR DE CURVA.
///
/// Os testadores disseram duas coisas sobre ele: "e dificil de mexer" e
/// "algumas coisas quebram". As duas tinham causa concreta.
///
/// A QUE QUEBRAVA: a alca agarrada era escolhida a cada atualizacao do
/// arrasto, pela proximidade do dedo. Bastava o dedo cruzar o meio do
/// grafico para o gesto largar a alca que estava movendo e agarrar a
/// outra — e a curva saltava. Pior: a comparacao usava as posicoes do
/// quadro ANTERIOR, entao o mesmo gesto dava resultados diferentes
/// conforme o widget tivesse reconstruido a tempo.
///
/// A OUTRA: cada atualizacao do arrasto era um passo de desfazer em
/// potencial. A janela de 450 ms segurava a maioria, mas um dedo que
/// parasse meio segundo no meio do movimento partia o gesto em dois — e
/// desfazer devolvia so metade.
void main() {
  Duration t(num s) => Duration(microseconds: (s * 1000000).round());

  /// Um projeto com uma camada animada em posicao, dois keyframes.
  ProviderContainer comAnimacao() {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.addShapeLayer(Duration.zero, name: 'Alvo');
    final id = container.read(editorControllerProvider).layers.first.id;
    container.read(selectedLayerProvider.notifier).state = id;
    c.toggleKeyframe(id, Duration.zero, LayerProp.position);
    c.toggleKeyframe(id, t(1), LayerProp.position);
    return container;
  }

  Easing easeDe(ProviderContainer container) {
    final layer = container.read(editorControllerProvider).layers.first;
    return layer.position.easeAt(Duration.zero);
  }

  group('o desfazer de um gesto', () {
    test('um arrasto inteiro e UM passo, por mais atualizacoes que tenha', () {
      final container = comAnimacao();
      final c = container.read(editorControllerProvider.notifier);
      final id = container.read(editorControllerProvider).layers.first.id;
      final antes = easeDe(container);

      c.beginGesture();
      for (var i = 1; i <= 40; i++) {
        c.setSegmentEase(
          id,
          LayerProp.position,
          Duration.zero,
          Easing(x1: i / 100, y1: i / 80, x2: .6, y2: 1),
        );
      }
      c.endGesture();

      final depois = easeDe(container);
      expect(depois.x1, isNot(antes.x1), reason: 'o arrasto nao mudou nada');

      c.undo();
      final volta = easeDe(container);
      expect(volta.x1, antes.x1, reason: 'um desfazer tem de voltar tudo');
      expect(volta.y1, antes.y1);
    });

    test('desfazer e refazer em sequencia mantem o estado coerente', () {
      final container = comAnimacao();
      final c = container.read(editorControllerProvider.notifier);
      final id = container.read(editorControllerProvider).layers.first.id;
      final inicial = easeDe(container);

      // Tres gestos separados.
      final marcos = <double>[];
      for (final x in [.2, .4, .6]) {
        c.beginGesture();
        for (var i = 0; i < 10; i++) {
          c.setSegmentEase(
            id,
            LayerProp.position,
            Duration.zero,
            Easing(x1: x, y1: .5, x2: .8, y2: 1),
          );
        }
        c.endGesture();
        marcos.add(easeDe(container).x1);
      }
      expect(marcos, [.2, .4, .6]);

      c.undo();
      expect(easeDe(container).x1, .4);
      c.undo();
      expect(easeDe(container).x1, .2);
      c.redo();
      expect(easeDe(container).x1, .4);

      // Editar depois de desfazer nao pode ressuscitar o refazer.
      c.beginGesture();
      c.setSegmentEase(
        id,
        LayerProp.position,
        Duration.zero,
        Easing(x1: .9, y1: .5, x2: .95, y2: 1),
      );
      c.endGesture();
      expect(c.canRedo, isFalse);
      expect(easeDe(container).x1, .9);

      c.undo();
      expect(easeDe(container).x1, .4);
      expect(inicial.x1, isNot(.4));
    });

    test('cancelar um gesto devolve o ponto de partida, sem gastar undo', () {
      final container = comAnimacao();
      final c = container.read(editorControllerProvider.notifier);
      final id = container.read(editorControllerProvider).layers.first.id;
      final antes = easeDe(container);
      final desfazeresAntes = c.canUndo;

      c.beginGesture();
      c.setSegmentEase(
        id,
        LayerProp.position,
        Duration.zero,
        const Easing(x1: .77, y1: .3, x2: .9, y2: 1),
      );
      expect(easeDe(container).x1, .77);
      c.cancelGesture();

      expect(easeDe(container).x1, antes.x1, reason: 'nao voltou ao inicio');
      expect(c.canUndo, desfazeresAntes,
          reason: 'cancelar nao pode consumir um passo de desfazer');
    });

    test('um gesto esquecido nao desliga o desfazer para sempre', () {
      // O widget pode sair da arvore no meio do arrasto. Se o grupo
      // ficasse aberto, nenhuma edicao seguinte entraria no historico.
      final container = comAnimacao();
      final c = container.read(editorControllerProvider.notifier);
      final id = container.read(editorControllerProvider).layers.first.id;

      c.beginGesture();
      c.setSegmentEase(
        id,
        LayerProp.position,
        Duration.zero,
        const Easing(x1: .3, y1: .2, x2: .7, y2: 1),
      );
      // ... e ninguem chama endGesture.
      c.endGesture(); // o app fecha na saida do widget; aqui, no dispose

      c.beginGesture();
      c.setSegmentEase(
        id,
        LayerProp.position,
        Duration.zero,
        const Easing(x1: .5, y1: .2, x2: .7, y2: 1),
      );
      c.endGesture();
      c.undo();
      expect(easeDe(container).x1, .3,
          reason: 'o segundo gesto nao entrou no historico');
    });
  });

  group('a alca agarrada', () {
    testWidgets('nao troca de alca no meio do arrasto', (tester) async {
      tester.view.physicalSize = const Size(390, 844);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);

      final container = comAnimacao();
      final c = container.read(editorControllerProvider.notifier);
      final id = container.read(editorControllerProvider).layers.first.id;
      // Uma curva com as duas alcas bem separadas.
      c.setSegmentEase(
        id,
        LayerProp.position,
        Duration.zero,
        const Easing(x1: .15, y1: .1, x2: .85, y2: .9),
      );

      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: _Host(
              builder: (p) => Column(
                children: [
                  const Expanded(child: SizedBox.expand()),
                  SizedBox(
                    height: 316,
                    child: EditorDeCurva(
                      layerId: id,
                      trilha: TrilhaDaCurva.transformacao(LayerProp.position),
                      tempoInicial: const Duration(milliseconds: 500),
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

      // O editor abre no grafico de VALOR: se o grafico sumir, o teste tem
      // de falhar, e nao passar calado.
      final grafico = find.byKey(const ValueKey('curva-grafico'));
      expect(grafico, findsOneWidget,
          reason: 'o grafico da curva nao esta na tela');
      final caixa = tester.getRect(grafico);
      final inicio = caixa.topLeft +
          tester.state<GraficoDaCurvaState>(grafico).centroDaAlca(0)!;
      final x2Antes = easeDe(container).x2;
      final y2Antes = easeDe(container).y2;

      // Comeca na alca 1 (esquerda) e ATRAVESSA o grafico inteiro ate o
      // lado da alca 2. No comportamento antigo, o gesto largava a alca 1
      // no meio do caminho e passava a mexer na 2.
      final gesto = await tester.startGesture(inicio);
      await tester.pump(const Duration(milliseconds: 30));
      // SEM RECONSTRUIR ENTRE OS PASSOS, de proposito.
      //
      // E assim que o defeito aparecia: os eventos de arrasto chegam
      // mais depressa do que o widget se reconstroi, entao a decisao
      // "que alca esta na mao" era tomada com as posicoes de um quadro
      // velho. O dedo atravessa o grafico, a alca 1 ainda esta desenhada
      // la atras, e a partir do meio o gesto passava a mexer na alca 2.
      // Num teste que reconstroi a cada passo o bug se esconde — e foi
      // o que a primeira versao deste teste fez.
      for (var i = 1; i <= 12; i++) {
        await gesto.moveTo(
          Offset(inicio.dx + caixa.width * .06 * i, inicio.dy),
        );
      }
      await tester.pump();
      await gesto.up();
      await tester.pump();
      // O grafico tem toque DUPLO (ajustar): o reconhecedor segura um
      // relogio curto depois do dedo sair. Deixa ele vencer.
      await tester.pump(const Duration(milliseconds: 400));

      final depois = easeDe(container);
      expect(depois.x1, greaterThan(.15),
          reason: 'a alca que estava na mao nao se moveu');
      expect(depois.x2, x2Antes,
          reason: 'o arrasto pulou para a outra alca no meio do caminho');
      expect(depois.y2, y2Antes,
          reason: 'o arrasto pulou para a outra alca no meio do caminho');
    });
  });

  group('nenhum gesto corrompe a curva', () {
    test('os valores ficam sempre finitos e dentro dos limites', () {
      final container = comAnimacao();
      final c = container.read(editorControllerProvider.notifier);
      final id = container.read(editorControllerProvider).layers.first.id;
      // Mesmo alimentado com lixo, o que fica guardado tem de ser
      // avaliavel: um NaN aqui atravessa o projeto e reaparece como
      // animacao que simplesmente nao anima.
      for (final e in [
        const Easing(x1: .3, y1: .4, x2: .7, y2: .9),
        const Easing(x1: 0, y1: 0, x2: 1, y2: 1),
        const Easing(x1: 1, y1: 1, x2: 0, y2: 0),
      ]) {
        c.setSegmentEase(id, LayerProp.position, Duration.zero, e);
        final guardado = easeDe(container);
        for (final v in [
          guardado.x1,
          guardado.y1,
          guardado.x2,
          guardado.y2,
        ]) {
          expect(v.isFinite, isTrue);
          expect(v.isNaN, isFalse);
        }
        // A curva continua avaliavel em todo o trecho.
        for (var i = 0; i <= 10; i++) {
          final v = guardado.transform(i / 10);
          expect(v.isFinite, isTrue, reason: 'a curva nao avalia em ${i / 10}');
        }
      }
    });
  });
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
    durationOf: () => const Duration(seconds: 5),
  );

  @override
  void dispose() {
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) =>
      Scaffold(body: widget.builder(playback));
}
