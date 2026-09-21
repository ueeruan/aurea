import 'package:flutter/widgets.dart';

import '../shell/contrato.dart';
import 'cena3d.dart';

/// ANIMACAO (3D) — as animacoes prontas do Texto 3D e os keyframes dos
/// objetos da cena.
class PainelAnimacao3D extends StatelessWidget {
  const PainelAnimacao3D({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context) => Painel3DDePortas(
    layerId: layerId,
    id: PainelId.animacao3d,
    titulo: 'Animação 3D',
    rotuloDoTexto: 'Animações do texto',
    rotuloDaCena: 'Movimento dos objetos',
  );
}
