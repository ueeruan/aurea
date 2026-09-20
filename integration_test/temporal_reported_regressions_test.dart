import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:aurea/src/features/editor/application/quadros_de_video.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/editor/presentation/widgets/owned_video_frame.dart';
import 'package:aurea/src/features/export/presentation/export_video_screen.dart';
import 'package:aurea/src/features/export/domain/export_settings.dart';
import '../test/renderer_reported_regressions_test.dart' as raster;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  raster.main();
  testWidgets('Temporal preview crosses seconds monotonically and retains frames on cache miss', (tester) async {
    final dir = await getTemporaryDirectory();
    final path = '${dir.path}/frame-counter.mp4';
    final generated = await FFmpegKit.executeWithArguments([
      '-y', '-v', 'error', '-f', 'lavfi', '-i',
      "nullsrc=s=64x64:r=30:d=3,geq=r='N*2+20':g=100:b=100",
      '-c:v', 'mpeg4', '-threads', '1', '-q:v', '1', path,
    ]);
    expect(ReturnCode.isSuccess(await generated.getReturnCode()), isTrue);
    final cache = QuadrosDeVideo.instance; cache.limpar();
    addTearDown(() { QuadrosDeVideo.desligado = false; cache.limpar(); });
    final time = ValueNotifier(const Duration(milliseconds: 800)); addTearDown(time.dispose);
    final boundary = GlobalKey();
    await tester.pumpWidget(MaterialApp(home: Center(child: RepaintBoundary(key: boundary,
      child: SizedBox(width: 64, height: 64, child: ValueListenableBuilder<Duration>(
        valueListenable: time, builder: (_, t, _) => TemporalFrameSet(
          source: path, times: [t], builder: (frames) => RawImage(image: frames.first),
          fallback: const ColoredBox(key: ValueKey('live-fallback'), color: Colors.purple),
        ),
      )),
    ))));
    Future<int> red() async {
      final img = await (boundary.currentContext!.findRenderObject() as RenderRepaintBoundary).toImage();
      final bytes = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
      final v = bytes!.getUint8((32 * 64 + 32) * 4); img.dispose(); return v;
    }
    var previous = -1;
    for (final frame in [24, 25, 28, 29, 30, 31, 32, 34, 56, 58, 59, 60, 61, 62]) {
      time.value = Duration(microseconds: (frame * 1e6 / 30).round());
      final until = DateTime.now().add(const Duration(seconds: 5));
      while (cache.quadro(path, time.value, prefetch: false) == null && DateTime.now().isBefore(until)) {
        await tester.pump(const Duration(milliseconds: 16));
      }
      expect(cache.quadro(path, time.value, prefetch: false), isNotNull);
      await tester.pump();
      final value = await red();
      expect(value, greaterThanOrEqualTo(previous - 1), reason: 'Frame $frame went backwards');
      previous = value;
      expect(find.byKey(const ValueKey('live-fallback')), findsNothing);
    }
    QuadrosDeVideo.desligado = true;
    time.value = const Duration(milliseconds: 2300);
    await tester.pump();
    expect(await red(), previous, reason: 'A cache miss must retain the complete sample, not switch clocks');
    expect(find.byKey(const ValueKey('live-fallback')), findsNothing);
    await tester.pumpWidget(const SizedBox.shrink());
  });
  testWidgets('Android: actual MP4 export across cut with RGB, Posterize, Tile and Time Remap', (tester) async {
    final dir = await getTemporaryDirectory();
    final path = '${dir.path}/export-regression-source.mp4';
    final generated = await FFmpegKit.executeWithArguments([
      '-y', '-v', 'error', '-f', 'lavfi', '-i',
      'testsrc2=size=160x240:rate=30:duration=3',
      '-c:v', 'mpeg4', '-threads', '1', '-q:v', '3', path,
    ]);
    expect(ReturnCode.isSuccess(await generated.getReturnCode()), isTrue);
    final prefs = await SharedPreferences.getInstance();
    final c = ProviderContainer(overrides: [sharedPreferencesProvider.overrideWithValue(prefs)]);
    addTearDown(c.dispose);
    c.read(editorControllerProvider.notifier).openProject(VideoProject(
      name: 'Export regression', createdAt: DateTime.now(), aspectRatio: 2/3,
      resolutionHeight: 160, fps: 15, layers: [
        for (var i = 0; i < 2; i++) VideoLayer(
          id: 'cut$i', name: 'Cut $i', sourcePath: path,
          startTime: Duration(milliseconds: i * 500),
          duration: const Duration(milliseconds: 500),
          sourceDuration: const Duration(seconds: 3),
          sourceOffset: Duration(seconds: i), volume: 0,
          proporcaoDaFonte: 2/3, position: AnimatedOffset(const Offset(80, 120)),
          scaleX: AnimatedDouble(.4), scaleY: AnimatedDouble(.4),
          effects: [EffectInstance(type: EffectType.posterizeTime),
            EffectInstance(type: EffectType.rgbTimeWarp),
            EffectInstance(type: EffectType.motionTile),
            EffectInstance(type: EffectType.deepGlow)],
        ),
      ],
    ));
    c.read(editorControllerProvider.notifier).ligarCurvaDeTempo('cut1', true);
    await tester.pumpWidget(UncontrolledProviderScope(container: c,
      child: const MaterialApp(home: ExportVideoScreen(settings: ExportSettings(fps: 15)))));
    await tester.tap(find.text('Exportar'));
    final until = DateTime.now().add(const Duration(seconds: 90));
    while (find.text('Video pronto').evaluate().isEmpty && DateTime.now().isBefore(until)) {
      await tester.pump(const Duration(milliseconds: 50));
      expect(tester.takeException(), isNull);
    }
    expect(find.text('Video pronto'), findsOneWidget);
    final output = tester.widgetList<Text>(find.byWidgetPredicate((w) => w is Text)).map((t) => t.data ?? '')
        .firstWhere((t) => t.startsWith('/') && t.endsWith('.mp4'));
    expect(await File(output).length(), greaterThan(1000));
    final raw = '${dir.path}/export-regression.rgba';
    final decoded = await FFmpegKit.executeWithArguments([
      '-y', '-v', 'error', '-i', output, '-an', '-pix_fmt', 'rgba', '-f', 'rawvideo', raw,
    ]);
    expect(ReturnCode.isSuccess(await decoded.getReturnCode()), isTrue);
    final bytes = await File(raw).readAsBytes();
    const stride = 160 * 240 * 4;
    // Projects retain a five-second minimum, even with only one second of clips.
    final expectedFrames = (c.read(editorControllerProvider).duration.inMicroseconds * 15 / 1e6).ceil();
    expect(bytes.length ~/ stride, expectedFrames);
    for (final frame in [0, 7, 8, 14]) {
      for (final xy in [(2, 2), (157, 237)]) {
        final p = frame * stride + (xy.$2 * 160 + xy.$1) * 4;
        expect(bytes[p] + bytes[p+1] + bytes[p+2], greaterThan(10),
          reason: 'Export frame $frame corner $xy must not be black');
      }
    }
    debugPrint('Verified MP4: $output, $expectedFrames decoded frames, both sides of cut visible');
    await tester.pumpWidget(const SizedBox.shrink());
  }, timeout: const Timeout(Duration(minutes: 3)));
  testWidgets('Android: temporal cache, live parameter changes and full stack at 40%', (tester) async {
    final dir = await getTemporaryDirectory();
    final path = '${dir.path}/temporal-regression.mp4';
    final session = await FFmpegKit.executeWithArguments([
      '-y', '-v', 'error', '-f', 'lavfi', '-i',
      'testsrc2=size=160x240:rate=30:duration=6',
      '-c:v', 'mpeg4', '-threads', '1', '-q:v', '3', path,
    ]);
    expect(ReturnCode.isSuccess(await session.getReturnCode()), isTrue);
    final cache = QuadrosDeVideo.instance;
    cache.limpar();
    var heartbeats = 0;
    final heartbeat = Timer.periodic(const Duration(milliseconds: 20), (_) => heartbeats++);
    addTearDown(heartbeat.cancel);
    Future<ui.Image> ready(Duration t) async {
      final until = DateTime.now().add(const Duration(seconds: 12));
      while (DateTime.now().isBefore(until)) {
        final frame = cache.quadro(path, t);
        if (frame != null) return frame.clone();
        await tester.pump(const Duration(milliseconds: 25));
      }
      throw StateError('Temporal frame timeout at $t');
    }
    for (final ms in [0, 33, 100, 1700, 3400, 333, 2400]) {
      final frame = await ready(Duration(milliseconds: ms));
      expect(frame.width, lessThanOrEqualTo(360)); frame.dispose();
    }
    expect(heartbeats, greaterThan(5), reason: 'UI event loop must keep running during decode');
    expect(cache.imagensEmCache, lessThanOrEqualTo(64));
    expect(cache.extracoesPendentes, lessThanOrEqualTo(6));

    var poster = EffectInstance(type: EffectType.posterizeTime);
    var warp = EffectInstance(type: EffectType.rgbTimeWarp)
        .withParamEdited('red_frames', Duration.zero, -3)
        .withParamEdited('blue_frames', Duration.zero, 3);
    final layer = VideoLayer(id: 'clip', name: 'Temporal', sourcePath: path,
      startTime: Duration.zero, duration: const Duration(seconds: 6),
      sourceDuration: const Duration(seconds: 6), proporcaoDaFonte: 2 / 3,
      position: AnimatedOffset(const Offset(80, 120)),
      scaleX: AnimatedDouble(.4), scaleY: AnimatedDouble(.4),
      effects: [poster, warp, EffectInstance(type: EffectType.motionTile)]);
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final project = VideoProject(name: 'Regression', createdAt: DateTime.now(), aspectRatio: 2/3,
      resolutionHeight: 160, layers: [layer]);
    c.read(editorControllerProvider.notifier).openProject(project);
    final time = ValueNotifier(const Duration(milliseconds: 1000));
    addTearDown(time.dispose);
    final videos = VideoLayerManager(); addTearDown(videos.dispose);
    final boundary = GlobalKey();
    await tester.pumpWidget(UncontrolledProviderScope(container: c, child: MaterialApp(home: Center(
      child: RepaintBoundary(key: boundary, child: SizedBox(width: 160, height: 240,
        child: CompositionView(time: time, videos: videos, selectedId: null))),
    ))));
    for (final fps in [12.0, 6.0, 24.0, 10.0, 30.0]) {
      c.read(editorControllerProvider.notifier).editEffectParam('clip', poster.id, 'frame_rate', time.value, fps);
      for (var i = 0; i < 8; i++) {
        time.value += const Duration(milliseconds: 33);
        await tester.pump(const Duration(milliseconds: 33));
      }
      expect(tester.takeException(), isNull);
    }
    time.value = const Duration(seconds: 1);
    for (final ms in [900, 1000, 1100]) { (await ready(Duration(milliseconds: ms))).dispose(); }
    for (var i=0;i<8;i++) { await tester.pump(const Duration(milliseconds: 40)); }
    final image = await (boundary.currentContext!.findRenderObject() as RenderRepaintBoundary).toImage();
    final bytes = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!.buffer.asUint8List();
    for (final xy in [(2,2), (157,2), (2,237), (157,237)]) {
      final p = (xy.$2 * image.width + xy.$1)*4;
      expect(bytes[p]+bytes[p+1]+bytes[p+2], greaterThan(10), reason: 'Motion Tile stack corner $xy');
    }
    image.dispose();
    await tester.pumpWidget(const SizedBox.shrink());
    cache.limpar();
  }, timeout: const Timeout(Duration(minutes: 3)));
}
