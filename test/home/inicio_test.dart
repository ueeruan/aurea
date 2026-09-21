// A INICIO NOVA: a marca, o "+ Novo projeto", os projetos recentes em
// cartoes com miniatura e ficha, e o ⋯ de cada projeto com as cinco
// acoes — cada uma chegando ao controlador que ja existia.
import 'dart:io';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/application/projects_view.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_home.dart';

VideoProject _comDuracao(String nome, Duration d, {double ratio = 16 / 9}) =>
    VideoProject(
      name: nome,
      createdAt: DateTime(2026, 9, 1),
      aspectRatio: ratio,
      layers: [NullLayer(name: 'n', startTime: Duration.zero, duration: d)],
    );

class _ProjetosVazios extends ProjectsController {
  @override
  List<VideoProject> build() => [];
}

void main() {
  testWidgets('a Inicio: marca, criar e os cartoes com nome, quadro e duracao', (
    tester,
  ) async {
    final vertical = VideoProject.empty('Reels do fim de semana', aspectRatio: 9 / 16);
    final longo = _comDuracao('Aula completa', const Duration(minutes: 1, seconds: 12));
    await montarInicio(tester, projetos: [vertical, longo]);

    expect(find.text('AUREA'), findsOneWidget);
    expect(find.byKey(const ValueKey('novo-projeto')), findsOneWidget);
    expect(find.text('Novo projeto'), findsOneWidget);
    expect(find.text('Projetos recentes'), findsOneWidget);

    // O nome como foi digitado, o quadro de saida e a duracao.
    expect(find.text('Reels do fim de semana'), findsOneWidget);
    expect(find.text('1080 × 1920 · 0:05'), findsOneWidget);
    expect(find.text('Aula completa'), findsOneWidget);
    expect(find.text('1920 × 1080 · 1:12'), findsOneWidget);
    // Cada cartao tem o seu ⋯ a vista.
    expect(find.byKey(ValueKey('projeto-menu-${vertical.id}')), findsOneWidget);
    expect(find.byKey(ValueKey('projeto-menu-${longo.id}')), findsOneWidget);
    // A ultima edicao: o relogio e a hora (editado nesta sessao = hoje).
    expect(find.byIcon(CupertinoIcons.clock), findsNWidgets(2));
    expect(tester.takeException(), isNull);
  });

  testWidgets('o ⋯ do projeto tem as cinco acoes, e cada uma chega ao '
      'controlador', (tester) async {
    final a = VideoProject.empty('Alfa');
    final b = VideoProject.empty('Beta');
    final (c, registro) = await montarInicio(tester, projetos: [a, b]);
    List<VideoProject> lista() => c.read(projectsControllerProvider);

    Future<void> abrirMenu(VideoProject p) async {
      await tester.tap(find.byKey(ValueKey('projeto-menu-${p.id}')));
      await assentar(tester);
    }

    await abrirMenu(a);
    for (final chave in [
      'abrir',
      'renomear',
      'duplicar',
      'compartilhar',
      'excluir',
    ]) {
      expect(find.byKey(ValueKey('menu-$chave')), findsOneWidget, reason: chave);
    }

    // ABRIR: o projeto vai para o editor e a tela do editor e empilhada.
    await tester.tap(find.byKey(const ValueKey('menu-abrir')));
    await assentar(tester);
    expect(registro.carregados.single.id, a.id);
    expect(find.byKey(chaveDoEditorFalso), findsOneWidget);
    // Abrir nao cria nada.
    expect(lista(), hasLength(2));
    tester.state<NavigatorState>(find.byType(Navigator)).pop();
    await assentar(tester);
    expect(find.byKey(chaveDoEditorFalso), findsNothing);

    // RENOMEAR: o nome novo volta pelo upsert do controlador.
    await abrirMenu(a);
    await tester.tap(find.byKey(const ValueKey('menu-renomear')));
    await assentar(tester);
    await tester.enterText(
      find.byKey(const ValueKey('estudio-nome')),
      'Alfa final',
    );
    await tester.tap(find.text('OK'));
    await assentar(tester);
    expect(lista().map((p) => p.name), containsAll(['Alfa final', 'Beta']));
    expect(lista().firstWhere((p) => p.id == a.id).name, 'Alfa final');
    expect(find.text('Alfa final'), findsOneWidget);

    // DUPLICAR: copia com id novo, na frente.
    await abrirMenu(b);
    await tester.tap(find.byKey(const ValueKey('menu-duplicar')));
    await assentar(tester);
    expect(lista(), hasLength(3));
    expect(lista().first.name, 'Beta (cópia)');
    expect(lista().first.id, isNot(b.id));

    // COMPARTILHAR: o pacote do projeto certo.
    await abrirMenu(b);
    await tester.tap(find.byKey(const ValueKey('menu-compartilhar')));
    await assentar(tester);
    expect(registro.compartilhados.single.id, b.id);
    // O aviso de "salvo" some sozinho.
    await tester.pump(const Duration(seconds: 5));

    // EXCLUIR: pergunta antes e tira da lista.
    await abrirMenu(b);
    await tester.tap(find.byKey(const ValueKey('menu-excluir')));
    await assentar(tester);
    expect(lista().any((p) => p.id == b.id), isTrue, reason: 'ainda pergunta');
    await tester.tap(find.byKey(const ValueKey('excluir-confirmar')));
    await assentar(tester);
    expect(lista().any((p) => p.id == b.id), isFalse);
    expect(find.byKey(ValueKey('projeto-${b.id}')), findsNothing);

    await c.read(projectsControllerProvider.notifier).flush();
    expect(tester.takeException(), isNull);
  });

  testWidgets('o menu da Inicio guarda o resto, e apaga todos com '
      'confirmacao', (tester) async {
    final (c, _) = await montarInicio(
      tester,
      projetos: [VideoProject.empty('Um'), VideoProject.empty('Dois')],
    );
    await tester.tap(find.byKey(const ValueKey('home-menu')));
    await assentar(tester);
    for (final chave in [
      'importar-midia',
      'abrir-template',
      'importar-projeto',
      'modelos',
      'aprender',
      'ajustes',
      'sobre',
      'apagar-todos',
    ]) {
      expect(find.byKey(ValueKey('menu-$chave')), findsOneWidget, reason: chave);
    }
    // Ajustes (temas la dentro) e Sobre sao abas da casca.
    await tester.tap(find.byKey(const ValueKey('menu-sobre')));
    await assentar(tester);
    expect(c.read(homeTabProvider), 4);
    await tester.tap(find.byKey(const ValueKey('home-menu')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('menu-ajustes')));
    await assentar(tester);
    expect(c.read(homeTabProvider), 2);

    await tester.tap(find.byKey(const ValueKey('home-menu')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('menu-apagar-todos')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('apagar-todos-confirmar')));
    await assentar(tester);
    expect(c.read(projectsControllerProvider), isEmpty);
    // Sem projeto, a dica no lugar da lista.
    expect(
      find.text('Seus projetos aparecem aqui, com a miniatura do que voce fez.'),
      findsOneWidget,
    );
  });

  testWidgets('os modelos e os tutoriais continuam a um toque', (tester) async {
    await montarInicio(tester);
    await tester.tap(find.byKey(const ValueKey('home-menu')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('menu-modelos')));
    await assentar(tester);
    for (final id in ['vhf', 'dnyx', 'reference', 'notes', 'pindown']) {
      expect(find.byKey(ValueKey('modelo-$id')), findsOneWidget, reason: id);
    }
    await tester.tap(find.byKey(const ValueKey('folha-fechar')));
    await assentar(tester);

    await tester.tap(find.byKey(const ValueKey('home-menu')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('menu-aprender')));
    await assentar(tester);
    for (final chave in [
      'inicio-tutorial-cena3d',
      'inicio-tutorial-cena-completa',
      'inicio-tutorial-texto-bounce',
      'inicio-novidades',
      'inicio-relatar',
    ]) {
      expect(find.byKey(ValueKey(chave)), findsOneWidget, reason: chave);
    }
    expect(tester.takeException(), isNull);
  });

  testWidgets('toque longo escolhe varios; o lote duplica e exclui', (
    tester,
  ) async {
    final a = VideoProject.empty('A');
    final b = VideoProject.empty('B');
    final (c, _) = await montarInicio(tester, projetos: [a, b]);

    await tester.longPress(find.byKey(ValueKey('projeto-${a.id}')));
    await tester.pump();
    expect(c.read(selecaoDeProjetosProvider), {a.id});
    expect(find.byKey(ValueKey('projeto-marca-${a.id}')), findsOneWidget);
    // Escolhendo, o toque marca em vez de abrir.
    await tester.tap(find.byKey(ValueKey('projeto-${b.id}')));
    await tester.pump();
    expect(c.read(selecaoDeProjetosProvider), {a.id, b.id});

    await tester.tap(find.byKey(const ValueKey('projetos-lote-duplicar')));
    await tester.pump();
    expect(c.read(projectsControllerProvider), hasLength(4));
    expect(c.read(selecaoDeProjetosProvider), isEmpty);

    await tester.longPress(find.byKey(ValueKey('projeto-${a.id}')));
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('projetos-lote-excluir')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('projetos-excluir-lote')));
    await assentar(tester);
    expect(c.read(projectsControllerProvider).any((p) => p.id == a.id), isFalse);
    expect(c.read(projectsControllerProvider), hasLength(3));
    await c.read(projectsControllerProvider.notifier).flush();
  });

  testWidgets('a busca filtra pelo nome', (tester) async {
    await montarInicio(
      tester,
      projetos: [VideoProject.empty('Vinheta'), VideoProject.empty('Clipe')],
    );
    await tester.tap(find.byKey(const ValueKey('projetos-buscar')));
    await tester.pump();
    await tester.enterText(
      find.byKey(const ValueKey('projetos-busca-campo')),
      'vin',
    );
    await tester.pump();
    expect(find.text('Vinheta'), findsOneWidget);
    expect(find.text('Clipe'), findsNothing);
    await tester.enterText(
      find.byKey(const ValueKey('projetos-busca-campo')),
      'zzz',
    );
    await tester.pump();
    expect(find.text('Nenhum projeto com esse nome.'), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('projetos-busca-limpar')));
    await tester.pump();
    expect(find.text('Clipe'), findsOneWidget);
  });

  testWidgets('com o editor por cima a Inicio nao refaz a lista; ao voltar '
      'esta em dia', (tester) async {
    final a = VideoProject.empty('Antes');
    final (c, registro) = await montarInicio(tester, projetos: [a]);
    // Tocar no cartao abre.
    await tester.tap(find.byKey(ValueKey('projeto-${a.id}')));
    await assentar(tester);
    expect(registro.carregados.single.id, a.id);
    expect(find.byKey(chaveDoEditorFalso), findsOneWidget);

    // O editor publica a edicao; a Inicio, escondida, nao reconstroi.
    c.read(projectsControllerProvider.notifier).upsert(a.copyWith(name: 'Depois'));
    await tester.pump();
    expect(find.text('Antes', skipOffstage: false), findsOneWidget);
    expect(find.text('Depois', skipOffstage: false), findsNothing);

    // Voltou: le a lista uma vez, ja com o nome novo.
    tester.state<NavigatorState>(find.byType(Navigator)).pop();
    await assentar(tester);
    expect(find.text('Depois'), findsOneWidget);
    await c.read(projectsControllerProvider.notifier).flush();
  });

  testWidgets('a lista e preguicosa: cem projetos, so os da tela montados', (
    tester,
  ) async {
    await montarInicio(
      tester,
      projetos: [for (var i = 0; i < 100; i++) VideoProject.empty('P$i')],
    );
    final montados = find.textContaining(RegExp(r'^P\d+$')).evaluate().length;
    expect(montados, lessThan(20));
    expect(tester.takeException(), isNull);
  });

  for (final tela in const [Size(320, 568), Size(430, 932)]) {
    testWidgets('sem estouro em ${tela.width.toInt()}x${tela.height.toInt()}: '
        'lista, menu e folha', (tester) async {
      await montarInicio(
        tester,
        tela: tela,
        projetos: [
          VideoProject.empty(
            'Um nome de projeto comprido demais para caber numa linha so',
            aspectRatio: 9 / 16,
            resolutionHeight: 2160,
          ),
          _comDuracao('Longo', const Duration(hours: 1, minutes: 2, seconds: 3)),
          for (var i = 0; i < 10; i++) VideoProject.empty('Projeto $i'),
        ],
      );
      expect(tester.takeException(), isNull);

      await tester.drag(find.byType(CustomScrollView), const Offset(0, -600));
      await tester.pump();
      expect(tester.takeException(), isNull);
      await tester.drag(find.byType(CustomScrollView), const Offset(0, 600));
      await tester.pump();

      await tester.tap(find.byKey(const ValueKey('home-menu')));
      await assentar(tester);
      expect(tester.takeException(), isNull);
      await tester.tapAt(const Offset(5, 5));
      await assentar(tester);

      await tester.tap(find.byKey(const ValueKey('novo-projeto')));
      await assentar(tester);
      expect(find.byKey(const ValueKey('criar-projeto')), findsOneWidget);
      expect(tester.takeException(), isNull);
      // A folha rola quando nao cabe: o CRIAR e alcancavel.
      await tester.ensureVisible(find.byKey(const ValueKey('criar-projeto')));
      await tester.pump();
      expect(
        find.byKey(const ValueKey('criar-projeto')).hitTestable(),
        findsOneWidget,
      );
      await tester.tap(find.byKey(const ValueKey('formato-livre')));
      await tester.pump();
      expect(tester.takeException(), isNull);
    });
  }

  test('recente quer dizer recente: quem foi editado vai para a frente', () async {
    final pasta = Directory.systemTemp.createTempSync('aurea_inicio');
    addTearDown(() => pasta.deleteSync(recursive: true));
    final container = ProviderContainer(
      overrides: [
        projectsControllerProvider.overrideWith(_ProjetosVazios.new),
        projectRepositoryProvider.overrideWithValue(
          ProjectRepository(directory: pasta),
        ),
      ],
    );
    addTearDown(container.dispose);
    final c = container.read(projectsControllerProvider.notifier);
    final a = VideoProject.empty('A');
    final b = VideoProject.empty('B');
    final d = VideoProject.empty('C');
    for (final p in [a, b, d]) {
      c.add(p);
    }
    expect(container.read(projectsControllerProvider).map((p) => p.name), [
      'C',
      'B',
      'A',
    ]);
    c.upsert(a.copyWith(name: 'A editado'));
    expect(container.read(projectsControllerProvider).map((p) => p.name), [
      'A editado',
      'C',
      'B',
    ]);
    await c.flush();
    // A "ultima edicao" do cartao e a data do arquivo gravado.
    final quando = ProjectRepository(directory: pasta).editadoEm(a.id);
    expect(quando, isNotNull);
    expect(
      DateTime.now().difference(quando!).inMinutes.abs(),
      lessThan(5),
    );
    expect(ProjectRepository(directory: pasta).editadoEm('nao-existe'), isNull);
  });
}
