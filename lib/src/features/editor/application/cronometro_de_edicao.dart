import 'package:flutter/foundation.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../core/storage/prefs.dart';

enum EstadoDoCronometro { parado, rodando, pausado }

/// O CRONOMETRO DE EDICAO (menu ⋮ da timeline): quanto tempo a pessoa
/// passou editando ESTE projeto — util para cobrar um trabalho ou so
/// para saber.
///
/// Mora no aparelho, por projeto, e nao dentro do projeto: contar tempo
/// nao e editar, e cada segundo contado viraria um passo de desfazer.
/// Sem preferencias (testes), guarda na memoria.
class CronometroDeEdicao extends ChangeNotifier {
  CronometroDeEdicao(this.projetoId, {this.prefs, this.relogio});

  final String projetoId;
  final SharedPreferences? prefs;

  /// O relogio de parede (trocado nos testes).
  final DateTime Function()? relogio;

  static final Map<String, int> _memoria = {};

  DateTime _agora() => (relogio ?? DateTime.now)();

  String get _chaveTotal => 'edicao.cronometro.$projetoId.total';
  String get _chaveDesde => 'edicao.cronometro.$projetoId.desde';

  int? _ler(String chave) {
    final p = prefs;
    return p == null ? _memoria[chave] : p.getInt(chave);
  }

  void _gravar(String chave, int? valor) {
    final p = prefs;
    if (p == null) {
      if (valor == null) {
        _memoria.remove(chave);
      } else {
        _memoria[chave] = valor;
      }
      return;
    }
    if (valor == null) {
      p.remove(chave);
    } else {
      p.setInt(chave, valor);
    }
  }

  EstadoDoCronometro get estado {
    if (_ler(_chaveDesde) != null) return EstadoDoCronometro.rodando;
    return (_ler(_chaveTotal) ?? 0) > 0
        ? EstadoDoCronometro.pausado
        : EstadoDoCronometro.parado;
  }

  /// O tempo acumulado, contando o trecho que esta rodando agora.
  Duration get total {
    final base = _ler(_chaveTotal) ?? 0;
    final desde = _ler(_chaveDesde);
    final corrente = desde == null
        ? 0
        : (_agora().millisecondsSinceEpoch - desde).clamp(0, 1 << 52);
    return Duration(milliseconds: base + corrente);
  }

  /// Comeca (ou retoma) a contar.
  void iniciar() {
    if (estado == EstadoDoCronometro.rodando) return;
    _gravar(_chaveDesde, _agora().millisecondsSinceEpoch);
    notifyListeners();
  }

  /// Guarda o trecho contado e para.
  void pausar() {
    final desde = _ler(_chaveDesde);
    if (desde == null) return;
    _gravar(_chaveTotal, total.inMilliseconds);
    _gravar(_chaveDesde, null);
    notifyListeners();
  }

  /// Zera tudo.
  void apagar() {
    _gravar(_chaveTotal, null);
    _gravar(_chaveDesde, null);
    notifyListeners();
  }
}

/// Um cronometro por projeto, compartilhado pelo menu e pela barra.
final cronometroDeEdicaoProvider =
    ChangeNotifierProvider.family<CronometroDeEdicao, String>((ref, id) {
      SharedPreferences? prefs;
      try {
        prefs = ref.read(sharedPreferencesProvider);
      } catch (_) {
        prefs = null;
      }
      return CronometroDeEdicao(id, prefs: prefs);
    });

/// "1:02:03" com horas, "12:03" sem.
String textoDoCronometro(Duration d) {
  final h = d.inHours;
  final m = d.inMinutes % 60;
  final s = d.inSeconds % 60;
  final mm = m.toString().padLeft(2, '0');
  final ss = s.toString().padLeft(2, '0');
  return h > 0 ? '$h:$mm:$ss' : '$mm:$ss';
}
