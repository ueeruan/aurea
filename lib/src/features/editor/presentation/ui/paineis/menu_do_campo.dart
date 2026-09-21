import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/ui/pro_mode.dart';
import '../../../domain/animadores.dart';
import '../../../domain/layer_meta.dart' show ExposedProperty;
import 'comum_de_objetos.dart';
import 'pecas_centrais.dart' show umPasso;

// ===========================================================================
// O MENU DO CAMPO: o toque longo no valor de uma propriedade
// ===========================================================================
//
// No editor antigo eram dois alvos (o nome abria "Animar sozinho", o valor
// abria a expressao) e depois um menu so (`menuDoCampo` do painel de
// transformacao). Aqui e o mesmo menu, aberto pelo toque longo na caixa de
// valor de uma linha ([AureaPropertyRow.aoSegurarValor]) ou pelo ⋯ do
// cabecalho do painel Transformar (a linha de ponto — posicao, pivo — nao
// tem caixa unica para segurar):
//
//  * Expressao… (Pro): a conta que calcula o valor;
//  * Animar sozinho…: o animador automatico, sem keyframe nenhum;
//  * Expor no projeto: a propriedade vira controle em ⚙ Propriedades.

/// As propriedades que aceitam EXPRESSAO (as trilhas numericas).
const _comExpressao = {
  LayerProp.opacity,
  LayerProp.rotation,
  LayerProp.scale,
  LayerProp.skew,
};

/// As que o projeto sabe EXPOR (as que `setExposedValue` escreve), com o
/// nome gravado, a faixa e a chave.
const _expostas = <LayerProp, (String, double, double)>{
  LayerProp.opacity: ('opacity', 0, 1),
  LayerProp.rotation: ('rotation', -360, 360),
  LayerProp.scale: ('scale', 0, 4),
};

/// O id da propriedade exposta de [layerId]/[prop] (o mesmo do editor
/// antigo para a opacidade: `expor-<id>-opacity`).
String idDaExposicao(String layerId, LayerProp prop) =>
    'expor-$layerId-${_expostas[prop]?.$1 ?? prop.name}';

/// ABRE O MENU DO CAMPO da propriedade [prop] da camada [layerId].
Future<void> menuDoCampo(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  LayerProp prop, {
  required String nome,
  String unidade = '',
}) async {
  final c = ref.read(editorControllerProvider.notifier);
  final projeto = ref.read(editorControllerProvider);
  final camada = projeto.layerById(layerId);
  if (camada == null || prop == LayerProp.parent) return;
  final pro = ref.read(proModeProvider);
  final temExpressao = c.propExpression(camada, prop) != null;
  final temAnimador = c.propAnimador(camada, prop) != null;
  final exposta = projeto.exposed.any(
    (e) => e.id == idDaExposicao(layerId, prop),
  );
  final escolha = await mostrarAureaMenu<String>(
    context,
    titulo: nome,
    itens: [
      if (pro && _comExpressao.contains(prop))
        AureaMenuItem(
          valor: 'expressao',
          rotulo: 'Expressão…',
          icone: CupertinoIcons.function,
          marcado: temExpressao,
          chave: 'campo-expressao',
        ),
      AureaMenuItem(
        valor: 'animar',
        rotulo: 'Animar sozinho…',
        icone: CupertinoIcons.wand_stars,
        marcado: temAnimador,
        chave: 'campo-animar',
      ),
      if (_expostas.containsKey(prop))
        AureaMenuItem(
          valor: 'expor',
          rotulo: exposta ? 'Tirar de Propriedades' : 'Expor no projeto',
          icone: exposta
              ? CupertinoIcons.slider_horizontal_below_rectangle
              : CupertinoIcons.slider_horizontal_3,
          marcado: exposta,
          chave: 'campo-expor',
        ),
    ],
  );
  if (escolha == null || !context.mounted) return;
  switch (escolha) {
    case 'expressao':
      final atual = ref.read(editorControllerProvider).layerById(layerId);
      if (atual == null) return;
      final erro = switch (prop) {
        LayerProp.opacity => atual.opacity.expressionError?.mensagem,
        LayerProp.rotation => atual.rotation.expressionError?.mensagem,
        LayerProp.scale => atual.scaleX.expressionError?.mensagem,
        LayerProp.skew => atual.skewX.expressionError?.mensagem,
        _ => null,
      };
      final r = await showExpressionEditor(
        context,
        atual: c.propExpression(atual, prop),
        erro: erro,
        nome: nome,
      );
      if (r == null) return;
      umPasso(ref, () => c.setPropExpression(layerId, prop, r));
    case 'animar':
      await mostrarFolhaDoAnimador(
        context,
        layerId: layerId,
        prop: prop,
        nome: nome,
        unidade: unidade,
      );
    case 'expor':
      final id = idDaExposicao(layerId, prop);
      if (exposta) {
        umPasso(ref, () => c.unexposeProperty(id));
        return;
      }
      final (propriedade, min, max) = _expostas[prop]!;
      umPasso(
        ref,
        () => c.exposeProperty(
          ExposedProperty(
            id: id,
            layerId: layerId,
            property: propriedade,
            label: '${camada.name} · ${translate(context, nome)}',
            min: min,
            max: max,
          ),
        ),
      );
      AureaSnack.show(
        context,
        moldar(context, '{0} exposta em ⚙ Propriedades do projeto', [
          translate(context, nome),
        ]),
      );
  }
}

/// A FOLHA "ANIMAR SOZINHO": a propriedade balanca por conta propria. O
/// que se escolhe e a forma do balanco (tipo, forca, volta, comeco).
Future<void> mostrarFolhaDoAnimador(
  BuildContext context, {
  required String layerId,
  required LayerProp prop,
  required String nome,
  String unidade = '',
}) => mostrarAureaFolha<void>(
  context,
  titulo: moldar(context, 'Animar {0} sozinho', [translate(context, nome)]),
  modal: false,
  construtor: (_) =>
      FolhaDoAnimador(layerId: layerId, prop: prop, unidade: unidade),
);

/// O CORPO da folha "Animar sozinho" (publico para o teste montar).
class FolhaDoAnimador extends ConsumerWidget {
  const FolhaDoAnimador({
    super.key,
    required this.layerId,
    required this.prop,
    this.unidade = '',
  });

  final String layerId;
  final LayerProp prop;
  final String unidade;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final camada = ref.watch(
      editorControllerProvider.select((p) => p.layerById(layerId)),
    );
    if (camada == null) {
      return const AureaAvisoDoPainel(texto: 'Esta camada não existe mais.');
    }
    final c = ref.read(editorControllerProvider.notifier);
    final ponto = EditorController.propEhPonto(prop);
    final atual = c.propAnimador(camada, prop);
    final a = atual ?? const AnimadorAutomatico();
    final multiplica = a.modo == ModoDoAnimador.multiplicar;
    // Cada passo grava o animador inteiro; o arrasto da linha ja abre o
    // gesto (um arrasto = um desfazer).
    void aplicar(AnimadorAutomatico novo) =>
        c.setPropAnimador(layerId, prop, novo);

    AureaPropertyRow forca(String rotulo, String chave, double v, bool doY) =>
        linhaNumerica(
          ref,
          rotulo: rotulo,
          chave: chave,
          valor: multiplica ? v * 100 : v,
          min: multiplica ? 0 : -2000,
          max: multiplica ? 400 : 2000,
          casas: multiplica ? 0 : 1,
          unidade: multiplica ? '%' : unidade,
          aoMudar: (x) {
            final f = multiplica ? x / 100 : x;
            aplicar(doY ? a.copyWith(forcaY: f) : a.copyWith(forca: f));
          },
        );

    return ListView(
      shrinkWrap: true,
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        0,
        AureaDims.margemDoPainel,
        AureaDims.topoDoPainel,
      ),
      children: [
        AureaPropertyRow.personalizada(
          rotulo: 'Forma',
          chave: 'animador-tipo',
          filho: FileiraDePilulas<TipoDoAnimador>(
            chave: 'animador-tipo',
            chaveDe: (t) => t.name,
            opcoes: TipoDoAnimador.values,
            atual: atual == null ? null : a.tipo,
            rotuloDe: rotuloDoAnimador,
            aoEscolher: (t) => umPasso(ref, () => aplicar(a.copyWith(tipo: t))),
          ),
        ),
        AureaAvisoDoPainel(texto: explicacaoDoAnimador(a.tipo)),
        forca(ponto ? 'Força X' : 'Força', 'animador-forca', a.forca, false),
        if (ponto) forca('Força Y', 'animador-forca-y', a.forcaDoY, true),
        linhaNumerica(
          ref,
          rotulo: 'Volta',
          chave: 'animador-periodo',
          valor: a.periodo,
          min: .05,
          max: 30,
          casas: 2,
          unidade: 's',
          aoMudar: (v) => aplicar(a.copyWith(periodo: v)),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Começo',
          chave: 'animador-fase',
          valor: a.fase * 100,
          min: 0,
          max: 100,
          unidade: '%',
          aoMudar: (v) => aplicar(a.copyWith(fase: v / 100)),
        ),
        if (a.tipo == TipoDoAnimador.aleatorio)
          linhaNumerica(
            ref,
            rotulo: 'Sorteio',
            chave: 'animador-semente',
            valor: a.semente.toDouble(),
            min: 1,
            max: 999,
            aoMudar: (v) => aplicar(a.copyWith(semente: v.round())),
          ),
        // SOMAR ou MULTIPLICAR: escala e opacidade ficam melhores em
        // porcentagem do valor; posicao, em pixels.
        AureaPropertyRow.personalizada(
          rotulo: 'Modo',
          chave: 'animador-modo',
          filho: FileiraDePilulas<ModoDoAnimador>(
            chave: 'animador-modo',
            chaveDe: (m) => m.name,
            opcoes: ModoDoAnimador.values,
            atual: a.modo,
            rotuloDe: (m) => m == ModoDoAnimador.somar
                ? 'Somar ao valor'
                : 'Por cento do valor',
            aoEscolher: (m) => umPasso(
              ref,
              () => aplicar(
                a.copyWith(
                  modo: m,
                  forca: m == ModoDoAnimador.multiplicar
                      ? .2
                      : (ponto ? 40 : 20),
                  limparForcaY: true,
                ),
              ),
            ),
          ),
        ),
        if (atual != null)
          LinhaDeAcao(
            key: const ValueKey('animador-tirar'),
            rotulo: 'Tirar o animador',
            icone: CupertinoIcons.xmark_circle,
            destrutiva: true,
            aoTocar: () {
              umPasso(ref, () => c.setPropAnimador(layerId, prop, null));
              Navigator.of(context).maybePop();
            },
          ),
      ],
    );
  }
}
