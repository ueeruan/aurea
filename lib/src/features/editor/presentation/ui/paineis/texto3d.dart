import 'package:flutter/widgets.dart';

import '../shell/contrato.dart';
import 'cena3d.dart';

/// TEXTO 3D — a letra, a fonte, o metal, a profundidade, o chanfro e o
/// cartao Caracteres, na folha do Texto 3D que ja existe.
class PainelTexto3D extends StatelessWidget {
  const PainelTexto3D({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context) => Painel3DDePortas(
    layerId: layerId,
    id: PainelId.texto3d,
    titulo: 'Texto 3D',
    rotuloDoTexto: 'Letra, metal, profundidade e caracteres',
    rotuloDaCena: 'Objetos da cena',
  );
}
