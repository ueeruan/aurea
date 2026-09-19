import 'package:aurea/src/core/l10n/app_language.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/am_colors.dart';
import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../../help/presentation/quick_guide_screen.dart';
import '../../application/editor_controller.dart';
import '../../application/effect_preset_store.dart';
import '../../application/playback_controller.dart';
import '../../application/ui/pro_mode.dart';
import '../../domain/effect.dart';
import '../../domain/effect_preset.dart';
import '../../domain/layer.dart';
import '../context/effects/effect_gallery.dart';
import '../widgets/campo_de_valor.dart';
import '../widgets/linha_de_parametro.dart';
import '../widgets/rails_do_painel.dart';
import 'audio_effects_panel.dart';
import 'color_picker_sheet.dart';
import 'curve_panel.dart';
import 'presets_screen.dart';

/// O PAINEL "EFEITOS", NA PLANTA DO ALIGHT MOTION (16/09, pedido do dono:
/// "a aba de configuracao dos efeitos deve ser igual a do AM").
///
/// ```
///  ‹   ▼ Unsharp Mask                     •••   🗑
///  ◇+  [Quantidade]  ┃┃┃┃┃┃│┃┃┃┃┃┃   [  50% ]
///  ∿   [   Raio   ]  ┃┃┃┃┃┃│┃┃┃┃┃┃   [  1,0 ]
///      [  Limiar  ]  ┃┃┃┃┃┃│┃┃┃┃┃┃   [    0 ]
/// ```
///
/// - o RAIL e o mesmo de toda ferramenta: voltar, keyframe, curva — e
///   mira o efeito aberto (o keyframe do efeito e universal: guarda todos
///   os parametros de uma vez);
/// - um CARTAO por efeito: ▼ recolhe, ••• guarda o resto (ligar,
///   duplicar, ordem, resetar, presets), 🗑 tira. Segurar o cabecalho e
///   arrastar reordena;
/// - cada parametro e a LINHA MEDIDA na referencia (`LinhaDeParametro`):
///   chip com o nome, fita no meio, caixa de valor que abre o teclado.
///
/// Estrutura e medida da referencia; cor e nome, da Aurea.
class EffectsPanel extends ConsumerStatefulWidget {
  const EffectsPanel({super.key, required this.playback, required this.onBack});

  final PlaybackController playback;
  final VoidCallback onBack;

  @override
  ConsumerState<EffectsPanel> createState() => _EffectsPanelState();
}

class _EffectsPanelState extends ConsumerState<EffectsPanel> {
  /// 'idDoEfeito/chave' do parametro em edicao: o chip dele fica aceso.
  String? _selectedParam;

  // Um efeito aberto por vez: abrir outro fecha o anterior, e um efeito
  // recem-adicionado abre sozinho.
  String? _openEffectId;
  String? _layerId;
  Set<String> _knownEffects = {};

  EditorController get _controller =>
      ref.read(editorControllerProvider.notifier);

  void _alternarExpandido(String effectId) {
    setState(() {
      _openEffectId = _openEffectId == effectId ? null : effectId;
      _selectedParam = null;
    });
  }

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
    final preset = saveEffectPreset(
      name: nome.trim(),
      effects: [effect],
      layerStart: layer.startTime,
      layerDuration: layer.duration,
      layerSize: _controller.layerBoxSize(layer, widget.playback.time.value),
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
        title: const AppText('Nome do preset'),
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
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(dialogContext).pop(campo.text),
            child: const AppText('Salvar'),
          ),
        ],
      ),
    );
  }

  /// RESETAR os parametros no instante atual, num desfazer so. O tempo e
  /// lido NO TOQUE: o painel nao pausa a reproducao.
  void _resetarEfeito(String layerId, EffectInstance effect) {
    final agora = widget.playback.time.value;
    _controller.beginGesture();
    try {
      for (final e in effect.spec.params.entries) {
        _controller.editEffectParam(
          layerId,
          effect.id,
          e.key,
          agora,
          e.value.initial,
        );
      }
      if (effect.spec.hasColor) {
        _controller.setEffectColor(
          layerId,
          effect.id,
          effect.spec.defaultColor,
        );
      }
    } finally {
      _controller.endGesture();
    }
  }

  /// O MENU ••• do cartao: o que nao cabe no cabecalho da referencia.
  Future<void> _menu(
    BuildContext context,
    Layer layer,
    EffectInstance effect, {
    required int posicao,
    required int total,
    required List<int> visiveis,
  }) async {
    final pro = ref.read(proModeProvider);
    final acao = await showCupertinoModalPopup<String>(
      context: context,
      builder: (c) {
        CupertinoActionSheetAction item(
          String id,
          String rotulo, {
          bool destrutivo = false,
        }) => CupertinoActionSheetAction(
          key: ValueKey('efeito-menu-$id'),
          isDestructiveAction: destrutivo,
          onPressed: () => Navigator.of(c).pop(id),
          child: AppText(rotulo),
        );
        // EFEITO REMOVIDO DO CATALOGO: o menu inteiro (duplicar, assar,
        // salvar como preset, guia) nao faz sentido — nao ha ficha para
        // descrever nem parametro para mexer. Sobra o que a pessoa
        // precisa: entender o que aconteceu e tirar dali.
        if (!effect.conhecido) {
          return CupertinoActionSheet(
            title: const AppText('Efeito removido'),
            message: const AppText(
              'Este efeito saiu do Aurea e nao desenha mais nada. '
              'Ele ficou guardado aqui para voce decidir — o resto da '
              'camada esta intacto.',
            ),
            actions: [
              item('remover', 'Remover efeito', destrutivo: true),
            ],
            cancelButton: CupertinoActionSheetAction(
              onPressed: () => Navigator.of(c).pop(),
              child: const AppText('Manter'),
            ),
          );
        }
        return CupertinoActionSheet(
          title: AppText(effect.spec.name),
          actions: [
            item('ligar', effect.enabled ? 'Desativar efeito' : 'Ativar efeito'),
            item('duplicar', 'Duplicar'),
            if (posicao > 0) item('subir', 'Mover para cima'),
            if (posicao < total - 1) item('descer', 'Mover para baixo'),
            item('resetar', 'Resetar'),
            item('salvar', 'Salvar como preset'),
            item('presets', 'Meus presets'),
            if (pro && effect.spec.procedural)
              item('assar', 'Assar em keyframes'),
            item('ajuda', 'Como usar este efeito'),
          ],
          cancelButton: CupertinoActionSheetAction(
            onPressed: () => Navigator.of(c).pop(),
            child: const AppText('Cancelar'),
          ),
        );
      },
    );
    if (acao == null || !context.mounted) return;
    final i = layer.effects.indexWhere((e) => e.id == effect.id);
    switch (acao) {
      case 'ligar':
        _controller.toggleEffectEnabled(layer.id, effect.id);
      case 'duplicar':
        _controller.duplicateEffect(layer.id, effect.id);
      case 'subir':
        _controller.reorderEffect(
          layer.id,
          effect.id,
          visiveis[posicao - 1] - i,
        );
      case 'descer':
        _controller.reorderEffect(
          layer.id,
          effect.id,
          visiveis[posicao + 1] - i,
        );
      case 'resetar':
        _resetarEfeito(layer.id, effect);
      case 'salvar':
        await _salvarComoPreset(context, layer.id, effect);
      case 'presets':
        await abrirTelaDePresets(
          context,
          layerId: layer.id,
          at: widget.playback.time.value,
        );
      case 'assar':
        _controller.bakeEffectToKeyframes(
          layer.id,
          effect.id,
          ref.read(editorControllerProvider).fps,
        );
        AureaSnack.show(
          context,
          'Movimento assado em keyframes',
          actionLabel: 'Desfazer',
          onAction: _controller.undo,
        );
      case 'ajuda':
        await Navigator.of(context).push(
          MaterialPageRoute<void>(
            builder: (_) => QuickGuideScreen(initialQuery: effect.spec.name),
          ),
        );
      case 'remover':
        // O EFEITO QUE NAO EXISTE MAIS e removido AQUI, por escolha da
        // pessoa — nunca pelo carregador, que nao tem como saber se o
        // resto da camada depende dele.
        ref
            .read(editorControllerProvider.notifier)
            .removeEffect(layer.id, effect.id);
    }
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(projetoVisivelProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer == null || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }
    if (layer is AudioLayer) return AudioEffectsPanel(layerId: id);
    final realLayer =
        ref.watch(editorControllerProvider).layerById(id) ?? layer;
    final effectIds = layer.effects.map((e) => e.id).toSet();
    if (_layerId != id) {
      _layerId = id;
      _openEffectId = layer.effects.firstOrNull?.id;
      _selectedParam = null;
    } else {
      final added = effectIds.difference(_knownEffects);
      if (added.isNotEmpty) {
        _openEffectId = added.last;
        _selectedParam = null;
      } else if (_openEffectId != null && !effectIds.contains(_openEffectId)) {
        _openEffectId = null;
        _selectedParam = null;
      }
    }
    _knownEffects = effectIds;

    return ColoredBox(
      color: AmColors.panel,
      // Reavalia os valores conforme o cabecote anda: o rail tambem
      // precisa saber se ha keyframe no instante.
      child: ValueListenableBuilder<Duration>(
        valueListenable: widget.playback.time,
        builder: (context, t, _) {
          final local = layer.localTime(t);
          // A curva interna de tempo (congelar, rampa pronta) nao vira
          // cartao. Os indices da lista sao os dos visiveis; mover usa a
          // distancia real na pilha.
          final visiveis = [
            for (var i = 0; i < layer.effects.length; i++)
              if (!efeitosInternos.contains(layer.effects[i].type)) i,
          ];

          // O ALVO DO RAIL: o efeito do parametro em edicao, ou o aberto.
          // Le o projeto DE VERDADE (e nao a edicao pendente): o losango
          // diz se ja ha marca gravada ali.
          final alvoId = _selectedParam?.split('/').first ?? _openEffectId;
          EffectInstance? alvo;
          for (final e in realLayer.effects) {
            if (e.id == alvoId) alvo = e;
          }
          final alvoFinal = alvo;
          final rail = AlvoDoRail(
            temKeyframeAqui: alvoFinal != null && alvoFinal.hasKeyframeAt(local),
            animado: alvoFinal != null && alvoFinal.hasAnimation,
            // O instante e lido NO TOQUE, nunca o do build.
            aoAlternarKeyframe: alvoFinal == null
                ? null
                : () => _controller.toggleEffectKeyframe(
                    id,
                    alvoFinal.id,
                    widget.playback.time.value,
                  ),
            aoAbrirCurva: alvoFinal == null || !alvoFinal.hasAnimation
                ? null
                : () => showTrackCurveSheet(
                    context,
                    ref,
                    widget.playback,
                    label: alvoFinal.spec.name,
                    layerId: id,
                    trackOf: (l) {
                      for (final e in l.effects) {
                        if (e.id != alvoFinal.id) continue;
                        for (final trilha in e.params.values) {
                          if (trilha.isAnimated) return trilha;
                        }
                      }
                      return null;
                    },
                    onSetEase: (seg, e) => _controller.setEffectSegmentEase(
                      id,
                      alvoFinal.id,
                      seg,
                      e,
                    ),
                    onSetEaseAll: (e) => _controller
                        .applyEaseToAllEffectSegments(id, alvoFinal.id, e),
                  ),
          );

          return Row(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              RailEsquerdo(aoVoltar: widget.onBack, alvo: rail),
              Expanded(
                child: ReorderableListView(
                  padding: const EdgeInsets.fromLTRB(2, 8, 12, 16),
                  buildDefaultDragHandles: false,
                  // onReorderItem ja entrega o destino descontando o item
                  // retirado.
                  onReorderItem: (de, para) {
                    if (para == de) return;
                    _controller.reorderEffect(
                      id,
                      layer.effects[visiveis[de]].id,
                      visiveis[para] - visiveis[de],
                    );
                  },
                  footer: _Rodape(
                    layerId: id,
                    video: layer is VideoLayer,
                    playback: widget.playback,
                  ),
                  children: [
                    for (final (v, i) in visiveis.indexed)
                      _CartaoDoEfeito(
                        key: ValueKey(layer.effects[i].id),
                        index: v,
                        effect: layer.effects[i],
                        local: local,
                        expanded: _openEffectId == layer.effects[i].id,
                        selectedParam: _selectedParam,
                        onToggleExpanded: () =>
                            _alternarExpandido(layer.effects[i].id),
                        onMenu: () => _menu(
                          context,
                          realLayer,
                          layer.effects[i],
                          posicao: v,
                          total: visiveis.length,
                          visiveis: visiveis,
                        ),
                        onRemove: () =>
                            _controller.removeEffect(id, layer.effects[i].id),
                        onSelectParam: (p) =>
                            setState(() => _selectedParam = p),
                        onParam: (key, valor) => _controller.editEffectParam(
                          id,
                          layer.effects[i].id,
                          key,
                          widget.playback.time.value,
                          valor,
                        ),
                        onBeginGesture: _controller.beginGesture,
                        onEndGesture: _controller.endGesture,
                        onColor: (c) => _controller.setEffectColor(
                          id,
                          layer.effects[i].id,
                          c,
                        ),
                        onExtraColor: (k, c) => _controller
                            .setEffectExtraColor(id, layer.effects[i].id, k, c),
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

/// Embaixo da pilha: efeitos de audio (video) e o "+ Adicionar efeito".
class _Rodape extends ConsumerWidget {
  const _Rodape({
    required this.layerId,
    required this.video,
    required this.playback,
  });

  final String layerId;
  final bool video;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) => Column(
    children: [
      if (video)
        TextButton.icon(
          icon: const Icon(CupertinoIcons.music_note),
          label: const AppText('Efeitos de audio'),
          onPressed: () => showModalBottomSheet<void>(
            context: context,
            backgroundColor: AmColors.panel,
            isScrollControlled: true,
            builder: (context) => SafeArea(
              child: SizedBox(
                height: MediaQuery.sizeOf(context).height * 0.6,
                child: AudioEffectsPanel(layerId: layerId),
              ),
            ),
          ),
        ),
      const SizedBox(height: 8),
      Tocavel(
        key: const ValueKey('efeitos-adicionar'),
        haptico: true,
        onTap: () => showEffectGallery(context, ref, layerId, playback),
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
              Icon(CupertinoIcons.plus, size: 17, color: AmColors.action),
              SizedBox(width: 8),
              AppText(
                'Adicionar efeito',
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w600,
                  color: AmColors.action,
                ),
              ),
            ],
          ),
        ),
      ),
    ],
  );
}

class _CartaoDoEfeito extends StatefulWidget {
  const _CartaoDoEfeito({
    super.key,
    required this.index,
    required this.effect,
    required this.local,
    required this.expanded,
    required this.selectedParam,
    required this.onToggleExpanded,
    required this.onMenu,
    required this.onRemove,
    required this.onSelectParam,
    required this.onParam,
    required this.onBeginGesture,
    required this.onEndGesture,
    required this.onColor,
    required this.onExtraColor,
  });

  /// Posicao entre os visiveis: e o que o arrasto entrega ao reordenar.
  final int index;
  final EffectInstance effect;
  final Duration local;
  final bool expanded;
  final String? selectedParam;
  final VoidCallback onToggleExpanded;
  final VoidCallback onMenu;
  final VoidCallback onRemove;
  final ValueChanged<String> onSelectParam;
  final void Function(String key, double value) onParam;
  final VoidCallback onBeginGesture;
  final VoidCallback onEndGesture;
  final ValueChanged<Color> onColor;
  final void Function(int index, Color color) onExtraColor;

  @override
  State<_CartaoDoEfeito> createState() => _CartaoDoEfeitoState();
}

class _CartaoDoEfeitoState extends State<_CartaoDoEfeito> {
  /// Os grupos ABERTOS desta ficha, por rotulo.
  ///
  /// FECHA POR PADRAO, MENOS O PRIMEIRO. O Shake abre no Global — os
  /// numeros que a pessoa procura primeiro — e os quatro eixos ficam a um
  /// toque, em vez de empurrar 34 linhas para a tela.
  final _abertos = <String>{};

  @override
  void initState() {
    super.initState();
    final grupos = widget.effect.spec.grupos;
    if (grupos.isNotEmpty) _abertos.add(grupos.first.rotulo);
  }

  EffectInstance get effect => widget.effect;
  Duration get local => widget.local;
  bool get expanded => widget.expanded;
  int get index => widget.index;
  String? get selectedParam => widget.selectedParam;
  VoidCallback get onToggleExpanded => widget.onToggleExpanded;
  VoidCallback get onMenu => widget.onMenu;
  VoidCallback get onRemove => widget.onRemove;
  ValueChanged<String> get onSelectParam => widget.onSelectParam;
  void Function(String key, double value) get onParam => widget.onParam;
  VoidCallback get onBeginGesture => widget.onBeginGesture;
  VoidCallback get onEndGesture => widget.onEndGesture;
  ValueChanged<Color> get onColor => widget.onColor;
  void Function(int index, Color color) get onExtraColor => widget.onExtraColor;

  Widget _linhaDe(MapEntry<String, EffectParam> entry) {
    final p = entry.value;
    final chave = '${effect.id}/${entry.key}';
    return switch (p.kind) {
        ParamKind.toggle => _LinhaDeInterruptor(
          key: ValueKey('efeito-param-$chave'),
          rotulo: p.label,
          valor: effect.paramAt(entry.key, local) >= .5,
          aoMudar: (v) => onParam(entry.key, v ? 1 : 0),
        ),
        ParamKind.choice => _LinhaDeEscolha(
          key: ValueKey('efeito-param-$chave'),
          rotulo: p.label,
          opcoes: p.options,
          valor: effect
              .paramAt(entry.key, local)
              .round()
              .clamp(0, p.options.isEmpty ? 0 : p.options.length - 1),
          aoMudar: (i) => onParam(entry.key, i.toDouble()),
        ),
        ParamKind.seed => _LinhaDeSemente(
          key: ValueKey('efeito-param-$chave'),
          rotulo: p.label,
          valor: effect.paramAt(entry.key, local),
          aoMudar: (v) => onParam(entry.key, v),
        ),
        // Numero e ponto: a linha medida da referencia.
        _ => LinhaDeParametro(
          key: ValueKey('efeito-param-$chave'),
          rotulo: p.label,
          valor: effect.paramAt(entry.key, local),
          porPixel: p.dragStep ?? (p.max - p.min) / 500,
          casas: p.decimals ?? _casasAutomaticas(p),
          sufixo: p.unit,
          escolhida: selectedParam == chave,
          aoEscolher: () => onSelectParam(chave),
          aoComecar: onBeginGesture,
          aoTerminar: onEndGesture,
          aoMudar: (v) => onParam(entry.key, v.clamp(p.min, p.max)),
          aoDigitar: (v) => onParam(entry.key, v.clamp(p.min, p.max)),
        ),
    };
  }

  List<Widget> _linhas() {
    final linhas = <Widget>[];
    final grupos = effect.spec.grupos;
    // O QUE NAO ESTA EM GRUPO NENHUM VAI SOLTO, ANTES DOS GRUPOS. Um
    // numero escondido por esquecimento de arrumacao e pior que um numero
    // fora de lugar.
    final emGrupo = <String>{for (final g in grupos) ...g.chaves};
    for (final entry in effect.spec.params.entries) {
      if (!emGrupo.contains(entry.key)) linhas.add(_linhaDe(entry));
    }
    for (final g in grupos) {
      final linhas_ = [
        for (final k in g.chaves)
          if (effect.spec.params[k] != null)
            MapEntry<String, EffectParam>(k, effect.spec.params[k]!),
      ];
      if (linhas_.isEmpty) continue;
      final aberto = _abertos.contains(g.rotulo);
      linhas.add(
        _CabecaDeGrupo(
          key: ValueKey('efeito-grupo-${effect.id}-${g.rotulo}'),
          rotulo: g.rotulo,
          aberto: aberto,
          aoTocar: () => setState(() {
            if (aberto) {
              _abertos.remove(g.rotulo);
            } else {
              _abertos.add(g.rotulo);
            }
          }),
        ),
      );
      if (aberto) linhas.addAll(linhas_.map(_linhaDe));
    }
    final nomes = effect.spec.colorLabels;
    if (effect.spec.hasColor) {
      linhas.add(
        _LinhaDeCor(
          rotulo: nomes.isNotEmpty ? nomes.first : 'Cor',
          cor: effect.color,
          aoMudar: onColor,
        ),
      );
    }
    for (var i = 0; i < effect.spec.extraColors; i++) {
      linhas.add(
        _LinhaDeCor(
          rotulo: i + 1 < nomes.length ? nomes[i + 1] : 'Cor ${i + 2}',
          cor: effect.extraColor(i),
          aoMudar: (c) => onExtraColor(i, c),
        ),
      );
    }
    return linhas;
  }

  static int _casasAutomaticas(EffectParam p) {
    final faixa = (p.max - p.min).abs();
    if (faixa <= 2) return 3;
    if (faixa <= 20) return 2;
    return 1;
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.fromLTRB(10, 2, 6, 8),
      decoration: BoxDecoration(
        color: AmColors.panelHigh,
        borderRadius: BorderRadius.circular(14),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            height: 48,
            child: Row(
              children: [
                // SEGURAR E ARRASTAR o nome reordena; tocar recolhe.
                Expanded(
                  child: ReorderableDelayedDragStartListener(
                    index: index,
                    child: Tocavel(
                      key: ValueKey('efeito-cabecalho-${effect.id}'),
                      encolhe: 1,
                      onTap: onToggleExpanded,
                      child: Row(
                        children: [
                          Icon(
                            expanded
                                ? CupertinoIcons.arrowtriangle_down_fill
                                : CupertinoIcons.arrowtriangle_right_fill,
                            size: 13,
                            color: AmColors.text,
                          ),
                          const SizedBox(width: 12),
                          Flexible(
                            child: Opacity(
                              // Desligado continua visivel, so esmaecido.
                              opacity: effect.enabled ? 1 : .45,
                              child: AppText(
                                effect.spec.name,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  fontSize: 17,
                                  fontWeight: FontWeight.w600,
                                  color: AmColors.text,
                                ),
                              ),
                            ),
                          ),
                          if (!effect.enabled) ...[
                            const SizedBox(width: 8),
                            const Icon(
                              CupertinoIcons.eye_slash,
                              size: 16,
                              color: AmColors.muted,
                            ),
                          ],
                        ],
                      ),
                    ),
                  ),
                ),
                _BotaoDoCabecalho(
                  key: ValueKey('efeito-menu-${effect.id}'),
                  rotulo: 'Mais opcoes do efeito',
                  icone: CupertinoIcons.ellipsis,
                  aoTocar: onMenu,
                ),
                _BotaoDoCabecalho(
                  key: ValueKey('efeito-remover-${effect.id}'),
                  rotulo: 'Remover efeito',
                  icone: CupertinoIcons.trash,
                  aoTocar: onRemove,
                ),
              ],
            ),
          ),
          // Recolhido: nem constroi o corpo.
          if (expanded)
            Opacity(
              opacity: effect.enabled ? 1 : .45,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: _linhas(),
              ),
            ),
        ],
      ),
    );
  }
}

class _BotaoDoCabecalho extends StatelessWidget {
  const _BotaoDoCabecalho({
    super.key,
    required this.rotulo,
    required this.icone,
    required this.aoTocar,
  });

  final String rotulo;
  final IconData icone;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    button: true,
    label: rotulo,
    child: Tocavel(
      onTap: aoTocar,
      child: SizedBox(
        width: 44,
        height: 44,
        child: Icon(icone, size: 22, color: AmColors.text),
      ),
    ),
  );
}

/// A CABECA DE UM GRUPO DA FICHA (Global, X Shake, Y Shake...).
///
/// Discreta de proposito: ela nao e um botao de acao, e uma divisoria que
/// abre e fecha. Por isso o triangulo pequeno e o texto em caixa alta
/// apagada, e nao o peso do nome do efeito.
class _CabecaDeGrupo extends StatelessWidget {
  const _CabecaDeGrupo({
    super.key,
    required this.rotulo,
    required this.aberto,
    required this.aoTocar,
  });

  final String rotulo;
  final bool aberto;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    button: true,
    expanded: aberto,
    label: rotulo,
    child: Tocavel(
      onTap: aoTocar,
      child: SizedBox(
        height: 42,
        child: Row(
          children: [
            Icon(
              aberto
                  ? CupertinoIcons.chevron_down
                  : CupertinoIcons.chevron_right,
              size: 13,
              color: AmColors.muted,
            ),
            const SizedBox(width: 8),
            Expanded(
              child: AppText(
                rotulo,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                  letterSpacing: .6,
                  color: AmColors.muted,
                ),
              ),
            ),
            Container(height: 1, width: 40, color: AmColors.hairline),
          ],
        ),
      ),
    ),
  );
}

/// O CHIP DA ESQUERDA das linhas que nao tem fita: mesma largura e mesmo
/// tipo do chip da `LinhaDeParametro`, para a coluna dos nomes nao pular.
class _ChipDoRotulo extends StatelessWidget {
  const _ChipDoRotulo(this.rotulo);

  final String rotulo;

  @override
  Widget build(BuildContext context) => Container(
    width: 94,
    height: 32,
    alignment: Alignment.center,
    padding: const EdgeInsets.symmetric(horizontal: 6),
    child: AppText(
      rotulo,
      maxLines: 2,
      textAlign: TextAlign.center,
      overflow: TextOverflow.ellipsis,
      style: const TextStyle(
        fontSize: 12,
        height: 1.1,
        fontWeight: FontWeight.w600,
        color: AmColors.muted,
      ),
    ),
  );
}

/// [nome] ........................ [interruptor]
class _LinhaDeInterruptor extends StatelessWidget {
  const _LinhaDeInterruptor({
    super.key,
    required this.rotulo,
    required this.valor,
    required this.aoMudar,
  });

  final String rotulo;
  final bool valor;
  final ValueChanged<bool> aoMudar;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: LinhaDeParametro.altura,
    child: Row(
      children: [
        _ChipDoRotulo(rotulo),
        const Spacer(),
        Semantics(
          label: rotulo,
          child: CupertinoSwitch(
            value: valor,
            // Lima = ligado, a mesma leitura do resto do cromo.
            activeTrackColor: AmColors.action,
            onChanged: aoMudar,
          ),
        ),
        const SizedBox(width: 4),
      ],
    ),
  );
}

/// [nome]  (A) (B) (C)
class _LinhaDeEscolha extends StatelessWidget {
  const _LinhaDeEscolha({
    super.key,
    required this.rotulo,
    required this.opcoes,
    required this.valor,
    required this.aoMudar,
  });

  final String rotulo;
  final List<String> opcoes;
  final int valor;
  final ValueChanged<int> aoMudar;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 6),
    child: Row(
      children: [
        _ChipDoRotulo(rotulo),
        const SizedBox(width: 8),
        Expanded(
          child: Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var i = 0; i < opcoes.length; i++)
                Tocavel(
                  onTap: () => aoMudar(i),
                  child: Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 11,
                      vertical: 6,
                    ),
                    decoration: BoxDecoration(
                      color: valor == i ? AmColors.accentDim : AmColors.campo,
                      borderRadius: BorderRadius.circular(CampoDeValor.raio),
                    ),
                    child: AppText(
                      opcoes[i],
                      style: TextStyle(
                        fontSize: 12,
                        color: valor == i ? AmColors.accent : AmColors.text,
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

/// SEMENTE: inteiro que nunca interpola — o dado sorteia de novo.
class _LinhaDeSemente extends StatelessWidget {
  const _LinhaDeSemente({
    super.key,
    required this.rotulo,
    required this.valor,
    required this.aoMudar,
  });

  final String rotulo;
  final double valor;
  final ValueChanged<double> aoMudar;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: LinhaDeParametro.altura,
    child: Row(
      children: [
        _ChipDoRotulo(rotulo),
        const Spacer(),
        AppText(
          '${valor.round()}',
          style: const TextStyle(fontSize: 13, color: AmColors.text),
        ),
        const SizedBox(width: 12),
        Tocavel(
          onTap: () => aoMudar(((valor.round() + 1) % 100).toDouble()),
          child: Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
            decoration: BoxDecoration(
              color: AmColors.campo,
              borderRadius: BorderRadius.circular(CampoDeValor.raio),
            ),
            child: const Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(CupertinoIcons.shuffle, size: 14, color: AmColors.text),
                SizedBox(width: 6),
                AppText(
                  'Sortear',
                  style: TextStyle(fontSize: 12, color: AmColors.text),
                ),
              ],
            ),
          ),
        ),
      ],
    ),
  );
}

/// [Cor] ............ 255 85 102 ■ — toque na amostra abre o seletor.
class _LinhaDeCor extends StatelessWidget {
  const _LinhaDeCor({
    required this.cor,
    required this.aoMudar,
    this.rotulo = 'Cor',
  });

  final Color cor;
  final ValueChanged<Color> aoMudar;
  final String rotulo;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: LinhaDeParametro.altura,
    child: Row(
      children: [
        _ChipDoRotulo(rotulo),
        const Spacer(),
        AppText(
          '${(cor.r * 255).round()} ${(cor.g * 255).round()} '
          '${(cor.b * 255).round()}',
          style: const TextStyle(
            fontSize: 13,
            color: AmColors.text,
            fontFeatures: [FontFeature.tabularFigures()],
          ),
        ),
        const SizedBox(width: 10),
        Tocavel(
          onTap: () async {
            final escolhida = await showColorPicker(
              context,
              initial: cor,
              onChanged: aoMudar,
            );
            if (escolhida != null) aoMudar(escolhida);
          },
          child: Container(
            width: 30,
            height: 30,
            decoration: BoxDecoration(
              color: cor,
              borderRadius: BorderRadius.circular(6),
            ),
          ),
        ),
        const SizedBox(width: 4),
      ],
    ),
  );
}
