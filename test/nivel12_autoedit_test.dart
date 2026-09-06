import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/autoedit/domain/autoedit_plan.dart';
import 'package:aurea/src/features/autoedit/domain/autoedit_style.dart';
import 'package:aurea/src/features/editor/domain/caption.dart';

Cue _fala(int deMs, int ateMs, [String texto = 'oi']) => Cue(
      start: Duration(milliseconds: deMs),
      end: Duration(milliseconds: ateMs),
      text: texto,
    );

(Duration, Duration) _silencio(int deMs, int ateMs) =>
    (Duration(milliseconds: deMs), Duration(milliseconds: ateMs));

void main() {
  group('Cortes de silencio', () {
    test('ritmo 0 nao corta nada', () {
      final cortes = cortesDeSilencio(
        [_silencio(1000, 4000)],
        [_fala(0, 1000)],
        ritmo: 0,
      );
      expect(cortes, isEmpty);
    });

    test('ritmo 1 corta quase tudo, mas deixa a folga', () {
      final cortes = cortesDeSilencio(
        [_silencio(1000, 4000)],
        const [],
        ritmo: 1,
      );
      expect(cortes, hasLength(1));
      final (a, b) = cortes.single;
      // 3 s de silencio menos 2 x 120 ms de folga.
      expect((b - a).inMilliseconds, 3000 - 240);
    });

    test('ritmo pela metade deixa metade do silencio util', () {
      final cortes = cortesDeSilencio(
        [_silencio(0, 2240)],
        const [],
        ritmo: 0.5,
      );
      final (a, b) = cortes.single;
      // Sobra util = 2240 - 240 = 2000; metade corta 1000.
      expect((b - a).inMilliseconds, 1000);
    });

    test('o corte sai do meio, nao de uma ponta', () {
      final cortes = cortesDeSilencio(
        [_silencio(1000, 5000)],
        const [],
        ritmo: 1,
      );
      final (a, b) = cortes.single;
      final meioDoCorte = (a + (b - a) * 0.5).inMilliseconds;
      expect(meioDoCorte, closeTo(3000, 2));
    });

    test('NUNCA corta no meio de uma palavra', () {
      // O detector achou silencio de 1 s a 4 s, mas ha uma palavra
      // atravessada de 2,5 s a 3,5 s.
      final cortes = cortesDeSilencio(
        [_silencio(1000, 4000)],
        [_fala(2500, 3500, 'obrigado')],
        ritmo: 1,
      );
      for (final (a, b) in cortes) {
        expect(b <= const Duration(milliseconds: 2500) ||
            a >= const Duration(milliseconds: 3500), isTrue,
            reason: 'o corte $a..$b invade a palavra');
      }
    });

    test('silencio menor que as duas folgas nao vira corte', () {
      expect(
        cortesDeSilencio([_silencio(0, 200)], const [], ritmo: 1),
        isEmpty,
      );
    });

    test('o tempo removido e a soma dos cortes', () {
      final cortes = [_silencio(0, 500), _silencio(1000, 1750)];
      expect(tempoRemovido(cortes).inMilliseconds, 1250);
    });
  });

  group('Zoom nas trocas de frase', () {
    test('duas palavras coladas sao uma frase so', () {
      final trocas = trocasDeFrase([
        _fala(0, 400, 'bom'),
        _fala(450, 900, 'dia'),
      ]);
      expect(trocas, hasLength(1));
      expect(trocas.single, Duration.zero);
    });

    test('pausa longa comeca outra frase', () {
      final trocas = trocasDeFrase([
        _fala(0, 400),
        _fala(450, 900),
        _fala(2000, 2400),
      ]);
      expect(trocas, hasLength(2));
      expect(trocas.last.inMilliseconds, 2000);
    });

    test('zoom nenhum nao gera keyframe nenhum', () {
      final kfs = keyframesDeZoom(
        [Duration.zero, const Duration(seconds: 2)],
        AutoEditZoom.nenhum,
      );
      expect(kfs, isEmpty);
    });

    test('cada zoom COMECA exatamente no inicio de uma fala', () {
      final trocas = [const Duration(seconds: 1), const Duration(seconds: 5)];
      final kfs = keyframesDeZoom(trocas, AutoEditZoom.forte);
      expect(kfs.first.time, trocas.first);
      expect(kfs[2].time, trocas[1]);
    });

    test('o zoom alterna: uma frase fecha, a proxima volta', () {
      final kfs = keyframesDeZoom(
        [Duration.zero, const Duration(seconds: 4)],
        AutoEditZoom.forte,
      );
      expect(kfs[0].value, 1.0);
      expect(kfs[1].value, AutoEditZoom.forte.escala);
      expect(kfs[2].value, AutoEditZoom.forte.escala);
      expect(kfs[3].value, 1.0);
    });

    test('nao passa do fim do video', () {
      final kfs = keyframesDeZoom(
        [const Duration(seconds: 1), const Duration(seconds: 30)],
        AutoEditZoom.sutil,
        ate: const Duration(seconds: 10),
      );
      expect(kfs, hasLength(2));
    });
  });

  group('Neutralidade', () {
    test('So legendas deixa o video intacto', () {
      final plano = planejar(
        estilo: AutoEditStyles.soLegendas,
        silencios: [_silencio(1000, 5000)],
        falas: [_fala(0, 900, 'ola')],
      );
      expect(plano.cortes, isEmpty);
      expect(plano.zoom, isEmpty);
      expect(plano.falas, hasLength(1));
      expect(plano.economia, Duration.zero);
    });

    test('sem fala detectada nao inventa legenda nem zoom', () {
      final plano = planejar(
        estilo: AutoEditStyles.viral,
        silencios: [_silencio(0, 60000)],
        falas: const [],
      );
      expect(plano.falas, isEmpty);
      expect(plano.zoom, isEmpty);
      // Cortar silencio continua valendo: e o que sobra a oferecer.
      expect(plano.cortes, isNotEmpty);
    });

    test('estilo Limpo nao da zoom', () {
      final plano = planejar(
        estilo: AutoEditStyles.limpo,
        silencios: [_silencio(1000, 3000)],
        falas: [_fala(0, 900), _fala(3100, 4000)],
      );
      expect(plano.zoom, isEmpty);
      expect(plano.cortes, isNotEmpty);
    });
  });

  group('Os seis estilos', () {
    test('sao seis, com id unico', () {
      expect(AutoEditStyles.todos, hasLength(6));
      expect(
        AutoEditStyles.todos.map((e) => e.id).toSet(),
        hasLength(6),
      );
    });

    test('Viral e karaoke palavra por palavra e corte seco', () {
      expect(AutoEditStyles.viral.captionMode, CaptionMode.palavra);
      expect(AutoEditStyles.viral.ritmo, 1.0);
      expect(AutoEditStyles.viral.zoom, AutoEditZoom.forte);
    });

    test('Podcast abaixa a musica sob a fala', () {
      expect(AutoEditStyles.podcast.ducking, isTrue);
    });

    test('Entrevista corta pausa longa e nao da zoom', () {
      expect(AutoEditStyles.entrevista.zoom, AutoEditZoom.nenhum);
      expect(AutoEditStyles.entrevista.ritmo, greaterThan(0.5));
    });

    test('trocar o ritmo na tela de ajuste nao muda o resto', () {
      final ajustado = AutoEditStyles.viral.copyWith(ritmo: 0.2);
      expect(ajustado.ritmo, 0.2);
      expect(ajustado.zoom, AutoEditZoom.forte);
      expect(ajustado.captionMode, CaptionMode.palavra);
    });
  });

  group('Replanejar na tela de ajuste', () {
    test('o plano guarda os silencios brutos', () {
      final plano = planejar(
        estilo: AutoEditStyles.limpo,
        silencios: [_silencio(1000, 5000)],
        falas: const [],
      );
      expect(plano.silencios, hasLength(1));
    });

    test('subir o ritmo depois de baixar volta a cortar mais', () {
      final inicial = planejar(
        estilo: AutoEditStyles.viral,
        silencios: [_silencio(0, 5000)],
        falas: const [],
      );
      final baixo = planejar(
        estilo: AutoEditStyles.viral.copyWith(ritmo: 0.1),
        silencios: inicial.silencios,
        falas: inicial.falas,
      );
      final alto = planejar(
        estilo: AutoEditStyles.viral.copyWith(ritmo: 0.9),
        silencios: baixo.silencios,
        falas: baixo.falas,
      );
      expect(baixo.economia, lessThan(inicial.economia));
      // O ponto: replanejar a partir do plano baixo ainda alcanca o alto.
      expect(alto.economia, greaterThan(baixo.economia));
    });

    test('trocar o zoom nao mexe nos cortes', () {
      final base = planejar(
        estilo: AutoEditStyles.viral,
        silencios: [_silencio(0, 5000)],
        falas: [_fala(0, 300), _fala(5200, 5600)],
      );
      final semZoom = planejar(
        estilo: AutoEditStyles.viral.copyWith(zoom: AutoEditZoom.nenhum),
        silencios: base.silencios,
        falas: base.falas,
      );
      expect(semZoom.zoom, isEmpty);
      expect(semZoom.cortes.length, base.cortes.length);
      expect(semZoom.economia, base.economia);
    });
  });
}
