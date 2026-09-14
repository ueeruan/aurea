// TEXTO 3D (estilo Element 3D): leitor TrueType, malha com furo e
// chanfro, cena, salvar/abrir e o material novo na assinatura da GPU.
//
// A GPU nunca roda em teste. O que se prova aqui e a GEOMETRIA — que e
// o que decide se a face existe na GPU (o motor descarta costas pela
// ordem dos vertices) — e os dados que chegam a ela.
import 'dart:io';
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:flutter_test/flutter_test.dart';

const _arquivoDaFonte = 'assets/templates/dnyx/AureaMotionSans.ttf';

FonteTrueType _fonteEmpacotada() =>
    FonteTrueType.ler(File(_arquivoDaFonte).readAsBytesSync());

({double minX, double maxX, double minY, double maxY}) _caixa(
  GlifoDaFonte g, {
  double tolerancia = 4,
}) {
  var minX = double.infinity, maxX = -double.infinity;
  var minY = double.infinity, maxY = -double.infinity;
  for (final c in g.contornos) {
    final p = c.planificar(tolerancia);
    for (var i = 0; i < p.length; i += 2) {
      if (p[i] < minX) minX = p[i];
      if (p[i] > maxX) maxX = p[i];
      if (p[i + 1] < minY) minY = p[i + 1];
      if (p[i + 1] > maxY) maxY = p[i + 1];
    }
  }
  return (minX: minX, maxX: maxX, minY: minY, maxY: maxY);
}

void main() {
  group('leitor TrueType', () {
    late FonteTrueType fonte;
    setUpAll(() => fonte = _fonteEmpacotada());

    test('le as medidas da fonte empacotada', () {
      expect(fonte.unidadesPorEm, 2048);
      expect(fonte.ascendente, greaterThan(0));
      expect(fonte.descendente, lessThan(0));
      expect(fonte.quantidadeDeGlifos, greaterThan(200));
    });

    test('o cmap acha o A, e o A tem contorno e furo', () {
      expect(fonte.glifoDe(0x41), isNot(0));
      final a = fonte.glifoDoCaractere(0x41)!;
      // O contorno de fora e o triangulo vazado do meio.
      expect(a.contornos, hasLength(2));
      expect(a.avanco, greaterThan(0));
      for (final c in a.contornos) {
        expect(c.planificar(4).length, greaterThanOrEqualTo(6));
      }
    });

    test('caractere que a fonte nao tem devolve nulo, sem excecao', () {
      expect(fonte.glifoDe(0x1F600), 0);
      expect(fonte.glifoDoCaractere(0x1F600), isNull);
    });

    test('o espaco anda e nao desenha', () {
      final espaco = fonte.glifoDoCaractere(0x20)!;
      expect(espaco.contornos, isEmpty);
      expect(espaco.avanco, greaterThan(0));
    });

    test('glifo COMPOSTO: o ç traz a cedilha abaixo da linha de base', () {
      final c = fonte.glifoDoCaractere(0x63)!; // c
      final cedilha = fonte.glifoDoCaractere(0xE7)!; // ç
      expect(cedilha.contornos.length, greaterThan(c.contornos.length));
      expect(_caixa(cedilha).minY, lessThan(_caixa(c).minY - 100));
    });

    test('glifo COMPOSTO: o acento do Á fica acima do A', () {
      final a = _caixa(fonte.glifoDoCaractere(0x41)!);
      final agudo = _caixa(fonte.glifoDoCaractere(0xC1)!);
      expect(agudo.maxY, greaterThan(a.maxY + 100));
      expect(agudo.minY, closeTo(a.minY, 1));
    });

    test('Y PARA CIMA: a barra do T esta no maior Y', () {
      final t = fonte.glifoDoCaractere(0x54)!;
      final p = t.contornos.single.planificar(4);
      final caixa = _caixa(t);
      double larguraEm(double y) {
        var lo = double.infinity, hi = -double.infinity;
        for (var i = 0; i < p.length; i += 2) {
          if ((p[i + 1] - y).abs() < 1) {
            if (p[i] < lo) lo = p[i];
            if (p[i] > hi) hi = p[i];
          }
        }
        return hi - lo;
      }

      expect(caixa.minY, closeTo(0, 1));
      expect(larguraEm(caixa.maxY), greaterThan(larguraEm(caixa.minY) * 2));
    });

    test('kerning do GPOS: o par AV aproxima as letras', () {
      expect(fonte.kerningEntre(0x41, 0x56), lessThan(0));
      expect(fonte.kerningEntre(0x4C, 0x54), lessThan(0)); // LT
      expect(fonte.kerningEntre(0x41, 0x1F600), 0);
    });

    test('planificar: tolerancia menor, mais pontos na curva', () {
      final o = fonte.glifoDoCaractere(0x4F)!.contornos.first;
      final grosso = o.planificar(40).length;
      final fino = o.planificar(1).length;
      expect(fino, greaterThan(grosso));
      // Nunca repete o primeiro ponto no fim.
      final p = o.planificar(4);
      expect(
        (p[0] - p[p.length - 2]).abs() + (p[1] - p[p.length - 1]).abs(),
        greaterThan(1e-6),
      );
    });

    test('fonte CFF e arquivo quebrado sao recusados com o motivo', () {
      final cff = Uint8List.fromList([
        0x4F, 0x54, 0x54, 0x4F, 0, 0, 0, 0, 0, 0, 0, 0, //
      ]);
      expect(FonteTrueType.motivoDeRecusa(cff), RecusaDaFonte.contornoCff);
      final inteira = File(_arquivoDaFonte).readAsBytesSync();
      expect(FonteTrueType.motivoDeRecusa(inteira), isNull);
      expect(
        FonteTrueType.motivoDeRecusa(Uint8List.sublistView(inteira, 0, 100)),
        RecusaDaFonte.corrompida,
      );
      expect(
        () => FonteTrueType.ler(Uint8List.fromList([1, 2, 3])),
        throwsA(isA<FonteNaoSuportada>()),
      );
    });
  });
}
