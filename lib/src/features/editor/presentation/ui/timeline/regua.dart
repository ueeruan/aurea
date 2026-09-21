import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/foundation.dart' show listEquals;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/video_project.dart' show Marker;
import 'estado_da_timeline.dart';

/// A REGUA (42): riscos de segundo (18), meio segundo (12) e quadro (5, so
/// quando cabem com [AureaDims.vaoMinimoDoRisco] de vao), o tempo nos
/// segundos que cabem um rotulo, e as marcas do projeto. So a janela visivel
/// e desenhada; o tique do relogio repinta, nunca reconstroi.
class ReguaDaTimeline extends ConsumerWidget {
  const ReguaDaTimeline({super.key, required this.estado});

  final EstadoDaTimeline estado;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // POR VALOR: o projeto refaz as listas de marcas e batidas a cada
    // edicao (o construtor copia), e por identidade a regua se
    // reconstruiria a cada passo de qualquer arrasto.
    final dados = ref.watch(
      projetoVisivelProvider.select(
        (p) => _DadosDaRegua(p.fps, p.markers, p.beats),
      ),
    );
    final base = DefaultTextStyle.of(context).style;
    final controlador = ref.read(editorControllerProvider.notifier);
    // O caminho do grupo so muda ao entrar e sair: observa-lo pelo projeto
    // e barato (a lista de camadas troca quando se entra num grupo).
    final caminho = ref.watch(
      editorControllerProvider.select(
        (_) => controlador.caminhoDoGrupo.join('›'),
      ),
    );
    return SizedBox(
      key: const ValueKey('timeline-regua'),
      height: AureaDims.regua,
      child: Stack(
        children: [
          Positioned.fill(
            child: RepaintBoundary(
              child: CustomPaint(
                painter: PintorDaRegua(
                  estado: estado,
                  fps: dados.fps <= 0 ? 30 : dados.fps,
                  marcas: dados.marcas,
                  batidas: dados.batidas,
                  risco: AureaCores.textoSecundario,
                  estilo: base.copyWith(
                    fontSize: AureaDims.textoDeRotulo,
                    color: AureaCores.textoSecundario,
                    decoration: TextDecoration.none,
                  ),
                ),
              ),
            ),
          ),
          // DENTRO DE UM GRUPO: o caminho de volta mora na ponta da regua,
          // acima dos cabecalhos. Tocar sai um nivel.
          if (caminho.isNotEmpty)
            Positioned(
              left: AureaDims.e4,
              top: AureaDims.e4,
              width: AureaDims.cabecalhoDaCamada + 24,
              height: 20,
              child: GestureDetector(
                key: const ValueKey('timeline-sair-do-grupo'),
                behavior: HitTestBehavior.opaque,
                onTap: controlador.exitGroup,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: AureaDims.e4),
                  decoration: BoxDecoration(
                    color: AureaCores.elevado,
                    borderRadius: BorderRadius.circular(AureaDims.raioLg),
                  ),
                  child: Row(
                    children: [
                      Icon(
                        CupertinoIcons.chevron_left,
                        size: 11,
                        color: AureaCores.texto,
                      ),
                      const SizedBox(width: 2),
                      Flexible(
                        child: Text(
                          controlador.caminhoDoGrupo.last,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: base.copyWith(
                            fontSize: AureaDims.textoDeRotulo,
                            color: AureaCores.texto,
                            decoration: TextDecoration.none,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}

/// O que a regua desenha do projeto, comparado por valor.
@immutable
class _DadosDaRegua {
  const _DadosDaRegua(this.fps, this.marcas, this.batidas);

  final int fps;
  final List<Marker> marcas;
  final List<Duration> batidas;

  @override
  bool operator ==(Object other) {
    if (other is! _DadosDaRegua ||
        other.fps != fps ||
        other.marcas.length != marcas.length ||
        !listEquals(other.batidas, batidas)) {
      return false;
    }
    for (var i = 0; i < marcas.length; i++) {
      final a = marcas[i];
      final b = other.marcas[i];
      if (a.time != b.time || a.label != b.label || a.color != b.color) {
        return false;
      }
    }
    return true;
  }

  @override
  int get hashCode => Object.hash(fps, marcas.length, batidas.length);
}

/// Os passos em que um ROTULO de tempo pode cair (segundos).
const _passosDeRotulo = <int>[
  1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600, //
];

/// Metade do selo do tempo do cabecote (70), com um respiro.
const double _meiaLarguraDoSelo = 35 + 3;

/// O espaco minimo entre dois rotulos de tempo, em dp.
const double _vaoDoRotulo = 56;

class PintorDaRegua extends CustomPainter {
  PintorDaRegua({
    required this.estado,
    required this.fps,
    required this.marcas,
    required this.batidas,
    required this.risco,
    required this.estilo,
  }) : super(repaint: estado.vista);

  final EstadoDaTimeline estado;
  final int fps;
  final List<Marker> marcas;
  final List<Duration> batidas;
  final Color risco;
  final TextStyle estilo;

  /// Os rotulos ja medidos: medir texto e a conta mais cara que cabe num
  /// `paint`, e a regua repinta a cada quadro de scrub e de play.
  final Map<int, TextPainter> _rotulos = {};

  TextPainter _rotulo(int segundo) {
    final pronto = _rotulos[segundo];
    if (pronto != null) return pronto;
    if (_rotulos.length > 160) _rotulos.clear();
    final h = segundo ~/ 3600;
    final m = (segundo % 3600) ~/ 60;
    final s = segundo % 60;
    final texto = h > 0
        ? '$h:${m.toString().padLeft(2, '0')}:${s.toString().padLeft(2, '0')}'
        : '$m:${s.toString().padLeft(2, '0')}';
    return _rotulos[segundo] = TextPainter(
      text: TextSpan(text: texto, style: estilo),
      textDirection: TextDirection.ltr,
    )..layout();
  }

  @override
  void paint(Canvas canvas, Size size) {
    final e = estado;
    final pps = e.pps.value;
    final baseY = size.height;
    final t0 = e.tempoDoX(0) / 1e6;
    final t1 = e.tempoDoX(size.width) / 1e6;
    final passoDoRotulo = _passosDeRotulo.firstWhere(
      (p) => p * pps >= _vaoDoRotulo,
      orElse: () => _passosDeRotulo.last,
    );
    // Os riscos de segundo andam de um em um enquanto cabem com o vao
    // minimo; afastado demais, andam no passo do rotulo.
    final passoDoSegundo = pps >= AureaDims.vaoMinimoDoRisco
        ? 1
        : _passosDeRotulo.firstWhere(
            (p) => p * pps >= AureaDims.vaoMinimoDoRisco,
            orElse: () => _passosDeRotulo.last,
          );
    final comMeio = pps / 2 >= AureaDims.vaoMinimoDoRisco;
    final passoDoQuadro = pps / fps;
    final comQuadros = passoDoQuadro >= AureaDims.vaoMinimoDoRisco;

    // TRES CAMINHOS, TRES CHAMADAS, em vez de um `drawLine` por risco.
    final segundos = Path();
    final meios = Path();
    final quadros = Path();
    final primeiro = math.max(
      0,
      (t0 / passoDoSegundo).floor() * passoDoSegundo,
    );
    final ultimo = t1.ceil();
    for (var s = primeiro; s <= ultimo; s += passoDoSegundo) {
      final x = e.xDoTempo(s * 1e6);
      segundos
        ..moveTo(x, baseY - AureaDims.riscoDeSegundo)
        ..lineTo(x, baseY);
      if (s % passoDoRotulo == 0) {
        final r = _rotulo(s);
        // O SELO DO TEMPO (70, no centro) cobre o rotulo que cairia por
        // baixo dele: melhor nao desenhar que mostrar meio numero.
        final debaixoDoSelo =
            x + 3 + r.width > e.centro - _meiaLarguraDoSelo &&
            x + 3 < e.centro + _meiaLarguraDoSelo;
        if (!debaixoDoSelo) {
          r.paint(
            canvas,
            Offset(x + 3, baseY - AureaDims.riscoDeSegundo - r.height),
          );
        }
      }
      if (passoDoSegundo != 1) continue;
      if (comMeio) {
        final xm = x + pps / 2;
        meios
          ..moveTo(xm, baseY - AureaDims.riscoDeMeioSegundo)
          ..lineTo(xm, baseY);
      }
      if (comQuadros) {
        for (var q = 1; q < fps; q++) {
          // O quadro que cai no meio segundo ja tem o risco maior.
          if (comMeio && q * 2 == fps) continue;
          final xq = x + q * passoDoQuadro;
          quadros
            ..moveTo(xq, baseY - AureaDims.riscoDeQuadro)
            ..lineTo(xq, baseY);
        }
      }
    }
    final pincel = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = AureaDims.larguraDoRisco
      ..color = risco;
    canvas.drawPath(segundos, pincel);
    canvas.drawPath(meios, pincel);
    canvas.drawPath(quadros, pincel..color = risco.withValues(alpha: .6));

    _pintarBatidas(canvas, size, t0, t1);
    _pintarMarcas(canvas, size);
  }

  /// AS BATIDAS: risquinhos finos na base (densos demais para bandeira).
  void _pintarBatidas(Canvas canvas, Size size, double t0, double t1) {
    if (batidas.isEmpty) return;
    final deUs = (t0 * 1e6).floor();
    var lo = 0;
    var hi = batidas.length;
    while (lo < hi) {
      final mid = (lo + hi) >> 1;
      if (batidas[mid].inMicroseconds < deUs) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    final caminho = Path();
    final ateUs = t1 * 1e6;
    for (var i = lo; i < batidas.length; i++) {
      final us = batidas[i].inMicroseconds;
      if (us > ateUs) break;
      final x = estado.xDoTempo(us);
      caminho
        ..moveTo(x, size.height - 8)
        ..lineTo(x, size.height);
    }
    canvas.drawPath(
      caminho,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = AureaCores.destaque.withValues(alpha: .55),
    );
  }

  /// AS MARCAS do projeto: bandeirinha no alto e um fio ate a base.
  void _pintarMarcas(Canvas canvas, Size size) {
    for (final m in marcas) {
      final x = estado.xDoTempo(m.time.inMicroseconds);
      if (x < -6 || x > size.width + 6) continue;
      final tinta = Paint()..color = m.color;
      canvas.drawPath(
        Path()
          ..moveTo(x - 4, 0)
          ..lineTo(x + 4, 0)
          ..lineTo(x, 7)
          ..close(),
        tinta,
      );
      canvas.drawLine(
        Offset(x, 7),
        Offset(x, size.height),
        tinta
          ..strokeWidth = 1
          ..color = m.color.withValues(alpha: .6),
      );
    }
  }

  @override
  bool shouldRepaint(PintorDaRegua old) =>
      old.estado != estado ||
      old.fps != fps ||
      !identical(old.marcas, marcas) ||
      !identical(old.batidas, batidas) ||
      old.risco != risco ||
      old.estilo != estilo;
}

/// O relogio do cabecote em texto (m:ss:qq) — o mesmo formato do selo.
String textoDoTempo(Duration t, int fps) {
  final us = t.inMicroseconds;
  final m = us ~/ 60000000;
  final s = (us % 60000000) ~/ 1000000;
  final q = (us % 1000000) * fps ~/ 1000000;
  return '$m:${s.toString().padLeft(2, '0')}:${q.toString().padLeft(2, '0')}';
}
