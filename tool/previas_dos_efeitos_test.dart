// GERADOR DAS PREVIAS ANIMADAS DA GALERIA DE EFEITOS.
//
// Renderiza cada efeito do catalogo pelo motor de verdade (o mesmo
// CompositionView da previa e da exportacao) sobre a amostra de
// amostra_dos_efeitos.dart, em quadrosDaPrevia quadros, e grava uma tira
// JPEG por efeito em assets/efeitos/previas mais o manifesto.
//
// Efeito que nasce neutro (Niveis, Curvas... com os valores iniciais a foto
// sai igual) usa o preset do meio, e o manifesto guarda qual: a galeria
// aplica o mesmo preset ao tocar, para o efeito entregar o que a previa
// mostrou.
//
// So roda de proposito:
//   AUREA_GERAR_PREVIAS=1 flutter test tool/previas_dos_efeitos_test.dart
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/amostra_dos_efeitos.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/foundation.dart' show SynchronousFuture;
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;

import '../test/apoio/print_da_ui.dart' show carregarFontesReais;

/// Diferenca media por canal (0..255) abaixo da qual a previa "e a foto".
const double _limiarNeutro = 1.5;

void main() {
  final gerar = Platform.environment['AUREA_GERAR_PREVIAS'] == '1';

  // Fonte e shader carregam em IO de verdade: dentro do corpo do
  // testWidgets (relogio falso) o await nunca volta.
  ui.Image? foto;
  setUpAll(() async {
    if (!gerar) return;
    TestWidgetsFlutterBinding.ensureInitialized();
    await carregarFontesReais();
    await PixelEffectEngine.warmUp();
    // A FOTO DECODIFICA AQUI, e nao com precacheImage dentro do teste: la
    // o carregamento de arquivo trava o relogio falso (ja travou o teste
    // de print da Comunidade).
    final codec = await ui.instantiateImageCodec(
      File(fotoDaAmostra).readAsBytesSync(),
    );
    foto = (await codec.getNextFrame()).image;
  });

  testWidgets(
    'gera as tiras das previas dos efeitos',
    (tester) async {
      tester.view.physicalSize = const Size(
        ladoDaAmostra * 1.0,
        ladoDaAmostra * 1.0,
      );
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);

      // Conferencia rapida: AUREA_PREVIAS_SO=blur,glow grava so esses, numa
      // pasta de build, sem tocar nos assets nem no manifesto.
      final so = Platform.environment['AUREA_PREVIAS_SO']
          ?.split(',')
          .where((e) => e.trim().isNotEmpty)
          .toSet();
      final pasta = Directory(
        so == null ? pastaDasPrevias : 'build/previas_conferencia',
      )..createSync(recursive: true);
      final chave = GlobalKey();
      final tempo = ValueNotifier(Duration.zero);
      final videos = VideoLayerManager();
      addTearDown(tempo.dispose);
      addTearDown(videos.dispose);

      // O palco desenha a foto com Image.file: a chave e o FileImage do
      // mesmo caminho, entao a foto ja decodificada entra direto no cache.
      PaintingBinding.instance.imageCache.putIfAbsent(
        FileImage(File(fotoDaAmostra)),
        () => OneFrameImageStreamCompleter(
          SynchronousFuture(ImageInfo(image: foto!.clone())),
        ),
      );

      Future<List<Uint8List>> quadros(EffectType? tipo, EffectPronto? p) async {
        final container = ProviderContainer();
        container
            .read(editorControllerProvider.notifier)
            .openProject(amostraDoEfeito(tipo, pronto: p));
        tempo.value = Duration.zero;
        await tester.pumpWidget(
          UncontrolledProviderScope(
            container: container,
            child: MaterialApp(
              debugShowCheckedModeBanner: false,
              home: Align(
                alignment: Alignment.topLeft,
                child: RepaintBoundary(
                  key: chave,
                  child: SizedBox(
                    width: ladoDaAmostra * 1.0,
                    height: ladoDaAmostra * 1.0,
                    child: CompositionView(
                      time: tempo,
                      videos: videos,
                      selectedId: null,
                      exporting: true,
                    ),
                  ),
                ),
              ),
            ),
          ),
        );
        final out = <Uint8List>[];
        for (var i = 0; i < quadrosDaPrevia; i++) {
          tempo.value = instanteDoQuadro(i);
          await tester.pump();
          // Warp e ordenacao de pixel carregam programa e isolate fora do
          // quadro: da tempo de chegar e pinta de novo.
          await tester.runAsync(
            () => Future<void>.delayed(const Duration(milliseconds: 40)),
          );
          await tester.pump();
          final boundary =
              chave.currentContext!.findRenderObject()! as RenderRepaintBoundary;
          final imagem = boundary.toImageSync(
            pixelRatio: ladoDaPrevia / ladoDaAmostra,
          );
          final dados = await tester.runAsync(
            () => imagem.toByteData(format: ui.ImageByteFormat.rawRgba),
          );
          imagem.dispose();
          out.add(dados!.buffer.asUint8List());
        }
        await tester.pumpWidget(const SizedBox.shrink());
        container.dispose();
        return out;
      }

      double diferenca(List<Uint8List> a, List<Uint8List> b) {
        var soma = 0.0;
        var n = 0;
        for (var q = 0; q < a.length; q++) {
          for (var i = 0; i < a[q].length; i += 4) {
            soma += (a[q][i] - b[q][i]).abs() +
                (a[q][i + 1] - b[q][i + 1]).abs() +
                (a[q][i + 2] - b[q][i + 2]).abs();
            n += 3;
          }
        }
        return n == 0 ? 0 : soma / n;
      }

      void gravarTira(String id, List<Uint8List> q) {
        final tira = img.Image(
          width: ladoDaPrevia * quadrosDaPrevia,
          height: ladoDaPrevia,
        );
        img.fill(tira, color: img.ColorRgb8(0, 0, 0));
        for (var i = 0; i < q.length; i++) {
          final quadro = img.Image.fromBytes(
            width: ladoDaPrevia,
            height: ladoDaPrevia,
            bytes: q[i].buffer,
            numChannels: 4,
            order: img.ChannelOrder.rgba,
          );
          img.compositeImage(tira, quadro, dstX: i * ladoDaPrevia);
        }
        File('${pasta.path}/$id.jpg')
            .writeAsBytesSync(img.encodeJpg(tira, quality: 80));
      }

      final amostra = await quadros(null, null);
      gravarTira('_amostra', amostra);
      final manifesto = <String, Object?>{};
      final relatorio = StringBuffer();
      for (final entrada in effectSpecs.entries) {
        final spec = entrada.value;
        if (so != null && !so.contains(spec.id)) continue;
        final candidatos = <int?>[
          null,
          if (spec.presets.length > 1) 1,
          if (spec.presets.isNotEmpty) 0,
          if (spec.presets.length > 2) 2,
        ];
        List<Uint8List>? escolhidos;
        int? preset;
        var melhor = -1.0;
        for (final c in candidatos) {
          final q = await quadros(
            entrada.key,
            c == null ? null : spec.presets[c],
          );
          final d = diferenca(q, amostra);
          if (d > melhor) {
            melhor = d;
            escolhidos = q;
            preset = c;
          }
          if (d >= _limiarNeutro) break;
        }
        gravarTira(spec.id, escolhidos!);
        final neutro = melhor < _limiarNeutro;
        manifesto[spec.id] = {
          'preset': preset,
          if (neutro) 'neutro': true,
        };
        // ignore: avoid_print
        print('${spec.id}: ${melhor.toStringAsFixed(2)}');
        relatorio.writeln(
          '${spec.id}: diferenca ${melhor.toStringAsFixed(2)}'
          '${preset == null ? '' : ' (preset $preset)'}'
          '${neutro ? ' NEUTRO' : ''}',
        );
      }
      if (so != null) {
        // ignore: avoid_print
        print(relatorio);
        return;
      }
      File('${pasta.path}/manifesto.json').writeAsStringSync(
        const JsonEncoder.withIndent('  ').convert({
          'versao': versaoDasPrevias,
          'quadros': quadrosDaPrevia,
          'lado': ladoDaPrevia,
          'fps': fpsDaPrevia,
          'efeitos': manifesto,
        }),
      );
      Directory('build').createSync(recursive: true);
      File('build/relatorio_das_previas.txt')
          .writeAsStringSync(relatorio.toString());
      // ignore: avoid_print
      print(relatorio);
    },
    skip: !gerar,
    timeout: const Timeout(Duration(minutes: 40)),
  );
}
