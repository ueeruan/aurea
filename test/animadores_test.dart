import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/animadores.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart' as kf;

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/gear.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/animador_sheet.dart';
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

Duration _s(double v) =>
    Duration(microseconds: (v * Duration.microsecondsPerSecond).round());

void main() {
  group('a forma do balanço', () {
    test('a onda vai de -1 a 1 e volta ao começo a cada volta', () {
      const a = AnimadorAutomatico(periodo: 4);
      expect(a.onda(0), closeTo(0, 1e-9));
      expect(a.onda(1), closeTo(1, 1e-9));
      expect(a.onda(3), closeTo(-1, 1e-9));
      expect(a.onda(4), closeTo(a.onda(0), 1e-9));
    });

    test('vaivém, rampa e pulso têm cada um o seu jeito', () {
      const vaivem = AnimadorAutomatico(
        tipo: TipoDoAnimador.triangulo,
        periodo: 4,
      );
      expect(vaivem.onda(0), closeTo(-1, 1e-9));
      expect(vaivem.onda(1), closeTo(0, 1e-9));
      expect(vaivem.onda(2), closeTo(1, 1e-9));
      const rampa = AnimadorAutomatico(tipo: TipoDoAnimador.dente, periodo: 4);
      expect(rampa.onda(0), closeTo(-1, 1e-9));
      expect(rampa.onda(3.999), greaterThan(.9));
      expect(rampa.onda(4), closeTo(-1, 1e-9), reason: 'recomeça do zero');
      const pulso = AnimadorAutomatico(tipo: TipoDoAnimador.pulso, periodo: 4);
      expect(pulso.onda(1), 1);
      expect(pulso.onda(3), -1);
    });

    test('o sorteio é macio, fica no limite e é sempre o mesmo', () {
      const a = AnimadorAutomatico(
        tipo: TipoDoAnimador.aleatorio,
        periodo: 1,
        semente: 7,
      );
      var anterior = a.onda(0);
      for (var i = 1; i <= 200; i++) {
        final v = a.onda(i / 20);
        expect(v.abs(), lessThanOrEqualTo(1.0001));
        expect((v - anterior).abs(), lessThan(.6), reason: 'sem pulo seco');
        anterior = v;
      }
      expect(a.onda(3.3), closeTo(a.onda(3.3), 1e-12));
      const outra = AnimadorAutomatico(
        tipo: TipoDoAnimador.aleatorio,
        periodo: 1,
        semente: 8,
      );
      expect(outra.onda(3.3), isNot(closeTo(a.onda(3.3), 1e-6)));
    });

    test('o eixo Y anda atrasado: dois eixos em fase dariam uma reta', () {
      const a = AnimadorAutomatico(periodo: 4, forca: 10);
      final p = a.valorDoPonto(Offset.zero, 0);
      expect(p.dx, closeTo(0, 1e-9));
      expect(p.dy, closeTo(10, 1e-9));
    });

    test('somar mexe em pixels; por cento mexe em fração do valor', () {
      const soma = AnimadorAutomatico(periodo: 4, forca: 30);
      expect(soma.valor(100, 1), closeTo(130, 1e-9));
      const vezes = AnimadorAutomatico(
        periodo: 4,
        forca: .5,
        modo: ModoDoAnimador.multiplicar,
      );
      expect(vezes.valor(100, 1), closeTo(150, 1e-9));
      expect(vezes.valor(100, 3), closeTo(50, 1e-9));
    });
  });

  group('a propriedade que anda sozinha', () {
    const a = AnimadorAutomatico(periodo: 4, forca: 10);

    test('o número balança sem nenhum keyframe', () {
      final v = kf.AnimatedDouble(5, null, kf.LoopSpec.none, null, a);
      expect(v.isAnimated, isFalse, reason: 'nenhum keyframe mesmo');
      expect(v.valueAt(Duration.zero), closeTo(5, 1e-9));
      expect(v.valueAt(_s(1)), closeTo(15, 1e-9));
    });

    test('a expressão manda no valor e o animador balança o que ela deu', () {
      final v = kf.AnimatedDouble(5, null, kf.LoopSpec.none, 'value * 2', a);
      expect(v.valueAt(_s(1)), closeTo(20, 1e-9));
    });

    test('o ponto balança nas duas pontas', () {
      final p = kf.AnimatedOffset(
        const Offset(100, 50),
        null,
        kf.LoopSpec.none,
        a,
      );
      expect(p.valueAt(_s(1)), const Offset(110, 50));
    });

    test('as cópias levam o animador junto', () {
      final v = kf.AnimatedDouble(5, null, kf.LoopSpec.none, null, a);
      expect(v.withBase(9).animador, a);
      expect(v.withExpression('value').animador, a);
      expect(v.withKeyframe(_s(1), 3).animador, a);
      expect(v.withLoop(const kf.LoopSpec(count: 2)).animador, a);
      expect(
        v.withKeyframe(_s(1), 3).withEaseAll(kf.Easing.linear).animador,
        a,
      );
      expect(v.withAnimador(null).animador, isNull);

      final p = kf.AnimatedOffset(Offset.zero, null, kf.LoopSpec.none, a);
      expect(p.withBase(const Offset(2, 2)).animador, a);
      expect(p.withKeyframe(_s(1), Offset.zero).animador, a);
      expect(p.withAnimador(null).animador, isNull);
    });

    test('tirar o último keyframe não assa o balanço dentro da base', () {
      final v = kf.AnimatedDouble(
        5,
        null,
        kf.LoopSpec.none,
        null,
        a,
      ).withKeyframe(_s(1), 40);
      final limpo = v.withoutKeyframe(_s(1));
      expect(limpo.isAnimated, isFalse);
      expect(limpo.animador, a);
      expect(limpo.base, closeTo(40, 1e-9), reason: 'o valor cru do keyframe');

      final p = kf.AnimatedOffset(
        Offset.zero,
        null,
        kf.LoopSpec.none,
        a,
      ).withKeyframe(_s(1), const Offset(7, 7));
      final limpoP = p.withoutKeyframe(_s(1));
      expect(limpoP.base, const Offset(7, 7));
      expect(limpoP.animador, a);
    });
  });

  test('o controlador põe e tira o animador da propriedade', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.single.id;
    Layer camada() => c.read(editorControllerProvider).layerById(id)!;

    const a = AnimadorAutomatico(periodo: 2, forca: 25);
    e.setPropAnimador(id, LayerProp.opacity, a);
    e.setPropAnimador(id, LayerProp.position, a);
    expect(e.propAnimador(camada(), LayerProp.opacity), a);
    expect(e.propAnimador(camada(), LayerProp.position), a);
    // A escala anda com as duas medidas juntas.
    e.setPropAnimador(id, LayerProp.scale, a);
    final l = camada();
    expect(l.scaleX.animador, a);
    expect(l.scaleY.animador, a);

    e.setPropAnimador(id, LayerProp.opacity, null);
    expect(e.propAnimador(camada(), LayerProp.opacity), isNull);
    expect(EditorController.propEhPonto(LayerProp.position), isTrue);
    expect(EditorController.propEhPonto(LayerProp.opacity), isFalse);
  });

  test('o animador vai e volta do arquivo', () {
    const a = AnimadorAutomatico(
      tipo: TipoDoAnimador.aleatorio,
      forca: 33,
      forcaY: 12,
      periodo: 1.5,
      fase: .25,
      semente: 9,
      modo: ModoDoAnimador.multiplicar,
    );
    final projeto = VideoProject.empty('Balanço').copyWith(
      layers: [
        ShapeLayer(
          name: 'Forma',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          opacity: kf.AnimatedDouble(1, null, kf.LoopSpec.none, null, a),
          position: kf.AnimatedOffset(
            const Offset(10, 20),
            null,
            kf.LoopSpec.none,
            a,
          ),
        ),
      ],
    );
    final volta = projectFromJson(projectToJson(projeto)).layers.single;
    expect(volta.opacity.animador, a);
    expect(volta.position.animador, a);
    expect(volta.position.animador!.forcaDoY, 12);
  });

  test('quem anda sozinho conta como animado: prévia e exportação', () {
    const a = AnimadorAutomatico(periodo: 2, forca: 25);
    final parada = ShapeLayer(
      name: 'Forma',
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
    );
    final andando = parada.copyLayer(
      opacity: kf.AnimatedDouble(1, null, kf.LoopSpec.none, null, a),
    );
    expect(parada.temAnimadorAutomatico, isFalse);
    expect(andando.temAnimadorAutomatico, isTrue);
    expect(andando.hasAnimation, isTrue, reason: 'sem keyframe, mas anda');
    // O portao da previa: sem isto, a camada so se mexeria ao tocar nela.
    expect(
      projectNeedsClockRebuild(
        VideoProject.empty('x').copyWith(layers: [andando]),
      ),
      isTrue,
    );
    expect(
      projectNeedsClockRebuild(
        VideoProject.empty('x').copyWith(layers: [parada]),
      ),
      isFalse,
    );
  });

  testWidgets('o toque longo no nome oferece animar sozinho', (tester) async {
    var pedidos = 0;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: Center(
            child: ParameterRow(
              label: 'Opacidade',
              value: 80,
              min: 0,
              max: 100,
              onChanged: (_) {},
              onReset: () {},
              onAnimador: () => pedidos++,
            ),
          ),
        ),
      ),
    );
    await tester.longPress(find.text('Opacidade'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('animador-propriedade')));
    await tester.pumpAndSettle();
    expect(pedidos, 1);
  });

  testWidgets('a folha põe, ajusta e tira o animador', (tester) async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.single.id;
    Layer camada() => c.read(editorControllerProvider).layerById(id)!;

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Consumer(
            builder: (context, ref, _) => Scaffold(
              body: Center(
                child: TextButton(
                  onPressed: () => showAnimadorSheet(
                    context,
                    ref,
                    id,
                    LayerProp.position,
                    nome: 'a posição',
                    unidade: 'px',
                  ),
                  child: const Text('abrir'),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    // Sem animador ainda: nada para tirar.
    expect(find.byKey(const ValueKey('animador-tirar')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('animador-tipo-triangulo')));
    await tester.pumpAndSettle();
    final posto = e.propAnimador(camada(), LayerProp.position)!;
    expect(posto.tipo, TipoDoAnimador.triangulo);
    // Um ponto tem as duas forças.
    expect(find.byKey(const ValueKey('animador-forca-y')), findsOneWidget);
    expect(find.byKey(const ValueKey('animador-tirar')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('animador-modo-multiplicar')));
    await tester.pumpAndSettle();
    expect(
      e.propAnimador(camada(), LayerProp.position)!.modo,
      ModoDoAnimador.multiplicar,
    );
    await tester.tap(find.byKey(const ValueKey('animador-tirar')));
    await tester.pumpAndSettle();
    expect(e.propAnimador(camada(), LayerProp.position), isNull);
    expect(tester.takeException(), isNull);
  });
}
