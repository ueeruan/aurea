import 'dart:async';

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/cronometro_de_edicao.dart';
import '../../application/editor_controller.dart';
import '../../application/operacoes_do_lote.dart';
import '../../application/playback_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../application/ui/opcoes_de_visualizacao.dart';
import '../../domain/layer.dart';
import '../../domain/layout_ops.dart';
import '../../domain/video_project.dart';
import '../am/align_sheet.dart';
import '../am/am_colors.dart';
import '../am/beats_sheet.dart';
import '../am/cameras_sheet.dart';
import '../am/export_sheet.dart';
import '../am/layer_look.dart';
import '../am/layer_menu.dart' show showParentSheet;
import 'layer_actions.dart';
import 'menu_da_camada.dart';
import 'project_settings_sheet.dart';
import 'transport_bar.dart' show parseTimecodeInput;

/// O CROMO DO EDITOR (v1.1.1): as barras que cercam palco e timeline.
///
/// Barra do projeto de 44 no topo (sair · titulo · tempo · projeto ·
/// exportar), barra do LOTE no lugar dela quando ha multi-selecao,
/// barra de reproducao de 46 sobre a timeline (desfazer/refazer ·
/// quadro-play-quadro · colar · marcas · tela cheia), trilho vertical a
/// direita do palco (solo · camera · zoom do palco) e a barra flutuante
/// de aparar/dividir quando ha camada selecionada.
///
/// AS CORES SAO AS DA AUREA ([AmColors]): o lima e acao e "ligado", o
/// teal e keyframe, o violeta e selecao e grupo. Estrutura de editor de
/// motion se aprende de qualquer referencia; paleta, icones e textos sao
/// nossos.
abstract final class CromoEditor {
  static const Color fundo = AmColors.topBar;
  static const Color palco = AmColors.bg;
  static const Color trilho = AmColors.chip;

  /// Acao e estado ligado (o lima da logo).
  static const Color acao = AmColors.action;
  static const Color sobreAcao = AmColors.onAction;

  /// Keyframe no cabecote (teal).
  static const Color keyframe = AmColors.accent;

  /// Selecao e grupo (violeta da logo): a barra do lote e o chip de grupo.
  static const Color selecao = AmColors.selection;
  static const Color branco = AmColors.text;
  static const Color apagado = Color(0x66FFFFFF);

  static const double navbar = 44;
  static const double playbar = 46;
}

/// O ZOOM DO PALCO (trilho da direita): 1.0 = ajustado a janela.
final zoomDoPalcoProvider = StateProvider<double>((ref) => 1.0);

/// Um botao de icone do cromo: alvo de 40, sem enfeite proprio.
class _BotaoDoCromo extends StatelessWidget {
  const _BotaoDoCromo({
    super.key,
    required this.icone,
    required this.dica,
    required this.onTap,
    this.onLongPress,
    this.cor,
    this.tamanho = 21,
    this.largura = 40,
    this.altura = 44,
  });

  final IconData icone;
  final String dica;
  final VoidCallback? onTap;
  final VoidCallback? onLongPress;
  final Color? cor;
  final double tamanho;
  final double largura;
  final double altura;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: dica,
      child: Tocavel(
        onTap: onTap == null
            ? null
            : () {
                HapticFeedback.lightImpact();
                onTap!();
              },
        onLongPress: onLongPress == null
            ? null
            : () {
                HapticFeedback.mediumImpact();
                onLongPress!();
              },
        child: SizedBox(
          width: largura,
          height: altura,
          child: Icon(
            icone,
            size: tamanho,
            color: cor ??
                (onTap == null
                    ? CromoEditor.apagado.withValues(alpha: .25)
                    : CromoEditor.branco),
          ),
        ),
      ),
    );
  }
}

String _tempoCurto(Duration d) {
  final cs = (d.inMilliseconds % 1000) ~/ 10;
  final s = d.inSeconds % 60;
  final m = d.inMinutes;
  return '$m:${s.toString().padLeft(2, '0')}.${cs.toString().padLeft(2, '0')}';
}

/// A BARRA DO PROJETO: sair · trilha de grupos (dentro de grupo) ·
/// titulo editavel ali mesmo · cronometro de edicao (contando) · tempo
/// corrente (toque digita) · projeto · exportar.
class BarraDoProjeto extends ConsumerWidget {
  const BarraDoProjeto({
    super.key,
    required this.onBack,
    required this.playback,
    this.onMenu,
  });

  final VoidCallback onBack;
  final PlaybackController playback;

  /// O ⋮ DO PROJETO. Este menu morava num botao redondo flutuando no
  /// canto da linha do tempo, por cima dos clipes — o mesmo vicio da
  /// barra de acoes que saiu antes dele. Menu e coisa de barra.
  final VoidCallback? onMenu;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final dentroDeGrupo = controller.dentroDeGrupo;
    return Container(
      height: CromoEditor.navbar,
      color: CromoEditor.fundo,
      padding: const EdgeInsets.only(right: 6),
      child: Row(
        children: [
          Tooltip(
            message: dentroDeGrupo ? 'Voltar' : 'Projetos',
            child: Tocavel(
              key: const ValueKey('editor-back'),
              onTap: onBack,
              child: SizedBox(
                width: 44,
                height: 44,
                // A porta com a seta: sair do projeto.
                child: Transform.flip(
                  flipX: true,
                  child: const Icon(
                    Icons.logout,
                    size: 20,
                    color: CromoEditor.branco,
                  ),
                ),
              ),
            ),
          ),
          if (dentroDeGrupo) _TrilhaDeGrupos(controller: controller),
          const Expanded(child: _TituloEditavel()),
          _ChipDoCronometro(projetoId: project.id),
          // O relogio mora na barra do projeto: tocar digita o tempo.
          ValueListenableBuilder<Duration>(
            valueListenable: playback.time,
            builder: (context, t, _) => Tooltip(
              message: 'Ir para o tempo',
              child: Tocavel(
                key: const ValueKey('navbar-tempo'),
                onTap: () => digitarTempo(context, ref, playback),
                child: Padding(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 6,
                    vertical: 12,
                  ),
                  child: AppText(
                    _tempoCurto(t),
                    style: const TextStyle(
                      color: CromoEditor.apagado,
                      fontSize: 12.5,
                      fontFeatures: [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ),
            ),
          ),
          if (onMenu != null)
            _BotaoDoCromo(
              key: const ValueKey('editor-menu'),
              icone: Icons.more_vert,
              dica: 'Mais da linha do tempo',
              tamanho: 19,
              onTap: onMenu!,
            ),
          _BotaoDoCromo(
            key: const ValueKey('editor-settings'),
            icone: CupertinoIcons.gear_alt_fill,
            dica: 'Projeto',
            tamanho: 19,
            onTap: () => showProjectSettingsSheet(context, ref),
          ),
          _BotaoDoCromo(
            key: const ValueKey('editor-export'),
            icone: CupertinoIcons.square_arrow_up,
            dica: 'Exportar',
            cor: CromoEditor.acao,
            onTap: () {
              // EXPORTAR DE DENTRO DE UM GRUPO exportava so o grupo: o
              // palco da exportacao le o estado do editor. Sai de todos
              // antes — o projeto inteiro e o que se exporta.
              controller.exitAllGroups();
              showExportSheet(context, ref);
            },
          ),
        ],
      ),
    );
  }
}

/// A BARRA DA CAMADA: com uma camada escolhida ela toma o lugar da barra
/// do projeto — voltar (tira a selecao), o tipo e o nome editavel ali
/// mesmo, parentesco, lixeira e o ⋯ com tudo o que se faz com a camada.
class BarraDaCamada extends ConsumerWidget {
  const BarraDaCamada({
    super.key,
    required this.layerId,
    required this.onBack,
    required this.playback,
  });

  final String layerId;
  final VoidCallback onBack;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final layer = project.layerById(layerId);
    if (layer == null) {
      return Container(height: CromoEditor.navbar, color: CromoEditor.fundo);
    }
    final temPai =
        project.linkFor(layerId, LayerProp.parent) != null ||
        (layer is Scene3DLayer && layer.cameraParentLayerId != null);
    return Container(
      key: const ValueKey('barra-da-camada'),
      height: CromoEditor.navbar,
      color: CromoEditor.fundo,
      padding: const EdgeInsets.only(right: 4),
      child: Row(
        children: [
          _BotaoDoCromo(
            key: const ValueKey('editor-back'),
            icone: CupertinoIcons.chevron_left,
            dica: 'Voltar (tirar a seleção)',
            largura: 44,
            onTap: onBack,
          ),
          Container(
            width: 24,
            height: 24,
            margin: const EdgeInsets.only(right: 8),
            decoration: BoxDecoration(
              color: layerTypeColor(layer),
              borderRadius: BorderRadius.circular(7),
            ),
            child: Icon(layerTypeIcon(layer), size: 14, color: Colors.white),
          ),
          Expanded(child: _NomeDaCamadaEditavel(layerId: layerId)),
          // PARENTESCO so acende quando ESTA ligado: e informacao, nao
          // acao de todo dia — o lugar dele e o menu ⋮.
          if (temPai)
            _BotaoDoCromo(
              key: const ValueKey('camada-parentesco'),
              icone: CupertinoIcons.link_circle_fill,
              dica: 'Segue outra camada',
              cor: CromoEditor.keyframe,
              tamanho: 20,
              onTap: () {
                playback.pause();
                showParentSheet(context, ref, layer, playback.time.value);
              },
            ),
          // DUPLICAR sobe para a barra: era a unica acao da barra
          // flutuante que nao tinha outra porta a um toque.
          _BotaoDoCromo(
            key: const ValueKey('camada-duplicar'),
            icone: CupertinoIcons.plus_square_on_square,
            dica: 'Duplicar camada',
            tamanho: 19,
            onTap: () =>
                ref.read(editorControllerProvider.notifier).duplicateLayer(layerId),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-lixeira'),
            icone: CupertinoIcons.trash,
            dica: 'Excluir camada',
            tamanho: 19,
            onTap: () => excluirCamadas(context, ref, {layerId}),
          ),
          _BotaoDoCromo(
            key: const ValueKey('camada-menu'),
            icone: Icons.more_vert,
            dica: 'Tudo o que se faz com a camada',
            tamanho: 19,
            onTap: () => menuDaCamada(context, ref, layerId, playback),
          ),
        ],
      ),
    );
  }
}

/// O NOME DA CAMADA EDITAVEL ALI MESMO. Vazio nao vale.
class _NomeDaCamadaEditavel extends ConsumerStatefulWidget {
  const _NomeDaCamadaEditavel({required this.layerId});

  final String layerId;

  @override
  ConsumerState<_NomeDaCamadaEditavel> createState() =>
      _NomeDaCamadaEditavelState();
}

class _NomeDaCamadaEditavelState extends ConsumerState<_NomeDaCamadaEditavel> {
  bool _editando = false;
  final _campo = TextEditingController();
  final _foco = FocusNode();

  @override
  void initState() {
    super.initState();
    _foco.addListener(() {
      if (!_foco.hasFocus && _editando) _confirmar();
    });
  }

  @override
  void dispose() {
    _campo.dispose();
    _foco.dispose();
    super.dispose();
  }

  void _comecar() {
    final nome =
        ref.read(editorControllerProvider).layerById(widget.layerId)?.name ??
        '';
    _campo.text = nome;
    _campo.selection = TextSelection(
      baseOffset: 0,
      extentOffset: nome.length,
    );
    setState(() => _editando = true);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _foco.requestFocus();
    });
  }

  void _confirmar() {
    if (!_editando) return;
    final nome = _campo.text.trim();
    setState(() => _editando = false);
    if (nome.isEmpty) return;
    ref.read(editorControllerProvider.notifier).renameLayer(widget.layerId, nome);
  }

  @override
  Widget build(BuildContext context) {
    final nome = ref.watch(
      editorControllerProvider.select(
        (p) => p.layerById(widget.layerId)?.name ?? '',
      ),
    );
    if (_editando) {
      return CupertinoTextField(
        key: const ValueKey('camada-nome-campo'),
        controller: _campo,
        focusNode: _foco,
        maxLines: 1,
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
        style: const TextStyle(
          color: CromoEditor.branco,
          fontSize: 14,
          fontWeight: FontWeight.w600,
        ),
        decoration: BoxDecoration(
          color: CromoEditor.trilho,
          borderRadius: BorderRadius.circular(8),
        ),
        textInputAction: TextInputAction.done,
        onSubmitted: (_) => _confirmar(),
      );
    }
    return GestureDetector(
      key: const ValueKey('camada-nome'),
      behavior: HitTestBehavior.opaque,
      onTap: _comecar,
      child: AppText(
        nome.trim().isEmpty ? '(Camada sem nome)' : nome,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: TextStyle(
          color: nome.trim().isEmpty
              ? CromoEditor.apagado
              : CromoEditor.branco,
          fontSize: 14,
          fontWeight: FontWeight.w600,
        ),
      ),
    );
  }
}

/// A TRILHA DE GRUPOS: o projeto e cada grupo aberto por fora do atual,
/// como migalhas. Tocar numa volta ate aquele nivel; a ultima migalha
/// (um nivel acima) e a mesma "sair do grupo" de sempre.
class _TrilhaDeGrupos extends StatelessWidget {
  const _TrilhaDeGrupos({required this.controller});

  final EditorController controller;

  @override
  Widget build(BuildContext context) {
    final caminho = controller.caminhoDoGrupo;
    // Migalhas: o projeto (nivel 0) e os grupos por fora do atual.
    final migalhas = <(String, int)>[
      (controller.nomeDoProjetoRaiz, 0),
      for (var i = 0; i < caminho.length - 1; i++) (caminho[i], i + 1),
    ];
    return ConstrainedBox(
      constraints: BoxConstraints(
        // 30%, e nao 38%: a barra ganhou o ⋮ do menu e o teto antigo
        // estourava 29 px num celular de 390. A trilha rola por dentro,
        // entao nenhuma migalha se perde — so aparecem menos de uma vez.
        maxWidth: MediaQuery.sizeOf(context).width * .30,
      ),
      child: SingleChildScrollView(
        key: const ValueKey('navbar-trilha-de-grupos'),
        scrollDirection: Axis.horizontal,
        reverse: true,
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (final (nome, nivel) in migalhas)
              Tooltip(
                message: nivel == 0 ? 'Voltar ao projeto' : 'Voltar a $nome',
                child: Tocavel(
                  key: ValueKey(
                    nivel == migalhas.last.$2
                        ? 'navbar-sair-grupo'
                        : 'navbar-migalha-$nivel',
                  ),
                  onTap: () => controller.sairAteONivel(nivel),
                  child: Container(
                    height: 26,
                    constraints: const BoxConstraints(maxWidth: 96),
                    padding: const EdgeInsets.symmetric(horizontal: 8),
                    margin: const EdgeInsets.only(right: 2),
                    decoration: BoxDecoration(
                      color: CromoEditor.trilho,
                      borderRadius: BorderRadius.circular(13),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          nivel == 0
                              ? CupertinoIcons.film
                              : CupertinoIcons.rectangle_stack,
                          size: 12,
                          color: CromoEditor.selecao,
                        ),
                        const SizedBox(width: 4),
                        Flexible(
                          child: AppText(
                            nome,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              color: CromoEditor.branco,
                              fontSize: 11.5,
                            ),
                          ),
                        ),
                        const SizedBox(width: 2),
                        const Icon(
                          CupertinoIcons.chevron_right,
                          size: 10,
                          color: CromoEditor.apagado,
                        ),
                      ],
                    ),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

/// O TITULO EDITAVEL ALI MESMO: tocar vira campo, confirmar (ou tocar
/// fora) renomeia — e o renomear entra no desfazer. Dentro de um grupo
/// e o nome do grupo. Vazio nao vale: volta o nome que estava.
class _TituloEditavel extends ConsumerStatefulWidget {
  const _TituloEditavel();

  @override
  ConsumerState<_TituloEditavel> createState() => _TituloEditavelState();
}

class _TituloEditavelState extends ConsumerState<_TituloEditavel> {
  bool _editando = false;
  final _campo = TextEditingController();
  final _foco = FocusNode();

  @override
  void initState() {
    super.initState();
    _foco.addListener(() {
      if (!_foco.hasFocus && _editando) _confirmar();
    });
  }

  @override
  void dispose() {
    _campo.dispose();
    _foco.dispose();
    super.dispose();
  }

  void _comecar() {
    _campo.text = ref.read(editorControllerProvider).name;
    _campo.selection = TextSelection(
      baseOffset: 0,
      extentOffset: _campo.text.length,
    );
    setState(() => _editando = true);
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _foco.requestFocus();
    });
  }

  void _confirmar() {
    if (!_editando) return;
    final nome = _campo.text.trim();
    final atual = ref.read(editorControllerProvider).name;
    setState(() => _editando = false);
    if (nome.isEmpty || nome == atual) return;
    ref.read(editorControllerProvider.notifier).renameProject(nome);
  }

  @override
  Widget build(BuildContext context) {
    final nome = ref.watch(editorControllerProvider.select((p) => p.name));
    if (_editando) {
      return CupertinoTextField(
        key: const ValueKey('editor-project-name-campo'),
        controller: _campo,
        focusNode: _foco,
        maxLength: 320,
        maxLines: 1,
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
        style: const TextStyle(
          color: CromoEditor.branco,
          fontSize: 14,
          fontWeight: FontWeight.w600,
        ),
        decoration: BoxDecoration(
          color: CromoEditor.trilho,
          borderRadius: BorderRadius.circular(8),
        ),
        textInputAction: TextInputAction.done,
        onSubmitted: (_) => _confirmar(),
      );
    }
    return GestureDetector(
      key: const ValueKey('editor-project-name'),
      behavior: HitTestBehavior.opaque,
      onTap: _comecar,
      child: AppText(
        nome.trim().isEmpty ? '(Sem título)' : nome,
        maxLines: 1,
        overflow: TextOverflow.ellipsis,
        style: TextStyle(
          color: nome.trim().isEmpty ? CromoEditor.apagado : CromoEditor.branco,
          fontSize: 14,
          fontWeight: FontWeight.w600,
        ),
      ),
    );
  }
}

/// A BARRA DO LOTE: quando ha multi-selecao, o topo inteiro vira
/// violeta (a cor da selecao). DUAS PAGINAS, trocadas pelas setas:
///
///  1. o lote — agrupar, agrupar e mascarar (a de cima mostra so o que
///     cobre), agrupar e recortar (a de cima fura as de baixo), excluir;
///  2. o layout — alinhar na tela pelos seis lados e distribuir na
///     vertical e na horizontal. Segurar um alinhamento abre a folha
///     completa (alinhar a selecao, a uma ancora, espacamento exato).
class BarraDoLote extends ConsumerStatefulWidget {
  const BarraDoLote({super.key, required this.playback});

  final PlaybackController playback;

  @override
  ConsumerState<BarraDoLote> createState() => _BarraDoLoteState();
}

class _BarraDoLoteState extends ConsumerState<BarraDoLote> {
  bool _paginaDoLayout = false;

  @override
  Widget build(BuildContext context) {
    final multi = ref.watch(multiSelectProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final ids = multi.toList();
    final bastam = multi.length >= 2;
    // O tempo sai na hora do toque (a regra da barra da selecao).
    Duration t() => widget.playback.time.value;

    Widget botao(
      String chave,
      IconData icone,
      String dica,
      VoidCallback? onTap, {
      VoidCallback? onLongPress,
      double largura = 30,
    }) => _BotaoDoCromo(
      key: ValueKey(chave),
      icone: icone,
      dica: dica,
      cor: CromoEditor.branco,
      tamanho: 18,
      largura: largura,
      onTap: onTap,
      onLongPress: onLongPress,
    );

    Widget alinhar(String chave, IconData icone, String dica, AlignEdge edge) =>
        botao(
          chave,
          icone,
          '$dica · segure para mais',
          () => controller.alignSelection(ids, edge, t()),
          onLongPress: () => showAlignSheet(context, ref, ids, t()),
        );

    final paginaDoLote = <Widget>[
      Expanded(
        child: AppText(
          bastam
              ? '${multi.length} selecionadas'
              : 'Selecione ao menos duas camadas',
          key: const ValueKey('selectbar-mensagem'),
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: const TextStyle(
            color: CromoEditor.branco,
            fontSize: 13,
            fontWeight: FontWeight.w700,
          ),
        ),
      ),
      botao(
        'selectbar-agrupar',
        CupertinoIcons.rectangle_stack,
        'Agrupar seleção',
        bastam ? () => agruparSelecao(ref, multi) : null,
        largura: 36,
      ),
      botao(
        'selectbar-mascarar',
        CupertinoIcons.square_stack_3d_down_right_fill,
        'Agrupar e mascarar: a de cima mostra só o que cobre',
        bastam
            ? () {
                agruparComForma(
                  controller,
                  ref.read(editorControllerProvider),
                  ids,
                  recortar: false,
                );
                ref.read(multiSelectProvider.notifier).state = const {};
              }
            : null,
        largura: 36,
      ),
      botao(
        'selectbar-recortar',
        CupertinoIcons.square_stack_3d_down_right,
        'Agrupar e recortar: a de cima fura as de baixo',
        bastam
            ? () {
                agruparComForma(
                  controller,
                  ref.read(editorControllerProvider),
                  ids,
                  recortar: true,
                );
                ref.read(multiSelectProvider.notifier).state = const {};
              }
            : null,
        largura: 36,
      ),
      botao(
        'selectbar-excluir',
        CupertinoIcons.trash,
        'Excluir seleção',
        () => excluirCamadas(context, ref, multi),
        largura: 36,
      ),
      botao(
        'selectbar-pagina-layout',
        CupertinoIcons.chevron_right,
        'Alinhar e distribuir',
        () => setState(() => _paginaDoLayout = true),
      ),
    ];

    final paginaDoLayout = <Widget>[
      botao(
        'selectbar-pagina-lote',
        CupertinoIcons.chevron_left,
        'Voltar às ações do lote',
        () => setState(() => _paginaDoLayout = false),
      ),
      const Spacer(),
      alinhar(
        'selectbar-alinhar-esquerda',
        CupertinoIcons.arrow_left_to_line,
        'Alinhar à esquerda',
        AlignEdge.left,
      ),
      alinhar(
        'selectbar-alinhar-centro',
        CupertinoIcons.arrow_left_right,
        'Centralizar na horizontal',
        AlignEdge.centerH,
      ),
      alinhar(
        'selectbar-alinhar-direita',
        CupertinoIcons.arrow_right_to_line,
        'Alinhar à direita',
        AlignEdge.right,
      ),
      alinhar(
        'selectbar-alinhar-topo',
        CupertinoIcons.arrow_up_to_line,
        'Alinhar ao topo',
        AlignEdge.top,
      ),
      alinhar(
        'selectbar-alinhar-meio',
        CupertinoIcons.arrow_up_arrow_down,
        'Centralizar na vertical',
        AlignEdge.centerV,
      ),
      alinhar(
        'selectbar-alinhar-base',
        CupertinoIcons.arrow_down_to_line,
        'Alinhar à base',
        AlignEdge.bottom,
      ),
      botao(
        'selectbar-distribuir-v',
        CupertinoIcons.arrow_up_down_square,
        'Distribuir na vertical (vãos iguais)',
        multi.length >= 3
            ? () => controller.distributeSelection(
                ids,
                DistributeAxis.vertical,
                DistributeMode.byGap,
                t(),
              )
            : null,
      ),
      botao(
        'selectbar-distribuir-h',
        CupertinoIcons.arrow_left_right_square,
        'Distribuir na horizontal (vãos iguais)',
        multi.length >= 3
            ? () => controller.distributeSelection(
                ids,
                DistributeAxis.horizontal,
                DistributeMode.byGap,
                t(),
              )
            : null,
      ),
      const SizedBox(width: 2),
    ];

    return Container(
      height: CromoEditor.navbar,
      color: CromoEditor.selecao,
      child: Row(
        children: [
          botao(
            'selectbar-cancelar',
            CupertinoIcons.xmark,
            'Cancelar seleção',
            () => ref.read(multiSelectProvider.notifier).state = const {},
            largura: 36,
          ),
          ...(_paginaDoLayout ? paginaDoLayout : paginaDoLote),
        ],
      ),
    );
  }
}

/// A BARRA DE TEMPO DO LOTE (flutua sobre a timeline com duas ou mais
/// camadas escolhidas). Com o cabecote passando por dentro do lote:
/// aparar o comeco, dividir, aparar o fim. Com o cabecote de fora:
/// estender ate ele, mover ate ele. E sempre: alinhar os comecos,
/// distribuir uma depois da outra, alinhar os fins.
class BarraDoLoteNoTempo extends ConsumerWidget {
  const BarraDoLoteNoTempo({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final multi = ref.watch(multiSelectProvider);
    final projeto = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    Duration t() => playback.time.value;
    VideoProject atual() => ref.read(editorControllerProvider);

    return ValueListenableBuilder<Duration>(
      valueListenable: playback.time,
      builder: (context, agora, _) {
        final dentro = cabecoteDentroDoLote(projeto, multi, agora);
        return Container(
          key: const ValueKey('barra-do-lote-no-tempo'),
          height: 46,
          padding: const EdgeInsets.symmetric(horizontal: 4),
          decoration: BoxDecoration(
            color: CromoEditor.trilho.withValues(alpha: .97),
            borderRadius: BorderRadius.circular(12),
            boxShadow: const [
              BoxShadow(
                color: Colors.black45,
                blurRadius: 10,
                offset: Offset(0, 3),
              ),
            ],
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (dentro) ...[
                _BotaoDoCromo(
                  key: const ValueKey('lote-aparar-esq'),
                  icone: CupertinoIcons.arrow_right_to_line,
                  dica: 'Aparar o início das camadas no cabeçote',
                  onTap: () => aparaInicioDoLote(controller, atual(), multi, t()),
                ),
                _BotaoDoCromo(
                  key: const ValueKey('lote-dividir'),
                  icone: CupertinoIcons.scissors,
                  dica: 'Dividir as camadas no cabeçote',
                  onTap: () => dividirLote(controller, atual(), multi, t()),
                ),
                _BotaoDoCromo(
                  key: const ValueKey('lote-aparar-dir'),
                  icone: CupertinoIcons.arrow_left_to_line,
                  dica: 'Aparar o fim das camadas no cabeçote',
                  onTap: () => aparaFimDoLote(controller, atual(), multi, t()),
                ),
              ] else ...[
                _BotaoDoCromo(
                  key: const ValueKey('lote-estender'),
                  icone: CupertinoIcons.arrow_right_arrow_left_square,
                  dica: 'Estender as camadas até o cabeçote',
                  onTap: () =>
                      estenderLoteAteOCabecote(controller, atual(), multi, t()),
                ),
                _BotaoDoCromo(
                  key: const ValueKey('lote-mover'),
                  icone: CupertinoIcons.arrow_right_arrow_left,
                  dica: 'Mover as camadas até o cabeçote',
                  onTap: () =>
                      moverLoteAteOCabecote(controller, atual(), multi, t()),
                ),
              ],
              Container(
                width: 1,
                height: 22,
                margin: const EdgeInsets.symmetric(horizontal: 4),
                color: CromoEditor.apagado.withValues(alpha: .25),
              ),
              _BotaoDoCromo(
                key: const ValueKey('lote-alinhar-inicios'),
                icone: CupertinoIcons.increase_indent,
                dica: 'Alinhar os inícios no tempo',
                onTap: () => alinharIniciosNoTempo(controller, atual(), multi),
              ),
              _BotaoDoCromo(
                key: const ValueKey('lote-distribuir-tempo'),
                icone: CupertinoIcons.text_justify,
                dica: 'Distribuir: uma depois da outra',
                onTap: () => distribuirNoTempo(controller, atual(), multi),
              ),
              _BotaoDoCromo(
                key: const ValueKey('lote-alinhar-fins'),
                icone: CupertinoIcons.decrease_indent,
                dica: 'Alinhar os fins no tempo',
                onTap: () => alinharFinsNoTempo(controller, atual(), multi),
              ),
            ],
          ),
        );
      },
    );
  }
}

/// A BARRA DE REPRODUCAO (46, sobre a timeline): desfazer/refazer a
/// esquerda, quadro-play-quadro no centro; a direita copiar e colar,
/// marcador no cabecote, opcoes de visualizacao e tela cheia.
///
/// Os saltos: toque em |◀ ▶| anda por KEYFRAME quando a camada
/// selecionada tem marcas (o pedido dos testadores), senao UM QUADRO;
/// segurar vai ao inicio/fim.
///
/// Enquanto o dedo manipula alguma coisa ([infobarProvider]), a barra
/// vira a barra de informacoes: o numero que esta mudando fica onde o
/// olho ja esta. Tocando, um medidor de nivel do som corre por tras
/// dos botoes.
class BarraDeReproducao extends ConsumerWidget {
  const BarraDeReproducao({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final info = ref.watch(infobarProvider);
    if (info != null) {
      return Container(
        key: const ValueKey('barra-de-informacoes'),
        height: CromoEditor.playbar,
        color: CromoEditor.fundo,
        child: _ConteudoDaInfobar(info: info),
      );
    }
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.watch(editorControllerProvider);
    final selected = ref.watch(selectedLayerProvider);
    final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
    final duration = project.duration;
    final fps = project.fps <= 0 ? 30 : project.fps;
    final quadro = Duration(microseconds: 1000000 ~/ fps);

    final camada = selected == null ? null : project.layerById(selected);
    final marcas = camada == null
        ? const <Duration>[]
        : [for (final k in camada.keyframeTimes) camada.startTime + k];
    Duration? anterior(Duration agora) {
      Duration? achada;
      for (final m in marcas) {
        if (m < agora - const Duration(milliseconds: 1)) achada = m;
      }
      return achada;
    }

    Duration? proxima(Duration agora) {
      for (final m in marcas) {
        if (m > agora + const Duration(milliseconds: 1)) return m;
      }
      return null;
    }

    Duration limitar(Duration t) =>
        t < Duration.zero ? Duration.zero : (t > duration ? duration : t);

    final expandido = ref.watch(
      editorSessionProvider.select((s) => s.previewExpanded),
    );

    return Container(
      height: CromoEditor.playbar,
      color: CromoEditor.fundo,
      child: Stack(
        children: [
          Positioned.fill(
            child: _MedidorDeNivel(playback: playback),
          ),
          LayoutBuilder(
            builder: (context, c) {
              // NUM 320 os seis botoes laterais (2 + 4) em 40 estouravam
              // a fileira: o trio do centro e fixo (132), os lados dividem
              // o que sobra, nunca abaixo de 30.
              final lado = c.maxWidth.isFinite
                  ? ((c.maxWidth - 132) / 6).clamp(30.0, 40.0).toDouble()
                  : 40.0;
              return Row(
                children: [
                  _BotaoDoCromo(
                    key: const ValueKey('editor-undo'),
                    icone: CupertinoIcons.arrow_uturn_left,
                    dica: 'Desfazer',
                    largura: lado,
                    onTap: controller.canUndo ? controller.undo : null,
                  ),
                  _BotaoDoCromo(
                    key: const ValueKey('editor-redo'),
                    icone: CupertinoIcons.arrow_uturn_right,
                    dica: 'Refazer',
                    largura: lado,
                    onTap: controller.canRedo ? controller.redo : null,
                  ),
                  Expanded(
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        _BotaoDoCromo(
                          key: const ValueKey('transport-start'),
                          icone: CupertinoIcons.backward_end,
                          dica: marcas.isEmpty
                              ? 'Um quadro atrás · segure para o início'
                              : 'Keyframe anterior · segure para o início',
                          onTap: () => playback.seek(
                            anterior(playback.time.value) ??
                                limitar(playback.time.value - quadro),
                          ),
                          onLongPress: () => playback.seek(Duration.zero),
                        ),
                        ListenableBuilder(
                          listenable: Listenable.merge([
                            playback.playing,
                            playback.loop,
                          ]),
                          builder: (context, _) => Stack(
                            alignment: Alignment.center,
                            children: [
                              _BotaoDoCromo(
                                key: const ValueKey('transport-play'),
                                icone: playback.playing.value
                                    ? CupertinoIcons.pause_fill
                                    : CupertinoIcons.play_fill,
                                dica: playback.loop.value
                                    ? 'Repetição ligada · segure para desligar'
                                    : (playback.playing.value
                                          ? 'Pausar'
                                          : 'Reproduzir · segure para repetir'),
                                cor: playback.loop.value
                                    ? CromoEditor.acao
                                    : CromoEditor.branco,
                                tamanho: 26,
                                largura: 52,
                                onTap: playback.toggle,
                                onLongPress: () =>
                                    playback.loop.value = !playback.loop.value,
                              ),
                              // Repetindo, o play ganha a setinha do laco.
                              if (playback.loop.value)
                                const Positioned(
                                  right: 8,
                                  bottom: 8,
                                  child: IgnorePointer(
                                    child: Icon(
                                      CupertinoIcons.repeat,
                                      key: ValueKey('transport-play-laco'),
                                      size: 11,
                                      color: CromoEditor.acao,
                                    ),
                                  ),
                                ),
                            ],
                          ),
                        ),
                        _BotaoDoCromo(
                          key: const ValueKey('transport-end'),
                          icone: CupertinoIcons.forward_end,
                          dica: marcas.isEmpty
                              ? 'Um quadro à frente · segure para o fim'
                              : 'Próximo keyframe · segure para o fim',
                          onTap: () => playback.seek(
                            proxima(playback.time.value) ??
                                limitar(playback.time.value + quadro),
                          ),
                          onLongPress: () => playback.seek(duration),
                        ),
                      ],
                    ),
                  ),
                  _BotaoDoCromo(
                    key: const ValueKey('playbar-colar'),
                    icone: CupertinoIcons.doc_on_clipboard,
                    dica: 'Copiar e colar',
                    largura: lado,
                    onTap: () => menuDeCopiarEColar(context, ref, playback),
                  ),
                  // O MARCADOR SAIU DAQUI (16/09). Ele chamava
                  // `toggleMarker` no cabecote — exatamente o que tocar
                  // no relogio grande da regua ja faz, e o que o menu ⋮
                  // tambem oferece. Era a terceira porta para a mesma
                  // acao, ocupando um lugar na barra de reproducao. O
                  // relogio continua marcando com um toque e abrindo as
                  // marcas com um toque longo.
                  _BotaoDoCromo(
                    key: const ValueKey('playbar-visualizacao'),
                    icone: !opcoes.visaoDaCamera
                        ? CupertinoIcons.videocam
                        : (opcoes.aberta
                              ? CupertinoIcons.eye_fill
                              : CupertinoIcons.eye),
                    dica: opcoes.aberta
                        ? 'Fechar as opções de visualização'
                        : 'Opções de visualização',
                    cor: opcoes.aberta ? CromoEditor.acao : null,
                    largura: lado,
                    onTap: () => ref
                        .read(opcoesDeVisualizacaoProvider.notifier)
                        .alternarColuna(),
                  ),
                  _BotaoDoCromo(
                    key: const ValueKey('transport-expand'),
                    icone: expandido
                        ? CupertinoIcons.fullscreen_exit
                        : CupertinoIcons.fullscreen,
                    dica: expandido ? 'Sair da tela cheia' : 'Tela cheia',
                    largura: lado,
                    onTap: () => ref
                        .read(editorSessionProvider.notifier)
                        .togglePreviewExpanded(),
                  ),
                ],
              );
            },
          ),
        ],
      ),
    );
  }

}

/// O CONTEUDO DA BARRA DE INFORMACOES: tempo e deslocamento quando o
/// arrasto e no tempo; ate seis pares rotulo/valor quando e no palco.
class _ConteudoDaInfobar extends StatelessWidget {
  const _ConteudoDaInfobar({required this.info});

  final DadosDaInfobar info;

  @override
  Widget build(BuildContext context) {
    const estiloValor = TextStyle(
      color: CromoEditor.branco,
      fontSize: 13,
      fontWeight: FontWeight.w600,
      fontFeatures: [FontFeature.tabularFigures()],
    );
    const estiloRotulo = TextStyle(color: CromoEditor.apagado, fontSize: 10.5);
    final tempo = info.tempo;
    if (tempo != null) {
      final desloc = info.deslocamento ?? Duration.zero;
      return Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Icon(
            CupertinoIcons.rhombus,
            size: 14,
            color: CromoEditor.keyframe,
          ),
          const SizedBox(width: 6),
          AppText(
            tempoDaInfobar(tempo),
            key: const ValueKey('infobar-tempo'),
            style: estiloValor,
          ),
          const SizedBox(width: 22),
          const Icon(
            CupertinoIcons.arrow_right_arrow_left,
            size: 14,
            color: CromoEditor.apagado,
          ),
          const SizedBox(width: 6),
          AppText(
            '${desloc.isNegative ? '' : '+'}${tempoDaInfobar(desloc)}',
            key: const ValueKey('infobar-deslocamento'),
            style: estiloValor,
          ),
        ],
      );
    }
    final pares = info.pares.take(6).toList();
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12),
      child: Row(
        children: [
          for (final (rotulo, valor) in pares)
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  AppText(rotulo, maxLines: 1, style: estiloRotulo),
                  AppText(
                    valor,
                    maxLines: 1,
                    overflow: TextOverflow.fade,
                    style: estiloValor,
                  ),
                ],
              ),
            ),
        ],
      ),
    );
  }
}

/// O MEDIDOR DE NIVEL: uma faixa que respira do centro para as bordas
/// no volume do som que esta tocando. Parado, nao desenha nada.
class _MedidorDeNivel extends ConsumerWidget {
  const _MedidorDeNivel({required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return IgnorePointer(
      child: ValueListenableBuilder<bool>(
        valueListenable: playback.playing,
        builder: (context, tocando, _) {
          if (!tocando) return const SizedBox.shrink();
          return ValueListenableBuilder<Duration>(
            valueListenable: playback.time,
            builder: (context, t, _) {
              final nivel = nivelDeAudioEm(
                ref.read(editorControllerProvider),
                t,
              );
              return CustomPaint(
                key: const ValueKey('medidor-de-nivel'),
                painter: _PintorDoMedidor(nivel),
              );
            },
          );
        },
      ),
    );
  }
}

class _PintorDoMedidor extends CustomPainter {
  const _PintorDoMedidor(this.nivel);

  final double nivel;

  @override
  void paint(Canvas canvas, Size size) {
    if (nivel <= 0.01) return;
    final meia = size.width / 2 * nivel;
    final centro = size.width / 2;
    final faixa = Rect.fromLTRB(centro - meia, 0, centro + meia, size.height);
    canvas.drawRect(
      faixa,
      Paint()
        ..shader = LinearGradient(
          colors: [
            CromoEditor.acao.withValues(alpha: 0),
            CromoEditor.acao.withValues(alpha: .16),
            CromoEditor.acao.withValues(alpha: 0),
          ],
        ).createShader(faixa),
    );
  }

  @override
  bool shouldRepaint(_PintorDoMedidor old) => old.nivel != nivel;
}

/// A BARRA FLUTUANTE DA SELECAO: aparar, dividir, keyframe, duplicar e
/// excluir da camada selecionada, num cartao sobre a timeline.
/// A COLUNA DE OPCOES DE VISUALIZACAO (borda direita do palco, aberta
/// pelo olho da barra de reproducao): pixels, grade, solo, visao da
/// camera e o zoom do palco (+ · 100% · −; tocar no numero volta ao
/// ajustado). Segurar a camera abre a lista de cameras.
class ColunaDeVisualizacao extends ConsumerWidget {
  const ColunaDeVisualizacao({super.key, required this.playback});

  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final selected = ref.watch(selectedLayerProvider);
    final project = ref.watch(editorControllerProvider);
    final soloAtivo = selected != null && project.metaOf(selected).solo;
    final zoom = ref.watch(zoomDoPalcoProvider);
    final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
    final notifier = ref.read(opcoesDeVisualizacaoProvider.notifier);

    void mudarZoom(double fator) {
      final novo = (zoom * fator).clamp(0.25, 4.0);
      ref.read(zoomDoPalcoProvider.notifier).state = novo;
    }

    CameraLayer? primeiraCamera(List<Layer> ls) {
      for (final l in ls) {
        if (l is CameraLayer) return l;
        if (l is GroupLayer) {
          final achada = primeiraCamera(l.children);
          if (achada != null) return achada;
        }
      }
      return null;
    }

    final camera = primeiraCamera(project.layers);

    // A COLUNA CABE NO PALCO: num celular o palco tem pouco mais de 200
    // pontos de altura, e sete alvos de 44 passavam por cima da barra de
    // reproducao — o toque ia parar nela. Os botoes dividem a altura que
    // ha (quatro chaves, dois de zoom e o numero valendo meio).
    return LayoutBuilder(
      builder: (context, c) {
        final h = c.maxHeight.isFinite
            ? ((c.maxHeight - 18) / 6.6).floorToDouble().clamp(18.0, 44.0)
            : 44.0;
        return _colunaDeVisualizacao(
          context,
          ref,
          h: h,
          camera: camera,
          soloAtivo: soloAtivo,
          selected: selected,
          zoom: zoom,
          opcoes: opcoes,
          notifier: notifier,
          mudarZoom: mudarZoom,
        );
      },
    );
  }

  Widget _colunaDeVisualizacao(
    BuildContext context,
    WidgetRef ref, {
    required double h,
    required CameraLayer? camera,
    required bool soloAtivo,
    required String? selected,
    required double zoom,
    required OpcoesDeVisualizacao opcoes,
    required OpcoesDeVisualizacaoNotifier notifier,
    required void Function(double) mudarZoom,
  }) {
    return Container(
      key: const ValueKey('coluna-de-visualizacao'),
      width: 40,
      padding: const EdgeInsets.symmetric(vertical: 4),
      decoration: const BoxDecoration(
        color: CromoEditor.trilho,
        borderRadius: BorderRadius.only(
          topLeft: Radius.circular(12),
          bottomLeft: Radius.circular(12),
        ),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          _BotaoDoCromo(
            key: const ValueKey('visao-pixels'),
            altura: h,
            icone: CupertinoIcons.square_grid_3x2,
            dica: opcoes.pixels
                ? 'Pixels reais ligados'
                : 'Pixels reais (resolução cheia e grade de pixels no zoom)',
            cor: opcoes.pixels ? CromoEditor.acao : CromoEditor.branco,
            tamanho: 18,
            onTap: notifier.alternarPixels,
          ),
          _BotaoDoCromo(
            key: const ValueKey('visao-grade'),
            altura: h,
            icone: CupertinoIcons.grid,
            dica: opcoes.grade ? 'Esconder a grade' : 'Mostrar a grade',
            cor: opcoes.grade ? CromoEditor.acao : CromoEditor.branco,
            tamanho: 18,
            onTap: notifier.alternarGrade,
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-solo'),
            altura: h,
            icone: Icons.center_focus_strong,
            dica: soloAtivo ? 'Tirar do solo' : 'Solo da camada selecionada',
            cor: soloAtivo ? CromoEditor.acao : CromoEditor.branco,
            tamanho: 19,
            onTap: selected == null
                ? null
                : () => ref
                      .read(editorControllerProvider.notifier)
                      .toggleSolo(selected),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-camera'),
            altura: h,
            icone: opcoes.visaoDaCamera
                ? CupertinoIcons.videocam_fill
                : CupertinoIcons.videocam,
            dica: camera == null
                ? 'Visão da câmera (adicione uma em + › Objeto)'
                : (opcoes.visaoDaCamera
                      ? 'Vendo pela câmera · toque para a vista livre, '
                            'segure para as câmeras'
                      : 'Vista livre · toque para ver pela câmera'),
            cor: camera != null && opcoes.visaoDaCamera
                ? CromoEditor.acao
                : CromoEditor.branco,
            tamanho: 18,
            onTap: () {
              if (camera == null) {
                AureaSnack.show(
                  context,
                  'Adicione uma Câmera (+ › Objeto) primeiro',
                );
                return;
              }
              notifier.alternarVisaoDaCamera();
            },
            onLongPress: camera == null
                ? null
                : () => showCamerasSheet(context, ref, camera.id, playback),
          ),
          Container(
            width: 22,
            height: 1,
            margin: const EdgeInsets.symmetric(vertical: 3),
            color: CromoEditor.apagado.withValues(alpha: .2),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-zoom-mais'),
            altura: h,
            icone: Icons.zoom_in,
            dica: 'Aproximar o palco',
            tamanho: 19,
            onTap: zoom >= 4.0 ? null : () => mudarZoom(1.25),
          ),
          Tooltip(
            message: 'Voltar ao ajustado',
            child: Tocavel(
              key: const ValueKey('rail-zoom-texto'),
              onTap: zoom == 1.0
                  ? null
                  : () => ref.read(zoomDoPalcoProvider.notifier).state = 1.0,
              child: SizedBox(
                width: 40,
                height: h * .6,
                child: Center(
                  child: AppText(
                    '${(zoom * 100).round()}%',
                    style: TextStyle(
                      fontSize: 9.5,
                      color: zoom == 1.0
                          ? CromoEditor.apagado
                          : CromoEditor.acao,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ),
            ),
          ),
          _BotaoDoCromo(
            key: const ValueKey('rail-zoom-menos'),
            altura: h,
            icone: Icons.zoom_out,
            dica: 'Afastar o palco',
            tamanho: 19,
            onTap: zoom <= 0.25 ? null : () => mudarZoom(0.8),
          ),
        ],
      ),
    );
  }
}

/// O ZOOM SOBRE O PALCO: com o palco fora do ajustado, o numero fica no
/// canto de cima a esquerda; tocar volta ao ajustado.
class IndicadorDeZoomDoPalco extends ConsumerWidget {
  const IndicadorDeZoomDoPalco({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final zoom = ref.watch(zoomDoPalcoProvider);
    if (zoom == 1.0) return const SizedBox.shrink();
    return Tooltip(
      message: 'Voltar ao ajustado',
      child: Tocavel(
        key: const ValueKey('preview-zoom-indicador'),
        onTap: () => ref.read(zoomDoPalcoProvider.notifier).state = 1.0,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
          decoration: BoxDecoration(
            color: Colors.black.withValues(alpha: .55),
            borderRadius: BorderRadius.circular(999),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.zoom_in, size: 14, color: CromoEditor.branco),
              const SizedBox(width: 4),
              AppText(
                '${(zoom * 100).round()}%',
                style: const TextStyle(
                  color: CromoEditor.branco,
                  fontSize: 11.5,
                  fontWeight: FontWeight.w700,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// O ⋮ DA TIMELINE (canto inferior esquerdo): tudo o que vale para o
/// projeto inteiro ou para a linha do tempo, e nao para uma camada —
/// selecao, reproducao, modo de previa, miniatura, marcas de introducao
/// e final, marcadores e batidas, cronometro de edicao, agrupar e guia.
Future<void> menuDaTimeline(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback, {
  required VoidCallback onAgrupar,
  required VoidCallback onGuia,
  VoidCallback? onDefinirMiniatura,
}) async {
  playback.pause();
  await showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AmColors.panel,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
    ),
    builder: (folha) => _MenuDaTimeline(
      playback: playback,
      contextoDoEditor: context,
      onAgrupar: onAgrupar,
      onGuia: onGuia,
      onDefinirMiniatura: onDefinirMiniatura,
    ),
  );
}

class _MenuDaTimeline extends ConsumerWidget {
  const _MenuDaTimeline({
    required this.playback,
    required this.contextoDoEditor,
    required this.onAgrupar,
    required this.onGuia,
    required this.onDefinirMiniatura,
  });

  final PlaybackController playback;

  /// O contexto do editor: as folhas que este menu abre nascem dele, e
  /// nao da folha que se fecha.
  final BuildContext contextoDoEditor;
  final VoidCallback onAgrupar;
  final VoidCallback onGuia;
  final VoidCallback? onDefinirMiniatura;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final projeto = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final opcoes = ref.watch(opcoesDeVisualizacaoProvider);
    final cronometro = ref.watch(cronometroDeEdicaoProvider(projeto.id));
    final agora = playback.time.value;
    final expandido = ref.watch(
      editorSessionProvider.select((s) => s.previewExpanded),
    );

    void fechar() => Navigator.of(context).pop();
    void fecharE(VoidCallback acao) {
      fechar();
      acao();
    }

    Widget item(
      String chave,
      IconData icone,
      String rotulo, {
      String? detalhe,
      bool? marcado,
      bool radio = false,
      required VoidCallback? onTap,
    }) => ItemDoMenu(
      chave: chave,
      icone: icone,
      rotulo: rotulo,
      detalhe: detalhe,
      marcado: marcado,
      radio: radio,
      onTap: onTap,
    );

    Widget secao(String titulo) => Padding(
      padding: const EdgeInsets.fromLTRB(20, 14, 20, 4),
      child: AppText(
        titulo,
        style: const TextStyle(
          color: AmColors.muted,
          fontSize: 12,
          fontWeight: FontWeight.w600,
          letterSpacing: .2,
        ),
      ),
    );

    final estado = cronometro.estado;
    final todas = [for (final l in projeto.layers) l.id];

    return SafeArea(
      child: ConstrainedBox(
        constraints: BoxConstraints(
          maxHeight: MediaQuery.sizeOf(context).height * .78,
        ),
        child: ListView(
          key: const ValueKey('timeline-menu'),
          shrinkWrap: true,
          padding: const EdgeInsets.only(bottom: 12),
          children: [
            const SizedBox(height: 6),
            Center(
              child: Container(
                width: 36,
                height: 4,
                decoration: BoxDecoration(
                  color: AmColors.muted.withValues(alpha: .4),
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
            ),
            secao('Seleção'),
            item(
              'timeline-menu-selecionar-todas',
              CupertinoIcons.checkmark_square,
              'Selecionar todas as camadas',
              onTap: todas.length < 2
                  ? null
                  : () => fecharE(() {
                      ref.read(selectedLayerProvider.notifier).state = null;
                      ref.read(multiSelectProvider.notifier).state = todas
                          .toSet();
                    }),
            ),
            item(
              'timeline-menu-limpar-selecao',
              CupertinoIcons.square,
              'Limpar seleção',
              onTap: () => fecharE(() {
                ref.read(multiSelectProvider.notifier).state = const {};
                ref.read(selectedLayerProvider.notifier).state = null;
              }),
            ),
            secao('Reprodução e prévia'),
            item(
              'timeline-menu-loop',
              CupertinoIcons.repeat,
              'Reprodução em loop',
              marcado: playback.loop.value,
              onTap: () => fecharE(
                () => playback.loop.value = !playback.loop.value,
              ),
            ),
            item(
              'timeline-menu-tela-cheia',
              CupertinoIcons.fullscreen,
              expandido ? 'Sair da tela cheia' : 'Tela cheia',
              onTap: () => fecharE(
                () => ref
                    .read(editorSessionProvider.notifier)
                    .togglePreviewExpanded(),
              ),
            ),
            for (final modo in ModoDePrevia.values)
              item(
                'timeline-menu-modo-${modo.name}',
                switch (modo) {
                  ModoDePrevia.resultadoFinal => CupertinoIcons.sparkles,
                  ModoDePrevia.semEfeitos => CupertinoIcons.wand_rays_inverse,
                  ModoDePrevia.meioTransparente =>
                    CupertinoIcons.circle_lefthalf_fill,
                },
                'Prévia: ${rotuloDoModoDePrevia(modo)}',
                marcado: opcoes.modo == modo,
                radio: true,
                onTap: () => fecharE(
                  () => ref
                      .read(opcoesDeVisualizacaoProvider.notifier)
                      .definirModo(modo),
                ),
              ),
            secao('Projeto'),
            item(
              'timeline-menu-aparar-projeto',
              CupertinoIcons.scissors_alt,
              'Aparar o projeto no cabeçote',
              detalhe: 'Corta tudo o que passa de ${tempoDaInfobar(agora)}',
              onTap: agora <= Duration.zero
                  ? null
                  : () => fecharE(() {
                      controller.aparaProjetoNoCabecote(agora);
                      AureaSnack.show(
                        contextoDoEditor,
                        'Projeto aparado no cabeçote',
                        actionLabel: 'Desfazer',
                        onAction: controller.undo,
                      );
                    }),
            ),
            item(
              'timeline-menu-miniatura',
              CupertinoIcons.photo,
              'Usar este quadro como miniatura',
              detalhe: projeto.thumbTime == null
                  ? null
                  : 'Hoje: ${tempoDaInfobar(projeto.thumbTime!)}',
              onTap: onDefinirMiniatura == null
                  ? null
                  : () => fecharE(onDefinirMiniatura!),
            ),
            if (projeto.thumbTime != null)
              item(
                'timeline-menu-limpar-miniatura',
                CupertinoIcons.photo_on_rectangle,
                'Voltar à miniatura automática',
                onTap: () =>
                    fecharE(() => controller.definirQuadroDaMiniatura(null)),
              ),
            item(
              'timeline-menu-intro',
              CupertinoIcons.arrow_right_to_line,
              'Marcar aqui o fim da introdução',
              detalhe: projeto.introFim == null
                  ? 'Esticado noutro projeto, a introdução toca intacta'
                  : 'Hoje: ${tempoDaInfobar(projeto.introFim!)}',
              onTap: () =>
                  fecharE(() => controller.marcarFimDaIntroducao(agora)),
            ),
            if (projeto.introFim != null)
              item(
                'timeline-menu-intro-tirar',
                CupertinoIcons.xmark,
                'Tirar a marca da introdução',
                onTap: () =>
                    fecharE(() => controller.marcarFimDaIntroducao(null)),
              ),
            item(
              'timeline-menu-final',
              CupertinoIcons.arrow_left_to_line,
              'Marcar aqui o começo do final',
              detalhe: projeto.finalInicio == null
                  ? 'Esticado noutro projeto, o final toca intacto'
                  : 'Hoje: ${tempoDaInfobar(projeto.finalInicio!)}',
              onTap: () =>
                  fecharE(() => controller.marcarInicioDoFinal(agora)),
            ),
            if (projeto.finalInicio != null)
              item(
                'timeline-menu-final-tirar',
                CupertinoIcons.xmark,
                'Tirar a marca do final',
                onTap: () =>
                    fecharE(() => controller.marcarInicioDoFinal(null)),
              ),
            secao('Marcas e ritmo'),
            item(
              'timeline-menu-marcador',
              CupertinoIcons.bookmark,
              'Marcar este instante',
              onTap: () => fecharE(
                () => controller.toggleMarker(playback.timeForInput()),
              ),
            ),
            item(
              'timeline-menu-marcas',
              CupertinoIcons.bookmark_solid,
              'Marcas na timeline',
              detalhe: projeto.markers.isEmpty
                  ? null
                  : '${projeto.markers.length}',
              onTap: () => fecharE(
                () => menuDasMarcas(contextoDoEditor, ref, playback),
              ),
            ),
            item(
              'timeline-menu-batidas',
              CupertinoIcons.music_note_2,
              'Batidas da música',
              onTap: () => fecharE(() async {
                final som = ref
                    .read(editorControllerProvider)
                    .layers
                    .where((l) => l is AudioLayer || l is VideoLayer)
                    .firstOrNull;
                if (som == null) {
                  AureaSnack.show(
                    contextoDoEditor,
                    'Adicione um áudio ou um vídeo primeiro',
                  );
                  return;
                }
                await showBeatsSheet(contextoDoEditor, ref, som.id);
              }),
            ),
            secao('Cronômetro de edição'),
            if (estado == EstadoDoCronometro.parado)
              item(
                'timeline-menu-cronometro-iniciar',
                CupertinoIcons.timer,
                'Iniciar o cronômetro',
                detalhe: 'Conta o tempo que você passa editando este projeto',
                onTap: () => fecharE(cronometro.iniciar),
              ),
            if (estado == EstadoDoCronometro.rodando)
              item(
                'timeline-menu-cronometro-pausar',
                CupertinoIcons.pause_circle,
                'Pausar o cronômetro',
                detalhe: textoDoCronometro(cronometro.total),
                onTap: () => fecharE(cronometro.pausar),
              ),
            if (estado == EstadoDoCronometro.pausado)
              item(
                'timeline-menu-cronometro-retomar',
                CupertinoIcons.play_circle,
                'Retomar o cronômetro',
                detalhe: textoDoCronometro(cronometro.total),
                onTap: () => fecharE(cronometro.iniciar),
              ),
            if (estado != EstadoDoCronometro.parado)
              item(
                'timeline-menu-cronometro-apagar',
                CupertinoIcons.trash,
                'Apagar o cronômetro',
                onTap: () => fecharE(cronometro.apagar),
              ),
            secao('Mais'),
            item(
              'timeline-menu-agrupar',
              CupertinoIcons.rectangle_stack,
              'Agrupar camadas…',
              onTap: () => fecharE(onAgrupar),
            ),
            item(
              'timeline-menu-guia',
              CupertinoIcons.book,
              'Guia rápido',
              onTap: () => fecharE(onGuia),
            ),
          ],
        ),
      ),
    );
  }
}

/// O CRONOMETRO NA BARRA DO PROJETO: so aparece contando, e anda de
/// segundo em segundo.
class _ChipDoCronometro extends ConsumerStatefulWidget {
  const _ChipDoCronometro({required this.projetoId});

  final String projetoId;

  @override
  ConsumerState<_ChipDoCronometro> createState() => _ChipDoCronometroState();
}

class _ChipDoCronometroState extends ConsumerState<_ChipDoCronometro> {
  Timer? _tique;

  @override
  void dispose() {
    _tique?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final cronometro = ref.watch(cronometroDeEdicaoProvider(widget.projetoId));
    final rodando = cronometro.estado == EstadoDoCronometro.rodando;
    if (rodando && _tique == null) {
      _tique = Timer.periodic(const Duration(seconds: 1), (_) {
        if (mounted) setState(() {});
      });
    } else if (!rodando && _tique != null) {
      _tique!.cancel();
      _tique = null;
    }
    if (!rodando) return const SizedBox.shrink();
    return Tooltip(
      message: 'Tempo de edição deste projeto',
      child: Container(
        key: const ValueKey('navbar-cronometro'),
        margin: const EdgeInsets.only(right: 2),
        padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
        decoration: BoxDecoration(
          color: CromoEditor.trilho,
          borderRadius: BorderRadius.circular(999),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(
              CupertinoIcons.timer,
              size: 12,
              color: CromoEditor.acao,
            ),
            const SizedBox(width: 3),
            AppText(
              textoDoCronometro(cronometro.total),
              style: const TextStyle(
                color: CromoEditor.branco,
                fontSize: 11,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// IR PARA O TEMPO — o relogio da navbar aceita "12.5", "1:02.5" etc.
Future<void> digitarTempo(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
) async {
  playback.pause();
  final project = ref.read(editorControllerProvider);
  final total = project.duration;
  final fps = project.fps;
  final ctrl = TextEditingController(
    text: (playback.time.value.inMilliseconds / 1000).toStringAsFixed(2),
  );
  final r = await showCupertinoDialog<String>(
    context: context,
    builder: (ctx) => CupertinoAlertDialog(
      title: const AppText('Ir para o tempo'),
      content: Padding(
        padding: const EdgeInsets.only(top: 12),
        child: CupertinoTextField(
          key: const ValueKey('transport-timecode-campo'),
          controller: ctrl,
          autofocus: true,
          keyboardType: const TextInputType.numberWithOptions(decimal: true),
          placeholder: translate(context, 'segundos, ou mm:ss.ms'),
          onSubmitted: (v) => Navigator.pop(ctx, v),
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.pop(ctx),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.pop(ctx, ctrl.text),
          child: const AppText('Ir'),
        ),
      ],
    ),
  );
  ctrl.dispose();
  final t = parseTimecodeInput(r ?? '', fps);
  if (t == null) return;
  playback.seek(t < Duration.zero ? Duration.zero : (t > total ? total : t));
}
