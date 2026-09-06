import 'dart:convert';
import 'dart:ui';

import 'package:aurea/src/features/editor/domain/expr.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/layout_ops.dart';
import 'package:aurea/src/features/editor/domain/lottie_export.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

ShapeLayer _shape(String name) => ShapeLayer(
      name: name,
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
      contents: ShapePresets.paramRect(),
    );

TextLayer _text(String name, String text) => TextLayer(
      name: name,
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
      text: text,
    );

VideoProject _p(List<Layer> layers) =>
    VideoProject(name: 'p', createdAt: DateTime(2026), layers: layers);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('aritmetica nos campos (PR-X4)', () {
    test('resolve expressao simples', () {
      expect(evalExpression('1080/3'), 360);
      expect(evalExpression('100+50'), 150);
      expect(evalExpression('(10+2)*5'), 60);
      expect(evalExpression('-40'), -40);
      expect(evalExpression('960'), 960);
    });

    test('aceita virgula decimal e milhar pt-BR', () {
      expect(evalExpression('1,5*2'), 3);
      expect(evalExpression('1.234,5'), 1234.5);
    });

    test('percentual com e sem base', () {
      expect(evalExpression('50%'), 0.5);
      expect(evalExpression('50%', percentOf: 1080), 540);
    });

    test('entrada invalida devolve null (campo mantem o valor)', () {
      expect(evalExpression('abc'), isNull);
      expect(evalExpression('10/0'), isNull);
      expect(evalExpression('(1+2'), isNull);
      expect(evalExpression(''), isNull);
    });
  });

  group('assistentes de keyframe (PR-X7)', () {
    test('sequenciar camadas escalona os inicios', () {
      final out = sequenceStarts([
        (
          id: 'a',
          start: Duration.zero,
          duration: const Duration(seconds: 1)
        ),
        (
          id: 'b',
          start: Duration.zero,
          duration: const Duration(seconds: 1)
        ),
        (
          id: 'c',
          start: Duration.zero,
          duration: const Duration(seconds: 1)
        ),
      ]);
      expect(out['a'], Duration.zero);
      expect(out['b'], const Duration(seconds: 1));
      expect(out['c'], const Duration(seconds: 2));
    });

    test('sobreposicao de 50% encavala as camadas', () {
      final out = sequenceStarts([
        (
          id: 'a',
          start: Duration.zero,
          duration: const Duration(seconds: 1)
        ),
        (
          id: 'b',
          start: Duration.zero,
          duration: const Duration(seconds: 1)
        ),
      ], overlap: 0.5);
      expect(out['b'], const Duration(milliseconds: 500));
    });

    test('distribuir no tempo espalha os inicios', () {
      final out = distributeInTime([
        (id: 'a', start: Duration.zero),
        (id: 'm', start: const Duration(milliseconds: 100)),
        (id: 'z', start: const Duration(seconds: 2)),
      ]);
      expect(out['m'], const Duration(seconds: 1));
      expect(out.containsKey('a'), isFalse);
    });

    test('escala exponencial cresce por razao constante', () {
      // De 10 a 1000: o meio geometrico e 100, nao 505.
      expect(exponentialScaleAt(10, 1000, 0.5), closeTo(100, 0.001));
      expect(exponentialScaleAt(10, 1000, 0), closeTo(10, 0.001));
      expect(exponentialScaleAt(10, 1000, 1), closeTo(1000, 0.01));
    });
  });

  group('motion blur (PR-X9)', () {
    test('fase -90 CENTRA a janela; 0 arrasta para frente', () {
      const centrado = MotionBlurSpec();
      final (a0, a1) = centrado.exposureWindow();
      expect(a0, closeTo(-0.25, 1e-9));
      expect(a1, closeTo(0.25, 1e-9));
      // Centrada: a soma das pontas e zero.
      expect(a0 + a1, closeTo(0, 1e-9));

      const arrastado = MotionBlurSpec(shutterPhase: 0);
      final (b0, b1) = arrastado.exposureWindow();
      expect(b0, closeTo(0, 1e-9));
      expect(b1, closeTo(0.5, 1e-9));
      // Janelas distintas: se dessem igual, a fase nao estaria aplicada.
      expect(a0, isNot(closeTo(b0, 0.01)));
    });

    test('angulo do obturador controla a largura da janela', () {
      const meio = MotionBlurSpec(shutterAngle: 90, shutterPhase: -45);
      final (c0, c1) = meio.exposureWindow();
      expect(c1 - c0, closeTo(0.25, 1e-9));
    });
  });

  group('layout responsivo (PR-X13/X14/X15)', () {
    test('ancora de crescimento: o lado escolhido fica parado', () {
      const antes = Size(100, 40);
      const depois = Size(300, 40);
      // Ancorado a esquerda, o centro anda metade do que cresceu.
      expect(anchorShift(antes, depois, GrowAnchor.centerLeft).dx, 100);
      // Ancorado a direita, anda para o outro lado.
      expect(anchorShift(antes, depois, GrowAnchor.centerRight).dx, -100);
      // Centrado, nao anda.
      expect(anchorShift(antes, depois, GrowAnchor.center).dx, 0);
    });

    test('conteiner abraca o texto com o preenchimento pedido', () {
      const spec = ContainerSpec(
          targetLayerId: 'x', padLeft: 20, padRight: 20, padTop: 10,
          padBottom: 10);
      final s = spec.sizeFor(const Size(200, 60));
      expect(s.width, 240);
      expect(s.height, 80);
    });

    test('conteiner respeita largura minima e maxima', () {
      const spec = ContainerSpec(
          targetLayerId: 'x', minWidth: 300, maxWidth: 500);
      expect(spec.sizeFor(const Size(10, 20)).width, 300);
      expect(spec.sizeFor(const Size(900, 20)).width, 500);
    });

    test('empilhamento distribui com o espaco pedido', () {
      final places = stackLayout(
        const StackSpec(gap: 10),
        [
          (id: 'a', size: const Size(100, 40)),
          (id: 'b', size: const Size(100, 60)),
          (id: 'c', size: const Size(100, 40)),
        ],
      );
      // Total = 140 + 20 de gaps = 160; comeca em -80.
      expect(places['a']!.dy, closeTo(-60, 1e-9));
      expect(places['b']!.dy, closeTo(0, 1e-9));
      expect(places['c']!.dy, closeTo(60, 1e-9));
    });

    test('remover o do meio reposiciona mantendo o espaco', () {
      final tres = stackLayout(const StackSpec(gap: 10), [
        (id: 'a', size: const Size(100, 40)),
        (id: 'b', size: const Size(100, 40)),
        (id: 'c', size: const Size(100, 40)),
      ]);
      final dois = stackLayout(const StackSpec(gap: 10), [
        (id: 'a', size: const Size(100, 40)),
        (id: 'c', size: const Size(100, 40)),
      ]);
      expect(dois['c']!.dy - dois['a']!.dy, closeTo(50, 1e-9));
      expect(tres['c']!.dy - tres['a']!.dy, closeTo(100, 1e-9));
    });

    test('alinhamento horizontal do empilhamento', () {
      final places = stackLayout(
        const StackSpec(align: StackAlign.start, gap: 0),
        [
          (id: 'largo', size: const Size(200, 20)),
          (id: 'estreito', size: const Size(100, 20)),
        ],
      );
      // Alinhados a esquerda: o estreito encosta na mesma borda.
      expect(places['largo']!.dx, closeTo(0, 1e-9));
      expect(places['estreito']!.dx, closeTo(-50, 1e-9));
    });
  });

  group('propriedades expostas (PR-X16)', () {
    test('limites impedem o cliente de quebrar o layout', () {
      const p = ExposedProperty(
        id: 'e1',
        layerId: 'l1',
        property: 'opacity',
        label: 'Opacidade',
        min: 0,
        max: 1,
        step: 0.25,
      );
      expect(p.clampValue(5), 1);
      expect(p.clampValue(-3), 0);
      expect(p.clampValue(0.4), 0.5);
    });
  });

  group('paleta e estilos (PR-X11/X12)', () {
    test('trocar a entrada muda so quem esta vinculado', () {
      final p = _p([_shape('a'), _shape('b')]).copyWith(
        meta: {
          'x': const LayerMeta(colorRef: 'primaria'),
        },
      );
      expect(p.paletteColorFor('x'), const Color(0xFFB8FF3D));
      final trocado =
          p.copyWith(palette: p.palette.withColor('primaria',
              const Color(0xFFFF0000)));
      expect(trocado.paletteColorFor('x'), const Color(0xFFFF0000));
      // Quem nao esta vinculado nao ve nada.
      expect(trocado.paletteColorFor('y'), isNull);
    });
  });

  group('organizacao (PR-X26)', () {
    test('solo: havendo um solo, so ele renderiza', () {
      final p = _p([_shape('a'), _shape('b')]);
      final ids = p.layers.map((l) => l.id).toList();
      expect(p.rendersInPreview(ids[0]), isTrue);
      final comSolo = p.copyWith(meta: {
        ids[1]: const LayerMeta(solo: true),
      });
      expect(comSolo.hasSolo, isTrue);
      expect(comSolo.rendersInPreview(ids[0]), isFalse);
      expect(comSolo.rendersInPreview(ids[1]), isTrue);
    });
  });

  group('dados (PR-X21/X22)', () {
    test('CSV vira colunas e linhas', () {
      final d = parseCsv('nome,valor\nAna,1200\nBia,980');
      expect(d.columns, ['nome', 'valor']);
      expect(d.rowCount, 2);
      expect(d.cell(0, 'nome'), 'Ana');
      expect(d.number(1, 'valor'), 980);
    });

    test('CSV com ponto-e-virgula e aspas', () {
      final d = parseCsv('a;b\n"tem; virgula";2');
      expect(d.cell(0, 'a'), 'tem; virgula');
      expect(d.number(0, 'b'), 2);
    });

    test('formatacao numerica', () {
      const f = NumberFormatSpec(decimals: 2, prefix: 'R\$ ');
      expect(f.format(1234.5), 'R\$ 1.234,50');
      const pct = NumberFormatSpec(percent: true, decimals: 1);
      expect(pct.format(0.256), '25,6%');
      const simples = NumberFormatSpec(thousands: false);
      expect(simples.format(42), '42');
    });

    test('contador formata o valor animado', () {
      final c = CounterSpec(
        value: AnimatedDouble(0)
            .withKeyframe(Duration.zero, 0)
            .withKeyframe(const Duration(seconds: 1), 1000),
        format: const NumberFormatSpec(suffix: ' un'),
      );
      expect(c.textAt(const Duration(milliseconds: 500)), '500 un');
      expect(c.textAt(const Duration(seconds: 1)), '1.000 un');
    });
  });

  group('cinematica inversa de dois ossos (PR-X18)', () {
    test('alvo alcancavel: a ponta chega no alvo', () {
      const root = Offset.zero;
      const target = Offset(120, 60);
      final sol = solveTwoBoneIk(
          root: root, target: target, l1: 100, l2: 100);
      expect(sol.reached, isTrue);
      final j = twoBoneJoints(root: root, l1: 100, l2: 100, sol: sol);
      expect((j.tip - target).distance, lessThan(0.01));
    });

    test('inverter a dobra espelha o cotovelo, mantendo a ponta', () {
      const root = Offset.zero;
      const target = Offset(120, 0);
      final a = solveTwoBoneIk(
          root: root, target: target, l1: 100, l2: 100);
      final b = solveTwoBoneIk(
          root: root, target: target, l1: 100, l2: 100, flip: true);
      final ja = twoBoneJoints(root: root, l1: 100, l2: 100, sol: a);
      final jb = twoBoneJoints(root: root, l1: 100, l2: 100, sol: b);
      expect(ja.elbow.dy * jb.elbow.dy, lessThan(0));
      expect((ja.tip - jb.tip).distance, lessThan(0.01));
    });

    test('fora do alcance: trava esticado, sem estourar', () {
      final sol = solveTwoBoneIk(
        root: Offset.zero,
        target: const Offset(500, 0),
        l1: 100,
        l2: 100,
      );
      expect(sol.reached, isFalse);
      final j =
          twoBoneJoints(root: Offset.zero, l1: 100, l2: 100, sol: sol);
      expect(j.tip.dx, closeTo(200, 0.01));
      expect(j.tip.dy, closeTo(0, 0.01));
    });

    test('esticar alonga os ossos ate o alvo', () {
      final sol = solveTwoBoneIk(
        root: Offset.zero,
        target: const Offset(300, 0),
        l1: 100,
        l2: 100,
        stretch: true,
      );
      expect(sol.reached, isFalse);
      expect(sol.angle1Deg.isFinite, isTrue);
    });
  });

  group('Lottie (PR-X23/X25)', () {
    test('validador acusa TODA camada nao suportada, sem falso negativo',
        () {
      final p = _p([
        _shape('forma'),
        VideoLayer(
            name: 'video',
            startTime: Duration.zero,
            duration: const Duration(seconds: 2),
            sourcePath: '/v.mp4'),
        ParticlesLayer(
            name: 'particulas',
            startTime: Duration.zero,
            duration: const Duration(seconds: 2)),
        Element3DLayer(
            name: 'cubo',
            startTime: Duration.zero,
            duration: const Duration(seconds: 2)),
        AdjustmentLayer(
            name: 'ajuste',
            startTime: Duration.zero,
            duration: const Duration(seconds: 2)),
      ]);
      final issues = validateForLottie(p);
      final nomes = issues.where((i) => i.blocking).map((i) => i.layerName);
      expect(nomes, containsAll(
          ['video', 'particulas', 'cubo', 'ajuste']));
      // A forma sobrevive: nao pode aparecer como bloqueante.
      expect(nomes, isNot(contains('forma')));
    });

    test('matriz de suporte e DADO: da para mudar sem tocar no codigo',
        () {
      final p = _p([
        ImageLayer(
            name: 'img',
            startTime: Duration.zero,
            duration: const Duration(seconds: 1),
            sourcePath: '/i.png'),
      ]);
      // Por padrao imagem e "parcial" (avisa, mas exporta).
      expect(validateForLottie(p).single.blocking, isFalse);
      // Uma biblioteca de destino sem suporte a imagem:
      const semImagem = LottieSupport(levels: {
        'image': LottieSupportLevel.none,
      });
      expect(
          validateForLottie(p, support: semImagem).single.blocking, isTrue);
    });

    test('exporta JSON valido com formas, transform e keyframes', () {
      final forma = _shape('quadrado').copyLayer(
        position: AnimatedOffset(const Offset(100, 100))
            .withKeyframe(Duration.zero, const Offset(0, 0))
            .withKeyframe(const Duration(seconds: 1), const Offset(200, 0)),
      );
      final out = exportLottie(_p([forma, _text('titulo', 'Aurea')]));
      expect(out.exported, 2);
      expect(out.skipped, 0);

      // Serializa de verdade (nada de objeto nao-JSON no meio).
      final encoded = jsonEncode(out.json);
      final back = jsonDecode(encoded) as Map<String, dynamic>;
      expect(back['fr'], 30);
      expect(back['w'], greaterThan(0));
      final layers = back['layers'] as List;
      expect(layers.length, 2);
      final shapeLayer = layers.first as Map<String, dynamic>;
      expect(shapeLayer['ty'], 4); // shape
      expect((shapeLayer['ks'] as Map)['p'], isA<Map>());
      // Posicao animada virou keyframes.
      expect(((shapeLayer['ks'] as Map)['p'] as Map)['a'], 1);
      final textLayer = layers[1] as Map<String, dynamic>;
      expect(textLayer['ty'], 5); // text
    });

    test('camada bloqueante e PULADA, nunca exportada quebrada', () {
      final out = exportLottie(_p([
        _shape('ok'),
        ParticlesLayer(
            name: 'p',
            startTime: Duration.zero,
            duration: const Duration(seconds: 1)),
      ]));
      expect(out.exported, 1);
      expect(out.skipped, 1);
      expect(out.issues.any((i) => i.blocking), isTrue);
    });

    test('SVG animado sai bem formado', () {
      final svg = exportAnimatedSvg(_p([_shape('a')]));
      expect(svg, startsWith('<svg'));
      expect(svg.trim(), endsWith('</svg>'));
      expect(svg, contains('<path'));
    });
  });

  group('persistencia da camada de oficio', () {
    test('meta, paleta, guias e dados sobrevivem ao roundtrip', () {
      final forma = _shape('card');
      final texto = _text('nome', 'Ana');
      final p = _p([forma, texto]).copyWith(
        meta: {
          forma.id: LayerMeta(
            label: LayerLabel.palette.first,
            solo: true,
            shy: true,
            colorRef: 'destaque',
            container: ContainerSpec(targetLayerId: texto.id, padLeft: 40),
            styles: LayerStyles(dropShadow: ShadowStyle(size: AnimatedDouble(20))),
          ),
        },
        palette: Palette.aurea.withColor('marca', const Color(0xFF123456)),
        guides: const GuidesSpec(vertical: [100, 200], columns: 12),
        motionBlur: const MotionBlurSpec(enabled: true, shutterPhase: -90),
        lottieMode: true,
        data: parseCsv('n,v\nAna,10'),
        bindings: [DataBinding(layerId: texto.id, column: 'n')],
      );

      final back = projectFromJson(
          jsonDecode(jsonEncode(projectToJson(p))) as Map<String, dynamic>);
      final m = back.metaOf(forma.id);
      expect(m.solo, isTrue);
      expect(m.shy, isTrue);
      expect(m.label!.color, LayerLabel.palette.first.color);
      expect(m.colorRef, 'destaque');
      expect(m.container!.targetLayerId, texto.id);
      expect(m.container!.padLeft, 40);
      expect(m.styles.dropShadow!.size.base, 20);
      expect(back.palette['marca'], const Color(0xFF123456));
      expect(back.guides.vertical, [100, 200]);
      expect(back.guides.columns, 12);
      expect(back.motionBlur.enabled, isTrue);
      expect(back.motionBlur.shutterPhase, -90);
      expect(back.lottieMode, isTrue);
      expect(back.data!.cell(0, 'n'), 'Ana');
      expect(back.bindings.single.column, 'n');
    });

    test('projeto sem a camada de oficio continua abrindo', () {
      final antigo = {
        'v': 1,
        'id': 'x',
        'name': 'velho',
        'createdAt': DateTime(2026).toIso8601String(),
        'aspect': 1.0,
        'fps': 30,
        'resH': 1080,
        'layers': <dynamic>[],
        'links': <dynamic>[],
      };
      final p = projectFromJson(antigo);
      expect(p.meta, isEmpty);
      expect(p.palette.entries, isNotEmpty);
      expect(p.motionBlur.enabled, isFalse);
    });
  });
}
