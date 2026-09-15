import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../application/info_da_midia.dart';
import '../../application/playback_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../application/ui/pro_mode.dart';
import '../../domain/layer.dart';
import '../../domain/layer_meta.dart';
import '../../domain/layout_ops.dart';
import '../../domain/mask.dart';
import '../am/am_colors.dart';
import '../am/audio_sheet.dart';
import '../am/freeze_sheet.dart';
import '../am/layer_menu.dart' show showParentSheet;
import '../am/speed_sheet.dart';
import '../context/quick_actions.dart';
import 'layer_actions.dart';

/// UMA LINHA DE MENU EM FOLHA: icone, rotulo, detalhe opcional e, quando
/// e uma escolha, o visto a direita. Sem ripple, com realce sutil (iOS).
class ItemDoMenu extends StatelessWidget {
  const ItemDoMenu({
    super.key,
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.onTap,
    this.detalhe,
    this.marcado,
    this.radio = false,
    this.perigo = false,
  });

  final String chave;
  final IconData icone;
  final String rotulo;
  final String? detalhe;
  final bool? marcado;
  final bool radio;
  final bool perigo;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final ativo = onTap != null;
    final cor = !ativo
        ? AmColors.muted.withValues(alpha: .6)
        : (perigo ? AmColors.pink : AmColors.text);
    return Tocavel(
      key: ValueKey(chave),
      onTap: onTap == null
          ? null
          : () {
              HapticFeedback.selectionClick();
              onTap!();
            },
      child: ConstrainedBox(
        constraints: const BoxConstraints(minHeight: 48),
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 8),
          child: Row(
            children: [
              Icon(icone, size: 20, color: cor),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    AppText(rotulo, style: TextStyle(color: cor, fontSize: 15)),
                    if (detalhe != null)
                      AppText(
                        detalhe!,
                        style: const TextStyle(
                          color: AmColors.muted,
                          fontSize: 12,
                        ),
                      ),
                  ],
                ),
              ),
              if (marcado != null)
                Icon(
                  marcado!
                      ? (radio
                            ? CupertinoIcons.largecircle_fill_circle
                            : CupertinoIcons.checkmark_alt)
                      : (radio ? CupertinoIcons.circle : null),
                  size: 18,
                  color: marcado! ? AmColors.action : AmColors.muted,
                ),
            ],
          ),
        ),
      ),
    );
  }
}

/// O titulo de uma secao do menu em folha.
class SecaoDoMenu extends StatelessWidget {
  const SecaoDoMenu(this.titulo, {super.key});

  final String titulo;

  @override
  Widget build(BuildContext context) => Padding(
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
}

/// A FOLHA DE MENU: alca, lista rolavel que nunca passa de 80% da tela.
Future<T?> mostrarFolhaDeMenu<T>(
  BuildContext context, {
  required String chave,
  required List<Widget> Function(BuildContext folha) itens,
}) {
  return showModalBottomSheet<T>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AmColors.panel,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
    ),
    builder: (folha) => SafeArea(
      child: ConstrainedBox(
        constraints: BoxConstraints(
          maxHeight: MediaQuery.sizeOf(folha).height * .8,
        ),
        child: ListView(
          key: ValueKey(chave),
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
            ...itens(folha),
          ],
        ),
      ),
    ),
  );
}

/// O ⋯ DA BARRA DA CAMADA: tudo o que se faz com UMA camada, com rotulo
/// — camada, etiqueta, recorte e grupo, midia, tempo e o resto das acoes.
Future<void> menuDaCamada(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  playback.pause();
  await mostrarFolhaDeMenu<void>(
    context,
    chave: 'camada-menu-folha',
    itens: (folha) => [
      Consumer(
        builder: (_, ref, _) =>
            _ItensDaCamada(
              layerId: layerId,
              playback: playback,
              contextoDoEditor: context,
              folha: folha,
            ),
      ),
    ],
  );
}

class _ItensDaCamada extends ConsumerWidget {
  const _ItensDaCamada({
    required this.layerId,
    required this.playback,
    required this.contextoDoEditor,
    required this.folha,
  });

  final String layerId;
  final PlaybackController playback;
  final BuildContext contextoDoEditor;
  final BuildContext folha;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final projeto = ref.watch(editorControllerProvider);
    final layer = projeto.layerById(layerId);
    if (layer == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final t = playback.time.value;
    final ctx = contextoDoEditor;

    void fecharE(VoidCallback acao) {
      Navigator.of(folha).pop();
      acao();
    }

    final visual = layer is! AudioLayer;
    final midia = layer is ImageLayer || layer is VideoLayer;
    final caminho = switch (layer) {
      ImageLayer l => l.sourcePath,
      VideoLayer l => l.sourcePath,
      AudioLayer l => l.sourcePath,
      _ => null,
    };
    final base = c.baseDeRecorteAbaixo(layerId);
    final recortada = layer.matteMode == MatteMode.recorte;
    final etiqueta = projeto.metaOf(layerId).label;
    final idx = projeto.layers.indexWhere((l) => l.id == layerId);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const SecaoDoMenu('Camada'),
        ItemDoMenu(
          chave: 'camada-menu-renomear',
          icone: CupertinoIcons.pencil,
          rotulo: 'Renomear',
          onTap: () => fecharE(() => renomearCamada(ctx, ref, layer)),
        ),
        ItemDoMenu(
          chave: 'camada-menu-duplicar',
          icone: CupertinoIcons.plus_square_on_square,
          rotulo: 'Duplicar',
          onTap: () => fecharE(() => c.duplicateLayer(layerId)),
        ),
        ItemDoMenu(
          chave: 'camada-menu-copiar',
          icone: CupertinoIcons.doc_on_doc,
          rotulo: 'Copiar camada',
          onTap: () => fecharE(() {
            c.copiarCamada(layerId);
            AureaSnack.show(ctx, 'Camada copiada');
          }),
        ),
        ItemDoMenu(
          chave: 'camada-menu-colar',
          icone: CupertinoIcons.doc_on_clipboard,
          rotulo: 'Colar camada no cabeçote',
          onTap: c.temCamadaCopiada
              ? () => fecharE(() => c.colarCamada(t, acimaDe: layerId))
              : null,
        ),
        ItemDoMenu(
          chave: 'camada-menu-copiar-estilo',
          icone: CupertinoIcons.paintbrush,
          rotulo: 'Copiar estilo',
          onTap: () => fecharE(() {
            c.copiarEstilo(layerId);
            AureaSnack.show(ctx, 'Estilo copiado');
          }),
        ),
        ItemDoMenu(
          chave: 'camada-menu-colar-estilo',
          icone: CupertinoIcons.paintbrush_fill,
          rotulo: 'Colar estilo…',
          onTap: c.categoriasColaveis(layerId).isEmpty
              ? null
              : () => fecharE(() => mostrarColarEstilo(ctx, ref, layerId)),
        ),
        ItemDoMenu(
          chave: 'camada-menu-subir',
          icone: CupertinoIcons.arrow_up_to_line,
          rotulo: 'Trazer para a frente',
          onTap: idx <= 0 ? null : () => fecharE(() => c.reorderLayer(layerId, -1)),
        ),
        ItemDoMenu(
          chave: 'camada-menu-descer',
          icone: CupertinoIcons.arrow_down_to_line,
          rotulo: 'Enviar para trás',
          onTap: idx < 0 || idx >= projeto.layers.length - 1
              ? null
              : () => fecharE(() => c.reorderLayer(layerId, 1)),
        ),
        const SecaoDoMenu('Etiqueta'),
        _LinhaDeEtiquetas(
          atual: etiqueta,
          onEscolher: (l) => c.setLayerLabel(layerId, l),
        ),
        if (visual) ...[
          const SecaoDoMenu('Recorte e grupo'),
          if (!recortada)
            ItemDoMenu(
              chave: 'camada-menu-recorte',
              icone: CupertinoIcons.arrow_turn_left_down,
              rotulo: 'Recortar pela camada de baixo',
              detalhe: base == null
                  ? 'Não há camada visível logo abaixo'
                  : 'Só aparece onde "${base.name}" tem imagem',
              onTap: base == null
                  ? null
                  : () => fecharE(() => c.recortarPelaDeBaixo(layerId)),
            )
          else
            ItemDoMenu(
              chave: 'camada-menu-soltar-recorte',
              icone: CupertinoIcons.arrow_turn_up_right,
              rotulo: 'Soltar o recorte',
              onTap: () => fecharE(
                () => c.setMatte(layerId, MatteMode.none, null),
              ),
            ),
          if (layer is! GroupLayer)
            ItemDoMenu(
              chave: 'camada-menu-agrupar',
              icone: CupertinoIcons.rectangle_stack,
              rotulo: 'Converter em grupo',
              onTap: () => fecharE(() => c.groupLayer(layerId)),
            ),
          if (layer is GroupLayer) ...[
            ItemDoMenu(
              chave: 'camada-menu-entrar',
              icone: CupertinoIcons.arrow_down_right_square,
              rotulo: 'Editar o grupo',
              onTap: () => fecharE(() => c.enterGroup(layerId)),
            ),
            ItemDoMenu(
              chave: 'camada-menu-desagrupar',
              icone: CupertinoIcons.square_split_2x2,
              rotulo: 'Desagrupar',
              onTap: () => fecharE(() => c.ungroupLayer(layerId)),
            ),
            ItemDoMenu(
              chave: 'camada-menu-grupo-mascara',
              icone: CupertinoIcons.square_stack_3d_down_right_fill,
              rotulo: 'Grupo de máscara',
              detalhe: 'A camada de cima mostra só o que cobre',
              marcado:
                  layer.children.isNotEmpty &&
                  layer.children.first.blendMode == BlendMode.dstIn,
              onTap: layer.children.length < 2
                  ? null
                  : () => fecharE(() {
                      final ligado =
                          layer.children.first.blendMode == BlendMode.dstIn;
                      c.definirFormaDoGrupo(
                        layerId,
                        ligado ? null : BlendMode.dstIn,
                      );
                    }),
            ),
            ItemDoMenu(
              chave: 'camada-menu-grupo-recorte',
              icone: CupertinoIcons.square_stack_3d_down_right,
              rotulo: 'Grupo de recorte',
              detalhe: 'A camada de cima fura as de baixo',
              marcado:
                  layer.children.isNotEmpty &&
                  layer.children.first.blendMode == BlendMode.dstOut,
              onTap: layer.children.length < 2
                  ? null
                  : () => fecharE(() {
                      final ligado =
                          layer.children.first.blendMode == BlendMode.dstOut;
                      c.definirFormaDoGrupo(
                        layerId,
                        ligado ? null : BlendMode.dstOut,
                      );
                    }),
            ),
          ],
          const SecaoDoMenu('Na composição'),
          ItemDoMenu(
            chave: 'camada-menu-caber',
            icone: CupertinoIcons.rectangle_arrow_up_right_arrow_down_left,
            rotulo: 'Caber na composição',
            onTap: () => fecharE(
              () => c.encaixarNaComposicao(
                layerId,
                EncaixeNaComposicao.caber,
                t,
              ),
            ),
          ),
          ItemDoMenu(
            chave: 'camada-menu-preencher',
            icone: CupertinoIcons.fullscreen,
            rotulo: 'Preencher a composição',
            onTap: () => fecharE(
              () => c.encaixarNaComposicao(
                layerId,
                EncaixeNaComposicao.preencher,
                t,
              ),
            ),
          ),
          ItemDoMenu(
            chave: 'camada-menu-esticar',
            icone: CupertinoIcons.arrow_up_left_arrow_down_right,
            rotulo: 'Esticar até as bordas',
            onTap: () => fecharE(
              () => c.encaixarNaComposicao(
                layerId,
                EncaixeNaComposicao.esticar,
                t,
              ),
            ),
          ),
          ItemDoMenu(
            chave: 'camada-menu-espelhar-h',
            icone: CupertinoIcons.arrow_left_right_square,
            rotulo: 'Espelhar na horizontal',
            onTap: () =>
                fecharE(() => c.espelharCamada(layerId, horizontal: true)),
          ),
          ItemDoMenu(
            chave: 'camada-menu-espelhar-v',
            icone: CupertinoIcons.arrow_up_down_square,
            rotulo: 'Espelhar na vertical',
            onTap: () =>
                fecharE(() => c.espelharCamada(layerId, horizontal: false)),
          ),
        ],
        if (midia || layer is AudioLayer) ...[
          const SecaoDoMenu('Mídia'),
          if (caminho != null)
            ItemDoMenu(
              chave: 'camada-menu-info-midia',
              icone: CupertinoIcons.info_circle,
              rotulo: 'Informações da mídia',
              onTap: () =>
                  fecharE(() => mostrarInfoDaMidia(ctx, caminho, layer.name)),
            ),
          if (layer is VideoLayer)
            ItemDoMenu(
              chave: 'camada-menu-extrair-audio',
              icone: CupertinoIcons.music_note_2,
              rotulo: 'Extrair o áudio',
              detalhe: 'O som vira uma camada própria e o vídeo fica mudo',
              onTap: () => fecharE(() => extrairAudioComAviso(ctx, ref, layer)),
            ),
          if (layer is AudioLayer || layer is VideoLayer)
            ItemDoMenu(
              chave: 'camada-menu-volume',
              icone: CupertinoIcons.speaker_2,
              rotulo: 'Volume',
              onTap: () => fecharE(
                () => showAudioSheet(ctx, ref, layerId, playback: playback),
              ),
            ),
        ],
        const SecaoDoMenu('Tempo'),
        ItemDoMenu(
          chave: 'camada-menu-aparar-inicio',
          icone: CupertinoIcons.arrow_right_to_line,
          rotulo: 'Aparar o início no cabeçote',
          onTap: layer.activeAt(t)
              ? () => fecharE(() => c.trimLayerStart(layerId, t))
              : null,
        ),
        ItemDoMenu(
          chave: 'camada-menu-dividir',
          icone: CupertinoIcons.scissors,
          rotulo: 'Dividir no cabeçote',
          onTap: layer.activeAt(t)
              ? () => fecharE(() => c.splitLayer(layerId, t))
              : null,
        ),
        ItemDoMenu(
          chave: 'camada-menu-aparar-fim',
          icone: CupertinoIcons.arrow_left_to_line,
          rotulo: 'Aparar o fim no cabeçote',
          onTap: layer.activeAt(t)
              ? () => fecharE(() => c.trimLayerEnd(layerId, t))
              : null,
        ),
        if (layer is VideoLayer || layer is AudioLayer)
          ItemDoMenu(
            chave: 'camada-menu-velocidade',
            icone: CupertinoIcons.speedometer,
            rotulo: 'Velocidade e remapear o tempo',
            onTap: () => fecharE(
              () => showSpeedSheet(ctx, ref, layerId, playback: playback),
            ),
          ),
        if (layer is VideoLayer)
          ItemDoMenu(
            chave: 'camada-menu-congelar',
            icone: CupertinoIcons.snow,
            rotulo: 'Congelar quadro',
            onTap: layer.activeAt(t)
                ? () => fecharE(() => showFreezeSheet(ctx, ref, layerId, t))
                : null,
          ),
        const SecaoDoMenu('Mais'),
        ItemDoMenu(
          chave: 'camada-menu-parentesco',
          icone: CupertinoIcons.link,
          rotulo: 'Seguir outra camada (parentesco)',
          onTap: () => fecharE(() => showParentSheet(ctx, ref, layer, t)),
        ),
        ItemDoMenu(
          chave: 'camada-menu-todas',
          icone: CupertinoIcons.square_grid_2x2,
          rotulo: 'Todas as ações…',
          onTap: () => fecharE(
            () => showAllActionsSheet(
              ctx,
              quickActionsFor(
                ctx,
                ref,
                layer,
                playback,
                pro: ref.read(proModeProvider),
                onAnimarTexto: () => ref
                    .read(editorSessionProvider.notifier)
                    .openPanel(EditorPanel.animators),
              ),
            ),
          ),
        ),
        ItemDoMenu(
          chave: 'camada-menu-excluir',
          icone: CupertinoIcons.trash,
          rotulo: 'Excluir camada',
          perigo: true,
          onTap: () => fecharE(() => excluirCamadas(ctx, ref, {layerId})),
        ),
      ],
    );
  }
}

/// A LINHA DE ETIQUETAS: "sem" e as doze cores; a escolhida ganha anel.
class _LinhaDeEtiquetas extends StatelessWidget {
  const _LinhaDeEtiquetas({required this.atual, required this.onEscolher});

  final LayerLabel? atual;
  final ValueChanged<LayerLabel?> onEscolher;

  @override
  Widget build(BuildContext context) {
    Widget bolinha(String chave, LayerLabel? l) {
      final escolhida = l == null
          ? atual == null
          : atual?.color.toARGB32() == l.color.toARGB32();
      return Tooltip(
        message: l?.name ?? 'Sem etiqueta',
        child: Tocavel(
          key: ValueKey(chave),
          onTap: () {
            HapticFeedback.selectionClick();
            onEscolher(l);
          },
          child: Container(
            width: 34,
            height: 34,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              border: Border.all(
                color: escolhida ? AmColors.text : Colors.transparent,
                width: 2,
              ),
            ),
            child: Container(
              width: 24,
              height: 24,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: l?.color ?? Colors.transparent,
                border: l == null
                    ? Border.all(color: AmColors.muted, width: 1.5)
                    : null,
              ),
              child: l == null
                  ? const Icon(
                      CupertinoIcons.nosign,
                      size: 14,
                      color: AmColors.muted,
                    )
                  : null,
            ),
          ),
        ),
      );
    }

    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 4),
      child: Wrap(
        spacing: 2,
        runSpacing: 2,
        children: [
          bolinha('camada-etiqueta-sem', null),
          for (var i = 0; i < LayerLabel.palette.length; i++)
            bolinha('camada-etiqueta-$i', LayerLabel.palette[i]),
        ],
      ),
    );
  }
}

/// COLAR ESTILO: escolher o que vai. So aparece o que faz sentido entre
/// as duas camadas; tudo nasce marcado.
Future<void> mostrarColarEstilo(
  BuildContext context,
  WidgetRef ref,
  String destinoId,
) async {
  final c = ref.read(editorControllerProvider.notifier);
  final possiveis = c.categoriasColaveis(destinoId);
  if (possiveis.isEmpty) {
    AureaSnack.show(context, 'Copie o estilo de outra camada primeiro');
    return;
  }
  final escolhidas = {...possiveis};
  final n = await mostrarFolhaDeMenu<int>(
    context,
    chave: 'colar-estilo-folha',
    itens: (folha) => [
      StatefulBuilder(
        builder: (context, setFolha) => Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            const SecaoDoMenu('Colar estilo'),
            for (final cat in CategoriaDeEstilo.values)
              if (possiveis.contains(cat))
                ItemDoMenu(
                  chave: 'colar-estilo-${cat.name}',
                  icone: switch (cat) {
                    CategoriaDeEstilo.corEPreenchimento =>
                      CupertinoIcons.drop_fill,
                    CategoriaDeEstilo.bordaESombra =>
                      CupertinoIcons.square_on_square,
                    CategoriaDeEstilo.mesclagemEOpacidade =>
                      CupertinoIcons.circle_lefthalf_fill,
                    CategoriaDeEstilo.moverETransformar =>
                      CupertinoIcons.move,
                    CategoriaDeEstilo.estiloDeTexto =>
                      CupertinoIcons.textformat,
                    CategoriaDeEstilo.volume => CupertinoIcons.speaker_2,
                    CategoriaDeEstilo.efeitos => CupertinoIcons.sparkles,
                    CategoriaDeEstilo.velocidade =>
                      CupertinoIcons.speedometer,
                  },
                  rotulo: rotuloDaCategoriaDeEstilo(cat),
                  marcado: escolhidas.contains(cat),
                  onTap: () => setFolha(() {
                    if (!escolhidas.remove(cat)) escolhidas.add(cat);
                  }),
                ),
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 12, 20, 4),
              child: Row(
                children: [
                  Expanded(
                    child: CupertinoButton(
                      key: const ValueKey('colar-estilo-cancelar'),
                      color: AmColors.chip,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: () => Navigator.of(folha).pop(),
                      child: const AppText(
                        'Cancelar',
                        style: TextStyle(color: AmColors.text, fontSize: 15),
                      ),
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: CupertinoButton(
                      key: const ValueKey('colar-estilo-colar'),
                      color: AmColors.action,
                      borderRadius: BorderRadius.circular(12),
                      onPressed: escolhidas.isEmpty
                          ? null
                          : () => Navigator.of(
                              folha,
                            ).pop(c.colarEstilo(destinoId, escolhidas)),
                      child: const AppText(
                        'Colar',
                        style: TextStyle(
                          color: AmColors.onAction,
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    ],
  );
  if (n != null && n > 0 && context.mounted) {
    AureaSnack.show(context, 'Estilo colado');
  }
}

/// O BOTAO COPIAR E COLAR DA BARRA DE REPRODUCAO: camada, selecao, estilo
/// e efeitos — o que vale para a selecao atual ou para a timeline.
Future<void> menuDeCopiarEColar(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
) async {
  final c = ref.read(editorControllerProvider.notifier);
  final selecionada = ref.read(selectedLayerProvider);
  final todas = [
    for (final l in ref.read(editorControllerProvider).layers) l.id,
  ];
  final t = playback.time.value;
  await mostrarFolhaDeMenu<void>(
    context,
    chave: 'copiar-colar-folha',
    itens: (folha) {
      void fecharE(VoidCallback acao) {
        Navigator.of(folha).pop();
        acao();
      }

      return [
        const SecaoDoMenu('Copiar e colar'),
        ItemDoMenu(
          chave: 'colar-copiar-camada',
          icone: CupertinoIcons.doc_on_doc,
          rotulo: 'Copiar camada',
          onTap: selecionada == null
              ? null
              : () => fecharE(() {
                  c.copiarCamada(selecionada);
                  AureaSnack.show(context, 'Camada copiada');
                }),
        ),
        ItemDoMenu(
          chave: 'colar-colar-camada',
          icone: CupertinoIcons.doc_on_clipboard,
          rotulo: 'Colar camada no cabeçote',
          onTap: c.temCamadaCopiada
              ? () => fecharE(() => c.colarCamada(t, acimaDe: selecionada))
              : null,
        ),
        ItemDoMenu(
          chave: 'colar-duplicar',
          icone: CupertinoIcons.plus_square_on_square,
          rotulo: 'Duplicar camada',
          onTap: selecionada == null
              ? null
              : () => fecharE(() => c.duplicateLayer(selecionada)),
        ),
        ItemDoMenu(
          chave: 'colar-selecionar-todas',
          icone: CupertinoIcons.checkmark_square,
          rotulo: 'Selecionar todas as camadas',
          onTap: todas.length < 2
              ? null
              : () => fecharE(() {
                  ref.read(selectedLayerProvider.notifier).state = null;
                  ref.read(multiSelectProvider.notifier).state = todas.toSet();
                }),
        ),
        ItemDoMenu(
          chave: 'colar-limpar-selecao',
          icone: CupertinoIcons.square,
          rotulo: 'Limpar seleção',
          onTap: () => fecharE(() {
            ref.read(multiSelectProvider.notifier).state = const {};
            ref.read(selectedLayerProvider.notifier).state = null;
          }),
        ),
        const SecaoDoMenu('Estilo e efeitos'),
        ItemDoMenu(
          chave: 'colar-copiar-estilo',
          icone: CupertinoIcons.paintbrush,
          rotulo: 'Copiar estilo',
          onTap: selecionada == null
              ? null
              : () => fecharE(() {
                  c.copiarEstilo(selecionada);
                  AureaSnack.show(context, 'Estilo copiado');
                }),
        ),
        ItemDoMenu(
          chave: 'colar-colar-estilo',
          icone: CupertinoIcons.paintbrush_fill,
          rotulo: 'Colar estilo…',
          onTap: selecionada == null || c.categoriasColaveis(selecionada).isEmpty
              ? null
              : () => fecharE(
                  () => mostrarColarEstilo(context, ref, selecionada),
                ),
        ),
        ItemDoMenu(
          chave: 'colar-copiar-efeitos',
          icone: CupertinoIcons.sparkles,
          rotulo: 'Copiar efeitos',
          onTap: selecionada == null
              ? null
              : () => fecharE(() {
                  final n = c.copyEffects(selecionada);
                  AureaSnack.show(
                    context,
                    n == 0
                        ? 'Esta camada não tem efeitos para copiar'
                        : 'Efeitos copiados: $n',
                  );
                }),
        ),
        ItemDoMenu(
          chave: 'colar-colar-efeitos',
          icone: CupertinoIcons.wand_stars,
          rotulo: 'Colar efeitos',
          onTap: selecionada == null || !c.temEfeitosCopiados
              ? null
              : () => fecharE(() {
                  final n = c.pasteEffects(selecionada);
                  AureaSnack.show(context, 'Efeitos colados: $n');
                }),
        ),
      ];
    },
  );
}

/// A FICHA DA MIDIA, em dialogo: nome, dimensoes, quadros, duracao,
/// formato, tamanho e amostragem.
Future<void> mostrarInfoDaMidia(
  BuildContext context,
  String caminho,
  String nome,
) async {
  final info = await lerInfoDaMidia(caminho);
  if (!context.mounted) return;
  await showCupertinoDialog<void>(
    context: context,
    barrierDismissible: true,
    builder: (dialogo) => CupertinoAlertDialog(
      title: const AppText('Informações da mídia'),
      content: Padding(
        padding: const EdgeInsets.only(top: 10),
        child: Column(
          key: const ValueKey('info-da-midia'),
          children: [
            for (final (rotulo, valor) in info.linhas)
              Padding(
                padding: const EdgeInsets.symmetric(vertical: 3),
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    SizedBox(
                      width: 104,
                      child: AppText(
                        rotulo,
                        textAlign: TextAlign.left,
                        style: const TextStyle(fontSize: 12.5),
                      ),
                    ),
                    Expanded(
                      child: Text(
                        valor,
                        textAlign: TextAlign.right,
                        style: const TextStyle(
                          fontSize: 12.5,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ),
                  ],
                ),
              ),
          ],
        ),
      ),
      actions: [
        CupertinoDialogAction(
          isDefaultAction: true,
          onPressed: () => Navigator.of(dialogo).pop(),
          child: const AppText('OK'),
        ),
      ],
    ),
  );
}

/// EXTRAIR O AUDIO com a conversa inteira: espera, cria a camada, avisa.
Future<void> extrairAudioComAviso(
  BuildContext context,
  WidgetRef ref,
  VideoLayer video,
) async {
  AureaSnack.show(context, 'Extraindo o áudio…');
  final caminho = await extrairAudioDoArquivo(video.sourcePath);
  if (!context.mounted) return;
  if (caminho == null) {
    AureaSnack.show(context, 'Este vídeo não tem áudio para extrair');
    return;
  }
  final id = ref
      .read(editorControllerProvider.notifier)
      .extrairAudioDaCamada(video.id, caminho);
  if (id == null) return;
  final c = ref.read(editorControllerProvider.notifier);
  AureaSnack.show(
    context,
    'Áudio extraído para uma camada própria',
    actionLabel: 'Desfazer',
    onAction: c.undo,
  );
}
