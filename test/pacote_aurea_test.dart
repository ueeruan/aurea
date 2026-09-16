// O PACOTE .aurea E O ZIP DE CENA (v1.1.1): o projeto viaja com as
// midias dele, e o pacote dos outros editores abre com as midias juntas.
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:archive/archive.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/domain/cena_xml_import.dart';
import 'package:aurea/src/features/projects/domain/pacote_aurea.dart';
import 'package:aurea/src/features/projects/domain/pacote_zip_import.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late Directory temp;

  setUp(() {
    temp = Directory.systemTemp.createTempSync('aurea_pacote');
  });

  tearDown(() {
    try {
      temp.deleteSync(recursive: true);
    } catch (_) {}
  });

  File _midia(String nome, List<int> bytes) =>
      File('${temp.path}/$nome')..writeAsBytesSync(bytes);

  VideoProject _projetoComMidia() {
    final foto = _midia('foto do niver.png', List.filled(600, 7));
    final som = _midia('trilha.mp3', List.filled(900, 3));
    return VideoProject.empty('Festa').copyWith(
      layers: [
        ImageLayer(
          name: 'Foto',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          sourcePath: foto.path,
        ),
        AudioLayer(
          name: 'Som',
          startTime: Duration.zero,
          duration: const Duration(seconds: 3),
          sourcePath: som.path,
        ),
      ],
    );
  }

  group('o pacote .aurea', () {
    test('acha as mídias do projeto e ignora caminho que não existe', () {
      final p = _projetoComMidia().copyWith(
        layers: [
          ..._projetoComMidia().layers,
          ImageLayer(
            name: 'Sumida',
            startTime: Duration.zero,
            duration: const Duration(seconds: 1),
            sourcePath: '${temp.path}/nao_existe.png',
          ),
        ],
      );
      final midias = PacoteAurea.midiasDoProjeto(p);
      expect(midias, hasLength(2));
      expect(midias.any((m) => m.endsWith('.mp3')), isTrue);
    });

    test('vai e volta: o projeto abre com as mídias apontando para cá', () {
      final original = _projetoComMidia();
      final bytes = PacoteAurea.montar(original);

      // O zip de verdade: projeto.json + midia/ sem recomprimir.
      final zip = ZipDecoder().decodeBytes(bytes);
      expect(zip.files.any((f) => f.name == 'projeto.json'), isTrue);
      expect(zip.files.where((f) => f.name.startsWith('midia/')), hasLength(2));

      final destino = Directory('${temp.path}/volta');
      final aberto = PacoteAurea.abrir(bytes, destino);
      expect(aberto.name, 'Festa');
      expect(aberto.id, isNot(original.id), reason: 'projeto novo, id novo');
      final img = aberto.layers.whereType<ImageLayer>().single;
      final som = aberto.layers.whereType<AudioLayer>().single;
      expect(File(img.sourcePath).existsSync(), isTrue);
      expect(File(som.sourcePath).existsSync(), isTrue);
      expect(img.sourcePath, startsWith(destino.path));
      expect(File(img.sourcePath).lengthSync(), 600);
    });

    test('zip qualquer não passa por pacote', () {
      final zip = Archive()
        ..addFile(ArchiveFile('outra_coisa.txt', 3, utf8.encode('oi!')));
      final bytes = ZipEncoder().encodeBytes(zip);
      expect(
        () => PacoteAurea.abrir(bytes, Directory('${temp.path}/x')),
        throwsFormatException,
      );
    });
  });

  group('o zip de cena (.amproj e afins)', () {
    Uint8List _zipDeCena() {
      const xml = '''
<scene title="Vinda" width="1080" height="1920" fps="30" totalTime="4000">
  <media uri="m1" filename="foto.png" />
  <image id="1" label="Foto" src="m1" startTime="0" endTime="3000">
    <transform><location value="540,960" /></transform>
  </image>
  <shape id="2" label="Quadro" startTime="0" endTime="4000" fillType="color">
    <transform><location value="540,960" /></transform>
    <fillColor value="#FF3366FF" />
    <path d="M 0 0 L 200 0 L 200 200 L 0 200 Z" />
  </shape>
</scene>''';
      final zip = Archive()
        ..addFile(ArchiveFile('manifest.json', 2, utf8.encode('{}')))
        ..addFile(ArchiveFile('cena.xml', xml.length, utf8.encode(xml)))
        ..addFile(ArchiveFile('foto.png', 5, [1, 2, 3, 4, 5]));
      return ZipEncoder().encodeBytes(zip);
    }

    test('a mídia que veio junto entra de verdade', () {
      final r = CenaEmZip.abrir(
        _zipDeCena(),
        Directory('${temp.path}/cena'),
        nome: 'Vinda',
      );
      expect(r.project.layers.whereType<ImageLayer>(), hasLength(1));
      final img = r.project.layers.whereType<ImageLayer>().single;
      expect(File(img.sourcePath).existsSync(), isTrue);
      expect(
        r.ignored.where((i) => i.contains('foto')),
        isEmpty,
        reason: 'com a midia junto nao ha o que avisar',
      );
    });

    test('sem XML de cena dentro, avisa em português', () {
      final zip = Archive()..addFile(ArchiveFile('so_isto.bin', 2, [0, 1]));
      expect(
        () => CenaEmZip.abrir(
          ZipEncoder().encodeBytes(zip),
          Directory('${temp.path}/vazio'),
        ),
        throwsA(isA<CenaXmlException>()),
      );
    });

    test('o XML solto continua avisando que a mídia ficou de fora', () {
      const xml = '''
<scene title="Solta" width="1080" height="1920" fps="30" totalTime="4000">
  <media uri="m1" filename="foto.png" />
  <image id="1" label="Foto" src="m1" startTime="0" endTime="3000" />
</scene>''';
      expect(
        () => importarCenaXml(xml),
        throwsA(isA<CenaXmlException>()),
        reason: 'so a imagem sem midia: nenhuma camada reconhecida',
      );
    });
  });
}
