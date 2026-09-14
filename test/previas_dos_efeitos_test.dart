// TODO EFEITO DA GALERIA TEM PREVIA DE VERDADE (pedido do beta 1.0.5).
//
// As tiras sao geradas por tool/previas_dos_efeitos_test.dart. Efeito novo
// sem tira cairia na miniatura antiga (o circulo que os testadores
// reclamaram): este teste falha ate alguem gerar de novo.
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/features/editor/domain/amostra_dos_efeitos.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('manifesto na versao atual cobre o catalogo inteiro', () {
    final arquivo = File('$pastaDasPrevias/manifesto.json');
    expect(arquivo.existsSync(), isTrue, reason: 'rode o gerador das previas');
    final m = jsonDecode(arquivo.readAsStringSync()) as Map<String, dynamic>;
    expect(m['versao'], versaoDasPrevias);
    expect(m['quadros'], quadrosDaPrevia);
    final efeitos = (m['efeitos'] as Map<String, dynamic>).keys.toSet();
    final faltando = [
      for (final s in effectSpecs.values)
        if (!efeitos.contains(s.id)) s.id,
    ];
    expect(faltando, isEmpty, reason: 'efeito sem previa: gere de novo');
  });

  test('cada tira existe, tem o tamanho de uma tira e o pacote a inclui', () {
    for (final s in effectSpecs.values) {
      final f = File('$pastaDasPrevias/${s.id}.jpg');
      expect(f.existsSync(), isTrue, reason: s.id);
      expect(f.lengthSync(), inInclusiveRange(2000, 400000), reason: s.id);
    }
    final pubspec = File('pubspec.yaml').readAsStringSync();
    expect(pubspec.contains('- $pastaDasPrevias/'), isTrue,
        reason: 'as tiras precisam estar nos assets do app');
  });
}
