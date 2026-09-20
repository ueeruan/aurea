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

  /// Texto 3D: a letra, a fonte, o metal, a espessura e o chanfro.
  texto3d,

  /// Video: a porta da Cena 3D rastreada (motor 2.0) e os rastreios 2D.
  rastrear,

  /// Camera da composicao: a lente (zoom animavel).
  camera,
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
    return const {AmSecao.volume, AmSecao.efeitos};
  }
  // O NULO nao tem aparencia nenhuma. Tem o transform (que e para o que
  // ele existe) e a grade de clones que ele controla.
  if (layer is NullLayer) {
    return const {AmSecao.moverTransformar, AmSecao.clonar};
  }
  // A CENA 3D ficou so com o transform e os efeitos: o editor dela saiu
  // junto com o motor, e uma secao que abre uma folha que nao existe e
  // pior do que nao ter secao.
  if (layer is Scene3DLayer) {
    // A CAMADA DE TEXTO 3D ganha a folha do texto: e por ela que o dono
    // muda a palavra, a fonte e o metal DEPOIS de criar — antes disso,
    // "Texto 3D" era um botao que so sabia criar.
    final temTexto = layer.scene.nodes.any((n) => n.texto3d != null);
    return {
      AmSecao.moverTransformar,
      if (temTexto) AmSecao.texto3d,
      // A CENA VOLTOU A TER EDITOR (`cena3d_sheet.dart`): ambiente, reflexo,
      // luz e o material de cada objeto importado.
      AmSecao.cena3d,
      AmSecao.efeitos,
    };
  }
  // A CAMERA DA COMPOSICAO: transform (posicao, giro 3D, ponto de
  // interesse pelo proprio palco) e a lente. Cor, borda e mescla nao
  // significam nada numa camera.
  if (layer is CameraLayer) {
    return const {AmSecao.moverTransformar, AmSecao.camera};
  }
  return {
    AmSecao.moverTransformar,
    if (layer is ShapeLayer ||
        layer is TextLayer ||
        layer is Element3DLayer)
      AmSecao.corPreenchimento,
    AmSecao.bordaSombra,
    AmSecao.mesclarOpacidade,
    // Video com som ganha as duas do audio — e a razao de a grade do
    // video bater exatamente em sete, e nao em oito.
    if (layer is VideoLayer) AmSecao.volume,
    // A PORTA DA CENA 3D RASTREADA: rastrear a camera do clipe e povoar
    // o espaco com objetos, texto e nulos.
    if (layer is VideoLayer) AmSecao.rastrear,
    if (layer is ShapeLayer) AmSecao.editarForma,
    // OS EDITORES DE TIPO. Moravam todos dentro do menu "Mais" — um menu
    // escondido — e por isso pareciam nao existir. Cada um e a secao do
    // seu tipo, e nenhum tipo passa de sete.
    if (layer is TextLayer) AmSecao.editarTexto,
    if (layer is CaptionLayer) AmSecao.editarLegendas,
    if (layer is ParticulasLayer) AmSecao.particulas,
    if (layer is Element3DLayer || layer is Scene3DLayer) AmSecao.cena3d,
    // SEM "PRESETS" NA GRADE (relato do beta 1.0.5): o quadrado com o selo
    // NEW vinha antes de "Efeitos" e empurrava os efeitos para uma fileira
    // que so se via rolando.
    AmSecao.efeitos,
  };
}
