import 'package:aurea/src/core/l10n/app_language.dart';
import '../../../domain/layer.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/theme/tokens.dart';
import '../../../application/editor_controller.dart';
import '../../../application/effect_preset_store.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/effect_favorites.dart';
import '../../../application/ui/pro_mode.dart';
import '../../../domain/effect.dart';
import '../../../domain/effect_preset.dart';
import '../../am/am_colors.dart';
import 'effect_thumbnail.dart';

/// A GALERIA DE EFEITOS (Fase 4): miniatura por efeito, busca com
/// sinonimos, categorias com contador, favoritos (Pro) e a aba Presets
/// (de fabrica e os salvos). Um toque no tile aplica.
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
  var showPresets = false;
  await EffectPresetStore.instance.load();
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
                      ? effectSpecs.keys.toList()
                      : effectsInCategory(category!));
            if (ref.read(editorControllerProvider).layerById(layerId)
                is! VideoLayer) {
              results = results
                  .where((t) => t != EffectType.opticalFlow)
                  .toList();
            }
            if (favoritos && query.isEmpty) {
              results = [
                for (final t in results)
                  if (favs.contains(effectSpecs[t]!.id)) t,
              ];
            }
            final presets = [
              ...factoryPresets(),
              ...EffectPresetStore.instance.presets,
            ];

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
                        chip(
                          'Presets',
                          showPresets,
                          () => setSheetState(() => showPresets = !showPresets),
                          key: const ValueKey('galeria-presets'),
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
                      onChanged: (v) => setSheetState(() {
                        query = v;
                        showPresets = false;
                      }),
                    ),
                    const SizedBox(height: 8),
                    if (!showPresets && query.isEmpty)
                      SizedBox(
                        height: 34,
                        child: SingleChildScrollView(
                          scrollDirection: Axis.horizontal,
                          child: Row(
                            children: [
                              chip(
                                'Todos ${effectSpecs.length}',
                                category == null && !favoritos,
                                () => setSheetState(() {
                                  category = null;
                                  favoritos = false;
                                }),
                                key: const ValueKey('galeria-todos'),
                              ),
                              if (pro)
                                chip(
                                  '★ Favoritos ${favs.length}',
                                  favoritos,
                                  () => setSheetState(() {
                                    favoritos = !favoritos;
                                    category = null;
                                  }),
                                  key: const ValueKey('galeria-favoritos'),
                                ),
                              for (final c in effectCategories)
                                chip(
                                  '${categoriaDoEfeito(c)} ${effectsInCategory(c).length}',
                                  category == c,
                                  () => setSheetState(() {
                                    category = category == c ? null : c;
                                    favoritos = false;
                                  }),
                                  key: ValueKey('galeria-cat-$c'),
                                ),
                            ],
                          ),
                        ),
                      ),
                    const SizedBox(height: 8),
                    Expanded(
                      child: showPresets
                          ? ListView.builder(
                              itemCount: presets.length,
                              itemBuilder: (context, i) {
                                final p = presets[i];
                                return ListTile(
                                  key: ValueKey('preset-${p.id}'),
                                  leading: const Icon(
                                    CupertinoIcons.square_stack_3d_down_right,
                                    color: AmColors.accent,
                                    size: 20,
                                  ),
                                  title: AppText(
                                    p.name,
                                    style: const TextStyle(
                                      color: AmColors.text,
                                      fontSize: 14,
                                    ),
                                  ),
                                  subtitle: AppText(
                                    '${p.category} · ${p.effects.length} efeito(s)'
                                    '${p.builtIn ? '' : ' · salvo por você'}',
                                    style: const TextStyle(
                                      fontSize: 11,
                                      color: AmColors.muted,
                                    ),
                                  ),
                                  onTap: () {
                                    controller.applyPreset(
                                      layerId,
                                      p,
                                      at: playback.time.value,
                                    );
                                    Navigator.of(sheetContext).pop();
                                  },
                                );
                              },
                            )
                          : results.isEmpty
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
                                return GridView.builder(
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
                                        controller.addEffect(layerId, type);
                                        Navigator.of(sheetContext).pop();
                                      },
                                    );
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
                  EffectThumbnail(type: type, size: lado),
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
