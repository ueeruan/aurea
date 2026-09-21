import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/font_service.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/animador_de_texto.dart';
import 'package:aurea/src/features/editor/domain/caption.dart';
import 'package:aurea/src/features/editor/domain/grid_rig.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/text_anim.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/animar.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/clonar.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/estilo.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/fonte.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/forma.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/grupo.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/legendas.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/particulas.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/pontos.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/texto.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/widgets/mask_node_editor.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

// OS PAINEIS DE TEXTO, FORMA E OBJETOS montados como a casca os monta: o
// painel de 200 na base, dentro do `EscopoDoEditor`, lendo o PainelId
// aberto do provider — abrir outro painel (Fonte, Efeitos, Pontos) troca o
// que esta na tela, como no editor.

/// Os paineis desta frente. Os das outras frentes viram uma caixa com a
/// chave deles: o teste so precisa saber que a casca foi pedida.
final Map<PainelId, Widget Function(String id)> _meus = {
  PainelId.texto: (id) => PainelTexto(layerId: id),
  PainelId.fonte: (id) => PainelFonte(layerId: id),
  PainelId.estilo: (id) => PainelEstilo(layerId: id),
  PainelId.animar: (id) => PainelAnimar(layerId: id),
  PainelId.legendas: (id) => PainelLegendas(layerId: id),
  PainelId.forma: (id) => PainelForma(layerId: id),
  PainelId.pontos: (id) => PainelPontos(layerId: id),
  PainelId.particulas: (id) => PainelParticulas(layerId: id),
  PainelId.clonar: (id) => PainelClonar(layerId: id),
  PainelId.grupo: (id) => PainelGrupo(layerId: id),
};

class _Hospedeiro extends ConsumerWidget {
  const _Hospedeiro({required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final aberto = ref.watch(painelAbertoProvider);
    if (aberto == null) return const SizedBox(key: ValueKey('sem-painel'));
    final construtor = _meus[aberto];
    if (construtor == null) {
      return SizedBox(key: ValueKey('painel-${aberto.name}'));
    }
    return KeyedSubtree(
      key: ValueKey('aberto-${aberto.name}'),
      child: construtor(layerId),
    );
  }
}

class Bancada {
  Bancada(this.c, this.pb, this.id);

  final ProviderContainer c;
  final PlaybackController pb;
  final String id;

  EditorController get ctrl => c.read(editorControllerProvider.notifier);
  Layer? get camada => c.read(editorControllerProvider).layerById(id);
}

/// Monta [painel] na camada que [preparar] criar (devolve o id dela).
Future<Bancada> montar(
  WidgetTester tester, {
  required PainelId painel,
  required String Function(EditorController c, ProviderContainer p) preparar,
  Size tamanho = const Size(390, 844),
}) async {
  tester.view.physicalSize = tamanho;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final container = ProviderContainer();
  addTearDown(container.dispose);
  final ctrl = container.read(editorControllerProvider.notifier);
  final id = preparar(ctrl, container);
  container.read(selectedLayerProvider.notifier).state = id;
  container.read(painelAbertoProvider.notifier).state = painel;
  // COMO O EDITOR: trocar a selecao fecha o painel aberto.
  container.listen<String?>(selectedLayerProvider, (antes, agora) {
    if (antes != agora) {
      container.read(painelAbertoProvider.notifier).state = null;
    }
  });
  final pb = PlaybackController(
    vsync: const TestVSync(),
    durationOf: () => container.read(editorControllerProvider).duration,
  );
  addTearDown(pb.dispose);
  final videos = VideoLayerManager();
  addTearDown(videos.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Scaffold(
          body: EscopoDoEditor(
            playback: pb,
            videos: videos,
            abrirPainel: (p) =>
                container.read(painelAbertoProvider.notifier).state = p,
            fecharPainel: () =>
                container.read(painelAbertoProvider.notifier).state = null,
            child: Column(
              children: [
                const Expanded(child: SizedBox.expand()),
                SizedBox(
                  height: AureaDims.painel,
                  child: _Hospedeiro(layerId: id),
                ),
              ],
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
  return Bancada(container, pb, id);
}

String _texto(EditorController c, ProviderContainer p) {
  c.addTextLayer(Duration.zero, text: 'Oi');
  return p.read(editorControllerProvider).layers.first.id;
}

String _forma(EditorController c, ProviderContainer p) {
  c.addShapeLayer(
    Duration.zero,
    contents: [
      ShapeParametric(kind: ParamShapeKind.rect, roundness: AnimatedDouble(0)),
      ShapeFill(color: const Color(0xFFFF0000)),
    ],
  );
  return p.read(editorControllerProvider).layers.first.id;
}

TextLayer _textoDe(Bancada b) => b.camada! as TextLayer;
ShapeLayer _formaDe(Bancada b) => b.camada! as ShapeLayer;

/// Acha tambem o que esta montado fora da vista (a lista do painel monta
/// um pouco alem da dobra): quem toca rola ate la antes.
Finder _chave(String k) => find.byKey(ValueKey(k), skipOffstage: false);

/// O interruptor de uma linha `prop-<chave>`.
Finder _interruptor(String chave) => find.descendant(
  of: _chave('prop-$chave'),
  matching: find.byType(CupertinoSwitch, skipOffstage: false),
  skipOffstage: false,
);

Future<void> _aba(WidgetTester tester, PainelId p, int i) async {
  await _tocar(tester, _chave('painel-${p.name}-aba-$i'));
}

/// Tocar num controle do painel: o painel tem 200 de altura, entao a linha
/// pode estar abaixo da dobra — rola ate ela antes, como o dedo faria.
Future<void> _mostrar(WidgetTester tester, Finder f) async {
  if (f.evaluate().isEmpty) {
    // Alem da dobra e do que a lista monta adiantado: rola a lista do
    // painel (a unica vertical na bancada) ate a linha nascer.
    await tester.scrollUntilVisible(
      f,
      80,
      scrollable: find
          .byWidgetPredicate(
            (w) => w is Scrollable && w.axisDirection == AxisDirection.down,
          )
          .first,
    );
  }
  await tester.ensureVisible(f);
  await tester.pumpAndSettle();
}

Future<void> _tocar(WidgetTester tester, Finder f) async {
  await _mostrar(tester, f);
  await tester.tap(f);
  await tester.pumpAndSettle();
}

Future<void> _arrastar(WidgetTester tester, Finder f, Offset d) async {
  await _mostrar(tester, f);
  await tester.drag(f, d);
  await tester.pumpAndSettle();
}

void main() {
  group('Texto', () {
    testWidgets('editar o texto muda a camada', (tester) async {
      final b = await montar(tester, painel: PainelId.texto, preparar: _texto);
      expect(_chave('painel-texto'), findsOneWidget);
      // O campo e a primeira coisa do painel, com o texto da camada.
      expect(find.text('Oi'), findsWidgets);
      await tester.enterText(_chave('texto-campo'), 'Olá\nmundo');
      await tester.pump();
      expect(_textoDe(b).text, 'Olá\nmundo');
      // Tamanho: arrastar a linha muda o corpo, e o arrasto e UM desfazer.
      final antes = _textoDe(b).fontSize;
      await _arrastar(tester, _chave('prop-tamanho'), const Offset(80, 0));
      await tester.pumpAndSettle();
      expect(_textoDe(b).fontSize, greaterThan(antes));
      b.ctrl.undo();
      expect(_textoDe(b).fontSize, antes);
      // Alinhamento e atalho da fonte.
      await _tocar(tester, _chave('alinhar-left'));
      await tester.pump();
      expect(_textoDe(b).alinhamento, TextAlign.left);
      await _tocar(tester, _chave('texto-fonte'));
      await tester.pumpAndSettle();
      expect(b.c.read(painelAbertoProvider), PainelId.fonte);
      expect(tester.takeException(), isNull);
    });

    testWidgets('trocar a fonte muda a familia (e a busca filtra)', (
      tester,
    ) async {
      FontService.instance.registrarSemArquivo('Fonte Teste Um');
      FontService.instance.registrarSemArquivo('Outra Letra');
      final b = await montar(tester, painel: PainelId.fonte, preparar: _texto);
      expect(_chave('painel-fonte'), findsOneWidget);
      await tester.enterText(_chave('fonte-busca'), 'teste');
      await tester.pumpAndSettle();
      expect(_chave('fonte-todas-Outra Letra'), findsNothing);
      await _tocar(tester, _chave('fonte-todas-Fonte Teste Um'));
      await tester.pump();
      expect(_textoDe(b).fontFamily, 'Fonte Teste Um');
      await tester.enterText(_chave('fonte-busca'), '');
      await tester.pumpAndSettle();
      // A usada vai para as recentes, no topo da lista.
      expect(_chave('fonte-rec-Fonte Teste Um'), findsOneWidget);
      await _tocar(tester, _chave('fonte-todas-padrao'));
      await tester.pump();
      expect(_textoDe(b).fontFamily, isNull);
      expect(tester.takeException(), isNull);
    });
  });

  group('Estilo', () {
    testWidgets('espacamento: arrasto cria o animador (um desfazer) e o '
        'losango crava marcas', (tester) async {
      final b = await montar(tester, painel: PainelId.estilo, preparar: _texto);
      expect(animadorDoEspacamento(_textoDe(b)), isNull);
      await _arrastar(tester, _chave('prop-espacamento'), const Offset(80, 0));
      await tester.pumpAndSettle();
      final a = animadorDoEspacamento(_textoDe(b))!;
      expect(
        valorDaPropriedade(a, TextAnimProp.tracking, Duration.zero),
        greaterThan(0),
      );
      // O gesto inteiro (criar + mexer) e um passo so.
      b.ctrl.undo();
      expect(animadorDoEspacamento(_textoDe(b)), isNull);

      await _tocar(tester, _chave('kf-espacamento'));
      await tester.pumpAndSettle();
      TextAnimator anim() => animadorDoEspacamento(_textoDe(b))!;
      expect(
        propriedadeDoAnimador(anim(), TextAnimProp.tracking)!.value.keyframes,
        hasLength(1),
      );
      b.pb.seek(const Duration(seconds: 1));
      await tester.pumpAndSettle();
      await _tocar(tester, _chave('kf-espacamento'));
      await tester.pumpAndSettle();
      final marcas = propriedadeDoAnimador(
        anim(),
        TextAnimProp.tracking,
      )!.value.keyframes;
      expect(marcas.map((k) => k.time), [
        Duration.zero,
        const Duration(seconds: 1),
      ]);
      // Negrito na mesma aba.
      final negrito = _textoDe(b).bold;
      await _tocar(tester, _interruptor('negrito'));
      await tester.pump();
      expect(_textoDe(b).bold, !negrito);
      expect(tester.takeException(), isNull);
    });

    testWidgets('contorno e sombra ligam; a sombra anima pelo losango', (
      tester,
    ) async {
      final b = await montar(tester, painel: PainelId.estilo, preparar: _texto);
      LayerStyles estilos() =>
          b.c.read(editorControllerProvider).metaOf(b.id).styles;

      await _aba(tester, PainelId.estilo, 1);
      expect(_chave('prop-contorno-largura'), findsNothing);
      await _tocar(tester, _interruptor('contorno'));
      await tester.pumpAndSettle();
      expect(estilos().stroke?.enabled, isTrue);
      expect(_chave('prop-contorno-largura'), findsOneWidget);
      final largura = estilos().stroke!.width.valueAt(Duration.zero);
      await _arrastar(
        tester,
        _chave('prop-contorno-largura'),
        const Offset(60, 0),
      );
      await tester.pumpAndSettle();
      expect(
        estilos().stroke!.width.valueAt(Duration.zero),
        greaterThan(largura),
      );
      await _tocar(tester, _interruptor('contorno'));
      await tester.pumpAndSettle();
      expect(estilos().bordas, isEmpty);

      await _aba(tester, PainelId.estilo, 2);
      await _tocar(tester, _interruptor('sombra'));
      await tester.pumpAndSettle();
      expect(estilos().dropShadow?.enabled, isTrue);
      // Marca em 0; em 1 s o arrasto fica PENDENTE (sem marca nova) e o
      // losango crava.
      await _tocar(tester, _chave('kf-sombra-distancia'));
      await tester.pumpAndSettle();
      expect(estilos().dropShadow!.distance.keyframes, hasLength(1));
      b.pb.seek(const Duration(seconds: 1));
      await tester.pumpAndSettle();
      await _arrastar(
        tester,
        _chave('prop-sombra-distancia'),
        const Offset(60, 0),
      );
      await tester.pumpAndSettle();
      expect(b.c.read(edicaoPendenteProvider), isNotNull);
      expect(estilos().dropShadow!.distance.keyframes, hasLength(1));
      await _tocar(tester, _chave('kf-sombra-distancia'));
      await tester.pumpAndSettle();
      expect(b.c.read(edicaoPendenteProvider), isNull);
      expect(estilos().dropShadow!.distance.keyframes, hasLength(2));
      expect(tester.takeException(), isNull);
    });

    testWidgets('a caixa atras do texto nasce abaixo dele e sai', (
      tester,
    ) async {
      final b = await montar(tester, painel: PainelId.estilo, preparar: _texto);
      await _aba(tester, PainelId.estilo, 3);
      await _tocar(tester, _interruptor('fundo'));
      await tester.pumpAndSettle();
      final camadas = b.c.read(editorControllerProvider).layers;
      expect(camadas, hasLength(2));
      final iTexto = camadas.indexWhere((l) => l.id == b.id);
      final caixa = camadas[iTexto + 1];
      expect(caixa, isA<ShapeLayer>());
      expect(
        b.c
            .read(editorControllerProvider)
            .metaOf(caixa.id)
            .container
            ?.targetLayerId,
        b.id,
      );
      // A selecao continua no texto e o painel continua aberto, na aba.
      expect(b.c.read(selectedLayerProvider), b.id);
      expect(b.c.read(painelAbertoProvider), PainelId.estilo);
      expect(_chave('prop-fundo-margem-x'), findsOneWidget);
      await _tocar(tester, _interruptor('fundo'));
      await tester.pumpAndSettle();
      expect(b.c.read(editorControllerProvider).layers, hasLength(1));
      expect(tester.takeException(), isNull);
    });
  });

  group('Animar', () {
    testWidgets('predefinicao de entrada e receita do animador aplicam', (
      tester,
    ) async {
      final b = await montar(tester, painel: PainelId.animar, preparar: _texto);
      final entrada = textAnimsForSlot(TextAnimSlot.entrada).first;
      await _tocar(tester, _chave('animar-entrada-${entrada.id}'));
      await tester.pumpAndSettle();
      expect(
        _textoDe(b).anims
            .where((a) => a.slot == TextAnimSlot.entrada)
            .single
            .specId,
        entrada.id,
      );
      await _mostrar(tester, _chave('prop-animar-duracao'));
      await _tocar(tester, _chave('animar-entrada-nenhuma'));
      await tester.pumpAndSettle();
      expect(_textoDe(b).anims, isEmpty);

      await _aba(tester, PainelId.animar, 3);
      await _tocar(tester, _chave('animar-receita-pop'));
      await tester.pumpAndSettle();
      expect(_textoDe(b).animators.map((a) => a.name), ['Pop']);
      // Trocar de receita troca, nao empilha.
      await _tocar(tester, _chave('animar-receita-bounce'));
      await tester.pumpAndSettle();
      expect(_textoDe(b).animators.map((a) => a.name), ['Bounce']);
      // A fileira rola de lado: volta ao comeco para achar "Nenhuma".
      await tester.drag(_chave('animar-receita-bounce'), const Offset(600, 0));
      await tester.pumpAndSettle();
      await _tocar(tester, _chave('animar-receita-nenhuma'));
      await tester.pumpAndSettle();
      expect(_textoDe(b).animators, isEmpty);
      expect(tester.takeException(), isNull);
    });

    testWidgets('o atalho do Animador de Texto abre Efeitos', (tester) async {
      final b = await montar(tester, painel: PainelId.animar, preparar: _texto);
      await _aba(tester, PainelId.animar, 3);
      await _tocar(tester, _chave('animar-efeito'));
      await tester.pumpAndSettle();
      expect(b.c.read(painelAbertoProvider), PainelId.efeitos);
      expect(_chave('painel-efeitos'), findsOneWidget);
    });
  });

  group('Forma e pontos', () {
    testWidgets('forma muda o raio (com losango) e a cor', (tester) async {
      final b = await montar(tester, painel: PainelId.forma, preparar: _forma);
      ShapeParametric sp() =>
          _formaDe(b).contents.whereType<ShapeParametric>().first;
      await _arrastar(
        tester,
        _chave('prop-forma-roundness'),
        const Offset(60, 0),
      );
      await tester.pumpAndSettle();
      expect(sp().roundness.valueAt(Duration.zero), greaterThan(0));
      await _tocar(tester, _chave('kf-forma-roundness'));
      await tester.pumpAndSettle();
      expect(sp().roundness.keyframes, hasLength(1));

      // Trocar para estrela troca os numeros da ficha.
      b.ctrl.setShapeParamKind(b.id, ParamShapeKind.star);
      await tester.pumpAndSettle();
      expect(_chave('prop-forma-roundness'), findsNothing);
      expect(_chave('prop-forma-outerRadius'), findsOneWidget);
      final raio = sp().outerRadius.valueAt(Duration.zero);
      await _arrastar(
        tester,
        _chave('prop-forma-outerRadius'),
        const Offset(60, 0),
      );
      await tester.pumpAndSettle();
      expect(sp().outerRadius.valueAt(Duration.zero), greaterThan(raio));

      await _aba(tester, PainelId.forma, 1);
      await _tocar(tester, _chave('cor-forma-cor'));
      await tester.pumpAndSettle();
      await tester.enterText(_chave('cor-hex'), '00FF00');
      await tester.pumpAndSettle();
      await _tocar(tester, _chave('cor-pronto'));
      await tester.pumpAndSettle();
      expect(
        _formaDe(b).contents.whereType<ShapeFill>().first.color.toARGB32(),
        0xFF00FF00,
      );
      // Traco liga e ganha espessura com losango.
      await _aba(tester, PainelId.forma, 2);
      await _tocar(tester, _interruptor('forma-traco'));
      await tester.pumpAndSettle();
      expect(_formaDe(b).contents.whereType<ShapeStroke>(), hasLength(1));
      await _tocar(tester, _chave('kf-forma-traco-width'));
      await tester.pumpAndSettle();
      expect(
        _formaDe(b).contents.whereType<ShapeStroke>().first.width.keyframes,
        hasLength(1),
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('o lapis da forma abre a edicao de pontos, e o trackpad '
        'crava pontos', (tester) async {
      final b = await montar(tester, painel: PainelId.forma, preparar: _forma);
      await _tocar(tester, _chave('forma-pontos'));
      await tester.pumpAndSettle();
      expect(b.c.read(painelAbertoProvider), PainelId.pontos);
      final alvo = b.c.read(pathEditTargetProvider);
      expect(alvo?.layerId, b.id);
      expect(_chave('painel-pontos'), findsOneWidget);
      expect(_chave('pontos-trackpad'), findsOneWidget);
      int pontos() => b.ctrl
          .shapeBezierOf(b.id, alvo!.maskId)!
          .path
          .valueAt(Duration.zero)
          .vertices
          .length;
      final antes = pontos();
      await _tocar(tester, _chave('pontos-modo-add'));
      await tester.pump();
      await tester.drag(_chave('pontos-trackpad'), const Offset(30, 10));
      await tester.pump();
      await tester.tap(_chave('pontos-trackpad'));
      await tester.pump(const Duration(milliseconds: 400));
      expect(pontos(), antes + 1);
      // O losango do cabecalho crava a forma do contorno.
      await _tocar(tester, _chave('pontos-kf'));
      await tester.pump();
      expect(
        b.ctrl.shapeBezierOf(b.id, alvo!.maskId)!.path.keyframes,
        hasLength(1),
      );
      // Fechar solta o alvo do palco.
      await _tocar(tester, _chave('painel-pontos-fechar'));
      await tester.pumpAndSettle();
      expect(b.c.read(pathEditTargetProvider), isNull);
      expect(tester.takeException(), isNull);
    });
  });

  group('Objetos', () {
    testWidgets('legendas: corrigir a fala e escolher estilo pronto', (
      tester,
    ) async {
      final b = await montar(
        tester,
        painel: PainelId.legendas,
        preparar: (c, p) {
          c.addCaptionLayer([
            Cue(
              start: Duration.zero,
              end: const Duration(seconds: 1),
              text: 'oi',
            ),
            Cue(
              start: const Duration(seconds: 1),
              end: const Duration(milliseconds: 1800),
              text: 'gente',
            ),
          ]);
          return p.read(editorControllerProvider).layers.first.id;
        },
      );
      final cue = (b.camada! as CaptionLayer).cues.first;
      await tester.enterText(_chave('fala-${cue.id}-texto'), 'olá');
      await tester.pump();
      final corrigida = (b.camada! as CaptionLayer).cues.first;
      expect(corrigida.text, 'olá');
      expect(corrigida.locked, isTrue);

      await _aba(tester, PainelId.legendas, 1);
      await _tocar(tester, _chave('legenda-pronto-Viral'));
      await tester.pumpAndSettle();
      expect((b.camada! as CaptionLayer).highlight.ativo, isTrue);
      // Trocar o estilo NAO duplica a camada.
      expect(b.c.read(editorControllerProvider).layers, hasLength(1));
      expect(tester.takeException(), isNull);
    });

    testWidgets('clonar: escolher camadas cria a grade; o espaco anima', (
      tester,
    ) async {
      late String forma;
      final b = await montar(
        tester,
        painel: PainelId.clonar,
        preparar: (c, p) {
          c.addShapeLayer(Duration.zero);
          forma = p.read(editorControllerProvider).layers.first.id;
          c.addNullLayer(Duration.zero);
          return p.read(editorControllerProvider).layers.first.id;
        },
      );
      await _tocar(tester, _chave('clonar-camadas'));
      await tester.pumpAndSettle();
      await _tocar(tester, _chave('clonar-camada-$forma'));
      await tester.pumpAndSettle();
      await _tocar(tester, _chave('folha-fechar'));
      await tester.pumpAndSettle();
      expect((b.camada! as NullLayer).grid?.assets, [forma]);
      await _tocar(tester, _chave('kf-clonar-spacingX'));
      await tester.pumpAndSettle();
      expect((b.camada! as NullLayer).grid!.spacingX.keyframes, hasLength(1));
      expect(tester.takeException(), isNull);
    });

    testWidgets('grupo: colapsar e duracao interna', (tester) async {
      final b = await montar(
        tester,
        painel: PainelId.grupo,
        preparar: (c, p) {
          c.addShapeLayer(Duration.zero);
          c.addTextLayer(Duration.zero);
          c.groupLayers([
            for (final l in p.read(editorControllerProvider).layers) l.id,
          ]);
          return p
              .read(editorControllerProvider)
              .layers
              .whereType<GroupLayer>()
              .single
              .id;
        },
      );
      await _tocar(tester, _interruptor('grupo-colapsar'));
      await tester.pump();
      expect((b.camada! as GroupLayer).collapse, isTrue);
      await _arrastar(
        tester,
        _chave('prop-grupo-duracao'),
        const Offset(60, 0),
      );
      await tester.pumpAndSettle();
      expect((b.camada! as GroupLayer).sourceDuration, isNotNull);
      expect(tester.takeException(), isNull);
    });
  });

  group('360x640', () {
    final casos =
        <(PainelId, String Function(EditorController, ProviderContainer), int)>[
          (PainelId.texto, _texto, 0),
          (PainelId.fonte, _texto, 0),
          (PainelId.estilo, _texto, 5),
          (PainelId.animar, _texto, 4),
          (PainelId.forma, _forma, 4),
          (
            PainelId.legendas,
            (c, p) {
              c.addCaptionLayer([
                Cue(
                  start: Duration.zero,
                  end: const Duration(seconds: 1),
                  text: 'uma fala bem comprida para ver se o campo aguenta',
                ),
              ]);
              return p.read(editorControllerProvider).layers.first.id;
            },
            2,
          ),
          (
            PainelId.particulas,
            (c, p) {
              c.addParticulasLayer(Duration.zero);
              return p.read(editorControllerProvider).layers.first.id;
            },
            5,
          ),
          (
            PainelId.grupo,
            (c, p) {
              c.addShapeLayer(Duration.zero);
              c.addTextLayer(Duration.zero);
              c.groupLayers([
                for (final l in p.read(editorControllerProvider).layers) l.id,
              ]);
              return p.read(editorControllerProvider).layers.first.id;
            },
            0,
          ),
        ];
    for (final (painel, preparar, abas) in casos) {
      testWidgets('${painel.name} abre todas as abas sem estourar', (
        tester,
      ) async {
        await montar(
          tester,
          painel: painel,
          preparar: preparar,
          tamanho: const Size(360, 640),
        );
        expect(_chave('painel-${painel.name}'), findsOneWidget);
        expect(tester.takeException(), isNull, reason: painel.name);
        for (var i = 1; i < abas; i++) {
          await _aba(tester, painel, i);
          expect(tester.takeException(), isNull, reason: '${painel.name} $i');
        }
      });
    }

    testWidgets('estilo com tudo ligado', (tester) async {
      await montar(
        tester,
        painel: PainelId.estilo,
        preparar: _texto,
        tamanho: const Size(360, 640),
      );
      for (final (aba, chave) in const [
        (1, 'contorno'),
        (2, 'sombra'),
        (2, 'brilho'),
        (3, 'fundo'),
        (4, 'degrade'),
      ]) {
        await _aba(tester, PainelId.estilo, aba);
        await _tocar(tester, _interruptor(chave));
        await tester.pumpAndSettle();
        expect(tester.takeException(), isNull, reason: chave);
        // Rola ate o fim da aba: as linhas de baixo tambem cabem.
        await tester.drag(
          find.byKey(ValueKey('estilo-aba-$aba')),
          const Offset(0, -600),
        );
        await tester.pumpAndSettle();
        expect(tester.takeException(), isNull, reason: '$chave rolada');
      }
    });

    testWidgets('pontos abre sem estourar', (tester) async {
      await montar(
        tester,
        painel: PainelId.forma,
        preparar: _forma,
        tamanho: const Size(360, 640),
      );
      await _tocar(tester, _chave('forma-pontos'));
      await tester.pumpAndSettle();
      expect(_chave('pontos-trackpad'), findsOneWidget);
      for (final modo in ['handle', 'add', 'move']) {
        await _tocar(tester, _chave('pontos-modo-$modo'));
        await tester.pumpAndSettle();
        expect(tester.takeException(), isNull, reason: modo);
      }
    });

    testWidgets('clonar com grade abre as tres abas sem estourar', (
      tester,
    ) async {
      await montar(
        tester,
        painel: PainelId.clonar,
        tamanho: const Size(360, 640),
        preparar: (c, p) {
          c.addShapeLayer(Duration.zero);
          final forma = p.read(editorControllerProvider).layers.first.id;
          c.addNullLayer(Duration.zero);
          final nulo = p.read(editorControllerProvider).layers.first.id;
          c.setGridAssets(nulo, [forma]);
          c.updateGrid(nulo, (g) => g.copyWith(proximity: ProximityGroup()));
          return nulo;
        },
      );
      for (var i = 1; i < 3; i++) {
        await _aba(tester, PainelId.clonar, i);
        expect(tester.takeException(), isNull, reason: 'aba $i');
      }
    });
  });
}
