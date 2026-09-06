import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/am_sections.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape_library.dart';

const _d = Duration(seconds: 4);

/// Uma de cada tipo — a grade precisa se comportar para todas.
List<Layer> _todasAsCamadas() => [
      VideoLayer(name: 'v', startTime: Duration.zero, duration: _d,
          sourcePath: '/tmp/v.mp4'),
      ImageLayer(name: 'i', startTime: Duration.zero, duration: _d,
          sourcePath: '/tmp/i.png'),
      TextLayer(name: 't', startTime: Duration.zero, duration: _d, text: 'oi'),
      ShapeLayer(name: 's', startTime: Duration.zero, duration: _d,
          contents: ShapeLibrary.roundedSquare()),
      GroupLayer(name: 'g', startTime: Duration.zero, duration: _d),
      CaptionLayer(name: 'l', startTime: Duration.zero, duration: _d),
      AudioLayer(name: 'a', startTime: Duration.zero, duration: _d,
          sourcePath: '/tmp/a.wav'),
      NullLayer(name: 'n', startTime: Duration.zero, duration: _d),
      ParticlesLayer(name: 'p', startTime: Duration.zero, duration: _d),
      Element3DLayer(name: 'e', startTime: Duration.zero, duration: _d),
      Scene3DLayer(name: 'c', startTime: Duration.zero, duration: _d),
      AdjustmentLayer(name: 'j', startTime: Duration.zero, duration: _d),
    ];

void main() {
  group('A grade nao cresce', () {
    test('nenhum tipo de camada ve mais de sete secoes', () {
      // O teto e por TIPO, nao no total de nomes: uma secao que so existe
      // para um tipo de camada e a saida que a propria regra preve.
      for (final camada in _todasAsCamadas()) {
        expect(secoesDe(camada).length, lessThanOrEqualTo(kAmMaximoSecoes),
            reason: '${camada.runtimeType}');
      }
    });

    test('nenhuma camada fica sem secao nenhuma', () {
      for (final camada in _todasAsCamadas()) {
        expect(secoesDe(camada), isNotEmpty,
            reason: '${camada.runtimeType} abriria um menu vazio');
      }
    });

    test('a ordem da grade nao muda quando o tipo muda', () {
      for (final camada in _todasAsCamadas()) {
        final secoes = secoesDe(camada).toList();
        final indices = [for (final s in secoes) AmSecao.values.indexOf(s)];
        final ordenado = [...indices]..sort();
        expect(indices, ordenado,
            reason: '${camada.runtimeType} reordenou a grade');
      }
    });
  });

  group('Nada inerte', () {
    test('Editar forma so aparece na forma', () {
      for (final camada in _todasAsCamadas()) {
        expect(secoesDe(camada).contains(AmSecao.editarForma),
            camada is ShapeLayer);
      }
    });

    test('o Nulo mostra so o que um nulo tem: transform e clonar', () {
      final nulo = NullLayer(name: 'n', startTime: Duration.zero, duration: _d);
      expect(secoesDe(nulo), {AmSecao.moverTransformar, AmSecao.clonar});
    });

    test('Clonar so existe no nulo', () {
      for (final camada in _todasAsCamadas()) {
        expect(secoesDe(camada).contains(AmSecao.clonar), camada is NullLayer,
            reason: '${camada.runtimeType}');
      }
    });

    test('a camada de som nao mostra posicao, cor nem opacidade', () {
      final som = AudioLayer(name: 'a', startTime: Duration.zero,
          duration: _d, sourcePath: '/tmp/a.wav');
      expect(secoesDe(som), {AmSecao.volume, AmSecao.fade, AmSecao.efeitos});
    });

    test('cor e preenchimento nao aparece em video nem em audio', () {
      for (final camada in _todasAsCamadas()) {
        if (camada is VideoLayer || camada is AudioLayer) {
          expect(secoesDe(camada), isNot(contains(AmSecao.corPreenchimento)));
        }
      }
    });

    test('volume e fade so em quem tem som', () {
      for (final camada in _todasAsCamadas()) {
        final temSom = camada is VideoLayer || camada is AudioLayer;
        expect(secoesDe(camada).contains(AmSecao.volume), temSom,
            reason: '${camada.runtimeType}');
      }
    });
  });

  group('Os editores de tipo sairam do menu escondido', () {
    test('cada tipo mostra o seu, e so o seu', () {
      final porSecao = <AmSecao, Type>{
        AmSecao.editarTexto: TextLayer,
        AmSecao.editarLegendas: CaptionLayer,
        AmSecao.particulas: ParticlesLayer,
      };
      for (final entrada in porSecao.entries) {
        for (final camada in _todasAsCamadas()) {
          expect(secoesDe(camada).contains(entrada.key),
              camada.runtimeType == entrada.value,
              reason: '${entrada.key} em ${camada.runtimeType}');
        }
      }
    });

    test('Cena 3D aparece no elemento e na cena', () {
      for (final camada in _todasAsCamadas()) {
        expect(secoesDe(camada).contains(AmSecao.cena3d),
            camada is Element3DLayer || camada is Scene3DLayer,
            reason: '${camada.runtimeType}');
      }
    });
  });
}
