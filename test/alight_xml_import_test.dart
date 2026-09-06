import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/projects/domain/alight_xml_import.dart';

const _preset = '''<?xml version="1.0" encoding="UTF-8"?>
<!-- preset exportado -->
<project version="4">
  <scene width="1080" height="1920" fps="30" duration="4000">
    <layers>
      <layer type="shape" shape="rect" name="Fundo" startTime="0" endTime="4000"
             width="1080" height="1920" color="#1A1A2E" x="0" y="0"/>
      <layer type="text" name="Titulo" startTime="500" endTime="3500" fontSize="96"
             color="#FFFFFF" text="Ola &amp; bem-vindo">
        <transform x="0" y="-200" scale="1" rotation="0" opacity="100"/>
        <property name="position">
          <keyframe time="500" value="-400,-200" easing="easeOut"/>
          <keyframe time="1200" value="0,-200"/>
        </property>
        <property name="opacity">
          <keyframe time="500" value="0"/>
          <keyframe time="900" value="100"/>
        </property>
        <effect id="glow" amount="60"/>
        <effect id="efeito_inventado"/>
      </layer>
      <layer type="group" name="Grupo" startTime="0" endTime="4000">
        <layer type="shape" shape="circle" name="Bolinha" radius="60" color="255,59,82" x="120" y="300"/>
        <layer type="image" name="Foto" src="foto.png"/>
      </layer>
    </layers>
  </scene>
</project>
''';

void main() {
  group('importar XML do Alight', () {
    test('le cena, camadas, transform, keyframes, cor e efeito', () {
      final r = importAlightXml(_preset, nome: 'Teste');
      expect(r.project.resolutionHeight, 1920);
      expect(r.project.aspectRatio, closeTo(1080 / 1920, 1e-9));
      expect(r.project.fps, 30);
      expect(r.layersImported, 3, reason: 'fundo, titulo e grupo');
      // Origem no centro (havia coordenada negativa): (0,0) vira o centro.
      final fundo = r.project.layers.firstWhere((l) => l.name == 'Fundo') as ShapeLayer;
      expect(fundo.position.base, const Offset(540, 960));
      final forma = fundo.contents.whereType<ShapeParametric>().single;
      expect(forma.kind, ParamShapeKind.rect);
      expect(forma.sizeX.base, 1080);
      final fill = fundo.contents.whereType<ShapeFill>().single;
      expect(fill.color, const Color(0xFF1A1A2E));

      final titulo = r.project.layers.firstWhere((l) => l.name == 'Titulo') as TextLayer;
      expect(titulo.text, 'Ola & bem-vindo');
      expect(titulo.fontSize, 96);
      expect(titulo.startTime, const Duration(milliseconds: 500));
      expect(titulo.duration, const Duration(seconds: 3));
      expect(titulo.position.keyframes.length, 2);
      expect(titulo.position.keyframes.first.value, const Offset(140, 760));
      expect(titulo.position.keyframes.first.time, const Duration(milliseconds: 500));
      expect(titulo.opacity.keyframes.length, 2);
      expect(titulo.opacity.keyframes.last.value, 1.0, reason: '100% vira 1');
      expect(titulo.effects.length, 1, reason: 'glow entra, inventado nao');
      expect(titulo.effects.single.type, EffectType.lightGlow);
      expect(r.keyframesImported, 4);

      final grupo = r.project.layers.firstWhere((l) => l.name == 'Grupo') as GroupLayer;
      expect(grupo.children.length, 1, reason: 'a imagem sem arquivo fica de fora');
      final bolinha = grupo.children.single as ShapeLayer;
      final circ = bolinha.contents.whereType<ShapeParametric>().single;
      expect(circ.kind, ParamShapeKind.ellipse);
      expect(circ.sizeX.base, 120, reason: 'raio 60 -> diametro 120');
      expect(bolinha.contents.whereType<ShapeFill>().single.color,
          const Color(0xFFFF3B52));

      expect(r.ignored.any((s) => s.contains('foto.png')), isTrue);
      expect(r.ignored.any((s) => s.contains('efeito_inventado')), isTrue);
    });

    test('XML sem camadas ou texto qualquer da erro claro', () {
      expect(() => importAlightXml('<project><scene width="10" height="10"/></project>'),
          throwsA(isA<AlightImportException>()));
      expect(() => importAlightXml('nada'), throwsA(isA<AlightImportException>()));
    });

    test('parser aguenta atributos com aspas simples, CDATA e fechamento torto', () {
      final root = parseXml("<a x='1'><b><![CDATA[<oi>]]></b><c/></d></a>");
      final a = root.children.single;
      expect(a.tag, 'a');
      expect(a.attrs['x'], '1');
      expect(a.children.length, 2);
      expect(a.children.first.text.toString(), '<oi>');
    });
  });
}
