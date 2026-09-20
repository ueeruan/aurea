// OPERACOES DE KEYFRAME — o que a rodada de setembro/2026 acrescentou.
//
// Tres regras, e os testes seguem essa ordem:
//
//   1. cravar uma marca no MEIO de um trecho nao pode mudar o desenho da
//      animacao (`Easing.dividirEm`, `comMarcaInserida`);
//   2. o arquivo guarda tudo o que a tela mostra (loop de posicao/pivo) e
//      abre mesmo com uma curva que esta versao nao conhece;
//   3. copiar, colar, apagar e mover falam de MARCA DE UMA PROPRIEDADE,
//      e nao do instante inteiro.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/keyframe_clipboard.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const t0 = Duration.zero;
const t1 = Duration(seconds: 1);
const t2 = Duration(seconds: 2);
const meio = Duration(milliseconds: 1000);

({ProviderContainer c, EditorController e, String id}) _bancada() {
  final c = ProviderContainer();
  final e = c.read(editorControllerProvider.notifier);
  e.addShapeLayer(t0);
  final id = c.read(editorControllerProvider).layers.single.id;
  return (c: c, e: e, id: id);
}

Layer _l(ProviderContainer c, String id) =>
    c.read(editorControllerProvider).layerById(id)!;

void main() {
  group('Easing.dividirEm — a marca no meio nao deforma o trecho', () {
    test('as duas metades reproduzem a curva original', () {
      const original = Easing.easeInOut;
      for (final f in [0.2, 0.5, 0.73]) {
        final (esquerda, direita) = original.dividirEm(f);
        final vNoCorte = original.transform(f);
        // Em qualquer instante, o valor da animacao PARTIDA tem de bater
        // com o da inteira: e literalmente a mesma curva, so que lida em
        // dois pedacos.
        for (var i = 0; i <= 20; i++) {
          final t = i / 20;
          final inteira = original.transform(t);
          final partida = t <= f
              ? esquerda.transform(t / f) * vNoCorte
              : vNoCorte +
                    direita.transform((t - f) / (1 - f)) * (1 - vNoCorte);
          expect(partida, closeTo(inteira, 0.01), reason: 'f=$f t=$t');
        }
      }
    });

    test('Manter continua Manter dos dois lados', () {
      final (a, b) = Easing.hold.dividirEm(0.4);
      expect(a.type, EasingType.hold);
      expect(b.type, EasingType.hold);
    });

    test('familia sem metade: a esquerda guarda a curva, a direita e reta', () {
      final (a, b) = Easing.bounce.dividirEm(0.5);
      expect(a.type, EasingType.bounce);
      expect(b.isLinear, isTrue);
    });

    test('nenhuma metade sai com valor nao finito', () {
      for (final e in [
        Easing.linear,
        Easing.easeIn,
        Easing.overshoot,
        const Easing(x1: 0, y1: 0, x2: 0, y2: 0),
        const Easing(x1: 1, y1: 1, x2: 1, y2: 1),
      ]) {
        for (final f in [0.001, 0.5, 0.999]) {
          final (a, b) = e.dividirEm(f);
          for (final v in [a.x1, a.y1, a.x2, a.y2, b.x1, b.y1, b.x2, b.y2]) {
            expect(v.isFinite, isTrue, reason: '$e em $f');
          }
        }
      }
    });
  });

  group('comMarcaInserida / toggleKeyframe', () {
    test('a marca nova herda a metade da curva, e nao nasce linear', () {
      final trilha = AnimatedDouble(0)
          .withKeyframe(t0, 0, Easing.easeInOut)
          .withKeyframe(t2, 100);
      final antes = trilha.valueAt(const Duration(milliseconds: 500));
      final depois = trilha.comMarcaInserida(t1);
      expect(depois.keyframes.length, 3);
      expect(
        depois.easeAt(t0).isLinear,
        isFalse,
        reason: 'a metade esquerda nao pode virar reta',
      );
      expect(
        depois.valueAt(const Duration(milliseconds: 500)),
        closeTo(antes, 0.6),
        reason: 'a animacao tem de ficar com o mesmo desenho',
      );
      expect(
        depois.valueAt(const Duration(milliseconds: 1500)),
        closeTo(trilha.valueAt(const Duration(milliseconds: 1500)), 0.6),
      );
    });

    test('fora de qualquer trecho a marca nasce reta, como sempre', () {
      final trilha = AnimatedDouble(0).withKeyframe(t0, 0, Easing.easeInOut);
      final depois = trilha.comMarcaInserida(t2);
      expect(depois.keyframes.length, 2);
      expect(depois.easeAt(t2).isLinear, isTrue);
    });

    test('o losango do editor usa a divisao', () {
      final b = _bancada();
      addTearDown(b.c.dispose);
      b.e.toggleKeyframe(b.id, t0, LayerProp.opacity);
      b.e.editOpacity(b.id, t0, 1);
      b.e.toggleKeyframe(b.id, t2, LayerProp.opacity);
      b.e.editOpacity(b.id, t2, 0);
      b.e.setSegmentEase(b.id, LayerProp.opacity, t0, Easing.easeInOut);
      final antes = _l(b.c, b.id).opacity.valueAt(meio);
      b.e.toggleKeyframe(b.id, t1, LayerProp.opacity);
      final trilha = _l(b.c, b.id).opacity;
      expect(trilha.keyframes.length, 3);
      expect(trilha.valueAt(meio), closeTo(antes, 0.01));
      expect(trilha.easeAt(t0).isLinear, isFalse);
    });

    test('a mesma regra vale para a posicao (AnimatedOffset)', () {
      final trilha = AnimatedOffset(Offset.zero)
          .withKeyframe(t0, Offset.zero, Easing.easeInOut)
          .withKeyframe(t2, const Offset(100, 0));
      final antes = trilha.valueAt(const Duration(milliseconds: 500)).dx;
      final depois = trilha.comMarcaInserida(t1);
      expect(depois.keyframes.length, 3);
      expect(
        depois.valueAt(const Duration(milliseconds: 500)).dx,
        closeTo(antes, 0.6),
      );
    });
  });

  group('arquivo', () {
    test('o loop de Posicao e de Pivo sobrevive a ida e volta', () {
      final projeto = VideoProject(
        name: 'loop',
        createdAt: DateTime(2026, 9, 20),
        layers: [
          ShapeLayer(
            id: 'a',
            name: 'a',
            startTime: t0,
            duration: t2,
            position: AnimatedOffset(Offset.zero)
                .withKeyframe(t0, Offset.zero)
                .withKeyframe(t1, const Offset(50, 0))
                .withLoop(const LoopSpec(mode: LoopMode.cycle, count: 3)),
            pivot: AnimatedOffset(Offset.zero)
                .withKeyframe(t0, Offset.zero)
                .withKeyframe(t1, const Offset(1, 1))
                .withLoop(const LoopSpec(mode: LoopMode.pingPong)),
          ),
        ],
      );
      final volta = projectFromJson(projectToJson(projeto));
      final camada = volta.layerById('a')!;
      expect(camada.position.loop.mode, LoopMode.cycle);
      expect(camada.position.loop.count, 3);
      expect(camada.pivot.loop.mode, LoopMode.pingPong);
    });

    test('projeto antigo, sem a chave do loop, abre igual', () {
      final json = projectToJson(
        VideoProject(
          name: 'antigo',
          createdAt: DateTime(2026, 9, 20),
          layers: [
            ShapeLayer(
              id: 'a',
              name: 'a',
              startTime: t0,
              duration: t2,
              position: AnimatedOffset(const Offset(3, 4)),
            ),
          ],
        ),
      );
      final camada = (json['layers'] as List).first as Map<String, dynamic>;
      expect((camada['pos'] as Map).containsKey('loop'), isFalse);
      final volta = projectFromJson(json);
      expect(volta.layerById('a')!.position.loop.mode, LoopMode.none);
      expect(volta.layerById('a')!.position.valueAt(t0), const Offset(3, 4));
    });

    test('curva de uma versao futura nao impede o projeto de abrir', () {
      final json = projectToJson(
        VideoProject(
          name: 'futuro',
          createdAt: DateTime(2026, 9, 20),
          layers: [
            ShapeLayer(
              id: 'a',
              name: 'a',
              startTime: t0,
              duration: t2,
              opacity: AnimatedDouble(1)
                  .withKeyframe(t0, 1, Easing.easeIn)
                  .withKeyframe(t1, 0),
            ),
          ],
        ),
      );
      final camada = (json['layers'] as List).first as Map<String, dynamic>;
      final marcas = (camada['op'] as Map)['k'] as List;
      ((marcas.first as Map)['e'] as Map)['t'] = 999;
      final volta = projectFromJson(json);
      expect(
        volta.layerById('a')!.opacity.easeAt(t0).type,
        EasingType.cubicBezier,
      );
    });
  });

  group('setPropertyLoop cobre todos os eixos do grupo', () {
    test('Rotacao marca X, Y e Z; Inclinacao marca os dois', () {
      final b = _bancada();
      addTearDown(b.c.dispose);
      b.e.setPropertyLoop(
        b.id,
        LayerProp.rotation,
        const LoopSpec(mode: LoopMode.cycle),
      );
      b.e.setPropertyLoop(
        b.id,
        LayerProp.skew,
        const LoopSpec(mode: LoopMode.pingPong),
      );
      final l = _l(b.c, b.id);
      expect(l.rotation.loop.mode, LoopMode.cycle);
      expect(l.rotationX.loop.mode, LoopMode.cycle);
      expect(l.rotationY.loop.mode, LoopMode.cycle);
      expect(l.skewX.loop.mode, LoopMode.pingPong);
      expect(l.skewY.loop.mode, LoopMode.pingPong);
    });
  });

  group('CatalogoDeCurvas — a fonte unica dos presets', () {
    test('a primeira linha tem os seis, com Hold e Bezier', () {
      final nomes = [for (final p in CatalogoDeCurvas.basicos) p.nome];
      expect(nomes, [
        'Linear',
        'Ease in',
        'Ease out',
        'Ease in-out',
        'Hold',
        'Bézier',
      ]);
      expect(CatalogoDeCurvas.basicos[4].ease.type, EasingType.hold);
      expect(CatalogoDeCurvas.basicos.last.personalizada, isTrue);
    });

    test('"Bezier" nao joga fora as alcas que a pessoa ajustou', () {
      const minha = Easing(x1: .9, y1: .1, x2: .2, y2: .8);
      final chip = CatalogoDeCurvas.basicos.last;
      expect(CatalogoDeCurvas.aoEscolher(chip, minha), minha);
      expect(CatalogoDeCurvas.aceso(chip, minha), isTrue);
      // De outra familia, o chip da o ponto de partida.
      expect(CatalogoDeCurvas.aoEscolher(chip, Easing.hold).type,
          EasingType.cubicBezier);
      expect(CatalogoDeCurvas.aceso(chip, Easing.hold), isFalse);
    });

    test('a faixa "alem dos basicos" nao repete a primeira linha', () {
      for (final p in CatalogoDeCurvas.alemDosBasicos) {
        expect(
          CatalogoDeCurvas.basicos.any(
            (b) => !b.personalizada && b.ease.mesmoPresetQue(p.ease),
          ),
          isFalse,
          reason: '${p.nome} ja esta na primeira linha',
        );
      }
      // E Manter continua acessivel pela familia Degraus.
      expect(
        CatalogoDeCurvas.familias[2].presets.any(
          (p) => p.ease.type == EasingType.hold,
        ),
        isTrue,
      );
    });

    test('a aba nasce na familia do trecho', () {
      expect(CatalogoDeCurvas.familiaDe(Easing.easeIn), 0);
      expect(CatalogoDeCurvas.familiaDe(Easing.bounce), 1);
      expect(CatalogoDeCurvas.familiaDe(Easing.hold), 2);
      expect(CatalogoDeCurvas.familiaDe(Easing.interfaceSpring), 3);
    });
  });

  group('copiar, colar, apagar e mover por propriedade', () {
    ({ProviderContainer c, EditorController e, String id}) animada() {
      final b = _bancada();
      b.e.toggleKeyframe(b.id, t0, LayerProp.opacity);
      b.e.editOpacity(b.id, t0, 1);
      b.e.toggleKeyframe(b.id, t1, LayerProp.opacity);
      b.e.editOpacity(b.id, t1, 0);
      b.e.setSegmentEase(b.id, LayerProp.opacity, t0, Easing.bounce);
      return b;
    }

    test('copiar preserva valor, relacao de tempo, interpolacao e alcas', () {
      final b = animada();
      addTearDown(b.c.dispose);
      final n = b.e.copiarKeyframes([
        (layerId: b.id, prop: LayerProp.opacity, tempo: t0),
        (layerId: b.id, prop: LayerProp.opacity, tempo: t1),
      ]);
      expect(n, 2);
      expect(KeyframeClipboard.temAlgo, isTrue);

      final r = b.e.colarKeyframes(b.id, t2);
      expect(r.coladas, 2);
      expect(r.foraDaCamada, 0);
      final trilha = _l(b.c, b.id).opacity;
      expect(trilha.hasKeyframeAt(t2), isTrue);
      expect(trilha.hasKeyframeAt(const Duration(seconds: 3)), isTrue);
      expect(trilha.valueAt(t2), 1);
      expect(trilha.valueAt(const Duration(seconds: 3)), 0);
      expect(trilha.easeAt(t2).type, EasingType.bounce);
      // As coladas viram a selecao.
      expect(b.c.read(keyframesSelecionadosProvider), hasLength(2));
      // E tudo isso num passo de desfazer so.
      b.e.undo();
      expect(_l(b.c, b.id).opacity.keyframes, hasLength(2));
    });

    test('colar fora da camada conta, e nao entra calado', () {
      final b = animada();
      addTearDown(b.c.dispose);
      b.e.copiarKeyframes([
        (layerId: b.id, prop: LayerProp.opacity, tempo: t0),
        (layerId: b.id, prop: LayerProp.opacity, tempo: t1),
      ]);
      final duracao = _l(b.c, b.id).duration;
      final r = b.e.colarKeyframes(b.id, duracao - const Duration(milliseconds: 500));
      expect(r.foraDaCamada, 1);
      expect(r.coladas, 1);
    });

    test('apagar leva so a propriedade pedida, num desfazer', () {
      final b = animada();
      addTearDown(b.c.dispose);
      b.e.toggleKeyframe(b.id, t1, LayerProp.position);
      expect(_l(b.c, b.id).position.hasKeyframeAt(t1), isTrue);

      final n = b.e.apagarKeyframes([
        (layerId: b.id, prop: LayerProp.opacity, tempo: t1),
      ]);
      expect(n, 1);
      expect(_l(b.c, b.id).opacity.hasKeyframeAt(t1), isFalse);
      expect(
        _l(b.c, b.id).position.hasKeyframeAt(t1),
        isTrue,
        reason: 'a posicao nao foi pedida',
      );
      b.e.undo();
      expect(_l(b.c, b.id).opacity.hasKeyframeAt(t1), isTrue);
    });

    test('mover uma propriedade deixa as outras onde estao', () {
      final b = animada();
      addTearDown(b.c.dispose);
      b.e.toggleKeyframe(b.id, t1, LayerProp.position);
      const destino = Duration(milliseconds: 1500);
      expect(
        b.e.moverKeyframeDaProp(b.id, LayerProp.opacity, t1, destino),
        isNull,
      );
      final l = _l(b.c, b.id);
      expect(l.opacity.hasKeyframeAt(destino), isTrue);
      expect(l.opacity.hasKeyframeAt(t1), isFalse);
      expect(l.opacity.valueAt(destino), 0);
      expect(l.position.hasKeyframeAt(t1), isTrue);
    });

    test('mover para cima de outra marca e recusado, sem mexer em nada', () {
      final b = animada();
      addTearDown(b.c.dispose);
      expect(
        b.e.moverKeyframeDaProp(b.id, LayerProp.opacity, t0, t1),
        MotivoDoKeyframeParado.ocupado,
      );
      expect(
        [for (final k in _l(b.c, b.id).opacity.keyframes) k.time],
        [t0, t1],
      );
    });

    test('mover a selecao inteira e tudo ou nada', () {
      final b = animada();
      addTearDown(b.c.dispose);
      final marcas = {
        (layerId: b.id, prop: LayerProp.opacity, tempo: t0),
        (layerId: b.id, prop: LayerProp.opacity, tempo: t1),
      };
      // Para tras, a primeira sairia da camada: ninguem anda.
      expect(
        b.e.moverKeyframes(marcas, const Duration(milliseconds: -500)),
        MotivoDoKeyframeParado.foraDaCamada,
      );
      expect(
        [for (final k in _l(b.c, b.id).opacity.keyframes) k.time],
        [t0, t1],
      );
      // Para a frente, as duas andam juntas e mantem a distancia.
      b.c.read(keyframesSelecionadosProvider.notifier).state = marcas;
      expect(b.e.moverKeyframes(marcas, const Duration(milliseconds: 500)), isNull);
      expect(
        [for (final k in _l(b.c, b.id).opacity.keyframes) k.time],
        [const Duration(milliseconds: 500), const Duration(milliseconds: 1500)],
      );
      // E a selecao acompanha as marcas.
      expect(
        {for (final m in b.c.read(keyframesSelecionadosProvider)) m.tempo},
        {const Duration(milliseconds: 500), const Duration(milliseconds: 1500)},
      );
    });
  });

  group('selecao de keyframe (estado de tela)', () {
    test('o toque soma e tira a mesma marca', () {
      const a = (layerId: 'x', prop: LayerProp.opacity, tempo: t1);
      var sel = alternarMarca(const {}, a);
      expect(marcaSelecionada(sel, a), isTrue);
      // Um tempo a 3 ms de distancia e A MESMA marca (a regua das trilhas).
      const quase = (
        layerId: 'x',
        prop: LayerProp.opacity,
        tempo: Duration(milliseconds: 1003),
      );
      expect(marcaSelecionada(sel, quase), isTrue);
      sel = alternarMarca(sel, quase);
      expect(sel, isEmpty);
    });

    test('o losango da timeline seleciona todas as props do instante', () {
      final b = _bancada();
      addTearDown(b.c.dispose);
      b.e.toggleKeyframe(b.id, t1, LayerProp.opacity);
      b.e.toggleKeyframe(b.id, t1, LayerProp.position);
      final marcas = marcasDoInstante(_l(b.c, b.id), t1);
      expect({for (final m in marcas) m.prop}, {
        LayerProp.position,
        LayerProp.opacity,
      });
    });

    test('a selecao nao fica apontando para marca que nao existe mais', () {
      final b = _bancada();
      addTearDown(b.c.dispose);
      b.e.toggleKeyframe(b.id, t1, LayerProp.opacity);
      b.c.read(keyframesSelecionadosProvider.notifier).state = {
        (layerId: b.id, prop: LayerProp.opacity, tempo: t1),
        (layerId: b.id, prop: LayerProp.scale, tempo: t1),
      };
      b.e.podarKeyframesSelecionados();
      expect(b.c.read(keyframesSelecionadosProvider), hasLength(1));
      expect(
        b.c.read(keyframesSelecionadosProvider).single.prop,
        LayerProp.opacity,
      );
    });
  });
}
