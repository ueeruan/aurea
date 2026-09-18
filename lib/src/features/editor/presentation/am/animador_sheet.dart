import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/l10n/app_language.dart';
import '../../../../core/ui/tocavel.dart';
import '../../application/editor_controller.dart';
import '../../domain/animadores.dart';
import '../context/parameter_row.dart';
import 'am_colors.dart';

/// ANIMAR SOZINHO: a folha que poe um animador automatico na
/// propriedade. Nada de keyframe — a propriedade balança por conta
/// propria, e o que se escolhe aqui e a FORMA desse balanço.
Future<void> showAnimadorSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  LayerProp prop, {
  required String nome,
  String unidade = '',
}) => showModalBottomSheet<void>(
  context: context,
  backgroundColor: AmColors.panel,
  barrierColor: Colors.black26,
  isScrollControlled: true,
  shape: const RoundedRectangleBorder(
    borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
  ),
  builder: (_) =>
      _Animador(layerId: layerId, prop: prop, nome: nome, unidade: unidade),
);

class _Animador extends ConsumerWidget {
  const _Animador({
    required this.layerId,
    required this.prop,
    required this.nome,
    required this.unidade,
  });

  final String layerId;
  final LayerProp prop;
  final String nome;
  final String unidade;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final project = ref.watch(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    final layer = project.layerById(layerId);
    if (layer == null) return const SizedBox.shrink();
    final ponto = EditorController.propEhPonto(prop);
    final atual = controller.propAnimador(layer, prop);
    final a = atual ?? const AnimadorAutomatico();
    void aplicar(AnimadorAutomatico novo) =>
        controller.setPropAnimador(layerId, prop, novo);
    final multiplica = a.modo == ModoDoAnimador.multiplicar;

    return SafeArea(
      child: SingleChildScrollView(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(16, 14, 16, 16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: AppTextMoldado(
                      'Animar {0} sozinho', [nome],
                      style: const TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                        color: AmColors.text,
                      ),
                    ),
                  ),
                  if (atual != null)
                    Tocavel(
                      key: const ValueKey('animador-tirar'),
                      onTap: () {
                        controller.setPropAnimador(layerId, prop, null);
                        Navigator.of(context).pop();
                      },
                      child: const Padding(
                        padding: EdgeInsets.symmetric(
                          horizontal: 10,
                          vertical: 6,
                        ),
                        child: AppText(
                          'Tirar',
                          style: TextStyle(
                            fontSize: 13,
                            fontWeight: FontWeight.w600,
                            color: AmColors.accent,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 10),
              // A FORMA DO BALANÇO.
              Wrap(
                spacing: 6,
                runSpacing: 6,
                children: [
                  for (final tipo in TipoDoAnimador.values)
                    Tocavel(
                      key: ValueKey('animador-tipo-${tipo.name}'),
                      onTap: () => aplicar(a.copyWith(tipo: tipo)),
                      child: Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: 8,
                        ),
                        decoration: BoxDecoration(
                          color: atual != null && a.tipo == tipo
                              ? AmColors.accent.withValues(alpha: .22)
                              : Colors.white10,
                          borderRadius: BorderRadius.circular(9),
                        ),
                        child: AppText(
                          rotuloDoAnimador(tipo),
                          style: TextStyle(
                            fontSize: 13,
                            fontWeight: FontWeight.w600,
                            color: atual != null && a.tipo == tipo
                                ? AmColors.accent
                                : AmColors.text,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
              const SizedBox(height: 6),
              AppText(
                explicacaoDoAnimador(a.tipo),
                style: const TextStyle(fontSize: 12, color: AmColors.muted),
              ),
              const SizedBox(height: 4),
              ParameterRow(
                label: ponto ? 'Força X' : 'Força',
                value: multiplica ? a.forca * 100 : a.forca,
                min: multiplica ? 0 : -2000,
                max: multiplica ? 400 : 2000,
                unitsPerPixel: multiplica ? .5 : 1,
                decimals: multiplica ? 0 : 1,
                unit: multiplica ? '%' : unidade,
                valueKey: const ValueKey('animador-forca'),
                onChanged: (v) =>
                    aplicar(a.copyWith(forca: multiplica ? v / 100 : v)),
              ),
              if (ponto)
                ParameterRow(
                  label: 'Força Y',
                  value: multiplica ? a.forcaDoY * 100 : a.forcaDoY,
                  min: multiplica ? 0 : -2000,
                  max: multiplica ? 400 : 2000,
                  unitsPerPixel: multiplica ? .5 : 1,
                  decimals: multiplica ? 0 : 1,
                  unit: multiplica ? '%' : unidade,
                  valueKey: const ValueKey('animador-forca-y'),
                  onChanged: (v) =>
                      aplicar(a.copyWith(forcaY: multiplica ? v / 100 : v)),
                ),
              ParameterRow(
                label: 'Volta',
                value: a.periodo,
                min: .05,
                max: 30,
                unitsPerPixel: .02,
                decimals: 2,
                unit: 's',
                valueKey: const ValueKey('animador-periodo'),
                onChanged: (v) => aplicar(a.copyWith(periodo: v)),
              ),
              ParameterRow(
                label: 'Começo',
                value: a.fase * 100,
                min: 0,
                max: 100,
                unitsPerPixel: .5,
                decimals: 0,
                unit: '%',
                valueKey: const ValueKey('animador-fase'),
                onChanged: (v) => aplicar(a.copyWith(fase: v / 100)),
              ),
              if (a.tipo == TipoDoAnimador.aleatorio)
                ParameterRow(
                  label: 'Sorteio',
                  value: a.semente.toDouble(),
                  min: 1,
                  max: 999,
                  unitsPerPixel: .2,
                  decimals: 0,
                  valueKey: const ValueKey('animador-semente'),
                  onChanged: (v) => aplicar(a.copyWith(semente: v.round())),
                ),
              const SizedBox(height: 6),
              // SOMAR ou MULTIPLICAR: escala e opacidade ficam melhores em
              // porcentagem do valor; posicao, em pixels.
              Row(
                children: [
                  for (final modo in ModoDoAnimador.values)
                    Expanded(
                      child: Tocavel(
                        key: ValueKey('animador-modo-${modo.name}'),
                        onTap: () => aplicar(
                          a.copyWith(
                            modo: modo,
                            forca: modo == ModoDoAnimador.multiplicar
                                ? .2
                                : (ponto ? 40 : 20),
                            limparForcaY: true,
                          ),
                        ),
                        child: Container(
                          margin: const EdgeInsets.only(right: 6),
                          padding: const EdgeInsets.symmetric(vertical: 9),
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: a.modo == modo
                                ? AmColors.accent.withValues(alpha: .22)
                                : Colors.white10,
                            borderRadius: BorderRadius.circular(9),
                          ),
                          child: AppText(
                            modo == ModoDoAnimador.somar
                                ? 'Somar ao valor'
                                : 'Por cento do valor',
                            style: TextStyle(
                              fontSize: 12.5,
                              fontWeight: FontWeight.w600,
                              color: a.modo == modo
                                  ? AmColors.accent
                                  : AmColors.text,
                            ),
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}
