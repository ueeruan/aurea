import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/shape.dart';
import '../../am/color_picker_sheet.dart' show showColorPicker;
import '../shell/contrato.dart';
import 'comum.dart';
import 'comum_de_objetos.dart';
import 'pontos.dart';

/// O nome de cada tipo de forma parametrica (rotulo de UI).
String rotuloDoTipoDeForma(ParamShapeKind k) => switch (k) {
  ParamShapeKind.rect => 'Retângulo',
  ParamShapeKind.ellipse => 'Elipse',
  ParamShapeKind.polygon => 'Polígono',
  ParamShapeKind.star => 'Estrela',
  ParamShapeKind.sector => 'Setor',
  ParamShapeKind.seta => 'Seta',
  ParamShapeKind.linhaLarga => 'Linha',
  ParamShapeKind.lua => 'Lua',
  ParamShapeKind.multifolio => 'Flor',
  ParamShapeKind.mais => 'Mais',
  ParamShapeKind.selo => 'Selo',
  ParamShapeKind.gota => 'Gota',
  ParamShapeKind.balao => 'Balão',
};

/// As chaves de parametro medidas em graus.
const _emGraus = {'shapeRotation', 'startAngle', 'sweep'};

/// FORMA — o "Editar forma" da referencia, em quatro abas curtas:
///
///  * Forma: o tipo e os numeros que ESTE tipo usa (`parametrosDaForma`:
///    estrela nao mostra largura, retangulo nao mostra pontas), cada um
///    com losango;
///  * Cor: o preenchimento (nenhum, cor, degrade);
///  * Traco: cor, espessura, opacidade e tracejado, com losango;
///  * Desenhar: o progresso do traco (inicio, fim, deslocamento).
///
/// O lapis do cabecalho abre o editor de pontos. Com este painel aberto o
/// palco mostra as alcas da forma viva (a casca espelha o painel na
/// sessao).
class PainelForma extends ConsumerStatefulWidget {
  const PainelForma({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelForma> createState() => _PainelFormaState();
}

class _PainelFormaState extends ConsumerState<PainelForma> {
  static const _titulo = 'Forma';
  static const _abas = ['Forma', 'Cor', 'Traço', 'Desenhar'];

  int _aba = 0;

  String get _id => widget.layerId;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final visivel = camadaVisivel(ref, _id);
    final gravada = camadaGravada(ref, _id);
    final chave = 'painel-${PainelId.forma.name}';
    if (visivel == null || gravada == null) {
      return const PainelSemCamada(titulo: _titulo);
    }
    if (visivel is! ShapeLayer || gravada is! ShapeLayer) {
      return PainelDeTipoErrado(
        titulo: _titulo,
        chave: chave,
        aviso: 'Esta camada não é uma forma.',
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      abas: _abas,
      abaAtiva: _aba,
      aoTrocarAba: (i) => setState(() => _aba = i),
      aoFechar: escopo.fecharPainel,
      acoes: [
        Tocavel(
          key: const ValueKey('forma-pontos'),
          onTap: () =>
              abrirEditarPontosDaForma(context, ref, escopo.playback, _id),
          child: SizedBox(
            width: AureaDims.toqueConfortavel,
            height: AureaDims.cabecalhoDoPainel,
            child: Icon(
              CupertinoIcons.scribble,
              size: AureaDims.iconeMd,
              color: AureaCores.destaque,
            ),
          ),
        ),
      ],
      corpo: NoCabecote(
        construir: (context, t) => ListView(
          key: ValueKey('forma-aba-$_aba'),
          padding: const EdgeInsets.fromLTRB(
            AureaDims.margemDoPainel,
            AureaDims.e4,
            AureaDims.margemDoPainel,
            AureaDims.topoDoPainel,
          ),
          children: switch (_aba) {
            0 => _forma(visivel, gravada, t, escopo.playback),
            1 => _cor(visivel),
            2 => _traco(visivel, gravada, t, escopo.playback),
            _ => _desenhar(visivel, gravada, t, escopo.playback),
          },
        ),
      ),
    );
  }

  Future<void> _escolherCor(Color atual, ValueChanged<Color> aplicar) async {
    EscopoDoEditor.of(context).playback.pause();
    final nova = await showColorPicker(
      context,
      initial: atual,
      onChanged: aplicar,
    );
    if (nova != null) aplicar(nova);
  }

  // -------------------------------------------------------------- forma

  List<Widget> _forma(
    ShapeLayer visivel,
    ShapeLayer gravada,
    Duration t,
    PlaybackController playback,
  ) {
    final sp = visivel.contents.whereType<ShapeParametric>().firstOrNull;
    final spGravada = gravada.contents.whereType<ShapeParametric>().firstOrNull;
    if (sp == null || spGravada == null) {
      return [
        const AureaAvisoDoPainel(
          texto:
              'Esta forma é um caminho desenhado. Converta para ajustar '
              'tamanho, cantos e pontas com keyframes, ou edite os pontos.',
        ),
        LinhaDeAcao(
          key: const ValueKey('forma-converter'),
          rotulo: 'Converter para paramétrica',
          icone: CupertinoIcons.arrow_2_squarepath,
          aoTocar: () {
            _c.convertShapeToParametric(_id);
            if (_c.shapeParametricOf(_id) == null) {
              AureaSnack.show(
                context,
                'Esta forma não tem equivalente paramétrico',
              );
            }
          },
        ),
        LinhaDePorta(
          rotulo: 'Editar pontos',
          icone: CupertinoIcons.scribble,
          aoTocar: () => abrirEditarPontosDaForma(context, ref, playback, _id),
        ),
      ];
    }
    final local = visivel.localTime(t);
    return [
      AureaPropertyRow.personalizada(
        rotulo: 'Tipo',
        chave: 'forma-tipo',
        filho: AureaDropdown<ParamShapeKind>(
          valor: sp.kind,
          opcoes: ParamShapeKind.values,
          rotuloDe: rotuloDoTipoDeForma,
          titulo: 'Tipo de forma',
          aoMudar: (k) => _c.setShapeParamKind(_id, k),
        ),
      ),
      for (final chave in parametrosDaForma(sp.kind)) ...[
        _parametro(sp, spGravada, chave, gravada, local, t, playback),
        if (chave == 'roundness')
          AureaPropertyRow.personalizada(
            rotulo: 'Canto em',
            chave: 'forma-canto-unidade',
            filho: FileiraDePilulas<bool>(
              chave: 'forma-canto',
              chaveDe: (pct) => pct ? 'porcento' : 'px',
              opcoes: const [true, false],
              atual: sp.roundnessPercent,
              rotuloDe: (pct) => pct ? '% do lado' : 'px',
              aoEscolher: (pct) => _c.setShapeRoundnessUnit(_id, percent: pct),
            ),
          ),
      ],
    ];
  }

  Widget _parametro(
    ShapeParametric sp,
    ShapeParametric spGravada,
    String chave,
    Layer gravada,
    Duration local,
    Duration t,
    PlaybackController playback,
  ) {
    final ficha = fichaDoParametroDaForma(chave, sp.kind);
    final trilha = shapeParamTrackOf(sp, chave);
    final max = chave == 'roundness' && sp.roundnessPercent
        ? 100.0
        : ficha.teto;
    return linhaNumerica(
      ref,
      rotulo: ficha.rotulo,
      chave: 'forma-$chave',
      valor: trilha?.valueAt(local) ?? 0,
      min: minimoDoParametroDaForma(chave),
      max: max,
      casas: chave == 'points' || chave == 'aperto' ? 1 : 0,
      unidade: _emGraus.contains(chave)
          ? '°'
          : (chave == 'roundness' && sp.roundnessPercent ? '%' : ''),
      losango: losangoDaTrilha(
        trilha: shapeParamTrackOf(spGravada, chave),
        gravada: gravada,
        t: t,
        playback: playback,
        aoAlternar: () => _c.toggleShapeParamKeyframe(_id, chave, t),
      ),
      aoMudar: (v) => _c.editShapeParam(_id, chave, t, v),
    );
  }

  // ---------------------------------------------------------------- cor

  List<Widget> _cor(ShapeLayer visivel) {
    final tipo = tipoDePreenchimentoDe(visivel.contents);
    final tipos = [
      TipoDePreenchimento.nenhum,
      TipoDePreenchimento.cor,
      TipoDePreenchimento.degrade,
      if (tipo == TipoDePreenchimento.midia) TipoDePreenchimento.midia,
    ];
    final cheio = visivel.contents.whereType<ShapeFill>().firstOrNull;
    final degrade = visivel.contents.whereType<ShapeGradientFill>().firstOrNull;
    // A COR PRIMARIA e a do primeiro fill OU traco da lista: so da para
    // usar `setShapePrimaryColor` no preenchimento quando ele vem antes.
    final itens = visivel.contents;
    final iCheio = cheio == null ? -1 : itens.indexOf(cheio);
    final iTraco = itens.indexWhere((i) => i is ShapeStroke);
    final corAlcancavel = cheio != null && (iTraco < 0 || iCheio < iTraco);
    return [
      AureaPropertyRow.personalizada(
        rotulo: 'Preencher',
        chave: 'forma-preencher',
        filho: FileiraDePilulas<TipoDePreenchimento>(
          chave: 'forma-preencher',
          chaveDe: (t) => t.name,
          opcoes: tipos,
          atual: tipo,
          rotuloDe: (t) => switch (t) {
            TipoDePreenchimento.nenhum => 'Nenhum',
            TipoDePreenchimento.cor => 'Cor',
            TipoDePreenchimento.degrade => 'Degradê',
            TipoDePreenchimento.midia => 'Mídia',
          },
          aoEscolher: (t) => _c.definirTipoDePreenchimento(_id, t),
        ),
      ),
      if (cheio != null && corAlcancavel)
        AureaPropertyRow.cor(
          rotulo: 'Cor',
          chave: 'forma-cor',
          cor: cheio.color,
          aoTocar: () => _escolherCor(
            cheio.color,
            (cor) => _c.setShapePrimaryColor(_id, cor),
          ),
        ),
      if (degrade != null) ...[
        AureaPropertyRow.cor(
          rotulo: 'Cor 1',
          chave: 'forma-degrade-1',
          cor: degrade.colorA,
          aoTocar: () => _escolherCor(
            degrade.colorA,
            (cor) => _c.updateShapeGradient(
              _id,
              degrade.id,
              (g) => g.copyWith(colorA: cor),
            ),
          ),
        ),
        AureaPropertyRow.cor(
          rotulo: 'Cor 2',
          chave: 'forma-degrade-2',
          cor: degrade.colorB,
          aoTocar: () => _escolherCor(
            degrade.colorB,
            (cor) => _c.updateShapeGradient(
              _id,
              degrade.id,
              (g) => g.copyWith(colorB: cor),
            ),
          ),
        ),
        linhaNumerica(
          ref,
          rotulo: 'Ângulo',
          chave: 'forma-degrade-angulo',
          valor: degrade.angleDeg,
          min: -360,
          max: 360,
          unidade: '°',
          aoMudar: (v) => _c.updateShapeGradient(
            _id,
            degrade.id,
            (g) => g.copyWith(angleDeg: v),
          ),
        ),
        linhaDeLigar(
          rotulo: 'Radial',
          chave: 'forma-degrade-radial',
          valor: degrade.radial,
          aoMudar: (v) => _c.updateShapeGradient(
            _id,
            degrade.id,
            (g) => g.copyWith(radial: v),
          ),
        ),
      ],
      if (tipo == TipoDePreenchimento.midia ||
          (cheio != null && !corAlcancavel))
        LinhaDePorta(
          rotulo: 'Mais opções de preenchimento',
          icone: CupertinoIcons.paintbrush,
          aoTocar: () => EscopoDoEditor.of(context).abrirPainel(PainelId.cor),
        ),
    ];
  }

  // -------------------------------------------------------------- traco

  List<Widget> _traco(
    ShapeLayer visivel,
    ShapeLayer gravada,
    Duration t,
    PlaybackController playback,
  ) {
    final traco = visivel.contents.whereType<ShapeStroke>().firstOrNull;
    final tracoGravado = gravada.contents.whereType<ShapeStroke>().firstOrNull;
    final ligar = linhaDeLigar(
      rotulo: 'Traço',
      chave: 'forma-traco',
      valor: traco != null,
      aoMudar: (v) => v ? _c.ensureShapeStroke(_id) : _c.removeShapeStroke(_id),
    );
    if (traco == null || tracoGravado == null) return [ligar];
    final local = visivel.localTime(t);
    Widget linha(
      String rotulo,
      String chave,
      AnimatedDouble visto,
      AnimatedDouble gravado, {
      required double min,
      required double max,
      double escala = 1,
      String unidade = '',
      int casas = 0,
    }) => linhaNumerica(
      ref,
      rotulo: rotulo,
      chave: 'forma-traco-$chave',
      valor: visto.valueAt(local) * escala,
      min: min,
      max: max,
      unidade: unidade,
      casas: casas,
      losango: losangoDaTrilha(
        trilha: gravado,
        gravada: gravada,
        t: t,
        playback: playback,
        aoAlternar: () =>
            _c.toggleShapeItemTrackKeyframe(_id, traco.id, chave, t),
      ),
      aoMudar: (v) =>
          _c.editShapeItemTrack(_id, traco.id, chave, t, v / escala),
    );
    return [
      ligar,
      AureaPropertyRow.cor(
        rotulo: 'Cor',
        chave: 'forma-traco-cor',
        cor: traco.color,
        aoTocar: () => _escolherCor(
          traco.color,
          (cor) => _c.updateShapeStroke(_id, (s) => s.copyWith(color: cor)),
        ),
      ),
      linha(
        'Espessura',
        'width',
        traco.width,
        tracoGravado.width,
        min: 0,
        max: 200,
        casas: 1,
      ),
      linha(
        'Opacidade',
        'opacity',
        traco.opacity,
        tracoGravado.opacity,
        min: 0,
        max: 100,
        escala: 100,
        unidade: '%',
      ),
      linha(
        'Tracejado',
        'dashLength',
        traco.dashLength,
        tracoGravado.dashLength,
        min: 0,
        max: 300,
      ),
      linha(
        'Vão',
        'gapLength',
        traco.gapLength,
        tracoGravado.gapLength,
        min: 0,
        max: 300,
      ),
    ];
  }

  // ----------------------------------------------------------- desenhar

  List<Widget> _desenhar(
    ShapeLayer visivel,
    ShapeLayer gravada,
    Duration t,
    PlaybackController playback,
  ) {
    final trim = visivel.contents.whereType<TrimOperator>().firstOrNull;
    final trimGravado = gravada.contents.whereType<TrimOperator>().firstOrNull;
    final ligar = linhaDeLigar(
      rotulo: 'Desenhar',
      chave: 'forma-desenhar',
      valor: trim != null,
      aoMudar: (v) => v ? _c.ensureShapeTrim(_id) : _c.removeShapeTrim(_id),
    );
    if (trim == null || trimGravado == null) {
      return [
        ligar,
        const AureaAvisoDoPainel(
          texto: 'O traço se desenha de um ponto a outro. Anime Início e Fim.',
        ),
      ];
    }
    final local = visivel.localTime(t);
    Widget linha(
      String rotulo,
      String chave,
      AnimatedDouble visto,
      AnimatedDouble gravado, {
      double min = 0,
    }) => linhaNumerica(
      ref,
      rotulo: rotulo,
      chave: 'forma-desenhar-$chave',
      valor: visto.valueAt(local) * 100,
      min: min,
      max: 100,
      unidade: '%',
      losango: losangoDaTrilha(
        trilha: gravado,
        gravada: gravada,
        t: t,
        playback: playback,
        aoAlternar: () =>
            _c.toggleShapeItemTrackKeyframe(_id, trim.id, chave, t),
      ),
      aoMudar: (v) => _c.editShapeItemTrack(_id, trim.id, chave, t, v / 100),
    );
    return [
      ligar,
      linha('Início', 'start', trim.start, trimGravado.start),
      linha('Fim', 'end', trim.end, trimGravado.end),
      linha(
        'Deslocamento',
        'offset',
        trim.offset,
        trimGravado.offset,
        min: -100,
      ),
    ];
  }
}
