import 'dart:math' as math;

import 'limites_de_importacao.dart';
import 'model_asset3d.dart';
import 'texturas_importadas.dart';

/// A PARTIR DE QUANTO UM MODELO E "PESADO".
///
/// Nao e o limite de recusa ([LimitesDeImportacao], que barra o que derruba
/// o app): e o ponto em que vale PERGUNTAR ao dono se ele quer a versao
/// reduzida. Abaixo disso a importacao segue calada — o aviso antigo
/// aparecia ate para um cubo, e aviso que aparece sempre ninguem le.
///
/// NUMEROS PROVISORIOS, nenhum medido em aparelho. A ordem de grandeza vem
/// do que o projeto ja mediu: ~5 ms por mil triangulos no pintor de CPU e o
/// teto de 1024 px com que toda textura de modelo e desenhada.
class LimitesDeModeloPesado {
  const LimitesDeModeloPesado({
    this.triangulos = 150000,
    this.ladoDaTextura = 2048,
    this.pixelsDasTexturas = 32 * 1000 * 1000,
    this.memoria = 150 * 1024 * 1024,
    this.arquivo = 80 * 1024 * 1024,
  });

  final int triangulos;

  /// Lado maior de UMA textura. Acima de 2048 o arquivo guarda dezesseis
  /// vezes os pixels que o app mostra.
  final int ladoDaTextura;
  final int pixelsDasTexturas;
  final int memoria;
  final int arquivo;
}

const limitesDeModeloPesado = LimitesDeModeloPesado();

/// O que "Otimizar automaticamente" entrega: a malha perto deste numero de
/// triangulos e as texturas no lado com que o app as desenha.
const alvoDeTriangulosOtimizado = 150000;

/// Por que o modelo foi considerado pesado (pode haver mais de um motivo).
enum MotivoDoPeso { triangulos, textura, pixels, memoria, arquivo }

/// A FICHA DE UM MODELO: o que ele custa, em numeros que o dono entende.
///
/// So inteiros, de proposito: a ficha atravessa a fronteira do isolate da
/// importacao dentro de uma excecao, e a mesma ficha serve para o que a
/// busca do Sketchfab DECLARA antes do download (la nao ha modelo lido,
/// so contagens).
class AnaliseDoModelo {
  const AnaliseDoModelo({
    this.triangulos = 0,
    this.vertices = 0,
    this.texturas = 0,
    this.maiorTextura = 0,
    this.pixelsDasTexturas = 0,
    this.materiais = 0,
    this.animacoes = 0,
    this.ossos = 0,
    this.memoriaDaMalha = 0,
    this.memoriaDasTexturas = 0,
    this.arquivoBytes = 0,
  });

  /// Le a ficha de um modelo ja importado. [arquivoBytes] e o tamanho no
  /// disco (modelo + complementares), quando quem chama sabe.
  ///
  /// Conta os CINCO mapas de cada material, e cada imagem uma vez so: o
  /// mesmo `data:` URI em dez materiais e uma textura, nao dez. As
  /// dimensoes saem do cabecalho — nenhuma imagem e decodificada aqui.
  factory AnaliseDoModelo.doModelo(ModelAsset3D asset, {int arquivoBytes = 0}) {
    final data = asset.data;
    final c = contarModelo(data);
    final vistas = <String>{};
    var maior = 0, pixels = 0, bytesDeTextura = 0, bytesJaContados = 0;
    final materiais = data['materials'];
    final lista = materiais is List ? materiais : const [];
    for (final m in lista) {
      if (m is! Map) continue;
      // O `estimatedBytes` do modelo ja soma a textura de cor de CADA
      // material (repetida ou nao). Tira-se essa parcela para a memoria de
      // texturas ser contada uma vez, aqui, com os cinco mapas.
      final cor = m['image'];
      if (cor is String) bytesJaContados += cor.length;
      for (final chave in mapasDoMaterial) {
        final uri = m[chave];
        if (uri is! String || uri.isEmpty || !vistas.add(uri)) continue;
        bytesDeTextura += uri.length;
        final bytes = bytesDoDataUri(uri);
        if (bytes == null) continue;
        final d = dimensoesDaImagem(bytes);
        if (d == null) continue;
        maior = math.max(maior, math.max(d.largura, d.altura));
        pixels += d.largura * d.altura;
      }
    }
    var ossos = 0;
    try {
      ossos = asset.joints.length;
    } catch (_) {
      // Skin sem a lista de ossos: a ficha informa, nao valida.
    }
    return AnaliseDoModelo(
      triangulos: c.triangulos,
      vertices: c.vertices,
      texturas: vistas.length,
      maiorTextura: maior,
      pixelsDasTexturas: pixels,
      materiais: lista.length,
      animacoes: asset.clips.length,
      ossos: ossos,
      memoriaDaMalha: math.max(0, asset.estimatedBytes - bytesJaContados),
      memoriaDasTexturas: bytesDeTextura,
      arquivoBytes: arquivoBytes,
    );
  }

  final int triangulos;
  final int vertices;

  /// Imagens DISTINTAS, somando os cinco mapas de todos os materiais.
  final int texturas;

  /// Lado maior da maior textura, em px (0 = nenhuma medida).
  final int maiorTextura;
  final int pixelsDasTexturas;
  final int materiais;
  final int animacoes;
  final int ossos;

  /// Geometria, niveis de detalhe e animacao, como ficam guardados.
  final int memoriaDaMalha;

  /// As imagens como ficam guardadas (o texto base64 dos `data:` URI).
  final int memoriaDasTexturas;

  /// Tamanho no disco do que foi escolhido (0 = desconhecido).
  final int arquivoBytes;

  int get memoriaBytes => memoriaDaMalha + memoriaDasTexturas;

  Set<MotivoDoPeso> motivos([
    LimitesDeModeloPesado limites = limitesDeModeloPesado,
  ]) => {
    if (triangulos > limites.triangulos) MotivoDoPeso.triangulos,
    if (maiorTextura > limites.ladoDaTextura) MotivoDoPeso.textura,
    if (pixelsDasTexturas > limites.pixelsDasTexturas) MotivoDoPeso.pixels,
    if (memoriaBytes > limites.memoria) MotivoDoPeso.memoria,
    if (arquivoBytes > limites.arquivo) MotivoDoPeso.arquivo,
  };

  bool pesado([LimitesDeModeloPesado limites = limitesDeModeloPesado]) =>
      motivos(limites).isNotEmpty;

  /// O "DEPOIS" DO AVISO: como o modelo deve ficar com a otimizacao
  /// automatica. E ESTIMATIVA — a malha de pecas soltas para antes do alvo,
  /// e a textura menor que o teto nao encolhe. O numero exato sai de
  /// [AnaliseDoModelo.doModelo] sobre o modelo ja otimizado.
  ///
  /// Animacoes, ossos e materiais nao mudam: a otimizacao nao toca neles.
  AnaliseDoModelo estimativaOtimizada({
    int alvoDeTriangulos = alvoDeTriangulosOtimizado,
    int ladoDaTextura = ladoDaTexturaOtimizada,
  }) {
    final malha = triangulos > alvoDeTriangulos && triangulos > 0
        ? alvoDeTriangulos / triangulos
        : 1.0;
    final lado = maiorTextura > ladoDaTextura && maiorTextura > 0
        ? ladoDaTextura / maiorTextura
        : 1.0;
    return AnaliseDoModelo(
      triangulos: (triangulos * malha).round(),
      vertices: (vertices * malha).round(),
      texturas: texturas,
      maiorTextura: math.min(maiorTextura, math.max(ladoDaTextura, 0)),
      pixelsDasTexturas: (pixelsDasTexturas * lado * lado).round(),
      materiais: materiais,
      animacoes: animacoes,
      ossos: ossos,
      memoriaDaMalha: (memoriaDaMalha * malha).round(),
      memoriaDasTexturas: (memoriaDasTexturas * lado * lado).round(),
      arquivoBytes: arquivoBytes,
    );
  }

  @override
  String toString() =>
      'AnaliseDoModelo($triangulos tri, $vertices vert, $texturas tex ate '
      '$maiorTextura px, $materiais mat, $animacoes anim, $ossos ossos, '
      '${memoriaLegivel(memoriaBytes)})';
}

/// "1,2 milhão", "150 mil", "980" — contagem para ler de relance.
String contagemLegivel(int n) {
  // Em DECIMOS inteiros: "1,96 milhao" arredonda para "2 milhões", e nao
  // para "2,0 milhão".
  String comUmaCasa(int decimos) => decimos % 10 == 0
      ? '${decimos ~/ 10}'
      : '${decimos ~/ 10},${decimos % 10}';
  if (n >= 999500) {
    if (n >= 10000000) return '${(n / 1000000).round()} milhões';
    final decimos = (n / 100000).round();
    return '${comUmaCasa(decimos)} ${decimos >= 20 ? 'milhões' : 'milhão'}';
  }
  if (n >= 10000) return '${(n / 1000).round()} mil';
  if (n >= 1000) return '${comUmaCasa((n / 100).round())} mil';
  return '$n';
}

/// "12 MB", "1,4 GB", "300 KB".
String memoriaLegivel(int bytes) {
  const kb = 1024, mb = kb * 1024, gb = mb * 1024;
  if (bytes >= gb) {
    return '${(bytes / gb).toStringAsFixed(1).replaceAll('.', ',')} GB';
  }
  if (bytes >= mb) return '${(bytes / mb).round()} MB';
  if (bytes >= kb) return '${(bytes / kb).round()} KB';
  return '$bytes B';
}
