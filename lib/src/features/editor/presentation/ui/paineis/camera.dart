import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../am/camera_sheet.dart' show showCameraSheet;
import '../shell/contrato.dart';
import 'comum.dart';

/// CAMERA — a lente da camera da composicao (zoom, com losango); foco,
/// neblina e projecao pela folha da camera que ja existe.
class PainelCamera extends ConsumerWidget {
  const PainelCamera({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Câmera';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, layerId);
    final gravada = camadaGravada(ref, layerId);
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
    final c = ref.read(editorControllerProvider.notifier);
    final porta = LinhaDePorta(
      rotulo: 'Foco, neblina e projeção',
      icone: CupertinoIcons.camera,
      aoTocar: () {
        escopo.playback.pause();
        showCameraSheet(context, ref, layerId, escopo.playback);
      },
    );
    if (visivel is! CameraLayer || gravada is! CameraLayer) {
      return PainelDePortas(
        titulo: _titulo,
        chave: 'painel-${PainelId.camera.name}',
        aviso: 'Esta camada não é uma câmera.',
        portas: const [],
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.camera.name}',
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) {
          final kf = losangoDasMarcas(
            marcasUs: [
              for (final k in gravada.zoom.keyframes) k.time.inMicroseconds,
            ],
            camada: gravada,
            t: t,
            playback: escopo.playback,
            aoAlternar: () => c.toggleCameraZoomKeyframe(layerId, t),
          );
          return ListView(
            padding: const EdgeInsets.fromLTRB(
              AureaDims.margemDoPainel,
              AureaDims.e4,
              AureaDims.margemDoPainel,
              AureaDims.topoDoPainel,
            ),
            children: [
              AureaPropertyRow(
                rotulo: 'Zoom',
                valor: visivel.zoom.valueAt(visivel.localTime(t)),
                aoMudar: (v) => c.editCameraZoom(layerId, t, v),
                min: 60,
                max: 12000,
                casas: 0,
                sensibilidade: 4,
                keyframe: kf.estado,
                aoAnterior: kf.anterior,
                aoProximo: kf.proximo,
                aoComecarGesto: c.beginGesture,
                aoTerminarGesto: c.endGesture,
              ),
              porta,
            ],
          );
        },
      ),
    );
  }
}
