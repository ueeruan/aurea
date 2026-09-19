import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';
import '../../../../core/ui/tocavel.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/am_colors.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../application/ui/editor_session.dart'
    show ModoDeTransformacao, modoDeTransformacaoProvider;
import '../../application/ui/preview_resolution.dart';
import '../../domain/ajuste_da_midia.dart';
import '../../domain/layer.dart';
import 'almofada_de_arrasto.dart';
import 'campo_de_valor.dart';
import 'dial_de_angulo.dart';
import 'faixa_de_bloqueio.dart';
import 'fita_de_ajuste.dart';
import 'rails_do_painel.dart';

/// LARGURA E ALTURA ANDAM JUNTAS?
///
/// Travado e o padrao porque e o que quase toda edicao quer: aumentar
/// sem esticar. Destravar e a excecao, e por isso e um botao de corrente
/// entre os dois campos, e nao dois campos sempre soltos.
final escalaTravadaProvider = StateProvider<bool>((ref) => true);

/// O PAINEL DE TRANSFORMACAO.
///
/// Cinco modos, cinco superficies, um rail de cada lado. Nenhum
/// deslizante — ver `docs/painel-de-transformacao-alight.md`, "A regra
/// que muda tudo": posicao e 2D, angulo e circular, e escala nao tem
/// intervalo natural. Um `Slider` mente sobre as tres.
class PainelDeTransformacao extends ConsumerStatefulWidget {
  const PainelDeTransformacao({
    super.key,
    required this.camada,
    required this.tempo,
    required this.playback,
    required this.aoVoltar,
    required this.alvoDoRail,
    this.mais,
    this.aoTrocarModo,
    this.aoSegurarCampo,
  });

  /// O QUE O TOQUE LONGO NO NUMERO FAZ. Vem de fora porque as duas
  /// coisas que moram ali — a expressao e o animador automatico — sao
  /// do painel das ferramentas, que ja tem a conta e o projeto em maos.
  final void Function(LayerProp prop, String nome, String unidade)?
  aoSegurarCampo;

  /// AVISA QUEM MANDA NO TITULO. O modo vigente e um estado DESTE
  /// painel, mas o cabecalho da zona E escreve "Transformar · <nome da
  /// propriedade>" a partir da sessao. Sem este aviso os dois se
  /// separam: o corpo mostra Escalar e o titulo continua dizendo
  /// Posicao — foi o que acontecia antes de o pivo existir.
  final ValueChanged<ModoDeTransformacao>? aoTrocarModo;

  final Layer camada;
  final Duration tempo;
  final PlaybackController playback;
  final VoidCallback aoVoltar;
  final Widget? mais;

  /// O que o rail esquerdo faz no modo vigente. Vem de fora porque quem
  /// sabe montar keyframe e curva e o painel das ferramentas, que ja tem
  /// a conta pronta para as outras categorias.
  final AlvoDoRail Function(ModoDeTransformacao) alvoDoRail;

  @override
  ConsumerState<PainelDeTransformacao> createState() =>
      _PainelDeTransformacaoState();
}

class _PainelDeTransformacaoState extends ConsumerState<PainelDeTransformacao> {
  /// A FOTO DO VALOR NO INICIO DO ARRASTO.
  ///
  /// As superficies entregam o deslocamento ACUMULADO, e nao o do
  /// quadro. Somar quadro a quadro arredondaria em cada soma e a camada
  /// terminaria alguns pixels longe de onde o dedo parou.
  Offset _posicaoAoComecar = Offset.zero;
  Offset _pivoAoComecar = Offset.zero;
  bool _moverZ = false;
  double _zAoComecar = 0;

  EditorController get _c => ref.read(editorControllerProvider.notifier);

  Layer get _camada =>
      ref.watch(projetoVisivelProvider).layerById(widget.camada.id) ??
      widget.camada;

  Duration get _local => _camada.localTime(widget.tempo);

  void _abrirLote() => _c.beginGesture();

  void _fecharLote() => _c.endGesture();

  /// O SEGURAR DE UM CAMPO, quando o painel das ferramentas ofereceu um.
  /// Nulo devolve o campo ao segurar antigo (o atalho para digitar).
  VoidCallback? _segurar(LayerProp prop, String nome, [String unidade = '']) {
    final f = widget.aoSegurarCampo;
    return f == null ? null : () => f(prop, nome, unidade);
  }

  @override
  Widget build(BuildContext context) {
    final modo = ref.watch(modoDeTransformacaoProvider);
    // A CAMADA BLOQUEADA MOSTRA O CADEADO NO LUGAR DOS CONTROLES.
    //
    // O portao do controlador ja recusa toda edicao, e uma superficie que
    // aceita o arrasto e nao move nada e a pior resposta possivel: parece
    // travamento. Aqui a superficie nem recebe o dedo, a faixa diz por que
    // e o botao desbloqueia sem sair do painel.
    final bloqueada = ref.watch(
      editorControllerProvider.select((p) => p.metaOf(widget.camada.id).locked),
    );
    return Row(
      children: [
        RailEsquerdo(
          aoVoltar: widget.aoVoltar,
          alvo: widget.alvoDoRail(modo),
          mais: widget.mais,
        ),
        Expanded(
          child: Column(
            children: [
              if (bloqueada)
                FaixaDeBloqueio(
                  camadaId: widget.camada.id,
                  aoDesbloquear: () =>
                      _c.toggleLocked(widget.camada.id),
                ),
              if (modo != ModoDeTransformacao.mover)
                SizedBox(height: 44, child: Center(child: _campos(modo))),
              Expanded(
                child: IgnorePointer(
                  ignoring: bloqueada,
                  child: Opacity(
                    opacity: bloqueada ? 0.45 : 1,
                    child: _superficie(modo),
                  ),
                ),
              ),
              const SizedBox(height: 10),
            ],
          ),
        ),
        RailDireito(
          modos: const [
            (Icons.open_with_rounded, 'Mover'),
            (Icons.rotate_right_rounded, 'Girar'),
            (Icons.aspect_ratio_rounded, 'Escalar'),
            (Icons.transform_rounded, 'Inclinar'),
            (Icons.filter_center_focus_rounded, 'Pivô'),
          ],
          vigente: modo.index,
          aoEscolher: (i) {
            final escolhido = ModoDeTransformacao.values[i];
            ref.read(modoDeTransformacaoProvider.notifier).state = escolhido;
            widget.aoTrocarModo?.call(escolhido);
          },
        ),
      ],
    );
  }

  // ------------------------------------------------------- os campos

  Widget _campos(ModoDeTransformacao modo) {
    final l = _camada;
    switch (modo) {
      case ModoDeTransformacao.mover:
        final p = l.position.valueAt(_local);
        return Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Flexible(
              child: CampoDeValor(
                rotulo: 'x',
                nome: 'Posição X',
                valor: p.dx,
                cor: AmColors.accent,
                aoSegurar: _segurar(LayerProp.position, 'Posição X'),
                aoDigitar: (v) =>
                    _c.editPosition(l.id, widget.tempo, Offset(v, p.dy)),
              ),
            ),
            const SizedBox(width: 6),
            Flexible(
              child: CampoDeValor(
                rotulo: 'y',
                nome: 'Posição Y',
                valor: p.dy,
                cor: AmColors.accent,
                aoSegurar: _segurar(LayerProp.position, 'Posição Y'),
                aoDigitar: (v) =>
                    _c.editPosition(l.id, widget.tempo, Offset(p.dx, v)),
              ),
            ),
            const SizedBox(width: 14),
            Flexible(
              child: CampoDeValor(
                rotulo: 'z',
                nome: 'Profundidade Z · toque para selecionar, segure para digitar',
                valor: l.positionZ.valueAt(_local),
                cor: _moverZ ? AmColors.accent : Colors.white,
                aoSelecionar: () => setState(() => _moverZ = !_moverZ),
                aoDigitar: (v) => _c.editPositionZ(l.id, widget.tempo, v),
              ),
            ),
          ],
        );
      case ModoDeTransformacao.girar:
        // O ANGULO MORA NO CENTRO DO DIAL, e nao aqui em cima: e para la
        // que o olho vai enquanto o dedo gira.
        //
        // COM O 3D LIGADO, NAO. Sao tres dials lado a lado, e o numero
        // de cada um encolhe junto com o botao — a fileira de cima
        // volta a carregar os valores exatos, como no modo mover.
        if (!l.is3D) return const SizedBox.shrink();
        return Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CampoDeValor(
              rotulo: 'x',
              nome: 'Giro em X',
              valor: l.rotationX.valueAt(_local),
              casas: 1,
              sufixo: '°',
              aoSegurar: _segurar(LayerProp.rotation, 'Giro em X', '°'),
              aoDigitar: (v) => _c.editRotationX(l.id, widget.tempo, v),
            ),
            const SizedBox(width: 6),
            CampoDeValor(
              rotulo: 'y',
              nome: 'Giro em Y',
              valor: l.rotationY.valueAt(_local),
              casas: 1,
              sufixo: '°',
              aoSegurar: _segurar(LayerProp.rotation, 'Giro em Y', '°'),
              aoDigitar: (v) => _c.editRotationY(l.id, widget.tempo, v),
            ),
            const SizedBox(width: 6),
            CampoDeValor(
              rotulo: 'z',
              nome: 'Giro em Z',
              valor: l.rotation.valueAt(_local),
              casas: 1,
              sufixo: '°',
              aoSegurar: _segurar(LayerProp.rotation, 'Giro em Z', '°'),
              aoDigitar: (v) => _c.editRotation(l.id, widget.tempo, v),
            ),
          ],
        );
      case ModoDeTransformacao.escalar:
        final travada = ref.watch(escalaTravadaProvider);
        return Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CampoDeValor(
              rotulo: 'Largura',
              nome: 'Largura',
              valor: l.scaleX.valueAt(_local) * 100,
              cor: AmColors.accent,
              sufixo: '%',
              aoSegurar: _segurar(LayerProp.scale, 'Largura', '%'),
              aoDigitar: (v) => _escalar(v / 100),
            ),
            _Corrente(
              travada: travada,
              aoTocar: () =>
                  ref.read(escalaTravadaProvider.notifier).state = !travada,
            ),
            CampoDeValor(
              rotulo: 'Altura',
              nome: 'Altura',
              valor: l.scaleY.valueAt(_local) * 100,
              cor: Colors.white,
              sufixo: '%',
              aoSegurar: _segurar(LayerProp.scale, 'Altura', '%'),
              aoDigitar: (v) => _escalar(v / 100, eixoY: true),
            ),
          ],
        );
      case ModoDeTransformacao.pivo:
        // O PONTO DE GIRO ANDA EM DOIS EIXOS, e o desenho do palco o
        // mostra em pixels da composicao — por isso dois campos com
        // sinal, e nao um par de 0 a 1. Zero e o centro.
        final pv = l.pivot.valueAt(_local);
        return Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Flexible(
              child: CampoDeValor(
                rotulo: 'x',
                nome: 'Pivô em X · 0 é o centro',
                valor: pv.dx,
                cor: AmColors.accent,
                aoSegurar: _segurar(LayerProp.pivot, 'Pivô em X'),
                aoDigitar: (v) =>
                    _c.editPivot(l.id, widget.tempo, Offset(v, pv.dy)),
              ),
            ),
            const SizedBox(width: 6),
            Flexible(
              child: CampoDeValor(
                rotulo: 'y',
                nome: 'Pivô em Y · 0 é o centro',
                valor: pv.dy,
                cor: AmColors.accent,
                aoSegurar: _segurar(LayerProp.pivot, 'Pivô em Y'),
                aoDigitar: (v) =>
                    _c.editPivot(l.id, widget.tempo, Offset(pv.dx, v)),
              ),
            ),
            const SizedBox(width: 14),
            Tocavel(
              key: const ValueKey('pivot-centro'),
              onTap: () => _c.editPivot(l.id, widget.tempo, Offset.zero),
              child: Container(
                height: 30,
                padding: const EdgeInsets.symmetric(horizontal: 14),
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: const Color(0xFF434A60),
                  borderRadius: BorderRadius.circular(8),
                ),
                child: const AppText(
                  'Centro',
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                    color: Colors.white,
                  ),
                ),
              ),
            ),
          ],
        );
      case ModoDeTransformacao.inclinar:
        return Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            CampoDeValor(
              rotulo: 'X Skew',
              nome: 'Inclinação X',
              valor: l.skewX.valueAt(_local),
              casas: 2,
              sufixo: '°',
              aoSegurar: _segurar(LayerProp.skew, 'Inclinação X', '°'),
              aoDigitar: (v) => _c.editSkewX(l.id, widget.tempo, v),
            ),
            const SizedBox(width: 8),
            CampoDeValor(
              rotulo: 'Y Skew',
              nome: 'Inclinação Y',
              valor: l.skewY.valueAt(_local),
              casas: 2,
              sufixo: '°',
              aoSegurar: _segurar(LayerProp.skew, 'Inclinação Y', '°'),
              aoDigitar: (v) => _c.editSkewY(l.id, widget.tempo, v),
            ),
          ],
        );
    }
  }

  // --------------------------------------------------- as superficies

  /// PREENCHER OU AJUSTAR, so para foto e video. Um toque volta a midia a
  /// 100% no centro cobrindo a composicao inteira ou cabendo inteira nela
  /// — a conta que o testador tentava fazer na mao arrastando a escala.
  Widget _encaixeDaMidia(Layer l) {
    final ajuste = switch (l) {
      VideoLayer v => v.ajuste,
      ImageLayer i => i.ajuste,
      _ => AjusteDaMidia.largura,
    };
    Widget opcao(String rotulo, AjusteDaMidia alvo, String chave) {
      final aceso = ajuste == alvo;
      return Padding(
        padding: const EdgeInsets.symmetric(horizontal: 4),
        child: Tocavel(
          key: ValueKey(chave),
          onTap: () => _c.setAjusteDaMidia(l.id, alvo),
          child: Container(
            height: 30,
            padding: const EdgeInsets.symmetric(horizontal: 14),
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: aceso
                  ? AmColors.accent.withValues(alpha: .18)
                  : const Color(0xFF1E222D),
              borderRadius: BorderRadius.circular(15),
            ),
            child: AppText(
              rotulo,
              style: TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: aceso ? AmColors.accent : AmColors.text,
              ),
            ),
          ),
        ),
      );
    }

    return Row(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        opcao('Preencher', AjusteDaMidia.cobrir, 'midia-preencher'),
        opcao('Ajustar', AjusteDaMidia.conter, 'midia-ajustar'),
      ],
    );
  }

  Widget _superficie(ModoDeTransformacao modo) {
    final l = _camada;
    switch (modo) {
      case ModoDeTransformacao.mover:
        final projeto = ref.watch(projetoVisivelProvider);
        final ganho = projeto.outputWidth / 360;
        return AlmofadaDeArrasto(
          key: const ValueKey('position-drag-pad'),
          dica: _moverZ
              ? 'Deslize para ajustar Z · toque em Z para voltar a X/Y'
              : 'Deslize para mover X/Y · toque em Z para profundidade',
          cabecalho: _campos(ModoDeTransformacao.mover),
          aoComecar: () {
            _posicaoAoComecar = l.position.valueAt(_local);
            _zAoComecar = l.positionZ.valueAt(_local);
            _abrirLote();
          },
          aoMover: (d) {
            if (_moverZ) {
              _c.editPositionZ(l.id, widget.tempo, _zAoComecar - d.dy * ganho);
              return;
            }
            var target = _posicaoAoComecar + d * ganho;
            double? x, y;
            // Follow the initial axis when the gesture is nearly straight.
            if (d.dx.abs() > 12 && d.dy.abs() < 6) {
              y = _posicaoAoComecar.dy;
            } else if (d.dy.abs() > 12 && d.dx.abs() < 6) {
              x = _posicaoAoComecar.dx;
            }
            final cx = projeto.outputWidth / 2;
            final cy = projeto.outputHeight / 2;
            if ((target.dx - cx).abs() < 5 * ganho) x = cx;
            if ((target.dy - cy).abs() < 5 * ganho) y = cy;
            target = Offset(x ?? target.dx, y ?? target.dy);
            ref.read(transformGuidesProvider.notifier).state = (x: x, y: y);
            _c.editPosition(l.id, widget.tempo, target);
          },
          aoTerminar: () {
            ref.read(transformGuidesProvider.notifier).state = (
              x: null,
              y: null,
            );
            _fecharLote();
          },
        );
      case ModoDeTransformacao.girar:
        if (!l.is3D) {
          return DialDeAngulo(
            // A CHAVE E O ANCORADOURO DOS TESTES DE GESTO do dial. Ela
            // existia no controle antigo do painel e sumiu quando o
            // corpo passou a ser este — dois testes de gesto ficaram
            // procurando um dial que nao se acha mais por chave.
            key: const ValueKey('rotation-dial'),
            angulo: l.rotation.valueAt(_local),
            aoComecar: _abrirLote,
            aoMudar: (g) => _c.editRotation(l.id, widget.tempo, g),
            aoTerminar: _fecharLote,
          );
        }
        // TRES EIXOS, TRES DIAIS. Um dial por eixo em vez de um seletor
        // de eixo com um dial so: girar em 3D e ajustar a RELACAO entre
        // os tres, e com um dial de cada vez essa relacao vira memoria.
        //
        // UM LOSANGO PARA OS TRES, e isso esta certo: no motor a
        // rotacao e uma propriedade de tres eixos, nao tres
        // propriedades — `toggleKeyframe` marca X, Y e Z no mesmo
        // instante, e a curva vale para os tres.
        return Center(
          child: SizedBox(
            height: 96,
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                _dial(
                  'Girar em X',
                  l.rotationX.valueAt(_local),
                  (g) => _c.editRotationX(l.id, widget.tempo, g),
                ),
                _dial(
                  'Girar em Y',
                  l.rotationY.valueAt(_local),
                  (g) => _c.editRotationY(l.id, widget.tempo, g),
                ),
                _dial(
                  'Girar em Z',
                  l.rotation.valueAt(_local),
                  (g) => _c.editRotation(l.id, widget.tempo, g),
                ),
              ],
            ),
          ),
        );
      case ModoDeTransformacao.escalar:
        return Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            if (l is ImageLayer || l is VideoLayer) ...[
              _encaixeDaMidia(l),
              const SizedBox(height: 6),
            ],
            Expanded(
              child: FitaDeAjuste(
                rotulo: 'Largura',
                valor: l.scaleX.valueAt(_local) * 100,
                porPixel: .5,
                altura: double.infinity,
                ativa: true,
                aoComecar: _abrirLote,
                aoMudar: (v) => _escalar(v / 100),
                aoTerminar: _fecharLote,
              ),
            ),
            const SizedBox(height: 8),
            Expanded(
              child: FitaDeAjuste(
                rotulo: 'Altura',
                valor: l.scaleY.valueAt(_local) * 100,
                porPixel: .5,
                altura: double.infinity,
                ativa: false,
                aoComecar: _abrirLote,
                aoMudar: (v) => _escalar(v / 100, eixoY: true),
                aoTerminar: _fecharLote,
              ),
            ),
          ],
        );
      case ModoDeTransformacao.inclinar:
        return Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Expanded(
              child: FitaDeAjuste(
                rotulo: 'Inclinacao X',
                valor: l.skewX.valueAt(_local),
                porPixel: .25,
                altura: double.infinity,
                aoComecar: _abrirLote,
                aoMudar: (v) => _c.editSkewX(l.id, widget.tempo, v),
                aoTerminar: _fecharLote,
              ),
            ),
            const SizedBox(height: 8),
            Expanded(
              child: FitaDeAjuste(
                rotulo: 'Inclinacao Y',
                valor: l.skewY.valueAt(_local),
                porPixel: .25,
                altura: double.infinity,
                // A SEGUNDA FITA NAO E A ATIVA: a linha central dela sai
                // branca, e e assim que se sabe qual das duas o dedo
                // estava mexendo na referencia.
                ativa: false,
                aoComecar: _abrirLote,
                aoMudar: (v) => _c.editSkewY(l.id, widget.tempo, v),
                aoTerminar: _fecharLote,
              ),
            ),
          ],
        );
      case ModoDeTransformacao.pivo:
        // O PONTO DE GIRO E UM LUGAR, NAO UM PAR DE NUMEROS. Arrastar e
        // o gesto certo: ninguem sabe de cabeca quanto vale o pivo em
        // pixels, mas todo mundo sabe onde ele deveria estar. Os campos
        // de cima ficam para o ajuste fino.
        //
        // O GANHO E O DA POSICAO (`largura/360`): o pivo e escrito em
        // pixels da composicao, os mesmos de `position`, e duas escalas
        // diferentes para dois valores do mesmo espaco fariam o ponto
        // andar mais rapido que a camada.
        final projeto = ref.watch(projetoVisivelProvider);
        final ganho = projeto.outputWidth / 360;
        return AlmofadaDeArrasto(
          key: const ValueKey('pivot-drag-pad'),
          rotulo: 'Pivô da camada',
          dica: 'Deslize o ponto de giro · o botão Centro devolve o zero',
          // SEM CABECALHO AQUI: a fileira de campos ja esta na linha de
          // cima, como nas outras faces que nao sao o mover. Com o
          // cabecalho, os mesmos dois campos apareciam DUAS vezes — um
          // em cima do outro, com o mesmo rotulo.
          aoComecar: () {
            _pivoAoComecar = l.pivot.valueAt(_local);
            _abrirLote();
          },
          aoMover: (d) => _c.editPivot(
            l.id,
            widget.tempo,
            _pivoAoComecar + d * ganho,
          ),
          aoTerminar: _fecharLote,
        );
    }
  }

  /// Um dos tres dials do giro em 3D.
  Widget _dial(String rotulo, double angulo, void Function(double) aoMudar) =>
      Expanded(
        child: DialDeAngulo(
          compacto: true,
          rotulo: rotulo,
          angulo: angulo,
          aoComecar: _abrirLote,
          aoMudar: aoMudar,
          aoTerminar: _fecharLote,
        ),
      );

  /// Escreve a escala. Travada, [valor] vale para os dois eixos; solta,
  /// vale so para o eixo que [eixoY] escolhe.
  void _escalar(double valor, {bool eixoY = false}) {
    if (ref.read(escalaTravadaProvider)) {
      _c.editScaleUniform(_camada.id, widget.tempo, valor);
      return;
    }
    if (eixoY) {
      _c.editScaleY(_camada.id, widget.tempo, valor);
      return;
    }
    _c.editScaleX(_camada.id, widget.tempo, valor);
  }
}

/// A CORRENTE entre Largura e Altura.
class _Corrente extends StatelessWidget {
  const _Corrente({required this.travada, required this.aoTocar});

  final bool travada;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    toggled: travada,
    label: travada ? 'Soltar largura e altura' : 'Travar largura e altura',
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: aoTocar,
      child: Container(
        width: 34,
        height: 24,
        margin: const EdgeInsets.symmetric(horizontal: 5),
        // O CAMPO E A CORRENTE TEM A MESMA ALTURA de proposito: na
        // referencia os tres formam uma fileira so, e um botao mais
        // baixo quebraria a linha de base do numero.
        decoration: BoxDecoration(
          color: const Color(0xFF434A60),
          borderRadius: BorderRadius.circular(8),
        ),
        child: Icon(
          travada ? Icons.link_rounded : Icons.link_off_rounded,
          size: 16,
          color: Colors.white,
        ),
      ),
    ),
  );
}
