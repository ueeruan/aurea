// O TIME SLICE — a matematica das faixas, e a ficha que faltava.
//
// O MOTOR EXISTE DESDE 14/09 e esta ligado no palco; o que faltava era a
// FICHA, sem a qual ninguem conseguia pedir o efeito nem mexer num
// parametro. Estas duas coisas andam juntas aqui, e o teste que mais
// importa e o ULTIMO: as chaves que o motor le tem de existir na ficha.
//
// Os valores de fabrica sao os do S_TimeSlice lidos do AE do dono:
// Slice Direction -90, Slice Number 12, Frame Offset 0, Interp Frames 0.
import 'dart:ui';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/time_slice.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('os atrasos das faixas', () {
    test('a escada do plugin: um quadro por faixa, centrada', () {
      // Com maximo = (N-1)/2 o S_TimeSlice puro: -2, -1, 0, 1, 2.
      final a = atrasosDasFaixas(
        faixas: 5,
        distribuicao: DistribuicaoDoTimeSlice.escada,
        maximo: 2,
      );
      expect(a, [-2, -1, 0, 1, 2]);
    });

    test('a escada abre o passo quando o maximo e maior', () {
      final a = atrasosDasFaixas(
        faixas: 3,
        distribuicao: DistribuicaoDoTimeSlice.escada,
        maximo: 4,
      );
      expect(a, [-4, 0, 4]);
    });

    test('uma faixa so nao se desloca', () {
      for (var d = 0; d <= 4; d++) {
        expect(
          atrasosDasFaixas(faixas: 1, distribuicao: d, maximo: 20),
          [0],
          reason: 'distribuicao $d com uma faixa',
        );
      }
    });

    test('linear vai de zero ao maximo, e a curva muda o caminho', () {
      final reta = atrasosDasFaixas(
        faixas: 5,
        distribuicao: DistribuicaoDoTimeSlice.linear,
        maximo: 8,
      );
      expect(reta, [0, 2, 4, 6, 8]);
      final quadrada = atrasosDasFaixas(
        faixas: 5,
        distribuicao: DistribuicaoDoTimeSlice.linear,
        maximo: 8,
        curva: 1,
      );
      // t^2 em 0, 0,25, 0,5, 0,75, 1 -> 0, 0,0625, 0,25, 0,5625, 1, e
      // vezes 8 da 0, 0,5, 2, 4,5, 8 — arredondado: 0, 1, 2, 5, 8.
      expect(quadrada, [0, 1, 2, 5, 8]);
      // e a curva nao pode passar do maximo em faixa nenhuma
      for (final d in [0, 1, 2, 3]) {
        for (final a in [
          atrasosDasFaixas(
            faixas: 9,
            distribuicao: d,
            maximo: 10,
            curva: d,
          ),
        ]) {
          for (final v in a) {
            expect(v.abs(), lessThanOrEqualTo(10));
          }
        }
      }
    });

    test('centro comeca no meio e abre para as bordas', () {
      final a = atrasosDasFaixas(
        faixas: 5,
        distribuicao: DistribuicaoDoTimeSlice.centro,
        maximo: 8,
      );
      expect(a[2], 0, reason: 'o meio nao se desloca');
      expect(a[0], 8);
      expect(a[4], 8);
      expect(a[1], a[3]);
      expect(a[1], lessThan(8));
    });

    test('aleatoria e sempre a MESMA com a mesma semente', () {
      List<int> com(int semente) => atrasosDasFaixas(
        faixas: 12,
        distribuicao: DistribuicaoDoTimeSlice.aleatoria,
        maximo: 10,
        semente: semente,
      );
      expect(com(7), com(7));
      expect(com(7), isNot(com(8)));
      for (final v in com(7)) {
        expect(v.abs(), lessThanOrEqualTo(10));
      }
      // e nao pode ser constante: sorteio que da tudo igual nao e sorteio
      expect(com(7).toSet().length, greaterThan(3));
    });

    test('a onda anda com o tempo pela varredura', () {
      List<int> em(double s) => atrasosDasFaixas(
        faixas: 8,
        distribuicao: DistribuicaoDoTimeSlice.onda,
        maximo: 10,
        ciclos: 1,
        varredura: 1,
        segundos: s,
      );
      expect(em(0), isNot(em(0.4)), reason: 'a onda tem de andar');
      // sem varredura ela fica parada no tempo
      List<int> parada(double s) => atrasosDasFaixas(
        faixas: 8,
        distribuicao: DistribuicaoDoTimeSlice.onda,
        maximo: 10,
        ciclos: 1,
        segundos: s,
      );
      expect(parada(0), parada(1.5));
    });

    test('o deslocamento entra em TODAS as faixas', () {
      final a = atrasosDasFaixas(
        faixas: 5,
        distribuicao: DistribuicaoDoTimeSlice.escada,
        maximo: 2,
        deslocamento: 10,
      );
      expect(a, [8, 9, 10, 11, 12]);
    });
  });

  group('a faixa no espaco da composicao', () {
    const tela = Size(200, 100);

    test('doze faixas verticais cobrem a largura inteira', () {
      final areas = [
        for (var k = 0; k < 12; k++)
          faixaDoTimeSlice(tela, anguloGraus: 0, k: k, n: 12).getBounds(),
      ];
      expect(areas.first.left, lessThanOrEqualTo(0));
      expect(areas.last.right, greaterThanOrEqualTo(200));
      for (var k = 1; k < 12; k++) {
        // A faixa seguinte comeca onde a anterior termina: sem buraco e
        // sem sobreposicao de valor.
        expect(areas[k].left, closeTo(areas[k - 1].right, 0.01));
      }
    });

    test('a direcao -90 do plugin deita as faixas', () {
      // -90 graus: o indice cresce de cima para baixo, entao o corte e
      // horizontal e a faixa atravessa a largura toda.
      final primeira = faixaDoTimeSlice(
        tela,
        anguloGraus: -90,
        k: 0,
        n: 4,
      ).getBounds();
      expect(primeira.width, greaterThan(200));
      expect(primeira.height, lessThan(100));
    });

    test('o vao abre um espaco entre as faixas', () {
      final comVao = faixaDoTimeSlice(
        tela,
        anguloGraus: 0,
        k: 0,
        n: 4,
        vao: 10,
      ).getBounds();
      final semVao = faixaDoTimeSlice(
        tela,
        anguloGraus: 0,
        k: 0,
        n: 4,
      ).getBounds();
      expect(comVao.right, lessThan(semVao.right));
    });

    test('vao maior que a faixa nao deixa lasca', () {
      final p = faixaDoTimeSlice(tela, anguloGraus: 0, k: 1, n: 4, vao: 200);
      expect(p.getBounds().isEmpty, isTrue);
    });

    test('as faixas das pontas passam do quadro', () {
      // Arredondamento nunca pode deixar a borda sem imagem.
      final primeira = faixaDoTimeSlice(
        tela,
        anguloGraus: 0,
        k: 0,
        n: 5,
      ).getBounds();
      final ultima = faixaDoTimeSlice(
        tela,
        anguloGraus: 0,
        k: 4,
        n: 5,
      ).getBounds();
      expect(primeira.left, lessThan(0));
      expect(ultima.right, greaterThan(200));
    });
  });

  group('o instante deslocado', () {
    test('prende nas pontas em vez de sair da camada', () {
      final camada = _camada(const Duration(seconds: 2));
      expect(
        localDeslocado(camada, const Duration(seconds: 1), 5, 30),
        const Duration(seconds: 1) + const Duration(microseconds: 166667),
      );
      // Antes do inicio vale o primeiro quadro.
      expect(
        localDeslocado(camada, Duration.zero, -10, 30),
        Duration.zero,
      );
      // Depois do fim vale o ultimo.
      final fim = localDeslocado(
        camada,
        const Duration(seconds: 2) - const Duration(microseconds: 1000),
        10,
        30,
      );
      expect(fim, lessThan(const Duration(seconds: 2)));
    });
  });

  group('a ficha', () {
    test('esta registrada e diz de que plugin ela veio', () {
      final spec = effectSpecs[EffectType.timeSlice];
      expect(spec, isNotNull);
      expect(spec!.id, 's_timeslice');
      expect(spec.category, 'Time');
    });

    test('os valores de fabrica sao os do S_TimeSlice lidos do AE', () {
      final p = effectSpecs[EffectType.timeSlice]!.params;
      expect(p['angle']!.initial, -90);
      expect(p['slices']!.initial, 12);
      expect(p['frame_offset']!.initial, 0);
    });

    /// O TESTE QUE MAIS IMPORTA: o motor le um punhado de chaves soltas
    /// (`paramAt('slices', ...)`), e nenhuma delas e conferida pelo
    /// compilador. Uma ficha com o nome trocado deixa o parametro mudo —
    /// o efeito abre, o controle aparece, e nao muda nada.
    test('TODAS as chaves que o motor le existem na ficha', () {
      const lidas = [
        'mix',
        'angle',
        'gap',
        'slices',
        'distribution',
        'max_offset',
        'curve',
        'cycles',
        'phase',
        'sweep',
        'seed',
        'frame_offset',
      ];
      final spec = effectSpecs[EffectType.timeSlice]!;
      for (final chave in lidas) {
        expect(spec.params.containsKey(chave), isTrue,
            reason: 'o motor le "$chave" e a ficha nao tem essa chave');
      }
    });

    test('os presets usam as chaves que existem na ficha', () {
      final spec = effectSpecs[EffectType.timeSlice]!;
      for (final pronto in spec.presets) {
        for (final chave in pronto.valores.keys) {
          expect(spec.params.containsKey(chave), isTrue,
              reason: 'preset "${pronto.nome}" mexe em "$chave"');
        }
      }
    });
  });
}

/// Uma camada minima, so para o `localDeslocado` ter duracao.
Layer _camada(Duration duracao) => ShapeLayer(
  name: 'bancada',
  startTime: Duration.zero,
  duration: duracao,
  contents: [ShapeFill(color: const Color(0xFFFFFFFF))],
);
