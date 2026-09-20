import 'package:aurea/src/core/atualizacao/atualizacao_service.dart';
import 'package:aurea/src/core/avisos/avisos_service.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// COM O APP FORA DA TELA, NINGUEM PERGUNTA NADA AO SERVIDOR.
///
/// Os dois relogios globais (avisos de dez em dez minutos, versao nova de
/// duas em duas horas) nasciam com a Inicio — que nunca sai da arvore — e
/// ficavam para sempre. Com o app em segundo plano eles seguiam batendo
/// no servidor, gastando radio e bateria para atualizar o que ninguem
/// esta vendo.
///
/// Estes testes prendem o par: para em `paused`, volta em `resumed`, e ao
/// voltar busca uma vez SO se a janela ja passou.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  void aoFundo() => WidgetsBinding.instance.handleAppLifecycleStateChanged(
    AppLifecycleState.paused,
  );
  void aoVoltar() => WidgetsBinding.instance.handleAppLifecycleStateChanged(
    AppLifecycleState.resumed,
  );

  setUp(() => SharedPreferences.setMockInitialValues({}));

  group('avisos', () {
    late AvisosService s;
    var relogio = DateTime(2026, 9, 20, 12);

    setUp(() {
      relogio = DateTime(2026, 9, 20, 12);
      s = AvisosService()..agora = () => relogio;
    });

    tearDown(() => s.parar());

    test('o relogio para no segundo plano e volta na frente', () async {
      await s.iniciar();
      expect(s.buscando, isTrue);
      aoFundo();
      expect(s.buscando, isFalse);
      aoVoltar();
      expect(s.buscando, isTrue);
    });

    test('parar tira o observador: o ciclo de vida nao o acorda', () async {
      await s.iniciar();
      s.parar();
      expect(s.buscando, isFalse);
      aoVoltar();
      expect(s.buscando, isFalse);
    });

    test('voltar cedo nao repete a busca; depois da janela, sim', () async {
      await s.iniciar();
      aoFundo();
      relogio = relogio.add(const Duration(minutes: 1));
      aoVoltar();
      // A ultima busca foi ha um minuto: a janela e de dez.
      final logo = s.ultimaBuscaParaTeste;
      aoFundo();
      relogio = relogio.add(const Duration(minutes: 30));
      aoVoltar();
      expect(s.ultimaBuscaParaTeste, isNot(logo));
    });
  });

  group('atualizacao', () {
    late AtualizacaoService s;

    setUp(() => s = AtualizacaoService(soAndroid: true));
    tearDown(() => s.parar());

    test('o relogio de duas horas para no segundo plano', () async {
      await s.iniciar();
      expect(s.verificando, isTrue);
      aoFundo();
      expect(s.verificando, isFalse);
      aoVoltar();
      expect(s.verificando, isTrue);
    });

    test('fora do Android nem comeca', () async {
      final fora = AtualizacaoService(soAndroid: false);
      await fora.iniciar();
      expect(fora.verificando, isFalse);
      fora.parar();
    });
  });
}
