// O MUNDO DO RASTREIO nas maos da pessoa: origem e escala real.
//
// A prova central e a INVARIANCIA: mover ou escalar o mundo inteiro nao
// pode mudar um pixel da reprojecao — se mudou, a transformacao nao foi
// de semelhanca e o objeto colado escorregaria.
import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:flutter_test/flutter_test.dart';

SolucaoCamera3D _mundinho() {
  // Duas cameras olhando para +Z e tres pontos na frente delas.
  PoseCamera pose(int q, List<double> c, List<double> giroRad) {
    final r = rotacaoDeVetor(giroRad);
    final rc = r.aplicar(c);
    return PoseCamera(q, r, [-rc[0], -rc[1], -rc[2]]);
  }

  return SolucaoCamera3D(
    largura: 640,
    altura: 360,
    focalPx: 700,
    poses: [
      pose(0, [0, 0, 0], [0, 0, 0]),
      pose(10, [120, 8, -30], [0, -.12, 0]),
    ],
    nuvem: {
      1: [-80, 40, 500],
      2: [60, -20, 620],
      3: [10, 90, 430],
    },
    erroPixels: .4,
    quadros: 11,
    fps: 24,
  );
}

List<Offset> _pixels(SolucaoCamera3D s) {
  final out = <Offset>[];
  for (final p in s.poses) {
    for (final v in s.nuvem.values) {
      final proj = projetar(p.rotacao, p.translacao, v);
      expect(proj, isNotNull, reason: 'ponto atras da camera');
      out.add(
        Offset(
          s.largura / 2 + proj![0] * s.focalPx,
          s.altura / 2 + proj[1] * s.focalPx,
        ),
      );
    }
  }
  return out;
}

void _mesmosPixels(SolucaoCamera3D a, SolucaoCamera3D b) {
  final pa = _pixels(a), pb = _pixels(b);
  expect(pa.length, pb.length);
  for (var i = 0; i < pa.length; i++) {
    expect((pa[i] - pb[i]).distance, lessThan(1e-6));
  }
}

void main() {
  test('definir origem move o (0,0,0) sem mexer um pixel da reprojecao', () {
    final s = _mundinho();
    final nova = definirOrigem(s, s.nuvem[2]!);
    expect(nova.nuvem[2], [0, 0, 0]);
    // A camera anda junto: a posicao relativa ao ponto nao muda.
    expect(
      nova.poses.first.posicao[0] - nova.nuvem[1]![0],
      closeTo(s.poses.first.posicao[0] - s.nuvem[1]![0], 1e-9),
    );
    _mesmosPixels(s, nova);
  });

  test('escalar o mundo muda o tamanho e nada mais', () {
    final s = _mundinho();
    final nova = escalarMundo(s, 2.5);
    expect(nova.nuvem[1]![2], closeTo(1250, 1e-9));
    expect(nova.poses.last.posicao[0], closeTo(300, 1e-9));
    _mesmosPixels(s, nova);
    // Fator invalido nao faz nada.
    expect(identical(escalarMundo(s, 0), s), isTrue);
    expect(identical(escalarMundo(s, double.nan), s), isTrue);
  });

  test('a distancia real vira o fator certo (100 unidades = 1 metro)', () {
    final s = _mundinho();
    // |p1 - p2| e conhecido; pedir 2 m deixa a distancia em 200 unidades.
    final f = fatorDeEscalaReal(s, 1, 2, 2)!;
    final nova = escalarMundo(s, f);
    final a = nova.nuvem[1]!, b = nova.nuvem[2]!;
    expect(
      norma([a[0] - b[0], a[1] - b[1], a[2] - b[2]]),
      closeTo(2 * unidadesPorMetro, 1e-6),
    );
    expect(fatorDeEscalaReal(s, 1, 99, 2), isNull, reason: 'ponto inexistente');
    expect(fatorDeEscalaReal(s, 1, 2, 0), isNull, reason: 'medida invalida');
  });

  test('as unidades convertem para metros como no mundo real', () {
    expect(UnidadeReal.mm.metros, closeTo(.001, 1e-12));
    expect(UnidadeReal.cm.metros, closeTo(.01, 1e-12));
    expect(UnidadeReal.m.metros, 1);
    expect(UnidadeReal.pol.metros, closeTo(.0254, 1e-12));
    expect(UnidadeReal.pe.metros, closeTo(.3048, 1e-12));
  });
}
