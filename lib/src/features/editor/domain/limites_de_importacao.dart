import 'dart:typed_data';

import 'model_import3d.dart';

/// IMPORTAR UM MODELO PODE DEMORAR OU FALHAR — NUNCA DERRUBAR O APP.
///
/// Os importadores ja recusam arquivo malformado com [ModelImportException].
/// O que ainda matava o app era o arquivo VALIDO e grande demais: faltar
/// memoria no meio do parse (o iOS mata o processo, nada captura) ou uma
/// textura de 16K decodificada inteira. Estes limites sao conferidos ANTES
/// de alocar — pelo tamanho do arquivo, pelo cabecalho da imagem, pelas
/// contagens declaradas no glTF e pelas linhas do OBJ — e viram uma
/// mensagem que diz o que fazer.
///
/// Os numeros sao PROVISORIOS e conservadores, escolhidos para o iPhone 13
/// (4 GB, o aparelho de referencia de compatibilidade): a malha importada
/// ainda vive em Dart, com copias no quadro avaliado, nos grupos por
/// material e na GPU. A calibragem com medicao no aparelho e da fase de
/// benchmark; ate la, recusar com clareza vale mais que abrir e cair.
class LimitesDeImportacao {
  const LimitesDeImportacao({
    this.arquivoBinario = 400 * _mb,
    this.arquivoDeTexto = 150 * _mb,
    this.fbxDeTexto = 60 * _mb,
    this.recursos = 600 * _mb,
    this.imagemArquivo = 64 * _mb,
    this.ladoDaImagem = 8192,
    this.pixelsPorImagem = 48 * 1000 * 1000,
    this.pixelsDasImagens = 128 * 1000 * 1000,
    this.vertices = 1500000,
    this.triangulos = 2000000,
  });

  static const _mb = 1024 * 1024;

  /// GLB, FBX binario, .bin do glTF.
  final int arquivoBinario;

  /// OBJ e o JSON do glTF: texto vira muito mais memoria que o arquivo.
  final int arquivoDeTexto;

  /// FBX ASCII: o leitor separa o arquivo inteiro em palavras.
  final int fbxDeTexto;

  /// Todos os arquivos complementares juntos.
  final int recursos;

  final int imagemArquivo;
  final int ladoDaImagem;
  final int pixelsPorImagem;
  final int pixelsDasImagens;
  final int vertices;
  final int triangulos;
}

const limitesDeImportacao = LimitesDeImportacao();

String _mbLegivel(int bytes) => '${(bytes / (1024 * 1024)).round()} MB';

String _milhares(int n) => n >= 1000000
    ? '${(n / 1000000).toStringAsFixed(n % 1000000 == 0 ? 0 : 1).replaceAll('.', ',')} milhoes'
    : n >= 1000
    ? '${(n / 1000).round()} mil'
    : '$n';

/// O modelo principal: [nome] com extensao, [bytes] no disco. [fbxDeTexto]
/// quando o cabecalho do FBX nao e o binario.
void conferirArquivoDoModelo(
  String nome,
  int bytes, {
  bool fbxDeTexto = false,
  LimitesDeImportacao limites = limitesDeImportacao,
}) {
  final ext = nome.toLowerCase().split('.').last;
  final teto = switch (ext) {
    'obj' || 'gltf' => limites.arquivoDeTexto,
    'fbx' when fbxDeTexto => limites.fbxDeTexto,
    _ => limites.arquivoBinario,
  };
  if (bytes > teto) {
    final dica = ext == 'fbx' && fbxDeTexto
        ? ' Exporte como FBX binario ou GLB.'
        : ext == 'obj'
        ? ' Exporte como GLB, que e menor e abre mais rapido.'
        : ' Reduza a malha (decimar) ou as texturas antes de importar.';
    modelFail(
      '$nome tem ${_mbLegivel(bytes)}; o limite para este formato e '
      '${_mbLegivel(teto)}.$dica',
    );
  }
}

/// Se [bytes] comeca como FBX binario ("Kaydara FBX Binary  \0").
bool fbxBinario(Uint8List bytes) {
  const magia = 'Kaydara FBX Binary';
  if (bytes.length < magia.length) return false;
  for (var i = 0; i < magia.length; i++) {
    if (bytes[i] != magia.codeUnitAt(i)) return false;
  }
  return true;
}

/// Os arquivos complementares, pelo tamanho no disco.
void conferirRecursos(
  Map<String, int> tamanhos, {
  LimitesDeImportacao limites = limitesDeImportacao,
}) {
  var total = 0;
  for (final e in tamanhos.entries) {
    total += e.value;
    final imagem = RegExp(
      r'\.(png|jpe?g|webp|bmp|ktx2?)$',
      caseSensitive: false,
    ).hasMatch(e.key);
    if (imagem && e.value > limites.imagemArquivo) {
      modelFail(
        'A textura ${e.key} tem ${_mbLegivel(e.value)}; o limite e '
        '${_mbLegivel(limites.imagemArquivo)} por imagem.',
      );
    }
  }
  if (total > limites.recursos) {
    modelFail(
      'Os arquivos complementares somam ${_mbLegivel(total)}; o limite e '
      '${_mbLegivel(limites.recursos)}.',
    );
  }
}

/// Largura e altura lidas do CABECALHO (PNG, JPEG, WebP, BMP), sem
/// decodificar. Nulo quando o formato nao e reconhecido.
({int largura, int altura})? dimensoesDaImagem(Uint8List b) {
  int u16be(int i) => (b[i] << 8) | b[i + 1];
  int u32be(int i) =>
      (b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3];
  int u16le(int i) => b[i] | (b[i + 1] << 8);
  int u24le(int i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16);
  int s32le(int i) =>
      (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)).toSigned(
        32,
      );

  // PNG: assinatura de 8 bytes, IHDR com largura e altura.
  if (b.length >= 24 &&
      b[0] == 0x89 &&
      b[1] == 0x50 &&
      b[2] == 0x4E &&
      b[3] == 0x47) {
    return (largura: u32be(16), altura: u32be(20));
  }
  // JPEG: percorre os marcadores ate um SOF.
  if (b.length >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
    var i = 2;
    while (i + 9 < b.length) {
      if (b[i] != 0xFF) {
        i++;
        continue;
      }
      final marcador = b[i + 1];
      if (marcador == 0xFF) {
        i++;
        continue;
      }
      if (marcador == 0xD8 ||
          marcador == 0x01 ||
          (marcador >= 0xD0 && marcador <= 0xD7)) {
        i += 2;
        continue;
      }
      final tamanho = u16be(i + 2);
      final sof =
          marcador >= 0xC0 &&
          marcador <= 0xCF &&
          marcador != 0xC4 &&
          marcador != 0xC8 &&
          marcador != 0xCC;
      if (sof) return (largura: u16be(i + 7), altura: u16be(i + 5));
      if (tamanho < 2) return null;
      i += 2 + tamanho;
    }
    return null;
  }
  // WebP: RIFF....WEBP e um dos tres blocos.
  if (b.length >= 30 &&
      b[0] == 0x52 &&
      b[1] == 0x49 &&
      b[2] == 0x46 &&
      b[3] == 0x46 &&
      b[8] == 0x57 &&
      b[9] == 0x45 &&
      b[10] == 0x42 &&
      b[11] == 0x50) {
    final bloco = String.fromCharCodes(b.sublist(12, 16));
    switch (bloco) {
      case 'VP8 ':
        return (largura: u16le(26) & 0x3FFF, altura: u16le(28) & 0x3FFF);
      case 'VP8L':
        final bits = b[21] | (b[22] << 8) | (b[23] << 16) | (b[24] << 24);
        return (
          largura: (bits & 0x3FFF) + 1,
          altura: ((bits >> 14) & 0x3FFF) + 1,
        );
      case 'VP8X':
        return (largura: u24le(24) + 1, altura: u24le(27) + 1);
    }
    return null;
  }
  // BMP.
  if (b.length >= 26 && b[0] == 0x42 && b[1] == 0x4D) {
    return (largura: s32le(18).abs(), altura: s32le(22).abs());
  }
  return null;
}

/// Uma imagem do modelo. Devolve os pixels dela, para a soma.
int conferirImagem(
  String nome,
  Uint8List bytes, {
  LimitesDeImportacao limites = limitesDeImportacao,
}) {
  if (bytes.length > limites.imagemArquivo) {
    modelFail(
      'A textura $nome tem ${_mbLegivel(bytes.length)}; o limite e '
      '${_mbLegivel(limites.imagemArquivo)} por imagem.',
    );
  }
  final d = dimensoesDaImagem(bytes);
  if (d == null) return 0;
  final lado = d.largura > d.altura ? d.largura : d.altura;
  final pixels = d.largura * d.altura;
  if (lado > limites.ladoDaImagem || pixels > limites.pixelsPorImagem) {
    modelFail(
      'A textura $nome tem ${d.largura}x${d.altura}. O limite e '
      '${limites.ladoDaImagem} px no lado maior: reduza a imagem antes de '
      'importar.',
    );
  }
  return pixels;
}

/// A soma dos pixels de todas as texturas.
void conferirPixelsDasImagens(
  int pixels, {
  LimitesDeImportacao limites = limitesDeImportacao,
}) {
  if (pixels > limites.pixelsDasImagens) {
    modelFail(
      'As texturas somam ${_milhares(pixels)} de pixels; o limite e '
      '${_milhares(limites.pixelsDasImagens)}. Reduza as imagens.',
    );
  }
}

/// Vertices e triangulos do modelo inteiro.
void conferirMalha({
  required int vertices,
  required int triangulos,
  LimitesDeImportacao limites = limitesDeImportacao,
}) {
  if (triangulos > limites.triangulos || vertices > limites.vertices) {
    modelFail(
      'O modelo tem ${_milhares(triangulos)} triangulos e '
      '${_milhares(vertices)} vertices. O limite e '
      '${_milhares(limites.triangulos)} triangulos e '
      '${_milhares(limites.vertices)} vertices: decime a malha antes de '
      'importar.',
    );
  }
}

/// O que um glTF DECLARA, lido do JSON antes de qualquer buffer: soma o
/// POSITION e os indices de cada primitiva de cada malha (sem contar
/// instancias pelos nos, que reaproveitam a mesma malha).
({int vertices, int triangulos}) contarGltf(Map<String, dynamic> doc) {
  List<dynamic> lista(Object? v) => v is List ? v : const [];
  final accessors = lista(doc['accessors']);
  int contagem(Object? i) {
    if (i is! int || i < 0 || i >= accessors.length) return 0;
    final a = accessors[i];
    final c = a is Map ? a['count'] : null;
    return c is int && c > 0 ? c : 0;
  }

  var vertices = 0, triangulos = 0;
  for (final m in lista(doc['meshes'])) {
    if (m is! Map) continue;
    for (final p in lista(m['primitives'])) {
      if (p is! Map) continue;
      final attrs = p['attributes'];
      final v = attrs is Map ? contagem(attrs['POSITION']) : 0;
      vertices += v;
      final modo = p['mode'] is int ? p['mode'] as int : 4;
      final n = p['indices'] != null ? contagem(p['indices']) : v;
      triangulos += switch (modo) {
        4 => n ~/ 3,
        5 || 6 => n > 2 ? n - 2 : 0,
        _ => 0,
      };
    }
  }
  return (vertices: vertices, triangulos: triangulos);
}

/// Vertices e triangulos de um OBJ, contando linhas `v` e `f` nos bytes
/// (sem decodificar o texto).
({int vertices, int triangulos}) contarObj(Uint8List b) {
  var vertices = 0, triangulos = 0;
  var i = 0;
  final n = b.length;
  while (i < n) {
    // Inicio de linha: pula espacos.
    while (i < n && (b[i] == 0x20 || b[i] == 0x09)) {
      i++;
    }
    if (i + 1 < n && (b[i + 1] == 0x20 || b[i + 1] == 0x09)) {
      if (b[i] == 0x76) {
        vertices++;
      } else if (b[i] == 0x66) {
        // Cantos da face: grupos separados por espaco depois do "f".
        var cantos = 0;
        var j = i + 1;
        var dentro = false;
        while (j < n && b[j] != 0x0A && b[j] != 0x0D) {
          final espaco = b[j] == 0x20 || b[j] == 0x09;
          if (!espaco && !dentro) cantos++;
          dentro = !espaco;
          j++;
        }
        if (cantos >= 3) triangulos += cantos - 2;
      }
    }
    while (i < n && b[i] != 0x0A) {
      i++;
    }
    i++;
  }
  return (vertices: vertices, triangulos: triangulos);
}

/// O modelo ja lido (qualquer formato): conta as primitivas.
({int vertices, int triangulos}) contarModelo(Map<String, dynamic> data) {
  var vertices = 0, triangulos = 0;
  final primitivas = data['primitives'];
  for (final p in primitivas is List ? primitivas : const []) {
    if (p is! Map) continue;
    final pos = p['positions'], idx = p['indices'];
    vertices += pos is List ? pos.length : 0;
    triangulos += (idx is List ? idx.length : 0) ~/ 3;
  }
  return (vertices: vertices, triangulos: triangulos);
}
