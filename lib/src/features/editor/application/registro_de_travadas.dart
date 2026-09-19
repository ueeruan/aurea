import 'dart:collection';

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import '../../../core/utils/versao_do_app.dart';
import 'motor3d_modo.dart';

/// O QUE TRAVOU O APARELHO, ANOTADO PELO PROPRIO APARELHO.
///
/// A primeira versao disto tinha um defeito que custou duas entregas
/// inteiras, e vale escrever por extenso para nao se repetir.
///
/// As marcas eram guardadas numa PILHA, empilhada e desempilhada dentro
/// de `marcando`. Quem lia a pilha era o retorno de
/// `addTimingsCallback` — e esse retorno NAO acontece durante o quadro.
/// O proprio Flutter avisa: os tempos sao juntados em lote e entregues
/// "dentro de mais ou menos um segundo", justamente para o custo de
/// medir nao pesar na versao entregue. Quando o retorno chegava, o
/// `finally` de toda marca ja havia rodado e a pilha estava vazia.
///
/// O registro entao dizia `nada marcado` em TODA travada, sempre, por
/// construcao — inclusive numa de 1.800 ms cuja causa estava marcada.
/// Eu li isso como "a causa esta fora das marcas" e fui procurar no
/// lugar errado, duas vezes. Nao era um resultado: era o instrumento
/// quebrado.
///
/// AGORA A MEDIDA ACONTECE ONDE O TRABALHO ACONTECE. Cada marca se
/// cronometra a si mesma e soma o que gastou. O quadro e recortado por
/// [LigacaoQueMedeOQuadro], que marca o inicio no primeiro codigo Dart
/// do quadro, e por um retorno persistente, que fecha a conta logo
/// depois de pintar — com a soma da janela ainda na mao. Nada depende
/// mais de um retorno atrasado.
///
/// O custo: um cronometro e duas somas de inteiro por marca, e um mapa
/// pequeno esvaziado por quadro. As marcas sao grossas (uma por no, por
/// quadro, no pior caso), entao isto roda na versao entregue — uma
/// ferramenta de diagnostico que so funciona na bancada nao serve para
/// nada.

/// Uma janela de quadro que passou do limite, com o que foi medido
/// dentro dela.
class Travada {
  const Travada({
    required this.quando,
    required this.totalMs,
    required this.oQue,
    required this.contexto,
  });

  final DateTime quando;

  /// Quanto durou a janela: do fim do quadro anterior ao fim deste.
  final int totalMs;

  /// O que foi medido nesta janela, do mais caro para o mais barato.
  /// `nada marcado` aqui e informacao de verdade: quer dizer que o tempo
  /// foi gasto fora de tudo que esta marcado.
  final String oQue;

  /// O estado que ajuda a entender: versao, motor 3D em GPU ou CPU,
  /// quantos triangulos.
  final String contexto;

  String get linha =>
      '${quando.toIso8601String().substring(11, 19)}  '
      '${totalMs.toString().padLeft(5)} ms  '
      '$oQue  |  $contexto';
}

/// O corte construcao/desenho de um quadro lento. So a API de tempos do
/// Flutter sabe dizer isto, e ela chega atrasada — por isso vive numa
/// lista separada, sem fingir saber a causa.
class QuadroLento {
  const QuadroLento({
    required this.quando,
    required this.totalMs,
    required this.construcaoMs,
    required this.desenhoMs,
  });

  final DateTime quando;
  final int totalMs;

  /// A parte em Dart: construir, posicionar e PINTAR. O pintor de CPU e
  /// a sincronia da cena 3D caem aqui, nao no desenho.
  final int construcaoMs;

  /// A parte na GPU: rasterizar.
  final int desenhoMs;

  String get linha =>
      '${quando.toIso8601String().substring(11, 19)}  '
      '${totalMs.toString().padLeft(5)} ms  '
      '(constroi ${construcaoMs.toString().padLeft(5)}, '
      'desenha ${desenhoMs.toString().padLeft(4)})';
}

abstract final class RegistroDeTravadas {
  /// Acima disto a pessoa PERCEBE. Um quadro de 60 fps tem 16,7 ms; um
  /// de 120 ms e um oitavo de segundo de tela parada.
  static const limiteMs = 120;

  /// Quantos registros guardar. Os mais recentes importam mais.
  static const _capacidade = 60;

  static final Queue<Travada> _travadas = Queue<Travada>();
  static final Queue<QuadroLento> _quadros = Queue<QuadroLento>();

  /// As travadas registradas, da mais recente para a mais antiga.
  static List<Travada> get travadas => _travadas.toList().reversed.toList();

  /// Os quadros lentos, da mais recente para a mais antiga.
  static List<QuadroLento> get quadros => _quadros.toList().reversed.toList();

  static bool _ligado = false;

  /// Relogio unico do processo. Serve para medir janelas sem perguntar a
  /// hora ao sistema a cada quadro.
  static final Stopwatch _relogio = Stopwatch()..start();
  static int _fimDoQuadroAnteriorUs = 0;

  /// Quando o quadro atual comecou. Vem de [LigacaoQueMedeOQuadro], que
  /// avisa no topo de `handleBeginFrame`.
  ///
  /// SEM ISTO A CONTA MEDE O OCIO. O retorno persistente roda no fim do
  /// quadro; medir do fim do quadro anterior ate agora inclui todo o
  /// tempo em que o aplicativo ficou parado esperando o dedo. Um toque
  /// depois de tres segundos de tela quieta seria registrado como uma
  /// travada de tres segundos com `nada marcado` — exatamente o tipo de
  /// registro falso que ja me mandou para o lugar errado uma vez.
  static int? _inicioDoQuadroUs;

  /// O que foi medido NESTA janela de quadro. Esvaziado a cada quadro.
  static final Map<String, int> _janelaUs = <String, int>{};

  /// A soma de sempre, por marca — o que responde "o que mais pesou
  /// desde que o aplicativo abriu".
  static final Map<String, int> _somaUs = <String, int>{};
  static final Map<String, int> _vezes = <String, int>{};
  static final Map<String, int> _piorUs = <String, int>{};

  /// Contexto do aplicativo no momento da travada. Quem sabe responder
  /// isso e outra camada (o motor 3D, o editor), entao ela se
  /// apresenta aqui em vez de este arquivo importar meio mundo.
  static String Function()? contextoAtual;

  /// Comeca a ouvir os quadros. Chamar uma vez, no inicio do app.
  ///
  /// Sao duas escutas, e cada uma responde uma pergunta que a outra nao
  /// responde:
  ///
  ///   - o retorno PERSISTENTE roda dentro do quadro, logo depois de
  ///     pintar, e por isso e ele que sabe O QUE foi feito;
  ///   - o retorno de TEMPOS chega depois, em lote, e e o unico que sabe
  ///     quanto foi construcao e quanto foi desenho.
  static void comecar() {
    if (_ligado) return;
    _ligado = true;
    _fimDoQuadroAnteriorUs = _relogio.elapsedMicroseconds;
    SchedulerBinding.instance.addPersistentFrameCallback(_fimDeQuadro);
    SchedulerBinding.instance.addTimingsCallback(_verQuadros);
  }

  /// Roda [corpo] cronometrado sob [nome]. Se o quadro travar, e esta
  /// soma que diz o quanto [nome] teve de culpa.
  static T marcando<T>(String nome, T Function() corpo) {
    final relogio = Stopwatch()..start();
    try {
      return corpo();
    } finally {
      relogio.stop();
      _somar(nome, relogio.elapsedMicroseconds);
    }
  }

  /// Versao assincrona: cobre uma gravacao em disco, por exemplo. O
  /// tempo aqui inclui a espera, entao ele nao entra na janela do
  /// quadro — so na soma de sempre.
  static Future<T> marcandoAsync<T>(
    String nome,
    Future<T> Function() corpo,
  ) async {
    final relogio = Stopwatch()..start();
    try {
      return await corpo();
    } finally {
      relogio.stop();
      final us = relogio.elapsedMicroseconds;
      _vezes[nome] = (_vezes[nome] ?? 0) + 1;
      _somaUs[nome] = (_somaUs[nome] ?? 0) + us;
      if (us > (_piorUs[nome] ?? 0)) _piorUs[nome] = us;
    }
  }

  static void _somar(String nome, int us) {
    _janelaUs[nome] = (_janelaUs[nome] ?? 0) + us;
    _vezes[nome] = (_vezes[nome] ?? 0) + 1;
    _somaUs[nome] = (_somaUs[nome] ?? 0) + us;
    if (us > (_piorUs[nome] ?? 0)) _piorUs[nome] = us;
  }

  /// Roda depois de construir, posicionar e pintar — ainda dentro do
  /// quadro. E aqui que a atribuicao acontece.
  /// O quadro comecou. Chamado por [LigacaoQueMedeOQuadro].
  static void quadroComecou() {
    _inicioDoQuadroUs = _relogio.elapsedMicroseconds;
  }

  static void _fimDeQuadro(Duration _) {
    final agora = _relogio.elapsedMicroseconds;
    // Com a ligacao instalada, o comeco e o comeco do quadro. Sem ela
    // (num teste, por exemplo), sobra a janela desde o quadro anterior.
    final inicio = _inicioDoQuadroUs ?? _fimDoQuadroAnteriorUs;
    _inicioDoQuadroUs = null;
    final janelaMs = (agora - inicio) ~/ 1000;
    _fimDoQuadroAnteriorUs = agora;
    if (janelaMs >= limiteMs) {
      _registrar(
        Travada(
          quando: DateTime.now(),
          totalMs: janelaMs,
          oQue: _resumoDaJanela(),
          contexto: contextoAtual?.call() ?? '',
        ),
      );
    }
    if (_janelaUs.isNotEmpty) _janelaUs.clear();
  }

  /// As marcas desta janela, da mais cara para a mais barata. Marcas
  /// insignificantes ficam de fora para a linha continuar legivel.
  static String _resumoDaJanela() {
    if (_janelaUs.isEmpty) return 'nada marcado';
    final ordenadas = _janelaUs.entries.toList()
      ..sort((a, b) => b.value.compareTo(a.value));
    final partes = <String>[];
    for (final e in ordenadas) {
      final ms = e.value ~/ 1000;
      if (ms < 4 && partes.isNotEmpty) break;
      partes.add('${e.key} ${ms}ms');
      if (partes.length == 4) break;
    }
    return partes.join(' + ');
  }

  static void _verQuadros(List<FrameTiming> quadros) {
    for (final q in quadros) {
      final total = q.totalSpan.inMilliseconds;
      if (total < limiteMs) continue;
      _quadros.addLast(
        QuadroLento(
          quando: DateTime.now(),
          totalMs: total,
          construcaoMs: q.buildDuration.inMilliseconds,
          desenhoMs: q.rasterDuration.inMilliseconds,
        ),
      );
      while (_quadros.length > _capacidade) {
        _quadros.removeFirst();
      }
    }
  }

  static void _registrar(Travada t) {
    _travadas.addLast(t);
    while (_travadas.length > _capacidade) {
      _travadas.removeFirst();
    }
  }

  /// Quantos triangulos a cena 3D pediu no ultimo quadro desenhado
  /// PELO PINTOR DE CPU. Zero quer dizer que ele nao foi usado.
  static int trianglesNoPintorDeCpu = 0;

  /// O pintor de CPU acabou de desenhar uma cena deste tamanho.
  static void marcarCena(int triangulos) {
    trianglesNoPintorDeCpu = triangulos;
  }

  static void limpar() {
    _inicioDoQuadroUs = null;
    _fimDoQuadroAnteriorUs = _relogio.elapsedMicroseconds;
    _travadas.clear();
    _quadros.clear();
    _janelaUs.clear();
    _somaUs.clear();
    _vezes.clear();
    _piorUs.clear();
    trianglesNoPintorDeCpu = 0;
  }

  /// Tudo em texto, para colar numa mensagem.
  static String emTexto() {
    final b = StringBuffer()
      ..writeln('AUREA $versaoCompleta — TRAVADAS')
      ..writeln(descreverMotor3D())
      ..writeln();
    final causas = porCausa();
    if (causas.isNotEmpty) {
      b
        ..writeln('POR MARCA, desde que o app abriu:')
        ..writeln();
      for (final c in causas.take(10)) {
        b.writeln(
          '${c.somaMs.toString().padLeft(7)} ms  '
          '${c.vezes.toString().padLeft(5)}x  '
          'pior ${c.piorMs.toString().padLeft(5)} ms   ${c.oQue}',
        );
      }
      b.writeln();
    }
    if (_travadas.isEmpty) {
      b.writeln('Nenhum quadro passou de ${limiteMs}ms desde que o app abriu.');
    } else {
      b
        ..writeln('TRAVADAS (${_travadas.length}), da mais recente:')
        ..writeln();
      for (final t in travadas) {
        b.writeln(t.linha);
      }
    }
    if (_quadros.isNotEmpty) {
      b
        ..writeln()
        ..writeln('CONSTROI x DESENHA (${_quadros.length}), da mais recente:')
        ..writeln();
      for (final q in quadros) {
        b.writeln(q.linha);
      }
    }
    return b.toString();
  }

  /// O resumo que interessa: qual marca somou mais tempo, quantas vezes,
  /// e qual foi a pior de uma vez so.
  static List<({String oQue, int vezes, int somaMs, int piorMs})> porCausa() {
    final saida = [
      for (final e in _somaUs.entries)
        (
          oQue: e.key,
          vezes: _vezes[e.key] ?? 0,
          somaMs: e.value ~/ 1000,
          piorMs: (_piorUs[e.key] ?? 0) ~/ 1000,
        ),
    ]..sort((a, b) => b.somaMs.compareTo(a.somaMs));
    return saida;
  }
}

/// QUEM ESTA DESENHANDO A CENA 3D, E POR QUE.
///
/// Esta e a pergunta que faltava responder no aparelho. "O aparelho tem
/// GPU" nao serve: o que importa e se ESTE modelo, nesta sessao, esta
/// indo pelo Flutter GPU ou pelo pintor de reserva — e, quando e pela
/// reserva, qual foi o motivo exato.
///
/// A versao vem junto porque sem ela o registro nao se explica: um
/// `nada marcado` de um build sem marcas e de um build com marcas sao a
/// mesma frase com sentidos opostos.
String descreverMotor3D() {
  final pref = Motor3DPreferencia.instancia;
  final partes = <String>[
    'app=$versaoCompleta',
    // O MOTOR 3D SAIU DA CONTA: o antigo foi apagado e o novo ainda
    // nao existe. O cracha do motor voltara quando houver um motor para
    // descrever.
    'motor=nenhum',
  ];
  if (RegistroDeTravadas.trianglesNoPintorDeCpu > 0) {
    partes.add('PINTOU-EM-CPU=${RegistroDeTravadas.trianglesNoPintorDeCpu}tri');
  }
  if (pref != null) {
    partes
      ..add('modo=${pref.modo.name}')
      ..add('caiuAntes=${pref.caiu}');
  }
  return partes.join(' ');
}


/// A LIGACAO QUE SABE QUANDO O QUADRO COMECOU.
///
/// `handleBeginFrame` e o primeiro codigo Dart de um quadro; dali ate o
/// fim de `handleDrawFrame` esta tudo que o Flutter contabiliza como
/// `constroi` — animacoes, construcao, posicionamento e PINTURA. Medir
/// daqui e medir a travada, e nao o tempo em que o aplicativo estava
/// parado esperando alguem tocar na tela.
class LigacaoQueMedeOQuadro extends WidgetsFlutterBinding {
  @override
  void handleBeginFrame(Duration? rawTimeStamp) {
    RegistroDeTravadas.quadroComecou();
    super.handleBeginFrame(rawTimeStamp);
  }

  /// A MARCA MAIS GROSSA DE TODAS, e a que fecha o cerco.
  ///
  /// `drawFrame` e construir, posicionar e pintar a arvore inteira. O
  /// que sobra do quadro sao os retornos de animacao, que rodam antes.
  /// Entao:
  ///
  ///   - travada com esta marca alta e as de dentro baixas -> o custo
  ///     esta em construcao/posicionamento/pintura de algo que ninguem
  ///     cronometrou;
  ///   - travada sem esta marca -> o custo esta num tique de animacao,
  ///     que e outro caminho inteiro.
  ///
  /// Sem esse corte, `nada marcado` deixa as duas hipoteses abertas.
  @override
  void drawFrame() {
    RegistroDeTravadas.marcando(
      'quadro: construir, posicionar e pintar',
      super.drawFrame,
    );
  }
}
