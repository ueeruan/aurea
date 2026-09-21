import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../domain/apple_motion.dart' show CascadeOrder;
import '../../../domain/keyframe.dart' show Easing;
import '../../../domain/layout_ops.dart';
import '../paineis/comum_de_objetos.dart' show FileiraDePilulas;
import '../paineis/pecas_centrais.dart' show FileiraDeAcoes, respiroDoPainel;

// ALINHAR, DISTRIBUIR E ESCALONAR — as duas folhas que agem sobre uma
// SELECAO inteira, e nao sobre uma camada.
//
// Cada toque chama UMA operacao do controlador, e cada operacao ja grava
// a selecao inteira numa mutacao so: o desfazer volta o alinhamento
// todo, nunca camada por camada.

// ---------------------------------------------------------------- alinhar

/// ALINHAR E DISTRIBUIR (spec motion-graphics-pro, PR-X1). Motion graphics
/// e 60% posicionamento exato — no dedo nao fica exato.
///
/// Folha sem veu ([mostrarAureaFolha] com `modal: false`): quem alinha
/// quer ver o palco mudar a cada toque.
Future<void> showAlignSheet(
  BuildContext context,
  WidgetRef ref,
  List<String> ids,
  Duration t,
) => mostrarAureaFolha<void>(
  context,
  // O numero vai moldado ANTES de virar titulo: a folha traduz o titulo
  // inteiro, e a frase com o numero ja dentro nao casaria com o catalogo.
  titulo: moldar(context, 'Alinhar · {0} camada(s)', [ids.length]),
  modal: false,
  construtor: (folha) => _Alinhar(ref: ref, ids: ids, t: t),
);

class _Alinhar extends StatefulWidget {
  const _Alinhar({required this.ref, required this.ids, required this.t});

  final WidgetRef ref;
  final List<String> ids;
  final Duration t;

  @override
  State<_Alinhar> createState() => _AlinharState();
}

class _AlinharState extends State<_Alinhar> {
  var _referencia = AlignTo.composition;

  EditorController get _c => widget.ref.read(editorControllerProvider.notifier);

  void _alinhar(AlignEdge borda) =>
      _c.alignSelection(widget.ids, borda, widget.t, to: _referencia);

  @override
  Widget build(BuildContext context) {
    final ids = widget.ids;
    final t = widget.t;
    // Distribuir so tem sentido com tres: com duas nao ha "meio" para
    // igualar. O botao fica APAGADO e explica no toque, em vez de sumir.
    final distribui = ids.length >= 3;

    Widget bloco(AlignEdge borda, IconData icone, String rotulo) => Expanded(
      child: _BlocoDeAlinhar(
        key: ValueKey('alinhar-${borda.name}'),
        icone: icone,
        rotulo: rotulo,
        aoTocar: () => _alinhar(borda),
      ),
    );

    return SingleChildScrollView(
      padding: respiroDoPainel,
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          AureaPropertyRow.personalizada(
            rotulo: 'Em relação a',
            chave: 'alinhar-referencia',
            filho: FileiraDePilulas<AlignTo>(
              chave: 'alinhar-referencia',
              chaveDe: (a) => a.name,
              opcoes: const [AlignTo.composition, AlignTo.selection],
              atual: _referencia,
              rotuloDe: (a) => switch (a) {
                AlignTo.composition => 'Composição',
                AlignTo.selection => 'Seleção',
                AlignTo.anchor => 'Âncora',
              },
              aoEscolher: (a) => setState(() => _referencia = a),
            ),
          ),
          // Horizontal e vertical em dois grupos de tres, com um vao
          // entre eles: o olho acha o eixo antes de achar o botao.
          Row(
            children: [
              bloco(
                AlignEdge.left,
                CupertinoIcons.rectangle_grid_1x2,
                'Esquerda',
              ),
              bloco(
                AlignEdge.centerH,
                CupertinoIcons.arrow_left_right,
                'Centro H',
              ),
              bloco(
                AlignEdge.right,
                CupertinoIcons.rectangle_grid_1x2_fill,
                'Direita',
              ),
              const SizedBox(width: AureaDims.e8),
              bloco(AlignEdge.top, CupertinoIcons.arrow_up_to_line, 'Topo'),
              bloco(
                AlignEdge.centerV,
                CupertinoIcons.arrow_up_arrow_down,
                'Centro V',
              ),
              bloco(
                AlignEdge.bottom,
                CupertinoIcons.arrow_down_to_line,
                'Base',
              ),
            ],
          ),
          const SizedBox(height: AureaDims.e8),
          AureaSection(
            titulo: 'Distribuir',
            chave: 'alinhar-distribuir',
            recolhivel: false,
            filhos: [
              const AureaAvisoDoPainel(
                texto:
                    'Por centro iguala os centros; por vão iguala os '
                    'espaços. Com tamanhos diferentes, dão resultados '
                    'distintos.',
              ),
              FileiraDeAcoes(
                acoes: [
                  for (final (rotulo, eixo, modo, chave) in const [
                    (
                      '↔ centro',
                      DistributeAxis.horizontal,
                      DistributeMode.byCenter,
                      'h-centro',
                    ),
                    (
                      '↔ vão igual',
                      DistributeAxis.horizontal,
                      DistributeMode.byGap,
                      'h-vao',
                    ),
                    (
                      '↕ centro',
                      DistributeAxis.vertical,
                      DistributeMode.byCenter,
                      'v-centro',
                    ),
                    (
                      '↕ vão igual',
                      DistributeAxis.vertical,
                      DistributeMode.byGap,
                      'v-vao',
                    ),
                  ])
                    Opacity(
                      opacity: distribui ? 1 : .35,
                      child: AureaChip(
                        key: ValueKey('alinhar-distribuir-$chave'),
                        rotulo: rotulo,
                        aoTocar: () {
                          if (!distribui) {
                            AureaSnack.show(
                              context,
                              'Distribuir precisa de 3 ou mais camadas',
                              duration: const Duration(milliseconds: 1600),
                            );
                            return;
                          }
                          _c.distributeSelection(ids, eixo, modo, t);
                        },
                      ),
                    ),
                ],
              ),
            ],
          ),
          // ESPACO EXATO: o vao em pixels entre as camadas, no eixo
          // horizontal (como a folha antiga). Nenhuma pilula acende — e
          // uma acao, nao um estado guardado.
          AureaPropertyRow.personalizada(
            rotulo: 'Espaço exato',
            chave: 'alinhar-espaco',
            filho: FileiraDePilulas<double>(
              chave: 'alinhar-espaco',
              chaveDe: (g) => '${g.round()}',
              opcoes: const [0.0, 16.0, 24.0, 48.0],
              atual: null,
              traduzir: false,
              rotuloDe: (g) => '${g.round()}px',
              aoEscolher: (g) =>
                  _c.spaceSelection(ids, DistributeAxis.horizontal, g, t),
            ),
          ),
        ],
      ),
    );
  }
}

/// UM BOTAO DE ALINHAR: icone no destaque e o nome embaixo, na altura de
/// um bloco de painel. O nome e o que o icone sozinho nao diz — "centro
/// H" e "centro V" tem desenhos parecidos demais para um toque certeiro.
class _BlocoDeAlinhar extends StatelessWidget {
  const _BlocoDeAlinhar({
    super.key,
    required this.icone,
    required this.rotulo,
    required this.aoTocar,
  });

  final IconData icone;
  final String rotulo;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: aoTocar,
    child: SizedBox(
      height: AureaDims.blocoDePainel,
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(icone, size: AureaDims.iconeMd + 2, color: AureaCores.destaque),
          const SizedBox(height: AureaDims.e4),
          AppText(
            rotulo,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: AureaEstilos.rotulo,
          ),
        ],
      ),
    ),
  );
}

// ---------------------------------------------------------------- cascata

/// A profundidade da folha de cascata: quem so quer escalonar nao precisa
/// ver intervalo, ordem e curva.
enum _Profundidade { pronto, montar, avancado }

/// ESCALONAR (cascata) as camadas de [targets].
///
/// A folha nao sabe do controlador por meio de retorno: ela mesma chama
/// UMA operacao — `cascadeSelection` ou `linkCascadeSelection` —, e cada
/// uma grava a selecao inteira numa mutacao so. Por isso o "Desfazer" do
/// aviso volta a cascata toda de uma vez.
void abrirCascata(
  BuildContext context,
  WidgetRef ref,
  Set<String> targets,
  Duration time,
) {
  unawaited(
    mostrarAureaFolha<void>(
      context,
      titulo: moldar(context, 'Cascata · {0} camadas', [targets.length]),
      construtor: (folha) => _Cascata(ref: ref, alvos: targets, tempo: time),
    ),
  );
}

class _Cascata extends StatefulWidget {
  const _Cascata({required this.ref, required this.alvos, required this.tempo});

  final WidgetRef ref;
  final Set<String> alvos;
  final Duration tempo;

  @override
  State<_Cascata> createState() => _CascataState();
}

/// As curvas oferecidas para o vinculo: as da Apple e as duas molas.
const _curvas = <(String, Easing)>[
  ('Apple padrão', Easing.appleStandard),
  ('Apple entrada', Easing.appleEntrance),
  ('Apple saída', Easing.appleExit),
  ('Mola interface', Easing.interfaceSpring),
  ('Mola suave', Easing.softSpring),
];

class _CascataState extends State<_Cascata> {
  var _profundidade = _Profundidade.pronto;
  var _intervaloMs = 40.0;
  var _ordem = CascadeOrder.start;
  var _curva = Easing.interfaceSpring;
  var _propriedade = LayerProp.position;

  bool get _avancado => _profundidade == _Profundidade.avancado;

  /// APLICA E FECHA. As regras de cada profundidade sao as da folha
  /// antiga, sem tirar nem por:
  ///
  ///   Pronto ... 40 ms, do inicio, Mola de interface;
  ///   Montar ... intervalo e ordem escolhidos, SEM curva nova (as marcas
  ///              ficam com a curva que ja tinham);
  ///   Avancado . a primeira camada vira fonte e as outras a seguem com
  ///              atraso incremental, na curva escolhida.
  void _aplicar() {
    final c = widget.ref.read(editorControllerProvider.notifier);
    final intervalo = Duration(milliseconds: _intervaloMs.round());
    if (_avancado) {
      c.linkCascadeSelection(
        widget.alvos,
        widget.tempo,
        interval: intervalo,
        order: _ordem,
        ease: _curva,
        property: _propriedade,
      );
    } else {
      final pronto = _profundidade == _Profundidade.pronto;
      c.cascadeSelection(
        widget.alvos,
        interval: pronto ? const Duration(milliseconds: 40) : intervalo,
        order: pronto ? CascadeOrder.start : _ordem,
        ease: pronto ? Easing.interfaceSpring : null,
      );
    }
    // O aviso sai ANTES de fechar: depois do pop este contexto esta de
    // saida. O mensageiro e o mesmo do editor.
    AureaSnack.show(
      context,
      _avancado ? 'Vinculo em cascata aplicado' : 'Cascata aplicada',
      actionLabel: 'Desfazer',
      onAction: c.undo,
    );
    Navigator.of(context).pop();
  }

  @override
  Widget build(BuildContext context) {
    final podeAplicar = widget.alvos.length >= 2;
    final rotuloDoBotao = switch (_profundidade) {
      _Profundidade.pronto => 'Escalonar seleção',
      _Profundidade.montar => 'Aplicar cascata',
      _Profundidade.avancado => 'Vincular com atraso incremental',
    };
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        AureaTabs(
          abas: const ['Pronto', 'Montar', 'Avançado'],
          ativa: _profundidade.index,
          aoTrocar: (i) =>
              setState(() => _profundidade = _Profundidade.values[i]),
          chave: 'cascata-aba',
        ),
        Flexible(
          child: SingleChildScrollView(
            padding: respiroDoPainel,
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                if (_profundidade == _Profundidade.pronto)
                  const AureaAvisoDoPainel(
                    texto: '40 ms entre cada camada, com Mola de interface.',
                  )
                else ...[
                  // Intervalo, ordem e curva sao estado DA FOLHA: nada vai
                  // ao projeto antes do botao, entao arrastar aqui nao
                  // gasta passo de desfazer.
                  AureaPropertyRow(
                    rotulo: 'Intervalo',
                    chave: 'cascata-intervalo',
                    valor: _intervaloMs,
                    min: 0,
                    max: 200,
                    casas: 0,
                    unidade: 'ms',
                    aoMudar: (v) => setState(() => _intervaloMs = v),
                  ),
                  AureaPropertyRow.personalizada(
                    rotulo: 'Ordem',
                    chave: 'cascata-ordem',
                    filho: FileiraDePilulas<CascadeOrder>(
                      chave: 'cascata-ordem',
                      chaveDe: (o) => o.name,
                      opcoes: CascadeOrder.values,
                      atual: _ordem,
                      rotuloDe: (o) => switch (o) {
                        CascadeOrder.start => 'Início',
                        CascadeOrder.center => 'Centro',
                        CascadeOrder.end => 'Fim',
                        CascadeOrder.random => 'Aleatória',
                      },
                      aoEscolher: (o) => setState(() => _ordem = o),
                    ),
                  ),
                  if (_avancado) ...[
                    AureaPropertyRow.personalizada(
                      rotulo: 'Vínculo',
                      chave: 'cascata-vinculo',
                      filho: FileiraDePilulas<LayerProp>(
                        chave: 'cascata-vinculo',
                        chaveDe: (p) => p.name,
                        opcoes: const [
                          LayerProp.position,
                          LayerProp.scale,
                          LayerProp.rotation,
                          LayerProp.opacity,
                        ],
                        atual: _propriedade,
                        rotuloDe: (p) => switch (p) {
                          LayerProp.position => 'Posição',
                          LayerProp.scale => 'Escala',
                          LayerProp.rotation => 'Rotação',
                          LayerProp.opacity => 'Opacidade',
                          _ => p.name,
                        },
                        aoEscolher: (p) => setState(() => _propriedade = p),
                      ),
                    ),
                    AureaPropertyRow.personalizada(
                      rotulo: 'Curva',
                      chave: 'cascata-curva',
                      filho: FileiraDePilulas<int>(
                        chave: 'cascata-curva',
                        opcoes: [for (var i = 0; i < _curvas.length; i++) i],
                        // As curvas sao constantes: a identidade basta
                        // para achar qual esta escolhida.
                        atual: _curvas.indexWhere(
                          (p) => identical(p.$2, _curva),
                        ),
                        rotuloDe: (i) => _curvas[i].$1,
                        aoEscolher: (i) => setState(() => _curva = _curvas[i].$2),
                      ),
                    ),
                    const AureaAvisoDoPainel(
                      texto:
                          'Os keyframes continuam reais e podem ser editados '
                          'por camada.',
                    ),
                  ],
                ],
                const SizedBox(height: AureaDims.e8),
                CupertinoButton(
                  key: const ValueKey('cascata-aplicar'),
                  color: AureaCores.acao,
                  disabledColor: AureaCores.campo,
                  padding: const EdgeInsets.symmetric(vertical: 12),
                  onPressed: podeAplicar ? _aplicar : null,
                  child: AppText(
                    rotuloDoBotao,
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w700,
                      color: podeAplicar
                          ? AureaCores.sobreAcao
                          : AureaCores.textoSecundario,
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ],
    );
  }
}
