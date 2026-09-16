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

    test('o overshoot nao fica ligado em segredo', () {
      final f = File(
          'lib/src/features/editor/presentation/am/curve_panel.dart');
      final codigo = _semComentarios(f);
      // Os comandos continuam existindo; o que a trava cobra e o ESTADO
      // do modo ficar a vista, e nao a ausencia do menu. Ver a nota no
      // teste do auto-key, logo abaixo.
      for (final rotulo in ['Copiar curva', 'Colar curva',
        'Aplicar em todos os segmentos', 'Overshoot']) {
        expect(codigo, contains(rotulo), reason: rotulo);
      }
      expect(codigo, contains('AmMenuIcon(ativo: _overshoot)'),
          reason: 'o botao do menu tem de mostrar que o overshoot esta '
              'ligado');
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

    // A REGRA MUDOU AQUI, e vale dizer por que.
    //
    // A 10.1.1 pedia ZERO menus de tres pontinhos no aplicativo: toda
    // funcao tinha de ter botao proprio. O painel de transformacao foi
    // redesenhado depois, e a fileira de keyframe que ocupava 48 px no
    // topo deu lugar a um menu com quatro itens — foi assim que as abas
    // passaram a caber sem rolagem, que era a queixa dos testadores.
    //
    // O que a regra protegia continua protegido, e e mais preciso do que
    // "nenhum tres-pontinhos": menu escondido pode guardar ACAO (copiar,
    // resetar — acontece e acaba), mas nao pode guardar MODO. Modo muda
    // o que TODA interacao seguinte faz: auto-key ligado transforma cada
    // ajuste em keyframe; overshoot ligado deixa a curva passar de 0..1.
    // Quem esquece um deles ligado nao percebe olhando a tela, e passa a
    // achar que o aplicativo faz coisas sozinho.
    //
    // Entao a trava agora cobra o ESTADO a vista, e nao a ausencia do
    // menu.
    test('o auto-key nao fica ligado em segredo', () {
      final f = File(
          'lib/src/features/editor/presentation/am/transform_panel.dart');
      final codigo = _semComentarios(f);
      expect(codigo.contains('_menuMais'), isFalse);
      expect(codigo, matches(RegExp(r'AmMenuIcon\(\s*ativo:')),
          reason: 'o botao do menu tem de mostrar que ha modo ligado');
      expect(codigo, contains('ref.watch(autoKeyframeProvider)'),
          reason: 'sem watch, o icone nao reage quando o modo muda');
    });

    test('so dois menus escondidos no aplicativo, e os dois com estado', () {
      // A conta continua sendo feita — o que mudou e o resultado
      // aceitavel. Dois paineis tem menu: transformacao e curva. Os dois
      // guardam modo, e os dois mostram o estado pelo AmMenuIcon. Um
      // terceiro aparecendo aqui e regressao, e o teste diz qual e.
      const permitidos = {
        'transform_panel.dart',
        'curve_panel.dart',
        // O proprio icone que mostra o estado.
        'am_widgets.dart',
        // O MENU DE CADA PROJETO na Inicio (2026-09-08). Nao e um menu de
        // painel escondendo modo: sao acoes sobre UM item da lista —
        // abrir, duplicar, renomear, excluir — no lugar do "segurar para
        // excluir" que ninguem descobria. Nao ha estado a mostrar; o
        // que a regra cobra (nada de modo escondido) continua valendo.
        'projects_tab.dart',
        // O MENU DE CADA PRESET na tela de presets (2026-09-15). Mesmo
        // caso do menu de projeto: acoes sobre UM cartao da lista —
        // aplicar, exportar, renomear, excluir. Nenhuma delas liga modo
        // nenhum, e todas tambem se alcancam pelo toque longo no cartao.
        'presets_screen.dart',
        // O ••• DE CADA EFEITO (2026-09-16), da planta do AM pedida pelo
        // dono: "▼ nome ••• lixeira". Guarda ACOES sobre um cartao —
        // ligar, duplicar, ordem, resetar, presets. O unico estado ali
        // (efeito desligado) aparece no proprio cartao: nome esmaecido e
        // olho riscado ao lado.
        'effects_panel.dart',
      };
      // O ESTUDIO 3D tem o terceiro (a barra "Cena | Camera | menu" da
      // missao de 2026-09-07). Ele nao usa o icone cru: usa o AmMenuIcon,
      // aceso quando ha modo ligado (avancado, grade, auto-key), e tudo
      // o que abre por ele tambem se alcanca pela tela — a cena pela
      // acao rapida, as vistas pelo menu da camera, os comandos pelo
      // modo avancado. E o que este teste confere abaixo.
      final estudio = File(
          'lib/src/features/editor/presentation/am/scene3d_studio.dart');
      final codigoEstudio = _semComentarios(estudio);
      expect(codigoEstudio.contains('CupertinoIcons.ellipsis'), isFalse,
          reason: 'o Estudio usa o AmMenuIcon, que carrega o estado');
      expect(RegExp(r'AmMenuIcon\(\s*ativo:').hasMatch(codigoEstudio), isTrue);
      expect(codigoEstudio.contains("ValueKey('estudio-cena')"), isTrue,
          reason: 'a cena (hierarquia) tem acao rapida fora do menu');
      final culpados = <String>[];
      for (final f in _fontes()) {
        if (!_semComentarios(f).contains('CupertinoIcons.ellipsis')) continue;
        final nome = f.path.split(RegExp(r'[/\\]')).last;
        if (!permitidos.contains(nome)) culpados.add(f.path);
      }
      expect(culpados, isEmpty,
          reason: 'menu escondido novo, sem estado a vista: $culpados');
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
