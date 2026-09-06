import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

/// CORRECAO 10.1.1 — as contagens que o documento manda fazer.
///
/// Sao regras sobre a FORMA do codigo, entao o teste le o codigo. Parece
/// estranho num teste, mas e a unica maneira de "nenhum slider sobrou" e
/// "nada so existe dentro de tres pontinhos" pararem de depender de
/// alguem lembrar.

List<File> _fontes() {
  final dir = Directory('lib');
  return [
    for (final f in dir.listSync(recursive: true))
      if (f is File && f.path.endsWith('.dart')) f,
  ];
}

/// O arquivo sem as linhas de comentario — senao um comentario que cita
/// "slider" derruba o teste.
String _semComentarios(File f) {
  final linhas = f.readAsLinesSync();
  return [
    for (final l in linhas)
      if (!l.trimLeft().startsWith('//')) l,
  ].join('\n');
}

void main() {
  group('Correcao 10.1.1 — contagens obrigatorias', () {
    test('sliders pequenos restantes: zero', () {
      final culpados = <String>[];
      for (final f in _fontes()) {
        final codigo = _semComentarios(f);
        if (codigo.contains('CupertinoSlider(') ||
            RegExp(r'[^a-zA-Z_]Slider\(').hasMatch(codigo)) {
          culpados.add(f.path);
        }
      }
      expect(culpados, isEmpty,
          reason: 'todo numero se ajusta pela superficie de arrasto '
              '(AmTickRuler), nunca por slider: $culpados');
    });

    test('o editor de curvas nao tem mais menu escondido', () {
      final f = File(
          'lib/src/features/editor/presentation/am/curve_panel.dart');
      final codigo = _semComentarios(f);
      expect(codigo.contains('_showMenu'), isFalse);
      // Os quatro comandos que o documento nomeia, agora como botoes.
      for (final rotulo in ["'Copiar'", "'Colar'", "'Em todos'",
        "'Overshoot'"]) {
        expect(codigo, contains(rotulo), reason: rotulo);
      }
    });

    test('o Modulo Grade nao mora mais dentro do Mais', () {
      final f = File(
          'lib/src/features/editor/presentation/am/layer_menu.dart');
      final codigo = _semComentarios(f);
      // Existe como tile da grade...
      expect(codigo, contains("rotulo: 'Clonar'"));
      // ...e nao como item do menu escondido. (O texto 'Modulo Grade'
      // continua existindo: e o TITULO da folha que o tile abre.)
      expect(codigo.contains("item(CupertinoIcons.circle_grid_3x3"), isFalse);
    });

    test('o trilho esquerdo tem tres itens, sem tres pontinhos', () {
      final f = File(
          'lib/src/features/editor/presentation/am/panel_chrome.dart');
      final codigo = _semComentarios(f);
      expect(codigo.contains('onMais'), isFalse);
      expect(codigo.contains('CupertinoIcons.ellipsis'), isFalse);
    });

    test('o painel de transformacao nao tem mais menu escondido', () {
      final f = File(
          'lib/src/features/editor/presentation/am/transform_panel.dart');
      final codigo = _semComentarios(f);
      expect(codigo.contains('_menuMais'), isFalse);
      expect(codigo, contains("rotulo: 'Resetar'"));
    });

    test('nenhum menu de tres pontinhos restou no aplicativo', () {
      // A tarefa de aceite do 10.1.3: percorrer as dez secoes e contar
      // quantos tres pontinhos sobraram. O resultado tem de ser zero.
      final culpados = <String>[];
      for (final f in _fontes()) {
        if (_semComentarios(f).contains('CupertinoIcons.ellipsis')) {
          culpados.add(f.path);
        }
      }
      expect(culpados, isEmpty,
          reason: 'funcao nenhuma pode existir so dentro de menu '
              'escondido: $culpados');
    });

    test('nenhuma gaveta no aplicativo', () {
      final culpados = <String>[];
      for (final f in _fontes()) {
        if (_semComentarios(f).contains('Drawer(')) culpados.add(f.path);
      }
      expect(culpados, isEmpty);
    });
  });
}
