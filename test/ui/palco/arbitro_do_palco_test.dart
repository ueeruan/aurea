// O ARBITRO DO PALCO SOZINHO: quem vira dono de cada gesto, sem widget.
//
// E aqui que as queixas do dono viram regra: "tento pincar e o objeto
// move" (o segundo dedo sempre vira pinca, e a pinca nunca arrasta) e
// "tento selecionar e ele anda" (abaixo da folga e toque, e toque nunca
// arrasta).

import 'package:aurea/src/features/editor/presentation/ui/palco/gestos_do_palco.dart';
import 'package:flutter_test/flutter_test.dart';

class _Delegado implements DelegadoDoPalco {
  _Delegado({this.alvo = AlvoNoPalco.vazio, this.naSelecao = false});

  AlvoNoPalco alvo;
  bool naSelecao;
  bool aceitaArrasto = true;
  final chamadas = <String>[];
  final passos = <Offset>[];
  final pincas = <({double escala, double giro, Offset passo})>[];

  @override
  AlvoNoPalco alvoEm(Offset noPalco) => alvo;

  @override
  bool pincaNaSelecao(Offset a, Offset b) => naSelecao;

  @override
  void tocou(AlvoNoPalco alvo, Offset noPalco) => chamadas.add('tocou');

  @override
  void tocouDuasVezesNoVazio(Offset noPalco) => chamadas.add('duplo');

  @override
  bool comecarArrasto(AlvoNoPalco alvo, Offset inicio) {
    chamadas.add('arrasto:${alvo.tipo.name}');
    return aceitaArrasto;
  }

  @override
  void arrastar(Offset atual, Offset passo) {
    chamadas.add('arrastar');
    passos.add(passo);
  }

  @override
  void comecarPinca({required bool daCamada, required Offset focal}) =>
      chamadas.add(daCamada ? 'pinca:camada' : 'pinca:vista');

  @override
  void pincar({
    required double escala,
    required double giro,
    required Offset focal,
    required Offset passo,
  }) {
    chamadas.add('pincar');
    pincas.add((escala: escala, giro: giro, passo: passo));
  }

  @override
  void terminar() => chamadas.add('terminar');
}

const _camada = AlvoNoPalco(TipoDeAlvo.camada, arrasta: 'a', toca: 'a');

void main() {
  test('tocar sem andar e toque: escolhe e nao arrasta', () {
    final d = _Delegado(alvo: _camada);
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..andou(1, const Offset(106, 103)) // o tremor do dedo
      ..subiu(1);
    expect(d.chamadas, ['tocou']);
    a.descartar();
  });

  test('passou da folga: UM arrasto, com o passo inteiro desde o toque', () {
    final d = _Delegado(alvo: _camada);
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..andou(1, const Offset(125, 100))
      ..andou(1, const Offset(130, 100))
      ..subiu(1);
    expect(d.chamadas, ['arrasto:camada', 'arrastar', 'arrastar', 'terminar']);
    // O primeiro passo leva a folga: o objeto fica debaixo do dedo.
    expect(d.passos.first, const Offset(25, 0));
    expect(d.passos.last, const Offset(5, 0));
    a.descartar();
  });

  test('numa alca a folga e so a do tremor', () {
    final d = _Delegado(
      alvo: const AlvoNoPalco(TipoDeAlvo.alcaDeEscala, arrasta: 'a'),
    );
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..andou(1, const Offset(106, 100));
    expect(d.chamadas, ['arrasto:alcaDeEscala', 'arrastar']);
    a.subiu(1);
    // Tocar numa alca (sem andar) nao escolhe nem tira nada.
    d.chamadas.clear();
    a
      ..desceu(2, const Offset(100, 100))
      ..subiu(2);
    expect(d.chamadas, isEmpty);
    a.descartar();
  });

  test('o segundo dedo antes da folga vira pinca — e nada arrastou', () {
    final d = _Delegado(alvo: _camada, naSelecao: true);
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..andou(1, const Offset(108, 100)) // o primeiro dedo anda um pouco
      ..desceu(2, const Offset(200, 100))
      ..andou(2, const Offset(300, 100));
    expect(d.chamadas.where((c) => c.startsWith('arrast')), isEmpty);
    expect(d.chamadas.first, 'pinca:camada');
    expect(d.pincas.last.escala, closeTo(192 / 92, 1e-9));
    a
      ..subiu(1)
      ..subiu(2);
    expect(d.chamadas.last, 'terminar');
    a.descartar();
  });

  test('sobra um dedo da pinca da camada: ele nao move nada', () {
    final d = _Delegado(alvo: _camada, naSelecao: true);
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..desceu(2, const Offset(200, 100))
      ..andou(2, const Offset(250, 100))
      ..subiu(2);
    d.chamadas.clear();
    a
      ..andou(1, const Offset(180, 160))
      ..andou(1, const Offset(260, 220))
      ..subiu(1);
    expect(d.chamadas, isEmpty, reason: 'nem arrasto, nem toque, nem escolha');
    expect(a.dono, DonoDoGesto.nenhum);
    a.descartar();
  });

  test('sobra um dedo da pinca da vista: ele continua passeando', () {
    final d = _Delegado();
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..desceu(2, const Offset(200, 100))
      ..subiu(2);
    expect(d.chamadas, ['pinca:vista', 'terminar', 'arrasto:vazio']);
    a.andou(1, const Offset(110, 100));
    expect(d.chamadas.last, 'arrastar');
    a.descartar();
  });

  test('arrasto de um dedo e depois o segundo: fecha o arrasto e pinca', () {
    final d = _Delegado(alvo: _camada, naSelecao: true);
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..andou(1, const Offset(140, 100))
      ..desceu(2, const Offset(240, 100));
    expect(d.chamadas, [
      'arrasto:camada',
      'arrastar',
      'terminar',
      'pinca:camada',
    ]);
    a.descartar();
  });

  test('o giro da pinca da camada tem zona morta; a escala nao', () {
    final d = _Delegado(naSelecao: true);
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(0, 0))
      ..desceu(2, const Offset(100, 0))
      // 2 graus de giro: tremor.
      ..andou(2, Offset.fromDirection(2 * 3.141592653589793 / 180, 100));
    expect(d.pincas.last.giro, 0);
    a.andou(2, Offset.fromDirection(30 * 3.141592653589793 / 180, 100));
    expect(d.pincas.last.giro, greaterThan(0.4));
    a.descartar();
  });

  test('dois toques no vazio reenquadram; um so escolhe (tira)', () {
    final d = _Delegado();
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(10, 10))
      ..subiu(1)
      ..desceu(2, const Offset(14, 12))
      ..subiu(2);
    expect(d.chamadas, ['tocou', 'duplo']);
    a.descartar();
  });

  test('arrasto recusado (cadeado) fica sem dono ate soltar', () {
    final d = _Delegado(alvo: _camada)..aceitaArrasto = false;
    final a = ArbitroDoPalco(d)
      ..desceu(1, const Offset(100, 100))
      ..andou(1, const Offset(150, 100))
      ..andou(1, const Offset(180, 100))
      ..subiu(1);
    expect(d.chamadas, ['arrasto:camada']);
    a.descartar();
  });
}
