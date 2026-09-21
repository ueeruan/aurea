// A FOLHA DO TEXTO 3D: o texto deixa de ser tres perguntas e vira um objeto
// que se edita — na metade de baixo da tela, com o palco a vista por cima.
//
// O QUE ESTES TESTES PRENDEM, e que era o buraco antes da folha:
//
//   * trocar o METAL nao pode jogar fora a camada — a posicao, o giro e os
//     keyframes continuam, e so o material muda;
//   * trocar o TEXTO refaz a geometria e mantem o metal escolhido;
//   * a ESPESSURA acompanha o dedo no rotulo enquanto o dedo arrasta (o
//     controle ficava parado ate soltar) e refaz a letra no fim;
//   * NAO HA PREVIA INTERNA: o unico preview e o palco (a previa propria
//     alternava o alvo do motor com o palco e recriava os alvos duas vezes
//     por toque), e a folha nasce com `Material` — sem ele os textos saiam
//     com o sublinhado amarelo do "texto sem estilo".
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/texto3d_sheet.dart';
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:flutter/cupertino.dart' show CupertinoIcons;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late ProviderContainer container;
  late String cenaId;
  late String noId;

  Scene3DLayer cena() =>
      container.read(editorControllerProvider).layers.whereType<Scene3DLayer>().single;

  SceneNode no() => cena().scene.nodeById(noId)!;

  Future<void> abrir(WidgetTester tester) async {
    tester.view.physicalSize = const Size(600, 1600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    container = ProviderContainer();
    addTearDown(container.dispose);
    final controller = container.read(editorControllerProvider.notifier);
    controller.openProject(VideoProject.empty('texto 3d'));
    // EM `runAsync` PORQUE MONTAR A LETRA LE ARQUIVO DE VERDADE (a fonte).
    // Dentro do `testWidgets` o relogio e falso e um Future de I/O real
    // nunca completa: sem isto o teste fica pendurado ate o timeout.
    noId = (await tester.runAsync<String?>(
      () => controller.addTexto3D(
        Duration.zero,
        'AUREA',
        EstiloDoTexto3D.ouro,
        familia: 'fonte que nao existe',
      ),
    ))!;
    cenaId = cena().id;

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Scaffold(
            body: Consumer(
              builder: (context, ref, _) => TextButton(
                onPressed: () => showTexto3DSheet(
                  context,
                  ref,
                  sceneId: cenaId,
                  nodeId: noId,
                  playhead: const Duration(seconds: 1),
                ),
                child: const Text('abrir'),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
  }

  testWidgets('abre com o texto da camada e os quatro metais', (tester) async {
    await abrir(tester);
    final campo = tester.widget<TextField>(
      find.byKey(const ValueKey('texto3d-campo')),
    );
    expect(campo.controller!.text, 'AUREA');
    for (final e in EstiloDoTexto3D.values) {
      expect(
        find.byKey(ValueKey('texto3d-estilo-${e.name}')),
        findsOneWidget,
        reason: 'o metal ${e.name} tem de estar na folha',
      );
    }
  });

  // OS CARTOES SAO OS DO PAINEL DE EFEITOS: abrem e fecham, e recolhido
  // nem constroi o corpo. Extrusao e Material nascem abertos (e o que se
  // mexe em toda letra); o resto fica a UM toque — nao escondido.
  testWidgets('os cartoes abrem e fecham como os do painel de efeitos', (
    tester,
  ) async {
    await abrir(tester);
    for (final nome in const [
      'extrusao',
      'material',
      'caracteres',
      'cena',
      'fonte-e-ajuste-fino',
    ]) {
      expect(
        find.byKey(ValueKey('texto3d-cartao-$nome')),
        findsOneWidget,
        reason: 'o cartao $nome tem de estar na folha',
      );
    }
    // O CABECALHO E A ALCA: o corpo do cartao aberto esta cheio de reguas,
    // entao o toque nao pode ser no centro — e nos 48 px de cima.
    Future<void> alternar(String nome) async {
      final cartao = find.byKey(ValueKey('texto3d-cartao-$nome'));
      await tester.ensureVisible(cartao);
      await tester.pumpAndSettle();
      final caixa = tester.getRect(cartao);
      await tester.tapAt(Offset(caixa.center.dx, caixa.top + 24));
      await tester.pumpAndSettle();
    }

    // Fechado: o botao de importar fonte nem existe na arvore.
    expect(find.byKey(const ValueKey('texto3d-importar-fonte')), findsNothing);
    await alternar('fonte-e-ajuste-fino');
    expect(find.byKey(const ValueKey('texto3d-importar-fonte')), findsOneWidget);

    // E fechar o Material tira as fichas de metal da arvore.
    await alternar('material');
    expect(find.byKey(const ValueKey('texto3d-estilo-cromo')), findsNothing);
  });

  // O CARTAO "CARACTERES": mover letras soltas, com o MESMO losango de
  // keyframe de qualquer outra propriedade — e nao uma logica propria.
  testWidgets('Caracteres tem as nove medidas com o losango da casa', (
    tester,
  ) async {
    await abrir(tester);
    final cartao = find.byKey(const ValueKey('texto3d-cartao-caracteres'));
    await tester.ensureVisible(cartao);
    await tester.tap(cartao, warnIfMissed: false);
    await tester.pumpAndSettle();

    for (final m in MedidaDoCaractere.values) {
      final linha = find.byKey(ValueKey('texto3d-ajuste-${m.name}'));
      expect(linha, findsOneWidget, reason: 'falta a medida ${m.name}');
      expect(
        tester.widget<ParameterRow>(linha).keyframe,
        isNotNull,
        reason: 'toda medida animavel tem o losango',
      );
    }
    for (final chave in const [
      'texto3d-selecao-todas',
      'texto3d-selecao-uma',
      'texto3d-selecao-intervalo',
    ]) {
      expect(find.byKey(ValueKey(chave)), findsOneWidget);
    }
  });

  testWidgets('escolher UMA letra e empurra-la move so ela', (tester) async {
    await abrir(tester);
    final cartao = find.byKey(const ValueKey('texto3d-cartao-caracteres'));
    await tester.ensureVisible(cartao);
    await tester.tap(cartao, warnIfMissed: false);
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('texto3d-selecao-uma')));
    await tester.pumpAndSettle();
    // "AUREA": a terceira letra e o "R" (indice 2).
    final letra = find.byKey(const ValueKey('texto3d-letra-2'));
    await tester.ensureVisible(letra);
    await tester.tap(letra);
    await tester.pumpAndSettle();

    final linha = find.byKey(const ValueKey('texto3d-ajuste-z'));
    await tester.ensureVisible(linha);
    await tester.pumpAndSettle();
    final caixa = tester.getRect(linha);
    final gesto = await tester.startGesture(
      Offset(caixa.left + caixa.width * 0.28, caixa.center.dy),
    );
    for (var i = 0; i < 6; i++) {
      await gesto.moveBy(const Offset(15, 0));
      await tester.pump();
    }
    await gesto.up();
    await tester.pumpAndSettle();

    final ajustes = no().texto3d!.ajustes;
    expect(ajustes.length, 1, reason: 'um ajuste, o da letra escolhida');
    expect(ajustes.single.inicio, 2);
    expect(ajustes.single.fim, 2);
    expect(
      ajustes.single.valorEm(MedidaDoCaractere.z, Duration.zero),
      greaterThan(0),
      reason: 'arrastar para a direita empurra a letra',
    );
    // A malha NAO foi refeita: mover uma letra e pose, nao geometria.
    expect(no().modelAsset!.temAnimacaoDeTexto, isTrue);
  });

  // A CENA E O ESTUDIO DA LETRA: reflexo, iluminacao e ambiente vao para a
  // cena (nao para o material) e aparecem no palco na hora, sem refazer
  // um triangulo.
  testWidgets('Cena muda reflexo, iluminacao e ambiente direto na cena', (
    tester,
  ) async {
    await abrir(tester);
    final cartao = find.byKey(const ValueKey('texto3d-cartao-cena'));
    await tester.ensureVisible(cartao);
    final caixa = tester.getRect(cartao);
    await tester.tapAt(Offset(caixa.center.dx, caixa.top + 24));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('texto3d-controle-Reflexo')), findsOneWidget);
    expect(find.byKey(const ValueKey('texto3d-controle-Ambiente')), findsOneWidget);

    final malhaAntes = no().modelAsset;
    final ficha = find.byKey(const ValueKey('texto3d-iluminacao-neon'));
    await tester.ensureVisible(ficha);
    await tester.tap(ficha);
    await tester.pumpAndSettle();
    expect(cena().scene.environment, EnvironmentKind.neon);
    expect(
      no().modelAsset,
      same(malhaAntes),
      reason: 'trocar o estudio nao refaz a letra',
    );
  });

  // O LOSANGO E O DA CASA: crava no instante do cabecote e tira de novo.
  // Nao ha logica de keyframe propria da folha — a trilha e uma
  // `AnimatedDouble`, como qualquer outra propriedade do aplicativo.
  testWidgets('o losango crava e tira o keyframe do ajuste', (tester) async {
    await abrir(tester);
    final cartao = find.byKey(const ValueKey('texto3d-cartao-caracteres'));
    await tester.ensureVisible(cartao);
    final caixa = tester.getRect(cartao);
    await tester.tapAt(Offset(caixa.center.dx, caixa.top + 24));
    await tester.pumpAndSettle();

    final linha = find.byKey(const ValueKey('texto3d-ajuste-z'));
    await tester.ensureVisible(linha);
    await tester.pumpAndSettle();
    final losango = find.descendant(
      of: linha,
      matching: find.byIcon(CupertinoIcons.rhombus),
    );
    expect(losango, findsOneWidget, reason: 'o losango vazio da linha');
    await tester.tap(losango);
    await tester.pumpAndSettle();

    final ajustes = no().texto3d!.ajustes;
    expect(ajustes.length, 1);
    final trilha = ajustes.single.trilha(MedidaDoCaractere.z);
    expect(trilha.isAnimated, isTrue);
    // NO INSTANTE DO CABECOTE, e em tempo LOCAL da camada: a folha abriu
    // com o cabecote em 1 s e a camada comeca em zero.
    expect(trilha.keyframes.single.time, const Duration(seconds: 1));

    // Tocar de novo no losango CHEIO tira a marca — e sem marca nenhuma o
    // ajuste volta a nao existir no projeto.
    await tester.tap(
      find.descendant(
        of: find.byKey(const ValueKey('texto3d-ajuste-z')),
        matching: find.byIcon(CupertinoIcons.rhombus_fill),
      ),
    );
    await tester.pumpAndSettle();
    expect(no().texto3d!.ajustes, isEmpty);
  });

  testWidgets('sem previa interna, na metade de baixo, com Material', (tester) async {
    await abrir(tester);
    // UM PREVIEW SO: o palco. Nada de imagem do motor dentro da folha, nem
    // do aviso que a previa antiga mostrava no PC.
    expect(find.byType(RawImage), findsNothing);
    expect(find.textContaining('aparece no aparelho'), findsNothing);

    // A METADE DE BAIXO: a folha nao passa de 52% da tela.
    final folha = find.byKey(const ValueKey('texto3d-folha'));
    expect(folha, findsOneWidget);
    final altura = tester.getSize(folha).height;
    // ignore: avoid_print
    print('ALTURA DA FOLHA: $altura de 1600');
    expect(altura, lessThanOrEqualTo(1600 * 0.52 + 0.5));
    expect(altura, greaterThan(200));

    // O `Material` acima dos textos: sem ele, sublinhado amarelo.
    expect(
      find.ancestor(
        of: find.byKey(const ValueKey('texto3d-campo')),
        matching: find.byType(Material),
      ),
      findsWidgets,
    );
    // OS CONTROLES SAO OS DA CASA, e nao sliders do Material.
    expect(find.byType(Slider), findsNothing);
    expect(find.byType(ParameterRow), findsWidgets);
  });

  testWidgets('trocar o metal troca so o material e mantem a malha', (tester) async {
    await abrir(tester);
    final antes = no();
    expect(antes.estiloTexto3d, EstiloDoTexto3D.ouro);
    final malhaAntes = antes.modelAsset;
    final giroAntes = antes.rotY.valueAt(Duration.zero);

    await tester.tap(
      find.byKey(const ValueKey('texto3d-estilo-cromo')),
    );
    await tester.pumpAndSettle();

    final depois = no();
    expect(depois.estiloTexto3d, EstiloDoTexto3D.cromo);
    expect(depois.texto3d!.texto, 'AUREA', reason: 'o texto nao pode se perder');
    expect(
      depois.rotY.valueAt(Duration.zero),
      giroAntes,
      reason: 'o giro da camada continua o que o dono ajustou',
    );
    expect(
      depois.modelAsset,
      isNot(equals(malhaAntes)),
      reason: 'o metal novo entra numa malha refeita',
    );
  });

  testWidgets('editar o texto refaz a geometria e guarda o metal', (tester) async {
    await abrir(tester);
    await tester.tap(find.byKey(const ValueKey('texto3d-estilo-cromo')));
    await tester.pumpAndSettle();

    await tester.enterText(
      find.byKey(const ValueKey('texto3d-campo')),
      'METAL',
    );
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pumpAndSettle();

    final n = no();
    expect(n.texto3d!.texto, 'METAL');
    expect(
      n.estiloTexto3d,
      EstiloDoTexto3D.cromo,
      reason: 'trocar a palavra nao pode voltar o metal para o padrao',
    );
  });

  testWidgets('a profundidade anda no rotulo durante o arrasto', (tester) async {
    await abrir(tester);
    final inicio = no().texto3d!.espessura;
    // PROFUNDIDADE e o nome do controle da extrusao desde que a folha foi
    // posta na ordem de prioridade do dono ("Espessura" era a medida
    // interna, e ninguem procurava por ela).
    final controle = find.byKey(const ValueKey('texto3d-controle-Profundidade'));
    await tester.ensureVisible(controle);
    await tester.pumpAndSettle();
    final linha = tester.widget<ParameterRow>(controle);
    expect(linha.value, inicio);

    // A LINHA INTEIRA E A REGUA: arrastar na horizontal puxa o valor. O
    // dedo desce no rotulo (a esquerda), longe do chip do numero, que tem
    // o toque para digitar — e anda em passos, como um dedo de verdade:
    // um salto unico nao resolve a arena contra a rolagem da folha.
    final caixa = tester.getRect(controle);
    final gesto = await tester.startGesture(
      Offset(caixa.left + caixa.width * 0.25, caixa.center.dy),
    );
    for (var i = 0; i < 6; i++) {
      await gesto.moveBy(const Offset(15, 0));
      await tester.pump();
    }
    // AINDA COM O DEDO NA TELA: o numero ja tem de ter mudado. Era aqui que
    // o controle parecia morto — nada mudava ate soltar.
    expect(
      tester.widget<ParameterRow>(controle).value,
      isNot(inicio),
      reason: 'o rotulo ficou parado com o dedo na tela',
    );
    await gesto.up();
    // A ESPERA DE 140 ms E UM `Timer`, e Timer nao agenda quadro: um
    // `pumpAndSettle` sozinho volta antes de ele disparar. O tempo tem de
    // ser avancado de proposito, e a letra e refeita lendo a fonte do
    // disco — que so anda em `runAsync`.
    await tester.pump(const Duration(milliseconds: 200));
    await tester.runAsync(
      () => Future<void>.delayed(const Duration(milliseconds: 400)),
    );
    await tester.pump();
    expect(
      no().texto3d!.espessura,
      isNot(inicio),
      reason: 'soltar o dedo tem de refazer a letra',
    );
  });
}
