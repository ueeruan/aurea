import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/acoes_da_inicio.dart';
import 'package:aurea/src/features/projects/presentation/projects_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../apoio/repositorio_sem_disco.dart';

/// Os projetos da Inicio numa lista em memoria: nada de disco nem de
/// isolate (o relogio de mentira do testWidgets nao anda isolate).
class ProjetosDeTeste extends ProjectsController {
  ProjetosDeTeste(this.iniciais);

  final List<VideoProject> iniciais;

  @override
  List<VideoProject> build() => [...iniciais];
}

/// O que as portas trocadas registraram: o projeto posto no editor e o
/// projeto compartilhado.
class Registro {
  final carregados = <VideoProject>[];
  final compartilhados = <VideoProject>[];
}

/// A tela do editor de mentira: a Inicio empilha ESTA no lugar do editor
/// inteiro (motor, palco, timeline), e o teste confere so a navegacao.
const chaveDoEditorFalso = ValueKey('editor-falso');

class EditorFalso extends StatelessWidget {
  const EditorFalso({super.key});

  @override
  Widget build(BuildContext context) =>
      const Scaffold(key: chaveDoEditorFalso, body: SizedBox.expand());
}

/// Monta a Inicio sozinha (sem a casca de abas) no tamanho [tela].
Future<(ProviderContainer, Registro)> montarInicio(
  WidgetTester tester, {
  List<VideoProject> projetos = const [],
  Size tela = const Size(390, 844),
  Map<String, Object> prefsIniciais = const {},
}) async {
  tester.view.physicalSize = tela;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  SharedPreferences.setMockInitialValues(prefsIniciais);
  final prefs = await SharedPreferences.getInstance();
  final registro = Registro();
  final c = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      projectsControllerProvider.overrideWith(() => ProjetosDeTeste(projetos)),
      projectRepositoryProvider.overrideWithValue(RepositorioSemDisco()),
      carregarNoEditorProvider.overrideWithValue(registro.carregados.add),
      telaDoEditorProvider.overrideWithValue((_) => const EditorFalso()),
      compartilharProjetoProvider.overrideWithValue((p) async {
        registro.compartilhados.add(p);
        return true;
      }),
    ],
  );
  addTearDown(c.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: Scaffold(body: ProjectsTab())),
    ),
  );
  await tester.pump();
  return (c, registro);
}

/// Quadros contados em vez de pumpAndSettle: o cursor do Cupertino pisca
/// sem parar num campo com foco. Um quadro para a rota nascer e dois de
/// 300 ms para ela chegar (as folhas e menus levam 100-200 ms).
Future<void> assentar(WidgetTester tester) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 300));
  await tester.pump(const Duration(milliseconds: 300));
}
