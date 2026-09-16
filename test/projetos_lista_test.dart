import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_view.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

VideoProject _p(String nome) => VideoProject.empty(nome);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('a lista arrumada', () {
    final projetos = [_p('Zebra'), _p('abacaxi'), _p('Mel')];

    test('recentes não reordena: a lista chega pronta da controladora', () {
      final saida = projetosArrumados(projetos, OrdemDosProjetos.recentes, '');
      expect(saida.map((p) => p.name), ['Zebra', 'abacaxi', 'Mel']);
    });

    test('por nome ignora maiúscula', () {
      final saida = projetosArrumados(projetos, OrdemDosProjetos.nome, '');
      expect(saida.map((p) => p.name), ['abacaxi', 'Mel', 'Zebra']);
    });

    test('a busca acha pedaço do nome, sem ligar para maiúscula', () {
      expect(
        projetosArrumados(
          projetos,
          OrdemDosProjetos.recentes,
          'ME',
        ).map((p) => p.name),
        ['Mel'],
      );
      expect(
        projetosArrumados(
          projetos,
          OrdemDosProjetos.recentes,
          'a',
        ).map((p) => p.name),
        ['Zebra', 'abacaxi'],
      );
      expect(
        projetosArrumados(projetos, OrdemDosProjetos.recentes, '  '),
        hasLength(3),
        reason: 'espaco em branco nao e busca',
      );
    });

    test('busca e ordem trabalham juntas', () {
      final saida = projetosArrumados(projetos, OrdemDosProjetos.nome, 'a');
      expect(saida.map((p) => p.name), ['abacaxi', 'Zebra']);
    });

    test('a lista original não é mexida', () {
      final antes = projetos.map((p) => p.name).toList();
      projetosArrumados(projetos, OrdemDosProjetos.nome, '');
      expect(projetos.map((p) => p.name), antes);
    });
  });

  group('a ordem escolhida', () {
    test('fica lembrada entre sessões', () async {
      SharedPreferences.setMockInitialValues({});
      final prefs = await SharedPreferences.getInstance();
      final c = ProviderContainer(
        overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
      );
      addTearDown(c.dispose);
      expect(c.read(ordemDosProjetosProvider), OrdemDosProjetos.recentes);
      c.read(ordemDosProjetosProvider.notifier).escolher(OrdemDosProjetos.nome);
      expect(prefs.getInt(OrdemDosProjetosNotifier.kChave), 1);

      final outro = ProviderContainer(
        overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
      );
      addTearDown(outro.dispose);
      expect(outro.read(ordemDosProjetosProvider), OrdemDosProjetos.nome);
    });

    test('sem prefs, continua no padrão em vez de estourar', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      expect(c.read(ordemDosProjetosProvider), OrdemDosProjetos.recentes);
      c
          .read(ordemDosProjetosProvider.notifier)
          .escolher(OrdemDosProjetos.duracao);
      expect(c.read(ordemDosProjetosProvider), OrdemDosProjetos.duracao);
    });

    test('todo jeito de ordenar tem nome na tela', () {
      for (final o in OrdemDosProjetos.values) {
        expect(rotuloDaOrdem(o).trim(), isNotEmpty, reason: o.name);
      }
    });
  });

  test(
    'a seleção começa vazia — e vazia quer dizer "não estou escolhendo"',
    () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      expect(c.read(selecaoDeProjetosProvider), isEmpty);
      c.read(selecaoDeProjetosProvider.notifier).state = {'a', 'b'};
      expect(c.read(selecaoDeProjetosProvider), hasLength(2));
    },
  );
}
