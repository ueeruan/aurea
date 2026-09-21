import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/animador_de_texto.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/layer_meta.dart';
import '../../../domain/shape.dart';
import '../../../domain/text_animator.dart';
import '../../../domain/video_project.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';

// ===========================================================================
// ESTILO DO TEXTO
// ===========================================================================
//
// Cinco abas curtas, cada uma com poucas linhas: Texto (negrito e
// espacamento entre letras), Contorno, Sombra (e brilho), Fundo (a caixa
// atras do texto) e Cor (cor e degrade). Nenhuma folha modal: tudo mora no
// painel, com o losango onde a propriedade anima.
//
// DE ONDE VEM CADA COISA (nada aqui inventa motor):
//
//  * contorno, sombra, brilho e degrade sao o ACABAMENTO da camada
//    (`LayerStyles`), o mesmo da folha de borda e sombra, com trilhas
//    animaveis;
//  * o espacamento e um animador de texto de cobertura total
//    ([nomeDoEspacamento]) — o motor ja soma o `tracking` por unidade, e e
//    ele que sabe animar isso;
//  * a caixa e uma forma que ABRACA o texto (`ContainerSpec`), camada
//    propria logo abaixo dele — o motor ja a redimensiona quando o texto
//    muda.

/// O nome do animador que guarda o espacamento entre letras. E dado do
/// projeto (aparece na pilha do Animador de Texto com este nome).
const nomeDoEspacamento = 'Espaçamento';

/// O animador de espacamento desta camada, quando ja existe.
TextAnimator? animadorDoEspacamento(TextLayer l) {
  for (final a in l.animators) {
    if (a.name == nomeDoEspacamento) return a;
  }
  return null;
}

/// A receita do espacamento: janela cheia, degrau seco, parada no tempo —
/// toda letra coberta por inteiro o tempo todo.
PresetDoAnimador presetDoEspacamento(double valor) => PresetDoAnimador(
  id: 'espacamento',
  nome: nomeDoEspacamento,
  receita: ReceitaDoAnimador(
    forma: SelectorShape.square,
    suavidade: 0,
    varredura: Duration.zero,
    espacamento: valor,
  ),
);

/// GARANTE o animador de espacamento e devolve o id dele. Criar sao duas
/// escritas (acrescentar e dar o nome) num passo so de desfazer.
String? garantirEspacamento(WidgetRef ref, String layerId, double valor) {
  final camada = ref.read(editorControllerProvider).layerById(layerId);
  if (camada is! TextLayer) return null;
  final existente = animadorDoEspacamento(camada);
  if (existente != null) return existente.id;
  final c = ref.read(editorControllerProvider.notifier);
  final preset = presetDoEspacamento(valor);
  String? novo;
  c.runAsOneUndo(() {
    novo = c.addTextAnimator(layerId, receita: preset.receita);
    final id = novo;
    if (id != null) c.aplicarPresetNoAnimador(layerId, id, preset);
  });
  return novo;
}

/// A CAIXA que abraca o texto [textoId], quando ha (a forma com o
/// `ContainerSpec` apontando para ele).
String? caixaDoTexto(VideoProject p, String textoId) {
  for (final e in p.meta.entries) {
    if (e.value.container?.targetLayerId == textoId &&
        p.layerById(e.key) is ShapeLayer) {
      return e.key;
    }
  }
  return null;
}

class PainelEstilo extends ConsumerStatefulWidget {
  const PainelEstilo({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelEstilo> createState() => _PainelEstiloState();
}

class _PainelEstiloState extends ConsumerState<PainelEstilo> {
  static const _titulo = 'Estilo';
  static const _abas = ['Texto', 'Contorno', 'Sombra', 'Fundo', 'Cor'];

  int _aba = 0;

  String get _id => widget.layerId;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, _id);
    final gravada = camadaGravada(ref, _id);
    final chave = 'painel-${PainelId.estilo.name}';
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
    if (visivel is! TextLayer || gravada is! TextLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é de texto.',
      );
    }
    final vistos = estilosVisiveis(ref, _id);
    final gravados = estilosGravados(ref, _id);
    final caixaId = ref.watch(
      projetoVisivelProvider.select((p) => caixaDoTexto(p, _id)),
    );
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) {
          final linhas = switch (_aba) {
            0 => _texto(visivel, gravada, t, escopo.playback),
            1 => _contorno(gravada, vistos, gravados, t, escopo.playback),
            2 => _sombra(gravada, vistos, gravados, t, escopo.playback),
            3 => _fundo(visivel, caixaId, t, escopo.playback),
            _ => _abaCor(
              visivel,
              gravada,
              vistos,
              gravados,
              t,
              escopo.playback,
            ),
          };
          return ListView(
            key: ValueKey('estilo-aba-$_aba'),
            padding: const EdgeInsets.fromLTRB(
              AureaDims.margemDoPainel,
              AureaDims.e4,
              AureaDims.margemDoPainel,
              AureaDims.topoDoPainel,
            ),
            children: linhas,
          );
        },
      ),
    );
  }

  // ------------------------------------------------------------- texto

  List<Widget> _texto(
    TextLayer visivel,
    TextLayer gravada,
    Duration t,
    PlaybackController playback,
  ) {
    final local = visivel.localTime(t);
    final vistoAnim = animadorDoEspacamento(visivel);
    final gravadoAnim = animadorDoEspacamento(gravada);
    final valor = vistoAnim == null
        ? 0.0
        : valorDaPropriedade(vistoAnim, TextAnimProp.tracking, local);
    final trilha = gravadoAnim == null
        ? null
        : propriedadeDoAnimador(gravadoAnim, TextAnimProp.tracking)?.value;
    final (min, max) = faixaDaPropriedade(TextAnimProp.tracking);
    return [
      linhaDeLigar(
        rotulo: 'Negrito',
        chave: 'negrito',
        valor: visivel.bold,
        aoMudar: (v) => _c.editTextLayer(_id, bold: v),
      ),
      linhaNumerica(
        ref,
        rotulo: 'Espaçamento',
        chave: 'espacamento',
        valor: valor,
        min: min,
        max: max,
        casas: 1,
        losango: losangoDaTrilha(
          trilha: trilha,
          gravada: gravada,
          t: t,
          playback: playback,
          aoAlternar: () => _c.runAsOneUndo(() {
            final aid = garantirEspacamento(ref, _id, 0);
            if (aid != null) {
              _c.toggleAnimadorPropKeyframe(_id, aid, TextAnimProp.tracking, t);
            }
          }),
        ),
        aoMudar: (v) {
          final existente = animadorDoEspacamento(
            ref.read(editorControllerProvider).layerById(_id)! as TextLayer,
          );
          if (existente == null) {
            garantirEspacamento(ref, _id, v);
            return;
          }
          _c.editAnimadorProp(_id, existente.id, TextAnimProp.tracking, t, v);
        },
        // ZERAR DEVOLVE O TEXTO AO CAMINHO RAPIDO: sem animador, o palco
        // desenha o paragrafo inteiro de uma vez em vez de letra a letra.
        aoResetar: () {
          final a = animadorDoEspacamento(gravada);
          if (a != null) _c.removeTextAnimator(_id, a.id);
        },
      ),
    ];
  }

  // ---------------------------------------------------------- acabamento

  /// Uma linha numerica de trilha do acabamento, com losango.
  Widget _linhaDoEstilo({
    required String rotulo,
    required String chave,
    required Layer gravada,
    required LayerStyles vistos,
    required LayerStyles gravados,
    required LerTrilha ler,
    required GravarTrilha gravar,
    required Duration t,
    required PlaybackController playback,
    required double min,
    required double max,
    double escala = 1,
    String unidade = '',
    int casas = 0,
  }) {
    final local = gravada.localTime(t);
    final valor = (ler(vistos)?.valueAt(local) ?? 0) * escala;
    return linhaNumerica(
      ref,
      rotulo: rotulo,
      chave: chave,
      valor: valor,
      min: min,
      max: max,
      casas: casas,
      unidade: unidade,
      losango: losangoDaTrilha(
        trilha: ler(gravados),
        gravada: gravada,
        t: t,
        playback: playback,
        aoAlternar: () => alternarMarcaDoEstilo(
          ref,
          layerId: _id,
          t: t,
          ler: ler,
          gravar: gravar,
        ),
      ),
      aoMudar: (v) => editarTrilhaDoEstilo(
        ref,
        layerId: _id,
        t: t,
        ler: ler,
        gravar: gravar,
        valor: v / escala,
      ),
    );
  }

  Future<void> _escolherCor(Color atual, void Function(Color) aplicar) async {
    EscopoDoEditor.of(context).playback.pause();
    final nova = await showColorPicker(
      context,
      initial: atual,
      onChanged: aplicar,
    );
    if (nova != null) aplicar(nova);
  }

  void _estilos(LayerStyles Function(LayerStyles) f) =>
      _c.updateLayerStyles(_id, f);

  List<Widget> _contorno(
    Layer gravada,
    LayerStyles vistos,
    LayerStyles gravados,
    Duration t,
    PlaybackController playback,
  ) {
    final borda = vistos.stroke;
    return [
      linhaDeLigar(
        rotulo: 'Contorno',
        chave: 'contorno',
        valor: borda?.enabled ?? false,
        // DESLIGAR TIRA AS BORDAS: borda desligada que fica no projeto
        // ainda tira a camada do caminho rapido do palco.
        aoMudar: (v) => _estilos(
          (s) => v
              ? comBordas(s, [
                  (s.stroke ??
                          StrokeStyle(
                            color: const Color(0xFF000000),
                            width: AnimatedDouble(6),
                          ))
                      .copyWith(enabled: true),
                  ...s.bordasExtras,
                ])
              : comBordas(s, const []),
        ),
      ),
      if (borda != null && borda.enabled) ...[
        AureaPropertyRow.cor(
          rotulo: 'Cor',
          chave: 'contorno-cor',
          cor: borda.color,
          aoTocar: () => _escolherCor(
            borda.color,
            (cor) => _estilos(
              (s) => s.stroke == null
                  ? s
                  : s.copyWith(stroke: s.stroke!.copyWith(color: cor)),
            ),
          ),
        ),
        _linhaDoEstilo(
          rotulo: 'Largura',
          chave: 'contorno-largura',
          gravada: gravada,
          vistos: vistos,
          gravados: gravados,
          ler: (s) => s.stroke?.width,
          gravar: (s, v) => s.copyWith(stroke: s.stroke!.copyWith(width: v)),
          t: t,
          playback: playback,
          min: 0,
          max: 60,
          casas: 1,
        ),
        _linhaDoEstilo(
          rotulo: 'Opacidade',
          chave: 'contorno-opacidade',
          gravada: gravada,
          vistos: vistos,
          gravados: gravados,
          ler: (s) => s.stroke?.opacity,
          gravar: (s, v) => s.copyWith(stroke: s.stroke!.copyWith(opacity: v)),
          t: t,
          playback: playback,
          min: 0,
          max: 100,
          escala: 100,
          unidade: '%',
        ),
        AureaPropertyRow.personalizada(
          rotulo: 'Posição',
          chave: 'contorno-posicao',
          filho: FileiraDePilulas<PosicaoDaBorda>(
            chave: 'contorno-posicao',
            chaveDe: (p) => p.name,
            opcoes: PosicaoDaBorda.values,
            atual: borda.posicao,
            rotuloDe: (p) => switch (p) {
              PosicaoDaBorda.fora => 'Fora',
              PosicaoDaBorda.dentro => 'Dentro',
              PosicaoDaBorda.centro => 'Centro',
            },
            aoEscolher: (p) => _estilos(
              (s) => s.stroke == null
                  ? s
                  : s.copyWith(stroke: s.stroke!.copyWith(posicao: p)),
            ),
          ),
        ),
      ],
    ];
  }

  List<Widget> _sombra(
    Layer gravada,
    LayerStyles vistos,
    LayerStyles gravados,
    Duration t,
    PlaybackController playback,
  ) {
    final sombra = vistos.dropShadow;
    final brilho = vistos.outerGlow;
    Widget linha(
      String rotulo,
      String chave,
      LerTrilha ler,
      GravarTrilha gravar, {
      required double min,
      required double max,
      double escala = 1,
      String unidade = '',
    }) => _linhaDoEstilo(
      rotulo: rotulo,
      chave: chave,
      gravada: gravada,
      vistos: vistos,
      gravados: gravados,
      ler: ler,
      gravar: gravar,
      t: t,
      playback: playback,
      min: min,
      max: max,
      escala: escala,
      unidade: unidade,
    );
    return [
      linhaDeLigar(
        rotulo: 'Sombra',
        chave: 'sombra',
        valor: sombra?.enabled ?? false,
        aoMudar: (v) => _estilos(
          (s) => v
              ? s.copyWith(
                  dropShadow: (s.dropShadow ?? ShadowStyle()).copyWith(
                    enabled: true,
                  ),
                )
              : s.copyWith(clearDropShadow: true),
        ),
      ),
      if (sombra != null && sombra.enabled) ...[
        AureaPropertyRow.cor(
          rotulo: 'Cor',
          chave: 'sombra-cor',
          cor: sombra.color,
          aoTocar: () => _escolherCor(
            sombra.color,
            (cor) => _estilos(
              (s) => s.dropShadow == null
                  ? s
                  : s.copyWith(dropShadow: s.dropShadow!.copyWith(color: cor)),
            ),
          ),
        ),
        linha(
          'Opacidade',
          'sombra-opacidade',
          (s) => s.dropShadow?.opacity,
          (s, v) => s.copyWith(dropShadow: s.dropShadow!.copyWith(opacity: v)),
          min: 0,
          max: 100,
          escala: 100,
          unidade: '%',
        ),
        linha(
          'Ângulo',
          'sombra-angulo',
          (s) => s.dropShadow?.angleDeg,
          (s, v) => s.copyWith(dropShadow: s.dropShadow!.copyWith(angleDeg: v)),
          min: -360,
          max: 360,
          unidade: '°',
        ),
        linha(
          'Distância',
          'sombra-distancia',
          (s) => s.dropShadow?.distance,
          (s, v) => s.copyWith(dropShadow: s.dropShadow!.copyWith(distance: v)),
          min: 0,
          max: 300,
        ),
        linha(
          'Desfoque',
          'sombra-desfoque',
          (s) => s.dropShadow?.size,
          (s, v) => s.copyWith(dropShadow: s.dropShadow!.copyWith(size: v)),
          min: 0,
          max: 120,
        ),
      ],
      linhaDeLigar(
        rotulo: 'Brilho',
        chave: 'brilho',
        valor: brilho?.enabled ?? false,
        aoMudar: (v) => _estilos(
          (s) => v
              ? s.copyWith(
                  outerGlow: (s.outerGlow ?? GlowStyle()).copyWith(
                    enabled: true,
                  ),
                )
              : s.copyWith(clearOuterGlow: true),
        ),
      ),
      if (brilho != null && brilho.enabled) ...[
        AureaPropertyRow.cor(
          rotulo: 'Cor',
          chave: 'brilho-cor',
          cor: brilho.color,
          aoTocar: () => _escolherCor(
            brilho.color,
            (cor) => _estilos(
              (s) => s.outerGlow == null
                  ? s
                  : s.copyWith(outerGlow: s.outerGlow!.copyWith(color: cor)),
            ),
          ),
        ),
        linha(
          'Tamanho',
          'brilho-tamanho',
          (s) => s.outerGlow?.size,
          (s, v) => s.copyWith(outerGlow: s.outerGlow!.copyWith(size: v)),
          min: 0,
          max: 120,
        ),
        linha(
          'Opacidade',
          'brilho-opacidade',
          (s) => s.outerGlow?.opacity,
          (s, v) => s.copyWith(outerGlow: s.outerGlow!.copyWith(opacity: v)),
          min: 0,
          max: 100,
          escala: 100,
          unidade: '%',
        ),
      ],
    ];
  }

  // ------------------------------------------------------------- fundo

  /// LIGAR A CAIXA: uma forma retangular nasce com o tempo do texto,
  /// abraca-o (`ContainerSpec`) e desce para logo abaixo dele. A selecao
  /// volta ao texto no mesmo quadro — o painel nao pisca.
  void _ligarCaixa(TextLayer texto) {
    final c = _c;
    final sel = ref.read(selectedLayerProvider.notifier);
    // CAMADA NOVA E SELECIONADA pelo controlador, e o editor fecha o painel
    // quando a selecao muda. A selecao volta ao texto e o painel reabre no
    // mesmo quadro: quem ligou a caixa continua na aba Fundo.
    final aberto = ref.read(painelAbertoProvider);
    c.runAsOneUndo(() {
      c.addShapeLayer(
        texto.startTime,
        name: 'Caixa',
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.rect,
            roundnessPercent: false,
            roundness: AnimatedDouble(16),
          ),
          ShapeFill(color: const Color(0xFF000000), opacity: 1),
        ],
      );
      final caixa = ref.read(selectedLayerProvider);
      if (caixa == null || caixa == texto.id) return;
      c.trimLayerEnd(caixa, texto.endTime);
      c.setContainer(caixa, ContainerSpec(targetLayerId: texto.id));
      final camadas = ref.read(editorControllerProvider).layers;
      final daCaixa = camadas.indexWhere((l) => l.id == caixa);
      final doTexto = camadas.indexWhere((l) => l.id == texto.id);
      // O indice 0 e o topo: a caixa vai para a casa logo depois do texto.
      if (daCaixa >= 0 && doTexto > daCaixa) {
        c.reorderLayer(caixa, doTexto - daCaixa);
      }
    });
    sel.state = texto.id;
    if (aberto != null) ref.read(painelAbertoProvider.notifier).state = aberto;
  }

  List<Widget> _fundo(
    TextLayer texto,
    String? caixaId,
    Duration t,
    PlaybackController playback,
  ) {
    final ligada = linhaDeLigar(
      rotulo: 'Caixa atrás',
      chave: 'fundo',
      valor: caixaId != null,
      aoMudar: (v) {
        if (v) {
          _ligarCaixa(texto);
        } else if (caixaId != null) {
          _c.removeLayer(caixaId);
        }
      },
    );
    if (caixaId == null) return [ligada];
    return [ligada, _CaixaDoTexto(caixaId: caixaId, t: t, playback: playback)];
  }

  // --------------------------------------------------------------- cor

  List<Widget> _abaCor(
    TextLayer visivel,
    Layer gravada,
    LayerStyles vistos,
    LayerStyles gravados,
    Duration t,
    PlaybackController playback,
  ) {
    final degrade = vistos.gradientOverlay;
    Widget linha(
      String rotulo,
      String chave,
      LerTrilha ler,
      GravarTrilha gravar, {
      required double min,
      required double max,
      double escala = 1,
      String unidade = '',
    }) => _linhaDoEstilo(
      rotulo: rotulo,
      chave: chave,
      gravada: gravada,
      vistos: vistos,
      gravados: gravados,
      ler: ler,
      gravar: gravar,
      t: t,
      playback: playback,
      min: min,
      max: max,
      escala: escala,
      unidade: unidade,
    );
    return [
      AureaPropertyRow.cor(
        rotulo: 'Cor do texto',
        chave: 'cor-texto',
        cor: visivel.color,
        aoTocar: () => _escolherCor(
          visivel.color,
          (cor) => _c.editTextLayer(_id, color: cor),
        ),
      ),
      linhaDeLigar(
        rotulo: 'Degradê',
        chave: 'degrade',
        valor: degrade?.enabled ?? false,
        aoMudar: (v) => _estilos(
          (s) => v
              ? s.copyWith(
                  gradientOverlay:
                      (s.gradientOverlay ??
                              GradientOverlayStyle(
                                colorA: visivel.color,
                                colorB: const Color(0xFF8FD3FF),
                              ))
                          .copyWith(enabled: true),
                )
              : s.copyWith(clearGradientOverlay: true),
        ),
      ),
      if (degrade != null && degrade.enabled) ...[
        AureaPropertyRow.cor(
          rotulo: 'Cor 1',
          chave: 'degrade-cor-1',
          cor: degrade.colorA,
          aoTocar: () => _escolherCor(
            degrade.colorA,
            (cor) => _estilos(
              (s) => s.gradientOverlay == null
                  ? s
                  : s.copyWith(
                      gradientOverlay: s.gradientOverlay!.copyWith(colorA: cor),
                    ),
            ),
          ),
        ),
        AureaPropertyRow.cor(
          rotulo: 'Cor 2',
          chave: 'degrade-cor-2',
          cor: degrade.colorB,
          aoTocar: () => _escolherCor(
            degrade.colorB,
            (cor) => _estilos(
              (s) => s.gradientOverlay == null
                  ? s
                  : s.copyWith(
                      gradientOverlay: s.gradientOverlay!.copyWith(colorB: cor),
                    ),
            ),
          ),
        ),
        linha(
          'Ângulo',
          'degrade-angulo',
          (s) => s.gradientOverlay?.angleDeg,
          (s, v) => s.copyWith(
            gradientOverlay: s.gradientOverlay!.copyWith(angleDeg: v),
          ),
          min: -360,
          max: 360,
          unidade: '°',
        ),
        linha(
          'Opacidade',
          'degrade-opacidade',
          (s) => s.gradientOverlay?.opacity,
          (s, v) => s.copyWith(
            gradientOverlay: s.gradientOverlay!.copyWith(opacity: v),
          ),
          min: 0,
          max: 100,
          escala: 100,
          unidade: '%',
        ),
      ],
    ];
  }
}

/// OS AJUSTES DA CAIXA atras do texto: cor, opacidade (losango da propria
/// camada da caixa), cantos (losango da forma) e margens (a folga do
/// `ContainerSpec`, que o motor reaplica sozinho quando o texto muda).
class _CaixaDoTexto extends ConsumerWidget {
  const _CaixaDoTexto({
    required this.caixaId,
    required this.t,
    required this.playback,
  });

  final String caixaId;
  final Duration t;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final visivel = camadaVisivel(ref, caixaId);
    final gravada = camadaGravada(ref, caixaId);
    final spec = ref.watch(
      projetoVisivelProvider.select((p) => p.metaOf(caixaId).container),
    );
    if (visivel is! ShapeLayer || gravada is! ShapeLayer || spec == null) {
      return const SizedBox.shrink();
    }
    final c = ref.read(editorControllerProvider.notifier);
    final local = visivel.localTime(t);
    ShapeParametric? forma(ShapeLayer l) =>
        l.contents.whereType<ShapeParametric>().firstOrNull;
    final preenchimento = visivel.contents.whereType<ShapeFill>().firstOrNull;
    final cantos = forma(visivel)?.roundness;
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        if (preenchimento != null)
          AureaPropertyRow.cor(
            rotulo: 'Cor da caixa',
            chave: 'fundo-cor',
            cor: preenchimento.color,
            aoTocar: () async {
              playback.pause();
              final nova = await showColorPicker(
                context,
                initial: preenchimento.color,
                onChanged: (cor) => c.setShapePrimaryColor(caixaId, cor),
              );
              if (nova != null) c.setShapePrimaryColor(caixaId, nova);
            },
          ),
        linhaNumerica(
          ref,
          rotulo: 'Opacidade',
          chave: 'fundo-opacidade',
          valor: visivel.opacity.valueAt(local) * 100,
          min: 0,
          max: 100,
          unidade: '%',
          losango: losangoDaPropriedade(
            ref,
            gravada: gravada,
            prop: LayerProp.opacity,
            t: t,
            playback: playback,
            contexto: context,
          ),
          aoMudar: (v) => c.editOpacity(caixaId, t, v / 100),
        ),
        if (cantos != null)
          linhaNumerica(
            ref,
            rotulo: 'Cantos',
            chave: 'fundo-cantos',
            valor: cantos.valueAt(local),
            min: 0,
            max: 200,
            losango: losangoDaTrilha(
              trilha: forma(gravada)?.roundness,
              gravada: gravada,
              t: t,
              playback: playback,
              aoAlternar: () =>
                  c.toggleShapeParamKeyframe(caixaId, 'roundness', t),
            ),
            aoMudar: (v) => c.editShapeParam(caixaId, 'roundness', t, v),
          ),
        linhaNumerica(
          ref,
          rotulo: 'Margem X',
          chave: 'fundo-margem-x',
          valor: spec.padLeft,
          min: 0,
          max: 400,
          aoMudar: (v) =>
              c.setContainer(caixaId, spec.copyWith(padLeft: v, padRight: v)),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Margem Y',
          chave: 'fundo-margem-y',
          valor: spec.padTop,
          min: 0,
          max: 400,
          aoMudar: (v) =>
              c.setContainer(caixaId, spec.copyWith(padTop: v, padBottom: v)),
        ),
      ],
    );
  }
}
