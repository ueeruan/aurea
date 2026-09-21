import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/opcoes_de_visualizacao.dart';
import '../palco/zoom_do_palco.dart';

/// A BARRA DE TRANSPORTE (46): desfazer e refazer a esquerda; quadro
/// anterior, play e proximo quadro no CENTRO; o tempo (atual / total) e o
/// modo de previa a direita. Enquanto o palco arrasta algo, a barra vira a
/// infobar (os numeros do que o dedo esta mudando).
///
/// O relogio chega por `ValueListenableBuilder`: o cabecote andando nao
/// reconstroi a barra inteira, so o texto do tempo e o icone do play.
///
/// Chaves: `transporte-desfazer`, `-refazer`, `-anterior`, `-play`,
/// `-proximo`, `-tempo`, `-modo`.
class BarraDeTransporte extends ConsumerWidget {
  const BarraDeTransporte({
    super.key,
    required this.playback,
    required this.telaCheia,
    required this.aoAlternarTelaCheia,
  });

  final PlaybackController playback;
  final bool telaCheia;
  final VoidCallback aoAlternarTelaCheia;

  Future<void> _menuDoModo(BuildContext botao, WidgetRef ref) async {
    final opcoes = ref.read(opcoesDeVisualizacaoProvider);
    final escolha = await mostrarAureaMenu<String>(
      botao,
      titulo: 'Prévia',
      itens: [
        for (final m in ModoDePrevia.values)
          AureaMenuItem(
            valor: 'modo-${m.name}',
            rotulo: rotuloDoModoDePrevia(m),
            marcado: opcoes.modo == m,
            chave: 'modo-${m.name}',
          ),
        AureaMenuItem(
          valor: 'grade',
          rotulo: 'Grade',
          icone: CupertinoIcons.grid,
          marcado: opcoes.grade,
          chave: 'grade',
        ),
        AureaMenuItem(
          valor: 'pixels',
          rotulo: 'Pixels reais',
          icone: CupertinoIcons.square_grid_4x3_fill,
          marcado: opcoes.pixels,
          chave: 'pixels',
        ),
        AureaMenuItem(
          valor: 'camera',
          rotulo: 'Visão da câmera',
          icone: CupertinoIcons.videocam,
          marcado: opcoes.visaoDaCamera,
          chave: 'camera',
        ),
        const AureaMenuItem(
          valor: 'ajustar',
          rotulo: 'Ajustar à tela',
          icone: CupertinoIcons.viewfinder,
          chave: 'ajustar',
        ),
        AureaMenuItem(
          valor: 'tela-cheia',
          rotulo: telaCheia ? 'Sair da tela cheia' : 'Tela cheia',
          icone: telaCheia
              ? CupertinoIcons.fullscreen_exit
              : CupertinoIcons.fullscreen,
          chave: 'tela-cheia',
        ),
      ],
    );
    if (escolha == null) return;
    final n = ref.read(opcoesDeVisualizacaoProvider.notifier);
    switch (escolha) {
      case 'grade':
        n.alternarGrade();
      case 'pixels':
        n.alternarPixels();
      case 'camera':
        n.alternarVisaoDaCamera();
      case 'ajustar':
        ref.read(zoomDoPalcoProvider.notifier).state = 1.0;
      case 'tela-cheia':
        aoAlternarTelaCheia();
      default:
        for (final m in ModoDePrevia.values) {
          if (escolha == 'modo-${m.name}') n.definirModo(m);
        }
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final info = ref.watch(infobarProvider);
    final c = ref.read(editorControllerProvider.notifier);
    // SO OS DOIS BOOLEANOS: o `select` roda a cada mutacao, mas a barra so
    // se refaz quando desfazer/refazer mudam de estado.
    final (podeDesfazer, podeRefazer) = ref.watch(
      editorControllerProvider.select((_) => (c.canUndo, c.canRedo)),
    );
    final total = ref.watch(editorControllerProvider.select((p) => p.duration));

    Widget botao(
      String chave,
      IconData icone,
      String dica,
      VoidCallback? acao, {
      double tamanho = AureaDims.iconeMd,
    }) => Semantics(
      label: translate(context, dica),
      button: true,
      child: Tocavel(
        key: ValueKey('transporte-$chave'),
        onTap: acao,
        child: SizedBox(
          width: AureaDims.botaoDeBarra,
          height: AureaDims.transporte,
          child: Icon(
            icone,
            size: tamanho,
            color: acao == null
                ? AureaCores.textoSecundario.withValues(alpha: .4)
                : AureaCores.texto,
          ),
        ),
      ),
    );

    if (info != null) {
      return Container(
        key: const ValueKey('transporte-infobar'),
        height: AureaDims.transporte,
        color: AureaCores.cromo,
        padding: const EdgeInsets.symmetric(horizontal: AureaDims.e10),
        child: _Infobar(info: info),
      );
    }

    return Container(
      key: const ValueKey('barra-de-transporte'),
      height: AureaDims.transporte,
      color: AureaCores.cromo,
      // TRES COLUNAS: os dois lados Expanded tem a MESMA largura, entao o
      // play fica no centro exato da tela; o tempo encolhe (FittedBox) antes
      // de invadir o play numa tela de 360.
      child: Row(
        children: [
          Expanded(
            child: Align(
              alignment: Alignment.centerLeft,
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const SizedBox(width: AureaDims.e2),
                  botao(
                    'desfazer',
                    CupertinoIcons.arrow_uturn_left,
                    'Desfazer',
                    podeDesfazer ? c.undo : null,
                  ),
                  botao(
                    'refazer',
                    CupertinoIcons.arrow_uturn_right,
                    'Refazer',
                    podeRefazer ? c.redo : null,
                  ),
                ],
              ),
            ),
          ),
          botao(
            'anterior',
            CupertinoIcons.backward_end_alt,
            'Quadro anterior',
            () {
              playback.pause();
              playback.stepFrame(-1);
            },
          ),
          ValueListenableBuilder<bool>(
            valueListenable: playback.playing,
            builder: (context, tocando, _) => botao(
              'play',
              tocando ? CupertinoIcons.pause_fill : CupertinoIcons.play_fill,
              tocando ? 'Pausar' : 'Tocar',
              playback.toggle,
              tamanho: AureaDims.iconeLg,
            ),
          ),
          botao(
            'proximo',
            CupertinoIcons.forward_end_alt,
            'Próximo quadro',
            () {
              playback.pause();
              playback.stepFrame(1);
            },
          ),
          Expanded(
            child: Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                Flexible(
                  child: FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: Alignment.centerRight,
                    // TOCAR O TEMPO digita para onde ir ("12.5", "1:02.5",
                    // "00:01:02:15") — a porta do relogio da barra antiga.
                    child: Tocavel(
                      onTap: () => irParaOTempo(context, ref, playback),
                      child: ValueListenableBuilder<Duration>(
                        valueListenable: playback.time,
                        builder: (context, t, _) => Text(
                          '${tempoDaInfobar(t)} / ${tempoDaInfobar(total)}',
                          key: const ValueKey('transporte-tempo'),
                          maxLines: 1,
                          style: AureaEstilos.valor.copyWith(
                            fontSize: 11,
                            fontWeight: FontWeight.w500,
                            color: AureaCores.textoSecundario,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
                Builder(
                  builder: (botaoCtx) => botao(
                    'modo',
                    CupertinoIcons.eye,
                    'Modo de prévia',
                    () => _menuDoModo(botaoCtx, ref),
                  ),
                ),
                const SizedBox(width: AureaDims.e2),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// Os numeros do que o dedo esta mudando no palco.
class _Infobar extends StatelessWidget {
  const _Infobar({required this.info});

  final DadosDaInfobar info;

  @override
  Widget build(BuildContext context) {
    final valor = AureaEstilos.valor;
    final tempo = info.tempo;
    if (tempo != null) {
      final d = info.deslocamento ?? Duration.zero;
      return Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            CupertinoIcons.rhombus,
            size: AureaDims.iconeSm - 2,
            color: AureaCores.keyframe,
          ),
          const SizedBox(width: AureaDims.e6),
          Text(tempoDaInfobar(tempo), style: valor),
          const SizedBox(width: AureaDims.e20),
          Text('${d.isNegative ? '' : '+'}${tempoDaInfobar(d)}', style: valor),
        ],
      );
    }
    return Row(
      children: [
        for (final (rotulo, v) in info.pares.take(6))
          Expanded(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                AppText(rotulo, maxLines: 1, style: AureaEstilos.rotulo),
                Text(v, maxLines: 1, overflow: TextOverflow.fade, style: valor),
              ],
            ),
          ),
      ],
    );
  }
}

/// IR PARA O TEMPO: o relogio da barra aceita "12.5", "1:02.5",
/// "00:01:02:15". O cabecote vai ao instante digitado, preso ao projeto.
Future<void> irParaOTempo(
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
    barrierDismissible: true,
    builder: (ctx) => CupertinoAlertDialog(
      title: const AppText('Ir para o tempo'),
      content: Padding(
        padding: const EdgeInsets.only(top: AureaDims.e10),
        child: CupertinoTextField(
          key: const ValueKey('transport-timecode-campo'),
          controller: ctrl,
          autofocus: true,
          keyboardType: const TextInputType.numberWithOptions(decimal: true),
          placeholder: translate(ctx, 'segundos, ou mm:ss.ms'),
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

/// Le "12.5", "1:02.5", "00:01:02:15" (o ultimo campo em quadros quando
/// ha tres separadores). Nulo quando o texto nao e um tempo.
Duration? parseTimecodeInput(String texto, int fps) {
  final s = texto.trim().replaceAll(',', '.');
  if (s.isEmpty) return null;
  final partes = s.split(':');
  try {
    if (partes.length == 1) {
      return Duration(microseconds: (double.parse(partes[0]) * 1e6).round());
    }
    if (partes.length == 2) {
      final m = int.parse(partes[0]);
      final seg = double.parse(partes[1]);
      return Duration(microseconds: ((m * 60 + seg) * 1e6).round());
    }
    if (partes.length == 3) {
      final h = int.parse(partes[0]);
      final m = int.parse(partes[1]);
      final seg = double.parse(partes[2]);
      return Duration(microseconds: ((h * 3600 + m * 60 + seg) * 1e6).round());
    }
    if (partes.length == 4) {
      final h = int.parse(partes[0]);
      final m = int.parse(partes[1]);
      final seg = int.parse(partes[2]);
      final q = int.parse(partes[3]);
      return Duration(
        microseconds:
            ((h * 3600 + m * 60 + seg) * 1e6).round() +
            (q * 1e6 / (fps <= 0 ? 30 : fps)).round(),
      );
    }
  } catch (_) {
    return null;
  }
  return null;
}
