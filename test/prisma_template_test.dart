import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/projects/domain/prisma_template.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// PRISMA · o loop de dezessete segundos recriado da referencia.
///
/// O que se guarda aqui: a ESTRUTURA do loop (seis cenas, sem buraco e
/// sem pular, fechando no preto em que abre), o PESO (cabe no orcamento
/// de triangulos do pintor em CPU, cena a cena), e que o projeto abre e
/// salva como qualquer outro — textura gerada e keyframe amostrado tem
/// de sobreviver ao arquivo.
void main() {
  final projeto = buildPrismaTemplate();
  Duration t(num s) => Duration(microseconds: (s * 1000000).round());
  final cenas = projeto.layers
      .whereType<Scene3DLayer>()
      .where((l) => l.id != 'prisma_fundo')
      .toList()
    ..sort((a, b) => a.startTime.compareTo(b.startTime));
  final fundo = projeto.layers.whereType<Scene3DLayer>().singleWhere((l) => l.id == 'prisma_fundo');

  group('o loop', () {
    test('dezessete segundos, quadrado, 24 fps, seis cenas marcadas', () {
      expect(prismaDuration, const Duration(milliseconds: 17000));
      expect(projeto.aspectRatio, 1);
      expect(projeto.fps, 24);
      expect(cenas, hasLength(6));
      expect(projeto.markers, hasLength(6));
      for (var i = 0; i < 6; i++) {
        expect(projeto.markers[i].time, t(prismaCenas[i]));
        expect(cenas[i].startTime, t(prismaCenas[i]));
      }
    });

    test('as cenas cobrem o loop inteiro, cada uma comecando antes de a anterior acabar', () {
      expect(cenas.first.startTime, Duration.zero);
      for (var i = 1; i < cenas.length; i++) {
        final fimAnterior = cenas[i - 1].startTime + cenas[i - 1].duration;
        expect(cenas[i].startTime, lessThan(fimAnterior),
            reason: '${cenas[i].name} deixa um buraco depois de ${cenas[i - 1].name}');
      }
      expect(cenas.last.startTime + cenas.last.duration, prismaDuration);
      expect(fundo.startTime, Duration.zero);
      expect(fundo.duration, prismaDuration);
    });

    test('fecha no preto em que abre', () {
      // No fim, o plano preto cobre o quadro: 900 unidades de plano vezes
      // a escala passam do quadro de 900 mesmo girado (raiz de 2); no
      // comeco, as gemas estao fora do quadro.
      final feixe = cenas.last;
      final cortina = feixe.scene.nodes.singleWhere((n) => n.id == 'prisma_cortina');
      expect(cortina.scale.valueAt(t(2.35)), greaterThan(1.5));
      expect(cortina.scale.valueAt(t(1.4)), 0);
      final gemas = cenas.first;
      for (final id in ['prisma_gema_cima', 'prisma_gema_baixo']) {
        final g = gemas.scene.nodes.singleWhere((n) => n.id == id);
        expect(g.x.valueAt(Duration.zero).abs(), greaterThan(400));
        expect(g.y.valueAt(Duration.zero).abs(), greaterThan(600));
      }
    });
  });

  group('as cenas', () {
    test('as gemas se encontram ponta com ponta, pocos virados uma para a outra', () {
      final gemas = cenas.first.scene;
      final cima = gemas.nodes.singleWhere((n) => n.id == 'prisma_gema_cima');
      final baixo = gemas.nodes.singleWhere((n) => n.id == 'prisma_gema_baixo');
      expect(cima.x.valueAt(t(1.0)), closeTo(0, 1e-9));
      expect(baixo.x.valueAt(t(1.0)), closeTo(0, 1e-9));
      expect(cima.y.valueAt(t(1.0)), closeTo(-baixo.y.valueAt(t(1.0)), 1e-9));
      expect(cima.rotX.valueAt(t(1.0)), 180, reason: 'o poco da de cima olha para baixo');
      expect(baixo.rotX.valueAt(t(1.0)), 0);
      final faisca = gemas.nodes.singleWhere((n) => n.id == 'prisma_faisca');
      expect(faisca.scale.valueAt(t(1.0)), 0, reason: 'apagada antes do encontro');
      expect(faisca.scale.valueAt(t(1.32)), 1);
      expect(faisca.scale.valueAt(t(2.3)), 0);
    });

    test('o anel: seis discos com gradientes diferentes, nascendo do centro', () {
      final anel = cenas[1].scene;
      final discos = anel.nodes.where((n) => n.id.startsWith('prisma_disco_')).toList();
      expect(discos, hasLength(6));
      final texturas = {
        for (final d in discos) (d.modelAsset!.data['materials'] as List).first['image'],
      };
      expect(texturas, hasLength(6), reason: 'cada disco tem o seu gradiente');
      for (final d in discos) {
        final r0 = d.x.valueAt(Duration.zero).abs() + d.y.valueAt(Duration.zero).abs();
        expect(r0, lessThan(1), reason: 'nasce do centro');
        final x = d.x.valueAt(t(1.0)), y = d.y.valueAt(t(1.0));
        final r = (x * x + y * y);
        expect(r, greaterThan(150 * 150));
        expect(r, lessThan(280 * 280));
      }
    });

    test('o alvo: um grupo nulo, o quadrado vira e some, os aneis ficam', () {
      final alvo = cenas[3].scene;
      final grupo = alvo.nodes.singleWhere((n) => n.id == 'prisma_alvo_grupo');
      expect(grupo.isNull, isTrue);
      expect(grupo.rotZ.valueAt(t(0.5)), 45, reason: 'losango');
      expect(grupo.rotZ.valueAt(t(1.35)), closeTo(0, 1e-9), reason: 'quadrado');
      expect(grupo.scale.valueAt(t(4.45)), greaterThan(4), reason: 'o mergulho pelo furo');
      final quadrado = alvo.nodes.singleWhere((n) => n.id == 'prisma_alvo_quadrado');
      expect(quadrado.parentId, 'prisma_alvo_grupo');
      expect(quadrado.rotX.valueAt(t(1.75)), 90);
      expect(quadrado.scale.valueAt(t(2.0)), 0);
      final aneis = alvo.nodes.where((n) => n.id.startsWith('prisma_alvo_anel_')).toList();
      expect(aneis, hasLength(6));
      expect(aneis.every((a) => a.parentId == 'prisma_alvo_grupo'), isTrue);
    });

    test('os cones: o glitch so enquanto a esfera treme', () {
      final cones = cenas[4];
      // O efeito era `glitch`, que saiu do catalogo; virou `glitchify`, e
      // os dois parametros com equivalente claro foram renomeados junto
      // (quantidade -> amount, velocidade -> speed). A curva de entrada e
      // saida veio inteira: e ela que diz "so enquanto a esfera treme".
      final glitch = cones.effects.singleWhere(
        (e) => e.type == EffectType.glitchify,
      );
      expect(glitch.paramAt('amount', t(1.0)), 0);
      expect(glitch.paramAt('amount', t(3.0)), greaterThan(1.0));
      expect(glitch.paramAt('amount', t(4.0)), 0);
      // `rgbSplit` NAO tem equivalente no catalogo oficial e saiu do
      // template. O que se cobra agora e a ausencia: um tipo que nao
      // existe mais nao pode voltar para dentro do projeto.
      expect(
        cones.effects.any((e) => e.type == EffectType.rgbSplit),
        isFalse,
      );
      // A esfera cromada nasce na juncao e da lugar a esfera de glitch.
      final cromo = cones.scene.nodes.singleWhere((n) => n.id == 'prisma_esfera_cromo');
      expect(cromo.scale.valueAt(t(1.0)), 0);
      expect(cromo.scale.valueAt(t(2.0)), 1);
      expect(cromo.scale.valueAt(t(3.0)), 0);
      final pilula = cones.scene.nodes.singleWhere((n) => n.id == 'prisma_pilula');
      expect(pilula.scale.valueAt(t(3.0)), greaterThan(2));
      expect(pilula.x.valueAt(t(4.25)), greaterThan(600), reason: 'foge para o canto');
    });
  });

  group('o peso e o arquivo', () {
    test('cada cena cabe no orcamento do pintor em CPU, com o fundo junto', () {
      int tris(Scene3DLayer l) => prismaTriangles(
        projeto.copyWith(layers: [l]),
      );
      final doFundo = tris(fundo);
      for (final c in cenas) {
        expect(tris(c) + doFundo, lessThanOrEqualTo(prismaTriangleBudget),
            reason: '${c.name} passa do orcamento');
      }
    });

    test('e deterministico e sobrevive ao arquivo', () {
      // Os ids automaticos (keyframes) mudam a cada construcao; o FILME
      // nao pode mudar. Compara-se tudo menos ids.
      Object semIds(Object? v) => switch (v) {
        Map m => {for (final e in m.entries) if (e.key != 'id') e.key: semIds(e.value)},
        List l => [for (final x in l) semIds(x)],
        _ => v ?? '',
      };
      final a = projectToJson(buildPrismaTemplate());
      final b = projectToJson(buildPrismaTemplate());
      expect(semIds(a), semIds(b), reason: 'abrir duas vezes tem de dar o mesmo filme');
      final volta = projectFromJson(a);
      expect(volta.layers.length, projeto.layers.length);
      final gemas = volta.layers.whereType<Scene3DLayer>().singleWhere((l) => l.id == 'prisma_gemas');
      expect(gemas.scene.nodes.singleWhere((n) => n.id == 'prisma_gema_cima').x.valueAt(t(1.0)), closeTo(0, 1e-9));
    });

    test('abre no editor como qualquer projeto', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);
      final c = container.read(editorControllerProvider.notifier);
      c.openProject(projeto.comIdNovo());
      final aberto = container.read(editorControllerProvider);
      expect(aberto.layers.whereType<Scene3DLayer>(), hasLength(7));
      expect(aberto.name, contains('PRISMA'));
    });
  });
}
