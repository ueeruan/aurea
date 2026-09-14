/// Bindings FFI do rastreio de camera 3D (src/tracker.h).
library;

import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

final class AttTracker extends Opaque {}

final class AttSfm extends Opaque {}

const attOk = 0;
const attErrFewPoints = -2;
const attErrNoParallax = -3;
const attErrDiverged = -4;

@Native<Int32 Function()>(symbol: 'att_version', isLeaf: true)
external int attVersion();

@Native<Pointer<AttTracker> Function(Int32, Int32, Int32)>(
  symbol: 'att_tracker_create',
)
external Pointer<AttTracker> _trackerCreate(int w, int h, int maxPoints);

@Native<Void Function(Pointer<AttTracker>)>(symbol: 'att_tracker_destroy')
external void _trackerDestroy(Pointer<AttTracker> t);

@Native<Int32 Function(Pointer<AttTracker>, Pointer<Uint8>, Int32)>(
  symbol: 'att_tracker_push',
)
external int _trackerPush(Pointer<AttTracker> t, Pointer<Uint8> gray, int f);

@Native<Int32 Function(Pointer<AttTracker>)>(
  symbol: 'att_tracker_observation_count',
)
external int _trackerObsCount(Pointer<AttTracker> t);

@Native<Int32 Function(Pointer<AttTracker>, Pointer<Double>, Int32)>(
  symbol: 'att_tracker_observations',
)
external int _trackerObs(Pointer<AttTracker> t, Pointer<Double> out, int max);

@Native<Pointer<AttSfm> Function(Int32, Int32, Int32, Int32)>(
  symbol: 'att_sfm_create',
)
external Pointer<AttSfm> _sfmCreate(int w, int h, int frames, int fps);

@Native<Void Function(Pointer<AttSfm>)>(symbol: 'att_sfm_destroy')
external void _sfmDestroy(Pointer<AttSfm> s);

@Native<Void Function(Pointer<AttSfm>, Pointer<Double>, Int32)>(
  symbol: 'att_sfm_add',
)
external void _sfmAdd(Pointer<AttSfm> s, Pointer<Double> obs, int n);

@Native<Int32 Function(Pointer<AttSfm>, Double, Int32)>(symbol: 'att_sfm_solve')
external int _sfmSolve(Pointer<AttSfm> s, double focal, int mode);

@Native<Double Function(Pointer<AttSfm>)>(symbol: 'att_sfm_focal')
external double _sfmFocal(Pointer<AttSfm> s);

@Native<Double Function(Pointer<AttSfm>)>(symbol: 'att_sfm_error')
external double _sfmError(Pointer<AttSfm> s);

@Native<Int32 Function(Pointer<AttSfm>)>(symbol: 'att_sfm_is_tripod')
external int _sfmIsTripod(Pointer<AttSfm> s);

@Native<Int32 Function(Pointer<AttSfm>)>(symbol: 'att_sfm_pose_count')
external int _sfmPoseCount(Pointer<AttSfm> s);

@Native<Int32 Function(Pointer<AttSfm>, Pointer<Double>, Int32)>(
  symbol: 'att_sfm_poses',
)
external int _sfmPoses(Pointer<AttSfm> s, Pointer<Double> out, int max);

@Native<Int32 Function(Pointer<AttSfm>)>(symbol: 'att_sfm_point_count')
external int _sfmPointCount(Pointer<AttSfm> s);

@Native<Int32 Function(Pointer<AttSfm>, Pointer<Double>, Int32)>(
  symbol: 'att_sfm_points',
)
external int _sfmPoints(Pointer<AttSfm> s, Pointer<Double> out, int max);

/// Segue pontos quadro a quadro. Os quadros tem de chegar em ordem.
class SeguidorDePontosNativo {
  SeguidorDePontosNativo(this.largura, this.altura, {int maximoDePontos = 400})
    : _t = _trackerCreate(largura, altura, maximoDePontos),
      _buf = calloc<Uint8>(largura * altura);

  final int largura, altura;
  final Pointer<AttTracker> _t;
  final Pointer<Uint8> _buf;

  /// [cinza] com largura*altura bytes. Devolve os pontos vivos.
  int empurrar(Uint8List cinza, int quadro) {
    _buf.asTypedList(largura * altura).setAll(0, cinza);
    return _trackerPush(_t, _buf, quadro);
  }

  /// [id, quadro, x, y] por observacao.
  Float64List observacoes() {
    final n = _trackerObsCount(_t);
    final p = calloc<Double>(n * 4 + 4);
    try {
      final escritas = _trackerObs(_t, p, n);
      return Float64List.fromList(p.asTypedList(escritas * 4));
    } finally {
      calloc.free(p);
    }
  }

  void fechar() {
    _trackerDestroy(_t);
    calloc.free(_buf);
  }
}

class ResultadoDoRastreioNativo {
  const ResultadoDoRastreioNativo({
    required this.codigo,
    required this.focal,
    required this.erro,
    required this.tripe,
    required this.poses,
    required this.pontos,
  });

  final int codigo;
  final double focal, erro;
  final bool tripe;

  /// [quadro, R(9 por linha), t(3)] por pose.
  final Float64List poses;

  /// [id, x, y, z, erro_px, vistas] por ponto.
  final Float64List pontos;
}

/// Resolve a camera a partir de observacoes [id, quadro, x, y].
ResultadoDoRastreioNativo resolverCameraNativa({
  required Float64List observacoes,
  required int largura,
  required int altura,
  required int quadros,
  required int fps,
  double focal = 0,
  bool tripe = false,
}) {
  final s = _sfmCreate(largura, altura, quadros, fps);
  if (s == nullptr) {
    return ResultadoDoRastreioNativo(
      codigo: -1,
      focal: 0,
      erro: 0,
      tripe: false,
      poses: Float64List(0),
      pontos: Float64List(0),
    );
  }
  final entrada = calloc<Double>(observacoes.length + 4);
  try {
    entrada.asTypedList(observacoes.length).setAll(0, observacoes);
    _sfmAdd(s, entrada, observacoes.length ~/ 4);
    final codigo = _sfmSolve(s, focal, tripe ? 1 : 0);
    final np = _sfmPoseCount(s), nq = _sfmPointCount(s);
    final bp = calloc<Double>(np * 13 + 13), bq = calloc<Double>(nq * 6 + 6);
    try {
      final ep = _sfmPoses(s, bp, np), eq = _sfmPoints(s, bq, nq);
      return ResultadoDoRastreioNativo(
        codigo: codigo,
        focal: _sfmFocal(s),
        erro: _sfmError(s),
        tripe: _sfmIsTripod(s) == 1,
        poses: Float64List.fromList(bp.asTypedList(ep * 13)),
        pontos: Float64List.fromList(bq.asTypedList(eq * 6)),
      );
    } finally {
      calloc.free(bp);
      calloc.free(bq);
    }
  } finally {
    calloc.free(entrada);
    _sfmDestroy(s);
  }
}
