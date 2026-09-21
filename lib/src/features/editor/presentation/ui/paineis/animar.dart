import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/animador_de_texto.dart';
import '../../../domain/layer.dart';
import '../../../domain/text_anim.dart';
import '../../../domain/text_animator.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';
import 'estilo.dart' show nomeDoEspacamento;

// ===========================================================================
// ANIMAR (texto): SO PREDEFINICOES
// ===========================================================================
//
// O animador manual antigo (posicoes, grade de miniaturas, seletores
// montados a mao) NAO voltou — o dono o julgou impossivel de usar, e ele
// virou o efeito "Animador de Texto" (Efeitos -> Texto). Aqui fica o que se
// escolhe com um toque:
//
//  * Entrada, Enfase e Saida: o catalogo de animacoes de texto
//    (`textAnimCatalog`), UMA por posicao — escolher outra troca, nao
//    empilha;
//  * Animador: as receitas do mesmo motor do efeito (Palavra por palavra,
//    Letra por letra, Pop, Bounce...). Trocar de receita troca a receita
//    do animador que ja esta la, em vez de empilhar cinco — e o atalho
//    abre o efeito na pilha de Efeitos, onde cada numero se ajusta.

/// A receita aplicada por este painel, reconhecida pelo nome (o efeito na
/// pilha tambem troca de receita pelo nome).
bool _ehDeReceita(TextAnimator a) =>
    presetsDoAnimador.any((p) => p.nome == a.name);

class PainelAnimar extends ConsumerStatefulWidget {
  const PainelAnimar({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelAnimar> createState() => _PainelAnimarState();
}

class _PainelAnimarState extends ConsumerState<PainelAnimar> {
  static const _titulo = 'Animar';
  static const _abas = ['Entrada', 'Ênfase', 'Saída', 'Animador'];
  static const _posicoes = [
    TextAnimSlot.entrada,
    TextAnimSlot.enfase,
    TextAnimSlot.saida,
  ];

  int _aba = 0;

  String get _id => widget.layerId;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, _id);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.animar.name}';
    if (camada is! TextLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de texto.',
      );
    }
    final linhas = _aba < _posicoes.length
        ? _posicao(camada, _posicoes[_aba])
        : _animador(camada, escopo);
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      corpo: ListView(
        key: ValueKey('animar-aba-$_aba'),
        padding: const EdgeInsets.fromLTRB(
          AureaDims.margemDoPainel,
          AureaDims.e6,
          AureaDims.margemDoPainel,
          AureaDims.topoDoPainel,
        ),
        children: linhas,
      ),
    );
  }

  // ------------------------------------------- entrada, enfase, saida

  List<Widget> _posicao(TextLayer camada, TextAnimSlot posicao) {
    final aplicada = camada.anims.where((a) => a.slot == posicao).firstOrNull;
    final opcoes = textAnimsForSlot(posicao);
    return [
      // UMA FILEIRA QUE ROLA DE LADO: com trinta animacoes em grade, a
      // duracao da escolhida ficava tres telas abaixo.
      FileiraDePilulas<String?>(
        chave: 'animar-${posicao.name}',
        chaveDe: (id) => id ?? 'nenhuma',
        opcoes: [null, for (final spec in opcoes) spec.id],
        atual: aplicada?.specId,
        rotuloDe: (id) =>
            id == null ? 'Nenhuma' : opcoes.firstWhere((s) => s.id == id).label,
        aoEscolher: (id) => _c.setTextAnim(_id, posicao, id),
      ),
      if (aplicada != null) ...[
        linhaNumerica(
          ref,
          rotulo: 'Duração',
          chave: 'animar-duracao',
          valor: aplicada.duration.inMilliseconds / 1000,
          min: .1,
          max: 10,
          casas: 2,
          unidade: 's',
          aoMudar: (v) => _c.updateTextAnim(
            _id,
            aplicada.id,
            (a) => a.copyWith(
              duration: Duration(milliseconds: (v * 1000).round()),
            ),
          ),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Atraso por letra',
          chave: 'animar-atraso',
          valor: aplicada.stagger.inMilliseconds.toDouble(),
          min: 0,
          max: 500,
          unidade: 'ms',
          aoMudar: (v) => _c.updateTextAnim(
            _id,
            aplicada.id,
            (a) => a.copyWith(stagger: Duration(milliseconds: v.round())),
          ),
        ),
      ],
    ];
  }

  // ------------------------------------------------------------- animador

  void _aplicarReceita(TextLayer camada, PresetDoAnimador preset) {
    final atual = camada.animators.where(_ehDeReceita).firstOrNull;
    if (atual != null) {
      _c.aplicarPresetNoAnimador(_id, atual.id, preset);
      return;
    }
    _c.runAsOneUndo(() {
      final novo = _c.addTextAnimator(_id, receita: preset.receita);
      if (novo != null) _c.aplicarPresetNoAnimador(_id, novo, preset);
    });
  }

  void _tirarReceitas(TextLayer camada) {
    final ids = [
      for (final a in camada.animators)
        if (_ehDeReceita(a)) a.id,
    ];
    if (ids.isEmpty) return;
    _c.runAsOneUndo(() {
      for (final id in ids) {
        _c.removeTextAnimator(_id, id);
      }
    });
  }

  List<Widget> _animador(TextLayer camada, EscopoDoEditor escopo) {
    final daReceita = camada.animators.where(_ehDeReceita).firstOrNull;
    // O ESPACAMENTO e animador tambem, mas e estilo: mora no painel Estilo.
    final pilha = [
      for (final a in camada.animators)
        if (a.name != nomeDoEspacamento) a,
    ];
    return [
      LinhaDePorta(
        key: const ValueKey('animar-efeito'),
        rotulo: 'Animador de Texto (efeito)',
        icone: CupertinoIcons.sparkles,
        aoTocar: () => escopo.abrirPainel(PainelId.efeitos),
      ),
      FileiraDePilulas<PresetDoAnimador?>(
        chave: 'animar-receita',
        chaveDe: (p) => p?.id ?? 'nenhuma',
        opcoes: [null, ...presetsDoAnimador],
        atual: presetsDoAnimador
            .where((p) => p.nome == daReceita?.name)
            .firstOrNull,
        rotuloDe: (p) => p?.nome ?? 'Nenhuma',
        aoEscolher: (p) =>
            p == null ? _tirarReceitas(camada) : _aplicarReceita(camada, p),
      ),
      if (pilha.isNotEmpty) ...[
        for (final a in pilha)
          AureaPropertyRow.personalizada(
            // O NOME do animador e dado do projeto (receita ou renomeado).
            rotulo: a.name,
            chave: 'animador-${a.id}',
            filho: Row(
              children: [
                AureaToggle(
                  valor: a.enabled,
                  aoMudar: (_) => _c.toggleTextAnimator(_id, a.id),
                ),
                const Spacer(),
                Tocavel(
                  key: ValueKey('animador-${a.id}-apagar'),
                  onTap: () => _c.removeTextAnimator(_id, a.id),
                  child: SizedBox(
                    width: AureaDims.toqueConfortavel,
                    height: AureaDims.toqueConfortavel,
                    child: Icon(
                      CupertinoIcons.trash,
                      size: AureaDims.iconeSm + 2,
                      color: AureaCores.textoSecundario,
                    ),
                  ),
                ),
              ],
            ),
          ),
      ],
    ];
  }
}
