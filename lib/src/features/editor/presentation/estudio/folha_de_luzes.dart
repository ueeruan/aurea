import 'package:aurea/src/core/l10n/app_language.dart';
import '../../domain/panorama3d.dart';

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import '../am/color_picker_sheet.dart';
import 'folhas_do_estudio.dart';
import 'scene3d_theme.dart';
import 'estado_do_estudio.dart';
import 'ficha_do_selecionado.dart';

/// Abre a folha modal de Luzes (Tela 7 do mockup).
Future<void> abrirFolhaDeLuzes(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  Duration tempo = Duration.zero,
}) => mostrarFolhaScene3D<void>(
  context,
  title: 'Luzes',
  body: FolhaDeLuzes(layerId: layerId, tempo: tempo),
);

class FolhaDeLuzes extends ConsumerStatefulWidget {
  const FolhaDeLuzes({
    super.key,
    required this.layerId,
    this.tempo = Duration.zero,
  });

  final String layerId;
  final Duration tempo;

  @override
  ConsumerState<FolhaDeLuzes> createState() => _FolhaDeLuzesState();
}

class _FolhaDeLuzesState extends ConsumerState<FolhaDeLuzes> {
  int _luzSelecionadaIndex = 0;

  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(projetoVisivelProvider);
    final bruta = projeto.layerById(widget.layerId);
    if (bruta is! Scene3DLayer) return const SizedBox.shrink();
    final camada = bruta;
    final c = ref.read(editorControllerProvider.notifier);
    final lights = camada.scene.lights;
    final local = camada.localTime(widget.tempo);

    final activeLight = lights.isNotEmpty
        ? lights[_luzSelecionadaIndex.clamp(0, lights.length - 1)]
        : null;

    final double intensidade =
        activeLight?.intensity.valueAt(local) ?? camada.scene.ambient;
    final cor = activeLight?.color ?? Colors.white;

    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              if (activeLight != null)
                DropdownButton<String>(
                  key: const ValueKey('scene-light-selection'),
                  isExpanded: true,
                  value: activeLight.id,
                  items: [
                    for (var i = 0; i < lights.length; i++)
                      DropdownMenuItem(
                        value: lights[i].id,
                        child: AppText('${_lightName(lights[i].kind)} ${i + 1}'),
                      ),
                  ],
                  onChanged: (id) {
                    final index = lights.indexWhere((l) => l.id == id);
                    if (index >= 0) {
                      setState(() => _luzSelecionadaIndex = index);
                    }
                  },
                ),
              Wrap(
                spacing: 8,
                children: [
                  for (final kind in Light3DKind.values)
                    TextButton.icon(
                      icon: const Icon(Icons.add, size: 16),
                      label: AppText(_lightName(kind)),
                      onPressed: () {
                        c.addSceneLight(widget.layerId, kind);
                        setState(() => _luzSelecionadaIndex = lights.length);
                      },
                    ),
                ],
              ),
              SwitchListTile.adaptive(
                contentPadding: EdgeInsets.zero,
                title: const AppText('Reflexos da cena'),
                subtitle: const AppText('Captura real dos objetos ao redor; reutilizada durante a reprodução.',
                ),
                value: camada.scene.reflectionProbe.enabled,
                onChanged: (v) => c.setSceneReflectionProbe(
                  widget.layerId,
                  camada.scene.reflectionProbe.copyWith(
                    enabled: v,
                    updateMode: ProbeUpdateMode.stopped,
                    position:
                        camada.scene.reflectionProbe.position ==
                            ProbePoint3D.zero
                        ? const ProbePoint3D(0, 300, 300)
                        : null,
                  ),
                ),
              ),
              if (camada.scene.reflectionProbe.enabled) ...[
                TextButton.icon(
                  icon: const Icon(Icons.refresh),
                  label: const AppText('Atualizar reflexos'),
                  onPressed: () => c.setSceneReflectionProbe(
                    widget.layerId,
                    camada.scene.reflectionProbe.copyWith(),
                  ),
                ),
                const AppText('Força dos reflexos'),
                Slider(
                  value: camada.scene.envReflect.clamp(0.0, 1.0),
                  onChanged: (v) => c.setSceneEnvReflect(widget.layerId, v),
                ),
              ],
              const AppText('Iluminação ambiente da cena'),
              Slider(
                key: const ValueKey('scene-ambient-intensity'),
                value: camada.scene.ambient.clamp(0.0, 1.0),
                onChanged: (v) => c.setSceneAmbient(widget.layerId, v),
              ),
              if (activeLight != null &&
                  activeLight.kind != Light3DKind.ambient)
                SwitchListTile.adaptive(
                  contentPadding: EdgeInsets.zero,
                  title: const AppText('Projetar sombras'),
                  value: activeLight.castsShadow,
                  onChanged: (v) =>
                      c.setSceneLightShadow(widget.layerId, activeLight.id, v),
                ),
              if (activeLight != null)
                TextButton.icon(
                  icon: const Icon(Icons.open_with),
                  label: const AppText('Posição, direção e alcance'),
                  onPressed: () {
                    ref.read(noSelecionadoProvider.notifier).state = null;
                    ref.read(cameraSelecionadaProvider.notifier).state = null;
                    ref.read(luzSelecionadaProvider.notifier).state =
                        activeLight.id;
                    abrirFichaDoSelecionado(
                      context,
                      ref,
                      layerId: widget.layerId,
                      tempo: widget.tempo,
                    );
                  },
                ),
              if (activeLight == null)
                const AppText('Adicione uma luz para ajustar sua cor e intensidade.',
                ),
            ],
          ),
        ),
        const SizedBox(height: 18),

        // Propriedades da Luz Selecionada
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Cor
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const AppText(
                    'Cor',
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w600,
                      color: Scene3DTheme.text,
                    ),
                  ),
                  GestureDetector(
                    behavior: HitTestBehavior.opaque,
                    onTap: () {
                      if (activeLight != null) {
                        showColorPicker(
                          context,
                          initial: cor,
                          withAlpha: false,
                          onChanged: (newCor) {
                            c.updateSceneLight(
                              widget.layerId,
                              activeLight.id,
                              (l) => l.copyWith(color: newCor),
                            );
                          },
                        );
                      }
                    },
                    child: Container(
                      width: 32,
                      height: 32,
                      decoration: BoxDecoration(
                        color: cor,
                        borderRadius: BorderRadius.circular(8),
                        border: Border.all(
                          color: Scene3DTheme.border,
                          width: 1.5,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 14),

              // Intensidade
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const AppText(
                    'Intensidade',
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w600,
                      color: Scene3DTheme.text,
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
                      intensidade.toStringAsFixed(1),
                      style: const TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                        color: Scene3DTheme.accent,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 6),
              SliderTheme(
                data: SliderTheme.of(context).copyWith(
                  activeTrackColor: Scene3DTheme.accent,
                  inactiveTrackColor: Scene3DTheme.border,
                  thumbColor: Scene3DTheme.accent,
                  overlayColor: Scene3DTheme.accent.withValues(alpha: 0.2),
                  trackHeight: 3,
                ),
                child: Slider(
                  value: intensidade.clamp(0.0, 3.0),
                  min: 0.0,
                  max: 3.0,
                  onChanged: (v) {
                    if (activeLight != null) {
                      c.editSceneLightProp(
                        widget.layerId,
                        activeLight.id,
                        PropDaLuz.intensidade,
                        widget.tempo,
                        v,
                      );
                    } else {
                      c.setSceneAmbient(widget.layerId, v);
                    }
                  },
                ),
              ),
              const SizedBox(height: 10),

              // Ângulo
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const AppText('Ângulo',
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w600,
                      color: Scene3DTheme.text,
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
                      '${activeLight?.coneDegrees.round() ?? 45}°',
                      style: TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w700,
                        color: Scene3DTheme.accent,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 6),
              SliderTheme(
                data: SliderTheme.of(context).copyWith(
                  activeTrackColor: Scene3DTheme.accent,
                  inactiveTrackColor: Scene3DTheme.border,
                  thumbColor: Scene3DTheme.accent,
                  overlayColor: Scene3DTheme.accent.withValues(alpha: 0.2),
                  trackHeight: 3,
                ),
                child: Slider(
                  value: (activeLight?.coneDegrees ?? 45).clamp(1.0, 90.0),
                  min: 1.0,
                  max: 90.0,
                  onChanged: activeLight?.kind != Light3DKind.spot
                      ? null
                      : (v) => c.updateSceneLight(
                          widget.layerId,
                          activeLight!.id,
                          (l) => l.copyWith(coneDegrees: v),
                        ),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 18),

        // Botão de Ação Inferior: Redefinir
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Scene3DActionButton(
            label: 'Redefinir',
            icon: Icons.refresh_rounded,
            outlined: true,
            onPressed: () {
              if (activeLight != null) {
                c.updateSceneLight(
                  widget.layerId,
                  activeLight.id,
                  (l) => l.copyWith(color: Colors.white),
                );
              }
              if (activeLight != null) {
                c.editSceneLightProp(
                  widget.layerId,
                  activeLight.id,
                  PropDaLuz.intensidade,
                  widget.tempo,
                  1,
                );
              }
            },
          ),
        ),
        const SizedBox(height: 10),
      ],
    );
  }

  String _lightName(Light3DKind kind) => switch (kind) {
    Light3DKind.directional => 'Direcional',
    Light3DKind.point => 'Pontual',
    Light3DKind.ambient => 'Ambiente',
    Light3DKind.spot => 'Spot',
  };
}
