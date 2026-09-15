import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../context/parameter_row.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// A FOLHA DA CAMERA da composicao — a lente.
///
/// Posicao, giro 3D e Z moram no transform, como em qualquer camada; o
/// que so a camera tem e a LENTE (distancia focal em pixels). 1200 e a
/// lente neutra: a composicao fica identica a um projeto sem camera.
/// Menos e grande-angular (o mundo entorta para os lados), mais e
/// teleobjetiva (o fundo "chega perto"). Animar a lente com a posicao
/// e o dolly-zoom de Hitchcock — por isso ela tem losango.
Future<void> showCameraSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) => showParamSheet(
  context,
  title: 'Câmera',
  heightFactor: 0.4,
  builder: (sheetContext) =>
      _Camera(ref: ref, layerId: layerId, playback: playback),
);

class _Camera extends StatelessWidget {
  const _Camera({
    required this.ref,
    required this.layerId,
    required this.playback,
  });

  final WidgetRef ref;
  final String layerId;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context) => SafeArea(
    child: ListenableBuilder(
      listenable: playback.time,
      builder: (context, _) {
        final projeto = ref.watch(editorControllerProvider);
        final camada = projeto.layerById(layerId);
        if (camada is! CameraLayer) return const SizedBox.shrink();
        final c = ref.read(editorControllerProvider.notifier);
        final t = playback.time.value;
        final local = camada.localTime(t);
        final zoom = camada.zoom.valueAt(local);
        // A lente em "milimetros" na convencao do app inteiro (36 mm de
        // filme na largura da composicao) — e o numero que quem veio de
        // camera reconhece.
        final mm = 36 * zoom / projeto.outputWidth;
        return Padding(
          padding: const EdgeInsets.fromLTRB(18, 12, 18, 18),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              ParameterRow(
                label: 'Lente',
                value: zoom,
                min: 60,
                max: 12000,
                unitsPerPixel: 8,
                decimals: 0,
                valueKey: const ValueKey('camera-lente'),
                keyframe: KeyframeState(
                  animated: camada.zoom.isAnimated,
                  here: camada.zoom.hasKeyframeAt(local),
                  onToggle: () => c.toggleCameraZoomKeyframe(layerId, t),
                ),
                onChanged: (v) => c.editCameraZoom(layerId, t, v),
                onReset: () => c.editCameraZoom(layerId, t, CameraLayer.lenteNeutra),
              ),
              const SizedBox(height: 6),
              AppText(
                '≈ ${mm.toStringAsFixed(0)} mm · 1200 é a lente neutra. '
                'Menos abre o ângulo (grande-angular), mais fecha '
                '(teleobjetiva). Animar a lente junto com a posição é o '
                'dolly-zoom.',
                style: const TextStyle(
                  fontSize: 11.5,
                  height: 1.35,
                  color: AmColors.muted,
                ),
              ),
              const SizedBox(height: 10),
              const AppText(
                'Posição, giro 3D e Z da câmera moram em Mover/Transformar, '
                'como em qualquer camada. Só camadas com o 3D ligado são '
                'vistas pela câmera.',
                style: TextStyle(
                  fontSize: 11.5,
                  height: 1.35,
                  color: AmColors.muted,
                ),
              ),
            ],
          ),
        );
      },
    ),
  );
}
