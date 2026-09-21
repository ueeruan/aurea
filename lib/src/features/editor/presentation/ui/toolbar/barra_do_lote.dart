import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import 'alinhar.dart' show abrirCascata, showAlignSheet;
import 'acoes_da_camada.dart' show excluirCamadas;
import '../shell/contrato.dart';
import 'barra_contextual.dart';
import 'escolher_pai.dart';

// ===========================================================================
// SELECAO MULTIPLA
// ===========================================================================
//
// DUAS PORTAS, e nenhuma e gesto escondido: "Selecionar várias" no menu da
// camada e "Selecionar" na barra do projeto ligam o MODO SELECIONAR. Com
// ele ligado, tocar numa camada (timeline ou palco) marca e desmarca — a
// regra mora em [alternarCamadaNoLote], e o palco ja a segue
// (`preview_stage`). A barra da base vira a do lote ate "Soltar".

/// AS CAMADAS DO LOTE: as marcadas mais a principal (a selecao simples
/// tambem conta — com uma so marcada, ela e a principal).
Set<String> camadasDoLote(WidgetRef ref) => {
  ...ref.read(multiSelectProvider),
  ?ref.read(selectedLayerProvider),
};

/// LIGA O MODO SELECIONAR, ja com [comCamada] marcada. O painel aberto
/// fecha: ele editaria UMA camada, e agora a conversa e com o lote.
void ligarModoSelecionar(WidgetRef ref, {String? comCamada}) {
  ref.read(painelAbertoProvider.notifier).state = null;
  ref.read(modoSelecionarProvider.notifier).state = true;
  if (comCamada != null) {
    ref.read(selectedLayerProvider.notifier).state = comCamada;
  }
}

/// DESLIGA o modo e solta as marcadas (a principal continua escolhida).
void sairDoModoSelecionar(WidgetRef ref) {
  ref.read(modoSelecionarProvider.notifier).state = false;
  ref.read(multiSelectProvider.notifier).state = const {};
}

/// O TOQUE NUMA CAMADA COM O MODO LIGADO: marca ou desmarca [id]. Devolve
/// falso (e nao faz nada) com o modo desligado — a timeline chama isto
/// antes de selecionar do jeito simples.
bool alternarCamadaNoLote(WidgetRef ref, String id) {
  if (!ref.read(modoSelecionarProvider)) return false;
  final r = alternarNaSelecao(
    ref.read(multiSelectProvider),
    ref.read(selectedLayerProvider),
    id,
  );
  HapticFeedback.selectionClick();
  ref.read(multiSelectProvider.notifier).state = r.multi;
  ref.read(selectedLayerProvider.notifier).state = r.principal;
  return true;
}

/// As acoes do lote que so fazem sentido com duas ou mais camadas.
const _precisamDeDuas = {
  AcaoDoLote.agrupar,
  AcaoDoLote.cascata,
  AcaoDoLote.vincular,
};

/// A BARRA DO LOTE: quantas estao marcadas, e Agrupar · Alinhar · Cascata
/// · Vincular · Apagar · Soltar. O que pede duas camadas fica apagado com
/// uma so (e nao some: a pessoa ve que existe e o que falta).
///
/// Quem executa e [aoAcionar] — a casca, com o `ref` e o contexto que
/// sobrevivem a barra (agrupar solta o lote, e a barra sai da arvore no
/// meio da acao).
///
/// Chaves: `barra-do-lote`, `lote-contagem` e `ferramenta-lote-<acao>`.
class BarraDoLote extends ConsumerWidget {
  const BarraDoLote({super.key, required this.aoAcionar});

  final void Function(String idDaAcao) aoAcionar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final n = {
      ...ref.watch(multiSelectProvider),
      ?ref.watch(selectedLayerProvider),
    }.length;
    final ferramentas = [
      for (final f in ferramentasDoLote(aoAcionar: aoAcionar))
        if ((n < 2 && _precisamDeDuas.contains(f.id)) ||
            (n < 1 && f.id != AcaoDoLote.soltar))
          Ferramenta(id: f.id, icone: f.icone, rotulo: f.rotulo)
        else
          f,
    ];
    return BarraContextual(
      chave: 'barra-do-lote',
      ferramentas: ferramentas,
      aoTocar: (f) => f.acao?.call(),
      inicio: Container(
        key: const ValueKey('lote-contagem'),
        width: AureaDims.toqueConfortavel,
        alignment: Alignment.center,
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text(
              '$n',
              style: AureaEstilos.titulo.copyWith(color: AureaCores.destaque),
            ),
            const SizedBox(height: AureaDims.e2),
            AppText(n == 1 ? 'camada' : 'camadas', style: AureaEstilos.rotulo),
          ],
        ),
      ),
    );
  }
}

/// EXECUTA uma acao do lote ([AcaoDoLote]) com o contexto e o `ref` de
/// quem vive mais que a barra (a casca). Cada acao e UM passo de desfazer.
Future<void> acionarLote(
  BuildContext context,
  WidgetRef ref,
  String acao, {
  required PlaybackController playback,
}) async {
  final alvos = camadasDoLote(ref);
  if (acao == AcaoDoLote.soltar) {
    sairDoModoSelecionar(ref);
    return;
  }
  if (alvos.isEmpty) return;
  final c = ref.read(editorControllerProvider.notifier);
  final t = playback.time.value;
  playback.pause();
  switch (acao) {
    case AcaoDoLote.agrupar:
      if (alvos.length < 2) {
        showReasonToast(context, 'Um grupo precisa de duas ou mais camadas');
        return;
      }
      // groupLayers e uma mutacao so, e o grupo novo vira a selecao.
      c.runAsOneUndo(() => c.groupLayers(alvos.toList()));
      sairDoModoSelecionar(ref);
    case AcaoDoLote.alinhar:
      await showAlignSheet(context, ref, alvos.toList(), t);
    case AcaoDoLote.cascata:
      if (alvos.length < 2) return;
      abrirCascata(context, ref, alvos, t);
    case AcaoDoLote.vincular:
      if (alvos.length < 2) return;
      final pai = await escolherPai(context, ref, alvos);
      if (pai == null || !context.mounted) return;
      vincularAoPai(ref, alvos, pai.paiId, t);
      sairDoModoSelecionar(ref);
      if (pai.paiId != null) {
        AureaSnack.show(
          context,
          translate(context, 'Camadas vinculadas'),
          actionLabel: translate(context, 'Desfazer'),
          onAction: c.undo,
        );
      }
    case AcaoDoLote.apagar:
      // O magnetico apaga uma por uma (fecha o buraco de cada): tudo num
      // passo, senao desfazer devolveria uma camada por toque.
      final soTravadas = alvos.every(c.isLocked);
      if (soTravadas) {
        excluirCamadas(context, ref, alvos);
      } else {
        c.runAsOneUndo(() => excluirCamadas(context, ref, alvos));
      }
      sairDoModoSelecionar(ref);
  }
}

/// O ICONE DO MODO SELECIONAR — usado pela barra do projeto e pelo menu.
const IconData iconeDeSelecionar = CupertinoIcons.checkmark_square;
