import 'dart:io';

import 'package:aurea/src/features/editor/application/interacao.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A LISTA DE PROJETOS NAO SE REFAZ A CADA PASSO DO DEDO.
///
/// O editor chama `upsert` a CADA mutacao, e arrastar uma camada sao
/// dezenas de mutacoes por segundo. Cada uma montava uma lista nova e
/// notificava a Inicio — que continua montada atras da rota do editor e
/// refazia a grade inteira, escondida, para ninguem ver.
///
/// O que estes testes prendem: durante um gesto a lista fica parada, e
/// quando o gesto acaba ela recebe o projeto UMA vez, com o valor final.
/// Fora de gesto (renomear pela Inicio) nada mudou: publica na hora.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late Directory pasta;
  late ProviderContainer container;
  late ProjectsController c;

  VideoProject projeto(String id, String nome) =>
      VideoProject(id: id, name: nome, createdAt: DateTime(2026, 9, 20));

  setUp(() async {
    // O sinal nasce desligado nos testes (ver [Interacao.ligada]); aqui a
    // simulacao do gesto e o proprio assunto.
    Interacao.ligada = true;
    pasta = Directory.systemTemp.createTempSync('aurea_projetos_');
    container = ProviderContainer(
      overrides: [
        projectRepositoryProvider.overrideWithValue(
          ProjectRepository(directory: pasta),
        ),
      ],
    );
    c = container.read(projectsControllerProvider.notifier);
    // O controlador le o disco numa microtarefa e reescreve `state`
    // quando termina. Deixar essa leitura acabar ANTES de contar
    // notificacoes: senao a do disco entra no meio e conta como se fosse
    // do gesto.
    await Future<void>.delayed(const Duration(milliseconds: 20));
  });

  tearDown(() async {
    container.dispose();
    Interacao.ligada = false;
    Interacao.zerar();
    // A leitura inicial do disco e as gravacoes sao assincronas: apagar a
    // pasta debaixo delas jogaria um erro no zone do teste que ja acabou.
    await Future<void>.delayed(const Duration(milliseconds: 20));
    try {
      pasta.deleteSync(recursive: true);
    } catch (_) {}
  });

  test('fora de gesto, o upsert publica na hora', () {
    var avisos = 0;
    container.listen(projectsControllerProvider, (_, _) => avisos++);
    c.upsert(projeto('a', 'Um'));
    expect(container.read(projectsControllerProvider).single.name, 'Um');
    expect(avisos, 1);
  });

  test('durante o gesto a lista nao e tocada nem uma vez', () {
    c.upsert(projeto('a', 'Um'));
    var avisos = 0;
    container.listen(projectsControllerProvider, (_, _) => avisos++);

    Interacao.marcar();
    for (var i = 0; i < 40; i++) {
      c.upsert(projeto('a', 'passo $i'));
      Interacao.marcar();
    }
    expect(avisos, 0, reason: 'quarenta passos do dedo, zero notificacoes');
    expect(
      container.read(projectsControllerProvider).single.name,
      'Um',
      reason: 'a Inicio ainda ve o que via antes do gesto',
    );
  });

  test('ao soltar, a lista recebe o valor final UMA vez', () {
    c.upsert(projeto('a', 'Um'));
    var avisos = 0;
    container.listen(projectsControllerProvider, (_, _) => avisos++);

    Interacao.marcar();
    for (var i = 0; i < 40; i++) {
      c.upsert(projeto('a', 'passo $i'));
    }
    Interacao.soltar();

    expect(avisos, 1);
    expect(container.read(projectsControllerProvider).single.name, 'passo 39');
  });

  test('gesto em dois projetos: um aviso por projeto ao soltar', () {
    c.upsert(projeto('a', 'A'));
    c.upsert(projeto('b', 'B'));
    var avisos = 0;
    container.listen(projectsControllerProvider, (_, _) => avisos++);

    Interacao.marcar();
    c.upsert(projeto('a', 'A2'));
    c.upsert(projeto('b', 'B2'));
    expect(avisos, 0);
    Interacao.soltar();
    expect(avisos, 2);
    final nomes = [
      for (final p in container.read(projectsControllerProvider)) p.name,
    ];
    expect(nomes, containsAll(<String>['A2', 'B2']));
  });

  test('RECENTE E RECENTE: quem mexeu por ultimo vai para a frente', () {
    c.upsert(projeto('a', 'A'));
    c.upsert(projeto('b', 'B'));
    expect(container.read(projectsControllerProvider).first.id, 'b');
    c.upsert(projeto('a', 'A2'));
    expect(container.read(projectsControllerProvider).first.id, 'a');
  });

  test('o flush publica o que o gesto deixou de fora', () async {
    c.upsert(projeto('a', 'Um'));
    Interacao.marcar();
    c.upsert(projeto('a', 'no meio do gesto'));
    expect(container.read(projectsControllerProvider).single.name, 'Um');
    await c.flush();
    expect(
      container.read(projectsControllerProvider).single.name,
      'no meio do gesto',
      reason: 'o app pode morrer em segundo plano: a lista vai em dia',
    );
  });

  test('apagar um projeto no meio de um gesto nao o ressuscita', () async {
    c.upsert(projeto('a', 'Um'));
    Interacao.marcar();
    c.upsert(projeto('a', 'dois'));
    c.remove('a');
    Interacao.soltar();
    expect(container.read(projectsControllerProvider), isEmpty);
    // O apagar em disco e assincrono: deixar terminar antes de a pasta
    // temporaria sumir no tearDown.
    await Future<void>.delayed(Duration.zero);
  });
}
