import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show ReorderableListView;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/effect.dart';
import '../../../domain/layer.dart';
import '../../am/color_picker_sheet.dart' show showColorPicker;
import '../../context/effects/effect_gallery.dart' show showEffectGallery;
import '../shell/contrato.dart';
import 'comum.dart';

/// EFEITOS — a pilha da camada: cada efeito um [AureaEffectCard] (abre e
/// recolhe, olho, reordenar pela alca, ⋯ com duplicar/subir/descer/apagar)
/// e, aberto, uma [AureaPropertyRow] por parametro, com losango proprio.
///
/// O "+" do cabecalho abre a galeria de efeitos que ja existe (com as
/// previas): a galeria nova e de outra frente.
class PainelEfeitos extends ConsumerWidget {
  const PainelEfeitos({super.key, required this.layerId});

  final String layerId;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    return AureaPanel(
      titulo: 'Efeitos',
      chave: 'painel-${PainelId.efeitos.name}',
      aoFechar: escopo.fecharPainel,
      acoes: [
        Tocavel(
          key: const ValueKey('efeitos-adicionar'),
          onTap: () {
            escopo.playback.pause();
            showEffectGallery(context, ref, layerId, escopo.playback);
          },
          child: SizedBox(
            width: AureaDims.toqueConfortavel,
            height: AureaDims.cabecalhoDoPainel,
            child: Icon(
              CupertinoIcons.plus,
              size: AureaDims.iconeMd,
              color: AureaCores.destaque,
            ),
          ),
        ),
      ],
      corpo: PilhaDeEfeitos(layerId: layerId),
    );
  }
}

/// A PILHA DE EFEITOS de uma camada, reutilizavel (o painel Cor mostra a
/// mesma pilha filtrada na correcao de cor).
///
/// Com [filtro] a pilha NAO reordena: a posicao na lista filtrada nao e a
/// posicao na pilha, e mover "um para cima" pularia efeitos escondidos.
class PilhaDeEfeitos extends ConsumerStatefulWidget {
  const PilhaDeEfeitos({
    super.key,
    required this.layerId,
    this.filtro,
    this.cabecalho = const [],
    this.vazio = 'Nenhum efeito nesta camada. Toque em + para adicionar.',
  });

  final String layerId;
  final bool Function(EffectInstance e)? filtro;

  /// Pecas acima da pilha (chips de adicionar, avisos).
  final List<Widget> cabecalho;
  final String vazio;

  @override
  ConsumerState<PilhaDeEfeitos> createState() => _PilhaDeEfeitosState();
}

class _PilhaDeEfeitosState extends ConsumerState<PilhaDeEfeitos> {
  /// Os cartoes abertos, por id do efeito (sobrevive a reordenar).
  final Set<String> _abertos = {};

  static const _padding = EdgeInsets.fromLTRB(
    AureaDims.margemDoPainel,
    AureaDims.e4,
    AureaDims.margemDoPainel,
    AureaDims.topoDoPainel,
  );

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
    final playback = EscopoDoEditor.of(context).playback;
    return NoCabecote(
      construir: (context, t) {
        Widget cartao(int i) {
          final e = efeitos[i];
          final registrado = gravada.effects
              .where((g) => g.id == e.id)
              .firstOrNull;
          return _CartaoDoEfeito(
            key: ValueKey('efeito-${e.id}'),
            layerId: widget.layerId,
            efeito: e,
            registrado: registrado ?? e,
            camada: gravada,
            t: t,
            playback: playback,
            aberto: _abertos.contains(e.id),
            aoMudarAberto: (v) => setState(() {
              if (v) {
                _abertos.add(e.id);
              } else {
                _abertos.remove(e.id);
              }
            }),
            indiceNaLista: filtro == null ? i : null,
          );
        }

        final topo = [
          ...widget.cabecalho,
          if (efeitos.isEmpty) AureaAvisoDoPainel(texto: widget.vazio),
        ];
        if (filtro != null) {
          return ListView(
            padding: _padding,
            children: [...topo, for (var i = 0; i < efeitos.length; i++) cartao(i)],
          );
        }
        return ReorderableListView.builder(
          padding: _padding,
          buildDefaultDragHandles: false,
          header: topo.isEmpty
              ? null
              : Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: topo,
                ),
          itemCount: efeitos.length,
          itemBuilder: (context, i) => cartao(i),
          // `onReorderItem` ja entrega o destino descontado do item que
          // saiu: [para] e a posicao final dele na lista.
          onReorderItem: (de, para) {
            if (para == de) return;
            ref
                .read(editorControllerProvider.notifier)
                .reorderEffect(widget.layerId, efeitos[de].id, para - de);
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
  final Layer camada;
  final Duration t;
  final PlaybackController playback;
  final bool aberto;
  final ValueChanged<bool> aoMudarAberto;
  final int? indiceNaLista;

  Future<void> _menu(BuildContext botao, WidgetRef ref) async {
    final c = ref.read(editorControllerProvider.notifier);
    final escolha = await mostrarAureaMenu<String>(
      botao,
      itens: const [
        AureaMenuItem(
          valor: 'duplicar',
          rotulo: 'Duplicar',
          icone: CupertinoIcons.plus_square_on_square,
        ),
        AureaMenuItem(
          valor: 'subir',
          rotulo: 'Subir',
          icone: CupertinoIcons.arrow_up,
        ),
        AureaMenuItem(
          valor: 'descer',
          rotulo: 'Descer',
          icone: CupertinoIcons.arrow_down,
        ),
        AureaMenuItem(
          valor: 'apagar',
          rotulo: 'Apagar',
          icone: CupertinoIcons.trash,
          destrutivo: true,
        ),
      ],
    );
    switch (escolha) {
      case 'duplicar':
        c.duplicateEffect(layerId, efeito.id);
      case 'subir':
        c.reorderEffect(layerId, efeito.id, -1);
      case 'descer':
        c.reorderEffect(layerId, efeito.id, 1);
      case 'apagar':
        c.removeEffect(layerId, efeito.id);
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final conhecido = efeito.conhecido;
    return AureaEffectCard(
      chave: 'efeito-${efeito.id}',
      nome: conhecido ? efeito.spec.name : efeito.type.name,
      ligado: efeito.enabled,
      aberto: aberto,
      aoMudarAberto: aoMudarAberto,
      indiceNaLista: indiceNaLista,
      aoAlternarLigado: () => ref
          .read(editorControllerProvider.notifier)
          .toggleEffectEnabled(layerId, efeito.id),
      aoMenu: (botao) => _menu(botao, ref),
      filhos: conhecido
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
    final local = camada.localTime(t);

    Widget? linha(String chave, EffectParam p) {
      final valor = efeito.paramAt(chave, local);
      void mudar(double v) =>
          c.editEffectParam(layerId, efeito.id, chave, t, v);
      final trilha = registrado.params[chave];
      final kf = losangoDasMarcas(
        marcasUs: [
          for (final k in trilha?.keyframes ?? const [])
            k.time.inMicroseconds,
        ],
        camada: camada,
        t: t,
        playback: playback,
        aoAlternar: () =>
            c.toggleEffectParamKeyframe(layerId, efeito.id, chave, t),
      );
      final base = '${efeito.id}-$chave';
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
            filho: AureaToggle(valor: valor > .5, aoMudar: (b) => mudar(b ? 1 : 0)),
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
            filho: AureaDropdown<int>(
              valor: valor.round().clamp(0, n - 1),
              opcoes: [for (var i = 0; i < n; i++) i],
              rotuloDe: (i) => p.options[i],
              aoMudar: (i) => mudar(i.toDouble()),
            ),
          );
        case ParamKind.number:
        case ParamKind.point:
        case ParamKind.seed:
          return AureaPropertyRow(
            rotulo: p.label,
            chave: base,
            valor: valor,
            aoMudar: mudar,
            min: p.min,
            max: p.max,
            unidade: p.unit,
            casas: p.decimals ?? (p.max - p.min > 20 ? 1 : 2),
            sensibilidade: p.dragStep,
            keyframe: kf.estado,
            aoAnterior: kf.anterior,
            aoProximo: kf.proximo,
            aoResetar: () => mudar(p.initial),
            aoComecarGesto: c.beginGesture,
            aoTerminarGesto: c.endGesture,
          );
      }
    }

    // A REGRA DOS GRUPOS DA FICHA: chave fora de todo grupo vem solta,
    // ANTES dos grupos — parametro escondido por esquecimento e pior que
    // parametro fora de lugar.
    final agrupadas = {for (final g in spec.grupos) ...g.chaves};
    final linhas = <Widget>[
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
          rotulo: 'Cor',
          chave: '${efeito.id}-cor',
          cor: efeito.color,
          aoTocar: () async {
            final nova = await showColorPicker(
              context,
              initial: efeito.color,
              onChanged: (cor) => c.setEffectColor(layerId, efeito.id, cor),
            );
            if (nova != null) c.setEffectColor(layerId, efeito.id, nova);
          },
        ),
    ];
    if (linhas.isEmpty) {
      return const [
        AureaAvisoDoPainel(texto: 'Este efeito não tem ajustes.'),
      ];
    }
    return linhas;
  }
}
