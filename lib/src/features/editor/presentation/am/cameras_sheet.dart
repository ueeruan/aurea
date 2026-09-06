import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/utils/time_format.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/camera_cuts.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// VARIAS CAMERAS, COM CORTE.
///
/// Uma camera so obriga a animar a mesma camera de um enquadramento ao
/// outro — e ai todo corte vira um voo. Cinema nao voa entre planos:
/// corta. Aqui cada camera guarda um enquadramento, e a lista de tomadas
/// diz qual esta no ar em cada instante.
Future<void> showCamerasSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  var transicao = 0.0;

  await showParamSheet(
    context,
    title: 'Cameras',
    heightFactor: 0.6,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        if (layer is! Scene3DLayer) return const SizedBox.shrink();

        final local = layer.localTime(playback.time.value);
        final cameras = layer.allCameras;
        final tomadas = sortedShots(layer.shots);
        final noAr = shotAt(layer.shots, local);

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 18),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('Cameras',
                    style: TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text)),
                const SizedBox(height: 4),
                Text(
                  'Playhead em ${formatTime(local)}. Toque numa camera '
                  'para CORTAR para ela aqui.',
                  style: const TextStyle(
                      fontSize: 11, height: 1.35, color: AmColors.muted),
                ),
                const SizedBox(height: 12),

                for (var i = 0; i < cameras.length; i++)
                  _LinhaCamera(
                    nome: cameras[i].name,
                    principal: i == 0,
                    noAr: noAr?.cameraId == cameras[i].id,
                    onCortar: () {
                      controller.setCameraShot(
                        layerId,
                        local,
                        cameras[i].id,
                        transition: Duration(
                            milliseconds: (transicao * 1000).round()),
                      );
                      setSheetState(() {});
                    },
                    onApagar: i == 0
                        ? null
                        : () {
                            controller.removeScene3DCamera(
                                layerId, cameras[i].id);
                            setSheetState(() {});
                          },
                  ),

                const SizedBox(height: 8),
                Row(
                  children: [
                    const SizedBox(
                      width: 74,
                      child: Text('Transicao',
                          style: TextStyle(
                              fontSize: 12, color: AmColors.muted)),
                    ),
                    Expanded(
                      child: AmTickRuler(
  value: transicao,
  min: 0,
  max: 3,
  unitsPerPixel: ((3) - (0)) / 420,
  height: 40,
  onChanged: (v) =>
                            setSheetState(() => transicao = v),
),
                    ),
                    SizedBox(
                      width: 66,
                      child: Text(
                          transicao < 0.05
                              ? 'corte'
                              : '${transicao.toStringAsFixed(1)} s',
                          textAlign: TextAlign.right,
                          style: const TextStyle(
                              fontSize: 11, color: AmColors.text)),
                    ),
                  ],
                ),
                const Text(
                  'Zero e corte seco. Maior que zero derrete de uma '
                  'camera na outra.',
                  style: TextStyle(
                      fontSize: 11, height: 1.35, color: AmColors.muted),
                ),

                const SizedBox(height: 14),
                // A PONTE COM A COMPOSICAO: todo rig de camera e "camera
                // parenteada a um nulo". Sem isto, orbita, tripe, dolly,
                // camera na mao e dolly zoom estao todos quebrados.
                const Text('Seguir um nulo da composicao',
                    style: TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.w600,
                        color: AmColors.text)),
                const SizedBox(height: 6),
                Builder(builder: (context) {
                  final nulos =
                      project.layers.whereType<NullLayer>().toList();
                  if (nulos.isEmpty) {
                    return const Text(
                      'Nao ha objeto nulo no projeto. Crie um e a camera '
                      'pode segui-lo — girar o nulo orbita a cena.',
                      style: TextStyle(
                          fontSize: 11, height: 1.35, color: AmColors.muted),
                    );
                  }
                  return Wrap(
                    spacing: 6,
                    runSpacing: 6,
                    children: [
                      GestureDetector(
                        onTap: () {
                          controller.setSceneCameraCompParent(layerId, null);
                          setSheetState(() {});
                        },
                        child: _Pastilha(
                          rotulo: 'Nenhum',
                          aceso: layer.cameraParentLayerId == null,
                        ),
                      ),
                      for (final n in nulos)
                        GestureDetector(
                          onTap: () {
                            controller.setSceneCameraCompParent(
                                layerId, n.id);
                            setSheetState(() {});
                          },
                          child: _Pastilha(
                            rotulo: n.name,
                            aceso: layer.cameraParentLayerId == n.id,
                          ),
                        ),
                    ],
                  );
                }),
                const SizedBox(height: 4),
                const Text(
                  'A camera herda posicao e rotacao do nulo — nunca a '
                  'escala. Camera nao tem escala.',
                  style: TextStyle(
                      fontSize: 11, height: 1.35, color: AmColors.muted),
                ),

                const SizedBox(height: 14),
                GestureDetector(
                  onTap: () {
                    controller.addScene3DCamera(layerId);
                    setSheetState(() {});
                  },
                  child: Container(
                    padding: const EdgeInsets.symmetric(vertical: 10),
                    alignment: Alignment.center,
                    decoration: BoxDecoration(
                      color: AmColors.accentDim,
                      borderRadius: BorderRadius.circular(10),
                    ),
                    child: const Text('Nova camera (enquadramento atual)',
                        style: TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w600,
                            color: AmColors.accent)),
                  ),
                ),

                if (tomadas.isNotEmpty) ...[
                  const SizedBox(height: 16),
                  const Text('Tomadas',
                      style: TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w600,
                          color: AmColors.text)),
                  const SizedBox(height: 6),
                  for (final t in tomadas)
                    _LinhaTomada(
                      tempo: formatTime(t.time),
                      camera: cameras
                          .where((c) => c.id == t.cameraId)
                          .map((c) => c.name)
                          .firstOrNull,
                      transicao: t.isCut
                          ? 'corte'
                          : '${(t.transition.inMilliseconds / 1000).toStringAsFixed(1)} s',
                      onIr: () => playback
                          .seek(layer.startTime + t.time),
                      onApagar: () {
                        controller.removeCameraShot(layerId, t.time);
                        setSheetState(() {});
                      },
                    ),
                  const SizedBox(height: 8),
                  GestureDetector(
                    onTap: () {
                      controller.clearCameraShots(layerId);
                      setSheetState(() {});
                      AureaSnack.show(sheetContext, 'Tomadas removidas',
                          actionLabel: 'Desfazer',
                          onAction: controller.undo);
                    },
                    child: const Text('Limpar tomadas',
                        style: TextStyle(
                            fontSize: 12, color: AmColors.pink)),
                  ),
                ],
              ],
            ),
          ),
        );
      },
    ),
  );
}

class _Pastilha extends StatelessWidget {
  const _Pastilha({required this.rotulo, required this.aceso});

  final String rotulo;
  final bool aceso;

  @override
  Widget build(BuildContext context) => Container(
        padding:
            const EdgeInsets.symmetric(horizontal: 11, vertical: 7),
        decoration: BoxDecoration(
          color: aceso ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(8),
        ),
        child: Text(rotulo,
            style: TextStyle(
                fontSize: 11,
                color: aceso ? AmColors.accent : AmColors.muted)),
      );
}

class _LinhaCamera extends StatelessWidget {
  const _LinhaCamera({
    required this.nome,
    required this.principal,
    required this.noAr,
    required this.onCortar,
    required this.onApagar,
  });

  final String nome;
  final bool principal;
  final bool noAr;
  final VoidCallback onCortar;
  final VoidCallback? onApagar;

  @override
  Widget build(BuildContext context) => GestureDetector(
        onTap: onCortar,
        behavior: HitTestBehavior.opaque,
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 9),
          child: Row(
            children: [
              Icon(
                noAr
                    ? CupertinoIcons.videocam_fill
                    : CupertinoIcons.videocam,
                size: 18,
                color: noAr ? AmColors.accent : AmColors.muted,
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  principal ? '$nome (principal)' : nome,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                      fontSize: 13,
                      color: noAr ? AmColors.accent : AmColors.text),
                ),
              ),
              if (onApagar != null)
                GestureDetector(
                  onTap: onApagar,
                  child: const Icon(CupertinoIcons.trash,
                      size: 15, color: AmColors.muted),
                ),
            ],
          ),
        ),
      );
}

class _LinhaTomada extends StatelessWidget {
  const _LinhaTomada({
    required this.tempo,
    required this.camera,
    required this.transicao,
    required this.onIr,
    required this.onApagar,
  });

  final String tempo;
  final String? camera;
  final String transicao;
  final VoidCallback onIr;
  final VoidCallback onApagar;

  @override
  Widget build(BuildContext context) => GestureDetector(
        onTap: onIr,
        behavior: HitTestBehavior.opaque,
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 7),
          child: Row(
            children: [
              SizedBox(
                width: 74,
                child: Text(tempo,
                    style: const TextStyle(
                        fontSize: 12, color: AmColors.accent)),
              ),
              Expanded(
                child: Text(camera ?? 'camera apagada',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                        fontSize: 12, color: AmColors.text)),
              ),
              Text(transicao,
                  style: const TextStyle(
                      fontSize: 11, color: AmColors.muted)),
              const SizedBox(width: 10),
              GestureDetector(
                onTap: onApagar,
                child: const Icon(CupertinoIcons.xmark,
                    size: 14, color: AmColors.muted),
              ),
            ],
          ),
        ),
      );
}
