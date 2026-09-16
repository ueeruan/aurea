import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:archive/archive.dart';

import 'cena_xml_import.dart';

/// UM ZIP QUE TRAZ UMA CENA: o pacote de outro editor (.amproj) ou o
/// "XML + mídia" exportado em zip. Dentro: um ou mais XML de cena e os
/// arquivos de mídia.
///
/// A leitura é a mesma tolerância do XML solto — o que o motor não
/// conhece vira aviso, nunca erro — com uma diferença que muda tudo:
/// as mídias vêm JUNTO, então foto, vídeo e áudio entram de verdade em
/// vez de "reimporte o arquivo".
class CenaEmZip {
  const CenaEmZip._();

  /// O maior XML com `<scene` dentro é o projeto; os outros costumam ser
  /// miniaturas de elemento.
  static CenaXmlResult abrir(
    Uint8List bytes,
    Directory pastaDasMidias, {
    String? nome,
  }) {
    final Archive zip;
    try {
      zip = ZipDecoder().decodeBytes(bytes);
    } catch (_) {
      throw const CenaXmlException('Esse arquivo não é um pacote que eu leia.');
    }

    String? xml;
    var tamanhoDoXml = -1;
    final midias = <String, Uint8List>{};
    for (final f in zip.files) {
      if (!f.isFile) continue;
      final nomeDaEntrada = f.name.replaceAll('\\', '/');
      if (nomeDaEntrada.toLowerCase().endsWith('.xml')) {
        final texto = utf8.decode(f.content as List<int>, allowMalformed: true);
        if (texto.contains('<scene') && texto.length > tamanhoDoXml) {
          xml = texto;
          tamanhoDoXml = texto.length;
        }
        continue;
      }
      // Tudo que nao e XML nem manifesto e midia em potencial, indexada
      // pelo caminho inteiro E pelo nome curto.
      if (nomeDaEntrada.toLowerCase().endsWith('.json') ||
          nomeDaEntrada.toLowerCase().endsWith('.txt')) {
        continue;
      }
      midias[nomeDaEntrada] = Uint8List.fromList(f.content as List<int>);
      midias.putIfAbsent(
        nomeDaEntrada.split('/').last,
        () => midias[nomeDaEntrada]!,
      );
    }
    if (xml == null) {
      throw const CenaXmlException('O pacote veio sem uma cena em XML.');
    }

    pastaDasMidias.createSync(recursive: true);
    final extraidos = <String, String>{};
    String? resolve(String referencia) {
      if (referencia.isEmpty) return null;
      final ja = extraidos[referencia];
      if (ja != null) return ja;
      final bytesDaMidia =
          midias[referencia] ?? midias[referencia.split('/').last];
      if (bytesDaMidia == null) return null;
      final nomeCurto = referencia
          .split('/')
          .last
          .replaceAll(RegExp(r'[^A-Za-z0-9._-]'), '_');
      final destino = File(
        '${pastaDasMidias.path}/${extraidos.length}-$nomeCurto',
      );
      destino.writeAsBytesSync(bytesDaMidia);
      extraidos[referencia] = destino.path;
      return destino.path;
    }

    return importarCenaXml(xml, nome: nome, arquivoDaMidia: resolve);
  }
}
