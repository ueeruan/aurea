/// O ANIMADOR DE TEXTO COMO EFEITO.
///
/// ==========================================================================
/// POR QUE ESTE ARQUIVO EXISTE
/// ==========================================================================
///
/// O animador manual tinha painel proprio, com posicoes (entrada/enfase/
/// saida), grade de miniaturas, modo avancado e um catalogo de seletores a
/// montar na mao. Era um editor de desktop dentro do celular, e o dono
/// resumiu: "praticamente impossivel de usar".
///
/// Aqui ele vira UM EFEITO COMUM — Selecionar Texto → Efeitos → Texto →
/// Animador de Texto. Aparece na pilha ao lado de Glow e Blur, com o mesmo
/// cartao que abre e fecha, as mesmas linhas de parametro e o mesmo losango
/// de keyframe.
///
/// ==========================================================================
/// UM MOTOR SO
/// ==========================================================================
///
/// Nada aqui inventa matematica. Tudo sai de [TextAnimator] +
/// [RangeSelector], que ja existiam e ja sao serializados, desenhados e
/// exportados. Este arquivo so faz duas coisas:
///
///   1. [animadorDaReceita] — o UNICO construtor de animador de texto.
///      Os dez presets sao dez [ReceitaDoAnimador] diferentes passando
///      por ele; nao existe (e nao pode existir) uma implementacao por
///      preset.
///   2. As leituras de volta ([faixaDoAnimador], [propriedadeDoAnimador]),
///      para o cartao mostrar o que ja esta guardado.
///
/// ==========================================================================
/// A CONTA DO OFFSET
/// ==========================================================================
///
/// O seletor de faixa cobre as unidades cujo `p = (i+0.5)/n` cai entre
/// `start+offset` e `end+offset`. Com a janela cheia (start 0, end 100) o
/// offset varre a frase inteira:
///
///   offset -100% → janela em [-1, 0]: NENHUMA unidade coberta;
///   offset    0% → janela em [ 0, 1]: TODAS cobertas;
///   offset +100% → janela em [ 1, 2]: nenhuma de novo.
///
/// E por isso que "Offset de -100 a 100 faz a animacao percorrer as
/// letras": a cobertura de cada unidade sobe e desce na vez dela. Quem
/// anima e o offset; a forma decide o perfil da onda que passa.
///
/// A INTERFACE FALA EM PORCENTAGEM (0..100, -100..100) e o motor guarda
/// fracao (0..1). A conversao mora aqui — [porcentoParaFracao] e
/// [fracaoParaPorcento] — para nao haver duas versoes dela na tela.
library;

import 'keyframe.dart';
import 'text_animator.dart';

/// O nome do efeito, na pilha e na galeria.
const nomeDoAnimadorDeTexto = 'Animador de Texto';

/// As unidades que o cartao oferece: caracteres, palavras, linhas.
///
/// `charactersNoSpaces` continua no motor (projetos antigos usam), mas
/// nao entra na fileira: "caracteres sem espacos" e uma distincao que so
/// faz sentido depois de a pessoa ja ter errado uma vez.
const unidadesDoAnimador = <SelectorBasedOn>[
  SelectorBasedOn.characters,
  SelectorBasedOn.words,
  SelectorBasedOn.lines,
];

String rotuloDaUnidade(SelectorBasedOn u) => switch (u) {
  SelectorBasedOn.characters => 'Caracteres',
  SelectorBasedOn.charactersNoSpaces => 'Caracteres',
  SelectorBasedOn.words => 'Palavras',
  SelectorBasedOn.lines => 'Linhas',
};

/// As seis formas da onda, com os nomes que o dono pediu (os mesmos do
/// After Effects — quem vem de la reconhece sem traduzir).
const formasDoAnimador = SelectorShape.values;

String rotuloDaForma(SelectorShape s) => switch (s) {
  SelectorShape.square => 'Square',
  SelectorShape.rampUp => 'Ramp Up',
  SelectorShape.rampDown => 'Ramp Down',
  SelectorShape.triangle => 'Triangle',
  SelectorShape.round => 'Round',
  SelectorShape.smooth => 'Smooth',
};

/// As propriedades animaveis do cartao, na ordem em que aparecem.
const propriedadesDoAnimador = <TextAnimProp>[
  TextAnimProp.positionX,
  TextAnimProp.positionY,
  TextAnimProp.scale,
  TextAnimProp.rotation,
  TextAnimProp.opacity,
  TextAnimProp.tracking,
  TextAnimProp.blur,
];

/// Faixa de arrasto de cada propriedade no cartao: (minimo, maximo).
(double, double) faixaDaPropriedade(TextAnimProp p) => switch (p) {
  TextAnimProp.positionX || TextAnimProp.positionY => (-1000, 1000),
  TextAnimProp.scale => (0, 400),
  TextAnimProp.rotation => (-720, 720),
  TextAnimProp.opacity => (0, 100),
  TextAnimProp.tracking => (-100, 200),
  TextAnimProp.blur => (0, 60),
  _ => (-1000, 1000),
};

String unidadeDaPropriedade(TextAnimProp p) => switch (p) {
  TextAnimProp.scale || TextAnimProp.opacity => '%',
  TextAnimProp.rotation => '°',
  _ => '',
};

/// O VALOR QUE NAO MUDA NADA. Escala e opacidade sao multiplicativas
/// (neutro 100); o resto e aditivo (neutro 0). A conta e a mesma de
/// [AnimatorProperty], mas sem construir um objeto — isto roda por
/// propriedade e por quadro.
double neutroDaPropriedade(TextAnimProp p) => switch (p) {
  TextAnimProp.scale ||
  TextAnimProp.opacity ||
  TextAnimProp.scaleX ||
  TextAnimProp.scaleY ||
  TextAnimProp.saturation ||
  TextAnimProp.brightness => 100,
  _ => 0,
};

double porcentoParaFracao(double p) => p / 100;
double fracaoParaPorcento(double f) => f * 100;

/// A RECEITA DE UM ANIMADOR — so numeros, nenhuma conta.
///
/// Um preset E uma receita. Nada mais: se um preset precisasse de codigo
/// proprio, ele deixaria de passar pelo mesmo motor e a proxima mudanca
/// no motor o deixaria para tras.
class ReceitaDoAnimador {
  const ReceitaDoAnimador({
    this.unidade = SelectorBasedOn.characters,
    this.forma = SelectorShape.rampUp,
    this.start = 0,
    this.end = 100,
    this.offset = 0,
    this.varredura = const Duration(milliseconds: 1000),
    this.suavidade = 100,
    this.easeHigh = 0,
    this.easeLow = 0,
    this.posicaoX = 0,
    this.posicaoY = 0,
    this.escala = 100,
    this.rotacao = 0,
    this.opacidade = 100,
    this.espacamento = 0,
    this.desfoque = 0,
  });

  final SelectorBasedOn unidade;
  final SelectorShape forma;

  /// Em PORCENTAGEM, como na tela: start/end 0..100, offset -100..100.
  final double start;
  final double end;
  final double offset;

  /// Quanto tempo o offset leva para atravessar a frase. Zero = o offset
  /// fica parado no valor de [offset] (sem keyframe nenhum).
  final Duration varredura;

  /// So vale na forma Square: 0 = degrau seco (maquina de escrever).
  final double suavidade;

  /// -100..100, os dois pesos da curva do seletor.
  final double easeHigh;
  final double easeLow;

  /// O estado DE PARTIDA de cada unidade (cobertura cheia). Neutro =
  /// 0 para os aditivos, 100 para os multiplicativos.
  final double posicaoX;
  final double posicaoY;
  final double escala;
  final double rotacao;
  final double opacidade;
  final double espacamento;
  final double desfoque;

  /// O valor desta receita para a propriedade [p].
  double valorDe(TextAnimProp p) => switch (p) {
    TextAnimProp.positionX => posicaoX,
    TextAnimProp.positionY => posicaoY,
    TextAnimProp.scale => escala,
    TextAnimProp.rotation => rotacao,
    TextAnimProp.opacity => opacidade,
    TextAnimProp.tracking => espacamento,
    TextAnimProp.blur => desfoque,
    _ => 0,
  };
}

/// O UNICO CONSTRUTOR DE ANIMADOR DE TEXTO.
///
/// Um seletor de faixa (a janela que anda) e uma propriedade por valor
/// que nao esta no neutro. Propriedade no neutro nao entra: um animador
/// com sete propriedades neutras custa sete contas por unidade e por
/// quadro para nao mudar nada.
TextAnimator animadorDaReceita(
  ReceitaDoAnimador r, {
  String? id,
  String nome = nomeDoAnimadorDeTexto,
  bool enabled = true,
}) {
  final varre = r.varredura > Duration.zero;
  final offset = varre
      ? (AnimatedDouble(porcentoParaFracao(r.offset))
            .withKeyframe(Duration.zero, 0, Easing.linear)
            .withKeyframe(r.varredura, 1))
      : AnimatedDouble(porcentoParaFracao(r.offset));

  return TextAnimator(
    id: id,
    name: nome,
    enabled: enabled,
    selectors: [
      RangeSelector(
        basedOn: r.unidade,
        shape: r.forma,
        start: AnimatedDouble(porcentoParaFracao(r.start)),
        end: AnimatedDouble(porcentoParaFracao(r.end)),
        offset: offset,
        smoothness: AnimatedDouble(r.suavidade),
        easeHigh: AnimatedDouble(r.easeHigh),
        easeLow: AnimatedDouble(r.easeLow),
      ),
    ],
    properties: [
      for (final p in propriedadesDoAnimador)
        if (r.valorDe(p) != neutroDaPropriedade(p))
          AnimatorProperty(type: p, value: AnimatedDouble(r.valorDe(p))),
    ],
  );
}

/// UM PRESET E UMA RECEITA COM NOME. Nao ha um bit de codigo por preset.
class PresetDoAnimador {
  const PresetDoAnimador({
    required this.id,
    required this.nome,
    required this.receita,
  });

  final String id;
  final String nome;
  final ReceitaDoAnimador receita;

  TextAnimator construir({String? id}) =>
      animadorDaReceita(receita, id: id, nome: nome);
}

/// OS DEZ PRESETS pedidos — todos pelo mesmo caminho, so mudando valores.
const presetsDoAnimador = <PresetDoAnimador>[
  PresetDoAnimador(
    id: 'palavra',
    nome: 'Palavra por palavra',
    receita: ReceitaDoAnimador(
      unidade: SelectorBasedOn.words,
      forma: SelectorShape.square,
      suavidade: 0,
      varredura: Duration(milliseconds: 1200),
      opacidade: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'letra',
    nome: 'Letra por letra',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.rampUp,
      varredura: Duration(milliseconds: 1000),
      opacidade: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'pop',
    nome: 'Pop',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.rampUp,
      varredura: Duration(milliseconds: 900),
      easeHigh: 60,
      escala: 0,
      opacidade: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'bounce',
    nome: 'Bounce',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.triangle,
      varredura: Duration(milliseconds: 1100),
      easeLow: 70,
      posicaoY: 90,
      rotacao: -14,
    ),
  ),
  PresetDoAnimador(
    id: 'fade_up',
    nome: 'Fade Up',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.rampUp,
      varredura: Duration(milliseconds: 1000),
      posicaoY: 70,
      opacidade: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'fade_down',
    nome: 'Fade Down',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.rampUp,
      varredura: Duration(milliseconds: 1000),
      posicaoY: -70,
      opacidade: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'slide_left',
    nome: 'Slide Left',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.rampUp,
      varredura: Duration(milliseconds: 950),
      posicaoX: 140,
      opacidade: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'slide_right',
    nome: 'Slide Right',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.rampUp,
      varredura: Duration(milliseconds: 950),
      posicaoX: -140,
      opacidade: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'scale_in',
    nome: 'Scale In',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.round,
      varredura: Duration(milliseconds: 900),
      easeHigh: 50,
      escala: 0,
    ),
  ),
  PresetDoAnimador(
    id: 'typewriter',
    nome: 'Typewriter',
    receita: ReceitaDoAnimador(
      forma: SelectorShape.square,
      suavidade: 0,
      varredura: Duration(milliseconds: 1600),
      opacidade: 0,
    ),
  ),
];

/// O preset com que o efeito nasce ao ser aplicado.
const presetPadraoDoAnimador = 'letra';

ReceitaDoAnimador get receitaPadraoDoAnimador => presetsDoAnimador
    .firstWhere((p) => p.id == presetPadraoDoAnimador)
    .receita;

// ------------------------------------------------------------ leituras

/// A faixa (o seletor que o cartao edita) deste animador, quando ha.
RangeSelector? faixaDoAnimador(TextAnimator a) {
  for (final s in a.selectors) {
    if (s is RangeSelector) return s;
  }
  return null;
}

/// A propriedade [tipo] deste animador, quando ja existe.
AnimatorProperty? propriedadeDoAnimador(TextAnimator a, TextAnimProp tipo) {
  for (final p in a.properties) {
    if (p.type == tipo) return p;
  }
  return null;
}

/// O valor da propriedade [tipo] no instante [t] — o neutro quando ela
/// ainda nao foi criada, para a linha nascer mostrando a verdade.
double valorDaPropriedade(TextAnimator a, TextAnimProp tipo, Duration t) =>
    propriedadeDoAnimador(a, tipo)?.value.valueAt(t) ??
    neutroDaPropriedade(tipo);

/// ESTE ANIMADOR E O EFEITO "Animador de Texto"?
///
/// O que define e ter uma faixa: e ela que o cartao edita. Animador
/// compilado do catalogo antigo (seletor escalonado) e animador de
/// wiggle ficam de fora — o cartao nao teria o que mostrar.
bool ehAnimadorDeTexto(TextAnimator a) => faixaDoAnimador(a) != null;
