import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/keyframe.dart';
import '../shell/contrato.dart';

// AS PECAS DOS PAINEIS CENTRAIS (Transformar, Efeitos, Cor, Tempo,
// Velocidade, Audio, Mascara, Borda e sombra, Propriedades).
//
// Nao sao componentes visuais novos: sao as regras que os nove paineis
// repetiriam — um passo de desfazer por acao, o sinal de interacao por
// passo de arrasto, a propriedade ativa da timeline e o seletor de cor
// num gesto so. O desenho continua todo no design system.

/// UM PASSO DE ARRASTO: marca o sinal de interacao (o palco troca
/// qualidade por resposta enquanto o dedo mexe) e so entao muda o valor.
ValueChanged<double> aCadaPasso(ValueChanged<double> mudar) => (v) {
  Interacao.marcar();
  mudar(v);
};

/// UMA ACAO DELIBERADA = UM PASSO DE DESFAZER, sempre.
///
/// Fora de um grupo, o controlador junta edicoes a menos de 450 ms numa
/// so. Reordenar, apagar, ligar e desligar sao escolhas distintas: se
/// viessem logo depois de um ajuste, um desfazer levaria as duas.
/// `runAsOneUndo` empilha o retrato antes, entao a acao entra sozinha.
void umPasso(WidgetRef ref, VoidCallback acao) =>
    ref.read(editorControllerProvider.notifier).runAsOneUndo(acao);

/// O SELETOR DE COR COMO UM GESTO: a cor muda viva enquanto o dedo
/// escolhe, e tudo vira UM passo de desfazer (abre o grupo antes da folha
/// e fecha quando ela sai). Sem o grupo, cada pausa de meio segundo no
/// quadro de cor virava um passo.
Future<void> escolherCor(
  BuildContext context,
  WidgetRef ref, {
  required Color inicial,
  required ValueChanged<Color> aplicar,
}) async {
  final c = ref.read(editorControllerProvider.notifier);
  c.beginGesture();
  try {
    final nova = await showColorPicker(
      context,
      initial: inicial,
      onChanged: (cor) {
        Interacao.marcar();
        aplicar(cor);
      },
    );
    if (nova != null) aplicar(nova);
  } finally {
    c.endGesture();
  }
}

/// O LOSANGO E AS SETAS de uma trilha qualquer, com o "agora" dado por
/// quem chama.
///
/// Existe ao lado de `losangoDasMarcas` (comum.dart) porque nem toda
/// trilha vive no tempo local QUANTIZADO da camada: a do Time Remap e a
/// do volume vivem no tempo do clipe cru (cabecote - inicio), e o
/// Posterize Time mudaria o "agora" delas por baixo. [agoraUs] e [marcasUs]
/// estao no mesmo relogio; [inicio] e o que volta ao relogio global.
({KeyframeState estado, VoidCallback? anterior, VoidCallback? proximo})
losangoNoInstante({
  required Iterable<int> marcasUs,
  required int agoraUs,
  required Duration inicio,
  required PlaybackController playback,
  required VoidCallback aoAlternar,
  VoidCallback? aoCurva,
}) {
  final marcas = marcasUs.toList(growable: false);
  final viz = marcasVizinhas(marcas, agoraUs);
  void ir(int us) {
    playback.pause();
    playback.seek(inicio + Duration(microseconds: us));
  }

  final ant = viz.anterior;
  final prox = viz.proxima;
  return (
    estado: KeyframeState(
      animated: marcas.isNotEmpty,
      here: temMarcaEm(marcas, agoraUs),
      onToggle: aoAlternar,
      onCurve: marcas.length < 2 ? null : aoCurva,
    ),
    anterior: ant == null ? null : () => ir(ant),
    proximo: prox == null ? null : () => ir(prox),
  );
}

/// Os instantes (µs) das marcas de uma trilha.
List<int> marcasDe(AnimatedDouble? trilha) => [
  for (final k in trilha?.keyframes ?? const <Keyframe<double>>[])
    k.time.inMicroseconds,
];

/// UM BOTAO DO CABECALHO do [AureaPanel] (o lugar das [AureaPanel.acoes]):
/// icone 20 numa area de toque de 44 x 38.
class AcaoDoCabecalho extends StatelessWidget {
  const AcaoDoCabecalho({
    super.key,
    required this.icone,
    required this.aoTocar,
    this.ativo = false,
  });

  final IconData icone;
  final VoidCallback aoTocar;

  /// Aceso no destaque (estado ligado); apagado no texto secundario.
  final bool ativo;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: aoTocar,
    child: SizedBox(
      width: AureaDims.toqueConfortavel,
      height: AureaDims.cabecalhoDoPainel,
      child: Icon(
        icone,
        size: AureaDims.iconeMd,
        color: ativo ? AureaCores.destaque : AureaCores.textoSecundario,
      ),
    ),
  );
}

/// ACOES DE UM TOQUE numa fileira que quebra linha: pilulas do DS.
/// Serve aos comandos que nao sao propriedade (congelar aqui,
/// normalizar, adicionar mascara).
class FileiraDeAcoes extends StatelessWidget {
  const FileiraDeAcoes({super.key, required this.acoes});

  final List<Widget> acoes;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.symmetric(vertical: AureaDims.e8),
    child: Wrap(
      spacing: AureaDims.e6,
      runSpacing: AureaDims.e6,
      children: acoes,
    ),
  );
}

/// O RESPIRO PADRAO DE UM CORPO PROPRIO (o que passa `corpo` ao painel em
/// vez de `filhos`): os mesmos 22 de lado e 15 embaixo do [AureaPanel].
const respiroDoPainel = EdgeInsets.fromLTRB(
  AureaDims.margemDoPainel,
  AureaDims.e4,
  AureaDims.margemDoPainel,
  AureaDims.topoDoPainel,
);

/// A PROPRIEDADE ATIVA ENQUANTO UM PAINEL ESTA ABERTO.
///
/// A sub-aba ativa decide quais keyframes a timeline destaca (a regra
/// que explica a interface do app de referencia). O painel escreve ao
/// trocar de aba e devolve a nulo quando sai — mas so se o valor ainda
/// for o dele: outro painel que abriu no mesmo quadro nao perde o seu.
///
/// A ESCRITA E ADIADA para depois do quadro: provider nao se muda no
/// meio de um build (initState, troca de aba durante a montagem).
mixin PropriedadeAtivaDoPainel<W extends ConsumerStatefulWidget>
    on ConsumerState<W> {
  StateController<PropriedadeAtiva?>? _ativa;
  PropriedadeAtiva? _minha;

  /// Marca [p] como a propriedade ativa da timeline.
  void ativarPropriedade(PropriedadeAtiva? p) {
    _ativa ??= ref.read(propriedadeAtivaProvider.notifier);
    _minha = p;
    final alvo = _ativa!;
    scheduleMicrotask(() {
      try {
        if (_minha == p) alvo.state = p;
      } catch (_) {
        // O container ja foi descartado (teste terminando): nada a fazer.
      }
    });
  }

  @override
  void dispose() {
    final alvo = _ativa;
    final minha = _minha;
    _minha = null;
    if (alvo != null && minha != null) {
      scheduleMicrotask(() {
        try {
          if (alvo.state == minha) alvo.state = null;
        } catch (_) {}
      });
    }
    super.dispose();
  }
}
