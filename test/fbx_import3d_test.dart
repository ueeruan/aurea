import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/fbx_import3d.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';

const asciiFbx = '''
; FBX 7.4.0 test
Objects: {
 Geometry: 10, "Geometry::Triangle", "Mesh" {
  Vertices: *9 {
   a: 0,0,0, 1,0,0, 0,1,0
  }
  PolygonVertexIndex: *3 {
   a: 0,1,-3
  }
 }
 Model: 20, "Model::Triangle", "Mesh" {
  Properties70: {
   P: "Lcl Translation", "Lcl Translation", "", "A", 5,0,0
  }
 }
}
Connections: {
 C: "OO",10,20
 C: "OO",20,0
}
''';

void main() {
  test('FBX cluster preserva osso e permite novas poses com deformacao', () {
    final source = asciiFbx
        .replaceFirst('Objects: {', '''Objects: {
 Model: 30, "Model::Bone", "LimbNode" { }
 Deformer: 100, "Deformer::Skin", "Skin" { }
 Deformer: 101, "Deformer::Cluster", "Cluster" {
  Indexes: *3 { a: 0,1,2 }
  Weights: *3 { a: 1,1,1 }
  Transform: *16 { a: 1,0,0,0,0,1,0,0,0,0,1,0,5,0,0,1 }
  TransformLink: *16 { a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1 }
 }
''')
        .replaceFirst('Connections: {', '''Connections: {
 C: "OO",100,10
 C: "OO",101,100
 C: "OO",30,101
 C: "OO",30,0
''');
    final model = importFbx3D(Uint8List.fromList(utf8.encode(source)));
    expect(model.joints, hasLength(1));
    final joint = model.joints.single;
    final bind = model.evaluate(Duration.zero, const ModelMotion3D());
    final pose = model.evaluate(
      Duration.zero,
      ModelMotion3D(
        keys: [
          ModelPoseKey3D(0, {
            joint: const ModelPose3D(translation: [1, 0, 0]),
          }),
        ],
      ),
    );
    expect(pose.mesh.verts[0][0] - bind.mesh.verts[0][0], closeTo(2, 1e-9));
  });
  test(
    'FBX ASCII preserva geometria, transformacao e identifica limitacoes',
    () {
      final asset = importFbx3D(Uint8List.fromList(utf8.encode(asciiFbx)));
      expect(asset.triangleCount, 1);
      expect(asset.nodes[1]['translation'], [5.0, 0.0, 0.0]);
      final frame = asset.evaluate(Duration.zero, const ModelMotion3D());
      expect(frame.mesh.verts, [
        [-1.0, -1.0, 0.0],
        [1.0, -1.0, 0.0],
        [-1.0, 1.0, 0.0],
      ]);
      expect(asset.warnings.join(' '), contains('suporte parcial'));
    },
  );
  test('FBX recusa texto e cabecalho truncados sem mutar cena', () {
    expect(
      () => importFbx3D(Uint8List.fromList([0, 1, 2])),
      throwsA(isA<ModelImportException>()),
    );
    expect(
      () => importFbx3D(
        Uint8List.fromList(
          utf8.encode(asciiFbx.replaceFirst('0,1,-3', '0,1,-30')),
        ),
      ),
      throwsA(isA<ModelImportException>()),
    );
  });
  test('FBX com imagem importa e AVISA que a textura precisa vir junto', () {
    // Antes o arquivo inteiro era recusado por ter textura — jogava fora
    // a malha. Agora a malha entra, com UV, e o aviso diz o que faltou;
    // uma imagem selecionada junto vale para os materiais.
    final source = asciiFbx.replaceFirst(
      'Objects: {',
      'Objects: {\n Texture: 30, "Texture::Map", "" {\n }\n'
          ' Material: 40, "Material::Traje", "" {\n }\n',
    );
    final bytes = Uint8List.fromList(utf8.encode(source));
    final semImagem = importFbx3D(bytes);
    expect(semImagem.data['materials'], isNotEmpty);
    expect(semImagem.warnings.any((w) => w.contains('textura')), isTrue);
    expect(
      (semImagem.data['materials'] as List).every((m) => m['image'] == null),
      isTrue,
    );
    final comImagem = importFbx3D(
      bytes,
      resources: {'mapa.png': Uint8List.fromList([1, 2, 3])},
    );
    expect(
      (comImagem.data['materials'] as List).any((m) => m['image'] != null),
      isTrue,
    );
  });
  test('FBX binario publico Assimp (checagem local opcional)', () {
    final file = File('build/model3d-fixtures/box.fbx');
    if (!file.existsSync()) return;
    final model = importFbx3D(file.readAsBytesSync());
    expect(model.triangleCount, 12);
    final frame = model.evaluate(Duration.zero, const ModelMotion3D());
    expect(frame.mesh.verts.expand((v) => v).every((v) => v.isFinite), isTrue);
  });
}
