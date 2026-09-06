import 'dart:convert';
import 'dart:io';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';

import '../../application/editor_controller.dart';
import '../../../export/domain/export_settings.dart';
import '../../../export/presentation/export_video_screen.dart';
import '../../domain/lottie_export.dart';
import '../../domain/template_pack.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// Uma linha de opcoes: rotulo a esquerda, pastilhas a direita.
class _Escolha extends StatelessWidget {
  const _Escolha({
    required this.rotulo,
    required this.opcoes,
    required this.indice,
    required this.onEscolher,
  });

  final String rotulo;
  final List<String> opcoes;
  final int indice;
  final ValueChanged<int> onEscolher;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 3),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: 74,
          child: Text(
            rotulo,
            style: const TextStyle(fontSize: 12, color: AmColors.muted),
          ),
        ),
        Expanded(
          child: Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var i = 0; i < opcoes.length; i++)
                GestureDetector(
                  onTap: () => onEscolher(i),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 10,
                      vertical: 5,
                    ),
                    decoration: BoxDecoration(
                      color: i == indice ? AmColors.accentDim : AmColors.chip,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: Text(
                      opcoes[i],
                      style: TextStyle(
                        fontSize: 11,
                        color: i == indice ? AmColors.accent : AmColors.muted,
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ],
    ),
  );
}

/// EXPORTAR (spec motion-graphics-pro, PR-X23/X25): Lottie com validador
/// e SVG animado. Motion designer que trabalha para produto entrega
/// Lottie, nao MP4.
Future<void> showExportSheet(BuildContext context, WidgetRef ref) async {
  final controller = ref.read(editorControllerProvider.notifier);
  // NUCLEO: exportar MP4, uma resolucao, uma qualidade — o botao e so.
  // Tamanho, codec, taxa, Lottie, SVG e template sao estudio.
  const completo = true;
  String? status;
  var busy = false;
  var ajustes = const ExportSettings();

  await showParamSheet(
    context,
    title: 'Exportar',
    heightFactor: 0.55,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final issues = validateForLottie(project);
        final blocking = issues.where((i) => i.blocking).toList();
        final warnings = issues.where((i) => !i.blocking).toList();

        Future<void> writeFile(
          String name,
          String content,
          String label,
        ) async {
          setSheetState(() => busy = true);
          try {
            final dir = await getApplicationDocumentsDirectory();
            final out = Directory('${dir.path}/exports');
            if (!out.existsSync()) out.createSync(recursive: true);
            final file = File('${out.path}/$name');
            await file.writeAsString(content);
            await Clipboard.setData(ClipboardData(text: file.path));
            setSheetState(() {
              busy = false;
              status =
                  '$label salvo em ${file.path}\n'
                  '(caminho copiado para a area de transferencia)';
            });
          } catch (e) {
            setSheetState(() {
              busy = false;
              status = 'Falhou: $e';
            });
          }
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 12, 18, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Exportar',
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(height: 10),

                // VIDEO: a composicao inteira, renderizada quadro a
                // quadro na resolucao do projeto e codificada em MP4.
                SizedBox(
                  width: double.infinity,
                  child: CupertinoButton(
                    color: AmColors.accent,
                    borderRadius: BorderRadius.circular(12),
                    onPressed: busy
                        ? null
                        : () {
                            closeParamSheet(sheetContext);
                            Future.microtask(() {
                              if (!context.mounted) return;
                              Navigator.of(context).push(
                                MaterialPageRoute<void>(
                                  fullscreenDialog: true,
                                  builder: (_) => ExportVideoScreen(
                                    quality: ajustes.quality,
                                    settings: ajustes,
                                  ),
                                ),
                              );
                            });
                          },
                    child: const Text(
                      'Exportar video (MP4)',
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                        color: Color(0xFF0B0E12),
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 10),

                if (completo) ...[
                  _Escolha(
                    rotulo: 'Formato',
                    opcoes: [
                      for (final f in ExportFormat.values) exportFormatLabel(f),
                    ],
                    indice: ajustes.format.index,
                    onEscolher: (i) => setSheetState(
                      () => ajustes = ajustes.copyWith(
                        format: ExportFormat.values[i],
                      ),
                    ),
                  ),
                  _Escolha(
                    rotulo: 'Tamanho',
                    opcoes: [
                      for (final t in ExportSize.values) exportSizeLabel(t),
                    ],
                    indice: ajustes.size.index,
                    onEscolher: (i) => setSheetState(
                      () => ajustes = ajustes.copyWith(
                        size: ExportSize.values[i],
                      ),
                    ),
                  ),
                  _Escolha(
                    rotulo: 'Quadros',
                    opcoes: const ['Projeto', '24', '25', '30', '50', '60'],
                    indice: ajustes.fps == null
                        ? 0
                        : (const [24, 25, 30, 50, 60].indexOf(ajustes.fps!) + 1)
                              .clamp(0, 5),
                    onEscolher: (i) => setSheetState(
                      () => ajustes = i == 0
                          ? ajustes.copyWith(clearFps: true)
                          : ajustes.copyWith(
                              fps: const [24, 25, 30, 50, 60][i - 1],
                            ),
                    ),
                  ),
                  if (ajustes.format == ExportFormat.mp4) ...[
                    _Escolha(
                      rotulo: 'Codec',
                      opcoes: [
                        for (final c in ExportCodec.values) exportCodecLabel(c),
                      ],
                      indice: ajustes.codec.index,
                      onEscolher: (i) => setSheetState(
                        () => ajustes = ajustes.copyWith(
                          codec: ExportCodec.values[i],
                        ),
                      ),
                    ),
                    _Escolha(
                      rotulo: 'Qualidade',
                      opcoes: const ['baixa', 'media', 'alta', 'na mao'],
                      indice: ajustes.bitrateMbps != null
                          ? 3
                          : const [
                              'baixa',
                              'media',
                              'alta',
                            ].indexOf(ajustes.quality).clamp(0, 2),
                      onEscolher: (i) => setSheetState(
                        () => ajustes = i == 3
                            ? ajustes.copyWith(bitrateMbps: 12)
                            : ajustes.copyWith(
                                quality: const ['baixa', 'media', 'alta'][i],
                                clearBitrate: true,
                              ),
                      ),
                    ),
                    if (ajustes.bitrateMbps != null)
                      Row(
                        children: [
                          const SizedBox(
                            width: 74,
                            child: Text(
                              'Taxa',
                              style: TextStyle(
                                fontSize: 12,
                                color: AmColors.muted,
                              ),
                            ),
                          ),
                          Expanded(
                            child: AmTickRuler(
                              value: ajustes.bitrateMbps!.clamp(1, 120),
                              min: 1,
                              max: 120,
                              unitsPerPixel: ((120) - (1)) / 420,
                              height: 40,
                              onChanged: (v) => setSheetState(
                                () =>
                                    ajustes = ajustes.copyWith(bitrateMbps: v),
                              ),
                            ),
                          ),
                          SizedBox(
                            width: 66,
                            child: Text(
                              '${ajustes.bitrateMbps!.toStringAsFixed(0)} Mb/s',
                              textAlign: TextAlign.right,
                              style: const TextStyle(
                                fontSize: 11,
                                color: AmColors.text,
                              ),
                            ),
                          ),
                        ],
                      ),
                  ],

                  const SizedBox(height: 6),
                  Builder(
                    builder: (context) {
                      final (w, h) = ajustes.resolve(
                        project.outputWidth,
                        project.outputHeight,
                      );
                      final f = ajustes.resolveFps(project.fps);
                      if (ajustes.format == ExportFormat.pngSequence) {
                        return Text(
                          '${w}x$h - $f fps - PNG com transparencia. '
                          'Sequencia ocupa muito espaco, mas nao perde nada.',
                          style: const TextStyle(
                            fontSize: 11,
                            height: 1.35,
                            color: AmColors.muted,
                          ),
                        );
                      }
                      final mb = ajustes.estimatedMegabytes(
                        w,
                        h,
                        f,
                        project.duration,
                      );
                      final aviso = ajustes.codec == ExportCodec.hevc
                          ? ' - HEVC nao toca em aparelho antigo'
                          : '';
                      return Text(
                        '${w}x$h - $f fps - ~${mb.toStringAsFixed(0)} MB$aviso',
                        style: const TextStyle(
                          fontSize: 11,
                          height: 1.35,
                          color: AmColors.muted,
                        ),
                      );
                    },
                  ),
                  const Divider(color: AmColors.hairline, height: 22),
                  const Text(
                    'Para produto (Lottie / SVG)',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  const SizedBox(height: 4),
                  // Modo compativel: avisa desde o comeco, em vez de
                  // surpreender no fim.
                  Row(
                    children: [
                      Transform.scale(
                        scale: 0.72,
                        child: CupertinoSwitch(
                          value: project.lottieMode,
                          activeTrackColor: AmColors.accent,
                          onChanged: (v) {
                            controller.setLottieMode(v);
                            setSheetState(() {});
                          },
                        ),
                      ),
                      const Expanded(
                        child: Text(
                          'Modo compativel com Lottie: avisa sobre o que '
                          'nao sobrevive enquanto voce monta.',
                          style: TextStyle(fontSize: 11, color: AmColors.muted),
                        ),
                      ),
                    ],
                  ),
                  const Divider(color: AmColors.hairline, height: 18),

                  // VALIDADOR: o que nao sobrevive, camada por camada.
                  Text(
                    blocking.isEmpty
                        ? 'Tudo sobrevive ao Lottie.'
                        : '${blocking.length} camada(s) NAO sobrevivem:',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: blocking.isEmpty
                          ? AmColors.accent
                          : const Color(0xFFE85B81),
                    ),
                  ),
                  const SizedBox(height: 4),
                  for (final i in blocking)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 4),
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          const Icon(
                            CupertinoIcons.exclamationmark_triangle,
                            size: 14,
                            color: Color(0xFFE85B81),
                          ),
                          const SizedBox(width: 6),
                          Expanded(
                            child: Text(
                              '${i.layerName}: ${i.message}',
                              style: const TextStyle(
                                fontSize: 11,
                                color: AmColors.muted,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  for (final i in warnings)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 4),
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          const Icon(
                            CupertinoIcons.info_circle,
                            size: 14,
                            color: AmColors.muted,
                          ),
                          const SizedBox(width: 6),
                          Expanded(
                            child: Text(
                              '${i.layerName}: ${i.message}',
                              style: const TextStyle(
                                fontSize: 11,
                                color: AmColors.muted,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  const SizedBox(height: 12),

                  SizedBox(
                    width: double.infinity,
                    child: CupertinoButton(
                      color: AmColors.accent,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: busy
                          ? null
                          : () {
                              final out = exportLottie(project);
                              writeFile(
                                '${project.name}.json',
                                const JsonEncoder.withIndent('  ')
                                    .convert(out.json),
                                'Lottie (${out.exported} camadas'
                                    '${out.skipped > 0 ? ', ${out.skipped} puladas' : ''})',
                              );
                            },
                      child: const Text(
                        'Exportar Lottie (.json)',
                        style: TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                          color: Color(0xFF0B0E12),
                        ),
                      ),
                    ),
                  ),
                  const SizedBox(height: 8),
                  SizedBox(
                    width: double.infinity,
                    child: CupertinoButton(
                      color: AmColors.chip,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: busy
                          ? null
                          : () => writeFile(
                              '${project.name}.svg',
                              exportAnimatedSvg(project),
                              'SVG animado',
                            ),
                      child: const Text(
                        'Exportar SVG animado',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w600,
                          color: AmColors.accent,
                        ),
                      ),
                    ),
                  ),
                  const Divider(color: AmColors.hairline, height: 22),
                  const Text(
                    'Template',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Builder(
                    builder: (context) {
                      final problemas = validateTemplate(project);
                      final trava = problemas.where((i) => i.blocking).toList();
                      return Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            trava.isEmpty
                                ? '${project.exposed.length} campo(s) para quem '
                                      'receber preencher.'
                                : trava.first.message,
                            style: TextStyle(
                              fontSize: 11,
                              height: 1.35,
                              color: trava.isEmpty
                                  ? AmColors.muted
                                  : AmColors.pink,
                            ),
                          ),
                          for (final aviso in problemas.where(
                            (i) => !i.blocking,
                          ))
                            Padding(
                              padding: const EdgeInsets.only(top: 3),
                              child: Text(
                                aviso.message,
                                style: const TextStyle(
                                  fontSize: 11,
                                  height: 1.35,
                                  color: AmColors.muted,
                                ),
                              ),
                            ),
                          const SizedBox(height: 8),
                          SizedBox(
                            width: double.infinity,
                            child: CupertinoButton(
                              color: AmColors.chip,
                              borderRadius: BorderRadius.circular(12),
                              onPressed: busy || trava.isNotEmpty
                                  ? null
                                  : () => writeFile(
                                      '${project.name}.aurea-template.json',
                                      TemplatePack(
                                        name: project.name,
                                        project: project,
                                      ).encode(),
                                      'Template',
                                    ),
                              child: Text(
                                'Exportar template',
                                style: TextStyle(
                                  fontSize: 14,
                                  fontWeight: FontWeight.w600,
                                  color: trava.isEmpty
                                      ? AmColors.accent
                                      : AmColors.muted,
                                ),
                              ),
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                ],
                if (status != null) ...[
                  const SizedBox(height: 10),
                  Text(
                    status!,
                    style: const TextStyle(
                      fontSize: 11,
                      color: AmColors.accent,
                    ),
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
