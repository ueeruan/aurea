import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../features/help/presentation/quick_guide_screen.dart';
import '../../../projects/domain/project_presets.dart';
import '../../application/editor_controller.dart';
import '../../application/freehand_session.dart';
import '../../application/preview_stats.dart';
import '../../application/ui/pro_mode.dart';
import '../../domain/layer_meta.dart';
import '../am/am_colors.dart';
import '../am/color_picker_sheet.dart';
import 'layer_actions.dart';
import 'onboarding.dart';
import '../../../../core/ui/pedir_nome.dart';
import 'folha_de_ajustes.dart';

/// ⚙ PROJETO (Fase 6) — o lugar das configuracoes, como nos tres apps
/// de referencia.
///
/// Simples: nome, proporcao, resolucao, fps, fundo, casca de cebola,
/// ajuda. Pro acrescenta guias, motion blur da composicao, paleta,
/// propriedades expostas e o diagnostico.
Future<void> showProjectSettingsSheet(BuildContext context, WidgetRef ref) {
  return folhaDoEstudio<void>(
    context,
    titulo: 'Projeto',
    alturaFator: 0.82,
    builder: (ctx, setSheet) {
      final p = ref.read(editorControllerProvider);
      final c = ref.read(editorControllerProvider.notifier);
      final pro = ref.read(proModeProvider);
      final onion = ref.read(onionSkinProvider);
      final diag = ref.read(debugOverlayProvider);
      void atualiza() {
        if (ctx.mounted) setSheet(() {});
      }

      final proporcaoAtual = ProjectPresets.aspects
          .where((a) => (a.ratio - p.aspectRatio).abs() < 0.01)
          .map((a) => a.key)
          .firstOrNull;

      return ListView(
        shrinkWrap: true,
        children: [
          LinhaDoEstudio(
            key: const ValueKey('projeto-nome'),
            icone: CupertinoIcons.pencil,
            titulo: p.name,
            subtitulo: 'Toque para renomear',
            chevron: true,
            onTap: () async {
              await renomearProjeto(context, ref);
              atualiza();
            },
          ),
          const SecaoDoEstudio('Composicao'),
          _Chips<String>(
            chave: 'projeto-proporcao',
            rotulo: translate(context, 'Proporção'),
            opcoes: [for (final a in ProjectPresets.aspects) a.key],
            rotuloDe: (k) => k,
            selecionado: proporcaoAtual,
            onEscolher: (k) {
              c.setComposition(
                aspectRatio: ProjectPresets.aspectByKey(k).ratio,
              );
              atualiza();
            },
          ),
          _Chips<int>(
            chave: 'projeto-resolucao',
            rotulo: translate(context, 'Resolução'),
            opcoes: ProjectPresets.resolutions,
            rotuloDe: ProjectPresets.resolutionLabel,
            selecionado: p.resolutionHeight,
            onEscolher: (r) {
              c.setComposition(resolutionHeight: r);
              atualiza();
            },
          ),
          _Chips<int>(
            chave: 'projeto-fps',
            rotulo: 'Quadros',
            opcoes: ProjectPresets.fpsOptions,
            rotuloDe: (f) => '$f',
            selecionado: p.fps,
            onEscolher: (f) {
              c.setComposition(fps: f);
              atualiza();
            },
          ),
          LinhaDoEstudio(
            key: const ValueKey('projeto-fundo'),
            icone: CupertinoIcons.square_fill,
            titulo: 'Fundo da composição',
            subtitulo:
                '${p.outputWidth} × ${p.outputHeight} · '
                '${(p.duration.inMilliseconds / 1000).toStringAsFixed(1)} s',
            trailing: Container(
              width: 28,
              height: 28,
              decoration: BoxDecoration(
                color: p.backgroundColor,
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: Colors.white24),
              ),
            ),
            onTap: () async {
              final cor = await showColorPicker(
                context,
                initial: p.backgroundColor,
                withAlpha: false,
                onChanged: c.setBackgroundColor,
              );
              if (cor != null) c.setBackgroundColor(cor);
              atualiza();
            },
          ),
          const SecaoDoEstudio('Preview'),
          LinhaDoEstudio(
            key: const ValueKey('projeto-cebola'),
            icone: CupertinoIcons.square_stack_3d_down_dottedline,
            titulo: 'Casca de cebola',
            subtitulo: onion == 0
                ? 'Desligada'
                : '$onion quadro${onion == 1 ? '' : 's'} vizinho${onion == 1 ? '' : 's'} em transparencia',
            trailing: AppText(
              onion == 0 ? 'Desligada' : '$onion',
              style: const TextStyle(fontSize: 13),
            ),
            onTap: () {
              ref.read(onionSkinProvider.notifier).state = (onion + 1) % 3;
              atualiza();
            },
          ),
          if (pro) ...[
            const SecaoDoEstudio('Guias'),
            LinhaDoEstudio(
              key: const ValueKey('projeto-areas-seguras'),
              icone: CupertinoIcons.rectangle_dock,
              titulo: translate(context, 'Áreas seguras'),
              subtitulo: 'Margens de titulo e acao no preview',
              ligado: p.guides.showSafeAreas,
              onTap: () {
                c.setGuides(
                  p.guides.copyWith(showSafeAreas: !p.guides.showSafeAreas),
                );
                atualiza();
              },
            ),
            _Chips<int>(
              chave: 'projeto-colunas',
              rotulo: 'Colunas',
              opcoes: const [0, 2, 3, 4, 6, 12],
              rotuloDe: (n) => n == 0 ? 'Sem' : '$n',
              selecionado: p.guides.columns,
              onEscolher: (n) {
                c.setGuides(p.guides.copyWith(columns: n));
                atualiza();
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('projeto-guia-vertical'),
              icone: CupertinoIcons.line_horizontal_3,
              titulo: 'Adicionar guia vertical',
              subtitulo:
                  '${p.guides.vertical.length} vertical, ${p.guides.horizontal.length} horizontal',
              onTap: () {
                c.addGuide(x: p.outputWidth / 2);
                atualiza();
              },
            ),
            LinhaDoEstudio(
              key: const ValueKey('projeto-guia-horizontal'),
              icone: CupertinoIcons.line_horizontal_3,
              titulo: 'Adicionar guia horizontal',
              onTap: () {
                c.addGuide(y: p.outputHeight / 2);
                atualiza();
              },
            ),
            if (p.guides.vertical.isNotEmpty || p.guides.horizontal.isNotEmpty)
              LinhaDoEstudio(
                key: const ValueKey('projeto-guias-limpar'),
                icone: CupertinoIcons.clear,
                titulo: 'Limpar guias',
                perigo: true,
                onTap: () {
                  c.setGuides(
                    p.guides.copyWith(vertical: const [], horizontal: const []),
                  );
                  atualiza();
                },
              ),
            const SecaoDoEstudio('Motion blur da composição'),
            LinhaDoEstudio(
              key: const ValueKey('projeto-motion-blur'),
              icone: CupertinoIcons.speedometer,
              titulo: 'Motion blur',
              subtitulo: p.motionBlur.enabled
                  ? 'Obturador ${p.motionBlur.shutterAngle.round()}° · ${p.motionBlur.samples} amostras'
                  : 'Desligado (as camadas com motion blur so borram com isto ligado)',
              ligado: p.motionBlur.enabled,
              onTap: () {
                c.setMotionBlur(
                  p.motionBlur.copyWith(enabled: !p.motionBlur.enabled),
                );
                atualiza();
              },
            ),
            if (p.motionBlur.enabled) ...[
              _Chips<int>(
                chave: 'projeto-obturador',
                rotulo: 'Obturador',
                opcoes: const [90, 180, 270, 360],
                rotuloDe: (a) => '$a°',
                selecionado: p.motionBlur.shutterAngle.round(),
                onEscolher: (a) {
                  c.setMotionBlur(
                    p.motionBlur.copyWith(shutterAngle: a.toDouble()),
                  );
                  atualiza();
                },
              ),
              _Chips<int>(
                chave: 'projeto-amostras',
                rotulo: 'Amostras',
                opcoes: const [8, 16, 32],
                rotuloDe: (n) => '$n',
                selecionado: p.motionBlur.samples,
                onEscolher: (n) {
                  c.setMotionBlur(p.motionBlur.copyWith(samples: n));
                  atualiza();
                },
              ),
            ],
            const SecaoDoEstudio('Paleta do projeto'),
            for (final e in p.palette.entries.entries)
              LinhaDoEstudio(
                key: ValueKey('projeto-paleta-${e.key}'),
                titulo: e.key,
                subtitulo: 'Toque para trocar a cor · segure para tirar',
                trailing: Container(
                  width: 28,
                  height: 28,
                  decoration: BoxDecoration(
                    color: e.value,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: Colors.white24),
                  ),
                ),
                onTap: () async {
                  final cor = await showColorPicker(
                    context,
                    initial: e.value,
                    onChanged: (cor) => c.setPaletteColor(e.key, cor),
                  );
                  if (cor != null) c.setPaletteColor(e.key, cor);
                  atualiza();
                },
              ),
            LinhaDoEstudio(
              key: const ValueKey('projeto-paleta-adicionar'),
              icone: CupertinoIcons.add_circled,
              titulo: 'Adicionar cor à paleta',
              onTap: () async {
                final nome = await pedirNome(
                  context,
                  titulo: 'Nome da cor',
                  atual: 'Cor ${p.palette.entries.length + 1}',
                );
                if (nome == null || nome.trim().isEmpty) return;
                if (!context.mounted) return;
                final cor = await showColorPicker(
                  context,
                  initial: AmColors.action,
                );
                if (cor != null) c.setPaletteColor(nome.trim(), cor);
                atualiza();
              },
            ),
            const SecaoDoEstudio('Propriedades expostas (template)'),
            if (p.exposed.isEmpty)
              const LinhaDoEstudio(
                icone: CupertinoIcons.slider_horizontal_3,
                titulo: 'Nenhuma propriedade exposta',
                subtitulo:
                    'Exponha um parametro pelo toque longo no nome dele (em breve) '
                    'para quem usar este projeto como template.',
              ),
            for (final ex in p.exposed)
              LinhaDoEstudio(
                key: ValueKey('projeto-exposta-${ex.id}'),
                icone: CupertinoIcons.slider_horizontal_3,
                titulo: ex.label,
                subtitulo: '${ex.group} · ${ex.property}',
                trailing: const Icon(
                  CupertinoIcons.minus_circle,
                  size: 18,
                  color: AmColors.pink,
                ),
                onTap: () {
                  c.unexposeProperty(ex.id);
                  atualiza();
                },
              ),
          ],
          if (pro) ...[
            const SecaoDoEstudio('Dados (CSV)'),
            LinhaDoEstudio(
              key: const ValueKey('projeto-dados'),
              icone: CupertinoIcons.table,
              titulo: p.data == null ? 'Carregar CSV' : p.data!.name,
              subtitulo: p.data == null
                  ? 'Colunas viram fontes para textos (vincular no painel do texto)'
                  : '${p.data!.columns.length} colunas · ${p.data!.rows.length} linhas · toque para trocar',
              chevron: true,
              onTap: () async {
                try {
                  final r = await FilePicker.platform.pickFiles(
                    type: FileType.custom,
                    allowedExtensions: const ['csv', 'txt'],
                  );
                  final caminho = r?.files.single.path;
                  if (caminho == null) return;
                  final conteudo = await File(caminho).readAsString();
                  c.setDataSource(
                    parseCsv(conteudo, name: r!.files.single.name),
                  );
                  c.applyDataBindings();
                } catch (_) {}
                atualiza();
              },
            ),
            if (p.data != null)
              LinhaDoEstudio(
                key: const ValueKey('projeto-dados-remover'),
                icone: CupertinoIcons.clear,
                titulo: 'Remover dados',
                perigo: true,
                onTap: () {
                  c.setDataSource(null);
                  atualiza();
                },
              ),
          ],
          const SecaoDoEstudio('Ajuda'),
          LinhaDoEstudio(
            key: const ValueKey('projeto-ajuda'),
            icone: CupertinoIcons.question_circle,
            titulo: 'Como usar o editor',
            subtitulo: 'Guia rapido, com busca',
            chevron: true,
            onTap: () => Navigator.of(context).push(
              MaterialPageRoute<void>(
                builder: (_) => const QuickGuideScreen(initialQuery: ''),
              ),
            ),
          ),
          LinhaDoEstudio(
            key: const ValueKey('projeto-dicas'),
            icone: CupertinoIcons.lightbulb,
            titulo: 'Ver as dicas de novo',
            subtitulo:
                'As quatro dicas de primeiro uso voltam ao abrir o editor',
            onTap: () async {
              await OnboardingPrefs.marcar(ref, false);
              if (ctx.mounted) Navigator.of(ctx).pop();
            },
          ),
          if (pro)
            LinhaDoEstudio(
              key: const ValueKey('projeto-diagnostico'),
              icone: CupertinoIcons.waveform_path_ecg,
              titulo: 'Diagnostico na tela',
              subtitulo:
                  'Marcha, composicoes por segundo, memoria e o motor 3D.',
              ligado: diag,
              onTap: () {
                ref.read(debugOverlayProvider.notifier).state = !diag;
                atualiza();
              },
            ),
          const SizedBox(height: 12),
        ],
      );
    },
  );
}

/// Uma linha de chips: rotulo a esquerda, opcoes a direita.
class _Chips<T> extends StatelessWidget {
  const _Chips({
    required this.chave,
    required this.rotulo,
    required this.opcoes,
    required this.rotuloDe,
    required this.selecionado,
    required this.onEscolher,
  });

  final String chave;
  final String rotulo;
  final List<T> opcoes;
  final String Function(T) rotuloDe;
  final T? selecionado;
  final ValueChanged<T> onEscolher;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(18, 6, 18, 6),
    child: Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: 84,
          child: Padding(
            padding: const EdgeInsets.only(top: 7),
            child: AppText(
              rotulo,
              style: const TextStyle(fontSize: 13, color: AmColors.muted),
            ),
          ),
        ),
        Expanded(
          child: Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (final o in opcoes)
                GestureDetector(
                  key: ValueKey('$chave-${rotuloDe(o)}'),
                  behavior: HitTestBehavior.opaque,
                  onTap: () => onEscolher(o),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 11,
                      vertical: 7,
                    ),
                    decoration: BoxDecoration(
                      color: o == selecionado
                          ? AmColors.actionDim
                          : AmColors.chip,
                      borderRadius: BorderRadius.circular(9),
                      border: o == selecionado
                          ? Border.all(color: AmColors.action)
                          : null,
                    ),
                    child: AppText(
                      rotuloDe(o),
                      style: TextStyle(
                        fontSize: 12.5,
                        fontWeight: FontWeight.w600,
                        color: o == selecionado
                            ? AmColors.action
                            : AmColors.text,
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
