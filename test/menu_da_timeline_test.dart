// O MENU ⋮ DA TIMELINE (v1.1.1): o que vale para o projeto inteiro.
//
// Miniatura escolhida, marcas de introducao e final (que nunca se
// cruzam e voltam do arquivo), aparar o projeto no cabecote num desfazer
// so, o cronometro de edicao que mora no aparelho, e o proprio menu
// abrindo com selecao e modo de previa funcionando.
import 'package:aurea/src/features/editor/application/cronometro_de_edicao.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/opcoes_de_visualizacao.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

const _s = Duration(seconds: 1);

ProviderContainer _container() {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  return c;
}

void main() {
  test('miniatura e marcas de re-temporizacao vao e voltam do arquivo', () {
    final p = VideoProject(
      name: 'p',
      createdAt: DateTime(2026, 9, 15),
      thumbTime: const Duration(milliseconds: 1500),
      introFim: _s,
      finalInicio: _s * 4,
    );
    final volta = projectFromJson(projectToJson(p));
    expect(volta.thumbTime, const Duration(milliseconds: 1500));
    expect(volta.introFim, _s);
    expect(volta.finalInicio, _s * 4);
    // Arquivo sem as chaves (ou com lixo) abre sem marcas.
    final json = projectToJson(VideoProject(name: 'q', createdAt: DateTime(2026)))
      ..['thumbUs'] = 'lixo'
      ..['introUs'] = -5;
    final limpo = projectFromJson(json);
    expect(limpo.thumbTime, isNull);
    expect(limpo.introFim, isNull);
    expect(limpo.finalInicio, isNull);
  });

  test('as marcas de introducao e final nunca se cruzam', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.marcarInicioDoFinal(_s * 2);
    e.marcarFimDaIntroducao(_s * 3); // depois do final: o final sai
    expect(c.read(editorControllerProvider).introFim, _s * 3);
    expect(c.read(editorControllerProvider).finalInicio, isNull);
    e.marcarInicioDoFinal(_s); // antes da introducao: a introducao sai
    expect(c.read(editorControllerProvider).finalInicio, _s);
    expect(c.read(editorControllerProvider).introFim, isNull);
    e.definirQuadroDaMiniatura(_s);
    expect(c.read(editorControllerProvider).thumbTime, _s);
    e.definirQuadroDaMiniatura(null);
    expect(c.read(editorControllerProvider).thumbTime, isNull);
  });

  test('aparar o projeto no cabecote: corta, tira o que vem depois, um desfazer', () {
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'A'); // 0..3
    e.addShapeLayer(_s * 2, name: 'B'); // 2..5
    e.addShapeLayer(_s * 6, name: 'C'); // 6..9
    e.aparaProjetoNoCabecote(const Duration(milliseconds: 2500));
    final camadas = c.read(editorControllerProvider).layers;
    expect(camadas.map((l) => l.name.substring(0, 1)), containsAll(['A', 'B']));
    expect(camadas.where((l) => l.name.startsWith('C')), isEmpty);
    for (final l in camadas) {
      expect(l.endTime, lessThanOrEqualTo(const Duration(milliseconds: 2500)));
    }
    e.undo();
    expect(c.read(editorControllerProvider).layers, hasLength(3));
  });

  test('cronometro: conta, pausa guardando, retoma somando, apaga', () {
    var agora = DateTime(2026, 9, 15, 10);
    final cron = CronometroDeEdicao('proj-teste-cron', relogio: () => agora);
    cron.apagar();
    expect(cron.estado, EstadoDoCronometro.parado);
    cron.iniciar();
    agora = agora.add(const Duration(minutes: 2));
    expect(cron.estado, EstadoDoCronometro.rodando);
    expect(cron.total, const Duration(minutes: 2));
    cron.pausar();
    agora = agora.add(const Duration(hours: 1)); // parado nao conta
    expect(cron.estado, EstadoDoCronometro.pausado);
    expect(cron.total, const Duration(minutes: 2));
    cron.iniciar();
    agora = agora.add(const Duration(seconds: 30));
    expect(cron.total, const Duration(minutes: 2, seconds: 30));
    expect(textoDoCronometro(cron.total), '02:30');
    cron.apagar();
    expect(cron.total, Duration.zero);
    expect(textoDoCronometro(const Duration(hours: 1, minutes: 2, seconds: 3)), '1:02:03');
  });

  testWidgets('o ⋮ abre o menu: selecionar todas e modo de previa', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'A');
    e.addShapeLayer(Duration.zero, name: 'B');
    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: const MaterialApp(home: EditorScreen()),
      ),
    );
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('editor-menu')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('timeline-menu')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('timeline-menu-selecionar-todas')));
    await tester.pumpAndSettle();
    expect(c.read(multiSelectProvider), hasLength(2));

    c.read(multiSelectProvider.notifier).state = const {};
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('editor-menu')));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(
      find.byKey(const ValueKey('timeline-menu-modo-semEfeitos')),
      80,
      scrollable: find.descendant(
        of: find.byKey(const ValueKey('timeline-menu')),
        matching: find.byType(Scrollable),
      ),
    );
    await tester.tap(find.byKey(const ValueKey('timeline-menu-modo-semEfeitos')));
    await tester.pumpAndSettle();
    expect(c.read(opcoesDeVisualizacaoProvider).modo, ModoDePrevia.semEfeitos);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
