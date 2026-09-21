import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/cut.dart' show FreezePlacement;
import '../paineis/comum_de_objetos.dart' show FileiraDePilulas;
import '../paineis/pecas_centrais.dart' show respiroDoPainel;

/// CONGELAR QUADRO no instante [globalTime] da camada [layerId].
///
/// Duracao e lugar do congelado sao estado DA FOLHA: o projeto so muda no
/// "Congelar aqui", numa operacao so do controlador — um passo de
/// desfazer, por mais que ela divida o clipe e empurre o resto.
Future<void> showFreezeSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  Duration globalTime,
) => mostrarAureaFolha<void>(
  context,
  titulo: 'Congelar quadro',
  construtor: (folha) =>
      _Congelar(ref: ref, layerId: layerId, globalTime: globalTime),
);

class _Congelar extends StatefulWidget {
  const _Congelar({
    required this.ref,
    required this.layerId,
    required this.globalTime,
  });

  final WidgetRef ref;
  final String layerId;
  final Duration globalTime;

  @override
  State<_Congelar> createState() => _CongelarState();
}

class _CongelarState extends State<_Congelar> {
  var _segundos = 1.0;
  var _onde = FreezePlacement.separateClip;

  void _congelar() {
    final ok = widget.ref
        .read(editorControllerProvider.notifier)
        .freezeFrame(
          widget.layerId,
          widget.globalTime,
          duration: Duration(milliseconds: (_segundos * 1000).round()),
          placement: _onde,
        );
    if (!ok) {
      // A folha FICA aberta: a pessoa so precisa mover o cabecote.
      AureaSnack.show(
        context,
        'Leve o cabecote para dentro de um clipe de video',
      );
      return;
    }
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) => SingleChildScrollView(
    padding: respiroDoPainel,
    child: Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        AureaPropertyRow(
          rotulo: 'Duração',
          chave: 'congelar-duracao',
          valor: _segundos,
          min: .1,
          max: 10,
          casas: 1,
          unidade: 's',
          aoMudar: (v) => setState(() => _segundos = v),
        ),
        AureaPropertyRow.personalizada(
          rotulo: 'Onde',
          chave: 'congelar-onde',
          filho: FileiraDePilulas<FreezePlacement>(
            chave: 'congelar-onde',
            chaveDe: (p) => p.name,
            opcoes: const [
              FreezePlacement.separateClip,
              FreezePlacement.insideClip,
            ],
            atual: _onde,
            rotuloDe: (p) => switch (p) {
              FreezePlacement.separateClip => 'Clipe separado',
              FreezePlacement.insideClip => 'Dentro do clipe',
            },
            aoEscolher: (p) => setState(() => _onde = p),
          ),
        ),
        const SizedBox(height: AureaDims.e8),
        CupertinoButton(
          key: const ValueKey('congelar-aqui'),
          color: AureaCores.acao,
          padding: const EdgeInsets.symmetric(vertical: 12),
          onPressed: _congelar,
          child: AppText(
            'Congelar aqui',
            style: TextStyle(
              fontSize: 14,
              fontWeight: FontWeight.w700,
              color: AureaCores.sobreAcao,
            ),
          ),
        ),
      ],
    ),
  );
}
