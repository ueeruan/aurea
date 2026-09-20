import 'dart:math' as math;

import '../../domain/orcamento_render.dart';

/// AS CONTAS DO PREVIEW EM RASCUNHO — puras, sem widget, para o teste
/// prender cada uma sem montar o palco.
///
/// ============================ POR QUE EXISTE ==========================
///
/// O palco baixava a qualidade so ENQUANTO TOCA. Arrastar um slider, mover
/// uma camada ou esfregar a timeline com o relogio parado rodava em
/// qualidade cheia — e e durante a edicao que o celular esquenta. Agora o
/// rascunho vale tambem com o dedo no comando (`Interacao.agora`), e a
/// exportacao nunca entra nele: la a qualidade e o unico criterio.

/// O PREVIEW ESTA EM RASCUNHO? Exportando, NUNCA — aconteca o que
/// acontecer com o relogio ou com o dedo.
bool emRascunho({
  required bool exporting,
  required bool tocando,
  required bool interagindo,
}) =>
    !exporting && (tocando || interagindo);

/// A RESOLUCAO DAS FOTOS DE EFEITO enquanto se interage: teto menor e
/// metade da escala do palco. Parado, tudo volta ao que era.
({double escalaDoPalco, double tetoPx}) fotosDoPreview({
  required double escalaDoPalco,
  required bool tocando,
  required bool interagindo,
}) =>
    (
      escalaDoPalco: interagindo ? escalaDoPalco * .5 : escalaDoPalco,
      tetoPx: (tocando || interagindo) ? 1080 : 2160,
    );

/// O ALVO DA CENA 3D NO PREVIEW.
///
/// ============================ O QUE MUDOU ==============================
///
/// O 3D desenhava sempre no alvo da receita (608x1080 numa composicao
/// 1080x1920, com MSAA 4x), acima do tamanho FISICO do palco (uns 500x900
/// px num celular) e ignorando a "Resolucao da previa" do menu. Agora o
/// alvo e o menor entre a receita e o que o palco mostra de verdade
/// ([escalaFisica] = pixels fisicos por pixel logico da composicao, que ja
/// carrega a escala do palco, o DPR e a resolucao escolhida).
///
/// ARREDONDADO A MULTIPLOS DE 64 NO LADO MAIOR, COM HISTERESE: trocar o
/// tamanho do alvo recria cor, profundidade, MSAA e staging no motor —
/// pico de memoria e engasgo. Uma pinca de zoom muda a escala a cada
/// quadro; sem esta folga o motor recriaria os alvos dezenas de vezes por
/// gesto. So uma diferenca de mais de [histerese] no lado maior troca o
/// alvo. Com [segurar] (o dedo esta mexendo em algo), o alvo anterior fica
/// ate soltar.
({int largura, int altura}) alvo3DDoPreview({
  required double compLargura,
  required double compAltura,
  required ReceitaDeQualidade receita,
  required double escalaFisica,
  ({int largura, int altura})? anterior,
  bool segurar = false,
  double histerese = .2,
  int passo = 64,
}) {
  if (!compLargura.isFinite ||
      !compAltura.isFinite ||
      compLargura <= 0 ||
      compAltura <= 0) {
    return (largura: 0, altura: 0);
  }
  final temAnterior =
      anterior != null && anterior.largura > 0 && anterior.altura > 0;
  if (segurar && temAnterior) return anterior;

  final fisica = escalaFisica.isFinite && escalaFisica > 0 ? escalaFisica : 1.0;
  final escala = math.min(
    escalaDoPreview(compLargura, compAltura, receita),
    fisica,
  );
  final maior = math.max(compLargura, compAltura) * escala;
  // O lado maior sobe ao multiplo de 64 seguinte (nunca abaixo de 64) e o
  // outro segue a proporcao da composicao.
  final maiorArredondado = math.max(passo, (maior / passo).ceil() * passo);
  final fator = maiorArredondado / math.max(compLargura, compAltura);
  final novo = (
    largura: math.max(1, (compLargura * fator).round()),
    altura: math.max(1, (compAltura * fator).round()),
  );
  if (!temAnterior) return novo;

  final ladoNovo = math.max(novo.largura, novo.altura);
  final ladoAnterior = math.max(anterior.largura, anterior.altura);
  final diferenca = (ladoNovo - ladoAnterior).abs() / ladoAnterior;
  return diferenca < histerese ? anterior : novo;
}

/// A QUALIDADE 3D DURANTE A INTERACAO: a sombra desce um nivel (0 = sem
/// sombra). O antisserrilhado NAO muda aqui de proposito: no motor a
/// contagem de amostras faz parte do alvo (`alvo_amostras == amostras` na
/// conferencia do `renderizador_3d.cpp`), entao trocar MSAA 4 -> 1 recria
/// os alvos de cor e MSAA — o mesmo pico que a histerese do tamanho existe
/// para evitar. O nivel da sombra troca so o mapa de sombra, que e
/// pequeno, e tira um passe inteiro do quadro.
({int sombra, int amostras}) qualidade3DDoPreview({
  required int sombra,
  required int amostras,
  required bool interagindo,
}) =>
    (
      sombra: interagindo ? math.max(0, sombra - 1) : sombra,
      amostras: amostras,
    );
