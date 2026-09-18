// A TRADUCAO NAO PODE TER BURACO — e o buraco tem de ser VISIVEL.
//
// O DEFEITO QUE ISTO PEGA, e por que ele e pior do que parece: uma chave
// que passa por `AppText` mas nao existe em `languages.tsv` NAO FALHA. O
// `translateFor` devolve `appTranslations[source]?[code] ?? source` — ou
// seja, cai no portugues. A tela abre em japones, aquele botao continua em
// portugues, e nao ha erro, registro nem aviso. So o testador ve.
//
// COMO O TESTE E MONTADO, e por que nao e "liste tudo e pronto":
//
//   * a varredura do codigo roda AQUI, e nao num relatorio que ninguem le.
//     Ela le os literais em posicao de `AppText`/`translate`, junta os
//     pedacos partidos em varias linhas e confere contra o catalogo;
//
//   * A DIVIDA CONHECIDA ESTA CONGELADA em `traducao_pendente.txt`. O teste
//     falha quando aparece chave NOVA fora do catalogo — que e o que impede
//     o buraco de crescer — e falha TAMBEM quando uma chave da lista ganha
//     traducao, para a lista encolher de verdade em vez de virar paisagem.
//
// Sem a segunda regra, a lista viraria um deposito: ninguem tira nada, e
// daqui a seis meses ela tem o dobro.
import 'dart:io';

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:aurea/src/core/l10n/translations.dart';
import 'package:flutter_test/flutter_test.dart';

/// Um literal Dart de aspas simples, sem quebra de linha dentro.
final _literal = RegExp(r"'((?:[^'\\\n]|\\.)*)'");

/// As chamadas em que um literal VIRA texto de tela.
final _chamada = RegExp(r'(?:AppText|translate|translateFor)\s*\(');

/// Onde procurar. `test/` e `tool/` ficam de fora de proposito: o texto de
/// um teste nao chega ao usuario.
const _pastas = ['lib'];

Set<String> _chavesDoCatalogo() => appTranslations.keys.toSet();

/// Junta os literais ADJACENTES de uma chamada, a partir do `(` em [pos].
///
/// `AppText('Uma frase longa ' 'que continua')` e UM texto. Lido linha a
/// linha ele vira dois pedacos truncados, e a chave gerada nao existe — o
/// teste acusaria um texto que esta traduzido.
///
/// O primeiro argumento que NAO seja literal encerra a leitura: sem isso,
/// `AppText('x', style: TextStyle(...))` recolheria literais do estilo.
String? _juntar(String fonte, int pos) {
  if (pos >= fonte.length || fonte[pos] != '(') return null;
  final pedacos = <String>[];
  var i = pos + 1;
  while (i < fonte.length) {
    final c = fonte[i];
    if (c.trim().isEmpty) {
      i++;
      continue;
    }
    if (c == ',') {
      if (pedacos.isNotEmpty) {
        var j = i + 1;
        while (j < fonte.length && fonte[j].trim().isEmpty) {
          j++;
        }
        if (j >= fonte.length || fonte[j] != "'") break;
      }
      i++;
      continue;
    }
    if (c == ')') break;
    if (c == "'") {
      final m = _literal.matchAsPrefix(fonte, i);
      if (m == null) {
        final fim = fonte.indexOf("'", i + 1);
        if (fim < 0) return null;
        pedacos.add(fonte.substring(i + 1, fim));
        i = fim + 1;
        continue;
      }
      pedacos.add(m.group(1)!);
      i = m.end;
      continue;
    }
    break;
  }
  return pedacos.isEmpty ? null : pedacos.join();
}

/// SO INTERPOLACAO NAO TEM O QUE TRADUZIR: `'${i + 1}'` nao e texto.
bool _soMarcadores(String t) {
  var limpo = t.replaceAll(RegExp(r'\$\{[^}]*\}'), '');
  limpo = limpo.replaceAll(RegExp(r'\$[A-Za-z_][A-Za-z0-9_]*'), '');
  return !RegExp(r'[A-Za-zÀ-ÿ]').hasMatch(limpo);
}

/// TODAS AS CHAVES DE INTERFACE USADAS NO CODIGO, e onde.
Map<String, String> _chavesUsadas() {
  final usadas = <String, String>{};
  for (final pasta in _pastas) {
    final dir = Directory(pasta);
    if (!dir.existsSync()) continue;
    for (final e in dir.listSync(recursive: true)) {
      if (e is! File || !e.path.endsWith('.dart')) continue;
      final rel = e.path.replaceAll('\\', '/');
      if (rel.contains('/l10n/')) continue;
      final fonte = e.readAsStringSync();
      for (final m in _chamada.allMatches(fonte)) {
        final bruto = _juntar(fonte, m.end - 1);
        if (bruto == null || bruto.isEmpty) continue;
        // A CHAVE DO CATALOGO E O TEXTO COM A QUEBRA DE VERDADE. No
        // codigo ela aparece como os dois caracteres `\n`; no catalogo,
        // como uma quebra de linha. Comparar sem desescapar acusaria
        // dezenas de textos que ESTAO traduzidos.
        final texto = bruto.replaceAll(r'\n', '\n');
        if (_soMarcadores(texto)) continue;
        // TEXTO MONTADO FICA DE FORA DESTA CONTA, e nao por descuido.
        //
        // `AppText('Excluir ${ids.length} projetos?')` monta a string ANTES
        // de procurar no catalogo — e o que chega la e "Excluir 3
        // projetos?", que nunca vai casar com molde nenhum. Texto com
        // interpolacao NAO TEM COMO ser traduzido por esta via, e listar a
        // chave crua aqui seria fingir que da.
        //
        // O conserto e outro (um molde com marcadores e os valores
        // entrando depois), e esta anotado como o que falta.
        if (texto.contains(r'$')) continue;
        final linha = fonte.substring(0, m.start).split('\n').length;
        usadas.putIfAbsent(texto, () => '$rel:$linha');
      }
    }
  }
  return usadas;
}

/// A DIVIDA CONGELADA, lida do arquivo.
Set<String> _pendentes() {
  final f = File('test/traducao_pendente.txt');
  if (!f.existsSync()) return {};
  return {
    for (final l in f.readAsLinesSync())
      if (l.trim().isNotEmpty && !l.startsWith('#'))
        l.replaceAll('\\n', '\n'),
  };
}

void main() {
  final catalogo = _chavesDoCatalogo();
  final usadas = _chavesUsadas();
  final pendentes = _pendentes();

  test('o catalogo tem os idiomas que o seletor oferece', () {
    // UM IDIOMA NA LISTA SEM COLUNA NO CATALOGO e uma tela que fica em
    // portugues inteira — e ninguem descobre ate um testador escolher.
    final colunas = appTranslations.values.first.keys.toSet();
    for (final codigo in appLanguages.keys) {
      if (codigo == 'pt') continue;
      expect(
        colunas,
        contains(codigo),
        reason: 'o seletor oferece "$codigo" e o catalogo nao tem a coluna',
      );
    }
  });

  test('nenhuma traducao esta vazia', () {
    // CELULA VAZIA E PIOR QUE CELULA ERRADA: a tela fica em branco.
    final vazias = <String>[];
    appTranslations.forEach((chave, porIdioma) {
      porIdioma.forEach((codigo, valor) {
        if (valor.trim().isEmpty) vazias.add('[$codigo] $chave');
      });
    });
    expect(vazias, isEmpty, reason: vazias.take(20).join('\n'));
  });

  test('nenhuma chave nova fica sem traducao', () {
    // A REGRA QUE IMPEDE O BURACO DE CRESCER. Uma tela nova com um
    // `AppText('...')` que ninguem cadastrou cai aqui, com o arquivo e a
    // linha — e nao na mao do testador.
    final novas = <String>[];
    usadas.forEach((chave, onde) {
      if (catalogo.contains(chave)) return;
      if (pendentes.contains(chave)) return;
      novas.add('$onde  ->  $chave');
    });
    expect(
      novas,
      isEmpty,
      reason:
          'chaves de interface sem traducao (${novas.length}). Cadastre em '
          'tool/languages.tsv e rode tool/generate_languages.py:\n'
          '${novas.take(30).join('\n')}',
    );
  });

  test('a divida congelada so ENCOLHE', () {
    // UMA CHAVE DA LISTA QUE PASSOU A TER TRADUCAO TEM DE SAIR DELA. Sem
    // esta regra, a lista viraria deposito: ninguem tira nada, e daqui a
    // seis meses ela tem o dobro.
    final resolvidas = [
      for (final k in pendentes)
        if (catalogo.contains(k)) k,
    ];
    expect(
      resolvidas,
      isEmpty,
      reason:
          'estas chaves ja tem traducao e continuam na lista de pendentes. '
          'Rode tool/chaves_faltando.py e atualize test/traducao_pendente.txt:\n'
          '${resolvidas.take(20).join('\n')}',
    );
  });

  test('todo texto montado preserva os marcadores', () {
    // UMA TRADUCAO QUE PERDE O `$` PERDE O NUMERO. `'Excluir $n projetos?'`
    // traduzido sem o marcador vira "Delete projects?" — o numero some, e o
    // texto deixa de dizer QUANTOS. Pior: se a traducao inventar um
    // marcador que nao existe no original, o Dart nao resolve e a tela
    // mostra o nome da variavel.
    final problemas = <String>[];
    for (final entrada in usadas.entries) {
      final chave = entrada.key;
      if (!chave.contains(r'$')) continue;
      final traducao = appTranslations[chave];
      if (traducao == null) continue;
      for (final codigo in appLanguages.keys) {
        if (codigo == 'pt') continue;
        final t = traducao[codigo] ?? '';
        if (t.isEmpty) continue;
        final faltam = RegExp(r'\$[A-Za-z_{]').allMatches(chave).length;
        final tem = RegExp(r'\$[A-Za-z_{]').allMatches(t).length;
        if (faltam != tem) {
          problemas.add('[$codigo] $chave  ->  $t');
        }
      }
    }
    expect(
      problemas,
      isEmpty,
      reason:
          'traducao que perdeu (ou inventou) marcador:\n'
          '${problemas.take(20).join('\n')}',
    );
  });
}
