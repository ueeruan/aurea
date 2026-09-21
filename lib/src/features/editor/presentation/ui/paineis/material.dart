import 'package:flutter/widgets.dart';

import '../shell/contrato.dart';
import 'cena3d.dart';

/// MATERIAL — o material de cada objeto 3D (realista, sem luz,
/// transparente, recorte) e, no Texto 3D, o metal da letra.
class PainelMaterial extends StatelessWidget {
  const PainelMaterial({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context) => Painel3DDePortas(
    layerId: layerId,
    id: PainelId.material,
    titulo: 'Material',
    rotuloDoTexto: 'Metal e acabamento do texto',
    rotuloDaCena: 'Material dos objetos',
  );
}
