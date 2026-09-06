import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';

Duration _s(num v) => Duration(milliseconds: (v * 1000).round());

GroupLayer _grupo({
  Duration? sourceDuration,
  AnimatedDouble? timeRemap,
  bool collapse = false,
  bool clipToComp = true,
}) =>
    GroupLayer(
      name: 'Precomp',
      startTime: _s(2),
      duration: _s(5),
      sourceDuration: sourceDuration,
      timeRemap: timeRemap,
      collapse: collapse,
      clipToComp: clipToComp,
      children: [
        TextLayer(
          name: 'Dentro',
          startTime: Duration.zero,
          duration: _s(5),
          text: 'oi',
        ),
      ],
    );

void main() {
  group('Duracao interna', () {
    test('sem duracao propria, vale a da barra', () {
      expect(_grupo().innerDuration, _s(5));
    });

    // Uma animacao de 10 s pode aparecer numa barra de 5 s. As duas
    // duracoes sao coisas diferentes, e confundi-las e o erro classico.
    test('a duracao interna e independente da barra', () {
      final g = _grupo(sourceDuration: _s(10));
      expect(g.innerDuration, _s(10));
      expect(g.duration, _s(5));
    });
  });

  group('Tempo do conteudo', () {
    test('sem remapeamento, e a identidade', () {
      final g = _grupo();
      for (final t in [0, 1, 3, 5]) {
        expect(g.contentTimeAt(_s(t)), _s(t));
      }
    });

    // CONGELAR: uma trilha de tempo constante mostra sempre o mesmo
    // instante do conteudo, aconteca o que acontecer la fora.
    test('valor constante congela o conteudo', () {
      final g = _grupo(timeRemap: AnimatedDouble(1.5));
      expect(g.contentTimeAt(_s(0)), _s(1.5));
      expect(g.contentTimeAt(_s(4)), _s(1.5));
    });

    test('rampa linear reproduz na velocidade da rampa', () {
      // Em 4 s de linha do tempo, percorre 8 s de conteudo: 2x.
      final g = _grupo(
        timeRemap: AnimatedDouble(0)
            .withKeyframe(Duration.zero, 0)
            .withKeyframe(_s(4), 8),
      );
      expect(g.contentTimeAt(_s(0)), _s(0));
      expect(g.contentTimeAt(_s(2)).inMilliseconds, closeTo(4000, 20));
      expect(g.contentTimeAt(_s(4)), _s(8));
    });

    test('rampa descendente reproduz de tras para frente', () {
      final g = _grupo(
        timeRemap: AnimatedDouble(0)
            .withKeyframe(Duration.zero, 5)
            .withKeyframe(_s(5), 0),
      );
      final a = g.contentTimeAt(_s(1));
      final b = g.contentTimeAt(_s(4));
      expect(b, lessThan(a));
    });

    // Nao existe conteudo antes do comeco: o valor negativo prende em
    // zero em vez de virar uma duracao negativa.
    test('tempo negativo prende em zero', () {
      final g = _grupo(timeRemap: AnimatedDouble(-3));
      expect(g.contentTimeAt(_s(1)), Duration.zero);
    });
  });

  group('Copiar', () {
    test('copiar preserva os ajustes da precomp', () {
      final g = _grupo(
        sourceDuration: _s(9),
        timeRemap: AnimatedDouble(2),
        collapse: true,
        clipToComp: false,
      );
      final c = g.copyLayer(name: 'Outro');
      expect(c.name, 'Outro');
      expect(c.sourceDuration, _s(9));
      expect(c.collapse, isTrue);
      expect(c.clipToComp, isFalse);
      expect(c.contentTimeAt(_s(1)), _s(2));
    });

    test('duplicar tambem, e com filhos novos', () {
      final g = _grupo(sourceDuration: _s(9), collapse: true);
      final d = g.duplicated();
      expect(d.sourceDuration, _s(9));
      expect(d.collapse, isTrue);
      expect(d.children.length, 1);
      expect(d.children.first.id, isNot(g.children.first.id));
    });
  });

  group('Quadro proprio', () {
    test('por padrao a precomp recorta', () {
      expect(_grupo().clipToComp, isTrue);
      expect(_grupo().collapse, isFalse);
    });

    test('colapsar e nao recortar sao independentes', () {
      expect(_grupo(collapse: true).clipToComp, isTrue);
      expect(_grupo(clipToComp: false).collapse, isFalse);
    });
  });
}
