import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ui/tocavel.dart';
import '../../../../help/presentation/quick_guide_screen.dart';
import '../../../application/ui/effect_favorites.dart';
import '../../../domain/effect.dart';
import '../../am/am_colors.dart';
import 'previa_do_efeito.dart';

/// O QUE ESTE EFEITO FAZ, antes de aplicar.
///
/// A grade da galeria mostra nome e previa; quem nunca usou o efeito
/// ainda fica na duvida — e aplicar para ver custa desfazer. Aqui a
/// previa vem grande, com a explicacao do guia, os prontos com o que
/// cada um muda, e as palavras que acham efeitos parecidos.
///
/// Devolve o que a pessoa escolheu: aplicar cru, aplicar um pronto, ou
/// procurar por uma palavra.
sealed class EscolhaDoDetalhe {
  const EscolhaDoDetalhe();
}

class AplicarEfeito extends EscolhaDoDetalhe {
  const AplicarEfeito([this.pronto]);

  /// O pronto escolhido; nulo = o efeito com os valores de fabrica.
  final EffectPronto? pronto;
}

class ProcurarPor extends EscolhaDoDetalhe {
  const ProcurarPor(this.palavra);

  final String palavra;
}

Future<EscolhaDoDetalhe?> showEffectDetail(
  BuildContext context,
  EffectType tipo,
) => showModalBottomSheet<EscolhaDoDetalhe>(
  context: context,
  backgroundColor: AmColors.panel,
  isScrollControlled: true,
  constraints: BoxConstraints(
    maxHeight: MediaQuery.of(context).size.height * .7,
  ),
  shape: const RoundedRectangleBorder(
    borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
  ),
  builder: (_) => _DetalheDoEfeito(tipo: tipo),
);

class _DetalheDoEfeito extends ConsumerWidget {
  const _DetalheDoEfeito({required this.tipo});

  final EffectType tipo;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final spec = effectSpecs[tipo]!;
    final favorito = ref.watch(effectFavoritesProvider).contains(spec.id);
    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Row(
              children: [
                ClipRRect(
                  borderRadius: BorderRadius.circular(12),
                  child: RelogioDasPrevias(
                    child: PreviaDoEfeito(tipo: tipo, lado: 104),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      AppText(
                        spec.name,
                        style: const TextStyle(
                          fontSize: 17,
                          fontWeight: FontWeight.w700,
                          color: AmColors.text,
                        ),
                      ),
                      const SizedBox(height: 2),
                      AppText(
                        categoriaDoEfeito(spec.category),
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.muted,
                        ),
                      ),
                      const SizedBox(height: 8),
                      Row(
                        children: [
                          Tocavel(
                            key: ValueKey('detalhe-favorito-${spec.id}'),
                            onTap: () => ref
                                .read(effectFavoritesProvider.notifier)
                                .toggle(spec.id),
                            child: Padding(
                              padding: const EdgeInsets.all(4),
                              child: Icon(
                                favorito
                                    ? CupertinoIcons.star_fill
                                    : CupertinoIcons.star,
                                size: 20,
                                color: favorito
                                    ? AmColors.action
                                    : AmColors.muted,
                              ),
                            ),
                          ),
                          const SizedBox(width: 10),
                          Tocavel(
                            key: const ValueKey('detalhe-aplicar'),
                            onTap: () =>
                                Navigator.of(context)
                                    .pop(const AplicarEfeito()),
                            child: Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 16,
                                vertical: 8,
                              ),
                              decoration: BoxDecoration(
                                color: AmColors.action,
                                borderRadius: BorderRadius.circular(9),
                              ),
                              child: const AppText(
                                'Aplicar',
                                style: TextStyle(
                                  fontSize: 13,
                                  fontWeight: FontWeight.w700,
                                  color: AmColors.onAction,
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
            ),
            const SizedBox(height: 14),
            Expanded(
              child: ListView(
                padding: EdgeInsets.zero,
                children: [
                  AppText(
                    effectHelp(tipo),
                    style: const TextStyle(
                      fontSize: 13,
                      height: 1.45,
                      color: AmColors.text,
                    ),
                  ),
                  if (spec.presets.isNotEmpty) ...[
                    const SizedBox(height: 16),
                    const _Titulo('Prontos'),
                    for (final pronto in spec.presets)
                      Tocavel(
                        key: ValueKey('detalhe-pronto-${pronto.nome}'),
                        onTap: () =>
                            Navigator.of(context).pop(AplicarEfeito(pronto)),
                        child: Container(
                          margin: const EdgeInsets.only(top: 6),
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 10,
                          ),
                          decoration: BoxDecoration(
                            color: AmColors.chip,
                            borderRadius: BorderRadius.circular(10),
                          ),
                          child: Row(
                            children: [
                              Expanded(
                                child: Column(
                                  crossAxisAlignment: CrossAxisAlignment.start,
                                  children: [
                                    AppText(
                                      pronto.nome,
                                      style: const TextStyle(
                                        fontSize: 13,
                                        fontWeight: FontWeight.w600,
                                        color: AmColors.text,
                                      ),
                                    ),
                                    AppText(
                                      _oQueMuda(spec, pronto),
                                      maxLines: 1,
                                      style: const TextStyle(
                                        fontSize: 11,
                                        color: AmColors.muted,
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                              const Icon(
                                CupertinoIcons.chevron_right,
                                size: 14,
                                color: AmColors.muted,
                              ),
                            ],
                          ),
                        ),
                      ),
                  ],
                  if (spec.synonyms.isNotEmpty) ...[
                    const SizedBox(height: 16),
                    const _Titulo('Parecidos'),
                    const SizedBox(height: 6),
                    Wrap(
                      spacing: 6,
                      runSpacing: 6,
                      children: [
                        // Sinonimo repetido (o catalogo tem alguns) nao
                        // pode virar dois chips com a mesma chave.
                        for (final palavra in spec.synonyms.toSet().take(8))
                          Tocavel(
                            key: ValueKey('detalhe-tag-$palavra'),
                            onTap: () =>
                                Navigator.of(context).pop(ProcurarPor(palavra)),
                            child: Container(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 10,
                                vertical: 6,
                              ),
                              decoration: BoxDecoration(
                                color: AmColors.chip,
                                borderRadius: BorderRadius.circular(8),
                              ),
                              child: AppText(
                                palavra,
                                style: const TextStyle(
                                  fontSize: 12,
                                  color: AmColors.text,
                                ),
                              ),
                            ),
                          ),
                      ],
                    ),
                  ],
                  const SizedBox(height: 12),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// O QUE O PRONTO MUDA, em palavras da ficha: "Bolhas 4 · Desfoque 16".
  static String _oQueMuda(EffectSpec spec, EffectPronto pronto) {
    final partes = <String>[
      for (final e in pronto.valores.entries)
        if (spec.params[e.key] case final p?) '${p.label} ${_numero(e.value)}',
    ];
    return partes.isEmpty ? 'Os valores de fábrica' : partes.join(' · ');
  }

  static String _numero(double v) {
    if (v == v.roundToDouble()) return v.round().toString();
    return v.toStringAsFixed(2).replaceFirst(RegExp(r'0+$'), '');
  }
}

class _Titulo extends StatelessWidget {
  const _Titulo(this.texto);

  final String texto;

  @override
  Widget build(BuildContext context) => AppText(
    texto,
    style: const TextStyle(
      fontSize: 12,
      fontWeight: FontWeight.w700,
      color: AmColors.muted,
    ),
  );
}
