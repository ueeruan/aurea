import 'package:aurea/src/features/editor/application/motor3d_modo.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// A MIGALHA DO MOTOR 3D.
///
/// O motor em GPU vive fora do Dart: quando ele derruba o app nao ha
/// excecao para pegar. A unica prova que sobrevive e uma marca em disco
/// gravada antes de desenhar e apagada quando a cena sai da tela em paz.
/// Estes testes fixam a leitura dessa marca — que e o que decide se o
/// app tenta a GPU de novo ou desenha em CPU e diz por que.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  Future<Motor3DPreferencia> abrir() async =>
      Motor3DPreferencia.carregar(await SharedPreferences.getInstance());

  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('sem historico, tenta a GPU', () async {
    final p = await abrir();
    expect(p.modo, Motor3DModo.automatico);
    expect(p.caiu, isFalse);
    expect(p.permiteGpu, isTrue);
    expect(p.motivoDeNaoTentar, isEmpty);
  });

  test('sessao que nao voltou de um quadro em GPU cai para CPU', () async {
    final antes = await abrir();
    await antes.marcarTentativa();
    // O app morre aqui: nenhum marcarSucesso. A proxima sessao le a marca.
    final depois = await abrir();
    expect(depois.caiu, isTrue);
    expect(depois.permiteGpu, isFalse);
    expect(depois.motivoDeNaoTentar, contains('fechou sozinho'));
  });

  test('cena que saiu da tela em paz nao deixa marca', () async {
    final antes = await abrir();
    await antes.marcarTentativa();
    await antes.marcarSucesso();
    final depois = await abrir();
    expect(depois.caiu, isFalse);
    expect(depois.permiteGpu, isTrue);
  });

  test('a marca so vale uma vez: a sessao seguinte comeca limpa', () async {
    await (await abrir()).marcarTentativa();
    final caida = await abrir();
    expect(caida.permiteGpu, isFalse);
    // Esta sessao desenhou em CPU, entao nao ha nova tentativa para
    // marcar — mas o motivo continua valendo ate a pessoa escolher.
    final seguinte = await abrir();
    expect(seguinte.caiu, isTrue, reason: 'a queda nao se apaga sozinha');
  });

  test('Sempre GPU ignora a queda; Sempre CPU ignora tudo', () async {
    await (await abrir()).marcarTentativa();
    final p = await abrir();
    expect(p.permiteGpu, isFalse);

    await p.definirModo(Motor3DModo.gpu);
    expect(p.permiteGpu, isTrue);
    expect(p.motivoDeNaoTentar, isEmpty);

    await p.definirModo(Motor3DModo.cpu);
    expect(p.permiteGpu, isFalse);
    expect(p.motivoDeNaoTentar, contains('Sempre CPU'));

    // A escolha sobrevive ao fechamento do app.
    expect((await abrir()).modo, Motor3DModo.cpu);
  });

  test('escolher um modo perdoa a queda anterior', () async {
    await (await abrir()).marcarTentativa();
    final p = await abrir();
    expect(p.caiu, isTrue);
    await p.definirModo(Motor3DModo.automatico);
    expect(p.caiu, isFalse);
    expect(p.permiteGpu, isTrue);
  });
}
