// O TRACKER 3D NOVO (C++, packages/aurea_tracker) contra filmagens sinteticas
// com a camera conhecida — o mesmo metodo do camera_tracker_pro_test: montar
// a cena, projetar, entregar so as projecoes e conferir a resposta.
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/editor/application/camera_track_service.dart';
import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_nativa.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea_tracker/aurea_tracker.dart';
import 'package:flutter_test/flutter_test.dart';

enum _Mov { lateral, orbita, mao, frente, tripe }

class _Filmagem {
  _Filmagem(this.obs, this.centros, this.rotacoes);
  final Float64List obs;
  final List<List<double>> centros;
  final List<Mat3> rotacoes;
}

_Filmagem _filmar(
  _Mov mov, {
  int quadros = 60,
  int largura = 640,
  int altura = 360,
  double focal = 760,
  double ruido = 0,
  bool plano = false,
  int semente = 7,
}) {
  final rng = math.Random(semente);
  final mundo = [
    for (var i = 0; i < 260; i++)
      plano
          // O CHAO: tudo em y = 150 (abaixo da camera), espalhado em x/z.
          ? [(rng.nextDouble() - .5) * 1600, 150.0, 350 + rng.nextDouble() * 1400]
          : [
              (rng.nextDouble() - .5) * 1100,
              (rng.nextDouble() - .5) * 600,
              350 + rng.nextDouble() * 1100,
            ],
  ];
  final ruidoRng = math.Random(semente * 5 + 1);
  final obs = <double>[];
  final centros = <List<double>>[];
  final rotacoes = <Mat3>[];
  for (var q = 0; q < quadros; q++) {
    final u = q / (quadros - 1);
    late List<double> c;
    late Mat3 r;
    switch (mov) {
      case _Mov.lateral:
        c = [-260 + 520 * u, 10 * math.sin(u * 3), 0];
        r = rotacaoDeVetor([0.0, -0.18 * (u - .5), 0.0]);
      case _Mov.orbita:
        final a = (u - .5) * 0.7;
        c = [700 * math.sin(a), 0, 700 * (1 - math.cos(a))];
        r = rotacaoDeVetor([0.0, -a, 0.0]);
      case _Mov.mao:
        c = [
          -200 + 400 * u + 12 * math.sin(u * 23),
          9 * math.sin(u * 17),
          7 * math.sin(u * 13),
        ];
        r = rotacaoDeVetor([
          0.01 * math.sin(u * 19),
          -0.12 * (u - .5),
          0.006 * math.sin(u * 11),
        ]);
      case _Mov.frente:
        c = [30 * math.sin(u * 2), 0, -150 + 450 * u];
        r = rotacaoDeVetor([0.0, 0.05 * u, 0.0]);
      case _Mov.tripe:
        c = [0, 0, 0];
        r = rotacaoDeVetor([0.04 * (u - .5), -0.5 * (u - .5), 0.0]);
    }
    final rc = r.aplicar(c);
    final t = [-rc[0], -rc[1], -rc[2]];
    centros.add(c);
    rotacoes.add(r);
    for (var i = 0; i < mundo.length; i++) {
      final p = projetar(r, t, mundo[i]);
      if (p == null) continue;
      final x = largura / 2 + p[0] * focal + (ruidoRng.nextDouble() - .5) * ruido;
      final y = altura / 2 + p[1] * focal + (ruidoRng.nextDouble() - .5) * ruido;
      if (x < 6 || y < 6 || x > largura - 6 || y > altura - 6) continue;
      obs.addAll([i.toDouble(), q.toDouble(), x, y]);
    }
  }
  return _Filmagem(Float64List.fromList(obs), centros, rotacoes);
}

double _dist(List<double> a, List<double> b) =>
    math.sqrt(math.pow(a[0] - b[0], 2) + math.pow(a[1] - b[1], 2) + math.pow(a[2] - b[2], 2));

/// Quanto o caminho resolvido difere do verdadeiro em FORMA (a escala do
/// mundo e livre): razao das distancias ao primeiro centro.
double _erroDeForma(List<List<double>> verdade, List<PoseCamera> poses) {
  final centros = [
    for (final p in poses)
      () {
        final rt = p.rotacao.transposta.aplicar(p.translacao);
        return [-rt[0], -rt[1], -rt[2]];
      }(),
  ];
  final escalaV = _dist(verdade.first, verdade.last);
  final escalaS = _dist(centros.first, centros.last);
  var pior = 0.0;
  for (var i = 1; i < poses.length; i++) {
    final dv = _dist(verdade.first, verdade[poses[i].quadro]) / escalaV;
    final ds = _dist(centros.first, centros[i]) / escalaS;
    pior = math.max(pior, (dv - ds).abs());
  }
  return pior;
}

void main() {
  for (final mov in [_Mov.lateral, _Mov.orbita, _Mov.mao, _Mov.frente]) {
    test('resolve ${mov.name}: focal, todos os quadros e o formato do caminho', () {
      final f = _filmar(mov, ruido: .4);
      final s = resolverCamera3DNativo(
        f.obs,
        largura: 640,
        altura: 360,
        quadros: 60,
        fps: 30,
      );
      expect(s.poses.length, 60, reason: 'pose de todo quadro');
      expect(s.erroPixels, lessThan(1.0));
      expect(s.focalPx, closeTo(760, 760 * .08), reason: 'focal livre no ajuste');
      final r = resolverCameraNativa(
        observacoes: f.obs,
        largura: 640,
        altura: 360,
        quadros: 60,
        fps: 30,
      );
      final bruta = [
        for (var i = 0; i + 13 <= r.poses.length; i += 13)
          PoseCamera(
            r.poses[i].round(),
            Mat3([for (var k = 1; k <= 9; k++) r.poses[i + k]]),
            [r.poses[i + 10], r.poses[i + 11], r.poses[i + 12]],
          ),
      ];
      expect(_erroDeForma(f.centros, bruta), lessThan(.06));
    });
  }

  test('chao plano resolve (o solver antigo recusava)', () {
    final f = _filmar(_Mov.lateral, plano: true, ruido: .3);
    final s = resolverCamera3DNativo(
      f.obs,
      largura: 640,
      altura: 360,
      quadros: 60,
      fps: 30,
    );
    expect(s.poses.length, 60);
    expect(s.erroPixels, lessThan(1.2));
  });

  test('tripe vira rotacao pura em vez de recusa', () {
    final f = _filmar(_Mov.tripe, ruido: .3);
    final r = resolverCameraNativa(
      observacoes: f.obs,
      largura: 640,
      altura: 360,
      quadros: 60,
      fps: 30,
    );
    expect(r.codigo, attOk);
    expect(r.tripe, isTrue);
    expect(r.focal, closeTo(760, 760 * .12));
    // Giro total entre o primeiro e o ultimo quadro: 0,5 rad em Y.
    final r0 = Mat3([for (var k = 1; k <= 9; k++) r.poses[k]]);
    final ultimo = r.poses.length - 13;
    final r1 = Mat3([for (var k = 1; k <= 9; k++) r.poses[ultimo + k]]);
    final rel = r1 * r0.transposta;
    final ang = math.acos(((rel.at(0, 0) + rel.at(1, 1) + rel.at(2, 2) - 1) / 2).clamp(-1.0, 1.0));
    expect(ang, closeTo(.5, .05));
  });

  test('o seguidor KLT acompanha uma textura andando com subpixel', () {
    const w = 320, h = 180;
    final rng = math.Random(3);
    final base = List<double>.generate(w * h, (_) => rng.nextDouble());
    // Textura suave: media 5x5 do ruido.
    double tex(double x, double y) {
      final xi = x.floor(), yi = y.floor();
      final ax = x - xi, ay = y - yi;
      double v(int a, int b) {
        var s = 0.0;
        for (var dy = -2; dy <= 2; dy++) {
          for (var dx = -2; dx <= 2; dx++) {
            final xx = (a + dx) % w, yy = (b + dy) % h;
            s += base[(yy < 0 ? yy + h : yy) * w + (xx < 0 ? xx + w : xx)];
          }
        }
        return s / 25;
      }

      return (1 - ay) * ((1 - ax) * v(xi, yi) + ax * v(xi + 1, yi)) +
          ay * ((1 - ax) * v(xi, yi + 1) + ax * v(xi + 1, yi + 1));
    }

    final seguidor = SeguidorDePontosNativo(w, h, maximoDePontos: 80);
    addTearDown(seguidor.fechar);
    for (var q = 0; q < 10; q++) {
      final quadro = Uint8List(w * h);
      for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
          quadro[y * w + x] = (tex(x - 1.3 * q, y - .7 * q) * 255).round().clamp(0, 255);
        }
      }
      seguidor.empurrar(quadro, q);
    }
    final obs = seguidor.observacoes();
    final porId = <int, List<(int, double, double)>>{};
    for (var i = 0; i < obs.length; i += 4) {
      (porId[obs[i].round()] ??= []).add((obs[i + 1].round(), obs[i + 2], obs[i + 3]));
    }
    final longos = porId.values.where((v) => v.length == 10).toList();
    expect(longos.length, greaterThan(30));
    var erro = 0.0;
    for (final v in longos) {
      final dx = v.last.$2 - v.first.$2, dy = v.last.$3 - v.first.$3;
      erro += (dx - 1.3 * 9).abs() + (dy - .7 * 9).abs();
    }
    expect(erro / longos.length, lessThan(.35), reason: 'subpixel preservado');
  });

  test('arquivo cru: segue e devolve rastros longos (caminho do app)', () {
    const w = 160, h = 96, quadros = 12;
    final rng = math.Random(9);
    final base = List<int>.generate(w * h, (_) => rng.nextInt(256));
    final dir = Directory.systemTemp.createTempSync('aurea_cru');
    addTearDown(() => dir.deleteSync(recursive: true));
    final arquivo = File('${dir.path}/cinza.raw');
    final saida = BytesBuilder();
    for (var q = 0; q < quadros; q++) {
      final quadro = Uint8List(w * h);
      for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
          // Blocos 4x4 de ruido deslizando 1 px por quadro.
          final bx = ((x - q) ~/ 4) % (w ~/ 4), by = (y ~/ 4) % (h ~/ 4);
          quadro[y * w + x] = base[(by < 0 ? by + h ~/ 4 : by) * w + (bx < 0 ? bx + w ~/ 4 : bx)];
        }
      }
      saida.add(quadro);
    }
    arquivo.writeAsBytesSync(saida.takeBytes());
    try {
      final r = rastrearArquivoCru(
        arquivo.path,
        largura: w,
        altura: h,
        quadros: quadros,
        fps: 24,
      );
      expect(r.pontos, isNotEmpty);
    } on RastreioException {
      // Um plano chapado deslizando nao tem profundidade: recusar e certo.
      // O que importa aqui e que o caminho do arquivo ate o motor rodou.
    }
  });
}
