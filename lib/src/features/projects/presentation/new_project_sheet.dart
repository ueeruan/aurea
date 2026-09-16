import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_theme.dart';
import '../../editor/domain/video_project.dart';
import '../../settings/application/settings_controller.dart';
import '../domain/project_presets.dart';

/// Abre a folha de criacao e devolve o projeto configurado (ou null).
///
/// [nomeSugerido] e o nome que o projeto recebe se a pessoa nao
/// escrever nenhum ("Projeto 3"): criar um projeto nao pode exigir
/// inventar um nome antes de ver o que se esta criando.
Future<VideoProject?> showNewProjectSheet(
  BuildContext context, {
  String? presetAspectKey,
  String? nomeSugerido,
}) {
  return showModalBottomSheet<VideoProject>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AppColors.surface,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(24)),
    ),
    builder: (_) => _NewProjectSheet(
      presetAspectKey: presetAspectKey,
      nomeSugerido: nomeSugerido,
    ),
  );
}

/// O quadro que sai de uma proporcao e uma resolucao, pela MESMA regra
/// do projeto ([VideoProject.outputWidth]): a resolucao e o lado menor,
/// entao 1080p em 9:16 e 1080 × 1920, e nao 608 × 1080.
({int largura, int altura}) quadroDoFormato(double ratio, int resolucao) =>
    ratio >= 1
    ? (largura: (resolucao * ratio).round(), altura: resolucao)
    : (largura: resolucao, altura: (resolucao / ratio).round());

/// A FOLHA DE PROJETO NOVO.
///
/// O que se escolhe primeiro e o FORMATO, e ele se escolhe olhando: a
/// moldura no alto e desenhada na proporcao real e muda junto com o
/// toque. Antes eram quatro caixas com icone, e a pessoa lia "4:5" sem
/// saber que forma isso tinha. Nome, resolucao e fps vem depois, e o
/// nome ja vem preenchido.
class _NewProjectSheet extends ConsumerStatefulWidget {
  const _NewProjectSheet({this.presetAspectKey, this.nomeSugerido});

  final String? presetAspectKey;
  final String? nomeSugerido;

  @override
  ConsumerState<_NewProjectSheet> createState() => _NewProjectSheetState();
}

class _NewProjectSheetState extends ConsumerState<_NewProjectSheet> {
  late final TextEditingController _nameController;
  late String _aspectKey;
  late int _fps;
  late int _resolution;

  /// MEDIDA LIVRE: quem sabe exatamente o quadro que quer digita os dois
  /// numeros, e a proporcao sai deles.
  bool _livre = false;
  final _larguraLivre = TextEditingController(text: '1080');
  final _alturaLivre = TextEditingController(text: '1350');

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
    _larguraLivre.dispose();
    _alturaLivre.dispose();
    super.dispose();
  }

  ({int largura, int altura}) _quadroLivre() {
    final w = (int.tryParse(_larguraLivre.text) ?? 1080).clamp(64, 7680);
    final h = (int.tryParse(_alturaLivre.text) ?? 1350).clamp(64, 7680);
    return (largura: w, altura: h);
  }

  void _create() {
    final name = _nameController.text.trim();
    final livre = _quadroLivre();
    final project = VideoProject.empty(
      name.isEmpty ? (widget.nomeSugerido ?? 'Projeto sem titulo') : name,
      aspectRatio: _livre
          ? livre.largura / livre.altura
          : ProjectPresets.aspectByKey(_aspectKey).ratio,
      fps: _fps,
      resolutionHeight: _livre
          ? (livre.largura < livre.altura ? livre.largura : livre.altura)
          : _resolution,
    );
    Navigator.of(context).pop(project);
  }

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.of(context).viewInsets.bottom;
    final aspect = ProjectPresets.aspectByKey(_aspectKey);
    final livre = _quadroLivre();
    final quadro = _livre ? livre : quadroDoFormato(aspect.ratio, _resolution);
    final ratioNaTela = _livre
        ? (livre.largura / livre.altura).clamp(0.2, 5.0).toDouble()
        : aspect.ratio;

    return Padding(
      padding: EdgeInsets.fromLTRB(20, 12, 20, 16 + bottomInset),
      child: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
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
            const SizedBox(height: 16),
            Row(
              crossAxisAlignment: CrossAxisAlignment.end,
              children: [
                Expanded(
                  child: AppText(
                    'Novo projeto',
                    style: Theme.of(context).textTheme.titleLarge,
                  ),
                ),
                // A FICHA, viva: muda com cada escolha.
                AppText(
                  '${quadro.largura} × ${quadro.altura} · $_fps fps',
                  key: const ValueKey('projeto-ficha'),
                  style: TextStyle(fontSize: 12.5, color: AppColors.muted),
                ),
              ],
            ),
            const SizedBox(height: 16),
            _Moldura(
              ratio: ratioNaTela,
              label: _livre
                  ? '${livre.largura} × ${livre.altura}'
                  : aspect.label,
              hint: _livre ? 'Medida livre' : aspect.hint,
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                for (final option in ProjectPresets.aspects)
                  Expanded(
                    child: _FormatoItem(
                      key: ValueKey('formato-${option.key}'),
                      option: option,
                      selected: !_livre && option.key == _aspectKey,
                      onTap: () => setState(() {
                        _livre = false;
                        _aspectKey = option.key;
                      }),
                    ),
                  ),
                // MEDIDA LIVRE: os dois numeros na mao, para quem sabe o
                // quadro exato que quer (banner, tela de LED, thumb).
                Expanded(
                  child: _FormatoItem(
                    key: const ValueKey('formato-livre'),
                    option: const AspectOption(
                      key: 'livre',
                      label: 'Livre',
                      hint: 'Você escolhe',
                      ratio: 1,
                      icon: CupertinoIcons.pencil_outline,
                    ),
                    selected: _livre,
                    onTap: () => setState(() => _livre = true),
                  ),
                ),
              ],
            ),
            if (_livre) ...[
              const SizedBox(height: 12),
              Row(
                children: [
                  Expanded(
                    child: _CampoDeMedida(
                      chave: 'livre-largura',
                      rotulo: 'Largura',
                      controller: _larguraLivre,
                      onMudou: () => setState(() {}),
                    ),
                  ),
                  const Padding(
                    padding: EdgeInsets.symmetric(horizontal: 10),
                    child: AppText('×', style: TextStyle(fontSize: 16)),
                  ),
                  Expanded(
                    child: _CampoDeMedida(
                      chave: 'livre-altura',
                      rotulo: 'Altura',
                      controller: _alturaLivre,
                      onMudou: () => setState(() {}),
                    ),
                  ),
                ],
              ),
            ],
            const SizedBox(height: 20),
            const _SectionLabel('Nome'),
            const SizedBox(height: 8),
            CupertinoTextField(
              key: const ValueKey('projeto-nome'),
              controller: _nameController,
              placeholder: widget.nomeSugerido ?? 'Nome do projeto',
              textCapitalization: TextCapitalization.sentences,
              style: TextStyle(
                fontSize: 17,
                letterSpacing: -0.2,
                color: AppColors.onDark,
              ),
              placeholderStyle: TextStyle(
                fontSize: 17,
                letterSpacing: -0.2,
                color: AppColors.muted,
              ),
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 13),
              decoration: BoxDecoration(
                color: AppColors.surfaceHigh,
                borderRadius: BorderRadius.circular(12),
              ),
              onSubmitted: (_) => _create(),
            ),
            if (!_livre) ...[
              const SizedBox(height: 18),
              const _SectionLabel('Resolução'),
              const SizedBox(height: 8),
              _Segmented<int>(
                values: ProjectPresets.resolutions,
                selected: _resolution,
                labelOf: ProjectPresets.resolutionLabel,
                onChanged: (v) => setState(() => _resolution = v),
              ),
            ],
            const SizedBox(height: 18),
            const _SectionLabel('Quadros por segundo'),
            const SizedBox(height: 8),
            _Segmented<int>(
              values: ProjectPresets.fpsOptions,
              selected: _fps,
              labelOf: (v) => '$v fps',
              onChanged: (v) => setState(() => _fps = v),
            ),
            const SizedBox(height: 24),
            SizedBox(
              height: 52,
              child: FilledButton(
                key: const ValueKey('criar-projeto'),
                onPressed: _create,
                child: const AppText('Criar projeto'),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// A MOLDURA: o formato escolhido, desenhado na proporcao real. Trocar
/// de formato anima a moldura de uma forma para a outra, para a pessoa
/// ver o que mudou em vez de ler.
class _Moldura extends StatelessWidget {
  const _Moldura({
    required this.ratio,
    required this.label,
    required this.hint,
  });

  final double ratio;
  final String label;
  final String hint;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 150,
      child: Center(
        child: TweenAnimationBuilder<double>(
          tween: Tween<double>(end: ratio),
          duration: const Duration(milliseconds: 240),
          curve: Curves.easeOutCubic,
          builder: (context, r, child) => AspectRatio(
            aspectRatio: r,
            child: DecoratedBox(
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(12),
                gradient: LinearGradient(
                  begin: Alignment.topLeft,
                  end: Alignment.bottomRight,
                  colors: [
                    AppColors.lime.withValues(alpha: 0.22),
                    AppColors.lime.withValues(alpha: 0.06),
                  ],
                ),
                border: Border.all(
                  color: AppColors.lime.withValues(alpha: 0.6),
                  width: 1.2,
                ),
              ),
              child: child,
            ),
          ),
          // Uma medida livre bem magra (64 x 1350) deixa a moldura com
          // 30 px de largura: o texto ENCOLHE em vez de estourar.
          child: Center(
            child: FittedBox(
              fit: BoxFit.scaleDown,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  AppText(
                    label,
                    key: const ValueKey('moldura-formato'),
                    style: TextStyle(
                      fontSize: 22,
                      fontWeight: FontWeight.w700,
                      letterSpacing: -0.3,
                      color: AppColors.onDark,
                    ),
                  ),
                  AppText(
                    hint,
                    style: TextStyle(fontSize: 12, color: AppColors.muted),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Um formato para escolher: uma moldura pequena na proporcao dele e o
/// nome embaixo. Sem caixa em volta — a moldura E o desenho.
class _FormatoItem extends StatelessWidget {
  const _FormatoItem({
    super.key,
    required this.option,
    required this.selected,
    required this.onTap,
  });

  final AspectOption option;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    const lado = 28.0;
    final r = option.ratio;
    final w = r >= 1 ? lado : lado * r;
    final h = r >= 1 ? lado / r : lado;
    final cor = selected ? AppColors.lime : AppColors.muted;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 6),
        child: Column(
          children: [
            SizedBox(
              height: lado,
              child: Center(
                child: AnimatedContainer(
                  duration: const Duration(milliseconds: 160),
                  width: w,
                  height: h,
                  decoration: BoxDecoration(
                    borderRadius: BorderRadius.circular(4),
                    color: selected
                        ? AppColors.lime.withValues(alpha: 0.2)
                        : Colors.transparent,
                    border: Border.all(color: cor, width: selected ? 2 : 1.2),
                  ),
                ),
              ),
            ),
            const SizedBox(height: 7),
            AppText(
              option.label,
              style: TextStyle(
                fontSize: 12.5,
                fontWeight: FontWeight.w600,
                letterSpacing: -0.1,
                color: selected ? AppColors.lime : AppColors.onDark,
              ),
            ),
            AppText(
              option.hint,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 10, color: AppColors.muted),
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
    return AppText(
      text.toUpperCase(),
      style: TextStyle(
        fontSize: 12,
        letterSpacing: 0.6,
        fontWeight: FontWeight.w500,
        color: AppColors.muted,
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
              child: AppText(
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

/// UM DOS DOIS NUMEROS da medida livre.
class _CampoDeMedida extends StatelessWidget {
  const _CampoDeMedida({
    required this.chave,
    required this.rotulo,
    required this.controller,
    required this.onMudou,
  });

  final String chave;
  final String rotulo;
  final TextEditingController controller;
  final VoidCallback onMudou;

  @override
  Widget build(BuildContext context) => Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      AppText(rotulo, style: TextStyle(fontSize: 11, color: AppColors.muted)),
      const SizedBox(height: 4),
      CupertinoTextField(
        key: ValueKey(chave),
        controller: controller,
        keyboardType: TextInputType.number,
        style: TextStyle(fontSize: 16, color: AppColors.onDark),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: AppColors.surfaceHigh,
          borderRadius: BorderRadius.circular(10),
        ),
        onChanged: (_) => onMudou(),
      ),
    ],
  );
}
