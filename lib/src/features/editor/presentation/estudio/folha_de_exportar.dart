import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../export/presentation/export_video_screen.dart';
import '../../../export/domain/export_settings.dart';
import '../../application/editor_controller.dart';
import '../../domain/layer.dart';
import 'folhas_do_estudio.dart';
import 'scene3d_theme.dart';

/// Abre a folha modal de Exportar (Tela 8 do mockup).
Future<void> abrirFolhaDeExportar(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
}) => mostrarFolhaScene3D<void>(
  context,
  title: 'Exportar',
  body: FolhaDeExportar(layerId: layerId),
);

class FolhaDeExportar extends ConsumerStatefulWidget {
  const FolhaDeExportar({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<FolhaDeExportar> createState() => _FolhaDeExportarState();
}

enum _FormatoExportar { video, imagem }

class _FolhaDeExportarState extends ConsumerState<FolhaDeExportar> {
  _FormatoExportar _formato = _FormatoExportar.video;
  String _resolucao = '1080p (Full HD)';
  String _fps = '30 FPS';

  @override
  Widget build(BuildContext context) {
    final projeto = ref.watch(projetoVisivelProvider);
    final bruta = projeto.layerById(widget.layerId);
    final duracaoSegundos =
        bruta is Scene3DLayer && bruta.duration.inMicroseconds > 0
        ? (bruta.duration.inMicroseconds / 1e6).round()
        : 10;
    final duracaoTexto =
        '${(duracaoSegundos ~/ 60).toString().padLeft(2, '0')}:${(duracaoSegundos % 60).toString().padLeft(2, '0')}';

    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // Formato: Vídeo / Imagem
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const AppText('Formato',
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: Scene3DTheme.textMuted,
                ),
              ),
              const SizedBox(height: 8),
              Row(
                children: [
                  Expanded(
                    child: _buildFormatPill('Vídeo', _FormatoExportar.video),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: _buildFormatPill(
                      'Sequência PNG',
                      _FormatoExportar.imagem,
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),

        // Resolução Dropdown
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const AppText('Resolução',
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: Scene3DTheme.textMuted,
                ),
              ),
              const SizedBox(height: 8),
              _buildDropdownField(
                value: _resolucao,
                items: const ['720p (HD)', '1080p (Full HD)', '4K (Ultra HD)'],
                onChanged: (v) => setState(() => _resolucao = v),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),

        // FPS Dropdown
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const AppText('FPS',
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: Scene3DTheme.textMuted,
                ),
              ),
              const SizedBox(height: 8),
              _buildDropdownField(
                value: _fps,
                items: const ['24 FPS', '30 FPS', '60 FPS'],
                onChanged: (v) => setState(() => _fps = v),
              ),
            ],
          ),
        ),
        const SizedBox(height: 16),

        // Duração
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const AppText(
                'Duração',
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w600,
                  color: Scene3DTheme.textMuted,
                ),
              ),
              const SizedBox(height: 8),
              Container(
                height: 48,
                decoration: Scene3DTheme.cardDecoration(borderRadius: 12),
                padding: const EdgeInsets.symmetric(horizontal: 14),
                alignment: Alignment.centerLeft,
                child: AppText(
                  duracaoTexto,
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    color: Scene3DTheme.text,
                  ),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 24),

        // Botão Inferior: Exportar
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 16),
          child: Scene3DActionButton(
            label: 'Exportar',
            icon: Icons.file_upload_outlined,
            onPressed: () {
              final navigator = Navigator.of(context);
              final settings = ExportSettings(
                size: _resolucao.startsWith('720')
                    ? ExportSize.p720
                    : (_resolucao.startsWith('4K')
                          ? ExportSize.p2160
                          : ExportSize.p1080),
                fps: int.parse(_fps.split(' ').first),
                format: _formato == _FormatoExportar.video
                    ? ExportFormat.mp4
                    : ExportFormat.pngSequence,
              );
              navigator.pop();
              navigator.push(
                MaterialPageRoute<void>(
                  builder: (_) => ExportVideoScreen(settings: settings),
                ),
              );
            },
          ),
        ),
        const SizedBox(height: 10),
      ],
    );
  }

  Widget _buildFormatPill(String label, _FormatoExportar fmt) {
    final active = _formato == fmt;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: () => setState(() => _formato = fmt),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 150),
        height: 44,
        decoration: Scene3DTheme.pillDecoration(active: active),
        child: Center(
          child: AppText(
            label,
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w700,
              color: active ? Scene3DTheme.onAccent : Scene3DTheme.textMuted,
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildDropdownField({
    required String value,
    required List<String> items,
    required ValueChanged<String> onChanged,
  }) {
    return Container(
      height: 48,
      decoration: Scene3DTheme.cardDecoration(borderRadius: 12),
      padding: const EdgeInsets.symmetric(horizontal: 14),
      child: DropdownButtonHideUnderline(
        child: DropdownButton<String>(
          value: value,
          dropdownColor: Scene3DTheme.panelElevated,
          icon: const Icon(
            Icons.keyboard_arrow_down_rounded,
            color: Scene3DTheme.textMuted,
          ),
          isExpanded: true,
          style: const TextStyle(
            fontSize: 14,
            fontWeight: FontWeight.w600,
            color: Scene3DTheme.text,
          ),
          items: items.map((item) {
            return DropdownMenuItem<String>(value: item, child: AppText(item));
          }).toList(),
          onChanged: (v) {
            if (v != null) onChanged(v);
          },
        ),
      ),
    );
  }
}
