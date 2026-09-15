import 'dart:typed_data';

import 'package:aurea_tracker/aurea_tracker.dart';

import 'algebra_numerica.dart';
import 'camera_solver3d.dart';
import 'pontos_seguidos.dart';

/// O RASTREIO DE CAMERA PELO MOTOR EM C++ (packages/aurea_tracker).
///
/// Mesmo contrato do solver em Dart (`resolverCamera3D`): recebe rastros,
/// devolve `SolucaoCamera3D` com o mundo ja arrumado. O que muda e o que ha
/// dentro: homografia para chao plano, ajuste de feixes com a focal livre,
/// pose de TODO quadro e tripe resolvido em vez de recusado.

/// Os rastros no formato do motor: [id, quadro, x, y].
Float64List observacoesDosPontos(List<PontoSeguido> pontos) {
  var n = 0;
  for (final p in pontos) {
    n += p.observacoes.length;
  }
  final out = Float64List(n * 4);
  var i = 0;
  for (final p in pontos) {
    for (final e in p.observacoes.entries) {
      out[i++] = p.id.toDouble();
      out[i++] = e.key.toDouble();
      out[i++] = e.value.dx;
      out[i++] = e.value.dy;
    }
  }
  return out;
}

SolucaoCamera3D resolverCamera3DNativo(
  Float64List observacoes, {
  required int largura,
  required int altura,
  required int quadros,
  required int fps,
  double? focalPx,
  TipoDeTomada tipoDeTomada = TipoDeTomada.auto,
  int pontosSeguidos = 0,
}) {
  final r = resolverCameraNativa(
    observacoes: observacoes,
    largura: largura,
    altura: altura,
    quadros: quadros,
    fps: fps,
    focal: focalPx ?? 0,
    tripe: tipoDeTomada == TipoDeTomada.tripe,
  );
  switch (r.codigo) {
    case attOk:
      break;
    case attErrFewPoints:
      throw const RastreioException(
        FalhaDoRastreio.poucosPontos,
        'Poucos pontos para rastrear. O plano precisa de textura: parede '
        'lisa, ceu limpo ou desfoque forte nao dao onde agarrar.',
      );
    case attErrNoParallax:
      throw const RastreioException(
        FalhaDoRastreio.semParalaxe,
        'Nao deu para medir profundidade nesse trecho. Filme andando alguns '
        'passos, com coisas perto e longe no quadro.',
      );
    default:
      throw const RastreioException(
        FalhaDoRastreio.naoConvergiu,
        'A camera nao fechou nesse trecho. Tente um trecho sem cortes e com '
        'menos borrao.',
      );
  }
  final poses = <PoseCamera>[
    for (var i = 0; i + 13 <= r.poses.length; i += 13)
      PoseCamera(
        r.poses[i].round(),
        Mat3([for (var k = 1; k <= 9; k++) r.poses[i + k]]),
        [r.poses[i + 10], r.poses[i + 11], r.poses[i + 12]],
      ),
  ];
  final nuvem = <int, List<double>>{};
  final erros = <int, double>{};
  final vistas = <int, int>{};
  for (var i = 0; i + 6 <= r.pontos.length; i += 6) {
    final id = r.pontos[i].round();
    nuvem[id] = [r.pontos[i + 1], r.pontos[i + 2], r.pontos[i + 3]];
    erros[id] = r.pontos[i + 4];
    vistas[id] = r.pontos[i + 5].round();
  }
  if (poses.isEmpty || nuvem.isEmpty) {
    throw const RastreioException(
      FalhaDoRastreio.naoConvergiu,
      'A camera nao fechou nesse trecho.',
    );
  }
  return arrumarMundo(
    SolucaoCamera3D(
      largura: largura,
      altura: altura,
      focalPx: r.focal,
      poses: poses,
      nuvem: nuvem,
      erroPixels: r.erro,
      quadros: quadros,
      fps: fps,
      errosPorPonto: erros,
      vistasPorPonto: vistas,
      pontosSeguidos: pontosSeguidos,
      tipoDeTomada: r.tripe ? TipoDeTomada.tripe : tipoDeTomada,
    ),
  );
}
