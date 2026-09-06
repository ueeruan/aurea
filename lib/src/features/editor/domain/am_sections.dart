import 'layer.dart';

/// AS SECOES DA GRADE.
///
/// Regra da UI final: a interface tem tamanho fixo e as features entram
/// DENTRO dela. No maximo sete por tipo de camada — precisou de uma
/// oitava, alguma sai ou vira secao de tipo.
///
/// A visibilidade mora aqui, fora do widget, por dois motivos: e a unica
/// forma de CONTAR as secoes num teste (a regra "no maximo sete" so vale
/// se alguem conta), e e a unica forma de provar que nada aparece inerte
/// para um tipo de camada que nao usa aquilo.
///
/// Nao ha mais interruptor: o que existe no aplicativo esta no aplicativo.
/// O que decide o que aparece e o TIPO DA CAMADA, so.
enum AmSecao {
  moverTransformar,
  corPreenchimento,
  bordaSombra,
  mesclarOpacidade,
  // As duas do som. Nao sao uma oitava e uma nona secao na grade: sao
  // secoes DE TIPO DE CAMADA, a saida que a propria regra do minimalismo
  // preve. Nenhum tipo passa de sete.
  volume,
  fade,
  editarForma,
  /// O Modulo Grade do nulo — clonar em grade, circulo, esfera ou caminho.
  clonar,
  /// Texto: conteudo, fonte, animadores e texto em caminho.
  editarTexto,
  /// Legenda: o texto de cada fala e o estilo.
  editarLegendas,
  particulas,
  /// Cena 3D e Elemento 3D: objetos, materiais, luzes, cameras e cortes.
  cena3d,
  presets,
  efeitos,
}

/// O teto da grade PARA UM TIPO DE CAMADA. Nao e decoracao: o teste falha
/// se alguem passar disso.
const int kAmMaximoSecoes = 7;

/// AS SECOES QUE APARECEM PARA ESTA CAMADA.
///
/// O que nao se aplica ao tipo nao entra: um nulo nao tem cor, um som nao
/// tem posicao na tela, um video nao tem pontos para editar. O que nao
/// aparece nao e "escondido" — e que nao existe para aquele tipo.
Set<AmSecao> secoesDe(Layer layer) {
  // A CAMADA DE SOM nao tem posicao, nem opacidade, nem cor: mover um som
  // na tela nao faz nada, e um controle que nao faz nada nao pode
  // aparecer. Ela mostra tres secoes.
  if (layer is AudioLayer) {
    return const {AmSecao.volume, AmSecao.fade, AmSecao.efeitos};
  }
  // O NULO nao tem aparencia nenhuma. Tem o transform (que e para o que
  // ele existe) e a grade de clones que ele controla.
  if (layer is NullLayer) {
    return const {AmSecao.moverTransformar, AmSecao.clonar};
  }
  return {
    AmSecao.moverTransformar,
    if (layer is ShapeLayer ||
        layer is TextLayer ||
        layer is Element3DLayer ||
        layer is Scene3DLayer)
      AmSecao.corPreenchimento,
    AmSecao.bordaSombra,
    AmSecao.mesclarOpacidade,
    // Video com som ganha as duas do audio — e a razao de a grade do
    // video bater exatamente em sete, e nao em oito.
    if (layer is VideoLayer) ...[AmSecao.volume, AmSecao.fade],
    if (layer is ShapeLayer) AmSecao.editarForma,
    // OS EDITORES DE TIPO. Moravam todos dentro do menu "Mais" — um menu
    // escondido — e por isso pareciam nao existir. Cada um e a secao do
    // seu tipo, e nenhum tipo passa de sete.
    if (layer is TextLayer) AmSecao.editarTexto,
    if (layer is CaptionLayer) AmSecao.editarLegendas,
    if (layer is ParticlesLayer) AmSecao.particulas,
    if (layer is Element3DLayer || layer is Scene3DLayer) AmSecao.cena3d,
    AmSecao.presets,
    AmSecao.efeitos,
  };
}
