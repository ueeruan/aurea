import 'dart:convert';
import 'dart:typed_data';

import 'package:image/image.dart' as img;

import 'limites_de_importacao.dart';

/// Os cinco mapas que um material importado pode carregar, cada um como
/// `data:` URI dentro de `data['materials'][i]`.
const mapasDoMaterial = [
  'image',
  'normalImage',
  'metalRoughImage',
  'emissiveImage',
  'occlusionImage',
];

/// Lado maximo com que o app DESENHA uma textura de modelo. E o mesmo teto
/// do `TextureCache` (a imagem e decodificada a 1024 no lado maior, tanto
/// para o pintor quanto para a placa): guardar mais que isso e so peso.
const ladoDaTexturaOtimizada = 1024;

/// Os bytes de um `data:` URI em base64, ou nulo quando nao e um.
Uint8List? bytesDoDataUri(Object? uri) {
  if (uri is! String || !uri.startsWith('data:')) return null;
  final virgula = uri.indexOf(',');
  if (virgula < 0) return null;
  try {
    return base64Decode(uri.substring(virgula + 1));
  } on FormatException {
    return null;
  }
}

/// REDUZ AS TEXTURAS DO MODELO para no maximo [lado] px no lado maior.
///
/// Roda no isolate da importacao, junto com a solda e os niveis de detalhe.
/// Como o app nunca desenha textura de modelo acima de 1024 px, reduzir
/// para esse lado NAO muda o que aparece na tela — muda o que fica
/// guardado: o arquivo de peso do projeto, a memoria do base64 e o tempo de
/// cada decodificacao. Um mapa 4096 em PNG sao dezenas de MB de texto que
/// o aparelho carrega para sempre e nunca mostra.
///
///  * a MESMA imagem usada por varios materiais converte uma vez so;
///  * imagem ja pequena, ou de formato que o pacote nao le, fica como veio
///    — reduzir e economia, nunca motivo para o modelo nao entrar;
///  * com transparencia de verdade sai PNG; o relevo que nao era JPEG
///    continua sem perda (bloco de JPEG em normal vira risco na luz); o
///    resto sai JPEG 90.
///
/// Devolve quantas imagens distintas foram reduzidas.
int reduzirTexturasDoModelo(
  Map<String, dynamic> data, {
  int lado = ladoDaTexturaOtimizada,
}) {
  if (lado < 1) return 0;
  final materiais = data['materials'];
  if (materiais is! List) return 0;
  // URI original -> URI novo (ou o mesmo, quando nao havia o que fazer).
  final feitos = <String, String>{};
  var reduzidas = 0;
  for (final m in materiais) {
    if (m is! Map) continue;
    for (final chave in mapasDoMaterial) {
      final uri = m[chave];
      if (uri is! String || !uri.startsWith('data:')) continue;
      final pronto = feitos[uri];
      if (pronto != null) {
        m[chave] = pronto;
        continue;
      }
      String novo = uri;
      try {
        novo = _reduzir(uri, lado, relevo: chave == 'normalImage') ?? uri;
      } catch (_) {
        // Imagem que o pacote nao decodifica: fica a original. O
        // `TextureCache` (codec da plataforma) ainda pode abri-la.
      }
      if (!identical(novo, uri)) reduzidas++;
      feitos[uri] = novo;
      m[chave] = novo;
    }
  }
  return reduzidas;
}

String? _reduzir(String uri, int lado, {required bool relevo}) {
  final bytes = bytesDoDataUri(uri);
  if (bytes == null) return null;
  // PELO CABECALHO PRIMEIRO: a imagem que ja cabe nao e decodificada.
  final d = dimensoesDaImagem(bytes);
  if (d != null && d.largura <= lado && d.altura <= lado) return null;
  final lida = img.decodeImage(bytes);
  if (lida == null) return null;
  if (lida.width <= lado && lida.height <= lado) return null;
  // PNG de paleta ou de 16 bits: a media entre vizinhos e o JPEG trabalham
  // em 8 bits por canal, com a cor de verdade e nao o indice dela.
  final original = lida.hasPalette || lida.format != img.Format.uint8
      ? lida.convert(format: img.Format.uint8, numChannels: lida.numChannels)
      : lida;
  final escala = lado / (original.width > original.height
      ? original.width
      : original.height);
  final largura = (original.width * escala).round().clamp(1, lado);
  final altura = (original.height * escala).round().clamp(1, lado);
  // MEDIA, e nao vizinho nem cubica: reduzir quatro vezes com amostra
  // pontual serrilha a textura inteira.
  final menor = img.copyResize(
    original,
    width: largura,
    height: altura,
    interpolation: img.Interpolation.average,
  );
  final eraJpeg = bytes.length > 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
  if (_temTransparencia(menor) || (relevo && !eraJpeg)) {
    return 'data:image/png;base64,${base64Encode(img.encodePng(menor))}';
  }
  // O JPEG nao tem canal alfa: tira-lo antes evita depender de como o
  // codificador trata um quarto canal que e todo opaco.
  final semAlfa = menor.numChannels > 3
      ? menor.convert(numChannels: 3)
      : menor;
  return 'data:image/jpeg;base64,'
      '${base64Encode(img.encodeJpg(semAlfa, quality: 90))}';
}

bool _temTransparencia(img.Image imagem) {
  if (!imagem.hasAlpha) return false;
  final opaco = imagem.maxChannelValue;
  for (final p in imagem) {
    if (p.a < opaco) return true;
  }
  return false;
}
