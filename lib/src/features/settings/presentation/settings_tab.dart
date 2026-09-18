import 'dart:io';

import '../../../core/l10n/app_language.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../editor/application/motor3d_modo.dart';
import '../../editor/application/qualidade3d_controller.dart';
import '../../editor/domain/orcamento_render.dart';
import 'estresse3d_screen.dart';
import 'travadas_screen.dart';
import '../../editor/application/proxy_service.dart';
import '../../editor/application/media_preview_service.dart';
import '../../../core/ui/snack.dart';

import 'package:path_provider/path_provider.dart';

import '../../../core/theme/app_theme.dart';
import '../../projects/domain/project_presets.dart';
import '../application/grafico_preferencia.dart';
import '../application/settings_controller.dart';
import '../../editor/domain/modo_de_transcricao.dart';

/// Aba Ajustes: listas agrupadas estilo iOS.
class SettingsTab extends ConsumerWidget {
  const SettingsTab({super.key});

  Future<void> _clearCache(BuildContext context) async {
    var removedBytes = 0;
    try {
      final tempDir = await getTemporaryDirectory();
      if (tempDir.existsSync()) {
        for (final entity in tempDir.listSync()) {
          try {
            if (entity is File) {
              removedBytes += entity.lengthSync();
              entity.deleteSync();
            } else if (entity is Directory) {
              entity.deleteSync(recursive: true);
            }
          } catch (_) {
            // Arquivo em uso; ignora e segue.
          }
        }
      }
    } catch (_) {}
    if (!context.mounted) return;
    final mb = (removedBytes / (1024 * 1024)).toStringAsFixed(1);
    await ProxyService.instance.clearCache();
    MediaPreviewService.instance.clearMemory();
    if (!context.mounted) return;
    AureaSnack.show(context, 'Cache limpo ($mb MB liberados)');
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final settings = ref.watch(settingsControllerProvider);
    final controller = ref.read(settingsControllerProvider.notifier);

    return SafeArea(
      bottom: false,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 12, 20, 120),
        children: [
          AppText('Ajustes', style: Theme.of(context).textTheme.headlineLarge),
          const SizedBox(height: 24),
          ListTile(
            leading: const Icon(Icons.language),
            title: const AppText('Idioma'),
            subtitle: AppText(appLanguages[ref.watch(appLanguageProvider)]!),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => showDialog<void>(
              context: context,
              builder: (dialogContext) => SimpleDialog(
                title: const AppText('Idioma'),
                children: [
                  for (final language in appLanguages.entries)
                    SimpleDialogOption(
                      key: ValueKey('language-${language.key}'),
                      onPressed: () async {
                        try {
                          await ref
                              .read(appLanguageProvider.notifier)
                              .select(language.key);
                          if (dialogContext.mounted) {
                            Navigator.pop(dialogContext);
                          }
                        } catch (_) {
                          if (dialogContext.mounted) {
                            ScaffoldMessenger.of(context).showSnackBar(
                              const SnackBar(
                                content: AppText('Tente novamente'),
                              ),
                            );
                          }
                        }
                      },
                      child: AppText(
                        language.value,
                        textDirection: language.key == 'ar'
                            ? TextDirection.rtl
                            : TextDirection.ltr,
                      ),
                    ),
                ],
              ),
            ),
          ),
          const _GroupHeader('Padroes de novos projetos'),
          _Group(
            children: [
              _SegmentedRow<String>(
                label: 'Proporcao',
                values: [for (final a in ProjectPresets.aspects) a.key],
                selected: settings.defaultAspectKey,
                labelOf: (k) => k,
                onChanged: controller.setDefaultAspect,
              ),
              const _GroupDivider(),
              _SegmentedRow<int>(
                label: 'Resolucao',
                values: ProjectPresets.resolutions,
                selected: settings.defaultResolution,
                labelOf: (r) => switch (r) {
                  720 => '720p',
                  1080 => '1080p',
                  2160 => '4K',
                  _ => '${r}p',
                },
                onChanged: controller.setDefaultResolution,
              ),
              const _GroupDivider(),
              // QUANTO DURA UMA CAMADA NOVA: quem faz edit rapido vive
              // encurtando os 3 s; agora escolhe uma vez.
              _SegmentedRow<int>(
                label: 'Camada nova dura',
                values: const [2, 3, 5],
                selected: settings.defaultLayerSeconds,
                labelOf: (v) => '$v s',
                onChanged: controller.setDefaultLayerSeconds,
              ),
              const _GroupDivider(),
              _SegmentedRow<int>(
                label: 'Quadros por segundo',
                values: ProjectPresets.fpsOptions,
                selected: settings.defaultFps,
                labelOf: (f) => '$f',
                onChanged: controller.setDefaultFps,
              ),
            ],
          ),
          const SizedBox(height: 26),
          const _GroupHeader('Aparencia'),
          _Group(
            children: [
              _SegmentedRow<String>(
                label: 'Tema',
                values: const ['escuro', 'claro', 'sistema'],
                selected: settings.themeMode,
                labelOf: (m) => switch (m) {
                  'claro' => 'Claro',
                  'sistema' => 'Sistema',
                  _ => 'Escuro',
                },
                onChanged: controller.setThemeMode,
              ),
            ],
          ),
          const SizedBox(height: 26),
          const _GroupHeader('Legendas'),
          _Group(
            children: [
              _SegmentedRow<ModoDeTranscricao>(
                label: 'Transcrição automática',
                values: ModoDeTranscricao.values,
                selected: settings.modoDeTranscricao,
                labelOf: (m) => m.emPalavras,
                onChanged: controller.setModoDeTranscricao,
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 14),
                child: AppText(
                  settings.modoDeTranscricao.explicacao,
                  style: TextStyle(fontSize: 12.5, color: AppColors.muted),
                ),
              ),
            ],
          ),
          const SizedBox(height: 26),
          const _GroupHeader('Exportacao'),
          _Group(
            children: [
              _SwitchRow(
                title: 'Salvar na galeria',
                subtitle: 'Copia o video exportado para a galeria',
                value: settings.saveToGallery,
                onChanged: controller.setSaveToGallery,
              ),
            ],
          ),
          const SizedBox(height: 26),
          const _GroupHeader('Cena 3D'),
          const _Group(
            children: [
              _Motor3DRow(),
              _GroupDivider(),
              _Qualidade3DRow(),
              _GroupDivider(),
              _EstresseRow(),
              _GroupDivider(),
              _TravadasRow(),
            ],
          ),
          // So no Android: no iPhone o Impeller e sempre Metal.
          if (Platform.isAndroid) ...[
            const SizedBox(height: 26),
            const _GroupHeader('Graficos'),
            const _Group(children: [_GraficoRow()]),
          ],
          const SizedBox(height: 26),
          const _GroupHeader('Geral'),
          _Group(
            children: [
              _SwitchRow(
                title: 'Vibracao ao interagir',
                value: settings.hapticFeedback,
                onChanged: controller.setHapticFeedback,
              ),
              const _GroupDivider(),
              _TapRow(
                title: 'Limpar cache',
                subtitle: 'Remove arquivos temporarios de preview e render',
                onTap: () => _clearCache(context),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _GroupHeader extends StatelessWidget {
  const _GroupHeader(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 16, bottom: 8),
      child: AppText(
        text.toUpperCase(),
        style: TextStyle(
          fontSize: 12,
          letterSpacing: 0.6,
          fontWeight: FontWeight.w500,
          color: AppColors.muted,
        ),
      ),
    );
  }
}

class _Group extends StatelessWidget {
  const _Group({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: AppColors.surface,
      clipBehavior: Clip.antiAlias,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      child: Column(children: children),
    );
  }
}

class _GroupDivider extends StatelessWidget {
  const _GroupDivider();

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 16),
      child: Divider(color: AppColors.hairline),
    );
  }
}

class _SegmentedRow<T extends Object> extends StatelessWidget {
  const _SegmentedRow({
    required this.label,
    required this.values,
    required this.selected,
    required this.labelOf,
    required this.onChanged,
  });

  final String label;
  final List<T> values;
  final T selected;
  final String Function(T) labelOf;
  final ValueChanged<T> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 14, 16, 14),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          AppText(label, style: Theme.of(context).textTheme.bodyLarge),
          const SizedBox(height: 10),
          SizedBox(
            width: double.infinity,
            child: CupertinoSlidingSegmentedControl<T>(
              groupValue: selected,
              backgroundColor: AppColors.background,
              thumbColor: AppColors.surfaceHigh,
              onValueChanged: (v) {
                if (v != null) onChanged(v);
              },
              children: {
                for (final v in values)
                  v: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 6),
                    child: AppText(
                      labelOf(v),
                      style: TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.w600,
                        letterSpacing: -0.1,
                        color: v == selected
                            ? AppColors.lime
                            : AppColors.onDark,
                      ),
                    ),
                  ),
              },
            ),
          ),
        ],
      ),
    );
  }
}

class _SwitchRow extends StatelessWidget {
  const _SwitchRow({
    required this.title,
    this.subtitle,
    required this.value,
    required this.onChanged,
  });

  final String title;
  final String? subtitle;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 10, 12, 10),
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                AppText(title, style: Theme.of(context).textTheme.bodyLarge),
                if (subtitle != null) ...[
                  const SizedBox(height: 1),
                  AppText(
                    subtitle!,
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                ],
              ],
            ),
          ),
          CupertinoSwitch(
            value: value,
            activeTrackColor: AppColors.lime,
            thumbColor: value ? const Color(0xFF0B0E12) : null,
            onChanged: onChanged,
          ),
        ],
      ),
    );
  }
}

class _TapRow extends StatelessWidget {
  const _TapRow({required this.title, this.subtitle, required this.onTap});

  final String title;
  final String? subtitle;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return InkWell(
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 12),
        child: Row(
          children: [
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText(title, style: Theme.of(context).textTheme.bodyLarge),
                  if (subtitle != null) ...[
                    const SizedBox(height: 1),
                    AppText(
                      subtitle!,
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                  ],
                ],
              ),
            ),
            Icon(
              CupertinoIcons.chevron_right,
              size: 16,
              color: AppColors.muted,
            ),
          ],
        ),
      ),
    );
  }
}

/// QUEM DESENHA A CENA 3D.
///
/// O motor em GPU e mais rapido e mais bonito, mas vive fora do Dart:
/// se ele quebra, o app fecha sem aviso. O app ja se protege sozinho —
/// depois de uma queda ele volta para o pintor em CPU — e esta linha e
/// onde a pessoa ve que isso aconteceu, por que, e como desfazer.
class _Motor3DRow extends StatefulWidget {
  const _Motor3DRow();

  @override
  State<_Motor3DRow> createState() => _Motor3DRowState();
}

class _Motor3DRowState extends State<_Motor3DRow> {
  @override
  Widget build(BuildContext context) {
    final pref = Motor3DPreferencia.instancia;
    if (pref == null) return const SizedBox.shrink();
    final aviso = pref.motivoDeNaoTentar;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SegmentedRow<Motor3DModo>(
          label: 'Motor 3D',
          values: Motor3DModo.values,
          selected: pref.modo,
          labelOf: motor3dModoRotulo,
          onChanged: (m) async {
            await pref.definirModo(m);
            if (mounted) setState(() {});
          },
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 14),
          child: AppText(
            aviso.isEmpty
                ? 'Automatico usa a GPU e desiste sozinho se o app fechar '
                      'desenhando. Reabra o app depois de trocar.'
                : 'Desenhando em CPU porque $aviso. Toque em Sempre GPU '
                      'para tentar de novo, e reabra o app.',
            style: TextStyle(
              fontSize: 12,
              height: 1.35,
              color: AppColors.muted,
            ),
          ),
        ),
      ],
    );
  }
}

/// O interruptor Vulkan/OpenGL ES do Android, com a explicacao e o
/// estado da migalha (ver [GraficoPreferencia]).
class _GraficoRow extends StatefulWidget {
  const _GraficoRow();

  @override
  State<_GraficoRow> createState() => _GraficoRowState();
}

class _GraficoRowState extends State<_GraficoRow> {
  @override
  Widget build(BuildContext context) {
    final pref = GraficoPreferencia.instancia;
    if (pref == null) return const SizedBox.shrink();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SwitchRow(
          title: 'Desenhar com OpenGL ES',
          subtitle: 'Para cores erradas no preview em alguns aparelhos',
          value: pref.openGl,
          onChanged: (v) async {
            await pref.definirOpenGl(v);
            if (mounted) setState(() {});
          },
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 14),
          child: AppText(
            pref.caiu
                ? 'O app nao voltou da ultima abertura em OpenGL ES, entao '
                      'a opcao foi desligada sozinha. Este aparelho fica no '
                      'Vulkan.'
                : 'O Android desenha com Vulkan quando o aparelho diz que '
                      'tem; em algumas GPUs Mali (MediaTek) e nele que as '
                      'cores saem erradas, no preview e na exportacao. '
                      'Ligue, reabra o app e compare. Se o app nao abrir, '
                      'a opcao se desliga sozinha.',
            style: TextStyle(
              fontSize: 12,
              height: 1.35,
              color: AppColors.muted,
            ),
          ),
        ),
      ],
    );
  }
}

/// O teto de qualidade da cena 3D (ver [ControladorDeQualidade3D]).
class _Qualidade3DRow extends StatefulWidget {
  const _Qualidade3DRow();

  @override
  State<_Qualidade3DRow> createState() => _Qualidade3DRowState();
}

class _Qualidade3DRowState extends State<_Qualidade3DRow> {
  @override
  Widget build(BuildContext context) {
    final c = ControladorDeQualidade3D.instancia;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SegmentedRow<TetoDeQualidade3D>(
          label: 'Qualidade 3D',
          values: TetoDeQualidade3D.values,
          selected: c.teto,
          labelOf: tetoDeQualidade3dRotulo,
          onChanged: (t) async {
            await c.definirTeto(t);
            if (mounted) setState(() {});
          },
        ),
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 14),
          child: ValueListenableBuilder<Qualidade3D>(
            valueListenable: c.nivel,
            builder: (context, nivel, _) => AppText(
              'Automatica escolhe pelo orcamento de memoria do aparelho e '
              'desce um degrau (sombra, MSAA, escala, textura, LOD) antes '
              'de o app travar; sobe de volta quando sobra folga. Agora: '
              '${qualidade3dRotulo(nivel)}.',
              style: TextStyle(
                fontSize: 12,
                height: 1.35,
                color: AppColors.muted,
              ),
            ),
          ),
        ),
      ],
    );
  }
}

/// O REGISTRO DE TRAVADAS.
///
/// Fica aqui, e nao escondido atras de uma flag de desenvolvimento,
/// porque quem tem o aparelho que trava e quem usa o aplicativo — nao
/// quem o escreve.
class _TravadasRow extends StatelessWidget {
  const _TravadasRow();

  @override
  Widget build(BuildContext context) => _TapRow(
    title: 'Travadas',
    subtitle: 'O que demorou, medido pelo proprio aparelho',
    onTap: () => Navigator.of(context)
        .push(MaterialPageRoute<void>(builder: (_) => const TravadasScreen())),
  );
}

class _EstresseRow extends StatelessWidget {
  const _EstresseRow();

  @override
  Widget build(BuildContext context) => _TapRow(
    title: 'Teste de estresse do motor 3D',
    subtitle: 'Nove cenas pesadas, com relatorio para enviar',
    onTap: () => Navigator.of(
      context,
    ).push(MaterialPageRoute<void>(builder: (_) => const Estresse3DScreen())),
  );
}
