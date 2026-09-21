// PROJETOS ANTIGOS ABREM NA CASCA NOVA — A TELA.
//
// A outra metade da regra do dono ("os projetos beta antigos precisam
// abrir na nova UI"): cada fixture de `projetos_antigos_test.dart` e
// aberta no `EditorScreen` de verdade, num iPhone 14 (390 x 844), e:
//
//   * a casca monta com o projeto, sem excecao;
//   * a timeline tem UMA linha por camada, e cada camada aparece na
//     timeline quando escolhida (a lista e preguicosa: a timeline rola
//     ate a escolhida — o mesmo que a pessoa ve);
//   * escolher CADA camada poe a barra de ferramentas DO TIPO dela
//     (`ferramentasDa`) na tela, sem excecao;
//   * CADA painel dessa barra abre com a camada antiga, sem excecao;
//   * dentro do grupo (precomp), os filhos aparecem e ganham a barra.
import 'dart:io';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/editor_shell.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/gestures.dart' show kDoubleTapTimeout;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_compat.dart';

/// A lista de projetos em memoria: o salvamento automatico escreve aqui.
class _ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];

  @override
  void upsert(VideoProject project) {
    state = [project];
  }
}

/// Abre [projeto] no editor de verdade, como a Home abre: o projeto entra
/// no controlador e a tela nasce depois.
Future<ProviderContainer> _abrirNaCasca(
  WidgetTester tester,
  VideoProject projeto,
) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final container = ProviderContainer(
    overrides: [
      projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
    ],
  );
  addTearDown(container.dispose);
  container.read(editorControllerProvider.notifier).openProject(projeto);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return container;
}

/// O projeto de uma pasta de repositorio, lido pelo `ProjectRepository`
/// (isolate de leitura: fora do relogio falso).
Future<VideoProject> _lerDoRepositorio(
  WidgetTester tester,
  String pasta,
) async {
  final copia = copiaTemporaria(pasta);
  addTearDown(() => copia.deleteSync(recursive: true));
  final lidos = await tester.runAsync(
    () => ProjectRepository(directory: copia).loadAll(),
  );
  return lidos!.single;
}

/// O que o percurso viu: as falhas (vazio = tudo abriu; o teste diz TODAS
/// de uma vez, nao so a primeira) e quanto foi exercitado.
typedef _Percurso = ({List<String> falhas, int camadas, int paineis});

/// ESCOLHE CADA CAMADA do nivel atual e confere linha, barra do tipo e
/// cada painel da barra.
Future<_Percurso> _percorrerCamadas(
  WidgetTester tester,
  ProviderContainer container,
) async {
  final falhas = <String>[];
  var paineis = 0;
  final camadas = container.read(editorControllerProvider).layers;
  for (final camada in camadas) {
    final rotulo = '${camada.runtimeType} "${camada.name}"';
    container.read(painelAbertoProvider.notifier).state = null;
    // A lista da timeline e preguicosa: escolher pelo estado REVELA a
    // linha (a timeline rola ate ela). Depois solta, e quem escolhe de
    // verdade e o DEDO no clipe — o cabecote esta no 0, no centro, e o
    // clipe comeca nele (mesmo toque do `editor_shell_test`).
    container.read(selectedLayerProvider.notifier).state = camada.id;
    await tester.pumpAndSettle();
    container.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    final linha = find.byKey(ValueKey('linha-${camada.id}'));
    if (linha.evaluate().length != 1) {
      falhas.add('$rotulo: sem linha na timeline');
      container.read(selectedLayerProvider.notifier).state = camada.id;
    } else {
      await tester.tapAt(tester.getCenter(linha) + const Offset(40, 0));
      // O clipe de GRUPO espera o toque duplo (que entra no grupo) antes
      // de aceitar o simples: o relogio falso precisa andar essa janela.
      await tester.pump(kDoubleTapTimeout + const Duration(milliseconds: 50));
    }
    await tester.pumpAndSettle();
    final erro = tester.takeException();
    if (erro != null) {
      falhas.add('$rotulo: escolher lancou $erro');
      continue;
    }
    if (container.read(selectedLayerProvider) != camada.id) {
      falhas.add('$rotulo: tocar no clipe nao escolheu a camada');
      container.read(selectedLayerProvider.notifier).state = camada.id;
      await tester.pumpAndSettle();
    }
    if (find.byKey(const ValueKey('barra-contextual')).evaluate().isEmpty) {
      falhas.add('$rotulo: sem barra de ferramentas');
      continue;
    }
    final ferramentas = ferramentasDa(camada);
    final primeira = ferramentas.first;
    if (find.byKey(ValueKey('ferramenta-${primeira.id}')).evaluate().isEmpty) {
      falhas.add('$rotulo: a barra nao e a do tipo (falta ${primeira.id})');
    }
    for (final f in ferramentas) {
      final painel = f.abre;
      if (painel == null) continue;
      container.read(painelAbertoProvider.notifier).state = painel;
      await tester.pumpAndSettle();
      paineis++;
      final erroDoPainel = tester.takeException();
      if (erroDoPainel != null) {
        falhas.add('$rotulo: painel ${painel.name} lancou $erroDoPainel');
      } else if (find
          .byKey(ValueKey('painel-${painel.name}'))
          .evaluate()
          .isEmpty) {
        falhas.add('$rotulo: painel ${painel.name} nao abriu');
      }
      container.read(painelAbertoProvider.notifier).state = null;
      await tester.pumpAndSettle();
    }
  }
  container.read(selectedLayerProvider.notifier).state = null;
  await tester.pumpAndSettle();
  return (falhas: falhas, camadas: camadas.length, paineis: paineis);
}

/// Confere o percurso e deixa os numeros no log do teste.
void _semFalhas(String nome, _Percurso r) {
  // ignore: avoid_print
  print(
    'CASCA [$nome]: ${r.camadas} camadas escolhidas, '
    '${r.paineis} paineis abertos, ${r.falhas.length} falhas',
  );
  expect(r.falhas, isEmpty, reason: r.falhas.join('\n'));
  expect(r.camadas, greaterThan(0));
  expect(r.paineis, greaterThan(0));
}

/// A casca montou com o projeto: as zonas estao la e a timeline nao abriu
/// vazia.
void _cascaMontada(WidgetTester tester, VideoProject p) {
  expect(tester.takeException(), isNull);
  expect(find.byType(EditorShell), findsOneWidget);
  expect(find.byType(TimelineDoEditor), findsOneWidget);
  expect(
    find.text('Toque em + para adicionar a primeira camada'),
    findsNothing,
    reason: 'a timeline nao pode abrir vazia com um projeto cheio',
  );
  // A primeira camada (a da frente) esta a vista sem rolar.
  expect(find.byKey(ValueKey('linha-${p.layers.first.id}')), findsOneWidget);
}

void main() {
  testWidgets('aparelho do beta 08/09 (audio): abre, cada camada tem linha, '
      'barra do tipo e paineis', (tester) async {
    final p = await _lerDoRepositorio(tester, repositorioBetaAudio);
    final c = await _abrirNaCasca(tester, p);
    _cascaMontada(tester, p);
    _semFalhas('beta 08/09', await _percorrerCamadas(tester, c));
  });

  testWidgets('projeto rico no formato do beta a02: abre, cada um dos 13 '
      'tipos de camada tem linha, barra do tipo e todos os paineis', (
    tester,
  ) async {
    final p = await _lerDoRepositorio(tester, repositorioRico);
    final c = await _abrirNaCasca(tester, p);
    _cascaMontada(tester, p);
    expect({
      for (final l in todasAsCamadas(p.layers)) l.runtimeType,
    }, hasLength(13));
    _semFalhas('rico beta a02', await _percorrerCamadas(tester, c));

    // DENTRO DO GRUPO (a precomp): os filhos viram as linhas e ganham a
    // barra e os paineis deles.
    final grupo = p.layers.whereType<GroupLayer>().single;
    c.read(editorControllerProvider.notifier).enterGroup(grupo.id);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(
      c.read(editorControllerProvider).layers.map((l) => l.id).toSet(),
      grupo.children.map((l) => l.id).toSet(),
    );
    _semFalhas(
      'rico beta a02, dentro do grupo',
      await _percorrerCamadas(tester, c),
    );
    c.read(editorControllerProvider.notifier).exitGroup();
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(c.read(editorControllerProvider).layers, hasLength(p.layers.length));
  });

  group('projetos de exemplo antigos em disco (output/, fora do git)', () {
    for (final caso in projetosEmDisco.entries) {
      final existe = File(caso.value).existsSync();
      testWidgets('${caso.key}: abre na casca, com barra e paineis', (
        tester,
      ) async {
        final p = abrirProjetoDoDisco(caso.value);
        final c = await _abrirNaCasca(tester, p);
        _cascaMontada(tester, p);
        _semFalhas(caso.key, await _percorrerCamadas(tester, c));
      }, skip: !existe);
    }
  });
}
