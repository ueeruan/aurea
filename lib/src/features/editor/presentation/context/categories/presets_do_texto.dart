import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../application/editor_controller.dart';
import '../../../domain/layer.dart';
import '../../../domain/text_animator.dart';
import '../../../domain/text_presets.dart';
import '../../am/am_colors.dart';
import '../../am/text_animators_panel.dart';

/// A ABA PRESETS: pilhas de animadores prontas, com previa viva.
///
/// A lista vem do dominio ([textPresets]) e a aplicacao e a de sempre
/// ([EditorController.applyTextPreset]): esta tela nao tem matematica
/// propria, so mostra. A previa e a MESMA da grade do animador manual
/// ([PreviaDeAnimador]) — escolher olhando, nao lendo o nome.
class PresetsDoTexto extends ConsumerWidget {
  const PresetsDoTexto({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer is! TextLayer || id == null) {
      return ColoredBox(color: AmColors.panel);
    }

    final atual = layer.animators;

    return ColoredBox(
      color: AmColors.panel,
      child: CustomScrollView(
        slivers: [
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(12, 10, 12, 6),
            sliver: SliverToBoxAdapter(
              child: AppText(
                atual.isEmpty
                    ? 'Uma pilha de animadores pronta, do inicio ao fim.'
                    : 'Aplicado: ${atual.map((a) => a.name).join(' + ')}',
                maxLines: 2,
                style: const TextStyle(
                  fontSize: 11.5,
                  height: 1.3,
                  color: AmColors.muted,
                ),
              ),
            ),
          ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(12, 4, 12, 20),
            sliver: SliverGrid(
              gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
                maxCrossAxisExtent: 132,
                mainAxisSpacing: 8,
                crossAxisSpacing: 8,
                childAspectRatio: 0.86,
              ),
              delegate: SliverChildBuilderDelegate(
                (context, i) {
                  if (i == 0) {
                    return _Cartao(
                      label: 'Nenhuma',
                      selecionado: atual.isEmpty,
                      onTap: () => controller.clearTextAnimators(id),
                      previa: const Center(
                        child: Icon(
                          CupertinoIcons.nosign,
                          size: 22,
                          color: AmColors.muted,
                        ),
                      ),
                    );
                  }
                  final preset = textPresets[i - 1];
                  return _Cartao(
                    label: preset.name,
                    selecionado:
                        atual.isNotEmpty &&
                        atual.length == 1 &&
                        atual.first.name == preset.name,
                    onTap: () => controller.applyTextPreset(id, preset),
                    previa: _PreviaDoPreset(preset: preset),
                  );
                },
                childCount: textPresets.length + 1,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// A PILHA E CONSTRUIDA UMA VEZ SO.
///
/// `preset.build()` monta seletores e keyframes do zero — chamado de
/// dentro do `build`, refaria tudo a cada quadro, e sao ate sete presets
/// vivos na tela ao mesmo tempo. Uma vez no `initState` basta: o preset
/// e constante.
class _PreviaDoPreset extends StatefulWidget {
  const _PreviaDoPreset({required this.preset});

  final TextPreset preset;

  @override
  State<_PreviaDoPreset> createState() => _PreviaDoPresetState();
}

class _PreviaDoPresetState extends State<_PreviaDoPreset> {
  late final List<TextAnimator> _pilha = widget.preset.build();

  @override
  Widget build(BuildContext context) =>
      PreviaDeAnimador(animadores: _pilha);
}

class _Cartao extends StatelessWidget {
  const _Cartao({
    required this.label,
    required this.selecionado,
    required this.previa,
    required this.onTap,
  });

  final String label;
  final bool selecionado;
  final Widget previa;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        decoration: BoxDecoration(
          color: selecionado ? AmColors.accentDim : AmColors.chip,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(
          children: [
            Expanded(child: ClipRect(child: previa)),
            Padding(
              padding: const EdgeInsets.only(bottom: 7, left: 4, right: 4),
              child: SizedBox(
                height: 26,
                child: AppText(
                  label,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 10.5,
                    height: 1.15,
                    color: selecionado ? AmColors.accent : AmColors.muted,
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
