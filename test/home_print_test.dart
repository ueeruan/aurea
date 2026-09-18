// A INICIO MONTADA POR INTEIRO, com projetos de verdade, no tamanho de um
// iPhone: o nome do projeto aparece exatamente como foi digitado e a lista
// rola sem estourar. Com AUREA_PRINT_DIR apontando uma pasta, sai um PNG de
// cada estado — e sem ela nao se escreve nada.
//
// O PRINT E OPCIONAL, e o `toImage` vai por `tester.runAsync` (ver
// `apoio/print_da_ui.dart`): o readback da GPU precisa do relogio de
// verdade, e fora dele o futuro nunca completa — o teste pendura ate o
// limite de dez minutos em vez de falhar.
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';

import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/boas_vindas.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'apoio/print_da_ui.dart';
import 'apoio/repositorio_sem_disco.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => [
    for (var i = 1; i <= 8; i++) VideoProject.empty('Projeto $i'),
  ];
}

/// A FONTE NATIVA DO BOTAO NAO EXISTE NO flutter_tester: sem isto o rotulo
/// do `FilledButton` sai em retangulo preto no PNG (no aparelho nao
/// acontece). Mesma correcao de `inicio_e_comunidade_print_test`.
ThemeData _temaDeTeste() {
  final theme = AppTheme.dark;
  final button = theme.filledButtonTheme.style!;
  final resolvido = button.textStyle!.resolve({})!.copyWith(
    fontFamily: 'Roboto',
  );
  return theme.copyWith(
    filledButtonTheme: FilledButtonThemeData(
      style: button.copyWith(textStyle: WidgetStatePropertyAll(resolvido)),
    ),
  );
}

void main() {
  setUpAll(carregarFontesReais);

  testWidgets('a inicio monta com projetos e rola', (tester) async {
    tester.view.physicalSize = const Size(1170, 2532);
    tester.view.devicePixelRatio = 3;
    addTearDown(tester.view.reset);
    SharedPreferences.setMockInitialValues({
      chaveDoAceite: '2026-09-17T00:00:00',
      releaseNoticeSeenKey: releaseNoticeRevision,
    });
    final prefs = await SharedPreferences.getInstance();
    final container = ProviderContainer(
      overrides: [
        sharedPreferencesProvider.overrideWithValue(prefs),
        projectsControllerProvider.overrideWith(_Projetos.new),
        projectRepositoryProvider.overrideWithValue(RepositorioSemDisco()),
      ],
    );
    addTearDown(container.dispose);

    final chave = GlobalKey();
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: RepaintBoundary(
          key: chave,
          child: MaterialApp(
            theme: _temaDeTeste(),
            locale: const Locale('pt'),
            home: const HomeShell(),
          ),
        ),
      ),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));

    // O NOME COMO FOI DIGITADO, e nao um rotulo generico: e o que o
    // testador procura na Inicio quando tem muitos projetos.
    expect(find.text('Projeto 1'), findsOneWidget);
    expect(tester.takeException(), isNull);

    await gravarPrint(tester, chave, 'home_topo');

    await tester.drag(find.byType(CustomScrollView), const Offset(0, -900));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(tester.takeException(), isNull);

    await gravarPrint(tester, chave, 'home_rolada');
  });
}
