import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/core/theme/aurea_paleta.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_ds.dart';

/// Um deslizante controlado, para o teste ler o valor que o dedo deu.
class _Controlado extends StatefulWidget {
  const _Controlado({
    super.key,
    required this.inicial,
    required this.construir,
  });

  final double inicial;
  final Widget Function(double valor, ValueChanged<double> mudar) construir;

  @override
  State<_Controlado> createState() => _ControladoState();
}

class _ControladoState extends State<_Controlado> {
  late double valor = widget.inicial;

  @override
  Widget build(BuildContext context) =>
      widget.construir(valor, (v) => setState(() => valor = v));
}

double _valorDe(WidgetTester tester) =>
    tester.state<_ControladoState>(find.byType(_Controlado)).valor;

void main() {
  group('AureaSlider', () {
    testWidgets('arrastar para a DIREITA aumenta (com e sem faixa)', (
      tester,
    ) async {
      for (final faixa in [true, false]) {
        await montarDs(
          tester,
          _Controlado(
            key: ValueKey('faixa-$faixa'),
            inicial: 50,
            construir: (v, mudar) => SizedBox(
              width: 300,
              child: AureaSlider(
                valor: v,
                aoMudar: mudar,
                min: faixa ? 0 : double.negativeInfinity,
                max: faixa ? 100 : double.infinity,
              ),
            ),
          ),
        );
        await tester.drag(find.byType(AureaSlider), const Offset(60, 0));
        await tester.pumpAndSettle();
        final depois = _valorDe(tester);
        expect(depois, greaterThan(50), reason: 'faixa=$faixa, direita');
        await tester.drag(find.byType(AureaSlider), const Offset(-120, 0));
        await tester.pumpAndSettle();
        expect(_valorDe(tester), lessThan(depois), reason: 'faixa=$faixa');
      }
    });

    testWidgets('com faixa, a alca anda COM o dedo (1 px = 1 px)', (
      tester,
    ) async {
      await montarDs(
        tester,
        _Controlado(
          inicial: 0,
          construir: (v, mudar) => SizedBox(
            width: 325,
            child: AureaSlider(valor: v, aoMudar: mudar, min: 0, max: 300),
          ),
        ),
      );
      // Largura util = 325 - 25 (a alca) = 300 px para 300 unidades.
      await tester.drag(find.byType(AureaSlider), const Offset(100, 0));
      await tester.pumpAndSettle();
      // O arrasto perde o trecho do "slop" antes de reconhecer; a conta e
      // 1 unidade por pixel, entao o valor fica perto de 100.
      expect(_valorDe(tester), inInclusiveRange(70, 100));
    });

    testWidgets('com faixa desenha trilho, PREENCHIMENTO e alca', (
      tester,
    ) async {
      await montarDs(
        tester,
        SizedBox(
          width: 300,
          child: AureaSlider(valor: 50, aoMudar: (_) {}, min: 0, max: 100),
        ),
      );
      expect(
        find.byType(AureaSlider),
        paints
          ..rrect()
          ..rrect(color: AureaCores.destaque)
          ..circle(),
      );
      // O preenchimento vai do inicio ate o meio: metade do trilho util.
      final pintor = tester
          .widgetList<CustomPaint>(
            find.descendant(
              of: find.byType(AureaSlider),
              matching: find.byType(CustomPaint),
            ),
          )
          .map((c) => c.painter)
          .whereType<PintorDoAureaSlider>()
          .single;
      final l = pintor.leitura(const Size(300, 44))!;
      expect(l.de, closeTo(12.5, .01));
      expect(l.ate, closeTo(12.5 + 275 / 2, .01));
      expect(l.alcaX, closeTo(l.ate, .01));
    });

    testWidgets('faixa que cruza o zero enche A PARTIR do zero', (
      tester,
    ) async {
      const pintor = PintorDoAureaSlider(
        valor: -50,
        min: -100,
        max: 100,
        porPixel: 1,
        habilitado: true,
        trilho: Color(0xFF000000),
        cheio: Color(0xFF000000),
        alca: Color(0xFF000000),
        risco: Color(0xFF000000),
      );
      final l = pintor.leitura(const Size(225, 44))!;
      // util = 200; zero no meio (12,5 + 100); -50 a um quarto.
      expect(l.ate, closeTo(112.5, .01));
      expect(l.de, closeTo(62.5, .01));
    });

    testWidgets('sem faixa nao desenha preenchimento (so riscos)', (
      tester,
    ) async {
      await montarDs(
        tester,
        SizedBox(width: 300, child: AureaSlider(valor: 10, aoMudar: (_) {})),
      );
      expect(find.byType(AureaSlider), isNot(paints..rrect()));
      expect(find.byType(AureaSlider), paints..line());
    });

    testWidgets('um arrasto avisa comeco e fim (um passo de desfazer)', (
      tester,
    ) async {
      var comecos = 0;
      var fins = 0;
      await montarDs(
        tester,
        SizedBox(
          width: 300,
          child: AureaSlider(
            valor: 0,
            aoMudar: (_) {},
            aoComecar: () => comecos++,
            aoTerminar: () => fins++,
          ),
        ),
      );
      await tester.drag(find.byType(AureaSlider), const Offset(80, 0));
      await tester.pumpAndSettle();
      expect((comecos, fins), (1, 1));
    });
  });

  group('AureaValueField', () {
    test('o texto nao come zeros da parte inteira (100 com 0 casas)', () {
      expect(AureaValueField.texto(100, 0, '%'), '100%');
      expect(AureaValueField.texto(120, 0, ''), '120');
      expect(AureaValueField.texto(12.5, 2, ''), '12.5');
      expect(AureaValueField.texto(3, 1, '°'), '3°');
      expect(AureaValueField.texto(-0.0001, 1, ''), '0');
    });

    testWidgets('caixa de 56 e o toque abre o teclado do app', (tester) async {
      await montarDs(
        tester,
        _Controlado(
          inicial: 12.5,
          construir: (v, mudar) => AureaValueField(
            valor: v,
            aoMudar: mudar,
            unidade: '%',
            min: 0,
            max: 100,
          ),
        ),
      );
      expect(tester.getSize(find.byType(AureaValueField)).width, 56);
      expect(find.text('12.5%'), findsOneWidget);
      await tester.tap(find.byType(AureaValueField));
      await tester.pumpAndSettle();
      expect(find.byType(TecladoNumerico), findsOneWidget);
      // Apaga tudo (a selecao inicial cobre o texto) e digita 42.
      await tester.tap(find.byKey(const ValueKey('tecla-apagar')));
      await tester.tap(find.byKey(const ValueKey('tecla-4')));
      await tester.tap(find.byKey(const ValueKey('tecla-2')));
      await tester.pump();
      await tester.tap(find.text('OK'));
      await tester.pumpAndSettle();
      expect(find.byType(TecladoNumerico), findsNothing);
      expect(_valorDe(tester), 42);
    });
  });

  group('AureaKeyframeButton', () {
    Future<void> montarLosango(
      WidgetTester tester, {
      required bool animado,
      required bool aqui,
      VoidCallback? toggle,
      VoidCallback? anterior,
      VoidCallback? proximo,
    }) => montarDs(
      tester,
      AureaKeyframeButton(
        estado: KeyframeState(
          animated: animado,
          here: aqui,
          onToggle: toggle ?? () {},
        ),
        aoAnterior: anterior,
        aoProximo: proximo,
      ),
    );

    Icon losango(WidgetTester tester) => tester.widget<Icon>(
      find.descendant(
        of: find.byKey(const ValueKey('kf')),
        matching: find.byType(Icon),
      ),
    );

    testWidgets('◇ sem marca: apagado e sem setas', (tester) async {
      await montarLosango(tester, animado: false, aqui: false);
      expect(losango(tester).icon, CupertinoIcons.rhombus);
      expect(losango(tester).color, isNot(AureaCores.keyframe));
      expect(find.byKey(const ValueKey('kf-anterior')), findsNothing);
      expect(find.byKey(const ValueKey('kf-proximo')), findsNothing);
      // A largura NAO muda com ou sem setas.
      expect(tester.getSize(find.byType(AureaKeyframeButton)).width, 64);
    });

    testWidgets('◇ animado fora da marca: aceso, com ‹ ›', (tester) async {
      var ant = 0;
      var prox = 0;
      await montarLosango(
        tester,
        animado: true,
        aqui: false,
        anterior: () => ant++,
        proximo: () => prox++,
      );
      expect(losango(tester).icon, CupertinoIcons.rhombus);
      expect(losango(tester).color, AureaCores.keyframe);
      await tester.tap(find.byKey(const ValueKey('kf-anterior')));
      await tester.tap(find.byKey(const ValueKey('kf-proximo')));
      await tester.pumpAndSettle();
      expect((ant, prox), (1, 1));
      expect(tester.getSize(find.byType(AureaKeyframeButton)).width, 64);
    });

    testWidgets('◆ marca neste quadro: cheio, e o toque alterna', (
      tester,
    ) async {
      var toques = 0;
      await montarLosango(
        tester,
        animado: true,
        aqui: true,
        toggle: () => toques++,
      );
      expect(losango(tester).icon, CupertinoIcons.rhombus_fill);
      await tester.tap(find.byKey(const ValueKey('kf')));
      await tester.pumpAndSettle();
      expect(toques, 1);
    });

    test('marcasVizinhas ignora a marca em cima e acha as vizinhas', () {
      final r = marcasVizinhas([0, 1000000, 2000000, 3000000], 2000000);
      expect(r.anterior, 1000000);
      expect(r.proxima, 3000000);
      final antes = marcasVizinhas([1000000], 0);
      expect((antes.anterior, antes.proxima), (null, 1000000));
      expect(temMarcaEm([1000000], 1004000), isTrue);
      expect(temMarcaEm([1000000], 1010000), isFalse);
    });
  });

  group('AureaPropertyRow', () {
    testWidgets('linha de 51: rotulo 75 | deslizante | valor 56 | losango', (
      tester,
    ) async {
      await montarDs(
        tester,
        _Controlado(
          inicial: 50,
          construir: (v, mudar) => AureaPropertyRow(
            rotulo: 'Opacidade',
            valor: v,
            aoMudar: mudar,
            min: 0,
            max: 100,
            unidade: '%',
            keyframe: KeyframeState(
              animated: false,
              here: false,
              onToggle: () {},
            ),
          ),
        ),
      );
      expect(tester.getSize(find.byType(AureaPropertyRow)).height, 51);
      expect(find.byKey(const ValueKey('prop-opacidade')), findsOneWidget);
      expect(
        tester.getSize(find.byKey(const ValueKey('valor-opacidade'))).width,
        56,
      );
      expect(find.byKey(const ValueKey('kf-opacidade')), findsOneWidget);
      // A LINHA INTEIRA arrasta: pegar pelo rotulo tambem aumenta.
      await tester.drag(find.text('Opacidade'), const Offset(80, 0));
      await tester.pumpAndSettle();
      expect(_valorDe(tester), greaterThan(50));
    });

    testWidgets('toque longo no rotulo reseta; variantes tambem tem 51', (
      tester,
    ) async {
      var resetou = 0;
      await montarDs(
        tester,
        Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            AureaPropertyRow(
              rotulo: 'Escala',
              valor: 1,
              aoMudar: (_) {},
              aoResetar: () => resetou++,
            ),
            AureaPropertyRow.ponto(
              rotulo: 'Posição',
              x: 10,
              y: 20,
              aoMudarX: (_) {},
              aoMudarY: (_) {},
            ),
            AureaPropertyRow.cor(
              rotulo: 'Cor',
              cor: const Color(0xFF3366FF),
              aoTocar: () {},
            ),
            AureaPropertyRow.personalizada(
              rotulo: 'Ligado',
              filho: AureaToggle(valor: true, aoMudar: (_) {}),
            ),
          ],
        ),
      );
      for (final r in tester.widgetList(find.byType(AureaPropertyRow))) {
        expect(tester.getSize(find.byWidget(r)).height, 51);
      }
      expect(find.byKey(const ValueKey('prop-posicao')), findsOneWidget);
      expect(find.byKey(const ValueKey('valor-posicao-x')), findsOneWidget);
      expect(find.text('#3366FF'), findsOneWidget);
      await tester.longPress(find.text('Escala'));
      await tester.pumpAndSettle();
      expect(resetou, 1);
    });

    testWidgets('arrastar a caixa X do ponto muda so o X (direita aumenta)', (
      tester,
    ) async {
      double x = 0;
      double y = 0;
      await montarDs(
        tester,
        StatefulBuilder(
          builder: (context, set) => AureaPropertyRow.ponto(
            rotulo: 'Posição',
            x: x,
            y: y,
            aoMudarX: (v) => set(() => x = v),
            aoMudarY: (v) => set(() => y = v),
          ),
        ),
      );
      await tester.drag(
        find.byKey(const ValueKey('valor-posicao-x')),
        const Offset(60, 0),
      );
      await tester.pumpAndSettle();
      expect(x, greaterThan(0));
      expect(y, 0);
    });
  });

  group('AureaEffectCard', () {
    testWidgets('cabecalho 37; abre e recolhe; olho e menu respondem', (
      tester,
    ) async {
      var olho = 0;
      var menu = 0;
      await montarDs(
        tester,
        AureaEffectCard(
          nome: 'Brilho',
          aoAlternarLigado: () => olho++,
          aoMenu: (_) => menu++,
          filhos: [
            AureaPropertyRow(rotulo: 'Raio', valor: 3, aoMudar: (_) {}),
          ],
        ),
      );
      expect(
        tester.getSize(find.byKey(const ValueKey('efeito-cabecalho'))).height,
        37,
      );
      expect(find.byKey(const ValueKey('efeito-corpo')), findsNothing);
      expect(find.text('Raio'), findsNothing);
      await tester.tap(find.byKey(const ValueKey('efeito-seta')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('efeito-corpo')), findsOneWidget);
      expect(find.text('Raio'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('efeito-seta')));
      await tester.pumpAndSettle();
      expect(find.text('Raio'), findsNothing);
      await tester.tap(find.byKey(const ValueKey('efeito-olho')));
      await tester.tap(find.byKey(const ValueKey('efeito-menu')));
      await tester.pumpAndSettle();
      expect((olho, menu), (1, 1));
    });
  });

  group('Menu, dropdown, abas, secao', () {
    testWidgets('dropdown abre o menu (itens de 40, largura 250) e escolhe', (
      tester,
    ) async {
      var escolhido = 'Normal';
      await montarDs(
        tester,
        StatefulBuilder(
          builder: (context, set) => SizedBox(
            width: 200,
            child: AureaDropdown<String>(
              valor: escolhido,
              opcoes: const ['Normal', 'Tela', 'Multiplicar'],
              rotuloDe: (s) => s,
              aoMudar: (v) => set(() => escolhido = v),
            ),
          ),
        ),
      );
      await tester.tap(find.byType(AureaDropdown<String>));
      await tester.pumpAndSettle();
      expect(find.byType(AureaMenu<String>), findsOneWidget);
      expect(tester.getSize(find.byType(AureaMenu<String>)).width, 250);
      expect(tester.getSize(find.byKey(const ValueKey('menu-1'))).height, 40);
      await tester.tap(find.byKey(const ValueKey('menu-2')));
      await tester.pumpAndSettle();
      expect(find.byType(AureaMenu<String>), findsNothing);
      expect(escolhido, 'Multiplicar');
    });

    testWidgets('abas trocam; secao recolhe', (tester) async {
      var aba = 0;
      await montarDs(
        tester,
        StatefulBuilder(
          builder: (context, set) => Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              SizedBox(
                width: 340,
                child: AureaTabs(
                  abas: const ['Posição', 'Escala', 'Rotação'],
                  ativa: aba,
                  aoTrocar: (i) => set(() => aba = i),
                ),
              ),
              const AureaSection(
                titulo: 'Avançado',
                chave: 'avancado',
                filhos: [Text('dentro')],
              ),
            ],
          ),
        ),
      );
      expect(tester.getSize(find.byType(AureaTabs)).height, 38);
      await tester.tap(find.byKey(const ValueKey('aba-2')));
      await tester.pumpAndSettle();
      expect(aba, 2);
      expect(find.text('dentro'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('secao-avancado')));
      await tester.pumpAndSettle();
      expect(find.text('dentro'), findsNothing);
    });

    testWidgets('botao de barra e bloco; folha abre e fecha', (tester) async {
      var toques = 0;
      await montarDs(
        tester,
        Builder(
          builder: (context) => Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              AureaToolbarButton(
                icone: CupertinoIcons.sparkles,
                rotulo: 'Efeitos',
                aoTocar: () => toques++,
              ),
              AureaToolbarButton(
                key: const ValueKey('bloco'),
                icone: CupertinoIcons.textformat,
                rotulo: 'Texto',
                bloco: true,
                largura: 80,
                aoTocar: () => mostrarAureaFolha<void>(
                  context,
                  titulo: 'Adicionar',
                  construtor: (_) => const SizedBox(
                    height: 120,
                    child: Text('conteudo da folha'),
                  ),
                ),
              ),
              AureaChip(rotulo: 'Ligado', ativo: true, aoTocar: () {}),
            ],
          ),
        ),
      );
      expect(tester.getSize(find.byType(AureaToolbarButton).first).height, 57);
      expect(tester.getSize(find.byKey(const ValueKey('bloco'))).height, 57);
      await tester.tap(find.text('Efeitos'));
      expect(toques, 1);
      await tester.tap(find.byKey(const ValueKey('bloco')));
      await tester.pumpAndSettle();
      expect(find.text('conteudo da folha'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('folha-fechar')));
      await tester.pumpAndSettle();
      expect(find.text('conteudo da folha'), findsNothing);
    });
  });

  group('Troca de tema (nada de cor presa em const)', () {
    tearDown(() => AureaPaleta.ativa = AureaPaleta.aurea);

    testWidgets('o mesmo painel pinta a paleta em vigor', (tester) async {
      Future<Color> corDoPainel(AureaPaleta p) async {
        AureaPaleta.ativa = p;
        // A raiz troca de chave com o tema (como o AureaApp): a arvore
        // nasce de novo lendo a paleta nova.
        await montarDs(
          tester,
          SizedBox(
            height: 200,
            child: AureaPanel(
              titulo: 'Transformar',
              aoFechar: () {},
              filhos: [
                AureaPropertyRow(
                  rotulo: 'Opacidade',
                  valor: 50,
                  aoMudar: (_) {},
                  min: 0,
                  max: 100,
                ),
              ],
            ),
          ),
          chaveDoApp: ValueKey('tema-${p.id.name}'),
        );
        return tester
            .widget<ColoredBox>(find.byKey(const ValueKey('painel')))
            .color;
      }

      final aurea = await corDoPainel(AureaPaleta.aurea);
      final midnight = await corDoPainel(AureaPaleta.midnight);
      expect(aurea, AureaPaleta.aurea.editor.surface);
      expect(midnight, AureaPaleta.midnight.editor.surface);
      expect(aurea, isNot(midnight));
      // O preenchimento do deslizante tambem segue o tema.
      expect(
        find.byType(AureaSlider),
        paints
          ..rrect()
          ..rrect(color: AureaPaleta.midnight.accent),
      );
    });
  });
}
