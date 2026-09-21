import 'package:flutter/services.dart';

import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import 'estado_da_timeline.dart';
import 'ima.dart';
import 'keyframes_da_timeline.dart';
import 'sessao_de_gesto.dart';

/// Quem move a marca de [deUs] para [paraUs] (tempo LOCAL da camada).
/// Nulo = moveu.
typedef MoverMarca = MotivoDoKeyframeParado? Function(int deUs, int paraUs);

/// UM LOSANGO NA MAO.
///
/// A marca cai SEMPRE num quadro (onde o cabecote consegue parar), um
/// quadro depois da vizinha de tras e um antes da da frente (encostar
/// juntaria dois instantes num losango so) e dentro da camada. Perto do
/// cabecote ela gruda nele, com um toque leve.
///
/// O alvo de cada passo parte da ORIGEM do gesto (e nao do passo anterior):
/// o ima nao prende o dedo. A vista fica parada enquanto o dedo manda — se
/// o tempo andasse junto, a marca fugiria de baixo do dedo.
class ArrastoDeLosango {
  ArrastoDeLosango._({
    required this.estado,
    required this.sessao,
    required this.inicioUs,
    required this.origemUs,
    required this.deslocUs,
    required this.quadroMin,
    required this.quadroMax,
    required this.fps,
    required this.mover,
    required this.xDoDedo,
  }) : atualUs = origemUs,
       xInicial = xDoDedo;

  /// Monta o arrasto da marca [origemUs] (local) da [camada]. [vizinhas] sao
  /// os instantes (locais, ordenados) que a marca nao pode atravessar.
  factory ArrastoDeLosango.comecar({
    required EstadoDaTimeline estado,
    required EditorController controlador,
    required Layer camada,
    required List<int> vizinhas,
    required int origemUs,
    required double xDoDedo,
    required int fps,
    required MoverMarca mover,
  }) {
    final f = fps < 1 ? 30 : fps;
    final inicio = camada.startTime.inMicroseconds;
    int? antes;
    int? depois;
    for (final t in vizinhas) {
      if (t < origemUs) {
        antes = t;
      } else if (t > origemUs) {
        depois = t;
        break;
      }
    }
    // A folga de um milesimo de quadro absorve o arredondamento para cima
    // do instante de cada quadro (ver `PlaybackController._naGrade`).
    var qMin = (quadroExato(inicio, f) - 1e-3).ceil();
    var qMax = (quadroExato(inicio + camada.duration.inMicroseconds, f) + 1e-3)
        .floor();
    if (antes != null) {
      final q = (quadroExato(inicio + antes, f) + 1 - 1e-3).ceil();
      if (q > qMin) qMin = q;
    }
    if (depois != null) {
      final q = (quadroExato(inicio + depois, f) - 1 + 1e-3).floor();
      if (q < qMax) qMax = q;
    }
    HapticFeedback.lightImpact();
    return ArrastoDeLosango._(
      estado: estado,
      sessao: SessaoDeGesto(controlador),
      inicioUs: inicio,
      origemUs: origemUs,
      deslocUs: inicio + origemUs - estado.tempoDoX(xDoDedo),
      quadroMin: qMin,
      quadroMax: qMax,
      fps: f,
      mover: mover,
      xDoDedo: xDoDedo,
    );
  }

  final EstadoDaTimeline estado;
  final SessaoDeGesto sessao;
  final int inicioUs;
  final int origemUs;

  /// Onde a marca esta agora (local).
  int atualUs;

  /// A distancia (µs) entre o instante sob o dedo e a marca, no toque.
  final double deslocUs;
  final int quadroMin;
  final int quadroMax;
  final int fps;
  final MoverMarca mover;
  double xDoDedo;

  /// Onde o dedo pegou a marca (a auto-rolagem so vale para longe dele).
  final double xInicial;

  /// Chamado quando a marca de fato mudou de lugar.
  void Function(int deUs, int paraUs)? aoMover;

  final HapticoDoIma _haptico = HapticoDoIma();

  /// O dedo foi para [x] (ou a vista rolou com o dedo parado nele).
  void seguir(double x) {
    xDoDedo = x;
    if (quadroMin > quadroMax) return;
    final desejado = estado.tempoDoX(x) + deslocUs;
    final cabecote = estado.vistaUs.value;
    final perto =
        (desejado - cabecote).abs() <= estado.usPorPx(toleranciaDoCabecoteDp);
    final alvo = perto ? cabecote : desejado;
    final quadro = (alvo * fps / 1e6).round().clamp(quadroMin, quadroMax);
    final global = instanteDoQuadroUs(quadro, fps);
    final guia = perto ? global : null;
    estado.guiaUs.value = guia;
    _haptico.avisar(guia);
    sessao.pedir(() {
      final para = global - inicioUs;
      if (para == atualUs) return;
      sessao.abrir();
      if (mover(atualUs, para) != null) return;
      final de = atualUs;
      atualUs = para;
      aoMover?.call(de, para);
    });
  }

  void encerrar() {
    sessao.encerrar();
    estado.guiaUs.value = null;
  }

  void descartar() {
    sessao.descartar();
    estado.guiaUs.value = null;
  }
}
