import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// QUEM DESENHA A CENA 3D — e o que fazer quando isso derruba o app.
///
/// O motor em GPU (Flutter GPU / Impeller) vive fora do Dart: quando ele
/// quebra, nao ha excecao para pegar — o processo morre e o aparelho
/// volta para a tela inicial. Um app nao pode responder a isso tentando
/// de novo para sempre; quem esta com o aparelho na mao so ve o app
/// fechando toda vez que abre a mesma cena.
///
/// A saida e uma MIGALHA em disco. Antes do primeiro quadro em GPU
/// gravamos "estou tentando"; depois do primeiro quadro que sai inteiro,
/// apagamos. Se o app comeca e a migalha ainda esta la, a conclusao e
/// direta: a ultima tentativa nao voltou viva. Entao a proxima sessao
/// desenha em CPU — mais lenta, mas viva — e diz por que.
///
/// A pessoa continua no comando: [Motor3DModo.gpu] e [Motor3DModo.cpu]
/// nos Ajustes ignoram a migalha, nos dois sentidos.
enum Motor3DModo {
  /// Tenta a GPU; desiste sozinho depois de uma queda.
  automatico,

  /// Sempre a GPU, mesmo tendo caido antes.
  gpu,

  /// Sempre o pintor em CPU.
  cpu,
}

String motor3dModoRotulo(Motor3DModo m) => switch (m) {
  Motor3DModo.automatico => 'Automatico',
  Motor3DModo.gpu => 'Sempre GPU',
  Motor3DModo.cpu => 'Sempre CPU',
};

/// A preferencia e a migalha, guardadas juntas.
class Motor3DPreferencia {
  Motor3DPreferencia._(this._prefs);

  static const _kModo = 'motor3d_modo';
  static const _kTentando = 'motor3d_tentando';
  static const _kCaiu = 'motor3d_caiu';

  static Motor3DPreferencia? _instancia;

  /// Le o estado gravado e RESOLVE a migalha: se a sessao anterior
  /// comecou a desenhar em GPU e nunca confirmou um quadro, ela nao
  /// voltou — marca a queda. Chamar uma vez, no inicio do app.
  static Future<Motor3DPreferencia> carregar(SharedPreferences prefs) async {
    final p = Motor3DPreferencia._(prefs);
    if (prefs.getBool(_kTentando) ?? false) {
      await prefs.setBool(_kCaiu, true);
      await prefs.setBool(_kTentando, false);
      debugPrint(
        'Motor 3D: a sessao anterior nao voltou de um quadro em GPU; '
        'esta sessao desenha em CPU.',
      );
    }
    return _instancia = p;
  }

  /// Para os testes e para quem roda sem o main() do app.
  static Motor3DPreferencia? get instancia => _instancia;

  final SharedPreferences _prefs;

  Motor3DModo get modo => Motor3DModo.values.firstWhere(
    (m) => m.name == _prefs.getString(_kModo),
    orElse: () => Motor3DModo.automatico,
  );

  Future<void> definirModo(Motor3DModo m) async {
    await _prefs.setString(_kModo, m.name);
    // Escolher um modo e um recomeco: a queda anterior deixa de valer.
    await _prefs.setBool(_kCaiu, false);
  }

  /// A sessao anterior caiu desenhando em GPU?
  bool get caiu => _prefs.getBool(_kCaiu) ?? false;

  /// A GPU pode ser tentada nesta sessao?
  bool get permiteGpu => switch (modo) {
    Motor3DModo.cpu => false,
    Motor3DModo.gpu => true,
    Motor3DModo.automatico => !caiu,
  };

  /// Por que a GPU nao vai ser tentada, quando nao vai.
  String get motivoDeNaoTentar => switch (modo) {
    Motor3DModo.cpu => 'voce escolheu Sempre CPU nos Ajustes',
    Motor3DModo.gpu => '',
    Motor3DModo.automatico => caiu
        ? 'o app fechou sozinho na ultima vez que desenhou em GPU'
        : '',
  };

  /// Marca que um quadro em GPU vai comecar. Se o app nao voltar daqui,
  /// a proxima sessao encontra esta marca.
  Future<void> marcarTentativa() => _prefs.setBool(_kTentando, true);

  /// O quadro saiu inteiro: a marca pode sair.
  Future<void> marcarSucesso() async {
    await _prefs.setBool(_kTentando, false);
    await _prefs.setBool(_kCaiu, false);
  }
}

/// A MARCA DE QUE UMA CENA EM GPU ESTA NA TELA.
///
/// A migalha da secao acima so tem valor se cobrir a janela inteira em
/// que o motor pode derrubar o app — nao apenas o primeiro quadro. Uma
/// falha de driver costuma vir depois de dezenas de quadros, quando a
/// memoria da GPU aperta.
///
/// Entao a marca vale ENQUANTO existe uma cena em GPU visivel e o app
/// esta em primeiro plano. Sair da cena apaga a marca; o app ir para o
/// segundo plano tambem — assim um encerramento do proprio iOS com o
/// app guardado nao e confundido com uma queda do motor.
class MarcaGpuViva with WidgetsBindingObserver {
  MarcaGpuViva._() {
    WidgetsBinding.instance.addObserver(this);
  }

  static MarcaGpuViva? _eu;
  static int _naTela = 0;
  static bool _emPrimeiroPlano = true;
  static bool _marcada = false;

  /// Uma cena em GPU apareceu.
  static void entrou() {
    _eu ??= MarcaGpuViva._();
    _naTela++;
    _sincronizar();
  }

  /// Uma cena em GPU saiu.
  static void saiu() {
    if (_naTela > 0) _naTela--;
    _sincronizar();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _emPrimeiroPlano = state == AppLifecycleState.resumed;
    _sincronizar();
  }

  static void _sincronizar() {
    final quer = _naTela > 0 && _emPrimeiroPlano;
    if (quer == _marcada) return;
    _marcada = quer;
    final p = Motor3DPreferencia.instancia;
    if (p == null) return;
    // Sem await de proposito: isto acontece dentro de initState/dispose
    // e de callbacks de ciclo de vida.
    unawaited(quer ? p.marcarTentativa() : p.marcarSucesso());
  }

  @visibleForTesting
  static void zerar() {
    _naTela = 0;
    _emPrimeiroPlano = true;
    _marcada = false;
  }
}
