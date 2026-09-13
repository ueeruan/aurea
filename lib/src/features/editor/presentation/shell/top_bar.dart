import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/theme/tokens.dart';
import '../../application/editor_controller.dart';
import '../am/export_sheet.dart';
import 'layer_actions.dart';
import 'project_settings_sheet.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// Cabeçalho da referência: voltar, nome e ações do contexto.
class EditorTopBar extends ConsumerWidget {
  const EditorTopBar({
    super.key,
    required this.onBack,
    required this.backLabel,
    this.title,
  });
  final VoidCallback onBack;
  final String backLabel;
  final String? title;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = AureaTokens.of(context);
    final project = ref.watch(editorControllerProvider);
    final selected = ref.watch(selectedLayerProvider);
    final layer = selected == null ? null : project.layerById(selected);
    Widget button(
      String key,
      IconData icon,
      String label,
      VoidCallback onTap,
    ) => IconButton(
      key: ValueKey(key),
      tooltip: label,
      onPressed: onTap,
      icon: Icon(icon, size: 22, color: t.text),
    );
    return Container(
      height: AureaTokens.topBar,
      color: t.surface,
      child: Row(
        children: [
          button('editor-back', CupertinoIcons.chevron_back, backLabel, onBack),
          Expanded(
            child: GestureDetector(
              key: const ValueKey('editor-project-name'),
              behavior: HitTestBehavior.opaque,
              onTap: () => layer == null
                  ? renomearProjeto(context, ref)
                  : renomearCamada(context, ref, layer),
              child: AppText(
                title ?? layer?.name ?? project.name,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: t.text,
                  fontSize: 16,
                  fontWeight: FontWeight.w500,
                ),
              ),
            ),
          ),
          if (title != 'Curva de gradação') ...[
            if (layer != null && title == null)
              button(
                'camada-excluir',
                CupertinoIcons.trash,
                'Excluir camada',
                () => excluirCamadas(context, ref, {layer.id}),
              ),
            button(
              'editor-settings',
              CupertinoIcons.gear,
              'Projeto',
              () => showProjectSettingsSheet(context, ref),
            ),
            Container(
              width: 32,
              height: 32,
              margin: const EdgeInsets.only(right: 12, left: 4),
              decoration: BoxDecoration(
                color: const Color(0xFF1ED6B1),
                borderRadius: BorderRadius.circular(8),
              ),
              child: IconButton(
                key: const ValueKey('editor-export'),
                tooltip: 'Exportar',
                padding: EdgeInsets.zero,
                icon: const Icon(
                  CupertinoIcons.arrow_up,
                  size: 18,
                  color: Color(0xFF12151A),
                ),
                onPressed: () => showExportSheet(context, ref),
              ),
            ),
          ],
        ],
      ),
    );
  }
}
