import 'package:aurea/src/core/l10n/app_language.dart';
import '../../../domain/layer.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/theme/tokens.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/effect_favorites.dart';
import '../../../application/ui/pro_mode.dart';
import '../../../domain/effect.dart';
import '../../am/am_colors.dart';
import 'previa_do_efeito.dart';

/// A GALERIA DE EFEITOS (Fase 4): previa animada de verdade por efeito,
/// busca com sinonimos, categorias com contador e favoritos. Um toque no
/// tile aplica o mesmo efeito que a previa mostra. Sem aba Presets: os
/// testadores do beta 1.0.5 pediram para tirar, atrapalhava achar as coisas.
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
                  child: AppText(texto,
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
                      placeholder: translate(context, 'glow, rgb split, pixelate...'),
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
                                category == null && !favoritos && !edits,
                                () => setSheetState(() {
                                  category = null;
                                  favoritos = false;
                                  edits = false;
                                }),
                                key: const ValueKey('galeria-todos'),
                              ),
                              // EDITS: batida, glitch, tempo e coloring
                              // num lugar so.
                              chip(
                                '${translate(sheetContext, 'Edits')} ${efeitosDeEdit.length}',
                                edits,
                                () => setSheetState(() {
                                  edits = !edits;
                                  category = null;
                                  favoritos = false;
                                }),
                                key: const ValueKey('galeria-edits'),
                              ),
                              if (pro)
                                chip(
                                  '★ Favoritos ${favs.length}',
                                  favoritos,
                                  () => setSheetState(() {
                                    favoritos = !favoritos;
                                    category = null;
                                    edits = false;
                                  }),
                                  key: const ValueKey('galeria-favoritos'),
                                ),
                              for (final c in effectCategories)
                                chip(
                                  '${translate(sheetContext, categoriaDoEfeito(c))} ${effectsInCategory(c).length}',
                                  category == c,
                                  () => setSheetState(() {
                                    category = category == c ? null : c;
                                    favoritos = false;
                                    edits = false;
                                  }),
                                  key: ValueKey('galeria-cat-$c'),
                                ),
                            ],
                          ),
                        ),
                      ),
                    const SizedBox(height: 8),
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
                                            effectFavoritesProvider.notifier,
                                          )
                                          .toggle(spec.id),
                                      onTap: () {
                                        controller.addEffect(
                                          layerId,
                                          type,
                                          pronto: PreviasDosEfeitos.instance
                                              .prontoDaPrevia(type),
                                        );
                                        Navigator.of(sheetContext).pop();
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
  });

  final EffectSpec spec;
  final EffectType type;
  final bool pro;
  final bool favorito;
  final VoidCallback onFavorito;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final t = AureaTokens.of(context);
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
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
                        child: AppText(
                          'custo ${spec.cost}',
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
