// GERA A FIXTURE DO PROJETO RICO (so quando pedido).
//
//   AUREA_GERAR_FIXTURE=1 flutter test test/compat/gerar_fixture_test.dart
//
// Monta o projeto rico pelo controlador e grava com o `ProjectRepository`
// de verdade (projeto + modelos separados, como no aparelho) em
// `test/compat/fixtures/repositorio_rico_beta_a02`. Sem a variavel, o
// teste e pulado: a fixture e um FORMATO CONGELADO, e regerar a toa
// apagaria justamente o que ela prova (que o arquivo de ontem abre hoje).
import 'dart:io';

import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_compat.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final gerar = Platform.environment['AUREA_GERAR_FIXTURE'] == '1';

  test('grava a fixture do projeto rico', () async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final projeto = await construirProjetoRico(container);
    final pasta = Directory(repositorioRico);
    if (pasta.existsSync()) pasta.deleteSync(recursive: true);
    pasta.createSync(recursive: true);
    final repo = ProjectRepository(directory: pasta);
    await repo.save(projeto);
    await repo.flush();
    final lidos = await ProjectRepository(directory: pasta).loadAll();
    expect(lidos, hasLength(1));
    expect(essencial(lidos.single), essencial(projeto));
  }, skip: gerar ? false : 'fixture congelada: AUREA_GERAR_FIXTURE=1 regera');
}
