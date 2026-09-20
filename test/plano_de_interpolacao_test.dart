import 'dart:io';
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/plano_de_interpolacao.dart';
import 'package:aurea/src/features/export/application/interpolacao_rife.dart';
import 'package:aurea/src/features/export/domain/video_color.dart';
import 'package:flutter_test/flutter_test.dart';

VideoLayer _clipe({
  double speed = 1,
  InterpolacaoDeQuadros modo = InterpolacaoDeQuadros.movimento,
}) => VideoLayer(
  name: 'v',
  sourcePath: '/v.mp4',
  startTime: Duration.zero,
  duration: const Duration(seconds: 2),
  speed: speed,
  interpolacao: modo,
  position: AnimatedOffset(const Offset(960, 540)),
);

void main() {
  group('plano', () {
    test('30 -> 60: um quadro do meio entre cada par, exatamente t = 1/2', () {
      final p = planoDeInterpolacao(
        quadrosBase: 3,
        taxaBase: 30,
        taxaSaida: 60,
      );
      expect(p, const [
        PassoDeInterpolacao(0, 0, 0, 0),
        PassoDeInterpolacao(1, 0, 1, .5),
        PassoDeInterpolacao(2, 1, 1, 0),
        PassoDeInterpolacao(3, 1, 2, .5),
        PassoDeInterpolacao(4, 2, 2, 0),
        PassoDeInterpolacao(5, 2, 2, 0),
      ]);
    });

    test('30 -> 120: quartos exatos e sem deriva no fim de um clipe longo', () {
      final p = planoDeInterpolacao(
        quadrosBase: 900,
        taxaBase: 30,
        taxaSaida: 120,
      );
      expect(p.length, 3600);
      for (final passo in p) {
        expect(passo.saida % 4 == 0 ? passo.copia : true, isTrue);
      }
      expect(p[3597], const PassoDeInterpolacao(3597, 899, 899, 0));
      expect(p[3593], const PassoDeInterpolacao(3593, 898, 899, .25));
      expect(p[3594].t, .5);
      expect(p[3595].t, .75);
      // Cada indice de saida aparece uma vez, em ordem.
      expect([for (final x in p) x.saida], List.generate(3600, (i) => i));
    });

    test('24 -> 60: instantes fracionarios corretos (i*24/60)', () {
      final p = planoDeInterpolacao(
        quadrosBase: 24,
        taxaBase: 24,
        taxaSaida: 60,
      );
      expect(p.length, 60);
      for (final passo in p.take(55)) {
        final pos = passo.saida * 24 / 60;
        expect(passo.a, pos.floor());
        expect(passo.t, closeTo(pos - pos.floor(), 1e-12));
        expect(passo.b, passo.copia ? passo.a : passo.a + 1);
      }
    });

    test('perto das pontas o quadro real fica (a rede fica presa ali)', () {
      // 25 -> 120: t = i*25/120 passa por 1/24 e 23/24.
      final p = planoDeInterpolacao(
        quadrosBase: 25,
        taxaBase: 25,
        taxaSaida: 120,
      );
      final perto = p.firstWhere(
        (x) => x.saida == 5,
      ); // 125/120 -> a=1, t=5/120
      expect(perto, const PassoDeInterpolacao(5, 1, 1, 0));
      final quaseB = p.firstWhere(
        (x) => x.saida == 23,
      ); // 575/120 -> a=4, t=95/120
      expect(quaseB.copia, isFalse, reason: '0,79 ainda e meio do caminho');
      final noFim = p.firstWhere(
        (x) => x.saida == 19,
      ); // 475/120 -> a=3, t=115/120
      expect(
        noFim,
        const PassoDeInterpolacao(19, 4, 4, 0),
        reason: 't=0,96 vira o quadro B',
      );
      for (final x in p.where((x) => !x.copia)) {
        expect(x.t, inExclusiveRange(pontaDoRife, 1 - pontaDoRife));
      }
    });

    test(
      'pares vizinhos em ordem crescente (o motor le cada arquivo uma vez)',
      () {
        final p = planoDeInterpolacao(
          quadrosBase: 50,
          taxaBase: 25,
          taxaSaida: 100,
        );
        var ultimoA = -1;
        for (final passo in p) {
          expect(passo.a, greaterThanOrEqualTo(ultimoA));
          ultimoA = passo.a;
          if (!passo.copia) expect(passo.b, passo.a + 1);
        }
      },
    );

    test(
      'bordas: sem quadros ou taxa invalida devolve vazio; um quadro so copia',
      () {
        expect(
          planoDeInterpolacao(quadrosBase: 0, taxaBase: 30, taxaSaida: 60),
          isEmpty,
        );
        expect(
          planoDeInterpolacao(quadrosBase: 5, taxaBase: 0, taxaSaida: 60),
          isEmpty,
        );
        expect(
          planoDeInterpolacao(quadrosBase: 1, taxaBase: 30, taxaSaida: 120),
          everyElement(
            predicate<PassoDeInterpolacao>((p) => p.copia && p.a == 0),
          ),
        );
      },
    );
  });

  group('estrategia', () {
    test('clipe sem camera lenta: extracao normal', () {
      final e = estrategiaDeInterpolacao(
        _clipe(),
        fps: 30,
        fpsDaFonte: 30,
        rifeDisponivel: true,
      );
      expect(e.como, ComoInterpolar.nada);
      expect(e.taxaBase, 30);
    });

    test('fonte de 60 fps a 50% numa composicao de 30: quadros reais, sem inventar', () {
      final e = estrategiaDeInterpolacao(
        _clipe(speed: .5),
        fps: 30,
        fpsDaFonte: 59.94,
        rifeDisponivel: true,
      );
      expect(e.como, ComoInterpolar.quadrosReais);
      expect(e.taxaBase, 60);
    });

    test('fonte de 30 a 25% com o motor: RIFE lendo a fonte a 30', () {
      final e = estrategiaDeInterpolacao(
        _clipe(speed: .25),
        fps: 30,
        fpsDaFonte: 29.97,
        rifeDisponivel: true,
      );
      expect(e.como, ComoInterpolar.rife);
      expect(e.taxaBase, 30);
    });

    test('24 fps a 50% em composicao de 30: RIFE de 24 para 60', () {
      final e = estrategiaDeInterpolacao(
        _clipe(speed: .5),
        fps: 30,
        fpsDaFonte: 23.976,
        rifeDisponivel: true,
      );
      expect(e.como, ComoInterpolar.rife);
      expect(e.taxaBase, 24);
    });

    test('sem motor, ou no modo mesclar: FFmpeg como sempre', () {
      expect(
        estrategiaDeInterpolacao(
          _clipe(speed: .5),
          fps: 30,
          fpsDaFonte: 30,
          rifeDisponivel: false,
        ).como,
        ComoInterpolar.ffmpeg,
      );
      expect(
        estrategiaDeInterpolacao(
          _clipe(speed: .5, modo: InterpolacaoDeQuadros.mesclar),
          fps: 30,
          fpsDaFonte: 30,
          rifeDisponivel: true,
        ).como,
        ComoInterpolar.ffmpeg,
      );
    });

    test('taxa da fonte desconhecida vale a da composicao', () {
      final e = estrategiaDeInterpolacao(
        _clipe(speed: .5),
        fps: 30,
        fpsDaFonte: null,
        rifeDisponivel: true,
      );
      expect(e.como, ComoInterpolar.rife);
      expect(e.taxaBase, 30);
      final absurda = estrategiaDeInterpolacao(
        _clipe(speed: .5),
        fps: 30,
        fpsDaFonte: 90000,
        rifeDisponivel: true,
      );
      expect(absurda.taxaBase, 30);
    });

    test('efeito Optical Flow desligado anula a interpolacao do clipe', () {
      final c = _clipe(speed: .5).copyLayer(
        effects: [EffectInstance(type: EffectType.opticalFlow, enabled: false)],
      );
      expect(
        estrategiaDeInterpolacao(
          c,
          fps: 30,
          fpsDaFonte: 30,
          rifeDisponivel: true,
        ).como,
        ComoInterpolar.nada,
      );
    });
  });

  group('taxa da fonte pelo ffprobe', () {
    test('razoes, media antes da nominal e valores invalidos', () {
      expect(
        fpsDeProps({'avg_frame_rate': '30000/1001'}),
        closeTo(29.97, 1e-3),
      );
      expect(fpsDeProps({'avg_frame_rate': '0/0', 'r_frame_rate': '60/1'}), 60);
      expect(
        fpsDeProps({'avg_frame_rate': '240/1', 'r_frame_rate': '30/1'}),
        240,
      );
      expect(fpsDeProps({'r_frame_rate': '90000/1'}), isNull);
      expect(fpsDeProps({'avg_frame_rate': 'lixo'}), isNull);
      expect(fpsDeProps({}), isNull);
      expect(fpsDeProps({'avg_frame_rate': 25}), 25);
    });
  });

  group('guardas do motor', () {
    test('memoria de GPU pedida segue a medida do host com folga de 2x', () {
      expect(memoriaMinimaDaGpuMb(1280, 720), inInclusiveRange(500, 560));
      expect(memoriaMinimaDaGpuMb(1920, 1080), inInclusiveRange(950, 1020));
      expect(memoriaMinimaDaGpuMb(3840, 2160), greaterThan(3000));
    });

    test('RIFE recusa GPU sem budget antes da inferencia nativa', () {
      final fonte = File(
        'lib/src/features/export/application/interpolacao_rife.dart',
      ).readAsStringSync();
      expect(fonte, contains('if (info.heapBudgetMb <= 0)'));
      expect(fonte, contains('A GPU não informou memória segura'));
    });

    test('tamanho pelo cabecalho do PNG', () {
      final cabeca = Uint8List.fromList([
        137,
        80,
        78,
        71,
        13,
        10,
        26,
        10,
        0,
        0,
        0,
        13,
        73,
        72,
        68,
        82,
        0,
        0,
        7,
        128,
        0,
        0,
        4,
        56,
      ]);
      expect(tamanhoDoPng(cabeca), (1920, 1080));
      expect(tamanhoDoPng(Uint8List(24)), isNull);
      expect(tamanhoDoPng(Uint8List(8)), isNull);
    });
  });
}
