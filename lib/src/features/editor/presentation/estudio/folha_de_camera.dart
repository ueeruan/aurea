import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/layer.dart';
import 'estado_do_estudio.dart';
import 'folhas_do_estudio.dart';
import 'scene3d_theme.dart';
import '../widgets/campo_de_valor.dart';

/// Abre a folha modal de Câmera (Tela 6 do mockup).
Future<void> abrirFolhaDeCameraNova(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required Duration tempo,
}) => mostrarFolhaScene3D<void>(
  context,
  title: translate(context, 'Câmera'),
  body: FolhaDeCamera(layerId: layerId, tempo: tempo),
);

class FolhaDeCamera extends ConsumerStatefulWidget {
  const FolhaDeCamera({super.key, required this.layerId, required this.tempo});

  final String layerId;
  final Duration tempo;

  @override
  ConsumerState<FolhaDeCamera> createState() => _FolhaDeCameraState();
}

class _FolhaDeCameraState extends ConsumerState<FolhaDeCamera> {
  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(projetoVisivelProvider);
    final bruta = projeto.layerById(widget.layerId);
    if (bruta is! Scene3DLayer) return const SizedBox.shrink();
    final camada = bruta;
    final c = ref.read(editorControllerProvider.notifier);
    final local = camada.localTime(widget.tempo);
    final selectedId = ref.watch(cameraSelecionadaProvider);
    final cam =
        camada.allCameras.where((c) => c.id == selectedId).firstOrNull ??
        cameraNoAr(camada, local);
    void edit(PropDaCamera p, double v) =>
        c.editSceneCameraProp(widget.layerId, cam.id, p, widget.tempo, v);

    final fov = cam.fovAt(local);

    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // Posição: X, Y, Z com reset
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: _buildVectorRow(
            label: translate(context, 'Posição'),
            x: cam.posX.valueAt(local),
            y: cam.posY.valueAt(local),
            z: cam.posZ.valueAt(local),
            onReset: () {
              c.runAsOneUndo(() {
                edit(PropDaCamera.posX, 0);
                edit(PropDaCamera.posY, 0);
                edit(PropDaCamera.posZ, 800);
              });
            },
            onChangedX: (v) => edit(PropDaCamera.posX, v),
            onChangedY: (v) => edit(PropDaCamera.posY, v),
            onChangedZ: (v) => edit(PropDaCamera.posZ, v),
          ),
        ),
        const SizedBox(height: 12),

        // Alvo (Target / LookAt): X, Y, Z com reset
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: _buildVectorRow(
            label: 'Alvo',
            x: cam.poiX.valueAt(local),
            y: cam.poiY.valueAt(local),
            z: cam.poiZ.valueAt(local),
            onReset: () {
              c.runAsOneUndo(() {
                edit(PropDaCamera.alvoX, 0);
                edit(PropDaCamera.alvoY, 0);
                edit(PropDaCamera.alvoZ, 0);
              });
            },
            onChangedX: (v) => edit(PropDaCamera.alvoX, v),
            onChangedY: (v) => edit(PropDaCamera.alvoY, v),
            onChangedZ: (v) => edit(PropDaCamera.alvoZ, v),
          ),
        ),
        const SizedBox(height: 16),

        // Campo de visão (FOV)
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Expanded(
                    child: AppText('Campo de visão (FOV)',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w600,
                        color: Scene3DTheme.text,
                      ),
                    ),
                  ),
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 8,
                      vertical: 3,
                    ),
                    decoration: BoxDecoration(
                      color: Scene3DTheme.panelElevated,
                      borderRadius: BorderRadius.circular(6),
                      border: Border.all(color: Scene3DTheme.border),
                    ),
                    child: AppText(
                      '${fov.round()}°',
                      style: const TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                        color: Scene3DTheme.accent,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              SliderTheme(
                data: SliderTheme.of(context).copyWith(
                  activeTrackColor: Scene3DTheme.accent,
                  inactiveTrackColor: Scene3DTheme.border,
                  thumbColor: Scene3DTheme.accent,
                  overlayColor: Scene3DTheme.accent.withValues(alpha: 0.2),
                  trackHeight: 3,
                ),
                child: Slider(
                  value: fov.clamp(10.0, 120.0),
                  min: 10.0,
                  max: 120.0,
                  onChanged: (v) {
                    edit(
                      PropDaCamera.lente,
                      cam.filmWidth / (2 * math.tan(v * math.pi / 360)),
                    );
                  },
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 18),

        // Botão Inferior: Redefinir
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Scene3DActionButton(
            label: 'Redefinir',
            icon: Icons.refresh_rounded,
            outlined: true,
            onPressed: () {
              c.runAsOneUndo(() {
                edit(PropDaCamera.posX, 0);
                edit(PropDaCamera.posY, 0);
                edit(PropDaCamera.posZ, 800);
                edit(PropDaCamera.alvoX, 0);
                edit(PropDaCamera.alvoY, 0);
                edit(PropDaCamera.alvoZ, 0);
                edit(
                  PropDaCamera.lente,
                  cam.filmWidth / (2 * math.tan(45 * math.pi / 360)),
                );
              });
            },
          ),
        ),
        const SizedBox(height: 10),
      ],
    );
  }

  Widget _buildVectorRow({
    required String label,
    required double x,
    required double y,
    required double z,
    required VoidCallback onReset,
    required ValueChanged<double> onChangedX,
    required ValueChanged<double> onChangedY,
    required ValueChanged<double> onChangedZ,
  }) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            AppText(
              label,
              style: const TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: Scene3DTheme.text,
              ),
            ),
            GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: onReset,
              child: const Icon(
                Icons.sync_rounded,
                color: Scene3DTheme.textMuted,
                size: 18,
              ),
            ),
          ],
        ),
        const SizedBox(height: 6),
        Row(
          children: [
            Expanded(child: _buildNumberInput('X', x, onChangedX)),
            const SizedBox(width: 8),
            Expanded(child: _buildNumberInput('Y', y, onChangedY)),
            const SizedBox(width: 8),
            Expanded(child: _buildNumberInput('Z', z, onChangedZ)),
          ],
        ),
      ],
    );
  }

  Widget _buildNumberInput(
    String axis,
    double val,
    ValueChanged<double> onChanged,
  ) => CampoDeValor(
    rotulo: axis,
    valor: val,
    casas: 2,
    largura: double.infinity,
    aoDigitar: onChanged,
    cor: Scene3DTheme.text,
  );
}
