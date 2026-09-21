import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../../../core/utils/time_format.dart';
import '../../../application/editor_controller.dart';
import '../../../application/font_service.dart';
import '../../../domain/caption.dart';
import '../../../domain/caption_highlight.dart';
import '../../../domain/layer.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';
import 'fonte.dart' show rotuloDaFontePadrao;

/// LEGENDAS — duas abas: as FALAS (cada uma corrigivel em linha; o tempo
/// leva o cabecote ate ela) e o ESTILO destaque da camada inteira (um
/// toque num pronto, e os ajustes finos embaixo).
///
/// Corrigir uma fala TRAVA a fala: uma nova transcricao nao sobrescreve o
/// que a pessoa corrigiu (regra do controlador, `updateCueText`).
class PainelLegendas extends ConsumerStatefulWidget {
  const PainelLegendas({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelLegendas> createState() => _PainelLegendasState();
}

class _PainelLegendasState extends ConsumerState<PainelLegendas> {
  static const _titulo = 'Legendas';
  static const _abas = ['Falas', 'Estilo'];

  int _aba = 0;

  String get _id => widget.layerId;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, _id);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.legendas.name}';
    if (camada is! CaptionLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de legendas.',
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: () {
        FocusScope.of(context).unfocus();
        escopo.fecharPainel();
      },
      corpo: _aba == 0 ? _falas(camada, escopo) : _estilo(camada),
    );
  }

  // ------------------------------------------------------------- falas

  Widget _falas(CaptionLayer camada, EscopoDoEditor escopo) {
    final cues = camada.cues;
    if (cues.isEmpty) {
      return const Padding(
        padding: EdgeInsets.symmetric(horizontal: AureaDims.margemDoPainel),
        child: AureaAvisoDoPainel(texto: 'Esta camada não tem falas.'),
      );
    }
    final fps = ref.watch(projetoVisivelProvider.select((p) => p.fps));
    return ListView.builder(
      key: const ValueKey('legendas-falas'),
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        AureaDims.e4,
        AureaDims.e10,
        AureaDims.topoDoPainel,
      ),
      itemCount: cues.length,
      itemBuilder: (context, i) {
        final cue = cues[i];
        return _LinhaDaFala(
          key: ValueKey('fala-${cue.id}'),
          cue: cue,
          tempo: formatTimecode(cue.start, fps),
          aoIr: () {
            escopo.playback.pause();
            escopo.playback.seek(camada.startTime + cue.start);
          },
          aoMudarTexto: (v) => _c.updateCueText(_id, cue.id, v),
          aoApagar: () => _c.removeCue(_id, cue.id),
        );
      },
    );
  }

  // ------------------------------------------------------------ estilo

  Widget _estilo(CaptionLayer camada) {
    final h = camada.highlight;
    void edita(CaptionHighlightStyle Function(CaptionHighlightStyle) f) =>
        _c.updateCaptionHighlight(_id, f);
    // SEM TEMPO POR PALAVRA nao ha o que destacar: a legenda foi gerada em
    // frases. Dizer isso, e nao mostrar controle que nao faz nada.
    final temPalavras =
        camada.cues.length > 1 &&
        camada.cues.every((c) => c.end - c.start < const Duration(seconds: 2));
    final familias = <String?>[null, ...FontService.instance.families];
    String nomeDaFonte(String? f) =>
        f ?? translate(context, rotuloDaFontePadrao);

    Future<void> cor(
      Color atual,
      CaptionHighlightStyle Function(CaptionHighlightStyle, Color) f,
    ) async {
      final nova = await showColorPicker(context, initial: atual);
      if (nova != null) edita((s) => f(s, nova));
    }

    return ListView(
      key: const ValueKey('legendas-estilo'),
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        AureaDims.e4,
        AureaDims.margemDoPainel,
        AureaDims.topoDoPainel,
      ),
      children: [
        if (!temPalavras)
          const AureaAvisoDoPainel(
            texto:
                'Esta legenda foi gerada em frases. O destaque precisa do '
                'tempo por palavra: gere de novo no modo palavra por palavra.',
          ),
        Wrap(
          spacing: AureaDims.e6,
          runSpacing: AureaDims.e6,
          children: [
            AureaChip(
              key: const ValueKey('legenda-pronto-comum'),
              rotulo: 'Comum',
              ativo: !h.ativo,
              aoTocar: () => edita((_) => const CaptionHighlightStyle()),
            ),
            for (final (nome, preset) in HighlightPresets.todos)
              AureaChip(
                key: ValueKey('legenda-pronto-$nome'),
                rotulo: nome,
                ativo:
                    h.ativo &&
                    h.layout == preset.layout &&
                    h.corDestaque == preset.corDestaque,
                aoTocar: () => edita((_) => preset),
              ),
          ],
        ),
        if (h.ativo) ...[
          const SizedBox(height: AureaDims.e6),
          AureaPropertyRow.cor(
            rotulo: 'Cor do destaque',
            chave: 'legenda-cor-destaque',
            cor: h.corDestaque,
            aoTocar: () =>
                cor(h.corDestaque, (s, c) => s.copyWith(corDestaque: c)),
          ),
          AureaPropertyRow.cor(
            rotulo: 'Cor do contexto',
            chave: 'legenda-cor-contexto',
            cor: h.corContexto,
            aoTocar: () =>
                cor(h.corContexto, (s, c) => s.copyWith(corContexto: c)),
          ),
          linhaNumerica(
            ref,
            rotulo: 'Destaque',
            chave: 'legenda-destaque',
            valor: h.destaque * 100,
            min: 100,
            max: 320,
            unidade: '%',
            aoMudar: (v) => edita((s) => s.copyWith(destaque: v / 100)),
            aoResetar: () => edita((s) => s.copyWith(destaque: 1.9)),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Arranjo',
            chave: 'legenda-arranjo',
            filho: AureaDropdown<HighlightLayout>(
              valor: h.layout,
              opcoes: HighlightLayout.values,
              rotuloDe: (l) => l.rotulo,
              titulo: 'Arranjo',
              aoMudar: (l) => edita((s) => s.copyWith(layout: l)),
            ),
          ),
          linhaDeLigar(
            rotulo: 'Maiúsculas',
            chave: 'legenda-maiusculas',
            valor: h.maiusculas,
            aoMudar: (v) => edita((s) => s.copyWith(maiusculas: v)),
          ),
          linhaNumerica(
            ref,
            rotulo: 'Espaçamento',
            chave: 'legenda-espacamento',
            valor: h.tracking,
            min: -6,
            max: 8,
            casas: 1,
            aoMudar: (v) => edita((s) => s.copyWith(tracking: v)),
            aoResetar: () => edita((s) => s.copyWith(tracking: 0)),
          ),
          linhaNumerica(
            ref,
            rotulo: 'Entrelinha',
            chave: 'legenda-entrelinha',
            valor: h.entrelinha * 100,
            min: 70,
            max: 180,
            unidade: '%',
            aoMudar: (v) => edita((s) => s.copyWith(entrelinha: v / 100)),
            aoResetar: () => edita((s) => s.copyWith(entrelinha: 1.05)),
          ),
          linhaNumerica(
            ref,
            rotulo: 'Inflar',
            chave: 'legenda-inflar',
            valor: h.duracaoInflar.inMilliseconds.toDouble(),
            min: 60,
            max: 600,
            unidade: 'ms',
            aoMudar: (v) => edita(
              (s) =>
                  s.copyWith(duracaoInflar: Duration(milliseconds: v.round())),
            ),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Contexto',
            chave: 'legenda-contexto',
            filho: FileiraDePilulas<int>(
              chave: 'legenda-contexto',
              opcoes: [for (var n = 0; n <= kMaxContextoPorLado; n++) n],
              atual: h.contextoPorLado,
              rotuloDe: (n) => '$n',
              traduzir: false,
              aoEscolher: (n) => edita((s) => s.copyWith(contextoPorLado: n)),
            ),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Fonte do destaque',
            chave: 'legenda-fonte-destaque',
            filho: AureaDropdown<String?>(
              valor: familias.contains(h.fonteDestaque)
                  ? h.fonteDestaque
                  : null,
              opcoes: familias,
              rotuloDe: nomeDaFonte,
              traduzir: false,
              aoMudar: (f) => edita(
                (s) => f == null
                    ? CaptionHighlightStyle(
                        ativo: s.ativo,
                        layout: s.layout,
                        destaque: s.destaque,
                        corDestaque: s.corDestaque,
                        corContexto: s.corContexto,
                        fonteContexto: s.fonteContexto,
                        maiusculas: s.maiusculas,
                        tracking: s.tracking,
                        entrelinha: s.entrelinha,
                        duracaoInflar: s.duracaoInflar,
                        contextoPorLado: s.contextoPorLado,
                        atrasDaPessoa: s.atrasDaPessoa,
                      )
                    : s.copyWith(fonteDestaque: f),
              ),
            ),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Fonte do contexto',
            chave: 'legenda-fonte-contexto',
            filho: AureaDropdown<String?>(
              valor: familias.contains(h.fonteContexto)
                  ? h.fonteContexto
                  : null,
              opcoes: familias,
              rotuloDe: nomeDaFonte,
              traduzir: false,
              aoMudar: (f) => edita(
                (s) => f == null
                    ? CaptionHighlightStyle(
                        ativo: s.ativo,
                        layout: s.layout,
                        destaque: s.destaque,
                        corDestaque: s.corDestaque,
                        corContexto: s.corContexto,
                        fonteDestaque: s.fonteDestaque,
                        maiusculas: s.maiusculas,
                        tracking: s.tracking,
                        entrelinha: s.entrelinha,
                        duracaoInflar: s.duracaoInflar,
                        contextoPorLado: s.contextoPorLado,
                        atrasDaPessoa: s.atrasDaPessoa,
                      )
                    : s.copyWith(fonteContexto: f),
              ),
            ),
          ),
        ],
      ],
    );
  }
}

/// UMA FALA: o tempo (toque leva o cabecote ate ela), o texto corrigivel
/// e o apagar. O campo segue a fala enquanto nao esta sendo digitado —
/// um desfazer atualiza o que se ve.
class _LinhaDaFala extends StatefulWidget {
  const _LinhaDaFala({
    super.key,
    required this.cue,
    required this.tempo,
    required this.aoIr,
    required this.aoMudarTexto,
    required this.aoApagar,
  });

  final Cue cue;
  final String tempo;
  final VoidCallback aoIr;
  final ValueChanged<String> aoMudarTexto;
  final VoidCallback aoApagar;

  @override
  State<_LinhaDaFala> createState() => _LinhaDaFalaState();
}

class _LinhaDaFalaState extends State<_LinhaDaFala> {
  late final TextEditingController _campo = TextEditingController(
    text: _plano(widget.cue.text),
  );
  final _foco = FocusNode();

  /// A fala GUARDA quebras de linha (o controlador quebra para caber);
  /// no campo de uma linha ela aparece corrida.
  static String _plano(String s) => s.replaceAll('\n', ' ');

  @override
  void didUpdateWidget(_LinhaDaFala old) {
    super.didUpdateWidget(old);
    final novo = _plano(widget.cue.text);
    if (!_foco.hasFocus && novo != _campo.text) _campo.text = novo;
  }

  @override
  void dispose() {
    _campo.dispose();
    _foco.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => SizedBox(
    height: AureaDims.linhaDePropriedade,
    child: Row(
      children: [
        Tocavel(
          key: ValueKey('fala-${widget.cue.id}-tempo'),
          onTap: widget.aoIr,
          child: Container(
            width: 72,
            height: AureaDims.alturaDaCaixaDeValor,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioMd),
            ),
            // O TEMPO e numero, nao frase: Text.
            child: Text(
              widget.tempo,
              style: AureaEstilos.valor.copyWith(
                fontSize: 11,
                color: widget.cue.locked
                    ? AureaCores.destaque
                    : AureaCores.textoSecundario,
              ),
            ),
          ),
        ),
        const SizedBox(width: AureaDims.e6),
        Expanded(
          child: CupertinoTextField(
            key: ValueKey('fala-${widget.cue.id}-texto'),
            controller: _campo,
            focusNode: _foco,
            maxLines: 1,
            style: AureaEstilos.corpo,
            padding: const EdgeInsets.symmetric(
              horizontal: AureaDims.e8,
              vertical: AureaDims.e6,
            ),
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioMd),
            ),
            onTapOutside: (_) => _foco.unfocus(),
            onChanged: widget.aoMudarTexto,
          ),
        ),
        Tocavel(
          key: ValueKey('fala-${widget.cue.id}-apagar'),
          onTap: widget.aoApagar,
          child: SizedBox(
            width: AureaDims.toqueMinimo,
            height: AureaDims.toqueConfortavel,
            child: Icon(
              CupertinoIcons.xmark,
              size: AureaDims.iconeSm,
              color: AureaCores.textoSecundario,
            ),
          ),
        ),
      ],
    ),
  );
}
