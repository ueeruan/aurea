// O PALCO TEM DE SER ACONCHEGANTE (relato do dono, 16/09, com a planta
// na mao: "o tamanho do preview dele e aconchegante, ta vendo?").
//
// Ate entao o palco encolhia ate colar na composicao: um 16:9 num
// celular de 390 dava 219 px, o preview virava 26% da tela, e a mesma
// tela mudava de cara conforme o formato do projeto (um 9:16 dava 40%).
//
// A planta que ele mandou tem os dois formatos lado a lado, e neles o
// palco tem SEMPRE a mesma altura — o que muda e onde sobra folga. Este
// teste cobra exatamente isso: qualquer formato, o mesmo palco.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _SemProjetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

void main() {
  Future<double> fracaoDoPalco(
    WidgetTester tester,
    double proporcao,
    double w,
    double h,
  ) async {
    tester.view.physicalSize = Size(w, h);
    tester.view.devicePixelRatio = 1;
    final c = ProviderContainer(
      overrides: [projectsControllerProvider.overrideWith(_SemProjetos.new)],
    );
    c.read(editorControllerProvider.notifier).openProject(
      VideoProject(
        name: 'p',
        createdAt: DateTime(2026, 9, 16),
        aspectRatio: proporcao,
        resolutionHeight: 1080,
      ),
    );
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: const MaterialApp(home: EditorScreen()),
      ),
    );
    await tester.pumpAndSettle();
    final r = tester.getRect(find.byType(PreviewStage));
    await tester.pumpWidget(const SizedBox.shrink());
    c.dispose();
    return r.height / h;
  }

  testWidgets('o palco tem o mesmo tamanho em qualquer formato', (
    tester,
  ) async {
    addTearDown(tester.view.reset);
    const w = 390.0, h = 844.0;
    final larga = await fracaoDoPalco(tester, 16 / 9, w, h);
    final vertical = await fracaoDoPalco(tester, 9 / 16, w, h);
    final quadrada = await fracaoDoPalco(tester, 1, w, h);
    final cinema = await fracaoDoPalco(tester, 2.39, w, h);

    for (final (nome, f) in [
      ('16:9', larga),
      ('9:16', vertical),
      ('1:1', quadrada),
      ('2.39:1', cinema),
    ]) {
      expect(
        f,
        greaterThan(0.35),
        reason: '$nome: palco com ${(f * 100).round()}% da tela; '
            'a planta pede perto de 44%',
      );
      // E ele nao come a tela: a linha do tempo continua viva.
      expect(f, lessThan(0.55), reason: nome);
    }
    // O MESMO palco, nao um por formato.
    expect(larga, closeTo(vertical, 0.001));
    expect(quadrada, closeTo(vertical, 0.001));
    expect(cinema, closeTo(vertical, 0.001));
  });

  testWidgets('num celular pequeno o palco cede ao painel', (tester) async {
    // Numa tela curta o painel tem prioridade: com 16 px a mais no
    // palco, o trilho do painel de transformacao perdia um botao. O
    // palco continua bem maior do que era (era a altura da composicao,
    // 211 px num 16:9), mas nao a ponto de comer controle.
    addTearDown(tester.view.reset);
    final f = await fracaoDoPalco(tester, 16 / 9, 375, 667);
    expect(f, greaterThan(0.28), reason: '${(f * 100).round()}% da tela');
    expect(f, lessThan(0.40), reason: '${(f * 100).round()}% da tela');
  });
}
