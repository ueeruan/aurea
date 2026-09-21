import 'package:flutter/widgets.dart';

import '../../../../../core/ds/ds.dart';
import 'estado_da_timeline.dart';

/// AS GUIAS por cima das linhas: o fio do IMA (onde o arrasto grudou) e o
/// traco de DESTINO do reordenar. Nao pegam toque e so repintam quando uma
/// delas muda (ou a vista anda com elas na tela).
class CamadaDeGuias extends StatelessWidget {
  const CamadaDeGuias({super.key, required this.estado, required this.lista});

  final EstadoDaTimeline estado;
  final ScrollController lista;

  @override
  Widget build(BuildContext context) => IgnorePointer(
    child: RepaintBoundary(
      child: CustomPaint(
        key: const ValueKey('timeline-guias'),
        painter: _PintorDasGuias(
          estado: estado,
          lista: lista,
          guia: AureaCores.destaque,
        ),
      ),
    ),
  );
}

class _PintorDasGuias extends CustomPainter {
  _PintorDasGuias({
    required this.estado,
    required this.lista,
    required this.guia,
  }) : super(
         repaint: Listenable.merge([
           estado.guiaUs,
           estado.destinoDoReordenar,
           estado.vista,
           lista,
         ]),
       );

  final EstadoDaTimeline estado;
  final ScrollController lista;
  final Color guia;

  @override
  void paint(Canvas canvas, Size size) {
    final g = estado.guiaUs.value;
    if (g != null) {
      final x = estado.xDoTempo(g);
      if (x >= AureaDims.cabecalhoDaCamada && x <= size.width) {
        canvas.drawLine(
          Offset(x, 0),
          Offset(x, size.height),
          Paint()
            ..strokeWidth = 1
            ..color = guia,
        );
      }
    }
    final d = estado.destinoDoReordenar.value;
    if (d != null) {
      final y = d - (lista.hasClients ? lista.offset : 0);
      if (y >= -1 && y <= size.height + 1) {
        final tinta = Paint()..color = guia;
        canvas.drawRect(
          Rect.fromLTWH(0, y - 1, size.width, AureaDims.tracoDeSelecao),
          tinta,
        );
        // A bolinha na ponta diz "cai aqui".
        canvas.drawCircle(Offset(AureaDims.e4, y), 3, tinta);
      }
    }
  }

  @override
  bool shouldRepaint(_PintorDasGuias old) =>
      old.estado != estado || old.lista != lista || old.guia != guia;
}
