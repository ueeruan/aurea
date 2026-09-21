import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:aurea/src/features/projects/domain/pacote_aurea.dart';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'dart:convert';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import 'dart:typed_data';

import '../../application/editor_controller.dart';
import '../../domain/lottie_export.dart';
import '../../domain/template_pack.dart';
import 'am_colors.dart';
import '../../domain/layer.dart';
import 'am_widgets.dart';

/// OUTROS FORMATOS — o que NAO e video.
///
/// ==========================================================================
/// ESTA FOLHA ERA A SEGUNDA PORTA DA EXPORTACAO DE VIDEO (20/09/2026)
/// ==========================================================================
///
/// Ela tinha Formato, Tamanho, Quadros, Codec, Qualidade e Taxa, e a tela
/// `ExportVideoScreen` tinha Formato, Tamanho, Quadros, Codec e
/// Qualidade. Os mesmos controles, duas vezes, em duas aparencias
/// diferentes — e os dois resumos discordavam: aqui o peso do arquivo
/// saia de `project.duration`, que tem piso de cinco segundos, e la de
/// `duracaoDoConteudo`, que e o que o motor grava. Um projeto de dois
/// segundos era anunciado com mais que o dobro do tamanho.
///
/// Tudo o que decide o VIDEO foi para a tela, que e a porta unica. O que
/// sobrou aqui e o que nunca foi video e continua inteiro: Lottie, SVG
/// animado, template, pacote `.aurea` e legendas `.srt`.
Future<void> showOutrosFormatosSheet(BuildContext context, WidgetRef ref) async {
  final controller = ref.read(editorControllerProvider.notifier);
  String? status;
  var busy = false;

  await showParamSheet(
    context,
    title: 'Outros formatos',
    heightFactor: 0.55,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final issues = validateForLottie(project);
        final blocking = issues.where((i) => i.blocking).toList();
        final warnings = issues.where((i) => !i.blocking).toList();

        Future<void> writeBytes(
          String name,
          Uint8List bytes,
          String label,
        ) async {
          setSheetState(() => busy = true);
          try {
            final safeName = name.replaceAll(
              RegExp(r'[<>:"/\\|?*\x00-\x1f]'),
              '_',
            );
            final path = await FilePicker.platform.saveFile(
              dialogTitle: 'Salvar $label',
              fileName: safeName,
              type: FileType.custom,
              allowedExtensions: [safeName.split('.').last],
              bytes: bytes,
            );
            if (path != null && !Platform.isAndroid && !Platform.isIOS) {
              await File(path).writeAsBytes(bytes, flush: true);
            }
            if (!sheetContext.mounted) return;
            setSheetState(() {
              busy = false;
              status = path == null ? 'Exportação cancelada' : '$label salvo';
            });
          } catch (e) {
            if (!sheetContext.mounted) return;
            setSheetState(() {
              busy = false;
              status = 'Não foi possível salvar: $e';
            });
          }
        }

        Future<void> writeFile(String name, String content, String label) =>
            writeBytes(name, Uint8List.fromList(utf8.encode(content)), label);

        Future<void> exportSvg() async {
          try {
            final svg = exportAnimatedSvg(project);
            if (!svg.contains('<path')) {
              setSheetState(
                () => status = 'Adicione uma forma vetorial para exportar SVG.',
              );
              return;
            }
            await writeFile('${project.name}.svg', svg, 'SVG animado');
          } catch (_) {
            if (sheetContext.mounted) {
              setSheetState(
                () => status = 'Não foi possível gerar o SVG deste projeto.',
              );
            }
          }
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 12, 18, 14),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // LEGENDAS (.srt): uma por camada de legenda.
                for (final legenda in project.layers.whereType<CaptionLayer>())
                  Padding(
                    padding: const EdgeInsets.only(bottom: 6),
                    child: SizedBox(
                      width: double.infinity,
                      child: CupertinoButton(
                        key: ValueKey('export-srt-${legenda.id}'),
                        color: AmColors.chip,
                        borderRadius: BorderRadius.circular(12),
                        padding: const EdgeInsets.symmetric(vertical: 10),
                        onPressed: busy
                            ? null
                            : () {
                                final srt = controller.exportCaptionsSrt(
                                  legenda.id,
                                );
                                if (srt == null) return;
                                final nome = legenda.name.replaceAll(
                                  RegExp(r'[^A-Za-z0-9_-]+'),
                                  '_',
                                );
                                writeFile('$nome.srt', srt, 'legendas (.srt)');
                              },
                        child: AppTextMoldado(
                          'Legendas (.srt) · {0}', [legenda.name],
                          style: const TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w600,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                    ),
                  ),

                const AppText(
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
                      child: AppText(
                        'Modo compativel com Lottie: avisa sobre o que '
                        'nao sobrevive enquanto voce monta.',
                        style: TextStyle(fontSize: 11, color: AmColors.muted),
                      ),
                    ),
                  ],
                ),
                Divider(color: AmColors.hairline, height: 18),

                // VALIDADOR: o que nao sobrevive, camada por camada.
                AppText(
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
                          child: AppText(
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
                          child: AppText(
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
                    key: const ValueKey('exportar-lottie'),
                    color: AmColors.accent,
                    borderRadius: BorderRadius.circular(12),
                    onPressed: busy
                        ? null
                        : () {
                            final out = exportLottie(project);
                            writeFile(
                              '${project.name}.json',
                              const JsonEncoder.withIndent(
                                '  ',
                              ).convert(out.json),
                              'Lottie (${out.exported} camadas'
                                  '${out.skipped > 0 ? ', ${out.skipped} puladas' : ''})',
                            );
                          },
                    child: const AppText(
                      'Exportar Lottie (.json)',
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                        color: AureaColors.onAccent,
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 8),
                SizedBox(
                  width: double.infinity,
                  child: CupertinoButton(
                    key: const ValueKey('exportar-svg'),
                    color: AmColors.chip,
                    borderRadius: BorderRadius.circular(12),
                    onPressed: busy ? null : exportSvg,
                    child: AppText(
                      'Exportar SVG animado',
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w600,
                        color: AmColors.accent,
                      ),
                    ),
                  ),
                ),
                Divider(color: AmColors.hairline, height: 22),
                const AppText(
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
                        AppText(
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
                        for (final aviso in problemas.where((i) => !i.blocking))
                          Padding(
                            padding: const EdgeInsets.only(top: 3),
                            child: AppText(
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
                            key: const ValueKey('exportar-template'),
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
                            child: AppText(
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
                        const SizedBox(height: 8),
                        // O PACOTE: projeto + midias num arquivo
                        // so, que abre em qualquer aparelho.
                        SizedBox(
                          width: double.infinity,
                          child: CupertinoButton(
                            key: const ValueKey('exportar-pacote'),
                            color: AmColors.chip,
                            borderRadius: BorderRadius.circular(12),
                            onPressed: busy
                                ? null
                                : () => writeBytes(
                                    '${project.name}.aurea',
                                    PacoteAurea.montar(project),
                                    'Pacote do projeto',
                                  ),
                            child: AppText(
                              'Exportar pacote .aurea',
                              style: TextStyle(
                                fontSize: 14,
                                fontWeight: FontWeight.w600,
                                color: AmColors.accent,
                              ),
                            ),
                          ),
                        ),
                      ],
                    );
                  },
                ),
                if (status != null) ...[
                  const SizedBox(height: 10),
                  AppText(
                    status!,
                    style: TextStyle(fontSize: 11, color: AmColors.accent),
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
