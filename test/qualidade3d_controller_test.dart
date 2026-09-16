import 'package:aurea/src/features/editor/application/qualidade3d_controller.dart';
import 'package:aurea/src/features/editor/application/sistema_nativo.dart';
import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// O CONTROLADOR DESCE A ESCADA ANTES DE O APP CAIR — e sobe devagar.
///
/// Quatro sinais entram (orcamento, tempo de quadro, memoria, termico) e
/// o nivel e o menor que qualquer um pede. O que estes testes fixam e a
/// ORDEM e a HISTERESE: doze quadros lentos descem um degrau, cento e
/// vinte folgados sobem um, e nunca antes de cinco segundos; o aviso de
/// memoria do sistema vai direto para emergencia e passa sozinho.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const mb = 1024 * 1024;
  const gb = 1024 * mb;

  const leve = PerfilDaCena(
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
  );

  const extrema = PerfilDaCena(
    triangulos: 1000000,
    vertices: 3000000,
    chamadas: 40,
    texturas: 6,
    temPanoramaImagem: false,
    spotsComSombra: 6,
    direcionalComSombra: true,
    animada: false,
    emissiva: true,
    dofPedido: false,
  );

  late ControladorDeQualidade3D c;
  late DateTime relogio;

  setUp(() async {
    SharedPreferences.setMockInitialValues({});
    relogio = DateTime(2026, 9, 7, 12);
    ControladorDeQualidade3D.agora = () => relogio;
    c = ControladorDeQualidade3D.instancia;
    c.zerar();
    await c.carregar(await SharedPreferences.getInstance());
    c.informarRam(4 * gb); // o iPhone 13
  });

  tearDown(() {
    c.zerar();
    ControladorDeQualidade3D.agora = DateTime.now;
  });

  group('o orcamento decide antes do primeiro quadro', () {
    test('cena comum em 1080p no iPhone 13: o topo', () {
      c.registrarCena(leve, 1080, 1920);
      expect(c.nivel.value.index, lessThanOrEqualTo(Qualidade3D.alta.index));
      expect(c.pressao.value, isNot(NivelDePressao.emergencia));
      expect(c.estimativa.value.total, greaterThan(0));
    });

    test('a cena extrema comeca mais baixo — nunca no nivel que a mataria', () {
      c.registrarCena(leve, 1080, 1920);
      final comum = c.nivel.value;
      c.registrarCena(extrema, 1080, 1920);
      expect(c.nivel.value.index, greaterThan(comum.index));
      expect(c.motivo.value, contains('orcamento'));
    });

    test('o teto dos Ajustes vale acima do orcamento', () async {
      await c.definirTeto(TetoDeQualidade3D.leve);
      c.registrarCena(leve, 1080, 1920);
      expect(c.nivel.value, Qualidade3D.baixa);
      final prefs = await SharedPreferences.getInstance();
      expect(prefs.getString('qualidade3d_teto'), 'leve');
    });
  });

  group('o tempo de quadro', () {
    test('doze quadros acima de 33 ms descem um degrau; ate dois', () {
      c.registrarCena(leve, 1080, 1920);
      final inicial = c.nivel.value;
      Qualidade3D abaixo(int n) => Qualidade3D.values[inicial.index + n];
      for (var i = 0; i < 12; i++) {
        c.amostraDeQuadro(60);
      }
      expect(c.nivel.value, abaixo(1));
      expect(c.motivo.value, contains('33 ms'));
      for (var i = 0; i < 12; i++) {
        c.amostraDeQuadro(60);
      }
      expect(c.nivel.value, abaixo(2));
      for (var i = 0; i < 40; i++) {
        c.amostraDeQuadro(60);
      }
      expect(c.nivel.value, abaixo(2), reason: 'o tempo so desce dois degraus');
    });

    test('um quadro lento isolado nao muda nada', () {
      c.registrarCena(leve, 1080, 1920);
      final inicial = c.nivel.value;
      c.amostraDeQuadro(200);
      for (var i = 0; i < 20; i++) {
        c.amostraDeQuadro(8);
      }
      expect(c.nivel.value, inicial);
    });

    test('sobe de volta so depois de 120 folgados E cinco segundos', () {
      c.registrarCena(leve, 1080, 1920);
      final inicial = c.nivel.value;
      final umAbaixo = Qualidade3D.values[inicial.index + 1];
      for (var i = 0; i < 12; i++) {
        c.amostraDeQuadro(60);
      }
      expect(c.nivel.value, umAbaixo);
      for (var i = 0; i < 200; i++) {
        c.amostraDeQuadro(6);
      }
      expect(c.nivel.value, umAbaixo, reason: 'cedo demais: nao passaram cinco segundos');
      relogio = relogio.add(const Duration(seconds: 6));
      for (var i = 0; i < 120; i++) {
        c.amostraDeQuadro(6);
      }
      expect(c.nivel.value, inicial);
    });
  });

  group('a memoria e o termico', () {
    test('o aviso de memoria do sistema e emergencia na hora, e passa', () {
      c.registrarCena(leve, 1080, 1920);
      final inicial = c.nivel.value;
      c.pressaoDeMemoria();
      expect(c.nivel.value, Qualidade3D.emergencia);
      expect(c.pressao.value, NivelDePressao.emergencia);
      expect(c.emEmergencia, isTrue);
      // Dez segundos depois a memoria ainda pode estar apertada: subir agora
      // pediria alvos e sombras novos antes de a antiga ser devolvida.
      relogio = relogio.add(const Duration(seconds: 10));
      c.atualizarSistema(termico: 0);
      expect(c.nivel.value, Qualidade3D.emergencia);
      relogio = relogio.add(const Duration(seconds: 21));
      c.atualizarSistema(termico: 0);
      expect(c.emEmergencia, isFalse);
      // Um degrau a menos que antes, por precaucao.
      expect(c.nivel.value, Qualidade3D.values[inicial.index + 1]);
    });

    test('pouca memoria disponivel poe um teto; muito pouca e emergencia', () {
      c.registrarCena(leve, 1080, 1920);
      final inicial = c.nivel.value;
      c.atualizarSistema(
        memoria: const MemoriaDoSistema(total: 4 * gb, disponivel: 200 * mb, baixa: false),
      );
      expect(c.nivel.value, Qualidade3D.media);
      expect(c.pressao.value.index, greaterThanOrEqualTo(NivelDePressao.alerta.index));
      c.atualizarSistema(
        memoria: const MemoriaDoSistema(total: 4 * gb, disponivel: 60 * mb, baixa: true),
      );
      expect(c.nivel.value, Qualidade3D.emergencia);
      relogio = relogio.add(const Duration(seconds: 31));
      c.atualizarSistema(
        memoria: const MemoriaDoSistema(total: 4 * gb, disponivel: 900 * mb, baixa: false),
      );
      expect(c.nivel.value, inicial, reason: 'com folga de volta, o teto de memoria some');
    });

    test('aparelho esquentando nao ganha alta', () {
      c.registrarCena(leve, 1080, 1920);
      final inicial = c.nivel.value;
      c.atualizarSistema(termico: 2);
      expect(c.nivel.value, Qualidade3D.media);
      c.atualizarSistema(termico: 3);
      expect(c.nivel.value, Qualidade3D.baixa);
      c.atualizarSistema(termico: 0);
      expect(c.nivel.value, inicial);
    });

    test('a RAM do sistema refaz o orcamento', () {
      c.registrarCena(extrema, 1080, 1920);
      final antes = c.nivel.value;
      c.atualizarSistema(
        memoria: const MemoriaDoSistema(total: 8 * gb, disponivel: 3 * gb, baixa: false),
      );
      expect(c.orcamentoBytes, orcamentoGpuBytes(8 * gb));
      expect(c.nivel.value.index, lessThan(antes.index), reason: 'mais RAM, nivel mais alto');
    });
  });

  group('a exportacao', () {
    test('nunca melhor que o preview, nunca alem da memoria', () {
      c.registrarCena(leve, 1080, 1920);
      final hd = c.paraExportacao(1080, 1920);
      expect(hd.nivel.index, greaterThanOrEqualTo(c.nivel.value.index));
      expect(hd.escala, 1.0);
      final quatroK = c.paraExportacao(3840, 2160);
      expect(quatroK.nivel.index, greaterThanOrEqualTo(Qualidade3D.media.index));
      expect(quatroK.escala, 1.0);
    });

    test('o degrau por tempo de quadro nao entra na exportacao', () {
      c.registrarCena(leve, 1080, 1920);
      final inicial = c.nivel.value;
      final semLag = c.paraExportacao(1080, 1920).nivel;
      for (var i = 0; i < 12; i++) {
        c.amostraDeQuadro(60);
      }
      expect(c.nivel.value, Qualidade3D.values[inicial.index + 1]);
      expect(c.paraExportacao(1080, 1920).nivel, semLag,
          reason: 'lag no preview nao e motivo para exportar pior');
    });
  });

  test('o historico registra cada mudanca, com o motivo', () {
    c.registrarCena(leve, 1080, 1920);
    c.historico.clear();
    for (var i = 0; i < 12; i++) {
      c.amostraDeQuadro(60);
    }
    expect(c.historico, hasLength(1));
    expect(c.historico.single, contains('->'));
    expect(c.historico.single, contains('33 ms'));
    expect(c.resumo(), contains('nivel ${qualidade3dRotulo(c.nivel.value)}'));
  });
}
