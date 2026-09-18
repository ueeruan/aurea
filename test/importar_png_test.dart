// O PNG QUE NAO IMPORTAVA — E O FUNDO BRANCO QUE APARECIA NO LUGAR DELE.
//
// SAO DOIS DEFEITOS COM UMA CAUSA EM COMUM: a imagem nao abre.
//
//   * "nao importa png": o `persist` validava com
//     `instantiateCodec(targetWidth: 1, targetHeight: 1)`. Esse pedido de
//     1x1 e uma PISTA de reducao, e nao uma prova de que o arquivo abre —
//     e quando ela falhava, o `catch` APAGAVA o arquivo e propagava. A
//     importacao inteira morria por causa de um arquivo que o palco
//     desenharia sem reclamar;
//   * "fica um fundo branco": quando o `Image.file` falha, o palco
//     desenhava uma caixa `Colors.white10` de 400x300. O testador via um
//     retangulo claro e chamava aquilo de fundo branco — era o DESENHO DA
//     FALHA, e nao um fundo.
//
// E UM TERCEIRO, achado no caminho: a importacao gravava em
// `getApplicationSupportDirectory` e a aba Midia lia de
// `getApplicationDocumentsDirectory`. Duas pastas com o mesmo nome, e o
// que entrava nunca aparecia na galeria.
//
// OS TESTES USAM UM PNG DE VERDADE, COM ALFA — um PNG de cor lisa
// passaria por qualquer uma das versoes, inclusive pela quebrada.
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/media/application/media_import_service.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image_picker/image_picker.dart';

/// UM PNG 4x2 COM UM PIXEL TRANSPARENTE, montado byte a byte.
///
/// Feito a mao de proposito: escrever o arquivo pelo `image` ou por um
/// encoder esconderia justamente o que se quer testar (o que o
/// decodificador do aparelho aceita). Sao os bytes de um PNG valido, com
/// um `tRNS`-livre mas com alfa de verdade no canal RGBA.
Uint8List _pngComAlfa() {
  // 4x2, RGBA: metade opaca vermelha, metade transparente.
  final cru = BytesBuilder();
  for (var y = 0; y < 2; y++) {
    cru.addByte(0); // filtro 0
    for (var x = 0; x < 4; x++) {
      final opaco = x < 2;
      cru
        ..addByte(255)
        ..addByte(0)
        ..addByte(0)
        ..addByte(opaco ? 255 : 0);
    }
  }
  return _png(4, 2, cru.toBytes());
}

/// Monta um PNG (IHDR + IDAT deflate "stored" + IEND).
Uint8List _png(int w, int h, Uint8List cru) {
  final saida = BytesBuilder()
    ..add(const [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
  final ihdr = BytesBuilder()
    ..add(_u32(w))
    ..add(_u32(h))
    ..add(const [8, 6, 0, 0, 0]); // 8 bits, RGBA
  saida.add(_bloco('IHDR', ihdr.toBytes()));
  saida.add(_bloco('IDAT', _zlibStored(cru)));
  saida.add(_bloco('IEND', Uint8List(0)));
  return saida.toBytes();
}

Uint8List _u32(int v) =>
    Uint8List(4)..buffer.asByteData().setUint32(0, v, Endian.big);

/// Deflate sem compressao (blocos "stored") — o minimo que o zlib aceita.
Uint8List _zlibStored(Uint8List dados) {
  final b = BytesBuilder()..add(const [0x78, 0x01]);
  var i = 0;
  while (i < dados.length) {
    final n = (dados.length - i).clamp(0, 65535);
    final ultimo = i + n >= dados.length;
    b
      ..addByte(ultimo ? 1 : 0)
      ..addByte(n & 0xFF)
      ..addByte((n >> 8) & 0xFF)
      ..addByte(~n & 0xFF)
      ..addByte((~n >> 8) & 0xFF)
      ..add(Uint8List.sublistView(dados, i, i + n));
    i += n;
  }
  var a = 1, c = 0;
  for (final byte in dados) {
    a = (a + byte) % 65521;
    c = (c + a) % 65521;
  }
  b
    ..addByte((c >> 8) & 0xFF)
    ..addByte(c & 0xFF)
    ..addByte((a >> 8) & 0xFF)
    ..addByte(a & 0xFF);
  return b.toBytes();
}

Uint8List _bloco(String tipo, Uint8List dados) {
  final b = BytesBuilder()
    ..add(_u32(dados.length))
    ..add(tipo.codeUnits)
    ..add(dados);
  final crc = _crc32(Uint8List.fromList([...tipo.codeUnits, ...dados]));
  b.add(_u32(crc));
  return b.toBytes();
}

int _crc32(Uint8List dados) {
  var c = 0xFFFFFFFF;
  for (final byte in dados) {
    c ^= byte;
    for (var k = 0; k < 8; k++) {
      c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB88320 : c >> 1;
    }
  }
  return (c ^ 0xFFFFFFFF) & 0xFFFFFFFF;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('o PNG entra, e o alfa sobrevive', () {
    test('um PNG com alfa passa pela importacao e chega ao disco', () async {
      final raiz = await Directory.systemTemp.createTemp('aurea-png');
      addTearDown(() => raiz.delete(recursive: true));
      final origem = File('${raiz.path}/entrada.png')
        ..writeAsBytesSync(_pngComAlfa());

      final servico = MediaImportService(
        null,
        () async => raiz,
      );
      final importado = await servico.persist(
        XFile(origem.path, name: 'entrada.png'),
        image: true,
      );

      final destino = File(importado.path);
      expect(destino.existsSync(), isTrue, reason: 'o arquivo nao chegou');
      expect(destino.lengthSync(), greaterThan(0));
      // E O CONTEUDO E O MESMO, byte a byte: a importacao COPIA, e nao
      // reencoda. Um reencode para JPEG perderia o alfa — e seria
      // exatamente o "fundo branco" do relato.
      expect(destino.readAsBytesSync(), origem.readAsBytesSync());
    });

    test('o alfa e de verdade — o pixel transparente continua transparente',
        () async {
      // A PROVA DIRETA DO "FUNDO BRANCO". Se o arquivo que chegou ao disco
      // tem o pixel transparente, o defeito nao esta na importacao.
      final raiz = await Directory.systemTemp.createTemp('aurea-png2');
      addTearDown(() => raiz.delete(recursive: true));
      final origem = File('${raiz.path}/a.png')..writeAsBytesSync(_pngComAlfa());
      final servico = MediaImportService(null, () async => raiz);
      final importado = await servico.persist(
        XFile(origem.path, name: 'a.png'),
        image: true,
      );

      final bytes = File(importado.path).readAsBytesSync();
      final buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
      final codigo = await ui.instantiateImageCodec(
        bytes,
        targetWidth: 4,
        targetHeight: 2,
      );
      buffer.dispose();
      final quadro = await codigo.getNextFrame();
      final dados = await quadro.image.toByteData(
        format: ui.ImageByteFormat.rawRgba,
      );
      quadro.image.dispose();
      codigo.dispose();
      final px = dados!.buffer.asUint8List();
      // (0,0) opaco vermelho; (3,0) transparente.
      expect(px[0 * 4 + 3], 255, reason: 'o pixel opaco perdeu o alfa');
      expect(px[3 * 4 + 3], 0, reason: 'o pixel transparente ficou opaco');
    });

    test('o arquivo SEM EXTENSAO no caminho ganha a extensao dos BYTES',
        () async {
      // O CAMINHO DO SELETOR NEM SEMPRE TEM EXTENSAO — o Android devolve
      // um arquivo em cache, e alguns provedores o gravam sem sufixo. O
      // `Image.file` nao liga (fareja o formato), mas o nome que a pessoa
      // le na camada liga, e um arquivo sem sufixo vira "arquivo estranho"
      // no projeto.
      final raiz = await Directory.systemTemp.createTemp('aurea-png3');
      addTearDown(() => raiz.delete(recursive: true));
      final origem = File('${raiz.path}/sem_sufixo')
        ..writeAsBytesSync(_pngComAlfa());
      final servico = MediaImportService(null, () async => raiz);
      final importado = await servico.persist(
        XFile(origem.path, name: 'foto'),
        image: true,
      );
      expect(importado.path, endsWith('.png'));
      expect(File(importado.path).existsSync(), isTrue);
    });

    test('um arquivo que NAO e imagem e recusado, e nao vira camada', () async {
      // A VALIDACAO CONTINUA EXISTINDO — o que mudou foi ela decodificar
      // de verdade em vez de pedir 1x1. Um arquivo de texto com nome de
      // PNG tem de ser recusado, e o arquivo NAO pode ficar no disco.
      final raiz = await Directory.systemTemp.createTemp('aurea-png4');
      addTearDown(() => raiz.delete(recursive: true));
      final origem = File('${raiz.path}/falso.png')
        ..writeAsStringSync('isto nao e uma imagem');
      final servico = MediaImportService(null, () async => raiz);

      await expectLater(
        servico.persist(XFile(origem.path, name: 'falso.png'), image: true),
        throwsA(anything),
      );
      final pasta = Directory('${raiz.path}/$subpastaDaMidia');
      final sobras = pasta.existsSync()
          ? pasta.listSync().whereType<File>().toList()
          : <File>[];
      expect(
        sobras,
        isEmpty,
        reason: 'a importacao falhou e deixou o arquivo no disco',
      );
    });

    test('o arquivo vazio e recusado', () async {
      final raiz = await Directory.systemTemp.createTemp('aurea-png5');
      addTearDown(() => raiz.delete(recursive: true));
      final origem = File('${raiz.path}/vazio.png')..writeAsBytesSync([]);
      final servico = MediaImportService(null, () async => raiz);
      await expectLater(
        servico.persist(XFile(origem.path, name: 'vazio.png'), image: true),
        throwsA(anything),
      );
    });
  });

  group('a pasta da midia e UMA so', () {
    test('a importacao grava na mesma pasta que a galeria le', () async {
      // ESTE ERA O TERCEIRO DEFEITO: gravar em ApplicationSupport e ler de
      // ApplicationDocuments. Nas duas existe `imported_media`, e sao
      // pastas diferentes — o que entrava nunca aparecia na galeria, e o
      // defeito nao dava erro nenhum.
      final raiz = await Directory.systemTemp.createTemp('aurea-png6');
      addTearDown(() => raiz.delete(recursive: true));
      final origem = File('${raiz.path}/a.png')..writeAsBytesSync(_pngComAlfa());
      final servico = MediaImportService(null, () async => raiz);
      final importado = await servico.persist(
        XFile(origem.path, name: 'a.png'),
        image: true,
      );
      expect(
        importado.path,
        contains('/$subpastaDaMidia/'),
        reason: 'a importacao gravou fora da pasta que a galeria le',
      );
    });
  });
}
