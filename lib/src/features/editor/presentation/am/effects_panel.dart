import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/blob_track_service.dart';
import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../domain/layer.dart';
import '../../application/playback_controller.dart';
import '../../domain/effect.dart';
import '../../domain/effect_preset.dart';
import '../../domain/keyframe.dart';
import 'am_colors.dart';
import 'color_picker_sheet.dart';
import 'am_widgets.dart';
import 'curve_panel.dart';
import '../../application/effect_preset_store.dart';
import '../../../help/presentation/quick_guide_screen.dart';

/// Painel "Efeitos": LISTA VERTICAL de blocos colapsaveis, um por efeito.
/// Cabecalho = chevron (colapsa) + nome + "..." (menu) + lixeira. Cada
/// linha de parametro = ponto verde (tem keyframes) + nome + regua de
/// ticks + valor alinhado (par X/Y numa linha so) + diamante ("keyframe
/// em tudo"). O trilho esquerdo tem voltar e o diamante do parametro
/// selecionado.
class EffectsPanel extends ConsumerStatefulWidget {
  const EffectsPanel({super.key, required this.playback, required this.onBack});

  final PlaybackController playback;
  final VoidCallback onBack;

  @override
  ConsumerState<EffectsPanel> createState() => _EffectsPanelState();
}

class _EffectsPanelState extends ConsumerState<EffectsPanel> {
  String? _selectedParam;

  /// Efeitos RECOLHIDOS (nao os expandidos): assim todo efeito nasce
  /// aberto — inclusive os que chegam depois por addEffect/applyPreset —
  /// sem precisar semear o conjunto a cada build.
  final Set<String> _recolhidos = <String>{};

  bool _expandido(String effectId) => !_recolhidos.contains(effectId);

  void _alternarExpandido(String effectId) {
    setState(() {
      if (!_recolhidos.remove(effectId)) _recolhidos.add(effectId);
    });
  }

  /// Menu "..." do efeito: SO o que o controller sabe fazer de verdade
  /// (mover, ligar/desligar, resetar, remover). Duplicar e curva de
  /// parametro ficam de fora porque nao existem no EditorController.
  /// Pede o nome e guarda o efeito como preset da pessoa.
  Future<void> _salvarComoPreset(
    BuildContext context,
    String layerId,
    EffectInstance effect,
  ) async {
    final nome = await _pedirNome(context, effect.spec.name);
    if (nome == null || nome.trim().isEmpty || !context.mounted) return;
    final project = ref.read(editorControllerProvider);
    final layer = project.layerById(layerId);
    if (layer == null) return;
    final controller = ref.read(editorControllerProvider.notifier);
    final preset = saveEffectPreset(
      name: nome.trim(),
      effects: [effect],
      layerStart: layer.startTime,
      layerDuration: layer.duration,
      layerSize: controller.layerBoxSize(layer, widget.playback.time.value),
    );
    await EffectPresetStore.instance.add(preset);
    if (!context.mounted) return;
    AureaSnack.show(
      context,
      'Preset "${preset.name}" salvo para todos os projetos',
    );
  }

  Future<String?> _pedirNome(BuildContext context, String inicial) {
    final campo = TextEditingController(text: inicial);
    return showCupertinoDialog<String>(
      context: context,
      builder: (dialogContext) => CupertinoAlertDialog(
        title: const Text('Nome do preset'),
        content: Padding(
          padding: const EdgeInsets.only(top: 12),
          child: CupertinoTextField(
            controller: campo,
            autofocus: true,
            onSubmitted: (v) => Navigator.of(dialogContext).pop(v),
          ),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(dialogContext).pop(),
            child: const Text('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(dialogContext).pop(campo.text),
            child: const Text('Salvar'),
          ),
        ],
      ),
    );
  }

  /// RESETAR OS PARAMETROS deste efeito no instante atual.
  ///
  /// O tempo e lido NO TOQUE: o painel nao pausa a reproducao, e o
  /// keyframe do reset tem de cair onde o cabecote esta agora. As N
  /// edicoes viram um undo so (coalesce de 450 ms).
  void _resetarEfeito(String layerId, EffectInstance effect) {
    final controller = ref.read(editorControllerProvider.notifier);
    final agora = widget.playback.time.value;
    for (final e in effect.spec.params.entries) {
      controller.editEffectParam(
        layerId,
        effect.id,
        e.key,
        agora,
        e.value.initial,
      );
    }
    if (effect.spec.hasColor) {
      controller.setEffectColor(layerId, effect.id, const Color(0xFFFF5566));
    }
  }

  /// CATALOGO (PR-C4): o gargalo de quem tem muitos efeitos nao e ter —
  /// e ACHAR. Busca com sinonimos, chips por categoria com contador,
  /// presets de fabrica, e aplicacao em dois toques.
  Future<void> _addEffect(BuildContext context, String layerId) async {
    final controller = ref.read(editorControllerProvider.notifier);
    final search = TextEditingController();
    var query = '';
    String? category;
    var showPresets = false;

    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: AmColors.panel,
      isScrollControlled: true,
      constraints: BoxConstraints(
        maxHeight: MediaQuery.of(context).size.height * 0.62,
      ),
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      builder: (sheetContext) => StatefulBuilder(
        builder: (sheetContext, setSheetState) {
          final results = query.isNotEmpty
              ? searchEffects(query)
              : (category == null
                    ? effectSpecs.keys.toList()
                    : effectsInCategory(category!));

          return SafeArea(
            child: Padding(
              padding: EdgeInsets.fromLTRB(
                16,
                14,
                16,
                10 + MediaQuery.of(sheetContext).viewInsets.bottom,
              ),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Expanded(
                        child: Text(
                          'Efeitos',
                          style: TextStyle(
                            fontSize: 18,
                            fontWeight: FontWeight.w700,
                            color: AmColors.text,
                          ),
                        ),
                      ),
                      GestureDetector(
                        onTap: () =>
                            setSheetState(() => showPresets = !showPresets),
                        child: Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 7,
                          ),
                          decoration: BoxDecoration(
                            color: showPresets
                                ? AmColors.accentDim
                                : AmColors.chip,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: const Text(
                            'Presets',
                            style: TextStyle(
                              fontSize: 12,
                              color: AmColors.accent,
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 10),
                  CupertinoTextField(
                    controller: search,
                    placeholder: 'buscar (glow, rgb split, pixelate...)',
                    placeholderStyle: const TextStyle(
                      fontSize: 13,
                      color: AmColors.muted,
                    ),
                    style: const TextStyle(fontSize: 14, color: AmColors.text),
                    prefix: const Padding(
                      padding: EdgeInsets.only(left: 10),
                      child: Icon(
                        CupertinoIcons.search,
                        size: 16,
                        color: AmColors.muted,
                      ),
                    ),
                    padding: const EdgeInsets.symmetric(
                      horizontal: 10,
                      vertical: 10,
                    ),
                    decoration: BoxDecoration(
                      color: AmColors.chip,
                      borderRadius: BorderRadius.circular(10),
                    ),
                    onChanged: (v) => setSheetState(() {
                      query = v;
                      showPresets = false;
                    }),
                  ),
                  const SizedBox(height: 10),
                  if (!showPresets && query.isEmpty)
                    SizedBox(
                      height: 34,
                      child: ListView(
                        scrollDirection: Axis.horizontal,
                        children: [
                          for (final c in [null, ...effectCategories])
                            Padding(
                              padding: const EdgeInsets.only(right: 8),
                              child: GestureDetector(
                                onTap: () => setSheetState(() => category = c),
                                child: Container(
                                  padding: const EdgeInsets.symmetric(
                                    horizontal: 12,
                                    vertical: 8,
                                  ),
                                  decoration: BoxDecoration(
                                    color: category == c
                                        ? AmColors.accentDim
                                        : AmColors.chip,
                                    borderRadius: BorderRadius.circular(9),
                                  ),
                                  child: Text(
                                    c == null
                                        ? 'Todos ${effectSpecs.length}'
                                        : '$c ${effectsInCategory(c).length}',
                                    style: const TextStyle(
                                      fontSize: 12,
                                      color: AmColors.accent,
                                    ),
                                  ),
                                ),
                              ),
                            ),
                        ],
                      ),
                    ),
                  const SizedBox(height: 8),
                  Expanded(
                    child: showPresets
                        // PREGUICOSO: `ListView(children:)` constroi
                        // TUDO de uma vez. Com `builder`, so o visivel
                        // (mais uma margem) existe.
                        ? ListView.builder(
                            itemCount: factoryPresets().length,
                            itemBuilder: (context, i) {
                              final p = factoryPresets()[i];
                              return ListTile(
                                leading: const Icon(
                                  CupertinoIcons.square_stack_3d_down_right,
                                  color: AmColors.accent,
                                  size: 20,
                                ),
                                title: Text(
                                  p.name,
                                  style: const TextStyle(
                                    color: AmColors.text,
                                    fontSize: 14,
                                  ),
                                ),
                                subtitle: Text(
                                  '${p.category} · '
                                  '${p.effects.length} efeito(s)',
                                  style: const TextStyle(
                                    fontSize: 11,
                                    color: AmColors.muted,
                                  ),
                                ),
                                onTap: () {
                                  controller.applyPreset(
                                    layerId,
                                    p,
                                    at: widget.playback.time.value,
                                  );
                                  Navigator.of(sheetContext).pop();
                                },
                              );
                            },
                          )
                        // ESTADO VAZIO com aparencia propria, e a lista
                        // preguicosa: trinta e oito ListTile construidos
                        // de uma vez e trabalho jogado fora, porque so
                        // meia duzia cabe na tela.
                        : results.isEmpty
                        ? const Center(
                            child: Padding(
                              padding: EdgeInsets.all(24),
                              child: Column(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  Icon(
                                    CupertinoIcons.search,
                                    size: 30,
                                    color: AmColors.muted,
                                  ),
                                  SizedBox(height: 10),
                                  Text(
                                    'Nada encontrado. '
                                    'Tente "glow", "rgb", "pixel" '
                                    'ou "shake".',
                                    textAlign: TextAlign.center,
                                    style: TextStyle(
                                      fontSize: 13,
                                      height: 1.4,
                                      color: AmColors.muted,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                          )
                        : ListView.builder(
                            itemCount: results.length,
                            itemBuilder: (context, i) {
                              final type = results[i];
                              final spec = effectSpecs[type]!;
                              return ListTile(
                                dense: true,
                                leading: const Icon(
                                  CupertinoIcons.wand_stars,
                                  color: AmColors.accent,
                                  size: 20,
                                ),
                                title: Text(
                                  spec.name,
                                  style: const TextStyle(
                                    color: AmColors.text,
                                    fontSize: 14,
                                  ),
                                ),
                                subtitle: Text(
                                  '${spec.category}'
                                  '${spec.cost > 1 ? ' · custo ${spec.cost}' : ''}',
                                  style: const TextStyle(
                                    fontSize: 11,
                                    color: AmColors.muted,
                                  ),
                                ),
                                onTap: () {
                                  controller.addEffect(layerId, type);
                                  Navigator.of(sheetContext).pop();
                                },
                              );
                            },
                          ),
                  ),
                ],
              ),
            ),
          );
        },
      ),
    );
    search.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer == null || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }
    final controller = ref.read(editorControllerProvider.notifier);

    return ColoredBox(
      color: AmColors.panel,
      // Reavalia os valores conforme o playhead anda. Envolve o Row
      // inteiro porque o diamante do trilho tambem precisa saber se ha
      // keyframe no cabecote.
      child: ValueListenableBuilder<Duration>(
        valueListenable: widget.playback.time,
        builder: (context, t, _) {
          final local = layer.localTime(t);

          // Parametro selecionado -> efeito + chaves (par X/Y vem como
          // 'x|y'). Resolve por id, nunca por indice: o efeito pode ter
          // sido removido ou reordenado desde a selecao.
          final partes = _selectedParam?.split('/');
          EffectInstance? sel;
          final chaves = <String>[];
          if (partes != null && partes.length == 2) {
            for (final e in layer.effects) {
              if (e.id == partes[0]) sel = e;
            }
            chaves.addAll(partes[1].split('|'));
          }
          // KEYFRAME UNIVERSAL: o diamante e do EFEITO inteiro, como
          // no Alight Motion — um keyframe guarda todos os parametros.
          final selAnimado = sel != null && sel.hasAnimation;
          final selKfAqui = sel != null && sel.hasKeyframeAt(local);

          return Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              SingleChildScrollView(
                child: Column(
                  children: [
                    AmRailButton(
                      onTap: widget.onBack,
                      child: const Icon(
                        CupertinoIcons.chevron_back,
                        size: 24,
                        color: AmColors.text,
                      ),
                    ),
                    // Diamante do parametro selecionado, como nos outros
                    // paineis: apagado e inerte quando nada esta selecionado.
                    // Com o par 'x|y' em estado misto (so um eixo com
                    // keyframe aqui, cenario da regua arrastada) o toggle
                    // cego trocaria o keyframe de eixo. Regra: diamante vazio
                    // COMPLETA (so onde falta), diamante cheio LIMPA os dois.
                    AmRailButton(
                      onTap: sel == null
                          ? null
                          : () =>
                                controller.toggleEffectKeyframe(id, sel!.id, t),
                      child: AmDiamondAdd(
                        active: selAnimado,
                        filled: selKfAqui,
                      ),
                    ),
                    AmRailButton(
                      tooltip: 'Como usar os efeitos',
                      onTap: () => Navigator.of(context).push(
                        MaterialPageRoute<void>(
                          builder: (_) => QuickGuideScreen(
                            initialQuery: sel?.spec.name ?? '',
                          ),
                        ),
                      ),
                      child: const Icon(
                        CupertinoIcons.question_circle,
                        size: 23,
                        color: AmColors.text,
                      ),
                    ),
                    // CURVA DO EFEITO: o mesmo editor de curvas dos outros
                    // paineis, sobre o keyframe universal — o easing do
                    // trecho vale para todos os parametros de uma vez.
                    AmRailButton(
                      onTap: sel == null || !sel.hasAnimation
                          ? null
                          : () => showTrackCurveSheet(
                              context,
                              ref,
                              widget.playback,
                              label: sel!.spec.name,
                              layerId: id,
                              trackOf: (layer) {
                                for (final e in layer.effects) {
                                  if (e.id != sel!.id) continue;
                                  for (final t in e.params.values) {
                                    if (t.isAnimated) return t;
                                  }
                                }
                                return null;
                              },
                              onSetEase: (seg, e) => controller
                                  .setEffectSegmentEase(id, sel!.id, seg, e),
                              onSetEaseAll: (e) => controller
                                  .applyEaseToAllEffectSegments(id, sel!.id, e),
                            ),
                      child: Opacity(
                        opacity: selAnimado ? 1 : 0.32,
                        child: AmCurveIcon(
                          color: selAnimado ? AmColors.text : AmColors.muted,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              Expanded(
                // REORDENAR PELA ALCA. A ordem dos efeitos e a ordem da
                // conta — desfocar depois de brilhar nao e o mesmo que
                // brilhar depois de desfocar — e arrastar pela alca e o
                // jeito de ver a ordem trocar, em vez de contar toques de
                // "mover para cima".
                child: ReorderableListView(
                  padding: const EdgeInsets.fromLTRB(4, 8, 16, 16),
                  buildDefaultDragHandles: false,
                  // onReorderItem ja entrega o destino descontando o
                  // item retirado — o onReorder antigo nao descontava.
                  onReorderItem: (de, para) {
                    if (para == de) return;
                    controller.reorderEffect(
                      id,
                      layer.effects[de].id,
                      para - de,
                    );
                  },
                  footer: Column(
                    children: [
                      // ANALISAR: o Blob Tracker precisa varrer o video
                      // uma vez antes de desenhar. Sem o comando, o efeito
                      // so mostra o rastreio simulado — e a pessoa nao
                      // teria como saber que falta um passo.
                      for (final effect in layer.effects)
                        if (effect.type == EffectType.blobTracker &&
                            layer is VideoLayer)
                          _BotaoAnalisar(
                            effectId: effect.id,
                            layerId: id,
                            controller: controller,
                          ),
                      const SizedBox(height: 8),
                      GestureDetector(
                        onTap: () => _addEffect(context, id),
                        child: Container(
                          padding: const EdgeInsets.symmetric(vertical: 13),
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: AmColors.chip,
                            borderRadius: BorderRadius.circular(12),
                          ),
                          child: const Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(
                                CupertinoIcons.plus,
                                size: 17,
                                color: AmColors.accent,
                              ),
                              SizedBox(width: 8),
                              Text(
                                'Adicionar efeito',
                                style: TextStyle(
                                  fontSize: 14,
                                  fontWeight: FontWeight.w600,
                                  color: AmColors.accent,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ],
                  ),
                  children: [
                    for (var i = 0; i < layer.effects.length; i++)
                      _EffectCard(
                        key: ValueKey(layer.effects[i].id),
                        index: i,
                        effect: layer.effects[i],
                        local: local,
                        expanded: _expandido(layer.effects[i].id),
                        selectedParam: _selectedParam,
                        onToggleExpanded: () =>
                            _alternarExpandido(layer.effects[i].id),
                        onSubir: i == 0
                            ? null
                            : () => controller.reorderEffect(
                                id,
                                layer.effects[i].id,
                                -1,
                              ),
                        onDescer: i == layer.effects.length - 1
                            ? null
                            : () => controller.reorderEffect(
                                id,
                                layer.effects[i].id,
                                1,
                              ),
                        onDuplicar: () =>
                            controller.duplicateEffect(id, layer.effects[i].id),
                        onResetar: () => _resetarEfeito(id, layer.effects[i]),
                        onSalvarPreset: () =>
                            _salvarComoPreset(context, id, layer.effects[i]),
                        onSelectParam: (p) =>
                            setState(() => _selectedParam = p),
                        onParam: (key, v) => controller.editEffectParam(
                          id,
                          layer.effects[i].id,
                          key,
                          t,
                          v,
                        ),
                        // Qualquer diamante do cartao e o diamante do
                        // efeito: keyframe universal neste instante.
                        onParamKeyframe: (_) => controller.toggleEffectKeyframe(
                          id,
                          layer.effects[i].id,
                          t,
                        ),
                        onToggleEnabled: () => controller.toggleEffectEnabled(
                          id,
                          layer.effects[i].id,
                        ),
                        onDepth: (d) => controller.setEffectDepth(
                          id,
                          layer.effects[i].id,
                          d,
                        ),
                        onPronto: (pr) => controller.applyEffectPronto(
                          id,
                          layer.effects[i].id,
                          pr,
                        ),
                        onColor: (c) => controller.setEffectColor(
                          id,
                          layer.effects[i].id,
                          c,
                        ),
                        onExtraColor: (k, c) => controller.setEffectExtraColor(
                          id,
                          layer.effects[i].id,
                          k,
                          c,
                        ),
                        onRemove: () =>
                            controller.removeEffect(id, layer.effects[i].id),
                      ),
                  ],
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _EffectCard extends StatelessWidget {
  const _EffectCard({
    super.key,
    required this.index,
    required this.effect,
    required this.local,
    required this.expanded,
    required this.selectedParam,
    required this.onToggleExpanded,
    required this.onSubir,
    required this.onDescer,
    required this.onDuplicar,
    required this.onResetar,
    required this.onSalvarPreset,
    required this.onSelectParam,
    required this.onParam,
    required this.onParamKeyframe,
    required this.onColor,
    required this.onExtraColor,
    required this.onRemove,
    required this.onToggleEnabled,
    required this.onDepth,
    required this.onPronto,
  });

  /// Posicao na lista: e o que a alca de arrastar entrega ao reordenar.
  final int index;

  final EffectInstance effect;
  final Duration local;
  final bool expanded;
  final String? selectedParam;
  final VoidCallback onToggleExpanded;

  /// Nulo quando o efeito ja esta na ponta: o botao fica esmaecido, nao
  /// some — botao que some troca o lugar dos vizinhos.
  final VoidCallback? onSubir;
  final VoidCallback? onDescer;
  final VoidCallback onDuplicar;
  final VoidCallback onResetar;
  final VoidCallback onSalvarPreset;
  final ValueChanged<String> onSelectParam;
  final void Function(String key, double value) onParam;
  final void Function(String key) onParamKeyframe;
  final ValueChanged<Color> onColor;
  final void Function(int index, Color color) onExtraColor;
  final VoidCallback onRemove;
  final VoidCallback onToggleEnabled;
  final ValueChanged<EffectDepth> onDepth;
  final ValueChanged<EffectPronto> onPronto;

  /// Linhas de parametro. O par X/Y de um ponto (ParamKind.point) vira
  /// UMA linha com dois valores. Pareia por ADJACENCIA + kind, nunca por
  /// nome: 'tile_center'/'tile_center_y' nao segue o sufixo X.
  List<Widget> _linhas() {
    final spec = effect.spec;
    // MONTAR mostra no maximo tres numeros, com o nome humano; AVANCADO
    // mostra a ficha inteira. PRONTO nao mostra numero nenhum.
    if (spec.temProfundidades && effect.depth == EffectDepth.pronto) {
      return const [];
    }
    final entradas = spec.temProfundidades && effect.depth == EffectDepth.montar
        ? [
            for (final k in spec.montar)
              if (spec.params[k] != null) MapEntry(k, spec.params[k]!),
          ]
        : spec.params.entries.toList();
    final linhas = <Widget>[];
    for (var i = 0; i < entradas.length; i++) {
      final entry = entradas[i];
      final ehPar =
          entry.value.kind == ParamKind.point &&
          i + 1 < entradas.length &&
          entradas[i + 1].value.kind == ParamKind.point;
      if (ehPar) {
        final x = entry;
        final y = entradas[i + 1];
        final paramKey = '${effect.id}/${x.key}|${y.key}';
        final xt = effect.track(x.key);
        final yt = effect.track(y.key);
        linhas.add(
          _PointRow(
            paramKey: paramKey,
            label: x.value.label.replaceFirst(RegExp(r' X$'), ''),
            xTrack: xt,
            yTrack: yt,
            local: local,
            xMin: x.value.min,
            xMax: x.value.max,
            yMin: y.value.min,
            yMax: y.value.max,
            selected: selectedParam == paramKey,
            onSelect: onSelectParam,
            onChangedX: (v) => onParam(x.key, v),
            onChangedY: (v) => onParam(y.key, v),
            // Dois toggles separados, coalescidos num undo so pelo
            // controller (450 ms).
            onKeyframe: () => onParamKeyframe(x.key),
          ),
        );
        i++;
        continue;
      }
      // PR-C1: cada TIPO de parametro ganha seu controle. Antes so
      // havia numero, e todo efeito que precisava de escolha ou
      // ponto ficava visivel e inerte.
      linhas.add(switch (entry.value.kind) {
        ParamKind.choice => _ChoiceRow(
          label: entry.value.label,
          options: entry.value.options,
          value: effect
              .paramAt(entry.key, local)
              .round()
              .clamp(0, entry.value.options.length - 1),
          onChanged: (i) => onParam(entry.key, i.toDouble()),
        ),
        ParamKind.seed => _SeedRow(
          label: entry.value.label,
          value: effect.paramAt(entry.key, local),
          onChanged: (v) => onParam(entry.key, v),
        ),
        ParamKind.toggle => _ToggleRow(
          label: entry.value.label,
          value: effect.paramAt(entry.key, local) > 0.5,
          onChanged: (v) => onParam(entry.key, v ? 1.0 : 0.0),
        ),
        // Numero (e ponto solteiro) usam a regua; o ponto vem em 0..1.
        _ => _ParamRow(
          paramKey: '${effect.id}/${entry.key}',
          label: entry.value.label,
          track: effect.track(entry.key),
          kfAqui: effect.hasKeyframeAt(local),
          animado: effect.hasAnimation,
          local: local,
          min: entry.value.min,
          max: entry.value.max,
          selected: selectedParam == '${effect.id}/${entry.key}',
          onSelect: onSelectParam,
          onChanged: (v) => onParam(entry.key, v),
          onKeyframe: () => onParamKeyframe(entry.key),
        ),
      });
    }
    return linhas;
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.fromLTRB(14, 10, 10, 12),
      decoration: BoxDecoration(
        color: AmColors.panelHigh,
        borderRadius: BorderRadius.circular(14),
      ),
      child: Opacity(
        // Desligado continua visivel, so esmaecido.
        opacity: effect.enabled ? 1 : 0.45,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                // So chevron + nome colapsam: um toque em "..." ou na
                // lixeira nao pode fechar o bloco junto.
                Expanded(
                  child: GestureDetector(
                    behavior: HitTestBehavior.opaque,
                    onTap: onToggleExpanded,
                    child: Row(
                      children: [
                        Icon(
                          expanded
                              ? CupertinoIcons.chevron_down
                              : CupertinoIcons.chevron_right,
                          size: 14,
                          color: AmColors.text,
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: Text(
                            effect.spec.name,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              fontSize: 17,
                              fontWeight: FontWeight.w700,
                              color: AmColors.text,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                // O OLHO FICA NO CABECALHO, a um toque. Ligar e desligar
                // um efeito para comparar e a acao mais frequente que
                // existe aqui — escondida no menu viraria dois toques por
                // comparacao, e comparar e o que se faz o tempo todo.
                GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: onToggleEnabled,
                  child: Icon(
                    effect.enabled
                        ? CupertinoIcons.eye
                        : CupertinoIcons.eye_slash,
                    size: 22,
                    color: effect.enabled ? AmColors.text : AmColors.muted,
                  ),
                ),
                const SizedBox(width: 14),
                // ORDEM E DUPLICAR NA PROPRIA LINHA.
                //
                // A ordem dos efeitos e o resultado: Blur depois de Glow
                // nao e a mesma imagem que Glow depois de Blur. Isso
                // morava dentro de um tres pontinhos.
                GestureDetector(
                  onTap: onSubir,
                  child: Opacity(
                    opacity: onSubir == null ? 0.32 : 1,
                    child: const Icon(
                      CupertinoIcons.chevron_up,
                      size: 20,
                      color: AmColors.text,
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                GestureDetector(
                  onTap: onDescer,
                  child: Opacity(
                    opacity: onDescer == null ? 0.32 : 1,
                    child: const Icon(
                      CupertinoIcons.chevron_down,
                      size: 20,
                      color: AmColors.text,
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                GestureDetector(
                  onTap: onDuplicar,
                  child: const Icon(
                    CupertinoIcons.plus_square_on_square,
                    size: 20,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(width: 12),
                GestureDetector(
                  onTap: onRemove,
                  child: const Icon(
                    CupertinoIcons.trash,
                    size: 22,
                    color: AmColors.text,
                  ),
                ),
                const SizedBox(width: 10),
                // A ALCA: so ela arrasta. O resto da linha continua
                // tocando, colapsando e abrindo menu.
                ReorderableDragStartListener(
                  index: index,
                  child: const Padding(
                    padding: EdgeInsets.symmetric(vertical: 6),
                    child: Icon(
                      CupertinoIcons.line_horizontal_3,
                      size: 22,
                      color: AmColors.muted,
                    ),
                  ),
                ),
              ],
            ),
            // Recolhido: nem constroi o corpo.
            if (expanded) ...[
              const SizedBox(height: 8),
              // OS DOIS COMANDOS QUE FALTAVAM, VISIVEIS.
              //
              // Resetar e salvar como preset moravam dentro do tres
              // pontinhos da linha. Ficam no cartao aberto, que e onde os
              // parametros estao — e onde faz sentido zerar ou guardar.
              Wrap(
                spacing: 6,
                runSpacing: 6,
                children: [
                  _ComandoDoEfeito(
                    rotulo: 'Resetar',
                    icone: CupertinoIcons.arrow_counterclockwise,
                    onTap: onResetar,
                  ),
                  _ComandoDoEfeito(
                    rotulo: 'Salvar preset',
                    icone: CupertinoIcons.square_stack_3d_down_right,
                    onTap: onSalvarPreset,
                  ),
                ],
              ),
              const SizedBox(height: 8),
              if (effect.spec.temProfundidades) ...[
                _ProntoRow(effect: effect, onPronto: onPronto),
                const SizedBox(height: 8),
                _CaminhoRow(depth: effect.depth, onDepth: onDepth),
                if (effect.depth != EffectDepth.pronto)
                  const SizedBox(height: 10),
              ],
              ..._linhas(),
              if (effect.spec.hasColor &&
                  (!effect.spec.temProfundidades ||
                      effect.depth == EffectDepth.avancado))
                _ColorRow(effect: effect, onColor: onColor),
              for (var i = 0; i < effect.spec.extraColors; i++)
                _ColorRow(
                  effect: effect,
                  label: 'Cor ${i + 2}',
                  color: effect.extraColor(i),
                  onColor: (c) => onExtraColor(i, c),
                ),
            ],
          ],
        ),
      ),
    );
  }
}

/// Ponto verde a esquerda do nome quando o parametro tem keyframes. O
/// espaco existe SEMPRE, para o nome nao pular de lugar ao animar.
class _PontoAnimado extends StatelessWidget {
  const _PontoAnimado({required this.animated});

  final bool animated;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 12,
      child: animated
          ? Center(
              child: Container(
                width: 6,
                height: 6,
                decoration: const BoxDecoration(
                  color: AmColors.accent,
                  shape: BoxShape.circle,
                ),
              ),
            )
          : null,
    );
  }
}

/// Nome do parametro, selecionavel (fundo accentDim quando selecionado).
class _NomeParam extends StatelessWidget {
  const _NomeParam({
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    // A linha tem 58 px (altura da regua); sem altura propria o alvo do
    // nome ficava com ~30 px e metade da linha nao selecionava o
    // parametro — e selecionar e o que liga o diamante do trilho. 44 px
    // e o minimo de toque do iOS e cabe sem crescer a linha; o `opaque`
    // faz o padding transparente contar como toque.
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        width: 88,
        height: 44,
        alignment: Alignment.centerLeft,
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 6),
        decoration: BoxDecoration(
          color: selected
              ? AmColors.accentDim.withValues(alpha: 0.5)
              : Colors.transparent,
          borderRadius: BorderRadius.circular(8),
        ),
        child: Text(
          label,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(
            fontSize: 15,
            color: selected ? AmColors.accent : AmColors.muted,
          ),
        ),
      ),
    );
  }
}

/// Valor alinhado a direita com digitos tabulares: ao arrastar a regua o
/// numero nao "danca" de largura.
const _estiloValor = TextStyle(
  fontSize: 14,
  color: AmColors.text,
  fontFeatures: [FontFeature.tabularFigures()],
);

class _ParamRow extends StatelessWidget {
  const _ParamRow({
    required this.paramKey,
    required this.label,
    required this.track,
    required this.local,
    required this.min,
    required this.max,
    required this.selected,
    required this.onSelect,
    required this.onChanged,
    required this.onKeyframe,
    this.kfAqui,
    this.animado,
  });

  final String paramKey;
  final String label;
  final AnimatedDouble track;
  final Duration local;
  final double min;
  final double max;
  final bool selected;
  final ValueChanged<String> onSelect;
  final ValueChanged<double> onChanged;
  final VoidCallback onKeyframe;

  /// Estado do diamante vindo do EFEITO (keyframe universal). Nulo =
  /// o da propria trilha.
  final bool? kfAqui;
  final bool? animado;

  @override
  Widget build(BuildContext context) {
    final value = track.valueAt(local);
    final animated = animado ?? track.isAnimated;
    final kfHere = kfAqui ?? track.hasKeyframeAt(local);

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Row(
        children: [
          _PontoAnimado(animated: animated),
          _NomeParam(
            label: label,
            selected: selected,
            onTap: () => onSelect(paramKey),
          ),
          Expanded(
            child: AmTickRuler(
              value: value,
              min: min,
              max: max,
              unitsPerPixel: (max - min) / 400,
              height: 58,
              onChanged: (v) {
                onSelect(paramKey);
                onChanged(v);
              },
            ),
          ),
          SizedBox(
            width: 58,
            child: Text(
              amNumber(value, value.abs() >= 10 ? 1 : 3),
              textAlign: TextAlign.right,
              style: _estiloValor,
            ),
          ),
          // Diamante do parametro ("keyframe em tudo").
          CupertinoButton(
            padding: const EdgeInsets.only(left: 8),
            onPressed: onKeyframe,
            child: Icon(
              kfHere ? CupertinoIcons.rhombus_fill : CupertinoIcons.rhombus,
              size: 24,
              color: animated ? AmColors.accent : AmColors.muted,
            ),
          ),
        ],
      ),
    );
  }
}

/// Par X/Y de um ponto numa linha so: duas reguas curtas e dois valores
/// alinhados. A sensibilidade da regua e por pixel (unitsPerPixel), entao
/// meia largura nao muda o arrasto — so o tanto de ticks visiveis.
class _PointRow extends StatelessWidget {
  const _PointRow({
    required this.paramKey,
    required this.label,
    required this.xTrack,
    required this.yTrack,
    required this.local,
    required this.xMin,
    required this.xMax,
    required this.yMin,
    required this.yMax,
    required this.selected,
    required this.onSelect,
    required this.onChangedX,
    required this.onChangedY,
    required this.onKeyframe,
  });

  final String paramKey;
  final String label;
  final AnimatedDouble xTrack;
  final AnimatedDouble yTrack;
  final Duration local;
  final double xMin;
  final double xMax;
  final double yMin;
  final double yMax;
  final bool selected;
  final ValueChanged<String> onSelect;
  final ValueChanged<double> onChangedX;
  final ValueChanged<double> onChangedY;
  final VoidCallback onKeyframe;

  @override
  Widget build(BuildContext context) {
    final x = xTrack.valueAt(local);
    final y = yTrack.valueAt(local);
    // Animado se qualquer eixo anima; "no cabecote" so se os DOIS tem
    // keyframe aqui — senao o diamante cheio mentiria sobre um deles.
    final animated = xTrack.isAnimated || yTrack.isAnimated;
    final kfHere = xTrack.hasKeyframeAt(local) && yTrack.hasKeyframeAt(local);

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 7),
      child: Row(
        children: [
          _PontoAnimado(animated: animated),
          _NomeParam(
            label: label,
            selected: selected,
            onTap: () => onSelect(paramKey),
          ),
          Expanded(
            child: AmTickRuler(
              value: x,
              min: xMin,
              max: xMax,
              unitsPerPixel: (xMax - xMin) / 400,
              height: 58,
              onChanged: (v) {
                onSelect(paramKey);
                onChangedX(v);
              },
            ),
          ),
          const SizedBox(width: 4),
          Expanded(
            child: AmTickRuler(
              value: y,
              min: yMin,
              max: yMax,
              unitsPerPixel: (yMax - yMin) / 400,
              height: 58,
              onChanged: (v) {
                onSelect(paramKey);
                onChangedY(v);
              },
            ),
          ),
          SizedBox(
            width: 44,
            child: Text(
              amNumber(x, 2),
              textAlign: TextAlign.right,
              style: _estiloValor,
            ),
          ),
          SizedBox(
            width: 44,
            child: Text(
              amNumber(y, 2),
              textAlign: TextAlign.right,
              style: _estiloValor,
            ),
          ),
          CupertinoButton(
            padding: const EdgeInsets.only(left: 8),
            onPressed: onKeyframe,
            child: Icon(
              kfHere ? CupertinoIcons.rhombus_fill : CupertinoIcons.rhombus,
              size: 24,
              color: animated ? AmColors.accent : AmColors.muted,
            ),
          ),
        ],
      ),
    );
  }
}

/// Controle de ESCOLHA em chips (PR-C1).
class _ChoiceRow extends StatelessWidget {
  const _ChoiceRow({
    required this.label,
    required this.options,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final List<String> options;
  final int value;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),
      child: Row(
        children: [
          // Mesma coluna do nome das linhas com regua (ponto + 88).
          const SizedBox(width: 12),
          SizedBox(
            width: 88,
            child: Text(
              label,
              style: const TextStyle(fontSize: 13, color: AmColors.muted),
            ),
          ),
          Expanded(
            child: Wrap(
              spacing: 6,
              runSpacing: 6,
              children: [
                for (var i = 0; i < options.length; i++)
                  GestureDetector(
                    onTap: () => onChanged(i),
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 11,
                        vertical: 6,
                      ),
                      decoration: BoxDecoration(
                        color: value == i ? AmColors.accentDim : AmColors.chip,
                        borderRadius: BorderRadius.circular(8),
                      ),
                      child: Text(
                        options[i],
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.accent,
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
}

/// SEMENTE (PR-C1): inteiro que nunca interpola — o dado sorteia de
/// novo, o resultado continua deterministico.
class _SeedRow extends StatelessWidget {
  const _SeedRow({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final double value;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),
      child: Row(
        children: [
          // Mesma coluna do nome das linhas com regua (ponto + 88).
          const SizedBox(width: 12),
          SizedBox(
            width: 88,
            child: Text(
              label,
              style: const TextStyle(fontSize: 13, color: AmColors.muted),
            ),
          ),
          Text(
            '${value.round()}',
            style: const TextStyle(fontSize: 13, color: AmColors.accent),
          ),
          const SizedBox(width: 12),
          GestureDetector(
            onTap: () => onChanged(((value.round() + 1) % 100).toDouble()),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
              decoration: BoxDecoration(
                color: AmColors.chip,
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Icon(
                    CupertinoIcons.shuffle,
                    size: 14,
                    color: AmColors.accent,
                  ),
                  SizedBox(width: 6),
                  Text(
                    'Sortear',
                    style: TextStyle(fontSize: 12, color: AmColors.accent),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Liga/desliga (PR-C1).
class _ToggleRow extends StatelessWidget {
  const _ToggleRow({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        // Mesma coluna do nome das linhas com regua (ponto + 88).
        const SizedBox(width: 12),
        SizedBox(
          width: 88,
          child: Text(
            label,
            style: const TextStyle(fontSize: 13, color: AmColors.muted),
          ),
        ),
        Transform.scale(
          scale: 0.7,
          child: CupertinoSwitch(
            value: value,
            activeTrackColor: AmColors.accent,
            onChanged: onChanged,
          ),
        ),
      ],
    );
  }
}

class _ColorRow extends StatelessWidget {
  const _ColorRow({
    required this.effect,
    required this.onColor,
    this.label = 'Cor',
    this.color,
  });

  final EffectInstance effect;
  final ValueChanged<Color> onColor;
  final String label;

  /// Nula = a cor principal do efeito.
  final Color? color;

  static const _swatches = [
    Color(0xFFFF5566),
    Color(0xFFFFB020),
    Color(0xFF2BE3A0),
    Color(0xFF35C4E7),
    Color(0xFF7C62FF),
    Color(0xFFFFFFFF),
  ];

  @override
  Widget build(BuildContext context) {
    final c = color ?? effect.color;
    return Padding(
      padding: const EdgeInsets.only(top: 6),
      child: Row(
        children: [
          // Mesma coluna do nome das linhas com regua (ponto + 88).
          const SizedBox(width: 12),
          SizedBox(
            width: 88,
            child: Text(
              label,
              style: const TextStyle(fontSize: 13, color: AmColors.muted),
            ),
          ),
          const Spacer(),
          Text(
            '${(c.r * 255).round()} ${(c.g * 255).round()} ${(c.b * 255).round()}',
            style: const TextStyle(fontSize: 13, color: AmColors.accent),
          ),
          const SizedBox(width: 10),
          GestureDetector(
            onTap: () async {
              // Espectro completo: qualquer cor, com hex e alfa.
              final picked = await showColorPicker(
                context,
                initial: c,
                recent: _swatches,
                onChanged: onColor,
              );
              if (picked != null) onColor(picked);
            },
            child: Container(
              width: 30,
              height: 30,
              decoration: BoxDecoration(
                color: c,
                borderRadius: BorderRadius.circular(6),
                border: Border.all(color: Colors.white30),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// O comando de analise do Blob Tracker, com o estado do que ja rodou.
class _BotaoAnalisar extends StatefulWidget {
  const _BotaoAnalisar({
    required this.effectId,
    required this.layerId,
    required this.controller,
  });

  final String effectId;
  final String layerId;
  final EditorController controller;

  @override
  State<_BotaoAnalisar> createState() => _BotaoAnalisarState();
}

class _BotaoAnalisarState extends State<_BotaoAnalisar> {
  bool _rodando = false;

  @override
  void initState() {
    super.initState();
    // Analise de outra sessao: le do disco em vez de refazer.
    BlobTrackService.instance.load(widget.effectId);
  }

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<int>(
      valueListenable: BlobTrackService.instance.revision,
      builder: (context, _, _) {
        final dados = BlobTrackService.instance.dataFor(widget.effectId);
        final temAnalise = dados != null && !dados.isEmpty;
        return Padding(
          padding: const EdgeInsets.only(top: 8, left: 4, right: 4),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              GestureDetector(
                onTap: _rodando
                    ? null
                    : () async {
                        setState(() => _rodando = true);
                        final n = await widget.controller.analyzeBlobsFor(
                          widget.layerId,
                          widget.effectId,
                        );
                        if (!context.mounted) return;
                        setState(() => _rodando = false);
                        AureaSnack.show(
                          context,
                          n == null
                              ? 'Nao consegui ler esse video'
                              : '$n quadros analisados',
                        );
                      },
                child: Container(
                  padding: const EdgeInsets.symmetric(vertical: 11),
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color: temAnalise ? AmColors.chip : AmColors.accentDim,
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Text(
                    _rodando
                        ? 'Analisando o video...'
                        : (temAnalise
                              ? 'Analisar de novo'
                              : 'Analisar o video'),
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                      color: temAnalise ? AmColors.text : AmColors.accent,
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 4),
              Text(
                temAnalise
                    ? '${dados.frames.length} quadros com caixas gravadas. '
                          'Desenhar virou consulta: o seek e instantaneo.'
                    : 'Ainda nao analisado — o que aparece e um rastreio '
                          'simulado, para ajustar a aparencia.',
                style: const TextStyle(
                  fontSize: 11,
                  height: 1.35,
                  color: AmColors.muted,
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

/// PRONTO: os tres presets. Um toque e acabou — e a primeira das tres
/// profundidades (constituicao, regra 2).
class _ProntoRow extends StatelessWidget {
  const _ProntoRow({required this.effect, required this.onPronto});

  final EffectInstance effect;
  final ValueChanged<EffectPronto> onPronto;

  /// O preset esta aceso quando TODOS os numeros dele batem com os de
  /// agora — assim a pessoa ve de onde partiu mesmo depois de ajustar.
  bool _bate(EffectPronto p) {
    for (final e in p.valores.entries) {
      final t = effect.params[e.key];
      if (t == null || (t.base - e.value).abs() > 0.001) return false;
    }
    return true;
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        for (final p in effect.spec.presets) ...[
          Expanded(
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: () => onPronto(p),
              child: Container(
                height: 38,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: _bate(p) ? AmColors.accentDim : AmColors.chip,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Text(
                  p.nome,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                    color: _bate(p) ? AmColors.accent : AmColors.text,
                  ),
                ),
              ),
            ),
          ),
          if (p != effect.spec.presets.last) const SizedBox(width: 8),
        ],
      ],
    );
  }
}

/// O CAMINHO entre as profundidades: "Ajustar" e "Avancado", sempre com
/// esses nomes. Tocar na profundidade em que ja se esta volta ao pronto,
/// e nada do que foi feito se perde no caminho.
class _CaminhoRow extends StatelessWidget {
  const _CaminhoRow({required this.depth, required this.onDepth});

  final EffectDepth depth;
  final ValueChanged<EffectDepth> onDepth;

  @override
  Widget build(BuildContext context) {
    Widget botao(String texto, EffectDepth alvo) {
      final aceso = depth == alvo;
      return Expanded(
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTap: () => onDepth(aceso ? EffectDepth.pronto : alvo),
          child: Container(
            height: 34,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: aceso ? AmColors.accent : Colors.transparent,
              border: Border.all(
                color: aceso ? AmColors.accent : AmColors.hairline,
              ),
              borderRadius: BorderRadius.circular(9),
            ),
            child: Text(
              texto,
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: aceso ? const Color(0xFF0B0E12) : AmColors.muted,
              ),
            ),
          ),
        ),
      );
    }

    return Row(
      children: [
        botao('Ajustar', EffectDepth.montar),
        const SizedBox(width: 8),
        botao('Avancado', EffectDepth.avancado),
      ],
    );
  }
}

/// UM COMANDO VISIVEL do cartao do efeito. Chip preenchido, sem contorno.
class _ComandoDoEfeito extends StatelessWidget {
  const _ComandoDoEfeito({
    required this.rotulo,
    required this.icone,
    required this.onTap,
  });

  final String rotulo;
  final IconData icone;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: Container(
      height: 30,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icone, size: 13, color: AmColors.text),
          const SizedBox(width: 5),
          Text(
            rotulo,
            style: const TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: AmColors.text,
            ),
          ),
        ],
      ),
    ),
  );
}
