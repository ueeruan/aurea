// A INICIO E A COMUNIDADE MONTADAS COM DADOS DE VERDADE, em dois
// tamanhos de celular: nada estoura, o nome do projeto aparece exatamente
// como foi digitado, e (com AUREA_PRINT_DIR) sai um PNG de cada tela.
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/community/application/comunidade_service.dart';
import 'package:aurea/src/features/community/application/conta_da_comunidade.dart';
import 'package:aurea/src/features/community/domain/post_da_comunidade.dart';
import 'package:aurea/src/features/community/presentation/community_tab.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/projects_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'apoio/print_da_ui.dart';
import 'apoio/repositorio_sem_disco.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => [
    VideoProject.empty('Projeto'),
    VideoProject.empty('Vinheta do canal — versão final'),
    VideoProject.empty('Efeitos'),
  ];
}

class _Mural extends ComunidadeService {
  @override
  Future<int?> totalDeUsuarios() async => 128;
  _Mural(this.feed);
  final List<PostDaComunidade> feed;
  @override
  Future<List<PostDaComunidade>> carregar({bool daRede = true}) async => feed;
  @override
  Future<List<PostDaComunidade>> respostas(String postId) async => const [];
}

Future<ProviderContainer> _montar(
  WidgetTester tester,
  Size tamanho,
  Widget tela,
  List<PostDaComunidade> feed,
) async {
  tester.view.physicalSize = tamanho;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  SharedPreferences.setMockInitialValues({
    'comunidade.conta': jsonEncode(
      ContaDaComunidade(
        id: 'conta-da-ana',
        apelido: 'Ana Motion',
        codigo: 'cd' * 24,
        criadaEm: DateTime(2026),
      ).toJson(),
    ),
  });
  final prefs = await SharedPreferences.getInstance();
  final c = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      projectsControllerProvider.overrideWith(_Projetos.new),
      projectRepositoryProvider.overrideWithValue(RepositorioSemDisco()),
      comunidadeServiceProvider.overrideWithValue(_Mural(feed)),
    ],
  );
  addTearDown(c.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: _temaDeTeste(),
        home: Scaffold(backgroundColor: AppColors.background, body: tela),
      ),
    ),
  );
  await tester.pump(const Duration(milliseconds: 400));
  await tester.pump(const Duration(milliseconds: 400));
  return c;
}

/// A fonte nativa do botao nao existe no flutter_tester: sem isto o texto
/// do FilledButton sai em retangulos no PNG (o app no aparelho nao tem o
/// problema). Mesma solucao de home_audit_layout_test.
ThemeData _temaDeTeste() {
  final theme = AppTheme.dark;
  final button = theme.filledButtonTheme.style!;
  final resolved = button.textStyle!
      .resolve({})!
      .copyWith(fontFamily: 'Roboto');
  return theme.copyWith(
    filledButtonTheme: FilledButtonThemeData(
      style: button.copyWith(textStyle: WidgetStatePropertyAll(resolved)),
    ),
  );
}

Widget _comFundo(GlobalKey chave, Widget tela) => RepaintBoundary(
  key: chave,
  child: ColoredBox(color: AppColors.background, child: tela),
);

void main() {
  setUpAll(carregarFontesReais);

  final imagem = File('assets/templates/dnyx/portrait.png').absolute.path;
  final feed = [
    PostDaComunidade(
      id: 'p1',
      autor: 'Bruno 3D',
      autorId: 'conta-do-bruno',
      texto: 'Terminei a vinheta com o rastreio de camera. O que acharam?',
      quando: DateTime.now().subtract(const Duration(hours: 2)),
      imagem: imagem,
      imagemLocal: true,
    ),
    PostDaComunidade(
      id: 'p2',
      autor: 'Luiza',
      autorId: 'conta-da-luiza',
      texto: 'Dica: segure o losango para abrir a curva de velocidade.',
      quando: DateTime.now().subtract(const Duration(days: 1)),
    ),
  ];

  for (final MapEntry(key: nome, value: tamanho) in const {
    'se': Size(375, 667),
    'pro-max': Size(430, 932),
  }.entries) {
    testWidgets('Inicio em $nome: sem estouro e nomes intactos', (
      tester,
    ) async {
      final chave = GlobalKey();
      await _montar(
        tester,
        tamanho,
        _comFundo(chave, const ProjectsTab()),
        feed,
      );
      expect(tester.takeException(), isNull);
      expect(find.text('Novo projeto'), findsOneWidget);
      // O nome da pessoa nao passa pelo dicionario: "Projeto" continua
      // "Projeto" mesmo sendo uma chave de traducao.
      expect(find.text('Projeto'), findsWidgets);
      expect(
        find
            .byType(AppText)
            .evaluate()
            .where(
              (e) =>
                  (e.widget as AppText).data ==
                  'Vinheta do canal — versão final',
            ),
        isEmpty,
      );
      await gravarPrint(tester, chave, 'inicio-$nome');
      await tester.pump(const Duration(seconds: 1));
    });

    testWidgets('Comunidade em $nome: feed sem estouro', (tester) async {
      final chave = GlobalKey();
      await _montar(
        tester,
        tamanho,
        _comFundo(chave, const CommunityTab()),
        feed,
      );
      await tester.pump(const Duration(milliseconds: 600));
      // A imagem do post decodifica fora do relogio falso do teste.
      // Com teto: no segundo tamanho a imagem ja esta no cache do teste
      // anterior e o precache pode nunca completar no relogio falso.
      await tester.runAsync(
        () => precacheImage(
          FileImage(File(imagem)),
          tester.element(find.byType(CommunityTab)),
        ).timeout(const Duration(seconds: 3), onTimeout: () {}),
      );
      await tester.pump();
      expect(tester.takeException(), isNull);
      expect(find.text('Bruno 3D'), findsWidgets);
      await gravarPrint(tester, chave, 'comunidade-$nome');
      // A fila de criadores filtra o feed por pessoa.
      await tester.tap(find.byKey(const ValueKey('criador-conta-da-luiza')));
      await tester.pump();
      expect(find.byKey(const ValueKey('post-p1')), findsNothing);
      expect(find.byKey(const ValueKey('post-p2')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('comunidade-ver-todos')));
      await tester.pump();
      expect(find.byKey(const ValueKey('post-p1')), findsOneWidget);
      await tester.pump(const Duration(seconds: 1));
    });
  }
}
