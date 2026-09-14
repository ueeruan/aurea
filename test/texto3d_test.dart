// TEXTO 3D (estilo Element 3D): leitor TrueType, malha com furo e
// chanfro, cena, salvar/abrir e o material novo na assinatura da GPU.
//
// A GPU nunca roda em teste. O que se prova aqui e a GEOMETRIA — que e
// o que decide se a face existe na GPU (o motor descarta costas pela
// ordem dos vertices) — e os dados que chegam a ela.
import 'dart:io';
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:flutter_test/flutter_test.dart';

/// Auditoria da malha: solda por POSICAO (a malha duplica vertice para
/// normal e material) e conta arestas, volume com sinal e normais que
/// discordam da ordem dos vertices.
({int arestasRuins, double volume, int normaisContra}) _auditar(
  MalhaDoTexto3D m, {
  bool rascunho = false,
}) {
  final ids = <String, int>{};
  int solda(Float64List p, int i) => ids.putIfAbsent(
    '${(p[3 * i] * 1e5).round()},${(p[3 * i + 1] * 1e5).round()},'
    '${(p[3 * i + 2] * 1e5).round()}',
    () => ids.length,
  );
  final arestas = <int, int>{};
  var volume = 0.0;
  var contra = 0;
  for (final prim in m.partes.values) {
    final idx = rascunho ? prim.indicesDoRascunho : prim.indices;
    final p = prim.posicoes, n = prim.normais;
    for (var t = 0; t + 2 < idx.length; t += 3) {
      final a = idx[t], b = idx[t + 1], c = idx[t + 2];
      final wa = solda(p, a), wb = solda(p, b), wc = solda(p, c);
      for (final (u, v) in [(wa, wb), (wb, wc), (wc, wa)]) {
        final k = u < v ? u * 1000000 + v : v * 1000000 + u;
        arestas[k] = (arestas[k] ?? 0) + 1;
      }
      final ax = p[3 * a], ay = p[3 * a + 1], az = p[3 * a + 2];
      final bx = p[3 * b], by = p[3 * b + 1], bz = p[3 * b + 2];
      final cx = p[3 * c], cy = p[3 * c + 1], cz = p[3 * c + 2];
      volume +=
          (ax * (by * cz - bz * cy) -
              ay * (bx * cz - bz * cx) +
              az * (bx * cy - by * cx)) /
          6;
      final ux = bx - ax, uy = by - ay, uz = bz - az;
      final vx = cx - ax, vy = cy - ay, vz = cz - az;
      final gx = uy * vz - uz * vy;
      final gy = uz * vx - ux * vz;
      final gz = ux * vy - uy * vx;
      for (final vi in [a, b, c]) {
        if (gx * n[3 * vi] + gy * n[3 * vi + 1] + gz * n[3 * vi + 2] < 0) {
          contra++;
        }
      }
    }
  }
  return (
    arestasRuins: arestas.values.where((c) => c != 2).length,
    volume: volume,
    normaisContra: contra,
  );
}

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

  group('malha do texto 3D', () {
    late FonteTrueType fonte;
    setUpAll(() => fonte = _fonteEmpacotada());

    MalhaDoTexto3D malha(Texto3D t) =>
        malhaDoTexto3D(disporTexto3D(t, fonte), t, fonte.unidadesPorEm);

    test('ELEMENT com chanfro redondo e fechado e aponta para fora', () {
      final m = malha(const Texto3D(texto: 'ELEMENT'));
      final a = _auditar(m);
      expect(a.arestasRuins, 0, reason: 'toda aresta em duas faces');
      expect(a.volume, greaterThan(0), reason: 'normal geometrica para fora');
      expect(a.normaisContra, 0);
      expect(m.partes.keys, containsAll(ParteDoTexto3D.values));
    });

    for (final tipo in TipoDeChanfro.values) {
      test('MOTION com chanfro ${tipo.name} e fechado', () {
        final a = _auditar(malha(Texto3D(texto: 'MOTION', chanfro: tipo)));
        expect(a.arestasRuins, 0);
        expect(a.volume, greaterThan(0));
        expect(a.normaisContra, 0);
      });
    }

    test('o O tem FURO: a tampa nao cobre o miolo', () {
      const t = Texto3D(texto: 'O', chanfro: TipoDeChanfro.nenhum);
      final m = malha(t);
      final frente = m.partes[ParteDoTexto3D.frente]!;
      bool cobre(double x, double y) {
        final p = frente.posicoes, idx = frente.indices;
        for (var i = 0; i + 2 < idx.length; i += 3) {
          final a = idx[i], b = idx[i + 1], c = idx[i + 2];
          if (p[3 * a + 2] <= 0) continue; // so a tampa da frente
          double lado(int u, int v) =>
              (p[3 * v] - p[3 * u]) * (y - p[3 * u + 1]) -
              (p[3 * v + 1] - p[3 * u + 1]) * (x - p[3 * u]);
          final l1 = lado(a, b), l2 = lado(b, c), l3 = lado(c, a);
          if ((l1 >= 0 && l2 >= 0 && l3 >= 0) ||
              (l1 <= 0 && l2 <= 0 && l3 <= 0)) {
            return true;
          }
        }
        return false;
      }

      expect(cobre(m.centroX, m.centroY), isFalse, reason: 'miolo vazado');
      // Na altura do meio, perto da borda esquerda, ha material.
      expect(cobre(m.minX + (m.maxX - m.minX) * 0.06, m.centroY), isTrue);
      expect(_auditar(m).arestasRuins, 0);
    });

    test('T: o topo da letra fica no maior Y', () {
      const t = Texto3D(texto: 'T', chanfro: TipoDeChanfro.nenhum);
      final m = malha(t);
      final p = m.partes[ParteDoTexto3D.frente]!.posicoes;
      double larguraPerto(double y) {
        var lo = double.infinity, hi = -double.infinity;
        for (var i = 0; i < p.length; i += 3) {
          if ((p[i + 1] - y).abs() < 0.5) {
            if (p[i] < lo) lo = p[i];
            if (p[i] > hi) hi = p[i];
          }
        }
        return hi - lo;
      }

      expect(larguraPerto(m.maxY), greaterThan(larguraPerto(m.minY) * 2));
    });

    test('o chanfro aumenta os triangulos, dentro do orcamento', () {
      final nenhum = malha(
        const Texto3D(texto: 'ELEMENT', chanfro: TipoDeChanfro.nenhum),
      );
      final angular = malha(
        const Texto3D(texto: 'ELEMENT', chanfro: TipoDeChanfro.angular),
      );
      final redondo = malha(const Texto3D(texto: 'ELEMENT'));
      expect(angular.triangulos, greaterThan(nenhum.triangulos));
      expect(redondo.triangulos, greaterThan(angular.triangulos));
      expect(redondo.triangulos, lessThanOrEqualTo(5000));
    });

    test(
      'o rascunho (LOD) nao tem chanfro, e fechado e fica abaixo de 3000',
      () {
        final m = malha(const Texto3D(texto: 'ELEMENT'));
        expect(m.triangulosDoRascunho, lessThan(3000));
        expect(m.triangulosDoRascunho, lessThan(m.triangulos));
        expect(m.partes[ParteDoTexto3D.chanfro]!.triangulosDoRascunho, 0);
        final a = _auditar(m, rascunho: true);
        expect(a.arestasRuins, 0);
        expect(a.volume, greaterThan(0));
      },
    );

    test('igualdade POR VALOR dos parametros', () {
      const a = Texto3D(texto: 'ELEMENT', espessura: 30);
      final b = Texto3D(texto: 'ELE${'MENT'}', espessura: 30.0);
      expect(a, b);
      expect(a.hashCode, b.hashCode);
      expect(a == a.copyWith(chanfro: TipoDeChanfro.angular), isFalse);
      expect(Texto3D.fromJson(a.toJson()), a);
    });

    test('a mesma letra com os mesmos parametros reaproveita a malha', () {
      const t = Texto3D(texto: 'ELEMENT');
      final letras = disporTexto3D(t, fonte).letras;
      final e1 = malhaDaLetra(letras[0].glifo, t, fonte.unidadesPorEm);
      final e2 = malhaDaLetra(letras[2].glifo, t, fonte.unidadesPorEm);
      expect(identical(e1, e2), isTrue);
    });

    test('glifos guardados refazem a MESMA malha sem a fonte', () {
      const t = Texto3D(texto: 'MOTION');
      final original = malha(t);
      final guardados = GlifosGuardados.fromJson(
        GlifosGuardados.capturar(fonte, t.texto).toJson(),
      )!;
      expect(guardados.cobre(t.texto), isTrue);
      final refeita = malhaDoTexto3D(
        disporTexto3D(t, guardados),
        t,
        guardados.unidadesPorEm,
      );
      expect(refeita.triangulos, original.triangulos);
      expect(refeita.minX, closeTo(original.minX, 1e-3));
      expect(refeita.maxY, closeTo(original.maxY, 1e-3));
    });
  });
}
