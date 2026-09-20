// A LARGADA COM MIDIA — O "LAG DO AUDIO" NO ANDROID.
//
// O RELATO: com musica na linha do tempo, o play engasga no Android (no
// iPhone nao). A correcao anterior atacou a onda sendo reconstruida a cada
// quadro, e o relato continuou.
//
// O QUE A MEDIDA NO EMULADOR MOSTROU (bancada
// `integration_test/lag_do_audio_test.dart`, com a trilha crua da
// ancoragem impressa amostra a amostra):
//
//   ANTES  relogio=1866ms  midia=1396ms  erro=-411ms
//          e o erro ficou entre -309 e -546 ms os 8 segundos inteiros.
//
//   DEPOIS relogio=266ms   midia=223ms   erro=0ms
//          e o erro ficou entre -69 e +40 ms.
//
// A causa nao era a onda nem o tocador: era a LARGADA. O relogio da
// composicao comeca a contar no TOQUE; no Android o som comeca a andar
// centenas de ms depois (busca no decodificador + buffer). A composicao
// nascia ~400 ms a frente da midia — imagem adiantada, som atrasado — e a
// correcao fracionada (8 ms por amostra, dez amostras por segundo) levaria
// dezenas de segundos para fechar essa conta.
//
// A CORRECAO: com midia na cena, o relogio SEGURA no instante do toque ate
// a primeira amostra real de posicao chegar, e entao se alinha a ela de
// uma vez. A deriva pequena continua sendo absorvida em fracao, como
// antes — o que mudou e so a PRIMEIRA amostra, que e alinhamento e nao
// deriva.
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late PlaybackController pc;

  /// Um controlador que sabe que HA midia na cena: e o que arma a espera.
  PlaybackController comMidia() => PlaybackController(
    vsync: TestVSync(),
    durationOf: () => const Duration(seconds: 30),
    temMidiaAtiva: () => true,
  );

  setUp(() => pc = comMidia());
  tearDown(() => pc.dispose());

  group('a largada espera a midia', () {
    test('sem midia na cena o relogio nao espera nada', () {
      final semMidia = PlaybackController(
        vsync: TestVSync(),
        durationOf: () => const Duration(seconds: 30),
      );
      addTearDown(semMidia.dispose);
      semMidia.seek(const Duration(seconds: 1));
      semMidia.play();
      // Sem tocador nao ha o que esperar: uma animacao de formas toca
      // igual, e esperar seria um atraso inventado por nos. A mesma
      // amostra que alinha quando ha midia aqui e absorvida em fracao —
      // e a prova de que a espera e o que muda o comportamento.
      semMidia.anchorToMedia(const Duration(seconds: 1));
      semMidia.anchorToMedia(const Duration(milliseconds: 900));
      expect(semMidia.debugBaseShiftUs.abs(), lessThanOrEqualTo(20000));
      expect(semMidia.debugBaseShiftUs, lessThan(0));
    });

    test('a PRIMEIRA amostra alinha de uma vez, mesmo sendo 400 ms', () {
      // O cabecote JA ESTAVA em 900 ms quando o play foi apertado.
      pc.seek(const Duration(milliseconds: 900));
      pc.play();
      // O som do Android comeca a andar ~400 ms depois do toque: quando a
      // primeira amostra real chega, ele ainda esta em 500 ms.
      pc.anchorToMedia(const Duration(milliseconds: 500));
      expect(
        pc.debugBaseShiftUs,
        -400000,
        reason:
            'o relogio vai para a midia de uma vez, e nao 8 ms por '
            'amostra (que era o defeito)',
      );
    });

    test('pela regra antiga isto teria sido absorvido em fracao', () {
      // A MESMA conta, depois da primeira amostra: agora e deriva, e a
      // deriva continua fracionada — o ganho da correcao nova nao pode
      // ter afrouxado o que ja estava certo.
      pc.seek(const Duration(seconds: 3));
      pc.play();
      pc.anchorToMedia(const Duration(seconds: 3));
      expect(pc.debugBaseShiftUs, 0, reason: 'a primeira amostra alinhou');
      pc.anchorToMedia(
        const Duration(seconds: 3) - const Duration(milliseconds: 100),
      );
      expect(pc.debugBaseShiftUs.abs(), lessThanOrEqualTo(20000));
      expect(pc.debugBaseShiftUs, lessThan(0));
    });

    test('pausar e tocar de novo arma a espera outra vez', () {
      pc.play();
      pc.pause();
      pc.seek(const Duration(seconds: 2));
      pc.play();
      pc.anchorToMedia(const Duration(seconds: 1));
      expect(pc.debugBaseShiftUs, -1000000);
    });

    test('um seek no meio do play nao segura o relogio', () {
      pc.play();
      pc.anchorToMedia(Duration.zero);
      pc.seek(const Duration(seconds: 4));
      expect(
        pc.debugAguardandoMidia,
        isFalse,
        reason: 'quem reposiciona e o dedo: a previa continua andando',
      );
      pc.anchorToMedia(const Duration(seconds: 3, milliseconds: 900));
      expect(
        pc.debugBaseShiftUs.abs(),
        lessThanOrEqualTo(20000),
        reason: 'e o que chega depois do seek e deriva, nao largada',
      );
    });

    test('o alinhamento para tras libera o relogio para voltar', () {
      pc.seek(const Duration(seconds: 5));
      pc.play();
      pc.anchorToMedia(const Duration(seconds: 4));
      // Sem esta liberacao o relogio ficaria preso no instante antigo: a
      // regra de monotonia existe para o jitter, e nao para um
      // realinhamento pedido.
      expect(pc.debugPodeVoltar, isTrue);
    });

    testWidgets('a espera da midia e curta e nao vira atraso no play', (
      tester,
    ) async {
      final c = comMidia();
      addTearDown(c.dispose);
      c.play();
      for (var i = 0; i < 2; i++) {
        await tester.pump(const Duration(milliseconds: 30));
      }
      expect(
        c.time.value,
        Duration.zero,
        reason: 'a janela curta ainda permite a primeira ancora da midia',
      );
      await tester.pump(const Duration(milliseconds: 60));
      expect(
        c.time.value,
        greaterThan(Duration.zero),
        reason: 'sem amostra, a previa parte em ate 80 ms e nao parece travada',
      );
      expect(c.debugAguardandoMidia, isFalse);
      c.pause();
    });

    testWidgets('tocador que nunca aparece nao congela a previa', (
      tester,
    ) async {
      final c = comMidia();
      addTearDown(c.dispose);
      c.play();
      // Um tocador quebrado (arquivo ilegivel) nao pode deixar a previa
      // parada para sempre: passado o teto, o relogio anda.
      for (var i = 0; i < 40; i++) {
        await tester.pump(const Duration(milliseconds: 30));
      }
      expect(c.time.value, greaterThan(Duration.zero));
      expect(c.debugAguardandoMidia, isFalse);
      c.pause();
    });

    test('so a PRIMEIRA alinha; a segunda volta a ser deriva', () {
      pc.seek(const Duration(seconds: 2));
      pc.play();
      pc.anchorToMedia(const Duration(seconds: 1, milliseconds: 500));
      expect(pc.debugBaseShiftUs, -500000, reason: 'a primeira alinhou');
      pc.anchorToMedia(const Duration(seconds: 1, milliseconds: 400));
      expect(
        pc.debugBaseShiftUs.abs(),
        lessThanOrEqualTo(20000),
        reason: 'a segunda e deriva de 100 ms: fracionada',
      );
    });
  });
}
