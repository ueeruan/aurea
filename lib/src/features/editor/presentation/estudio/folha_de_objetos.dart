import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../domain/layer.dart';
import '../../domain/scene3d.dart';
import 'estado_do_estudio.dart';
import 'ficha_do_selecionado.dart';
import 'folhas_do_estudio.dart';
import 'scene3d_theme.dart';

/// Abre a folha modal de Objetos (Hierarquia / Outliner - Tela 2 do mockup).
Future<void> abrirFolhaDeObjetos(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required Duration tempo,
}) => mostrarFolhaScene3D<void>(
  context,
  title: 'Objetos',
  body: FolhaDeObjetos(layerId: layerId, tempo: tempo),
);

class FolhaDeObjetos extends ConsumerStatefulWidget {
  const FolhaDeObjetos({super.key, required this.layerId, required this.tempo});

  final String layerId;
  final Duration tempo;

  @override
  ConsumerState<FolhaDeObjetos> createState() => _FolhaDeObjetosState();
}

enum _FiltroObjetos { todos, modelos, luzes, cameras }

class _FolhaDeObjetosState extends ConsumerState<FolhaDeObjetos> {
  _FiltroObjetos _filtro = _FiltroObjetos.todos;

  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(projetoVisivelProvider);
    final bruta = projeto.layerById(widget.layerId);
    if (bruta is! Scene3DLayer) return const SizedBox.shrink();
    final camada = bruta;
    final c = ref.read(editorControllerProvider.notifier);

    final noSelecionado = ref.watch(noSelecionadoProvider);
    final luzSelecionada = ref.watch(luzSelecionadaProvider);
    final cameraSelecionada = ref.watch(cameraSelecionadaProvider);

    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // Chips de Filtro: Todos, Modelos, Luzes, Câmeras
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Row(
            children: [
              _buildFilterChip('Todos', _FiltroObjetos.todos),
              const SizedBox(width: 8),
              _buildFilterChip('Modelos', _FiltroObjetos.modelos),
              const SizedBox(width: 8),
              _buildFilterChip('Luzes', _FiltroObjetos.luzes),
              const SizedBox(width: 8),
              _buildFilterChip('Câmeras', _FiltroObjetos.cameras),
            ],
          ),
        ),
        const SizedBox(height: 14),

        // Lista de Elementos da Cena
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Modelos e Nós 3D
              if (_filtro == _FiltroObjetos.todos ||
                  _filtro == _FiltroObjetos.modelos) ...[
                for (final n in camada.scene.nodes)
                  _buildNodeItem(
                    node: n,
                    isSelected: n.id == noSelecionado,
                    onSelect: () {
                      ref.read(noSelecionadoProvider.notifier).state = n.id;
                      ref.read(luzSelecionadaProvider.notifier).state = null;
                      ref.read(cameraSelecionadaProvider.notifier).state = null;
                      Navigator.of(context).pop();
                      abrirFichaDoSelecionado(
                        context,
                        ref,
                        layerId: widget.layerId,
                        tempo: widget.tempo,
                      );
                    },
                    onToggleVisibility: () {
                      c.setSceneNodeVisible(widget.layerId, n.id, !n.visible);
                    },
                  ),
              ],

              // Luzes
              if (_filtro == _FiltroObjetos.todos ||
                  _filtro == _FiltroObjetos.luzes) ...[
                for (final l in camada.scene.lights)
                  _buildGenericRow(
                    icon: switch (l.kind) {
                      Light3DKind.directional => Icons.wb_sunny_rounded,
                      Light3DKind.point => Icons.lightbulb_rounded,
                      Light3DKind.ambient => Icons.public_rounded,
                      Light3DKind.spot => Icons.highlight_rounded,
                    },
                    name: switch (l.kind) {
                      Light3DKind.directional => 'Luz Direcional',
                      Light3DKind.point => 'Luz Pontual',
                      Light3DKind.ambient => 'Luz Ambiente',
                      Light3DKind.spot => 'Luz Spot',
                    },
                    isSelected: l.id == luzSelecionada,
                    onSelect: () {
                      ref.read(luzSelecionadaProvider.notifier).state = l.id;
                      ref.read(noSelecionadoProvider.notifier).state = null;
                      ref.read(cameraSelecionadaProvider.notifier).state = null;
                      Navigator.of(context).pop();
                      abrirFichaDoSelecionado(
                        context,
                        ref,
                        layerId: widget.layerId,
                        tempo: widget.tempo,
                      );
                    },
                    visible: l.intensity.base > 0,
                    onToggleVisibility: () {
                      c.updateSceneLight(
                        widget.layerId,
                        l.id,
                        (light) => light.copyWith(
                          intensity: light.intensity.withBase(
                            light.intensity.base > 0 ? 0.0 : 1.0,
                          ),
                        ),
                      );
                    },
                  ),
              ],

              // Câmeras
              if (_filtro == _FiltroObjetos.todos ||
                  _filtro == _FiltroObjetos.cameras) ...[
                for (final cam in camada.allCameras)
                  _buildGenericRow(
                    icon: Icons.videocam_rounded,
                    name: cam.name,
                    isSelected: cam.id == cameraSelecionada,
                    onSelect: () {
                      ref.read(cameraSelecionadaProvider.notifier).state =
                          cam.id;
                      ref.read(noSelecionadoProvider.notifier).state = null;
                      ref.read(luzSelecionadaProvider.notifier).state = null;
                      Navigator.of(context).pop();
                      abrirFichaDoSelecionado(
                        context,
                        ref,
                        layerId: widget.layerId,
                        tempo: widget.tempo,
                      );
                    },
                    visible:
                        cameraNoAr(camada, camada.localTime(widget.tempo)).id ==
                        cam.id,
                    visibilityIcon: Icons.videocam_rounded,
                    onToggleVisibility: () =>
                        c.setCameraShot(widget.layerId, widget.tempo, cam.id),
                  ),
              ],
            ],
          ),
        ),
        const SizedBox(height: 20),

        // Botão Inferior de Ação: + Adicionar Objeto
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Scene3DActionButton(
            label: 'Adicionar Objeto',
            icon: Icons.add_rounded,
            onPressed: () {
              Navigator.of(context).pop();
              abrirFolhaDeAdicionar(
                context,
                ref,
                layerId: widget.layerId,
                tempo: widget.tempo,
                navegacao: null,
                aoAvisar: (_) {},
              );
            },
          ),
        ),
        const SizedBox(height: 10),
      ],
    );
  }

  Widget _buildFilterChip(String label, _FiltroObjetos filtro) {
    final active = _filtro == filtro;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: () => setState(() => _filtro = filtro),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 150),
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
        decoration: Scene3DTheme.pillDecoration(active: active),
        child: AppText(
          label,
          style: TextStyle(
            fontSize: 13,
            fontWeight: FontWeight.w600,
            color: active ? Scene3DTheme.onAccent : Scene3DTheme.textMuted,
          ),
        ),
      ),
    );
  }

  Widget _buildNodeItem({
    required SceneNode node,
    required bool isSelected,
    required VoidCallback onSelect,
    required VoidCallback onToggleVisibility,
  }) => _buildGenericRow(
    icon: node.modelAsset != null
        ? Icons.view_in_ar_rounded
        : Icons.category_rounded,
    name: node.name,
    isSelected: isSelected,
    onSelect: onSelect,
    visible: node.visible,
    onToggleVisibility: onToggleVisibility,
  );

  Widget _buildGenericRow({
    required IconData icon,
    required String name,
    required bool isSelected,
    required VoidCallback onSelect,
    required bool visible,
    required VoidCallback onToggleVisibility,
    IconData? visibilityIcon,
    bool hasChildren = false,
    bool isExpanded = false,
    VoidCallback? onToggleExpand,
  }) {
    return Container(
      height: 48,
      margin: const EdgeInsets.only(bottom: 6),
      decoration: Scene3DTheme.cardDecoration(
        borderRadius: 12,
        isSelected: isSelected,
        color: isSelected
            ? const Color(0xFF192622)
            : Scene3DTheme.panelElevated,
      ),
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: onSelect,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: Row(
            children: [
              if (hasChildren)
                GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: onToggleExpand,
                  child: Padding(
                    padding: const EdgeInsets.only(right: 6),
                    child: Icon(
                      isExpanded
                          ? Icons.keyboard_arrow_down_rounded
                          : Icons.keyboard_arrow_right_rounded,
                      color: Scene3DTheme.textMuted,
                      size: 20,
                    ),
                  ),
                )
              else
                const SizedBox(width: 6),
              Icon(
                icon,
                size: 20,
                color: isSelected
                    ? Scene3DTheme.accent
                    : Scene3DTheme.textMuted,
              ),
              const SizedBox(width: 10),
              Expanded(
                child: AppText(
                  name,
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: isSelected ? FontWeight.w700 : FontWeight.w500,
                    color: isSelected ? Scene3DTheme.accent : Scene3DTheme.text,
                  ),
                ),
              ),
              GestureDetector(
                behavior: HitTestBehavior.opaque,
                onTap: onToggleVisibility,
                child: Padding(
                  padding: const EdgeInsets.all(6),
                  child: Icon(
                    visibilityIcon ??
                        (visible
                            ? Icons.visibility_rounded
                            : Icons.visibility_off_rounded),
                    color: visible
                        ? Scene3DTheme.textMuted
                        : Scene3DTheme.textSubtle,
                    size: 19,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
