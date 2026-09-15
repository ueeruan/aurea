// VELOCIDADE (v1.1.1): quatro modos para onde vai a diferenca de duracao
// (estender inicio/fim, cortar inicio/fim), regua com imas e metade
// esquerda para camera lenta, valor digitado; camada sem midia explica
// em vez de mostrar controle que nao faz nada.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/velocidade.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/speed_sheet.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

Duration _seg(num n) => Duration(microseconds: (n * 1e6).round());

EnquadramentoDaVelocidade _conta(
  CompensacaoDaVelocidade modo,
  double nova, {
  Duration inicio = const Duration(seconds: 10),
  Duration duracao = const Duration(seconds: 4),
  Duration deslocamento = const Duration(seconds: 2),
  Duration? fonte = const Duration(seconds: 20),
  double atual = 1,
}) => enquadrarVelocidade(
  inicio: inicio,
  duracao: duracao,
  deslocamento: deslocamento,
  fonte: fonte,
  atual: atual,
  nova: nova,
  modo: modo,
)!;

ProviderContainer _container(Layer clipe) {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  c
      .read(editorControllerProvider.notifier)
      .openProject(
        VideoProject(name: 'p', createdAt: DateTime(2026, 9, 15), layers: [clipe]),
      );
  return c;
}

void main() {
  group('a conta dos quatro modos', () {
    test('estender fim: o comeco fica e a barra dobra na metade da velocidade', () {
      final e = _conta(CompensacaoDaVelocidade.estenderFim, .5);
      expect(e.inicio, _seg(10));
      expect(e.duracao, _seg(8));
      expect(e.deslocamento, _seg(2));
    });

    test('estender inicio: o fim fica no lugar', () {
      final e = _conta(CompensacaoDaVelocidade.estenderInicio, 2);
      expect(e.duracao, _seg(2));
      expect(e.inicio + e.duracao, _seg(14), reason: 'o fim nao anda');
      // Sem espaco antes do zero, o comeco para no zero.
      final curto = _conta(
        CompensacaoDaVelocidade.estenderInicio,
        .25,
        inicio: _seg(2),
      );
      expect(curto.inicio, Duration.zero);
      expect(curto.duracao, _seg(16));
    });

    test('cortar fim: a barra fica; sem fonte bastante, encurta pelo fim', () {
      final lento = _conta(CompensacaoDaVelocidade.cortarFim, .5);
      expect(lento.inicio, _seg(10));
      expect(lento.duracao, _seg(4));
      expect(lento.deslocamento, _seg(2));
      // 4 s a 8x pediriam 32 s de fonte; so ha 18 depois do ponto de entrada.
      final rapido = _conta(CompensacaoDaVelocidade.cortarFim, 8);
      expect(rapido.duracao, const Duration(milliseconds: 2250));
      expect(rapido.inicio, _seg(10));
      // Projeto antigo, sem medida da fonte: sem teto.
      final semFonte = _conta(CompensacaoDaVelocidade.cortarFim, 8, fonte: null);
      expect(semFonte.duracao, _seg(4));
    });

    test('cortar inicio: o ultimo quadro fica no lugar e a entrada anda', () {
      // Fonte tocada hoje: 2 s..6 s. A 0,5x cabem 2 s de fonte: 4 s..6 s.
      final lento = _conta(CompensacaoDaVelocidade.cortarInicio, .5);
      expect(lento.deslocamento, _seg(4));
      expect(lento.duracao, _seg(4));
      expect(lento.inicio, _seg(10));
      // A 4x a barra pediria 16 s de fonte antes de 6 s: so ha 6.
      final rapido = _conta(CompensacaoDaVelocidade.cortarInicio, 4);
      expect(rapido.deslocamento, Duration.zero);
      expect(rapido.duracao, const Duration(milliseconds: 1500));
      expect(rapido.inicio + rapido.duracao, _seg(14), reason: 'o fim fica');
    });

    test('barra curta demais nao existe', () {
      expect(
        enquadrarVelocidade(
          inicio: Duration.zero,
          duracao: const Duration(milliseconds: 100),
          deslocamento: Duration.zero,
          fonte: null,
          atual: 1,
          nova: 10,
          modo: CompensacaoDaVelocidade.estenderFim,
        ),
        isNull,
      );
    });
  });

  test('a regua gruda nos imas, anda de 0,05 e divide a faixa no 1x', () {
    expect(velocidadeDaRegua(.52), .5);
    expect(velocidadeDaRegua(1.04), 1);
    expect(velocidadeDaRegua(2.95), 3);
    expect(velocidadeDaRegua(1.37), 1.35);
    expect(velocidadeDaRegua(9), velocidadeMaximaDaRegua);
    expect(posicaoDaVelocidade(1), .5);
    expect(posicaoDaVelocidade(velocidadeMinima), 0);
    expect(posicaoDaVelocidade(4), 1);
    for (final v in [.1, .3, .75, 1.0, 1.8, 3.2, 4.0]) {
      expect(velocidadeDaPosicao(posicaoDaVelocidade(v)), closeTo(v, 1e-9));
    }
  });

  test('o controlador aplica o modo no clipe de audio', () {
    final c = _container(
      AudioLayer(
        id: 'a',
        name: 'musica',
        startTime: _seg(10),
        duration: _seg(4),
        sourcePath: '/a.m4a',
        sourceOffset: _seg(2),
        sourceDuration: _seg(20),
      ),
    );
    final e = c.read(editorControllerProvider.notifier);
    e.setClipSpeed('a', 2, modo: CompensacaoDaVelocidade.estenderInicio);
    var a = c.read(editorControllerProvider).layerById('a')! as AudioLayer;
    expect(a.speed, 2);
    expect(a.duration, _seg(2));
    expect(a.endTime, _seg(14));

    e.setClipSpeed('a', 1, modo: CompensacaoDaVelocidade.cortarInicio);
    a = c.read(editorControllerProvider).layerById('a')! as AudioLayer;
    expect(a.duration, _seg(2));
    expect(a.endTime, _seg(14));
    expect(a.sourceOffset, _seg(4), reason: 'saida em 6 s, 2 s de fonte a 1x');
  });

  testWidgets('a folha: modos, regua e numero mudam o clipe; sem midia explica', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container(
      AudioLayer(
        id: 'a',
        name: 'musica',
        startTime: _seg(10),
        duration: _seg(4),
        sourcePath: '/a.m4a',
        sourceDuration: _seg(20),
      ),
    );
    c
        .read(editorControllerProvider.notifier)
        .addTextLayer(Duration.zero);
    final texto = c
        .read(editorControllerProvider)
        .layers
        .firstWhere((l) => l is TextLayer)
        .id;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Consumer(
              builder: (context, ref, _) => Column(
                children: [
                  TextButton(
                    onPressed: () => showSpeedSheet(context, ref, 'a'),
                    child: const Text('abrir'),
                  ),
                  TextButton(
                    onPressed: () => showSpeedSheet(context, ref, texto),
                    child: const Text('texto'),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    for (final m in CompensacaoDaVelocidade.values) {
      expect(find.byKey(ValueKey('velocidade-modo-${m.name}')), findsOneWidget);
    }
    AudioLayer audio() =>
        c.read(editorControllerProvider).layerById('a')! as AudioLayer;

    await tester.tap(find.byKey(const ValueKey('velocidade-modo-estenderInicio')));
    await tester.pumpAndSettle();
    // Tocar na regua a tres quartos da largura: 2,5x.
    final regua = tester.getRect(find.byKey(const ValueKey('velocidade-regua')));
    await tester.tapAt(Offset(regua.left + regua.width * .75, regua.center.dy));
    await tester.pumpAndSettle();
    expect(audio().speed, 2.5);
    expect(audio().endTime, _seg(14), reason: 'estender inicio segura o fim');

    await tester.tap(find.byKey(const ValueKey('velocidade-valor')));
    await tester.pumpAndSettle();
    await tester.enterText(find.byKey(const ValueKey('valor-campo')), '0,5');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(audio().speed, .5);
    expect(audio().endTime, _seg(14));
    expect(find.text('0.50x'), findsWidgets);

    Navigator.of(tester.element(find.byKey(const ValueKey('velocidade-regua')))).pop();
    await tester.pumpAndSettle();
    await tester.tap(find.text('texto'));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('velocidade-sem-midia')), findsOneWidget);
    expect(find.byKey(const ValueKey('velocidade-regua')), findsNothing);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
