// A CAMADA DE PARTICULAS — do projeto salvo ate o pixel na tela.
//
// O MOTOR TEM TESTE PROPRIO (`motor_de_particulas_test.dart`): a fisica, o
// lote e o rasterizador sao medidos la. O que se mede AQUI e a COSTURA —
// que a camada entrega a receita certa, que a previa desenha pelo motor, e
// que um projeto gravado antes desta versao abre com a nuvem que ele
// mostrava.
//
// A COSTURA E ONDE OS DEFEITOS MORAM. Um motor perfeito ligado na camada
// errada continua sem desenhar nada, e nenhum teste de fisica repara nisso.
import 'dart:convert';
import 'dart:typed_data';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/preview_resolution.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart'
    show projectFromJson, projectToJson;
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/particulas_painter.dart';
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;

ParticulasLayer _camada({ParametrosDeParticulas? receita}) => ParticulasLayer(
  name: 'Nuvem',
  startTime: Duration.zero,
  duration: const Duration(seconds: 5),
  parametros:
      receita ??
      (MotorDeParticulasRender.preset(0) ?? ParametrosDeParticulas(maximo: 64)),
  position: AnimatedOffset(const Offset(960, 540)),
);

VideoProject _projeto(List<Layer> camadas) => VideoProject(
  name: 'particulas',
  createdAt: DateTime(2026, 9, 18),
  resolutionHeight: 1080,
  aspectRatio: 16 / 9,
  layers: camadas,
);

void main() {
  setUpAll(() {
    expect(
      MotorDeParticulasRender.disponivel,
      isTrue,
      reason: 'o motor de particulas nao carregou',
    );
  });

  test('todo controle avancado invalida a receita nativa', () {
    final base = ParametrosDeParticulas();
    final campos = <ParametrosDeParticulas>[
      base.clonar()..gravidade = 10,
      base.clonar()..ventoX = 10,
      base.clonar()..turbulencia = 10,
      base.clonar()..atracao = 10,
      base.clonar()..tamanho = 40,
      base.clonar()..opacidade = .4,
      base.clonar()..brilho = .8,
      base.clonar()..giroGrausS = 90,
      base.clonar()..tamanhoNaVida = TamanhoNaVida.cresce,
      base.clonar()..opacidadeNaVida = OpacidadeNaVida.fixa,
    ];
    for (final alterada in campos) {
      expect(alterada.assinatura, isNot(base.assinatura));
    }
  });

  group('a camada e um ENVELOPE, e nao um simulador', () {
    test('ela nao guarda nenhum estado de particula', () {
      // A LISTA DE CAMPOS E O CONTRATO. Se um dia aparecer aqui um
      // `posicoes`, um `idades` ou um `cache`, a simulacao voltou a ter
      // dois lugares — e os dois vao discordar.
      final l = _camada();
      expect(l.parametros, isNotNull);
      // O que ela tem e o mesmo que qualquer camada tem: transform.
      expect(l.position.valueAt(Duration.zero), const Offset(960, 540));
      expect(l.is3D, isFalse);
    });

    test('duas camadas com a mesma receita desenham igual', () {
      final a = _camada();
      final b = _camada(receita: a.parametros.clonar());
      final la = LoteDeParticulas(
        a.parametros
          ..centroX = 0
          ..centroY = 0,
      );
      final lb = LoteDeParticulas(
        b.parametros
          ..centroX = 0
          ..centroY = 0,
      );
      la.gerar(1.7);
      lb.gerar(1.7);
      expect(la.quantas, greaterThan(0));
      expect(la.floats, orderedEquals(lb.floats));
      la.liberar();
      lb.liberar();
    });

    test('trocar a receita NAO alcanca a camada antiga', () {
      // A RECEITA E IMUTAVEL DO LADO DE QUEM EDITA: o painel clona antes
      // de mexer. Sem isto o desfazer guardaria o mesmo objeto que a
      // camada nova, e voltar atras nao voltaria nada.
      final l = _camada();
      final antes = l.parametros.tamanho;
      final copia = l.parametros.clonar()..tamanho = antes + 100;
      final nova = l.withParametros(copia);
      expect(l.parametros.tamanho, antes, reason: 'a camada antiga mudou');
      expect(nova.parametros.tamanho, antes + 100);
      expect(nova.id, l.id, reason: 'trocar a receita nao cria camada nova');
    });

    test('duplicar a camada duplica a receita, e nao a compartilha', () {
      final l = _camada();
      final d = l.duplicated();
      d.parametros.tamanho = 999;
      expect(l.parametros.tamanho, isNot(999));
    });
  });

  group('projeto antigo abre com a nuvem que ele mostrava', () {
    test('o emissor 3 de um arquivo antigo e ANEL, e nao linha', () {
      // O NUMERO MUDOU DE SENTIDO entre os motores: no antigo, 3 era um
      // anel no plano XY; no novo, 3 e uma linha e o anel foi para 4. Sem
      // a marca `pv`, um projeto salvo abriria com as particulas nascendo
      // ao longo de um risco em vez de um circulo.
      final antigo = parametrosDeParticulasDoProjeto({
        'count': 120,
        'emitter': 3,
        'emitW': 600,
      });
      expect(antigo.emissor, EmissorDeParticulas.anel);
      expect(antigo.raio, 300, reason: 'o raio era metade da largura');
      expect(antigo.maximo, 120);

      // E O ARQUIVO NOVO: `pv: 2` diz que o numero ja e o do motor novo.
      final novo = parametrosDeParticulasDoProjeto({
        'pv': 2,
        'count': 120,
        'emitter': 3,
      });
      expect(novo.emissor, EmissorDeParticulas.linha);
    });

    test('os campos do motor antigo viram os do motor novo', () {
      final q = parametrosDeParticulasDoProjeto({
        'count': 200,
        'seed': 42,
        'speed': 80,
        'spread': 45,
        'dir': -90,
        'gravity': 120,
        'size': 12,
        'life': 3000,
        'emitW': 800,
        'emitH': 600,
        'twinkle': true,
        'color': 0x35C4E7FF,
        'star': true,
        'emitter': 0,
        'emitMode': 1,
        'windX': 20,
        'windY': -5,
        'drag': 1.5,
        'turb': 30,
        'turbScale': 250,
        'turbSpeed': 2,
        'sizeLife': 2,
        'sizeRnd': 0.7,
        'opLife': 1,
        'opRnd': 0.3,
        'shape': 2,
        'spin': 90,
        'trail': 0.4,
        'lifeRnd': 0.2,
        'glow': 0.6,
        'auxN': 4,
        'auxLife': 900,
        'auxInh': 0.5,
        'auxSpd': 130,
        'auxSize': 0.6,
        'auxStart': 0.3,
      });
      expect(q.maximo, 200);
      expect(q.semente, 42);
      expect(q.velocidade, 80);
      expect(q.aberturaGraus, 45);
      expect(q.direcaoGraus, -90);
      expect(q.gravidade, 120);
      expect(q.tamanho, 12);
      // A VIDA E GUARDADA EM MILISSEGUNDOS no arquivo antigo.
      expect(q.vidaS, 3.0);
      expect(q.largura, 800);
      expect(q.altura, 600);
      expect(q.cintilar, true);
      expect(q.corInicio, 0x35C4E7FF);
      expect(q.forma, FormaDaParticula.risco);
      expect(q.emissor, EmissorDeParticulas.caixa);
      expect(q.modoDeEmissao, ModoDeEmissao.esfera);
      expect(q.ventoX, 20);
      expect(q.ventoY, -5);
      expect(q.arrasto, 1.5);
      expect(q.turbulencia, 30);
      expect(q.turbulenciaEscala, 250);
      expect(q.turbulenciaVelocidade, 2);
      expect(q.tamanhoNaVida, TamanhoNaVida.encolhe);
      expect(q.tamanhoVariacao, 0.7);
      expect(q.opacidadeNaVida, OpacidadeNaVida.some);
      expect(q.opacidadeVariacao, 0.3);
      expect(q.giroGrausS, 90);
      expect(q.rastro, 0.4);
      expect(q.vidaVariacao, 0.2);
      expect(q.brilho, 0.6);
      expect(q.faiscas, 4);
      expect(q.faiscaVidaS, 0.9);
      expect(q.faiscaHeranca, 0.5);
      expect(q.faiscaVelocidade, 130);
      expect(q.faiscaTamanho, 0.6);
      expect(q.faiscaInicio, 0.3);
      // SEM TAXA: o projeto antigo era pre-roll e ficou pre-roll.
      expect(q.taxaDeNascimento, 0);
    });

    test('a nuvem de um projeto antigo ainda DESENHA', () {
      // A traducao nao basta: os numeros traduzidos tem de produzir um
      // lote vivo. Um `maximo` zerado ou uma vida em segundos lida como
      // milissegundos dariam uma nuvem vazia, e o projeto pareceria
      // "sem particulas" — o pior resultado possivel para quem so abriu.
      final q = parametrosDeParticulasDoProjeto({
        'count': 90,
        'life': 4000,
        'speed': 40,
        'emitW': 980,
        'emitH': 980,
        'size': 26,
      });
      final lote = LoteDeParticulas(
        q
          ..centroX = 0
          ..centroY = 0,
      )..gerar(1.0);
      expect(lote.quantas, greaterThan(45));
      final alvo = Uint8List(256 * 256 * 4);
      expect(lote.pintar(alvo, 256, 256), greaterThan(0));
      lote.liberar();
    });

    test('um projeto com particulas faz a ida e volta no arquivo', () {
      final original = _projeto([
        _camada(
          receita: ParametrosDeParticulas(
            maximo: 150,
            semente: 99,
            taxaDeNascimento: 30,
            atracao: -2,
            forma: FormaDaParticula.quadrado,
            corInicio: 0xFF8800FF,
            temCorFim: true,
            corFim: 0x00FF00FF,
          ),
        ),
      ]);
      final voltou = projectFromJson(
        jsonDecode(jsonEncode(projectToJson(original))) as Map<String, dynamic>,
      );
      final l = voltou.layers.single as ParticulasLayer;
      final q = l.parametros;
      expect(q.maximo, 150);
      expect(q.semente, 99);
      expect(q.taxaDeNascimento, 30);
      expect(q.atracao, -2);
      expect(q.forma, FormaDaParticula.quadrado);
      expect(q.corInicio, 0xFF8800FF);
      expect(q.temCorFim, isTrue);
      expect(q.corFim, 0x00FF00FF);
    });
  });

  group('a costura com a previa', () {
    testWidgets('a camada de particulas desenha pelo motor', (tester) async {
      final c = await openEditor(tester);
      final camada = _camada();
      c.read(editorControllerProvider.notifier).openProject(_projeto([camada]));
      await tester.pumpAndSettle();

      // O PINTOR DA NUVEM ESTA MONTADO, e o lote dele tem particulas —
      // o widget existir sem desenhar nada seria o defeito silencioso.
      final pintores = find
          .byWidgetPredicate(
            (w) => w is CustomPaint && w.painter is ParticulasPainter,
          )
          .evaluate();
      expect(pintores, hasLength(1));
      final pintor =
          (pintores.single.widget as CustomPaint).painter! as ParticulasPainter;
      expect(pintor.lote, isNotNull);
      expect(pintor.lote!.quantas, greaterThan(0));
    });

    testWidgets('o nivel de qualidade limita o lote do palco', (tester) async {
      final c = await openEditor(tester);
      final camada = _camada(
        receita: ParametrosDeParticulas(maximo: 6000, vidaS: 4),
      );
      c.read(editorControllerProvider.notifier).openProject(_projeto([camada]));
      await tester.pumpAndSettle();

      int quantas() {
        final p =
            find
                    .byWidgetPredicate(
                      (w) => w is CustomPaint && w.painter is ParticulasPainter,
                    )
                    .evaluate()
                    .single
                    .widget
                as CustomPaint;
        return (p.painter! as ParticulasPainter).lote!.capacidade;
      }

      // NIVEL CHEIO: o teto e 1536, e nao os 6000 pedidos.
      c.read(previewResolutionProvider.notifier).state = PreviewResolution.full;
      await tester.pumpAndSettle();
      final cheio = quantas();

      // A PREVIA EM 1/4 E O BOTAO QUE A PESSOA GIRA quando o aparelho
      // esta sofrendo — a nuvem entra na mesma conversa.
      c.read(previewResolutionProvider.notifier).state =
          PreviewResolution.quarter;
      await tester.pumpAndSettle();
      final reduzido = quantas();

      expect(reduzido, lessThan(cheio));
      expect(reduzido, greaterThan(0));
    });

    testWidgets('a camada de particulas nao desenha em cima do gizmo 3D', (
      tester,
    ) async {
      // A NUVEM NAO E UM ESPACO PROPRIO: ela nasce no centro da camada e
      // obedece ao mesmo transform de todo mundo. Ligar o 3D na camada
      // tem de ligar o gizmo como em qualquer outra.
      final c = await openEditor(tester);
      final camada = _camada();
      c
          .read(editorControllerProvider.notifier)
          .openProject(_projeto([camada.copyLayer(is3D: true)]));
      c.read(selectedLayerProvider.notifier).state = camada.id;
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('gizmo-3d')), findsOneWidget);
    });
  });
}
