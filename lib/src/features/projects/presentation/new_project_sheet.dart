import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_theme.dart';
import '../../editor/domain/video_project.dart';
import '../../settings/application/settings_controller.dart';
import '../domain/project_presets.dart';

/// Abre o sheet de criacao e devolve o projeto configurado (ou null).
Future<VideoProject?> showNewProjectSheet(
  BuildContext context, {
  String? presetAspectKey,
}) {
  return showModalBottomSheet<VideoProject>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AppColors.surface,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
    ),
    builder: (_) => _NewProjectSheet(presetAspectKey: presetAspectKey),
  );
}

class _NewProjectSheet extends ConsumerStatefulWidget {
  const _NewProjectSheet({this.presetAspectKey});

  final String? presetAspectKey;

  @override
  ConsumerState<_NewProjectSheet> createState() => _NewProjectSheetState();
}

class _NewProjectSheetState extends ConsumerState<_NewProjectSheet> {
  late final TextEditingController _nameController;
  late String _aspectKey;
  late int _fps;
  late int _resolution;

  @override
  void initState() {
    super.initState();
    final settings = ref.read(settingsControllerProvider);
    _nameController = TextEditingController();
    _aspectKey = widget.presetAspectKey ?? settings.defaultAspectKey;
    _fps = settings.defaultFps;
    _resolution = settings.defaultResolution;
  }

  @override
  void dispose() {
    _nameController.dispose();
    super.dispose();
  }

  void _create() {
    final name = _nameController.text.trim();
    final project = VideoProject.empty(
      name.isEmpty ? 'Projeto sem titulo' : name,
      aspectRatio: ProjectPresets.aspectByKey(_aspectKey).ratio,
      fps: _fps,
      resolutionHeight: _resolution,
    );
    Navigator.of(context).pop(project);
  }

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.of(context).viewInsets.bottom;

    return Padding(
      padding: EdgeInsets.fromLTRB(20, 12, 20, 24 + bottomInset),
      child: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Center(
              child: Container(
                width: 36,
                height: 5,
                decoration: BoxDecoration(
                  color: Colors.white.withValues(alpha: 0.18),
                  borderRadius: BorderRadius.circular(3),
                ),
              ),
            ),
            const SizedBox(height: 18),
            Text('Novo projeto',
                style: Theme.of(context).textTheme.titleLarge),
            const SizedBox(height: 16),
            CupertinoTextField(
              controller: _nameController,
              autofocus: true,
              placeholder: 'Nome do projeto',
              textCapitalization: TextCapitalization.sentences,
              style: const TextStyle(
                fontSize: 17,
                letterSpacing: -0.2,
                color: AppColors.onDark,
              ),
              placeholderStyle: const TextStyle(
                fontSize: 17,
                letterSpacing: -0.2,
                color: AppColors.muted,
              ),
              padding:
                  const EdgeInsets.symmetric(horizontal: 14, vertical: 13),
              decoration: BoxDecoration(
                color: AppColors.surfaceHigh,
                borderRadius: BorderRadius.circular(12),
              ),
              onSubmitted: (_) => _create(),
            ),
            const SizedBox(height: 22),
            const _SectionLabel('Proporcao'),
            const SizedBox(height: 10),
            Row(
              children: [
                for (final aspect in ProjectPresets.aspects) ...[
                  Expanded(
                    child: _AspectCard(
                      option: aspect,
                      selected: aspect.key == _aspectKey,
                      onTap: () => setState(() => _aspectKey = aspect.key),
                    ),
                  ),
                  if (aspect != ProjectPresets.aspects.last)
                    const SizedBox(width: 8),
                ],
              ],
            ),
            const SizedBox(height: 22),
            const _SectionLabel('Resolucao'),
            const SizedBox(height: 10),
            _Segmented<int>(
              values: ProjectPresets.resolutions,
              selected: _resolution,
              labelOf: ProjectPresets.resolutionLabel,
              onChanged: (v) => setState(() => _resolution = v),
            ),
            const SizedBox(height: 22),
            const _SectionLabel('Quadros por segundo'),
            const SizedBox(height: 10),
            _Segmented<int>(
              values: ProjectPresets.fpsOptions,
              selected: _fps,
              labelOf: (v) => '$v fps',
              onChanged: (v) => setState(() => _fps = v),
            ),
            const SizedBox(height: 26),
            SizedBox(
              width: double.infinity,
              child: FilledButton(
                onPressed: _create,
                child: const Text('Criar projeto'),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text.toUpperCase(),
      style: const TextStyle(
        fontSize: 12,
        letterSpacing: 0.6,
        fontWeight: FontWeight.w500,
        color: AppColors.muted,
      ),
    );
  }
}

class _AspectCard extends StatelessWidget {
  const _AspectCard({
    required this.option,
    required this.selected,
    required this.onTap,
  });

  final AspectOption option;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 180),
        curve: Curves.easeOut,
        padding: const EdgeInsets.symmetric(vertical: 11),
        decoration: BoxDecoration(
          color: selected
              ? AppColors.lime.withValues(alpha: 0.16)
              : AppColors.surfaceHigh,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Column(
          children: [
            Icon(option.icon,
                size: 20,
                color: selected ? AppColors.lime : AppColors.muted),
            const SizedBox(height: 5),
            Text(
              option.label,
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                letterSpacing: -0.1,
                color: selected ? AppColors.lime : AppColors.onDark,
              ),
            ),
            Text(
              option.hint,
              style: const TextStyle(fontSize: 9, color: AppColors.muted),
            ),
          ],
        ),
      ),
    );
  }
}

class _Segmented<T extends Object> extends StatelessWidget {
  const _Segmented({
    required this.values,
    required this.selected,
    required this.labelOf,
    required this.onChanged,
  });

  final List<T> values;
  final T selected;
  final String Function(T) labelOf;
  final ValueChanged<T> onChanged;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      child: CupertinoSlidingSegmentedControl<T>(
        groupValue: selected,
        backgroundColor: AppColors.surfaceHigh,
        thumbColor: AppColors.background,
        onValueChanged: (v) {
          if (v != null) onChanged(v);
        },
        children: {
          for (final v in values)
            v: Padding(
              padding: const EdgeInsets.symmetric(vertical: 7),
              child: Text(
                labelOf(v),
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  letterSpacing: -0.1,
                  color: v == selected ? AppColors.lime : AppColors.onDark,
                ),
              ),
            ),
        },
      ),
    );
  }
}
