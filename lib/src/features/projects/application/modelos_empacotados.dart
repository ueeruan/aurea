import 'dart:io';

import 'package:flutter/services.dart' show rootBundle;
import 'package:path_provider/path_provider.dart';

import '../../editor/application/model_import_service.dart';
import '../../editor/domain/model_asset3d.dart';

/// OS MODELOS DO MONOLITO: o astronauta, o portal e a arvore escaneada.
///
/// Viajam como assets do app e viram arquivos uma vez, na pasta de
/// documentos; a partir dai entram pelo MESMO importador que o usuario
/// usa (OBJ + MTL + textura), em isolate. E o caminho honesto: se o
/// importador melhora, os modelos do app melhoram junto, e o que o
/// usuario importa passa pela mesma porta.
class MonolitoModelos {
  const MonolitoModelos({
    required this.astronauta,
    required this.portal,
    required this.arvore,
  });

  final ModelAsset3D astronauta;
  final ModelAsset3D portal;
  final ModelAsset3D arvore;
}

const _pasta = 'assets/models/monolito';
const _arquivos = [
  'astronauta.obj',
  'astronauta.mtl',
  'Astronaut_BaseColornew.jpeg',
  'portal.obj',
  'portal.mtl',
  'NetherPortal.png',
  'arvore.obj',
  'arvore.mtl',
  'arvore.jpg',
];

Future<MonolitoModelos>? _emCurso;

/// Prepara os modelos (uma vez por processo).
Future<MonolitoModelos> carregarMonolitoModelos() =>
    _emCurso ??= _carregar().catchError((Object e) {
      _emCurso = null;
      throw e;
    });

Future<MonolitoModelos> _carregar() async =>
    carregarMonolitoModelosDe(await _pastaPreparada());

/// Os assets viram arquivos de verdade uma vez: o importador le do disco
/// (em isolate), como faria com o que o usuario escolhe.
Future<String> _pastaPreparada() async {
  final docs = await getApplicationDocumentsDirectory();
  final dir = Directory('${docs.path}/modelos/monolito');
  await dir.create(recursive: true);
  for (final nome in _arquivos) {
    final f = File('${dir.path}/$nome');
    if (await f.exists()) continue;
    final dados = await rootBundle.load('$_pasta/$nome');
    await f.writeAsBytes(dados.buffer.asUint8List(), flush: true);
  }
  return dir.path;
}

/// SO O ASTRONAUTA: as cenas que nao precisam do portal nem da arvore
/// (a Deriva) pagam so por ele.
Future<ModelAsset3D> carregarAstronauta() async =>
    carregarAstronautaDe(await _pastaPreparada());

Future<ModelAsset3D> carregarAstronautaDe(String pasta) => readModel3DFiles([
      '$pasta/astronauta.obj',
      '$pasta/astronauta.mtl',
      '$pasta/Astronaut_BaseColornew.jpeg',
    ]);

/// Importa os tres modelos de uma pasta com os arquivos ja no lugar
/// (a bancada de render usa a pasta de assets do projeto direto).
Future<MonolitoModelos> carregarMonolitoModelosDe(String pasta) async {
  String p(String nome) => '$pasta/$nome';
  final astronauta = await carregarAstronautaDe(pasta);
  final portal = acenderPortal(
    await readModel3DFiles([
      p('portal.obj'),
      p('portal.mtl'),
      p('NetherPortal.png'),
    ]),
  );
  final arvore = await readModel3DFiles([
    p('arvore.obj'),
    p('arvore.mtl'),
    p('arvore.jpg'),
  ]);
  return MonolitoModelos(
    astronauta: astronauta,
    portal: portal,
    arvore: arvore,
  );
}

/// ACENDE O PORTAL: o modelo voxel usa uma paleta de uma linha; as faces
/// cujo UV cai numa cor MAGENTA da paleta passam para um material
/// emissivo sem luz — e o roxo do portal que brilha, nao a pedra da
/// moldura. Sem isto o portal inteiro seria opaco e apagado.
ModelAsset3D acenderPortal(ModelAsset3D portal) {
  final data = Map<String, dynamic>.from(portal.data);
  final materiais = List<Map<String, dynamic>>.from(
    (data['materials'] as List).map((m) => Map<String, dynamic>.from(m as Map)),
  );
  final primitivas = <Map<String, dynamic>>[];
  var acesas = 0;
  for (final raw in data['primitives'] as List) {
    final p = Map<String, dynamic>.from(raw as Map);
    final uvs = p['uv'] as List?;
    final indices = (p['indices'] as List).cast<int>();
    if (uvs == null || uvs.isEmpty) {
      primitivas.add(p);
      continue;
    }
    // A paleta MagicaVoxel: 256 colunas, uma linha; o u do vertice diz a
    // coluna, e a coluna diz se e portal (roxo) ou moldura (obsidiana).
    final acesa = <int>[], apagada = <int>[];
    for (var i = 0; i + 2 < indices.length; i += 3) {
      final u = ((uvs[indices[i]] as List)[0] as num).toDouble();
      (_paletaMagenta(u) ? acesa : apagada).addAll(indices.sublist(i, i + 3));
    }
    if (acesa.isEmpty) {
      primitivas.add(p);
      continue;
    }
    final base = materiais[p['material'] as int];
    materiais.add({
      ...base,
      'name': '${base['name']} · aceso',
      'emissive': 1.0,
      'unlit': true,
      'color': [1.0, 1.0, 1.0, 1.0],
    });
    primitivas.add({...p, 'indices': acesa, 'material': materiais.length - 1});
    if (apagada.isNotEmpty) primitivas.add({...p, 'indices': apagada});
    acesas += acesa.length ~/ 3;
  }
  data['primitives'] = primitivas;
  data['materials'] = materiais;
  data['warnings'] = [
    ...portal.warnings,
    if (acesas > 0) 'Portal: $acesas faces acesas (emissivas).',
  ];
  return ModelAsset3D(data);
}

/// Colunas da paleta consideradas "portal": neste modelo os roxos moram
/// nas colunas 46-53 (u de .18 a .21) e a obsidiana nas colunas 1-11.
bool _paletaMagenta(double u) => u > .15 && u < .25;
