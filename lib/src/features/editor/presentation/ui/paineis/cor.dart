import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../media/application/media_import_service.dart';
import '../../../application/editor_controller.dart';
import '../../../application/ui/effect_recents.dart';
import '../../../domain/effect.dart';
import '../../../domain/layer.dart';
import '../../../domain/layer_meta.dart';
import '../../../domain/shape.dart';
import 'degrade.dart' show showGradientFillSheet;
import 'elemento3d.dart' show showElement3DSheet;
import '../shell/contrato.dart';
import 'comum.dart';
import 'efeitos.dart';
import 'pecas_centrais.dart';

/// A CATEGORIA DA CORRECAO DE COR no catalogo de efeitos.
const _categoriaDeCor = 'Color';

/// COR — o "Color & Fill" da referencia, mais a correcao de cor:
///
///  * FORMA: o preenchimento (nenhum, cor, degrade, midia) e a cor;
///  * TEXTO, VIDEO, IMAGEM e o resto: o preenchimento por cima da camada
///    (a propria cor, uma cor chapada ou um degrade) e, embaixo, a
///    CORRECAO DE COR — os efeitos da categoria Cor numa pilha propria,
///    com um toque para acrescentar cada um;
///  * ELEMENTO 3D: a ficha do elemento, onde mora o material.
///
/// A correcao e a MESMA pilha do painel Efeitos, filtrada: o cartao, o
/// losango e o desfazer sao os de la.
class PainelCor extends ConsumerWidget {
  const PainelCor({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Cor';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.cor.name}';
    switch (camada) {
      case ShapeLayer():
        return AureaPanel(
          titulo: _titulo,
          chave: chave,
          aoFechar: escopo.fecharPainel,
          filhos: [_PreenchimentoDaForma(forma: camada)],
        );
      case Element3DLayer():
        return AureaPanel(
          titulo: _titulo,
          chave: chave,
          aoFechar: escopo.fecharPainel,
          filhos: [
            LinhaDePorta(
              rotulo: 'Cor e material do elemento',
              aoTocar: () => showElement3DSheet(context, ref, layerId),
            ),
          ],
        );
      default:
        final c = ref.read(editorControllerProvider.notifier);
        return AureaPanel(
          titulo: _titulo,
          chave: chave,
          aoFechar: escopo.fecharPainel,
          corpo: PilhaDeEfeitos(
            layerId: layerId,
            filtro: (e) => e.conhecido && e.spec.category == _categoriaDeCor,
            vazio: 'Nenhuma correção de cor. Escolha uma acima.',
            cabecalho: [
              _SobreposicaoDeCor(camada: camada),
              AureaSection(
                titulo: 'Correção de cor',
                chave: 'cor-correcao',
                recolhivel: false,
                filhos: [
                  FileiraDeAcoes(
                    acoes: [
                      for (final tipo in effectsInCategory(_categoriaDeCor))
                        AureaChip(
                          key: ValueKey('cor-adicionar-${tipo.name}'),
                          rotulo: effectSpecs[tipo]?.name ?? tipo.name,
                          icone: CupertinoIcons.plus,
                          aoTocar: () {
                            umPasso(ref, () => c.addEffect(layerId, tipo));
                            ref
                                .read(effectRecentsProvider.notifier)
                                .registrar(tipo);
                          },
                        ),
                    ],
                  ),
                ],
              ),
            ],
          ),
        );
    }
  }
}

/// O PREENCHIMENTO POR CIMA DE UMA CAMADA que nao e forma: a cor dela
/// mesma (a do texto, ou a da midia), uma cor chapada ou um degrade.
/// Mesma conta do painel antigo (`colorOverlay`/`gradientOverlay`).
class _SobreposicaoDeCor extends ConsumerWidget {
  const _SobreposicaoDeCor({required this.camada});

  final Layer camada;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final id = camada.id;
    final c = ref.read(editorControllerProvider.notifier);
    // SO O ACABAMENTO desta camada.
    final estilos = ref.watch(
      editorControllerProvider.select((p) => p.metaOf(id).styles),
    );
    final texto = camada is TextLayer ? camada as TextLayer : null;
    final modo = estilos.gradientOverlay?.enabled == true
        ? 2
        : (estilos.colorOverlay?.enabled == true ? 1 : 0);

    void escolherModo(int novo) => umPasso(
      ref,
      () => c.updateLayerStyles(id, (s) {
        switch (novo) {
          case 0:
            return s.copyWith(
              clearColorOverlay: true,
              clearGradientOverlay: true,
            );
          case 1:
            return s.copyWith(
              colorOverlay: (s.colorOverlay ?? OverlayStyle()).copyWith(
                enabled: true,
              ),
              clearGradientOverlay: true,
            );
          default:
            return s.copyWith(
              gradientOverlay: (s.gradientOverlay ?? GradientOverlayStyle())
                  .copyWith(enabled: true),
              clearColorOverlay: true,
            );
        }
      }),
    );

    final degrade = estilos.gradientOverlay ?? GradientOverlayStyle();
    void corDoDegrade(bool inicio, Color nova) => c.updateLayerStyles(
      id,
      (s) => s.copyWith(
        gradientOverlay: (s.gradientOverlay ?? GradientOverlayStyle()).copyWith(
          colorA: inicio ? nova : null,
          colorB: inicio ? null : nova,
          enabled: true,
        ),
      ),
    );

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        AureaPropertyRow.personalizada(
          rotulo: 'Preenchimento',
          chave: 'cor-preenchimento',
          filho: AureaDropdown<int>(
            valor: modo,
            opcoes: const [0, 1, 2],
            rotuloDe: (m) => switch (m) {
              0 => texto != null ? 'Cor do texto' : 'Da própria camada',
              1 => 'Cor por cima',
              _ => 'Degradê',
            },
            titulo: 'Preenchimento',
            aoMudar: escolherModo,
          ),
        ),
        if (modo == 0 && texto != null)
          AureaPropertyRow.cor(
            rotulo: 'Cor do texto',
            chave: 'cor-do-texto',
            cor: texto.color,
            aoTocar: () => escolherCor(
              context,
              ref,
              inicial: texto.color,
              aplicar: (cor) => c.editTextLayer(id, color: cor),
            ),
          ),
        if (modo == 1)
          AureaPropertyRow.cor(
            rotulo: 'Cor',
            chave: 'cor-por-cima',
            cor: estilos.colorOverlay?.color ?? AureaCores.destaque,
            aoTocar: () => escolherCor(
              context,
              ref,
              inicial: estilos.colorOverlay?.color ?? AureaCores.destaque,
              aplicar: (cor) => c.updateLayerStyles(
                id,
                (s) => s.copyWith(
                  colorOverlay: (s.colorOverlay ?? OverlayStyle()).copyWith(
                    color: cor,
                    enabled: true,
                  ),
                ),
              ),
            ),
          ),
        if (modo == 2) ...[
          AureaPropertyRow.cor(
            rotulo: 'Início',
            chave: 'cor-degrade-inicio',
            cor: degrade.colorA,
            aoTocar: () => escolherCor(
              context,
              ref,
              inicial: degrade.colorA,
              aplicar: (cor) => corDoDegrade(true, cor),
            ),
          ),
          AureaPropertyRow.cor(
            rotulo: 'Fim',
            chave: 'cor-degrade-fim',
            cor: degrade.colorB,
            aoTocar: () => escolherCor(
              context,
              ref,
              inicial: degrade.colorB,
              aplicar: (cor) => corDoDegrade(false, cor),
            ),
          ),
        ],
      ],
    );
  }
}

/// O PREENCHIMENTO DA FORMA: nenhum, cor, degrade (o editor de degrade
/// vetorial que ja existe) ou uma foto dentro da forma.
class _PreenchimentoDaForma extends ConsumerWidget {
  const _PreenchimentoDaForma({required this.forma});

  final ShapeLayer forma;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final c = ref.read(editorControllerProvider.notifier);
    final id = forma.id;
    final tipo = tipoDePreenchimentoDe(forma.contents);
    final midia = forma.contents.whereType<ShapeMediaFill>().firstOrNull;

    Future<void> escolherFoto() async {
      final foto = await ref
          .read(mediaImportServiceProvider)
          .pickImageFromGallery();
      if (foto == null) return;
      umPasso(
        ref,
        () => c.definirTipoDePreenchimento(
          id,
          TipoDePreenchimento.midia,
          midia: foto.path,
        ),
      );
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        AureaPropertyRow.personalizada(
          rotulo: 'Preenchimento',
          chave: 'cor-preenchimento',
          filho: AureaDropdown<TipoDePreenchimento>(
            valor: tipo,
            opcoes: TipoDePreenchimento.values,
            rotuloDe: (t) => switch (t) {
              TipoDePreenchimento.nenhum => 'Nenhum',
              TipoDePreenchimento.cor => 'Cor',
              TipoDePreenchimento.degrade => 'Degradê',
              TipoDePreenchimento.midia => 'Mídia',
            },
            titulo: 'Preenchimento',
            aoMudar: (novo) {
              if (novo == TipoDePreenchimento.midia && midia == null) {
                escolherFoto();
                return;
              }
              umPasso(ref, () => c.definirTipoDePreenchimento(id, novo));
            },
          ),
        ),
        switch (tipo) {
          TipoDePreenchimento.nenhum => const AureaAvisoDoPainel(
            texto: 'Sem preenchimento: só o traço (se houver) aparece.',
          ),
          TipoDePreenchimento.cor => AureaPropertyRow.cor(
            rotulo: 'Cor',
            chave: 'cor-da-forma',
            cor: forma.primaryColor,
            aoTocar: () => escolherCor(
              context,
              ref,
              inicial: forma.primaryColor,
              aplicar: (cor) => c.setShapePrimaryColor(id, cor),
            ),
          ),
          TipoDePreenchimento.degrade => LinhaDePorta(
            rotulo: 'Editar degradê',
            icone: CupertinoIcons.color_filter,
            aoTocar: () => showGradientFillSheet(
              context,
              id,
              playback: EscopoDoEditor.of(context).playback,
            ),
          ),
          TipoDePreenchimento.midia => Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            mainAxisSize: MainAxisSize.min,
            children: [
              FileiraDeAcoes(
                acoes: [
                  AureaChip(
                    key: const ValueKey('cor-midia-escolher'),
                    rotulo: midia == null
                        ? 'Escolher uma foto'
                        : 'Trocar a foto',
                    icone: CupertinoIcons.photo,
                    aoTocar: escolherFoto,
                  ),
                ],
              ),
              if (midia != null)
                AureaPropertyRow.personalizada(
                  rotulo: 'Encaixe',
                  chave: 'cor-midia-encaixe',
                  filho: AureaDropdown<EncaixeNaForma>(
                    valor: midia.encaixe,
                    opcoes: EncaixeNaForma.values,
                    rotuloDe: (e) => switch (e) {
                      EncaixeNaForma.preencher => 'Preencher',
                      EncaixeNaForma.caber => 'Caber',
                      EncaixeNaForma.esticar => 'Esticar',
                    },
                    aoMudar: (e) => umPasso(
                      ref,
                      () => c.definirEncaixeDaMidiaNaForma(id, e),
                    ),
                  ),
                ),
            ],
          ),
        },
      ],
    );
  }
}
