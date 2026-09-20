import 'package:aurea/src/core/l10n/app_language.dart';

import '../../../domain/layer.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/theme/tokens.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/ui/effect_favorites.dart';
import '../../../application/ui/effect_recents.dart';
import '../../../application/ui/pro_mode.dart';
import '../../../domain/effect.dart';
import '../../../domain/presets_de_edicao.dart';
import '../../am/am_colors.dart';
import 'effect_detail_sheet.dart';
import 'previa_do_efeito.dart';

/// A GALERIA DE EFEITOS (Fase 4): previa animada de verdade por efeito,
/// busca com sinonimos, categorias com contador e favoritos. Um toque no
/// tile aplica o mesmo efeito que a previa mostra.
///
/// A ABA PRESETS (15/09, pedido do dono): os 15 presets do bundle
/// 4nas.ftbl convertidos para o motor — receitas de pilha inteira, um
/// toque aplica tudo. E outra coisa que a aba "Presets" que os
/// testadores do 1.0.5 mandaram tirar (aquela repetia os prontos de
/// cada efeito; esta traz looks completos que nao existem em nenhum
/// efeito sozinho).
Future<void> showEffectGallery(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  PlaybackController playback,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final search = TextEditingController();
  var query = '';
  String? category;
  var favoritos = false;
  var edits = false;
  var presets = false;
  var sugeridos = false;
  var recentes = false;
  await PreviasDosEfeitos.instance.manifesto();
  if (!context.mounted) return;

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
    builder: (sheetContext) => Consumer(
      builder: (sheetContext, ref, _) {
        final pro = ref.watch(proModeProvider);
        final favs = ref.watch(effectFavoritesProvider);
        ref.watch(effectRecentsProvider);
        final recentesDoAparelho = ref
            .read(effectRecentsProvider.notifier)
            .tipos;
        final camadaDaGaleria = ref
            .read(editorControllerProvider)
            .layerById(layerId);
        final recomendados = efeitosRecomendados(
          ehMidia:
              camadaDaGaleria is VideoLayer || camadaDaGaleria is ImageLayer,
          recentes: recentesDoAparelho,
        );
        return StatefulBuilder(
          builder: (sheetContext, setSheetState) {
            var results = query.isNotEmpty
                ? searchEffects(query)
                : (category == null
                      ? efeitosDoCatalogo
                      : effectsInCategory(category!));
            if (ref.read(editorControllerProvider).layerById(layerId)
                is! VideoLayer) {
              results = results
                  .where((t) => t != EffectType.opticalFlow)
                  .toList();
            }
            if (edits && query.isEmpty && !favoritos) {
              results = [
                for (final t in efeitosDeEdit)
                  if (results.contains(t)) t,
              ];
            }
            if (sugeridos && query.isEmpty) {
              results = [
                for (final t in recomendados)
                  if (results.contains(t)) t,
              ];
            }
            if (recentes && query.isEmpty) {
              results = [
                for (final t in recentesDoAparelho)
                  if (results.contains(t)) t,
              ];
            }
            if (favoritos && query.isEmpty) {
              results = [
                for (final t in results)
                  if (favs.contains(effectSpecs[t]!.id)) t,
              ];
            }

            Widget chip(
              String texto,
              bool aceso,
              VoidCallback onTap, {
              Key? key,
            }) => Padding(
              padding: const EdgeInsets.only(right: 8),
              child: GestureDetector(
                key: key,
                behavior: HitTestBehavior.opaque,
                onTap: onTap,
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 12,
                    vertical: 8,
                  ),
                  decoration: BoxDecoration(
                    color: aceso ? AmColors.actionDim : AmColors.chip,
                    borderRadius: BorderRadius.circular(9),
                    border: aceso ? Border.all(color: AmColors.action) : null,
                  ),
                  child: AppText(
                    texto,
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                      color: aceso ? AmColors.action : AmColors.text,
                    ),
                  ),
                ),
              ),
            );

            return SafeArea(
              child: Padding(
                padding: EdgeInsets.fromLTRB(
                  14,
                  12,
                  14,
                  8 + MediaQuery.of(sheetContext).viewInsets.bottom,
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        const Expanded(
                          child: AppText(
                            'Efeitos',
                            style: TextStyle(
                              fontSize: 18,
                              fontWeight: FontWeight.w700,
                              color: AmColors.text,
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 8),
                    CupertinoSearchTextField(
                      key: const ValueKey('galeria-busca'),
                      controller: search,
                      placeholder: translate(
                        context,
                        'glow, rgb split, pixelate...',
                      ),
                      style: const TextStyle(
                        fontSize: 14,
                        color: AmColors.text,
                      ),
                      onChanged: (v) => setSheetState(() => query = v),
                    ),
                    const SizedBox(height: 8),
                    if (query.isEmpty)
                      SizedBox(
                        height: 34,
                        child: SingleChildScrollView(
                          scrollDirection: Axis.horizontal,
                          child: Row(
                            children: [
                              chip(
                                '${translate(sheetContext, 'Todos')} ${efeitosDoCatalogo.length}',
                                category == null &&
                                    !favoritos &&
                                    !edits &&
                                    !presets,
                                () => setSheetState(() {
                                  category = null;
                                  favoritos = false;
                                  edits = false;
                                  presets = false;
                                  sugeridos = false;
                                  recentes = false;
                                }),
                                key: const ValueKey('galeria-todos'),
                              ),
                              // PRESETS e EDITS sairam da galeria (beta 89,
                              // pedido do dono): so ficam abas com efeito.
                              // SUGERIDOS e RECENTES: o que resolve
                              // antes de procurar. Sem eles, quem acabou
                              // de usar um efeito procurava tudo de novo.
                              chip(
                                '${translate(sheetContext, 'Sugeridos')} ${recomendados.length}',
                                sugeridos,
                                () => setSheetState(() {
                                  sugeridos = !sugeridos;
                                  category = null;
                                  favoritos = false;
                                  edits = false;
                                  presets = false;
                                  recentes = false;
                                }),
                                key: const ValueKey('galeria-sugeridos'),
                              ),
                              if (recentesDoAparelho.isNotEmpty)
                                chip(
                                  '${translate(sheetContext, 'Recentes')} ${recentesDoAparelho.length}',
                                  recentes,
                                  () => setSheetState(() {
                                    recentes = !recentes;
                                    category = null;
                                    favoritos = false;
                                    edits = false;
                                    presets = false;
                                    sugeridos = false;
                                  }),
                                  key: const ValueKey('galeria-recentes'),
                                ),
                              if (pro)
                                chip(
                                  '★ Favoritos ${favs.length}',
                                  favoritos,
                                  () => setSheetState(() {
                                    favoritos = !favoritos;
                                    category = null;
                                    edits = false;
                                    presets = false;
                                    sugeridos = false;
                                    recentes = false;
                                  }),
                                  key: const ValueKey('galeria-favoritos'),
                                ),
                              for (final c in effectCategories)
                                if (effectsInCategory(c).isNotEmpty)
                                  chip(
                                    '${translate(sheetContext, categoriaDoEfeito(c))} ${effectsInCategory(c).length}',
                                    category == c,
                                    () => setSheetState(() {
                                      category = category == c ? null : c;
                                      favoritos = false;
                                      edits = false;
                                      presets = false;
                                      sugeridos = false;
                                      recentes = false;
                                    }),
                                    key: ValueKey('galeria-cat-$c'),
                                  ),
                            ],
                          ),
                        ),
                      ),
                    const SizedBox(height: 8),
                    if (presets && query.isEmpty)
                      Expanded(
                        child: ListView.builder(
                          key: const ValueKey('galeria-lista-presets'),
                          padding: const EdgeInsets.only(bottom: 8),
                          itemCount: presetsDeEdicao.length,
                          itemBuilder: (context, i) {
                            final p = presetsDeEdicao[i];
                            final soVideo =
                                p.acao == AcaoDoPreset.cameraLenta &&
                                ref
                                        .read(editorControllerProvider)
                                        .layerById(layerId)
                                    is! VideoLayer;
                            return _PresetTile(
                              key: ValueKey('preset-${p.id}'),
                              preset: p,
                              desabilitado: soVideo,
                              onTap: () {
                                if (soVideo) {
                                  AureaSnack.show(
                                    context,
                                    'Esse preset e camera lenta: '
                                    'so em video.',
                                  );
                                  return;
                                }
                                final n = controller.aplicarPresetDeEdicao(
                                  layerId,
                                  p,
                                );
                                Navigator.of(sheetContext).pop();
                                AureaSnack.show(
                                  context,
                                  n == 0
                                      ? 'Nao deu para aplicar aqui.'
                                      : '${p.nome} aplicado — '
                                            '$n efeito${n == 1 ? '' : 's'}',
                                  actionLabel: n == 0 ? null : 'Desfazer',
                                  onAction: controller.undo,
                                );
                              },
                            );
                          },
                        ),
                      )
                    else
                      Expanded(
                        child: results.isEmpty
                            ? Center(
                                child: Padding(
                                  padding: const EdgeInsets.all(24),
                                  child: AppText(
                                    favoritos && query.isEmpty
                                        ? 'Nenhum favorito ainda. Toque na estrela de um efeito.'
                                        : 'Nada encontrado. Tente "glow", "rgb", "pixel" ou "shake".',
                                    textAlign: TextAlign.center,
                                    style: const TextStyle(
                                      fontSize: 13,
                                      height: 1.4,
                                      color: AmColors.muted,
                                    ),
                                  ),
                                ),
                              )
                            : LayoutBuilder(
                                builder: (context, c) {
                                  final colunas = (c.maxWidth / 118)
                                      .floor()
                                      .clamp(2, 5);
                                  return RelogioDasPrevias(
                                    child: GridView.builder(
                                      key: const ValueKey('galeria-grade'),
                                      padding: const EdgeInsets.only(bottom: 8),
                                      gridDelegate:
                                          SliverGridDelegateWithFixedCrossAxisCount(
                                            crossAxisCount: colunas,
                                            mainAxisSpacing: 10,
                                            crossAxisSpacing: 10,
                                            childAspectRatio: 0.7,
                                          ),
                                      itemCount: results.length,
                                      itemBuilder: (context, i) {
                                        final type = results[i];
                                        final spec = effectSpecs[type]!;
                                        return _EffectTile(
                                          key: ValueKey('efeito-${spec.id}'),
                                          spec: spec,
                                          type: type,
                                          pro: pro,
                                          favorito: favs.contains(spec.id),
                                          onFavorito: () => ref
                                              .read(
                                                effectFavoritesProvider
                                                    .notifier,
                                              )
                                              .toggle(spec.id),
                                          onTap: () {
                                            controller.addEffect(
                                              layerId,
                                              type,
                                              pronto: PreviasDosEfeitos.instance
                                                  .prontoDaPrevia(type),
                                            );
                                            ref
                                                .read(
                                                  effectRecentsProvider
                                                      .notifier,
                                                )
                                                .registrar(type);
                                            Navigator.of(sheetContext).pop();
                                          },
                                          // O TOQUE LONGO EXPLICA antes de
                                          // aplicar: previa grande, o que o
                                          // efeito faz, os prontos e as
                                          // palavras que acham parecidos.
                                          onDetalhe: () async {
                                            final escolha =
                                                await showEffectDetail(
                                                  sheetContext,
                                                  type,
                                                );
                                            if (escolha == null) return;
                                            switch (escolha) {
                                              case AplicarEfeito(:final pronto):
                                                controller.addEffect(
                                                  layerId,
                                                  type,
                                                  pronto:
                                                      pronto ??
                                                      PreviasDosEfeitos.instance
                                                          .prontoDaPrevia(type),
                                                );
                                                ref
                                                    .read(
                                                      effectRecentsProvider
                                                          .notifier,
                                                    )
                                                    .registrar(type);
                                                if (sheetContext.mounted) {
                                                  Navigator.of(sheetContext)
                                                      .pop();
                                                }
                                              case ProcurarPor(:final palavra):
                                                setSheetState(() {
                                                  search.text = palavra;
                                                  query = palavra;
                                                });
                                            }
                                          },
                                        );
                                      },
                                    ),
                                  );
                                },
                              ),
                      ),
                  ],
                ),
              ),
            );
          },
        );
      },
    ),
  );
  // A busca pode fechar a galeria enquanto o teclado ainda esta concluindo
  // a troca de foco. Descartar o controller no mesmo microtask deixava o
  // EditableText chamar clearComposing() num controller ja descartado.
  FocusManager.instance.primaryFocus?.unfocus();
  await WidgetsBinding.instance.endOfFrame;
  search.dispose();
}

class _EffectTile extends StatelessWidget {
  const _EffectTile({
    super.key,
    required this.spec,
    required this.type,
    required this.pro,
    required this.favorito,
    required this.onFavorito,
    required this.onTap,
    required this.onDetalhe,
  });

  final EffectSpec spec;
  final EffectType type;
  final bool pro;
  final bool favorito;
  final VoidCallback onFavorito;
  final VoidCallback onTap;
  final VoidCallback onDetalhe;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      onLongPress: onDetalhe,
      child: LayoutBuilder(
        builder: (context, c) {
          final lado = c.maxWidth;
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Stack(
                children: [
                  PreviaDoEfeito(tipo: type, lado: lado),
                  if (spec.cost > 1)
                    Positioned(
                      left: 6,
                      bottom: 6,
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 5,
                          vertical: 2,
                        ),
                        decoration: BoxDecoration(
                          color: Colors.black.withValues(alpha: .55),
                          borderRadius: BorderRadius.circular(6),
                        ),
                        child: AppTextMoldado(
                          'custo {0}',
                          [spec.cost],
                          style: const TextStyle(
                            fontSize: 9,
                            color: Colors.white,
                          ),
                        ),
                      ),
                    ),
                  if (pro)
                    Positioned(
                      right: 2,
                      top: 2,
                      child: Tooltip(
                        message: favorito ? 'Tirar dos favoritos' : 'Favoritar',
                        child: GestureDetector(
                          key: ValueKey('favorito-${spec.id}'),
                          behavior: HitTestBehavior.opaque,
                          onTap: onFavorito,
                          child: SizedBox(
                            width: 32,
                            height: 32,
                            child: Icon(
                              favorito
                                  ? CupertinoIcons.star_fill
                                  : CupertinoIcons.star,
                              size: 16,
                              color: favorito
                                  ? AmColors.action
                                  : Colors.white70,
                            ),
                          ),
                        ),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 5),
              AppText(
                spec.name,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w600,
                  color: t.text,
                ),
              ),
              AppText(
                categoriaDoEfeito(spec.category),
                maxLines: 1,
                style: TextStyle(fontSize: 10.5, color: t.muted),
              ),
            ],
          );
        },
      ),
    );
  }
}

/// O CARTAO DE UM PRESET DO BUNDLE: nome, o que faz, e a marca. Um
/// toque aplica a pilha inteira.
class _PresetTile extends StatelessWidget {
  const _PresetTile({
    super.key,
    required this.preset,
    required this.onTap,
    this.desabilitado = false,
  });

  final PresetDeEdicao preset;
  final VoidCallback onTap;
  final bool desabilitado;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Tocavel(
        onTap: onTap,
        child: Opacity(
          opacity: desabilitado ? 0.45 : 1,
          child: Container(
            padding: const EdgeInsets.fromLTRB(12, 10, 12, 10),
            decoration: BoxDecoration(
              color: t.chip,
              borderRadius: BorderRadius.circular(12),
            ),
            child: Row(
              children: [
                Container(
                  width: 40,
                  height: 40,
                  decoration: BoxDecoration(
                    color: AmColors.accentDim,
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Icon(
                    preset.acao == AcaoDoPreset.cameraLenta
                        ? CupertinoIcons.slowmo
                        : CupertinoIcons.wand_rays,
                    size: 20,
                    color: AmColors.accent,
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Flexible(
                            child: Text(
                              preset.nome,
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              style: TextStyle(
                                fontSize: 13.5,
                                fontWeight: FontWeight.w700,
                                color: t.text,
                              ),
                            ),
                          ),
                          const SizedBox(width: 6),
                          Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 5,
                              vertical: 1,
                            ),
                            decoration: BoxDecoration(
                              color: AmColors.accentDim,
                              borderRadius: BorderRadius.circular(5),
                            ),
                            child: Text(
                              preset.marca,
                              style: const TextStyle(
                                fontSize: 9,
                                fontWeight: FontWeight.w700,
                                color: AmColors.accent,
                              ),
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 2),
                      AppText(
                        preset.detalhe,
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          fontSize: 11,
                          height: 1.3,
                          color: t.muted,
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 6),
                Icon(
                  CupertinoIcons.plus_circle_fill,
                  size: 20,
                  color: AmColors.accent,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
