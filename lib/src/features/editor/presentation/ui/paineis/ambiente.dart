import 'package:flutter/widgets.dart';

import '../shell/contrato.dart';
import 'cena3d.dart';

/// AMBIENTE — ceu, neblina, panorama, reflexo e tom da cena 3D, e o
/// ambiente e o reflexo do Texto 3D.
class PainelAmbiente extends StatelessWidget {
  const PainelAmbiente({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context) => Painel3DDePortas(
    layerId: layerId,
    id: PainelId.ambiente,
    titulo: 'Ambiente',
    rotuloDoTexto: 'Ambiente e reflexo do texto',
    rotuloDaCena: 'Céu, neblina e reflexo',
  );
}
