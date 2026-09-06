import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/bloom.dart';
import 'package:aurea/src/features/editor/presentation/widgets/blend_mask.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

/// O QUE FECHAVA O APP NO DEEP GLOW E NO PRESET NEON.
///
/// Um desfoque de sigma S nao custa S pixels: o motor pinta num alvo de
/// (L + 6S) x (A + 6S), na razao de pixels da tela. A piramide do Deep
/// Glow em qualidade Alta dobra o sigma cinco vezes; o preset Neon do
/// Glow dobra quatro. Com raio 60 num 1080x1920 a 3x, o ultimo nivel
/// pedia uma textura de 284 megapixels — 1,1 GB. Isso nao fica lento:
/// fecha o app, que foi o que os testadores relataram.
///
/// Estes testes fixam os dois freios: o teto do sigma (o efeito nao pede
/// o impossivel) e o teto da foto (nenhum efeito, nem um futuro,
/// consegue pedir).
void main() {
  group('teto do sigma', () {
    test('o teto acompanha a composicao, nunca some', () {
      expect(sigmaTeto(1080, 1920), closeTo(216, 0.01));
      expect(sigmaTeto(1920, 1080), closeTo(216, 0.01));
      expect(sigmaTeto(3840, 2160), closeTo(432, 0.01));
      // Composicao minuscula nao pode zerar o desfoque.
      expect(sigmaTeto(4, 4), greaterThan(0));
    });

    test('a piramide corta no teto e conserva a luz', () {
      // Preset Neon: raio 60, piramide 4 -> 60, 120, 240, 480.
      final sigmas = [60.0, 120.0, 240.0, 480.0];
      final pesos = [1 / 1.875, 0.5 / 1.875, 0.25 / 1.875, 0.125 / 1.875];
      final teto = sigmaTeto(1080, 1920); // 216
      final p = piramideAteOTeto(sigmas, pesos, teto);

      expect(p, hasLength(3), reason: '240 e 480 viram um nivel de 216');
      expect(p.map((n) => n.sigma), [60.0, 120.0, 216.0]);
      expect(
        p.fold<double>(0, (a, n) => a + n.peso),
        closeTo(1.0, 1e-9),
        reason: 'cortar a piramide nao pode clarear nem escurecer o glow',
      );
      for (final n in p) {
        expect(n.sigma, lessThanOrEqualTo(teto));
      }
    });

    test('piramide inteira abaixo do teto passa intacta', () {
      final p = piramideAteOTeto([8.0, 16.0, 32.0], [.5, .3, .2], 216);
      expect(p.map((n) => n.sigma), [8.0, 16.0, 32.0]);
      expect(p.map((n) => n.peso), [.5, .3, .2]);
    });

    test('todos acima do teto viram um nivel so', () {
      final p = piramideAteOTeto([400.0, 800.0, 1600.0], [.5, .3, .2], 216);
      expect(p, hasLength(1));
      expect(p.single.sigma, 216);
      expect(p.single.peso, closeTo(1.0, 1e-9));
    });
  });

  group('teto da foto da mescla', () {
    double megapixels(Size tamanho, double margem, double pr) {
      final c = fotoQueCabe(tamanho, margem, pr);
      final l = (tamanho.width + 2 * c.margem) * c.razao;
      final a = (tamanho.height + 2 * c.margem) * c.razao;
      return l * a / 1e6;
    }

    test('o pedido que fechava o app cabe no teto', () {
      // O caso real: camada 1080x1920, margem de 2068 px (sigma 688),
      // razao de pixels 3 — 284 megapixels antes do freio.
      expect(
        megapixels(const Size(1080, 1920), 2068, 3),
        lessThanOrEqualTo(kFotoTetoMegapixels + 0.01),
      );
      // E o pior caso que a margem antiga alcancava.
      expect(
        megapixels(const Size(1080, 1920), 90816, 3),
        lessThanOrEqualTo(kFotoTetoMegapixels + 0.01),
      );
    });

    test('foto pequena mantem a resolucao da tela', () {
      final c = fotoQueCabe(const Size(200, 200), 12, 3);
      expect(c.razao, 3, reason: 'sem motivo para perder resolucao');
      expect(c.margem, 12, reason: 'a margem pedida cabe inteira');
    });

    test('a foto perde resolucao, nunca a margem que ja passou do teto', () {
      final c = fotoQueCabe(const Size(1080, 1920), 5000, 3);
      expect(c.margem, kFotoTetoDaMargem);
      expect(c.razao, lessThan(3));
      expect(c.razao, greaterThan(0));
    });

    test('margem invalida nao vira NaN na textura', () {
      for (final m in [double.nan, double.infinity, -10.0]) {
        final c = fotoQueCabe(const Size(300, 300), m, 2);
        expect(c.margem.isFinite, isTrue);
        expect(c.margem, greaterThanOrEqualTo(0));
        expect(c.razao.isFinite, isTrue);
        expect(c.razao, greaterThan(0));
      }
    });

    test('a razao nunca aumenta a foto', () {
      for (final pr in [1.0, 2.0, 3.0]) {
        for (final m in [0.0, 50.0, 500.0]) {
          final c = fotoQueCabe(const Size(800, 600), m, pr);
          expect(c.razao, lessThanOrEqualTo(pr));
          expect(math.max(c.razao, 0), greaterThan(0));
        }
      }
    });
  });
}
