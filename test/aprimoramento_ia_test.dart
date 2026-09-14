import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/aprimoramento_ia.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/export/application/export_engine.dart';
import 'package:aurea/src/features/export/domain/video_color.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// APRIMORAMENTO POR IA NO EDITOR: quando entra, em que resolucao a rede
/// trabalha, como o clipe guarda a escolha e o que o FFmpeg recebe.
VideoLayer _clipe() => VideoLayer(
  name: 'v',
  startTime: Duration.zero,
  duration: const Duration(seconds: 2),
  sourcePath: '/v.mp4',
  position: AnimatedOffset(const Offset(960, 540)),
);

void main() {
  group('contas', () {
    test('encaixe igual ao do FFmpeg (amplia e reduz, mantendo proporcao)', () {
      expect(encaixar(640, 360, 1920, 1080), (1920, 1080));
      expect(encaixar(360, 640, 1920, 1080), (608, 1080));
      expect(encaixar(3840, 2160, 1080, 1920), (1080, 608));
      expect(encaixar(0, 360, 1920, 1080), (0, 0));
    });

    test('rotacao de 90/270 troca os lados; 180 nao', () {
      expect(dimensoesExibidas(1920, 1080, -90), (1080, 1920));
      expect(dimensoesExibidas(1920, 1080, 270), (1080, 1920));
      expect(dimensoesExibidas(1920, 1080, 180), (1920, 1080));
      expect(dimensoesExibidas(1920, 1080, 0), (1920, 1080));
    });

    test('entrada da rede: a propria fonte, reduzida so acima de 960x540', () {
      expect(entradaDaIa(640, 360), (640, 360));
      expect(entradaDaIa(960, 540), (960, 540));
      expect(entradaDaIa(1280, 720), (960, 540));
      final (w, h) = entradaDaIa(1080, 1920);
      expect(w * h, lessThanOrEqualTo(areaMaximaDaEntradaDaIa));
      expect(w / h, closeTo(1080 / 1920, .01));
    });

    test('escala: a menor que cobre o encaixe, com folga de arredondamento', () {
      expect(escalaDaIa(960, 540, 1920, 1080), 2);
      expect(escalaDaIa(959, 540, 1920, 1080), 2);
      expect(escalaDaIa(640, 360, 1920, 1080), 4);
      expect(escalaDaIa(480, 270, 3840, 2160), 4, reason: 'teto do modelo');
      expect(escalaDaIa(1920, 1080, 1920, 1080), 1);
    });
  });

  group('plano', () {
    PlanoDeAprimoramento plano({
      bool ligado = true,
      bool motor = true,
      int w = 640,
      int h = 360,
      int rotacao = 0,
      int cw = 1920,
      int ch = 1080,
    }) => planoDeAprimoramento(
      ligado: ligado,
      motorDisponivel: motor,
      larguraDaFonte: w,
      alturaDaFonte: h,
      rotacao: rotacao,
      larguraDaComposicao: cw,
      alturaDaComposicao: ch,
    );

    test('360p numa composicao 1080p: IA 640x360 -> 1920x1080 (x4)', () {
      final p = plano();
      expect(p.aplica, isTrue);
      expect(p.entrada, (640, 360));
      expect(p.saida, (1920, 1080));
      expect(p.escala, 4);
      expect(p.emPalavras, 'IA 640x360 -> 1920x1080 (x4)');
    });

    test('720p numa composicao 1080p: a fonte cai para 960x540 e sobe x2', () {
      final p = plano(w: 1280, h: 720);
      expect(p.aplica, isTrue);
      expect(p.entrada, (960, 540));
      expect(p.escala, 2);
    });

    test('video que ja tem a resolucao nao passa pela rede', () {
      expect(plano(w: 1920, h: 1080).motivo, MotivoDoAprimoramento.jaTemResolucao);
      expect(
        plano(w: 1280, h: 720, cw: 1280, ch: 720).motivo,
        MotivoDoAprimoramento.jaTemResolucao,
      );
    });

    test('video em pe gravado deitado: a rotacao decide o encaixe', () {
      // 1080x1920 exibido numa composicao vertical: ja tem a resolucao.
      expect(
        plano(w: 1920, h: 1080, rotacao: -90, cw: 1080, ch: 1920).motivo,
        MotivoDoAprimoramento.jaTemResolucao,
      );
      // 640x360 girado (360x640) na vertical 1080x1920: 720 nao cobre
      // 1080, entao a rede sobe x4 e a saida e reduzida ao encaixe.
      final p = plano(rotacao: 90, cw: 1080, ch: 1920);
      expect(p.aplica, isTrue);
      expect(p.entrada, (360, 640));
      expect(p.saida, (1080, 1920));
      expect(p.escala, 4);
    });

    test('desligado, sem motor ou sem tamanho da fonte: diz por que', () {
      expect(plano(ligado: false).motivo, MotivoDoAprimoramento.desligado);
      expect(plano(motor: false).motivo, MotivoDoAprimoramento.semMotor);
      expect(plano(w: 0).motivo, MotivoDoAprimoramento.fonteDesconhecida);
      expect(plano(motor: false).emPalavras, contains('indisponível'));
    });
  });

  group('extracao', () {
    const cor = CorDoVideo(matriz: 'bt709', faixa: 'tv', largura: 1280, altura: 720);

    test('sem IA, as receitas sao as de sempre (encaixe na composicao)', () {
      final r = receitasDeExtracao(cor, fps: 30, largura: 1920, altura: 1080);
      expect(r.first, contains('scale=1920:1080:force_original_aspect_ratio=decrease'));
      expect(r.last, 'fps=30,scale=1920:1080:force_original_aspect_ratio=decrease,format=rgb24');
    });

    test('para a IA: a fonte sem ampliar, com teto de area, em todas as receitas', () {
      final r = receitasDeExtracao(
        const CorDoVideo(transferencia: 'smpte2084', largura: 3840, altura: 2160),
        fps: 30,
        largura: 1920,
        altura: 1080,
        areaMaximaParaIa: areaMaximaDaEntradaDaIa,
      );
      expect(r, hasLength(3), reason: 'HDR, SDR e reserva');
      const k = 'min(1,sqrt(518400/(iw*ih)))';
      for (final vf in r) {
        expect(vf, contains("scale=w='max(1,trunc(iw*$k))':h='max(1,trunc(ih*$k))'"));
        expect(vf, isNot(contains('force_original_aspect_ratio')));
        expect(vf, endsWith('format=rgb24'));
      }
    });

    test('rotacao pelo ffprobe: matriz de exibicao ou etiqueta antiga', () {
      expect(rotacaoDeProps({'side_data_list': [{'rotation': -90}]}), 270);
      expect(rotacaoDeProps({'side_data_list': [{'side_data_type': 'x'}, {'rotation': 180}]}), 180);
      expect(rotacaoDeProps({'tags': {'rotate': '90'}}), 90);
      expect(rotacaoDeProps({}), 0);
      expect(rotacaoDeProps({'tags': {'rotate': 'lixo'}}), 0);
    });
  });

  group('clipe', () {
    test('liga, muda a forca, desfaz e grava no projeto', () {
      final container = ProviderContainer();
      addTearDown(container.dispose);
      final editor = container.read(editorControllerProvider.notifier);
      final clipe = _clipe();
      editor.openProject(
        VideoProject(name: 'p', createdAt: DateTime(2026, 9, 14), layers: [clipe]),
      );
      VideoLayer v() => container.read(editorControllerProvider).layerById(clipe.id)! as VideoLayer;

      expect(v().aprimorar, isFalse);
      expect(v().forcaDoAprimoramento, 1.0);
      editor.setClipAprimoramento(clipe.id, ligado: true);
      expect(v().aprimorar, isTrue);
      editor.setClipAprimoramento(clipe.id, forca: 7);
      expect(v().forcaDoAprimoramento, 1.0, reason: 'fora da faixa vira o teto');
      editor.setClipAprimoramento(clipe.id, forca: .35);
      expect(v().forcaDoAprimoramento, .35);
      editor.setClipAprimoramento(clipe.id, forca: double.nan);
      expect(v().forcaDoAprimoramento, .35, reason: 'NaN nao entra');

      // Desligar guarda a forca: religar volta como estava.
      editor.setClipAprimoramento(clipe.id, ligado: false);
      expect(v().forcaDoAprimoramento, .35);

      final json = projectToJson(container.read(editorControllerProvider));
      final lido = projectFromJson(json).layers.single as VideoLayer;
      expect(lido.aprimorar, isFalse);
      expect(lido.forcaDoAprimoramento, .35);

      editor.setClipAprimoramento(clipe.id, ligado: true);
      final lido2 = projectFromJson(projectToJson(container.read(editorControllerProvider))).layers.single as VideoLayer;
      expect(lido2.aprimorar, isTrue);

      // Duplicar e dividir levam a escolha.
      expect(v().duplicated().aprimorar, isTrue);
      editor.splitLayer(clipe.id, const Duration(seconds: 1));
      final metades = container.read(editorControllerProvider).layers.whereType<VideoLayer>();
      expect(metades, hasLength(2));
      expect(metades.every((m) => m.aprimorar && m.forcaDoAprimoramento == .35), isTrue);
    });

    test('projeto antigo, sem os campos, abre desligado e com forca 1', () {
      final json = projectToJson(
        VideoProject(name: 'p', createdAt: DateTime(2026, 9, 14), layers: [_clipe()]),
      );
      final camada = (json['layers'] as List).single as Map<String, dynamic>;
      expect(camada.containsKey('aprimorar'), isFalse, reason: 'padrao nao suja o arquivo');
      expect(camada.containsKey('forcaAprimoramento'), isFalse);
      final lido = projectFromJson(json).layers.single as VideoLayer;
      expect(lido.aprimorar, isFalse);
      expect(lido.forcaDoAprimoramento, 1.0);
    });

    test('clipe aprimorado nunca vira corte puro (copia sem recodificar)', () {
      // 6 s: o projeto tem no minimo 5 s, e o corte puro cobre o relogio todo.
      final neutro = VideoLayer(
        name: 'v',
        startTime: Duration.zero,
        duration: const Duration(seconds: 6),
        sourcePath: '/v.mp4',
      );
      VideoProject projeto(VideoLayer l) =>
          VideoProject(name: 'p', createdAt: DateTime(2026, 9, 14), layers: [l]);
      final base = ExportEngine(projeto(neutro));
      expect(
        base.pureCutSource,
        isNotNull,
        reason: 'sem o aprimoramento o clipe neutro e copiavel (a trava abaixo mede algo)',
      );
      expect(ExportEngine(projeto(neutro.copyLayer(aprimorar: true))).pureCutSource, isNull);
    });

    test('forca NaN ou fora da faixa no arquivo nao derruba o projeto', () {
      final json = projectToJson(
        VideoProject(name: 'p', createdAt: DateTime(2026, 9, 14), layers: [_clipe()]),
      );
      final camada = (json['layers'] as List).single as Map<String, dynamic>;
      camada['aprimorar'] = true;
      camada['forcaAprimoramento'] = 9.5;
      final lido = projectFromJson(json).layers.single as VideoLayer;
      expect(lido.aprimorar, isTrue);
      expect(lido.forcaDoAprimoramento, 1.0);
    });
  });
}
