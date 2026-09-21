import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show ReorderableListView;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../../core/ds/ds.dart';
import '../../../../application/editor_controller.dart';
import '../../../../application/playback_controller.dart';
import '../../../../domain/animador_de_texto.dart';
import '../../../../domain/effect.dart';
import '../../../../domain/layer.dart';
import '../../../am/curve_panel.dart' show showTrackCurveSheet;
import '../../shell/contrato.dart';
import '../comum.dart';
import '../pecas_centrais.dart';
import 'animador_de_texto.dart';
import 'time_remap.dart';

/// A PILHA DE EFEITOS de uma camada, na ordem em que o motor aplica.
///
/// Um [AureaEffectCard] por efeito: seta abre e recolhe, olho liga e
/// desliga sem apagar, a alca ≡ reordena ARRASTANDO (sem toque longo:
/// a alca e so para isso) e o ⋯ guarda duplicar, resetar, copiar, colar
/// e apagar. Aberto, uma [AureaPropertyRow] por parametro, cada uma com o
/// seu losango — "toda linha de parametro tem o diamante".
///
/// Numa camada de texto, os Animadores de Texto entram no fim, como
/// cartoes iguais (o motor deles e outro, a pilha e a mesma).
///
/// Reutilizavel: o painel Cor mostra a mesma pilha filtrada na correcao
/// de cor. Com [filtro] a pilha NAO reordena — a posicao na lista
/// filtrada nao e a posicao na pilha, e arrastar pularia os escondidos.
class PilhaDeEfeitos extends ConsumerStatefulWidget {
  const PilhaDeEfeitos({
    super.key,
    required this.layerId,
    this.filtro,
    this.cabecalho = const [],
    this.rodape = const [],
    this.vazio = 'Nenhum efeito nesta camada. Toque em + para adicionar.',
  });

  final String layerId;
  final bool Function(EffectInstance e)? filtro;

  /// Pecas acima da pilha (chips de adicionar, avisos, linhas soltas).
  final List<Widget> cabecalho;

  /// Pecas abaixo da pilha (adicionar, colar).
  final List<Widget> rodape;
  final String vazio;

  @override
  ConsumerState<PilhaDeEfeitos> createState() => _PilhaDeEfeitosState();
}

class _PilhaDeEfeitosState extends ConsumerState<PilhaDeEfeitos>
    with PropriedadeAtivaDoPainel {
  /// Os cartoes abertos, por id (sobrevive a reordenar e a trocar de
  /// posicao). Animador de texto entra como `animador-<id>`.
  final Set<String> _abertos = {};

  /// O que ja estava na pilha no ultimo build: o que aparecer de novo
  /// (acabou de ser aplicado) abre sozinho — foi a pessoa que pediu.
  Set<String>? _conhecidos;

  void _abrir(String chave, bool aberto, {String? efeitoId}) {
    setState(() {
      if (aberto) {
        _abertos.add(chave);
      } else {
        _abertos.remove(chave);
      }
    });
    // O CARTAO ABERTO E A PROPRIEDADE ATIVA: a timeline acende as marcas
    // dele. Fechar devolve a camada inteira.
    ativarPropriedade(
      aberto && efeitoId != null
          ? PropriedadeAtiva.efeito(efeitoId)
          : null,
    );
  }

  @override
  Widget build(BuildContext context) {
    final visivel = camadaVisivel(ref, widget.layerId);
    final gravada = camadaGravada(ref, widget.layerId);
    if (visivel == null || gravada == null) {
      return const AureaAvisoDoPainel(texto: 'Esta camada não existe mais.');
    }
    final filtro = widget.filtro;
    final efeitos = [
      for (final e in visivel.effects)
        if (filtro == null || filtro(e)) e,
    ];
    final animadores = filtro == null && visivel is TextLayer
        ? [
            for (final a in visivel.animators)
              if (ehAnimadorDeTexto(a)) a,
          ]
        : const <Never>[];

    // O RECEM-APLICADO ABRE SOZINHO.
    final ids = {
      for (final e in efeitos) e.id,
      for (final a in animadores) 'animador-${a.id}',
    };
    final conhecidos = _conhecidos;
    if (conhecidos != null) {
      final novos = ids.difference(conhecidos);
      _abertos.addAll(novos);
      final efeitoNovo = [
        for (final e in efeitos)
          if (novos.contains(e.id)) e.id,
      ];
      if (efeitoNovo.isNotEmpty) {
        ativarPropriedade(PropriedadeAtiva.efeito(efeitoNovo.last));
      }
    }
    _abertos.retainAll(ids);
    _conhecidos = ids;

    final playback = EscopoDoEditor.of(context).playback;
    return NoCabecote(
      construir: (context, t) {
        Widget cartao(int i) {
          final e = efeitos[i];
          final registrado = gravada.effects
              .where((g) => g.id == e.id)
              .firstOrNull;
          return _CartaoDoEfeito(
            key: ValueKey('cartao-${e.id}'),
            layerId: widget.layerId,
            efeito: e,
            registrado: registrado ?? e,
            camada: gravada,
            t: t,
            playback: playback,
            aberto: _abertos.contains(e.id),
            aoMudarAberto: (v) => _abrir(e.id, v, efeitoId: e.id),
            indiceNaLista: filtro == null ? i : null,
          );
        }

        final topo = [
          ...widget.cabecalho,
          if (efeitos.isEmpty && animadores.isEmpty)
            AureaAvisoDoPainel(texto: widget.vazio),
        ];
        final fim = [
          for (final a in animadores)
            CartaoDoAnimadorDeTexto(
              key: ValueKey('cartao-animador-${a.id}'),
              layerId: widget.layerId,
              animador: a,
              camada: gravada,
              t: t,
              playback: playback,
              aberto: _abertos.contains('animador-${a.id}'),
              aoMudarAberto: (v) => _abrir('animador-${a.id}', v),
            ),
          ...widget.rodape,
        ];
        if (filtro != null) {
          return ListView(
            padding: respiroDoPainel,
            children: [
              ...topo,
              for (var i = 0; i < efeitos.length; i++) cartao(i),
              ...fim,
            ],
          );
        }
        return ReorderableListView.builder(
          key: const ValueKey('pilha-de-efeitos'),
          padding: respiroDoPainel,
          buildDefaultDragHandles: false,
          header: topo.isEmpty
              ? null
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: topo,
                ),
          footer: fim.isEmpty
              ? null
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: fim,
                ),
          itemCount: efeitos.length,
          itemBuilder: (context, i) => cartao(i),
          // `onReorderItem` ja entrega o destino descontado do item que
          // saiu: [para] e a posicao final dele. UM ARRASTO = UM PASSO de
          // desfazer, mesmo logo depois de um ajuste de numero.
          onReorderItem: (de, para) {
            if (para == de) return;
            final c = ref.read(editorControllerProvider.notifier);
            umPasso(
              ref,
              () => c.reorderEffect(widget.layerId, efeitos[de].id, para - de),
            );
          },
        );
      },
    );
  }
}

class _CartaoDoEfeito extends ConsumerWidget {
  const _CartaoDoEfeito({
    super.key,
    required this.layerId,
    required this.efeito,
    required this.registrado,
    required this.camada,
    required this.t,
    required this.playback,
    required this.aberto,
    required this.aoMudarAberto,
    required this.indiceNaLista,
  });

  final String layerId;

  /// O efeito como se VE (edicao pendente) — os numeros.
  final EffectInstance efeito;

  /// O efeito como esta GRAVADO — os losangos.
  final EffectInstance registrado;

  /// A camada gravada.
  final Layer camada;
  final Duration t;
  final PlaybackController playback;
  final bool aberto;
  final ValueChanged<bool> aoMudarAberto;
  final int? indiceNaLista;

  bool get _remap => efeito.type == EffectType.timeRemap;

  /// O TEMPO DO EFEITO: o Time Remap vive no tempo cru do clipe (a trilha
  /// dele E o tempo), todo o resto no tempo local da camada — que o
  /// Posterize Time pode quantizar.
  Duration get _local =>
      _remap ? t - camada.startTime : camada.localTime(t);

  Future<void> _menu(BuildContext botao, WidgetRef ref) async {
    final c = ref.read(editorControllerProvider.notifier);
    final conhecido = efeito.conhecido;
    final escolha = await mostrarAureaMenu<String>(
      botao,
      titulo: conhecido ? efeito.spec.name : null,
      itens: [
        // O TIME REMAP TEM UMA TRILHA SO: o clipe tem UM mapeamento de
        // tempo. Duplicar ou copiar nao significam nada nele.
        if (conhecido && !_remap)
          const AureaMenuItem(
            valor: 'duplicar',
            rotulo: 'Duplicar',
            icone: CupertinoIcons.plus_square_on_square,
            chave: 'efeito-duplicar',
          ),
        if (conhecido)
          const AureaMenuItem(
            valor: 'resetar',
            rotulo: 'Resetar',
            icone: CupertinoIcons.arrow_counterclockwise,
            chave: 'efeito-resetar',
          ),
        if (conhecido && !_remap)
          const AureaMenuItem(
            valor: 'copiar',
            rotulo: 'Copiar efeitos',
            icone: CupertinoIcons.doc_on_doc,
            chave: 'efeito-copiar',
          ),
        if (c.temEfeitosCopiados)
          const AureaMenuItem(
            valor: 'colar',
            rotulo: 'Colar efeitos',
            icone: CupertinoIcons.doc_on_clipboard,
            chave: 'efeito-colar',
          ),
        if (_remap)
          const AureaMenuItem(
            valor: 'curva',
            rotulo: 'Editar curva',
            icone: CupertinoIcons.graph_square,
            chave: 'efeito-curva',
          ),
        const AureaMenuItem(
          valor: 'apagar',
          rotulo: 'Apagar',
          icone: CupertinoIcons.trash,
          destrutivo: true,
          chave: 'efeito-apagar',
        ),
      ],
    );
    if (!botao.mounted) return;
    switch (escolha) {
      case 'duplicar':
        umPasso(ref, () => c.duplicateEffect(layerId, efeito.id));
      case 'resetar':
        _resetar(ref);
      case 'copiar':
        c.copyEffects(layerId);
      case 'colar':
        umPasso(ref, () => c.pasteEffects(layerId));
      case 'curva':
        _abrirCurva(botao, ref, 'tempo');
      case 'apagar':
        umPasso(ref, () => c.removeEffect(layerId, efeito.id));
    }
  }

  /// RESETAR os parametros no instante do cabecote, num desfazer so.
  void _resetar(WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    // O Time Remap volta a IDENTIDADE, nao ao zero do parametro: cravar o
    // `initial` (0 s) deixaria o clipe inteiro parado no primeiro quadro.
    if (_remap) {
      c.resetarCurvaDeTempo(layerId);
      return;
    }
    final agora = playback.time.value;
    c.runAsOneUndo(() {
      for (final e in efeito.spec.params.entries) {
        c.editEffectParam(layerId, efeito.id, e.key, agora, e.value.initial);
      }
      if (efeito.spec.hasColor) {
        c.setEffectColor(layerId, efeito.id, efeito.spec.defaultColor);
      }
    });
  }

  /// O EDITOR DE CURVA GERAL para a trilha [chave] deste efeito — o mesmo
  /// de qualquer propriedade, com a aba de velocidade. O Time Remap abre
  /// nele tambem (`rawTime`: o eixo e o tempo da fonte).
  void _abrirCurva(BuildContext context, WidgetRef ref, String chave) {
    final c = ref.read(editorControllerProvider.notifier);
    playback.pause();
    showTrackCurveSheet(
      context,
      ref,
      playback,
      label: efeito.spec.params[chave]?.label ?? efeito.spec.name,
      layerId: layerId,
      rawTime: _remap,
      trackOf: (l) {
        for (final e in l.effects) {
          if (e.id == efeito.id) return e.params[chave];
        }
        return null;
      },
      onSetEase: (seg, e) =>
          c.setEffectSegmentEase(layerId, efeito.id, seg, e),
      onSetEaseAll: (e) =>
          c.applyEaseToAllEffectSegments(layerId, efeito.id, e),
    );
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final conhecido = efeito.conhecido;
    final c = ref.read(editorControllerProvider.notifier);
    return AureaEffectCard(
      chave: 'efeito-${efeito.id}',
      nome: conhecido ? efeito.spec.name : efeito.type.name,
      traduzirNome: conhecido,
      ligado: efeito.enabled,
      aberto: aberto,
      aoMudarAberto: aoMudarAberto,
      indiceNaLista: indiceNaLista,
      aoAlternarLigado: () =>
          umPasso(ref, () => c.toggleEffectEnabled(layerId, efeito.id)),
      aoMenu: (botao) => _menu(botao, ref),
      filhos: !aberto
          ? const []
          : conhecido
          ? _linhas(context, ref)
          : const [
              AureaAvisoDoPainel(
                texto:
                    'Este efeito saiu do catálogo. Ele não desenha mais; '
                    'apague pelo ⋯.',
              ),
            ],
    );
  }

  List<Widget> _linhas(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final spec = efeito.spec;
    final local = _local;

    Widget? linha(String chave, EffectParam p) {
      final valor = efeito.paramAt(chave, local);
      final mudar = aCadaPasso(
        (v) => c.editEffectParam(
          layerId,
          efeito.id,
          chave,
          playback.time.value,
          v,
        ),
      );
      final kf = losangoNoInstante(
        marcasUs: marcasDe(registrado.params[chave]),
        agoraUs: local.inMicroseconds,
        inicio: camada.startTime,
        playback: playback,
        aoAlternar: () => c.toggleEffectParamKeyframe(
          layerId,
          efeito.id,
          chave,
          playback.time.value,
        ),
        aoCurva: () => _abrirCurva(context, ref, chave),
      );
      final base = '${efeito.id}-$chave';
      void resetar() => umPasso(
        ref,
        () => c.editEffectParam(
          layerId,
          efeito.id,
          chave,
          playback.time.value,
          p.initial,
        ),
      );
      switch (p.kind) {
        case ParamKind.color:
          // A cor do efeito vem pela linha de cor abaixo; parametro de cor
          // guardado como numero nao tem controle que faca sentido aqui.
          return null;
        case ParamKind.toggle:
          return AureaPropertyRow.personalizada(
            rotulo: p.label,
            chave: base,
            keyframe: kf.estado,
            aoAnterior: kf.anterior,
            aoProximo: kf.proximo,
            aoResetar: resetar,
            filho: AureaToggle(
              valor: valor > .5,
              aoMudar: (b) => umPasso(ref, () => mudar(b ? 1 : 0)),
            ),
          );
        case ParamKind.choice:
          final n = p.options.length;
          if (n == 0) return null;
          return AureaPropertyRow.personalizada(
            rotulo: p.label,
            chave: base,
            keyframe: kf.estado,
            aoAnterior: kf.anterior,
            aoProximo: kf.proximo,
            aoResetar: resetar,
            filho: AureaDropdown<int>(
              valor: valor.round().clamp(0, n - 1),
              opcoes: [for (var i = 0; i < n; i++) i],
              rotuloDe: (i) => p.options[i],
              titulo: p.label,
              aoMudar: (i) => umPasso(ref, () => mudar(i.toDouble())),
            ),
          );
        case ParamKind.number:
        case ParamKind.point:
        case ParamKind.seed:
          // A TRILHA DO TIME REMAP e um instante da fonte, sem teto util:
          // a regua de riscos, e nao um trilho de 0 a 24 horas.
          final semFaixa = _remap;
          return AureaPropertyRow(
            rotulo: p.label,
            chave: base,
            valor: valor,
            aoMudar: mudar,
            min: semFaixa ? 0 : p.min,
            max: semFaixa ? double.infinity : p.max,
            unidade: p.unit,
            casas: p.kind == ParamKind.seed
                ? 0
                : p.decimals ?? _casasAutomaticas(p),
            sensibilidade: p.dragStep,
            keyframe: kf.estado,
            aoAnterior: kf.anterior,
            aoProximo: kf.proximo,
            aoResetar: _remap ? null : resetar,
            aoComecarGesto: c.beginGesture,
            aoTerminarGesto: c.endGesture,
          );
      }
    }

    // A REGRA DOS GRUPOS DA FICHA: chave fora de todo grupo vem solta,
    // ANTES dos grupos — parametro escondido por esquecimento e pior que
    // parametro fora de lugar.
    final agrupadas = {for (final g in spec.grupos) ...g.chaves};
    final nomesDasCores = spec.colorLabels;
    final camadaDeVideo = camada is VideoLayer ? camada as VideoLayer : null;
    final linhas = <Widget>[
      if (_remap && camadaDeVideo != null)
        ...LinhasDoTimeRemap.antes(
          ref,
          layerId: layerId,
          t: t,
          playback: playback,
        ),
      for (final e in spec.params.entries)
        if (!agrupadas.contains(e.key)) ?linha(e.key, e.value),
      for (var gi = 0; gi < spec.grupos.length; gi++)
        AureaSection(
          titulo: spec.grupos[gi].rotulo,
          chave: '${efeito.id}-grupo-$gi',
          inicialmenteAberta: gi == 0,
          filhos: [
            for (final k in spec.grupos[gi].chaves)
              if (spec.params[k] case final p?) ?linha(k, p),
          ],
        ),
      if (spec.hasColor)
        AureaPropertyRow.cor(
          rotulo: nomesDasCores.isNotEmpty ? nomesDasCores.first : 'Cor',
          chave: '${efeito.id}-cor',
          cor: efeito.color,
          aoTocar: () => escolherCor(
            context,
            ref,
            inicial: efeito.color,
            aplicar: (cor) => c.setEffectColor(layerId, efeito.id, cor),
          ),
        ),
      for (var i = 0; i < spec.extraColors; i++)
        AureaPropertyRow.cor(
          rotulo: i + 1 < nomesDasCores.length
              ? nomesDasCores[i + 1]
              : 'Cor ${i + 2}',
          chave: '${efeito.id}-cor-${i + 2}',
          cor: efeito.extraColor(i),
          aoTocar: () => escolherCor(
            context,
            ref,
            inicial: efeito.extraColor(i),
            aplicar: (cor) =>
                c.setEffectExtraColor(layerId, efeito.id, i, cor),
          ),
        ),
      if (_remap && camadaDeVideo != null)
        ...LinhasDoTimeRemap.depois(
          context,
          ref,
          layerId: layerId,
          video: camadaDeVideo,
          t: t,
          abrirCurva: () => _abrirCurva(context, ref, 'tempo'),
        ),
    ];
    if (linhas.isEmpty) {
      return const [AureaAvisoDoPainel(texto: 'Este efeito não tem ajustes.')];
    }
    return linhas;
  }

  static int _casasAutomaticas(EffectParam p) {
    final faixa = (p.max - p.min).abs();
    if (faixa <= 2) return 3;
    if (faixa <= 20) return 2;
    return 1;
  }
}

