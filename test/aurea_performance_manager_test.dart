import 'package:aurea/src/features/editor/application/desempenho/aurea_performance_manager.dart';
import 'package:aurea/src/features/editor/application/interacao.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/qualidade3d_controller.dart';
import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// O GERENTE DE DESEMPENHO: o perfil se guarda, as sondas ligam e
/// DESLIGAM juntas, e a exportacao nao muda de jeito nenhum.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const mb = 1024 * 1024;

  late AureaPerformanceManager g;
  late ControladorDeQualidade3D q3d;

  setUp(() async {
    SharedPreferences.setMockInitialValues({});
    q3d = ControladorDeQualidade3D.instancia;
    q3d.zerar();
    g = AureaPerformanceManager.paraTeste(controlador: q3d);
    await g.carregar(await SharedPreferences.getInstance());
  });

  tearDown(() {
    // Nunca deixar o editor "aberto": sobrariam callbacks e um Timer.
    while (g.editorAberto) {
      g.editorFechou();
    }
    q3d.zerar();
    Interacao.zerar();
    PlaybackController.tocandoAgora.value = false;
  });

  group('perfil', () {
    test('nasce em Automatico', () {
      expect(g.perfil.value, PerfilDeDesempenho.automatico);
    });

    test('a escolha se guarda e volta na proxima abertura', () async {
      await g.definirPerfil(PerfilDeDesempenho.economia);
      final prefs = await SharedPreferences.getInstance();
      expect(
        prefs.getString(AureaPerformanceManager.chaveDoPerfil),
        'economia',
      );

      final outro = AureaPerformanceManager.paraTeste(controlador: q3d);
      await outro.carregar(prefs);
      expect(outro.perfil.value, PerfilDeDesempenho.economia);
      expect(outro.politica.value.escalaDaPrevia, .5);
    });

    test('um valor estranho no disco cai no Automatico', () async {
      SharedPreferences.setMockInitialValues({
        AureaPerformanceManager.chaveDoPerfil: 'turbo',
      });
      final outro = AureaPerformanceManager.paraTeste(controlador: q3d);
      await outro.carregar(await SharedPreferences.getInstance());
      expect(outro.perfil.value, PerfilDeDesempenho.automatico);
    });

    test('trocar de perfil publica politica nova', () async {
      final vistas = <PoliticaDeDesempenho>[];
      g.politica.addListener(() => vistas.add(g.politica.value));
      await g.definirPerfil(PerfilDeDesempenho.economia);
      expect(vistas, isNotEmpty);
      expect(vistas.last.escalaDaPrevia, .5);
      expect(vistas.last.fpsAlvoDaPrevia, 30);
    });
  });

  group('vida do editor', () {
    test('as sondas ligam com o editor e DESLIGAM ao sair', () {
      expect(g.medindoQuadros, isFalse);
      expect(q3d.sondando, isFalse);
      g.editorAbriu();
      expect(g.medindoQuadros, isTrue);
      expect(q3d.sondando, isTrue, reason: 'o gerente segurou a sonda');
      g.editorFechou();
      expect(g.medindoQuadros, isFalse);
      expect(q3d.sondando, isFalse);
    });

    test('duas aberturas exigem dois fechamentos', () {
      g.editorAbriu();
      g.editorAbriu();
      g.editorFechou();
      expect(g.editorAberto, isTrue);
      g.editorFechou();
      expect(g.editorAberto, isFalse);
      expect(g.medindoQuadros, isFalse);
    });

    test('o segundo plano para a medicao e a volta religa', () {
      g.editorAbriu();
      // Pelo caminho de verdade: os DOIS estao registrados no binding, e e
      // ele que avisa. Chamar o metodo na mao esconderia se um deles
      // esqueceu de se registrar.
      WidgetsBinding.instance.handleAppLifecycleStateChanged(
        AppLifecycleState.paused,
      );
      expect(g.medindoQuadros, isFalse);
      expect(q3d.sondando, isFalse, reason: 'nada de canal nativo escondido');
      WidgetsBinding.instance.handleAppLifecycleStateChanged(
        AppLifecycleState.resumed,
      );
      expect(g.medindoQuadros, isTrue);
      expect(q3d.sondando, isTrue);
    });

    test('ir para o segundo plano solta um gesto preso', () {
      Interacao.ligada = true;
      addTearDown(() => Interacao.ligada = false);
      g.editorAbriu();
      Interacao.marcar();
      expect(Interacao.agora.value, isTrue);
      WidgetsBinding.instance.handleAppLifecycleStateChanged(
        AppLifecycleState.paused,
      );
      expect(Interacao.agora.value, isFalse);
    });
  });

  group('sinais do aparelho entram na politica', () {
    test('o aparelho quente desce a previa', () {
      g.editorAbriu();
      q3d.termico = 2;
      g.recalcular();
      expect(g.politica.value.escalaDaPrevia, .5);
      expect(g.politica.value.motivo, contains('quente'));
    });

    test('pouca memoria disponivel vale um degrau', () {
      g.editorAbriu();
      q3d.disponivelBytes = 120 * mb;
      g.recalcular();
      expect(g.politica.value.escalaDaPrevia, lessThan(1));
    });

    test('doze quadros lentos descem um degrau no Automatico', () {
      g.editorAbriu();
      // PRECISA HAVER MOTIVO PARA O QUADRO EXISTIR. Com o editor parado
      // a escada nao mede nada: quadro em repouso ou e defeito de
      // terceiro ou e o quadro que a propria politica encomendou, e
      // reagir a ele fecha o laco (ver test/brilho_sem_laco_test.dart).
      PlaybackController.tocandoAgora.value = true;
      // Os oito primeiros sao a acomodacao do proprio "comecou a tocar",
      // que ja mexeu na politica.
      for (var i = 0; i < 20; i++) {
        g.amostraDeQuadro(60);
      }
      expect(g.politica.value.escalaDaPrevia, .75);
      PlaybackController.tocandoAgora.value = false;
    });

    test('com o editor PARADO, medir quadro nao mexe na politica', () {
      g.editorAbriu();
      final antes = g.politica.value;
      for (var i = 0; i < 400; i++) {
        g.amostraDeQuadro(60);
      }
      expect(g.politica.value, antes);
    });

    test('o teto do perfil chega ao controlador 3D e volta ao sair', () async {
      g.editorAbriu();
      await g.definirPerfil(PerfilDeDesempenho.economia);
      expect(q3d.tetoDaPolitica, Qualidade3D.baixa);
      g.editorFechou();
      // O gerente desfaz DEPOIS do quadro: `editorFechou` roda no dispose
      // do editor, com a arvore trancada.
      await Future<void>.delayed(Duration.zero);
      expect(
        q3d.tetoDaPolitica,
        Qualidade3D.ultra,
        reason: 'fora do editor nao ha previa para limitar',
      );
    });
  });

  group('A EXPORTACAO E IMUNE', () {
    test('nenhum perfil nem sinal muda a politica de exportacao', () async {
      g.editorAbriu();
      q3d.termico = 3;
      q3d.memoriaBaixa = true;
      for (var i = 0; i < 40; i++) {
        g.amostraDeQuadro(90);
      }
      for (final perfil in PerfilDeDesempenho.values) {
        await g.definirPerfil(perfil);
        expect(g.politicaDeExportacao, PoliticaDeDesempenho.exportacao);
        expect(g.politicaDeExportacao.escalaDaPrevia, 1);
        expect(g.politicaDeExportacao.niveisDoBrilho, 5);
        expect(g.politicaDeExportacao.teto3D, Qualidade3D.ultra);
      }
    });

    test('o teto do perfil NAO entra na receita de exportacao do 3D', () async {
      g.editorAbriu();
      await g.definirPerfil(PerfilDeDesempenho.economia);
      expect(q3d.tetoDaPolitica, Qualidade3D.baixa);
      // `paraExportacao` e o caminho do arquivo que sai: nao le o perfil.
      q3d.informarRam(4 * 1024 * mb);
      q3d.registrarCena(
        const PerfilDaCena(
          triangulos: 1200,
          vertices: 3600,
          chamadas: 100,
          texturas: 0,
          temPanoramaImagem: false,
          spotsComSombra: 0,
          direcionalComSombra: true,
          animada: false,
          emissiva: false,
          dofPedido: false,
        ),
        1920,
        1080,
      );
      final r = q3d.paraExportacao(1920, 1080);
      expect(
        r.nivel.index,
        lessThan(Qualidade3D.baixa.index),
        reason: 'a exportacao nao herda o perfil de bateria',
      );
    });
  });
}
