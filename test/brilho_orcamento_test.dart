// O GLOW TRAVAVA O APP, E O FREIO NAO EXISTIA.
//
// O kernel do brilho (`shaders/luz.frag`) e o mais caro do app e era o
// unico sem adaptacao de custo. Cada amostra dele e um bilinear de QUATRO
// leituras de textura — o `ImageFilter.shader` entrega a entrada sem
// interpolar, entao o bilinear e na mao. Com 64 amostras sao 256 leituras
// POR PIXEL; com 96, 384. Numa previa 1080x1920 a 3x isso passa de quatro
// bilhoes de leituras por quadro: nao e erro, e o app parado esperando a
// GPU. E o "travou" do relato.
//
// O freio e um orcamento de amostras no proprio shader (float 80 do bloco
// de uniformes). Estes testes prendem as tres coisas que fazem o freio
// valer: o shader tem o uniforme NA POSICAO certa, so quem o declara o
// recebe, e a previa pede menos que a exportacao.
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/amostras_do_brilho.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/estilizar_lote2.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/luz_e_diversos.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// Os efeitos que passam pelo `luz.frag`.
final _doBrilho = <EffectType>[
  for (final e in receitasSapphire.entries)
    if (e.value.asset == 'shaders/luz.frag') e.key,
];

String _shader() =>
    File('shaders/luz.frag').readAsStringSync();

/// Os uniforms de float NA ORDEM DE DECLARACAO.
///
/// O `impellerc --reflection-json` mostra que os uniforms de runtime
/// effect sao indexados por ordem de declaracao, SEM contar o
/// preenchimento de alinhamento do std140 (um `vec2` depois de um `float`
/// deixa um buraco de 4 bytes que NAO entra na contagem). Conferido com:
///
///   impellerc --runtime-stage-vulkan --input=shaders/luz.frag
///     --input-type=frag --reflection-json=...
///
/// — `p0` em 8, `c0` em 72, `uAmostras` em 80, bloco de 81 floats.
List<String> _uniformesEmOrdem(String fonte) {
  final out = <String>[];
  final re = RegExp(r'^uniform\s+(vec2|vec4|float|sampler2D)\s+(\w+)\s*;',
      multiLine: true);
  for (final m in re.allMatches(fonte)) {
    final tipo = m.group(1)!;
    if (tipo == 'sampler2D') continue;
    final floats = tipo == 'vec2' ? 2 : (tipo == 'vec4' ? 4 : 1);
    for (var i = 0; i < floats; i++) {
      out.add('${m.group(2)}${floats > 1 ? '.${'xyzw'[i]}' : ''}');
    }
  }
  return out;
}

void main() {
  group('o orcamento esta na posicao certa do bloco', () {
    test('uAmostras e o ultimo uniforme, no float 80', () {
      final ordem = _uniformesEmOrdem(_shader());
      expect(
        ordem.length,
        81,
        reason: 'o bloco do luz.frag tem 81 floats (80 + o orcamento)',
      );
      expect(ordem[80], 'uAmostras');
      // As ancoras que o Dart ja escreve. Se alguem inserir um uniforme
      // antes delas, TUDO desliza e os efeitos pintam errado em silencio —
      // e este teste e o que avisa.
      expect(ordem[0], 'uSize.x');
      expect(ordem[2], 'uFilter');
      expect(ordem[3], 'uLogico.x', reason: 'o padding do std140 nao conta');
      expect(ordem[5], 'uEscalaRef');
      expect(ordem[6], 'uTempo');
      expect(ordem[7], 'uModo');
      expect(ordem[8], 'p0.x');
      expect(ordem[72], 'c0.x');
    });

    test('o helper le o orcamento e nunca pede menos de 4 amostras', () {
      final s = _shader();
      expect(s, contains('int amostras(int maximo)'));
      expect(s, contains('clamp(uAmostras, 0.0625, 1.0)'));
      expect(s, contains('max(4.0,'));
    });

    test('orcamento NAO escrito vale o maximo, e nao o minimo', () {
      // UM UNIFORME QUE NINGUEM ESCREVEU CHEGA COMO ZERO. Com o zero
      // virando a fracao minima, quem esquecesse de mandar o orcamento
      // recebia 6% das amostras e via um brilho fraco — um defeito que
      // aparece longe da causa. A economia tem de ser uma ESCOLHA.
      //
      // Este teste nasceu de um defeito de verdade: o
      // `test/estilizar_sapphire_test.dart` escreve os uniformes a mao e
      // NAO conhece o orcamento. Ele passou a falhar de vez em quando
      // (16 pixels mudados onde exigia mais de 20) — intermitente, que e
      // como um efeito fraco demais se manifesta.
      expect(_shader(), contains('uAmostras <= 0.0 ? 1.0'));

      // E a conta: nao escrito (0) tem de dar o mesmo que cheio (1).
      int amostrasDoShader(double escrito, int maximo) {
        final f = escrito <= 0 ? 1.0 : escrito.clamp(0.0625, 1.0);
        final n = (maximo * f + 0.5).floor();
        return n < 4 ? 4 : n;
      }

      for (final maximo in [16, 20, 24, 40, 64, 96]) {
        expect(
          amostrasDoShader(0, maximo),
          amostrasDoShader(1, maximo),
          reason: 'kernel de $maximo',
        );
        expect(amostrasDoShader(0, maximo), maximo);
      }
    });

    test('os kernels sairam do numero fixo para o numero medido', () {
      final s = _shader();
      // Cada laco constante sobreviveu como TETO (o GLSL do runtime effect
      // exige limite constante) com a saida antecipada que o orcamento manda.
      expect(
        'if (i >= n) break;'.allMatches(s).length,
        3,
        reason: 'brilho, raios de borda e o campo de aura/escuridao',
      );
      expect('if (s >= n) break;'.allMatches(s).length, 1, reason: 'glint');
      expect('if (a >= n) break;'.allMatches(s).length, 1, reason: 'aneis');
      expect('if (i >= nh) break;'.allMatches(s).length, 1, reason: 'halo');
      // E os lacos que perderam o numero fixo o perderam de verdade.
      for (final fixo in [
        '(float(i) + .5) / 64.0',
        '(float(s) + salto) / 24.0',
        'float(a) * 6.2831853 / 20.0',
        '(float(i) + .5) / 16.0',
        'float(i) / 40.0',
        '(float(i) + .5) / 96.0',
        'campo /= 96.0',
      ]) {
        expect(s, isNot(contains(fixo)), reason: 'sobrou o numero fixo: $fixo');
      }
    });
  });

  group('so quem declara o uniforme o recebe', () {
    test('os efeitos do luz.frag pedem o orcamento', () {
      expect(_doBrilho, isNotEmpty);
      for (final t in _doBrilho) {
        expect(
          receitasSapphire[t]!.usaOrcamentoDeAmostras,
          isTrue,
          reason: '${effectSpecs[t]?.name ?? t.name} ficou sem o freio',
        );
      }
    });

    test('nenhum outro shader recebe o float 80', () {
      // Escrever o float 80 num shader que NAO declara `uAmostras` cai no
      // meio de outra coisa: no `jpeg_damage.frag` aquela posicao e a
      // tabela de quantizacao, e o efeito sairia corrompido sem erro
      // nenhum — o pior tipo de defeito.
      for (final e in receitasSapphire.entries) {
        if (e.value.asset == 'shaders/luz.frag') continue;
        expect(
          e.value.usaOrcamentoDeAmostras,
          isFalse,
          reason: '${e.key.name} usa ${e.value.asset}, que nao tem uAmostras',
        );
      }
    });

    test('nenhum outro shader do lote declara 81 floats', () {
      // A prova de que o opt-in e obrigatorio, e nao zelo: quem manda no
      // numero de floats e o proprio shader.
      final outros = <String>{
        for (final r in receitasSapphire.values) r.asset,
      }..remove('shaders/luz.frag');
      for (final asset in outros) {
        final ordem = _uniformesEmOrdem(File(asset).readAsStringSync());
        expect(
          ordem.contains('uAmostras'),
          isFalse,
          reason: '$asset passou a declarar uAmostras: reveja o opt-in',
        );
      }
    });
  });

  group('o orcamento por tamanho de trabalho', () {
    test('exportacao sem desconto, previa no meio, rascunho no fundo', () {
      expect(AmostrasDoBrilho.para(exportando: true, tocando: true), 1);
      expect(AmostrasDoBrilho.para(exportando: true, tocando: false), 1);
      expect(
        AmostrasDoBrilho.para(exportando: false, tocando: false),
        AmostrasDoBrilho.previa,
      );
      expect(
        AmostrasDoBrilho.para(exportando: false, tocando: true),
        AmostrasDoBrilho.rascunho,
      );
      expect(AmostrasDoBrilho.rascunho, lessThan(AmostrasDoBrilho.previa));
      expect(AmostrasDoBrilho.previa, lessThan(AmostrasDoBrilho.exportacao));
    });

    test('o pior kernel da previa custa uma fracao do de exportacao', () {
      const piorKernel = 96; // S_GlowAura e S_GlowDarks
      const leiturasPorAmostra = 4; // o bilinear na mao
      int leituras(double fracao) =>
          (piorKernel * fracao + 0.5).floor() * leiturasPorAmostra;

      expect(leituras(AmostrasDoBrilho.exportacao), 384);
      expect(leituras(AmostrasDoBrilho.previa), 168);
      expect(leituras(AmostrasDoBrilho.rascunho), 72);
      // E o piso do shader (4 amostras) vale mesmo com fracao minima.
      expect(
        (piorKernel * 0.0625 + 0.5).floor(),
        greaterThanOrEqualTo(4),
        reason: 'o clamp do shader nunca deixa o kernel virar nada',
      );
    });

    test('todos os efeitos do luz.frag tem valores finitos', () {
      for (final t in _doBrilho) {
        final v = valoresLuz(EffectInstance(type: t), Duration.zero);
        expect(v.where((x) => !x.isFinite), isEmpty, reason: t.name);
        // O indice do kernel tem de caber no bloco (p0.x ate p2.y).
        expect(v.length, lessThanOrEqualTo(20), reason: t.name);
      }
    });
  });

  _naTela();
}

// ------------------------------------------------------------- na tela
//
// O CAMINHO INTEIRO QUE O TESTADOR PERCORRE: adicionar o brilho, ver a
// imagem mudar, mexer num numero, animar, empilhar dois, tirar um, salvar
// e reabrir. Nenhum passo pode estourar nem sumir com a camada.
void _naTela() {
  Future<void> render(WidgetTester tester, List<Layer> layers) async {
    final project = VideoProject(
      name: 'brilho',
      createdAt: DateTime(2026, 9, 17),
      aspectRatio: 1,
      resolutionHeight: 300,
      layers: layers,
    );
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(project);
    final time = ValueNotifier<Duration>(const Duration(milliseconds: 400));
    final videos = VideoLayerManager();
    final key = GlobalKey();
    tester.view.physicalSize = const Size(300, 300);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          debugShowCheckedModeBanner: false,
          home: Material(
            color: Colors.black,
            child: Center(
              child: RepaintBoundary(
                key: key,
                child: SizedBox(
                  width: 300,
                  height: 300,
                  child: ColoredBox(
                    color: Colors.black,
                    child: CompositionView(
                      time: time,
                      videos: videos,
                      selectedId: null,
                      exporting: true,
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pump();
    await tester.pump();

    late ByteData dados;
    await tester.runAsync(() async {
      final obj =
          key.currentContext!.findRenderObject() as RenderRepaintBoundary;
      final img = await obj.toImage(pixelRatio: 1);
      dados = (await img.toByteData(format: ui.ImageByteFormat.rawRgba))!;
      img.dispose();
    });
    // A camada tem de chegar DESENHADA: uma foto vazia passaria no teste
    // sem provar nada.
    expect(dados.lengthInBytes, 300 * 300 * 4);
    expect(
      dados.buffer.asUint8List().where((b) => b != 0).length,
      greaterThan(0),
      reason: 'a composicao saiu preta',
    );
  }

  ShapeLayer claro({List<EffectInstance> effects = const []}) => ShapeLayer(
    id: 'claro',
    name: 'claro',
    startTime: Duration.zero,
    duration: const Duration(seconds: 2),
    position: AnimatedOffset(const Offset(150, 150)),
    contents: [
      ShapeParametric(
        kind: ParamShapeKind.rect,
        sizeX: AnimatedDouble(160),
        sizeY: AnimatedDouble(160),
      ),
      ShapeFill(color: const Color(0xFFFFFFFF)),
    ],
    effects: effects,
  );

  for (final t in _doBrilho) {
    testWidgets('aplica e desenha: ${effectSpecs[t]!.name}', (tester) async {
      await render(tester, [
        claro(effects: [EffectInstance(type: t)]),
      ]);
    });
  }

  testWidgets('os tres mais pesados empilhados na mesma camada', (
    tester,
  ) async {
    await render(tester, [
      claro(
        effects: [
          EffectInstance(type: EffectType.deepGlow),
          EffectInstance(type: EffectType.sGlowAura),
          EffectInstance(type: EffectType.sGlowDarks),
        ],
      ),
    ]);
  });

  testWidgets('o preset mais forte do brilho desenha', (tester) async {
    final spec = effectSpecs[EffectType.deepGlow]!;
    var fx = EffectInstance(type: EffectType.deepGlow);
    for (final pronto in spec.presets) {
      fx = fx.withPreset(pronto);
    }
    expect(fx.paramAt('raio', Duration.zero), 300, reason: 'o preset Aura');
    await render(tester, [
      claro(effects: [fx]),
    ]);
  });

  test('adicionar, animar, tirar e reabrir sem perder a camada', () {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(
      VideoProject(
        name: 'brilho',
        createdAt: DateTime(2026, 9, 17),
        layers: [claro()],
      ),
    );

    // ADICIONAR
    c.addEffect('claro', EffectType.deepGlow);
    expect(c.state.layerById('claro')!.effects, hasLength(1));
    final novo = c.state.layerById('claro')!.effects.single;

    // MEXER NUM NUMERO
    c.editEffectParam('claro', novo.id, 'raio', Duration.zero, 420);
    final fx = c.state.layerById('claro')!.effects.single;
    expect(fx.paramAt('raio', Duration.zero), 420);

    // ANIMAR: keyframe no inicio, e o do fim cravado pelo LOSANGO.
    //
    // KEYFRAME NAO NASCE DE EDITAR VALOR (`docs/keyframe-explicito.md`).
    // Num efeito que JA anima, mexer num numero FORA de uma marca nao
    // grava nada: o valor fica como EDICAO PENDENTE — a previa ja mostra,
    // a linha do tempo nao muda — e quem crava e o losango daquele
    // instante. O interruptor "keyframe automatico" existe para quem
    // quiser o atalho, e nasce desligado.
    c.toggleEffectKeyframe('claro', novo.id, Duration.zero);
    c.editEffectParam('claro', novo.id, 'raio', const Duration(seconds: 2), 80);
    expect(
      c.state.layerById('claro')!.effects.single.keyframeTimes,
      everyElement(Duration.zero),
      reason: 'editar fora da marca nao pode cravar keyframe sozinho',
    );
    expect(
      container.read(edicaoPendenteProvider),
      isNotNull,
      reason: 'o valor novo fica pendente esperando o losango',
    );
    c.toggleEffectKeyframe('claro', novo.id, const Duration(seconds: 2));
    expect(
      container.read(edicaoPendenteProvider),
      isNull,
      reason: 'o losango gravou: nao sobra pendencia',
    );
    final animado = c.state.layerById('claro')!.effects.single;
    expect(animado.hasAnimation, isTrue);
    expect(animado.paramAt('raio', Duration.zero), 420);
    expect(animado.paramAt('raio', const Duration(seconds: 2)), 80);

    // SALVAR E REABRIR com o efeito animado.
    final outro = ProviderContainer();
    addTearDown(outro.dispose);
    final c2 = outro.read(editorControllerProvider.notifier);
    c2.openProject(projectFromJson(projectToJson(c.state)));
    final relido = c2.state.layerById('claro')!.effects.single;
    expect(relido.type, EffectType.deepGlow);
    expect(relido.hasAnimation, isTrue);
    expect(relido.paramAt('raio', Duration.zero), 420);
    expect(relido.paramAt('raio', const Duration(seconds: 2)), 80);

    // TIRAR.
    c.removeEffect('claro', animado.id);
    expect(c.state.layerById('claro')!.effects, isEmpty);
    expect(
      c.state.layerById('claro'),
      isNotNull,
      reason: 'tirar o efeito nao pode levar a camada junto',
    );
  });

  test('muitos brilhos numa camada e em varias: nada se perde', () {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(
      VideoProject(
        name: 'brilho',
        createdAt: DateTime(2026, 9, 17),
        layers: [claro(), claro()],
      ),
    );
    for (var i = 0; i < 4; i++) {
      for (final t in _doBrilho) {
        c.addEffect('claro', t);
      }
    }
    expect(
      c.state.layerById('claro')!.effects,
      hasLength(4 * _doBrilho.length),
    );
    // Um desfazer volta o lote inteiro de uma vez.
    c.undo();
    expect(c.state.layerById('claro')!.effects, isEmpty);
  });
}
