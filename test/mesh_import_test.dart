import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/mesh_import.dart';

const _cuboObj = '''
# cubo de teste
v -1 -1 -1
v  1 -1 -1
v  1  1 -1
v -1  1 -1
v -1 -1  1
v  1 -1  1
v  1  1  1
v -1  1  1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1 4/4/1
f 5 6 7 8
f 1 2 6 5
f 2 3 7 6
f 3 4 8 7
f 4 1 5 8
''';

const _fbx7 = '''
; FBX 7.4.0 project file
Objects:  {
	Geometry: 140, "Geometry::Cube", "Mesh" {
		Vertices: *24 {
			a: -1,-1,1,1,-1,1,-1,1,1,1,1,1,-1,-1,-1,1,-1,-1,-1,1,-1,1,1,-1
		}
		PolygonVertexIndex: *24 {
			a: 0,1,3,-3,2,3,7,-7,6,7,5,-5,4,5,1,-1,2,6,4,-1,7,3,1,-6
		}
		GeometryVersion: 124
	}
}
''';

const _fbx6 = '''
Model: "Model::Cube", "Mesh" {
    Vertices: -1,-1,1,1,-1,1,-1,1,1,1,1,1,-1,-1,-1,1,-1,-1,-1,1,-1,1,1,-1
    PolygonVertexIndex: 0,1,3,-3,2,3,7,-7,6,7,5,-5,4,5,1,-1,2,6,4,-1,7,3,1,-6
    Edges: 0,1
    GeometryVersion: 124
}
''';

void main() {
  group('importar modelo 3D', () {
    test('OBJ: vertices, faces com barras, normalizado e com Y invertido',
        () {
      final r = importMeshText(_cuboObj, extension: 'obj');
      expect(r.format, 'OBJ');
      expect(r.vertexCount, 8);
      expect(r.faceCount, 6);
      expect(r.mesh.faces.every((f) => f.length == 4), isTrue);
      // Maior eixo = 1: vai de -0.5 a 0.5.
      for (final v in r.mesh.verts) {
        for (final c in v) {
          expect(c.abs(), closeTo(0.5, 1e-9));
        }
      }
      // Y invertido: o primeiro vertice tinha y = -1 (embaixo no OBJ) e
      // fica com y = +0.5 (embaixo na tela).
      expect(r.mesh.verts.first[1], closeTo(0.5, 1e-9));
      expect(r.heavy, isFalse);
    });

    test('FBX 7 (ASCII): indice negativo fecha o poligono', () {
      final r = importMeshText(_fbx7, extension: 'fbx');
      expect(r.format, 'FBX');
      expect(r.vertexCount, 8);
      expect(r.faceCount, 6);
      expect(r.mesh.faces.first, [0, 1, 3, 2]);
    });

    test('FBX 6 (listas sem chaves) tambem le', () {
      final r = importMeshText(_fbx6, extension: 'fbx');
      expect(r.vertexCount, 8);
      expect(r.faceCount, 6);
    });

    test('FBX binario e formato desconhecido dao erro claro', () {
      expect(() => importMeshText('Kaydara FBX Binary  \x00', extension: 'fbx'),
          throwsA(isA<MeshImportException>()));
      expect(() => importMeshText('x', extension: 'stl'),
          throwsA(isA<MeshImportException>()));
      expect(looksLikeBinaryFbx('Kaydara FBX Binary  \x00'.codeUnits), isTrue);
      expect(looksLikeBinaryFbx('; FBX 7.4.0 project file'.codeUnits),
          isFalse);
    });

    test('modelo pesado e sinalizado e o corte respeita o maximo', () {
      final sb = StringBuffer();
      const n = kMeshFacesMax + 10;
      sb.writeln('v 0 0 0\nv 1 0 0\nv 0 1 0');
      for (var i = 0; i < n; i++) {
        sb.writeln('f 1 2 3');
      }
      final r = importMeshText(sb.toString(), extension: 'obj');
      expect(r.heavy, isTrue);
      expect(r.truncated, isTrue);
      expect(r.faceCount, n);
      expect(r.mesh.faces.length, kMeshFacesMax);
    });
  });
}
