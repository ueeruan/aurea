import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/mask.dart';
import '../../am/layer_menu.dart' show ModoDeMescla, categoriasDeMescla;
import '../shell/contrato.dart';
import 'comum.dart';
import 'pecas_centrais.dart';
import 'pontos.dart' show abrirEditarPontosDaMascara;

/// TODOS os modos de mistura que o motor sabe fazer, na ordem das
/// categorias (os nativos e os proprios da Aurea lado a lado).
final List<ModoDeMescla> modosDeMistura = [
  for (final cat in categoriasDeMescla) ...cat.modos,
];

/// O modo de mistura em vigor numa camada.
ModoDeMescla modoDeMisturaDe(Layer l) {
  final proprio = l.customBlend;
  for (final m in modosDeMistura) {
    if (proprio != null ? m.aurea == proprio : m.nativo == l.blendMode) {
      return m;
    }
  }
  return modosDeMistura.first;
}

/// Grava [m] como modo de mistura de [id].
void gravarModoDeMistura(EditorController c, String id, ModoDeMescla m) {
  final proprio = m.aurea;
  final nativo = m.nativo;
  if (proprio != null) {
    c.setCustomBlend(id, proprio);
  } else if (nativo != null) {
    // Voltar a um modo nativo solta o modo proprio: senao ele continuaria
    // mandando por cima.
    c.setCustomBlend(id, null);
    c.setBlendMode(id, nativo);
  }
}

String _rotuloDoModoDaMascara(MaskMode m) => switch (m) {
  MaskMode.none => 'Nenhum',
  MaskMode.add => 'Somar',
  MaskMode.subtract => 'Subtrair',
  MaskMode.intersect => 'Interseção',
  MaskMode.lighten => 'Clarear',
  MaskMode.darken => 'Escurecer',
  MaskMode.difference => 'Diferença',
};

String _rotuloDoRecorte(MatteMode m) => switch (m) {
  MatteMode.none => 'Nenhum',
  MatteMode.alpha => 'Alfa da camada de cima',
  MatteMode.alphaInvert => 'Alfa invertido',
  MatteMode.luma => 'Luminância da de cima',
  MatteMode.lumaInvert => 'Luminância invertida',
  MatteMode.recorte => 'Só onde a de baixo tem pixel',
};

/// MASCARA — o "Blending & Opacity" da referencia com as mascaras da
/// camada:
///
///   Opacidade (losango) · Mistura (todos os modos) · Recorte (matte)
///   um cartao por mascara: modo, inverter, forma (editar pontos, com o
///   losango do caminho), suavizar, expansao e opacidade — cada numero
///   com o seu losango
///   + Retangulo · Circulo · Estrela · Coracao
class PainelMascara extends ConsumerStatefulWidget {
  const PainelMascara({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelMascara> createState() => _PainelMascaraState();
}

class _PainelMascaraState extends ConsumerState<PainelMascara>
    with PropriedadeAtivaDoPainel {
  static const _titulo = 'Mistura e máscara';

  /// Os cartoes de mascara abertos (a recem-criada abre sozinha).
  final Set<String> _abertas = {};
  Set<String>? _conhecidas;

  @override
  void initState() {
    super.initState();
    // A linha de cima e a opacidade: e a propriedade cujas marcas a
    // timeline acende enquanto o painel esta aberto.
    ativarPropriedade(const PropriedadeAtiva.transformacao(LayerProp.opacity));
  }

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, widget.layerId);
    final gravada = camadaGravada(ref, widget.layerId);
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
    final ids = {for (final m in visivel.masks) m.id};
    final conhecidas = _conhecidas;
    if (conhecidas != null) _abertas.addAll(ids.difference(conhecidas));
    _abertas.retainAll(ids);
    _conhecidas = ids;

    final c = ref.read(editorControllerProvider.notifier);
    final id = widget.layerId;
    return AureaPanel(
      titulo: _titulo,
      chave: 'painel-${PainelId.mascara.name}',
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) {
          final kf = losangoDaPropriedade(
            ref,
            gravada: gravada,
            prop: LayerProp.opacity,
            t: t,
            playback: escopo.playback,
          );
          return ListView(
            padding: respiroDoPainel,
            children: [
              AureaPropertyRow(
                rotulo: 'Opacidade',
                valor: visivel.opacity.valueAt(visivel.localTime(t)) * 100,
                aoMudar: aCadaPasso(
                  (v) =>
                      c.editOpacity(id, escopo.playback.time.value, v / 100),
                ),
                min: 0,
                max: 100,
                unidade: '%',
                casas: 0,
                keyframe: kf.estado,
                aoAnterior: kf.anterior,
                aoProximo: kf.proximo,
                aoResetar: () =>
                    umPasso(ref, () => c.resetProp(id, LayerProp.opacity)),
                aoComecarGesto: c.beginGesture,
                aoTerminarGesto: c.endGesture,
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Mistura',
                chave: 'mistura',
                filho: AureaDropdown<ModoDeMescla>(
                  valor: modoDeMisturaDe(visivel),
                  opcoes: modosDeMistura,
                  rotuloDe: (m) => m.rotulo,
                  titulo: 'Modo de mistura',
                  aoMudar: (m) =>
                      umPasso(ref, () => gravarModoDeMistura(c, id, m)),
                ),
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Recorte',
                chave: 'recorte',
                filho: AureaDropdown<MatteMode>(
                  valor: visivel.matteMode,
                  opcoes: MatteMode.values,
                  rotuloDe: _rotuloDoRecorte,
                  titulo: 'Recortar por outra camada',
                  aoMudar: (m) {
                    var ok = true;
                    umPasso(ref, () {
                      if (m == MatteMode.none) {
                        c.setMatte(id, MatteMode.none, null);
                      } else if (m == MatteMode.recorte) {
                        ok = c.recortarPelaDeBaixo(id);
                      } else {
                        ok = c.setMatteFromAbove(id, m);
                      }
                    });
                    if (!ok) {
                      AureaSnack.show(
                        context,
                        translate(
                          context,
                          m == MatteMode.recorte
                              ? 'Não há camada com imagem logo abaixo.'
                              : 'Não há camada com imagem logo acima.',
                        ),
                      );
                    }
                  },
                ),
              ),
              AureaSection(
                titulo: 'Máscaras',
                chave: 'mascaras',
                recolhivel: false,
                filhos: [
                  for (final (i, m) in visivel.masks.indexed)
                    _CartaoDaMascara(
                      key: ValueKey('cartao-mascara-${m.id}'),
                      layerId: id,
                      mascara: m,
                      gravada: gravada.masks
                              .where((g) => g.id == m.id)
                              .firstOrNull ??
                          m,
                      camada: gravada,
                      t: t,
                      indice: i,
                      total: visivel.masks.length,
                      aberta: _abertas.contains(m.id),
                      aoMudarAberta: (v) {
                        setState(() {
                          if (v) {
                            _abertas.add(m.id);
                          } else {
                            _abertas.remove(m.id);
                          }
                        });
                        // A MASCARA ABERTA e a propriedade ativa; fechada,
                        // volta a opacidade da linha de cima.
                        ativarPropriedade(
                          v
                              ? PropriedadeAtiva.mascara(m.id)
                              : const PropriedadeAtiva.transformacao(
                                  LayerProp.opacity,
                                ),
                        );
                      },
                    ),
                  FileiraDeAcoes(
                    acoes: [
                      for (final (chave, nome, forma) in [
                        ('retangulo', 'Retângulo', BezierPath.rect(460, 460)),
                        ('circulo', 'Círculo', BezierPath.ellipse(480, 480)),
                        ('estrela', 'Estrela', BezierPath.star(5, 250, 125)),
                        ('coracao', 'Coração', BezierPath.heart(440, 420)),
                      ])
                        AureaChip(
                          key: ValueKey('mascara-adicionar-$chave'),
                          rotulo: nome,
                          icone: CupertinoIcons.plus,
                          aoTocar: () => umPasso(
                            ref,
                            () => c.addMask(
                              id,
                              LayerMask(
                                name: translate(context, nome),
                                path: AnimatedPath(forma),
                                feather: AnimatedDouble(0),
                              ),
                            ),
                          ),
                        ),
                    ],
                  ),
                ],
              ),
            ],
          );
        },
      ),
    );
  }
}

class _CartaoDaMascara extends ConsumerWidget {
  const _CartaoDaMascara({
    super.key,
    required this.layerId,
    required this.mascara,
    required this.gravada,
    required this.camada,
    required this.t,
    required this.indice,
    required this.total,
    required this.aberta,
    required this.aoMudarAberta,
  });

  final String layerId;

  /// A mascara como se VE (numeros) e como esta GRAVADA (losangos).
  final LayerMask mascara;
  final LayerMask gravada;
  final Layer camada;
  final Duration t;
  final int indice;
  final int total;
  final bool aberta;
  final ValueChanged<bool> aoMudarAberta;

  Future<void> _menu(BuildContext botao, WidgetRef ref) async {
    final c = ref.read(editorControllerProvider.notifier);
    final escolha = await mostrarAureaMenu<String>(
      botao,
      itens: [
        if (indice > 0)
          const AureaMenuItem(
            valor: 'subir',
            rotulo: 'Subir',
            icone: CupertinoIcons.arrow_up,
          ),
        if (indice < total - 1)
          const AureaMenuItem(
            valor: 'descer',
            rotulo: 'Descer',
            icone: CupertinoIcons.arrow_down,
          ),
        const AureaMenuItem(
          valor: 'pontos',
          rotulo: 'Editar pontos',
          icone: CupertinoIcons.pencil_outline,
        ),
        const AureaMenuItem(
          valor: 'apagar',
          rotulo: 'Apagar',
          icone: CupertinoIcons.trash,
          destrutivo: true,
          chave: 'mascara-apagar',
        ),
      ],
    );
    if (!botao.mounted) return;
    switch (escolha) {
      case 'subir':
        umPasso(ref, () => c.reorderMask(layerId, mascara.id, -1));
      case 'descer':
        umPasso(ref, () => c.reorderMask(layerId, mascara.id, 1));
      case 'pontos':
        abrirEditarPontosDaMascara(
          botao,
          ref,
          EscopoDoEditor.of(botao).playback,
          mascara.id,
        );
      case 'apagar':
        umPasso(ref, () => c.removeMask(layerId, mascara.id));
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final playback = EscopoDoEditor.of(context).playback;
    final local = camada.localTime(t);
    final m = mascara;

    ({KeyframeState estado, VoidCallback? anterior, VoidCallback? proximo})
    losango(Iterable<int> marcas, VoidCallback alternar) => losangoNoInstante(
      marcasUs: marcas,
      agoraUs: local.inMicroseconds,
      inicio: camada.startTime,
      playback: playback,
      aoAlternar: alternar,
    );

    // UM NUMERO DA MASCARA, com o losango da trilha dele.
    AureaPropertyRow numero(
      String rotulo,
      String param,
      AnimatedDouble visivel,
      AnimatedDouble? registrada, {
      required double min,
      required double max,
      double escala = 1,
      String unidade = 'px',
    }) {
      final kf = losango(
        marcasDe(registrada),
        () => c.toggleMaskParamKeyframe(
          layerId,
          m.id,
          param,
          playback.time.value,
        ),
      );
      return AureaPropertyRow(
        rotulo: rotulo,
        chave: 'mascara-${m.id}-$param',
        valor: (visivel.valueAt(local) * escala).clamp(min, max).toDouble(),
        min: min,
        max: max,
        casas: 0,
        unidade: unidade,
        keyframe: kf.estado,
        aoAnterior: kf.anterior,
        aoProximo: kf.proximo,
        aoComecarGesto: c.beginGesture,
        aoTerminarGesto: c.endGesture,
        aoMudar: aCadaPasso(
          (v) => c.editMaskParam(
            layerId,
            m.id,
            param,
            playback.time.value,
            v / escala,
          ),
        ),
      );
    }

    final kfDoCaminho = losango(
      [for (final k in gravada.path.keyframes) k.time.inMicroseconds],
      () => c.toggleMaskPathKeyframe(layerId, m.id, playback.time.value),
    );
    final ligada = m.mode != MaskMode.none;
    return AureaEffectCard(
      chave: 'mascara-${m.id}',
      nome: m.name,
      traduzirNome: false,
      ligado: ligada,
      aberto: aberta,
      aoMudarAberto: aoMudarAberta,
      aoAlternarLigado: () => umPasso(
        ref,
        () => c.updateMask(
          layerId,
          m.id,
          (x) => x.copyWith(mode: ligada ? MaskMode.none : MaskMode.add),
        ),
      ),
      aoMenu: (botao) => _menu(botao, ref),
      filhos: !aberta
          ? const []
          : [
              AureaPropertyRow.personalizada(
                rotulo: 'Modo',
                chave: 'mascara-${m.id}-modo',
                filho: AureaDropdown<MaskMode>(
                  valor: m.mode,
                  opcoes: MaskMode.values,
                  rotuloDe: _rotuloDoModoDaMascara,
                  titulo: 'Modo da máscara',
                  aoMudar: (novo) => umPasso(
                    ref,
                    () => c.updateMask(
                      layerId,
                      m.id,
                      (x) => x.copyWith(mode: novo),
                    ),
                  ),
                ),
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Inverter',
                chave: 'mascara-${m.id}-inverter',
                filho: AureaToggle(
                  valor: m.inverted,
                  aoMudar: (_) => umPasso(
                    ref,
                    () => c.toggleMaskInverted(layerId, m.id),
                  ),
                ),
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Forma',
                chave: 'mascara-${m.id}-forma',
                keyframe: kfDoCaminho.estado,
                aoAnterior: kfDoCaminho.anterior,
                aoProximo: kfDoCaminho.proximo,
                filho: AureaChip(
                  key: ValueKey('mascara-${m.id}-pontos'),
                  rotulo: 'Editar pontos',
                  icone: CupertinoIcons.pencil_outline,
                  aoTocar: () => abrirEditarPontosDaMascara(
                    context,
                    ref,
                    playback,
                    m.id,
                  ),
                ),
              ),
              if (!m.path.valueAt(local).closed)
                const AureaAvisoDoPainel(
                  texto:
                      'Caminho aberto não corta; pode servir de entrada de '
                      'efeito.',
                ),
              numero(
                m.featherLinked ? 'Suavizar' : 'Suavizar X',
                'feather',
                m.feather,
                gravada.feather,
                min: 0,
                max: 200,
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Eixos juntos',
                chave: 'mascara-${m.id}-eixos',
                filho: AureaToggle(
                  valor: m.featherLinked,
                  aoMudar: (_) => umPasso(
                    ref,
                    () => c.toggleMaskFeatherAxes(layerId, m.id),
                  ),
                ),
              ),
              if (!m.featherLinked)
                numero(
                  'Suavizar Y',
                  'featherY',
                  m.featherVertical,
                  gravada.featherY,
                  min: 0,
                  max: 200,
                ),
              numero(
                'Expansão',
                'expansion',
                m.expansion,
                gravada.expansion,
                min: -200,
                max: 200,
              ),
              numero(
                'Opacidade',
                'opacity',
                m.opacity,
                gravada.opacity,
                min: 0,
                max: 100,
                escala: 100,
                unidade: '%',
              ),
            ],
    );
  }
}
