import 'dart:io';
import 'dart:ui' as ui;
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/panorama3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/presentation/widgets/particulas_painter.dart';
void main() {
 TestWidgetsFlutterBinding.ensureInitialized();
 test('capture reflection and star field with actual application painters', () async {
  final output=await Directory('output/beta-review').create(recursive:true);
  Future<void> capture(String name,void Function(ui.Canvas) draw) async {
   final recorder=ui.PictureRecorder();final canvas=ui.Canvas(recorder);
   canvas.drawColor(const ui.Color(0xff0a0d12),ui.BlendMode.src);
   draw(canvas);final picture=recorder.endRecording();final image=await picture.toImage(640,360);
   final bytes=(await image.toByteData(format:ui.ImageByteFormat.png))!;
   await File('${output.path}/$name.png').writeAsBytes(bytes.buffer.asUint8List(bytes.offsetInBytes,bytes.lengthInBytes));
   picture.dispose();image.dispose();
  }
  // A REVISAO VISUAL DA CENA 3D SAIU COM O MOTOR. O que ela fotografava
  // — cromo, panorama urbano, o pintor de CPU — nao existe mais; a
  // revisao das outras telas continua abaixo.
  final c=ProviderContainer();c.read(editorControllerProvider.notifier).setComposition(aspectRatio:16/9,resolutionHeight:360);
  c.read(editorControllerProvider.notifier).addParticulasLayer(Duration.zero);
  final particles=c.read(editorControllerProvider).layers.first as ParticulasLayer;
  final watch=Stopwatch()..start();
  // A SIMULACAO E O DESENHO AGORA SAO PASSOS SEPARADOS: o motor gera o
  // LOTE, o pintor poe na tela. A bancada mede a SOMA dos dois — que e o
  // que a pessoa sente.
  final lote=LoteDeParticulas(particles.parametros..centroX=0..centroY=0);
  lote.gerar(2.0);
  await capture('star-field',(canvas)=>ParticulasPainter(lote:lote,centroDoQuadro:const ui.Offset(320,180)).paint(canvas,const ui.Size(640,360)));
  // ignore: avoid_print
  print('${lote.quantas} particulas: simular + pintar + PNG = ${watch.elapsedMilliseconds} ms');
  lote.liberar();c.dispose();
 });
}
