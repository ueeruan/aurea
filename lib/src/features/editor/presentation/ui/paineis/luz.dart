import 'package:flutter/widgets.dart';

import '../shell/contrato.dart';
import 'cena3d.dart';

/// LUZ — as luzes da cena 3D (direcional, ponto, ambiente, foco) e a
/// iluminacao do Texto 3D.
class PainelLuz extends StatelessWidget {
  const PainelLuz({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context) => Painel3DDePortas(
    layerId: layerId,
    id: PainelId.luz,
    titulo: 'Luz',
    rotuloDoTexto: 'Iluminação do texto',
    rotuloDaCena: 'Luzes da cena',
  );
}
