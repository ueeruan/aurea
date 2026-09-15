import 'dart:ui';

import 'package:aurea/src/features/editor/domain/desenho_livre.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

/// Um anel fechado de tinta: quatro paredes de caneta em volta do centro.
List<TracoDoDesenho> _cercado(Rect r, {double espessura = 8}) {
  TracoDoDesenho parede(Offset a, Offset b) => TracoDoDesenho(
    ferramenta: FerramentaDeDesenho.caneta,
    pontos: [a, b],
    espessura: espessura,
  );
  return [
    parede(r.topLeft, r.topRight),
    parede(r.topRight, r.bottomRight),
    parede(r.bottomRight, r.bottomLeft),
    parede(r.bottomLeft, r.topLeft),
  ];
}

void main() {
  test('cada ferramenta tem a sua pincelada', () {
    final pontos = [
      const Offset(0, 0),
      const Offset(20, 10),
      const Offset(40, 0),
    ];
    final caneta = pinceladasDoTraco(
      TracoDoDesenho(
        ferramenta: FerramentaDeDesenho.caneta,
        pontos: pontos,
        espessura: 10,
      ),
    );
    expect(caneta.length, 1);
    expect(caneta.single.$2.strokeWidth, 10);
    expect(caneta.single.$2.blendMode, BlendMode.srcOver);

    // A BORRACHA tira tinta: e o modo de mistura que faz isso, nao uma
    // cor de fundo chutada.
    final borracha = pinceladasDoTraco(
      TracoDoDesenho(ferramenta: FerramentaDeDesenho.borracha, pontos: pontos),
    );
    expect(borracha.single.$2.blendMode, BlendMode.dstOut);

    // O PINCEL MACIO sao passadas cada vez mais finas; duro e uma so.
    final macio = pinceladasDoTraco(
      TracoDoDesenho(
        ferramenta: FerramentaDeDesenho.pincel,
        pontos: pontos,
        espessura: 20,
        dureza: .2,
      ),
    );
    expect(macio.length, greaterThan(1));
    expect(macio.first.$2.strokeWidth, greaterThan(macio.last.$2.strokeWidth));
    expect(
      macio.first.$2.color.a,
      lessThan(macio.last.$2.color.a),
      reason: 'a passada larga e a rala; o miolo e o forte',
    );
    final duro = pinceladasDoTraco(
      TracoDoDesenho(
        ferramenta: FerramentaDeDesenho.pincel,
        pontos: pontos,
        espessura: 20,
        dureza: 1,
      ),
    );
    expect(duro.length, 1);

    // O BALDE pinta por dentro E pela borda (a borda fecha a fresta da
    // grade ate a parede de tinta).
    final balde = pinceladasDoTraco(
      TracoDoDesenho(
        ferramenta: FerramentaDeDesenho.balde,
        pontos: [...pontos, const Offset(0, 20)],
      ),
    );
    expect(balde.length, 2);
    expect(balde.first.$2.style, PaintingStyle.fill);
    expect(balde.last.$2.style, PaintingStyle.stroke);
  });

  test('a opacidade vale para qualquer ferramenta', () {
    final t = TracoDoDesenho(
      ferramenta: FerramentaDeDesenho.caneta,
      pontos: [Offset.zero, const Offset(10, 0)],
      cor: const Color(0xFFFF0000),
      opacidade: .4,
    );
    expect(pinceladasDoTraco(t).single.$2.color.a, closeTo(.4, .01));

    // O PINCEL MACIO tambem: quatro passadas somadas nao podem passar da
    // opacidade pedida (era o que deixava o pincel meio transparente
    // pintando quase solido).
    final macio = pinceladasDoTraco(
      TracoDoDesenho(
        ferramenta: FerramentaDeDesenho.pincel,
        pontos: [Offset.zero, const Offset(10, 0)],
        dureza: .3,
        opacidade: .4,
      ),
    );
    var sobrou = 1.0;
    for (final (_, tinta) in macio) {
      sobrou *= 1 - tinta.color.a;
    }
    expect(1 - sobrou, closeTo(.4, .02), reason: 'o miolo para onde pediram');
  });

  group('o balde', () {
    final area = const Rect.fromLTRB(-200, -200, 200, 200);
    final anel = _cercado(const Rect.fromLTRB(-80, -60, 80, 60));

    test('pinta so a regiao cercada', () {
      final regiao = regiaoDoBalde(anel, Offset.zero, area);
      expect(regiao, isNotNull);
      final caminho = caminhoDoTraco(regiao!, fechado: true);
      final caixa = caminho.getBounds();
      // Fica dentro do cercado (com a folga de uma celula da grade).
      expect(caixa.left, greaterThan(-85));
      expect(caixa.right, lessThan(85));
      expect(caixa.top, greaterThan(-65));
      expect(caixa.bottom, lessThan(65));
      // E cobre quase todo o miolo.
      expect(caixa.width, greaterThan(120));
      expect(caixa.height, greaterThan(80));
      expect(caminho.contains(Offset.zero), isTrue);
    });

    test('o furo da borracha deixa a tinta escapar para fora', () {
      final comFuro = [
        ...anel,
        TracoDoDesenho(
          ferramenta: FerramentaDeDesenho.borracha,
          pontos: [const Offset(0, -80), const Offset(0, -40)],
          espessura: 20,
        ),
      ];
      final escapou = regiaoDoBalde(comFuro, Offset.zero, area);
      expect(escapou, isNotNull);
      expect(
        caminhoDoTraco(escapou!, fechado: true).getBounds().height,
        greaterThan(140),
        reason: 'sem parede inteira a tinta sai pelo furo',
      );
    });

    test('nao pinta em cima de tinta nem fora da composicao', () {
      expect(regiaoDoBalde(anel, const Offset(-80, 0), area), isNull);
      expect(regiaoDoBalde(anel, const Offset(500, 0), area), isNull);
    });

    test('sem nenhum traco, o balde pinta a composicao inteira', () {
      final tudo = regiaoDoBalde(const [], Offset.zero, area);
      expect(tudo, isNotNull);
      final caixa = caminhoDoTraco(tudo!, fechado: true).getBounds();
      expect(caixa.width, greaterThan(area.width - 20));
      expect(caixa.height, greaterThan(area.height - 20));
    });
  });

  test('o desenho na forma mede o que ocupa e sai como pinceladas', () {
    final desenho = ShapeDesenho(
      tracos: [
        TracoDoDesenho(
          ferramenta: FerramentaDeDesenho.caneta,
          pontos: [const Offset(-10, 0), const Offset(10, 0)],
          espessura: 8,
        ),
        TracoDoDesenho(
          ferramenta: FerramentaDeDesenho.borracha,
          pontos: [const Offset(0, -5), const Offset(0, 5)],
          espessura: 4,
        ),
      ],
    );
    final caixa = desenho.caixa!;
    expect(caixa.left, lessThanOrEqualTo(-14));
    expect(caixa.right, greaterThanOrEqualTo(14));
    expect(ShapeDesenho().caixa, isNull);

    final draws = evaluateShape([desenho], Duration.zero);
    // A caixa transparente (a medida da camada) mais os dois tracos.
    expect(draws.length, 3);
    expect(draws.first.paint.color.a, 0);
    expect(draws.last.paint.blendMode, BlendMode.dstOut);

    // A opacidade da camada multiplica a tinta do traco.
    final meio = evaluateShape([desenho], Duration.zero, opacity: .5);
    expect(meio[1].paint.color.a, closeTo(.5, .01));
  });

  test('o desenho vai e volta do arquivo', () {
    final traco = TracoDoDesenho(
      ferramenta: FerramentaDeDesenho.pincel,
      pontos: [const Offset(-3.25, 7.5), const Offset(12, -4)],
      cor: const Color(0xCC33AA55),
      espessura: 17,
      dureza: .3,
      opacidade: .6,
    );
    final projeto = VideoProject.empty('Desenho').copyWith(
      layers: [
        ShapeLayer(
          name: 'Desenho livre 1',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          contents: [
            ShapeDesenho(tracos: [traco]),
          ],
        ),
      ],
    );
    final volta = projectFromJson(projectToJson(projeto));
    final item = (volta.layers.single as ShapeLayer).contents.single;
    expect(item, isA<ShapeDesenho>());
    final lido = (item as ShapeDesenho).tracos.single;
    expect(lido.ferramenta, FerramentaDeDesenho.pincel);
    expect(lido.cor, traco.cor);
    expect(lido.espessura, 17);
    expect(lido.dureza, closeTo(.3, .001));
    expect(lido.opacidade, closeTo(.6, .001));
    expect(lido.pontos.length, 2);
    expect(lido.pontos.first.dx, closeTo(-3.25, .06));
    expect(lido.pontos.last.dy, closeTo(-4, .06));
  });
}
