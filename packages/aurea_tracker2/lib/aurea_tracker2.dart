/// Bindings FFI do motor 2.0 de rastreio de camera (src/motor.h).
library;

import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

final class At2Seguidor extends Opaque {}

final class At2Cena extends Opaque {}

const at2Ok = 0;
const at2ErrArgs = -1;
const at2ErrPoucosPontos = -2;
const at2ErrSemParalaxe = -3;
const at2ErrNaoConvergiu = -4;
const at2ErrPoucosQuadros = -5;

const at2ModoAuto = 0;
const at2ModoTripe = 1;

/// A assinatura do aviso de progresso vindo do C:
/// (fase, fracao, a, b, alvo).
typedef At2ProgressoNativo =
    Void Function(Int32, Double, Int32, Int32, Pointer<Void>);

@Native<Pointer<Utf8> Function()>(symbol: 'at2_versao')
external Pointer<Utf8> _versao();

/// A identidade do motor ("2.0.0"). Chamar isto e a PROVA de que a
/// biblioteca nativa carregou: quem falha aqui nao tem motor nenhum.
String versaoDoMotorNativo() => _versao().cast<Utf8>().toDartString();

@Native<Pointer<At2Seguidor> Function(Int32, Int32, Int32)>(
  symbol: 'at2_seguidor_criar',
)
external Pointer<At2Seguidor> _seguidorCriar(int w, int h, int maximo);

@Native<Void Function(Pointer<At2Seguidor>)>(symbol: 'at2_seguidor_destruir')
external void _seguidorDestruir(Pointer<At2Seguidor> s);

@Native<Int32 Function(Pointer<At2Seguidor>, Pointer<Uint8>, Int32)>(
  symbol: 'at2_seguidor_empurrar',
)
external int _seguidorEmpurrar(
  Pointer<At2Seguidor> s,
  Pointer<Uint8> cinza,
  int quadro,
);

@Native<Int32 Function(Pointer<At2Seguidor>)>(symbol: 'at2_seguidor_quantas')
external int _seguidorQuantas(Pointer<At2Seguidor> s);

@Native<Int32 Function(Pointer<At2Seguidor>, Pointer<Double>, Int32)>(
  symbol: 'at2_seguidor_observacoes',
)
external int _seguidorObservacoes(
  Pointer<At2Seguidor> s,
  Pointer<Double> saida,
  int maximo,
);

@Native<Pointer<At2Cena> Function(Int32, Int32, Int32, Int32)>(
  symbol: 'at2_cena_criar',
)
external Pointer<At2Cena> _cenaCriar(int w, int h, int quadros, int fps);

@Native<Void Function(Pointer<At2Cena>)>(symbol: 'at2_cena_destruir')
external void _cenaDestruir(Pointer<At2Cena> c);

@Native<Void Function(Pointer<At2Cena>, Pointer<Double>, Int32)>(
  symbol: 'at2_cena_observar',
)
external void _cenaObservar(
  Pointer<At2Cena> c,
  Pointer<Double> obs,
  int quantas,
);

@Native<
  Int32 Function(
    Pointer<At2Cena>,
    Double,
    Int32,
    Pointer<NativeFunction<At2ProgressoNativo>>,
    Pointer<Void>,
  )
>(symbol: 'at2_cena_resolver')
external int _cenaResolver(
  Pointer<At2Cena> c,
  double focal,
  int modo,
  Pointer<NativeFunction<At2ProgressoNativo>> progresso,
  Pointer<Void> alvo,
);

@Native<Double Function(Pointer<At2Cena>)>(symbol: 'at2_cena_focal')
external double _cenaFocal(Pointer<At2Cena> c);

@Native<Double Function(Pointer<At2Cena>)>(symbol: 'at2_cena_distorcao')
external double _cenaDistorcao(Pointer<At2Cena> c);

@Native<Double Function(Pointer<At2Cena>)>(symbol: 'at2_cena_erro')
external double _cenaErro(Pointer<At2Cena> c);

@Native<Int32 Function(Pointer<At2Cena>)>(symbol: 'at2_cena_tripe')
external int _cenaTripe(Pointer<At2Cena> c);

@Native<Int32 Function(Pointer<At2Cena>)>(symbol: 'at2_cena_quantas_poses')
external int _cenaQuantasPoses(Pointer<At2Cena> c);

@Native<Int32 Function(Pointer<At2Cena>, Pointer<Double>, Int32)>(
  symbol: 'at2_cena_poses',
)
external int _cenaPoses(Pointer<At2Cena> c, Pointer<Double> saida, int maximo);

@Native<Int32 Function(Pointer<At2Cena>)>(symbol: 'at2_cena_quantos_pontos')
external int _cenaQuantosPontos(Pointer<At2Cena> c);

@Native<Int32 Function(Pointer<At2Cena>, Pointer<Double>, Int32)>(
  symbol: 'at2_cena_pontos',
)
external int _cenaPontos(Pointer<At2Cena> c, Pointer<Double> saida, int maximo);

/// Segue pontos quadro a quadro. Os quadros tem de chegar em ordem.
class SeguidorDePontos2 {
  SeguidorDePontos2(this.largura, this.altura, {int maximoDePontos = 700})
    : _s = _seguidorCriar(largura, altura, maximoDePontos),
      _buf = calloc<Uint8>(largura * altura);

  final int largura, altura;
  final Pointer<At2Seguidor> _s;
  final Pointer<Uint8> _buf;

  /// [cinza] com largura*altura bytes. Devolve os pontos vivos.
  int empurrar(Uint8List cinza, int quadro) {
    _buf.asTypedList(largura * altura).setAll(0, cinza);
    return _seguidorEmpurrar(_s, _buf, quadro);
  }

  /// [id, quadro, x, y] por observacao.
  Float64List observacoes() {
    final n = _seguidorQuantas(_s);
    final p = calloc<Double>(n * 4 + 4);
    try {
      final escritas = _seguidorObservacoes(_s, p, n);
      return Float64List.fromList(p.asTypedList(escritas * 4));
    } finally {
      calloc.free(p);
    }
  }

  void fechar() {
    _seguidorDestruir(_s);
    calloc.free(_buf);
  }
}

class ResultadoDoMotor2 {
  const ResultadoDoMotor2({
    required this.codigo,
    required this.focal,
    required this.distorcao,
    required this.erro,
    required this.tripe,
    required this.poses,
    required this.pontos,
  });

  final int codigo;
  final double focal, distorcao, erro;
  final bool tripe;

  /// [quadro, R(9 por linha), t(3)] por pose.
  final Float64List poses;

  /// [id, x, y, z, erro_px, vistas] por ponto.
  final Float64List pontos;
}

/// Resolve a camera a partir de observacoes [id, quadro, x, y].
///
/// [progresso] e o ENDERECO de um callback nativo (NativeCallable
/// .listener) ou zero — passa como endereco cru para atravessar isolates.
ResultadoDoMotor2 resolverCenaNativa({
  required Float64List observacoes,
  required int largura,
  required int altura,
  required int quadros,
  required int fps,
  double focal = 0,
  bool tripe = false,
  int progresso = 0,
}) {
  final c = _cenaCriar(largura, altura, quadros, fps);
  if (c == nullptr) {
    return ResultadoDoMotor2(
      codigo: at2ErrArgs,
      focal: 0,
      distorcao: 0,
      erro: 0,
      tripe: false,
      poses: Float64List(0),
      pontos: Float64List(0),
    );
  }
  final entrada = calloc<Double>(observacoes.length + 4);
  try {
    entrada.asTypedList(observacoes.length).setAll(0, observacoes);
    _cenaObservar(c, entrada, observacoes.length ~/ 4);
    final codigo = _cenaResolver(
      c,
      focal,
      tripe ? at2ModoTripe : at2ModoAuto,
      Pointer<NativeFunction<At2ProgressoNativo>>.fromAddress(progresso),
      nullptr,
    );
    final np = _cenaQuantasPoses(c), nq = _cenaQuantosPontos(c);
    final bp = calloc<Double>(np * 13 + 13), bq = calloc<Double>(nq * 6 + 6);
    try {
      final ep = _cenaPoses(c, bp, np), eq = _cenaPontos(c, bq, nq);
      return ResultadoDoMotor2(
        codigo: codigo,
        focal: _cenaFocal(c),
        distorcao: _cenaDistorcao(c),
        erro: _cenaErro(c),
        tripe: _cenaTripe(c) == 1,
        poses: Float64List.fromList(bp.asTypedList(ep * 13)),
        pontos: Float64List.fromList(bq.asTypedList(eq * 6)),
      );
    } finally {
      calloc.free(bp);
      calloc.free(bq);
    }
  } finally {
    calloc.free(entrada);
    _cenaDestruir(c);
  }
}
