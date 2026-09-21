import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../domain/layer.dart';
import '../../am/cameras_sheet.dart' show showCamerasSheet;
import '../../am/cena3d_sheet.dart' show showCena3DSheet;
import '../../am/layer_menu.dart' show showElement3DSheet;
import '../../am/texto3d_sheet.dart' show showTexto3DSheet;
import '../shell/contrato.dart';
import 'comum.dart';

// AS PORTAS DO 3D. Os paineis de Material, Luz, Ambiente, Animacao,
// Texto 3D e Cena abrem, por enquanto, as folhas do 3D que ja existem
// (`cena3d_sheet`, `texto3d_sheet`, a ficha do Elemento 3D): o motor e o
// mesmo, e a frente do 3D reescreve cada painel no lugar deste.

/// Abre a folha da cena 3D (objetos, material, luz, ambiente).
Future<void> abrirFolhaDaCena(
  BuildContext context,
  WidgetRef ref,
  Layer camada,
) {
  final escopo = EscopoDoEditor.of(context);
  escopo.playback.pause();
  if (camada is Element3DLayer) {
    return showElement3DSheet(context, ref, camada.id);
  }
  return showCena3DSheet(
    context,
    ref,
    sceneId: camada.id,
    playhead: escopo.playback.time.value,
  );
}

/// O no de texto 3D da camada (nulo quando ela nao tem).
String? noDoTexto3D(Layer camada) => camada is Scene3DLayer
    ? camada.scene.nodes.where((n) => n.texto3d != null).firstOrNull?.id
    : null;

/// Abre a folha do Texto 3D (letra, fonte, metal, profundidade, chanfro,
/// caracteres, ambiente e animacoes do texto).
Future<void> abrirFolhaDoTexto3D(
  BuildContext context,
  WidgetRef ref,
  Layer camada,
) async {
  final no = noDoTexto3D(camada);
  if (no == null) return;
  final escopo = EscopoDoEditor.of(context);
  escopo.playback.pause();
  await showTexto3DSheet(
    context,
    ref,
    sceneId: camada.id,
    nodeId: no,
    playhead: escopo.playback.time.value,
    playback: escopo.playback,
  );
}

/// Um painel 3D feito de portas, para [id].
class Painel3DDePortas extends ConsumerWidget {
  const Painel3DDePortas({
    super.key,
    required this.layerId,
    required this.id,
    required this.titulo,
    required this.rotuloDaCena,
    this.rotuloDoTexto,
  });

  final String layerId;
  final PainelId id;
  final String titulo;

  /// A porta da folha da cena/elemento.
  final String rotuloDaCena;

  /// A porta da folha do Texto 3D, quando a camada tem texto 3D.
  final String? rotuloDoTexto;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return PainelSemCamada(titulo: titulo);
    final e3d = camada is Scene3DLayer || camada is Element3DLayer;
    final temTexto = noDoTexto3D(camada) != null;
    return PainelDePortas(
      titulo: titulo,
      chave: 'painel-${id.name}',
      aviso: e3d ? null : 'Esta camada não é 3D.',
      portas: [
        if (temTexto && rotuloDoTexto != null)
          LinhaDePorta(
            rotulo: rotuloDoTexto!,
            icone: CupertinoIcons.textformat,
            aoTocar: () => abrirFolhaDoTexto3D(context, ref, camada),
          ),
        if (e3d)
          LinhaDePorta(
            rotulo: rotuloDaCena,
            icone: CupertinoIcons.cube_box,
            aoTocar: () => abrirFolhaDaCena(context, ref, camada),
          ),
      ],
    );
  }
}

/// CENA — objetos, cameras e cortes da cena 3D.
class PainelCena3D extends ConsumerWidget {
  const PainelCena3D({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: 'Cena 3D');
    final escopo = EscopoDoEditor.of(context);
    return PainelDePortas(
      titulo: 'Cena 3D',
      chave: 'painel-${PainelId.cena3d.name}',
      portas: [
        LinhaDePorta(
          rotulo: 'Objetos, material, luz e ambiente',
          icone: CupertinoIcons.cube_box,
          aoTocar: () => abrirFolhaDaCena(context, ref, camada),
        ),
        if (camada is Scene3DLayer)
          LinhaDePorta(
            rotulo: 'Câmeras e cortes',
            icone: CupertinoIcons.videocam,
            aoTocar: () {
              escopo.playback.pause();
              showCamerasSheet(context, ref, layerId, escopo.playback);
            },
          ),
      ],
    );
  }
}
