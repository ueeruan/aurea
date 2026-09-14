import 'dart:math' as math;
import 'dart:typed_data';

/// LEITOR DE FONTE TRUETYPE, EM DART PURO.
///
/// O motor de texto do Flutter desenha a letra, mas nunca entrega o
/// CONTORNO dela — e sem contorno nao existe texto 3D: extrusao, chanfro
/// e normal saem da curva, nao do pixel. Nenhum pacote do projeto le
/// contorno de fonte, e o formato TrueType e pequeno e estavel desde
/// 1991; ler direto sai mais barato do que depender de alguem.
///
/// O que este leitor cobre, e so isto:
///   head/maxp/hhea/hmtx  medidas da fonte e o avanco de cada glifo
///   cmap 4 e 12          do caractere ao glifo (BMP e alem dele)
///   loca/glyf            contornos simples e compostos (os acentos)
///   GPOS e kern          kerning por par, quando a fonte traz
///
/// Fica de fora, de proposito: contorno CFF (OpenType 'OTTO'), fonte
/// variavel e shaping complexo (arabe, indiano). Uma fonte assim e
/// recusada com [FonteNaoSuportada], e o motivo vai para a interface.
///
/// SEM `dart:ui` DE PROPOSITO: este arquivo e a malha do texto rodam
/// num script de Dart puro, fora do laco de testes do Flutter.

/// Por que uma fonte nao virou texto 3D. A interface traduz cada um.
enum RecusaDaFonte { contornoCff, corrompida, semContornos, colecaoVazia }

class FonteNaoSuportada implements Exception {
  const FonteNaoSuportada(this.recusa, [this.detalhe = '']);

  final RecusaDaFonte recusa;
  final String detalhe;

  @override
  String toString() =>
      'FonteNaoSuportada(${recusa.name}${detalhe.isEmpty ? '' : ': $detalhe'})';
}

/// Um trecho do contorno: RETA ate ([x], [y]) ou CURVA quadratica com o
/// ponto de controle ([cx], [cy]). Unidades da fonte, Y PARA CIMA — a
/// mesma orientacao da cena 3D, entao nada e espelhado no caminho.
class TrechoDoContorno {
  const TrechoDoContorno.reta(this.x, this.y) : cx = null, cy = null;
  const TrechoDoContorno.curva(double this.cx, double this.cy, this.x, this.y);

  final double? cx;
  final double? cy;
  final double x;
  final double y;

  bool get ehCurva => cx != null;
}

/// Um contorno fechado de glifo: o ponto de partida e os trechos, o
/// ultimo voltando ao comeco.
class ContornoDoGlifo {
  const ContornoDoGlifo(this.inicioX, this.inicioY, this.trechos);

  final double inicioX;
  final double inicioY;
  final List<TrechoDoContorno> trechos;

  /// O CONTORNO EM PONTOS, com erro maximo [tolerancia] (unidades da
  /// fonte) entre a curva e a reta que a substitui.
  ///
  /// Uma quadratica dividida em n pedacos iguais de parametro erra no
  /// maximo |P0 - 2P1 + P2| / (8 n^2). Isolando n, cada curva ganha so
  /// os pontos de que precisa: uma curva quase reta sai com um pedaco,
  /// o bojo de um "O" com varios — e o orcamento de triangulos vai para
  /// onde o olho ve curva.
  ///
  /// Devolve [x0, y0, x1, y1, ...], sem repetir o primeiro no fim e sem
  /// pontos colados (vertice duplicado vira triangulo de area zero).
  Float64List planificar(double tolerancia) {
    final tol = tolerancia <= 0 ? 1e-3 : tolerancia;
    final out = <double>[inicioX, inicioY];
    var px = inicioX, py = inicioY;
    void por(double x, double y) {
      final n = out.length;
      if ((out[n - 2] - x).abs() < 1e-7 && (out[n - 1] - y).abs() < 1e-7) {
        return;
      }
      out
        ..add(x)
        ..add(y);
    }

    for (final t in trechos) {
      if (!t.ehCurva) {
        por(t.x, t.y);
      } else {
        final cx = t.cx!, cy = t.cy!;
        final ddx = px - 2 * cx + t.x, ddy = py - 2 * cy + t.y;
        final dd = math.sqrt(ddx * ddx + ddy * ddy);
        final n = math.max(1, math.min(64, math.sqrt(dd / (8 * tol)).ceil()));
        for (var i = 1; i <= n; i++) {
          final s = i / n, u = 1 - s;
          por(
            u * u * px + 2 * u * s * cx + s * s * t.x,
            u * u * py + 2 * u * s * cy + s * s * t.y,
          );
        }
      }
      px = t.x;
      py = t.y;
    }
    // O ultimo trecho volta ao comeco: o ponto final repete o inicial.
    while (out.length >= 4 &&
        (out[out.length - 2] - out[0]).abs() < 1e-7 &&
        (out[out.length - 1] - out[1]).abs() < 1e-7) {
      out
        ..removeLast()
        ..removeLast();
    }
    return Float64List.fromList(out);
  }
}

/// O glifo de um caractere: contornos e quanto a caneta anda depois dele.
class GlifoDaFonte {
  const GlifoDaFonte({required this.contornos, required this.avanco});

  final List<ContornoDoGlifo> contornos;
  final double avanco;
}

/// DE ONDE O TEXTO 3D TIRA AS LETRAS.
///
/// A fonte de verdade ([FonteTrueType]) e a copia guardada no projeto
/// (os glifos que o texto usa) respondem a mesma coisa: assim um projeto
/// aberto noutro aparelho, sem a fonte importada, continua com o texto
/// no lugar.
abstract class FonteDeGlifos {
  double get unidadesPorEm;

  /// Acima da linha de base (positivo) e abaixo dela (negativo).
  double get ascendente;
  double get descendente;
  double get entreLinhas;

  /// Nulo quando a fonte nao tem o caractere.
  GlifoDaFonte? glifoDoCaractere(int codigo);

  /// Ajuste de avanco entre dois caracteres vizinhos, em unidades da fonte.
  double kerningEntre(int esquerdo, int direito);
}

class FonteTrueType implements FonteDeGlifos {
  FonteTrueType._(this._b, this._tabelas);

  final ByteData _b;
  final Map<String, ({int offset, int tamanho})> _tabelas;

  late final int _unidadesPorEm;
  late final int _formatoDoLoca;
  late final int _glifos;
  late final int _metricasH;
  late final double _ascendente, _descendente, _entreLinhas;
  late final int _cmap; // offset da subtabela escolhida, ou -1
  late final int _formatoDoCmap;
  bool _cmapSimbolo = false;

  final Map<int, GlifoDaFonte> _porGlifo = {};
  final Map<int, double> _kernCache = {};
  List<int>? _lookupsDeKern;

  /// Le a fonte inteira a partir dos bytes do arquivo.
  ///
  /// Lanca [FonteNaoSuportada] com o motivo quando nao da: contorno CFF,
  /// arquivo corrompido, fonte sem `glyf`. Nunca lanca `RangeError` cru —
  /// um arquivo truncado nao pode derrubar a folha que escolheu a fonte.
  factory FonteTrueType.ler(Uint8List bytes) {
    try {
      return _ler(ByteData.sublistView(bytes));
    } on FonteNaoSuportada {
      rethrow;
    } catch (e) {
      throw FonteNaoSuportada(RecusaDaFonte.corrompida, '$e');
    }
  }

  /// O motivo de recusa de [bytes], ou nulo se a fonte serve.
  static RecusaDaFonte? motivoDeRecusa(Uint8List bytes) {
    try {
      FonteTrueType.ler(bytes);
      return null;
    } on FonteNaoSuportada catch (e) {
      return e.recusa;
    }
  }

  static FonteTrueType _ler(ByteData b) {
    if (b.lengthInBytes < 12) {
      throw const FonteNaoSuportada(RecusaDaFonte.corrompida, 'curto');
    }
    var diretorio = 0;
    final assinatura = b.getUint32(0);
    // COLECAO (.ttc): varias fontes num arquivo so. Vale a primeira — os
    // offsets das tabelas sao absolutos, entao basta ler o diretorio dela.
    if (assinatura == 0x74746366) {
      final quantas = b.getUint32(8);
      if (quantas == 0) {
        throw const FonteNaoSuportada(RecusaDaFonte.colecaoVazia);
      }
      diretorio = b.getUint32(12);
    }
    final versao = b.getUint32(diretorio);
    if (versao == 0x4F54544F) {
      throw const FonteNaoSuportada(RecusaDaFonte.contornoCff);
    }
    if (versao != 0x00010000 && versao != 0x74727565) {
      throw const FonteNaoSuportada(RecusaDaFonte.corrompida, 'assinatura');
    }
    final n = b.getUint16(diretorio + 4);
    final tabelas = <String, ({int offset, int tamanho})>{};
    for (var i = 0; i < n; i++) {
      final r = diretorio + 12 + 16 * i;
      final tag = String.fromCharCodes([
        b.getUint8(r),
        b.getUint8(r + 1),
        b.getUint8(r + 2),
        b.getUint8(r + 3),
      ]);
      final offset = b.getUint32(r + 8), tamanho = b.getUint32(r + 12);
      if (offset + tamanho > b.lengthInBytes) {
        throw FonteNaoSuportada(RecusaDaFonte.corrompida, 'tabela $tag');
      }
      tabelas[tag] = (offset: offset, tamanho: tamanho);
    }
    for (final obrigatoria in const ['head', 'maxp', 'cmap', 'hmtx', 'hhea']) {
      if (!tabelas.containsKey(obrigatoria)) {
        throw FonteNaoSuportada(RecusaDaFonte.corrompida, 'sem $obrigatoria');
      }
    }
    // OpenType com cabecalho TrueType mas contorno CFF: acontece.
    if (!tabelas.containsKey('glyf') || !tabelas.containsKey('loca')) {
      throw FonteNaoSuportada(
        tabelas.containsKey('CFF ') || tabelas.containsKey('CFF2')
            ? RecusaDaFonte.contornoCff
            : RecusaDaFonte.semContornos,
      );
    }
    final f = FonteTrueType._(b, tabelas);
    final head = tabelas['head']!.offset;
    if (b.getUint32(head + 12) != 0x5F0F3CF5) {
      throw const FonteNaoSuportada(RecusaDaFonte.corrompida, 'head');
    }
    f._unidadesPorEm = math.max(16, b.getUint16(head + 18));
    f._formatoDoLoca = b.getInt16(head + 50);
    f._glifos = b.getUint16(tabelas['maxp']!.offset + 4);
    final hhea = tabelas['hhea']!.offset;
    f._ascendente = b.getInt16(hhea + 4).toDouble();
    f._descendente = b.getInt16(hhea + 6).toDouble();
    f._entreLinhas = b.getInt16(hhea + 8).toDouble();
    f._metricasH = math.max(1, b.getUint16(hhea + 34));
    f._escolherCmap();
    return f;
  }

  @override
  double get unidadesPorEm => _unidadesPorEm.toDouble();

  @override
  double get ascendente => _ascendente > 0 ? _ascendente : _unidadesPorEm * 0.8;

  @override
  double get descendente =>
      _descendente < 0 ? _descendente : -_unidadesPorEm * 0.2;

  @override
  double get entreLinhas => math.max(0, _entreLinhas);

  int get quantidadeDeGlifos => _glifos;

  // ------------------------------------------------------------- cmap

  /// Escolhe a melhor tabela de caracteres: Unicode completo (formato 12)
  /// antes do BMP (formato 4). Fonte de simbolos (3,0) mapeia em 0xF000.
  void _escolherCmap() {
    final cmap = _tabelas['cmap']!.offset;
    final quantas = _b.getUint16(cmap + 2);
    var melhor = -1, formato = 0, nota = -1;
    for (var i = 0; i < quantas; i++) {
      final r = cmap + 4 + 8 * i;
      final plataforma = _b.getUint16(r), codificacao = _b.getUint16(r + 2);
      final sub = cmap + _b.getUint32(r + 4);
      if (sub + 2 > _b.lengthInBytes) continue;
      final f = _b.getUint16(sub);
      if (f != 4 && f != 12) continue;
      final unicode =
          plataforma == 0 ||
          (plataforma == 3 && (codificacao == 1 || codificacao == 10));
      final simbolo = plataforma == 3 && codificacao == 0;
      if (!unicode && !simbolo) continue;
      final pontos = (f == 12 ? 4 : 2) + (unicode ? 2 : 0);
      if (pontos > nota) {
        nota = pontos;
        melhor = sub;
        formato = f;
        _cmapSimbolo = simbolo;
      }
    }
    _cmap = melhor;
    _formatoDoCmap = formato;
  }

  /// O indice do glifo de [codigo]; 0 (o .notdef) quando nao ha.
  int glifoDe(int codigo) {
    if (_cmap < 0) return 0;
    var g = _formatoDoCmap == 12 ? _cmap12(codigo) : _cmap4(codigo);
    if (g == 0 && _cmapSimbolo && codigo < 0x100) {
      g = _formatoDoCmap == 12
          ? _cmap12(0xF000 | codigo)
          : _cmap4(0xF000 | codigo);
    }
    return g < _glifos ? g : 0;
  }

  int _cmap4(int c) {
    if (c > 0xFFFF) return 0;
    final t = _cmap;
    final segmentos = _b.getUint16(t + 6) ~/ 2;
    final fins = t + 14;
    var lo = 0, hi = segmentos - 1;
    while (lo < hi) {
      final meio = (lo + hi) >> 1;
      if (_b.getUint16(fins + 2 * meio) < c) {
        lo = meio + 1;
      } else {
        hi = meio;
      }
    }
    if (segmentos == 0 || _b.getUint16(fins + 2 * lo) < c) return 0;
    final inicios = fins + 2 * segmentos + 2;
    final inicio = _b.getUint16(inicios + 2 * lo);
    if (inicio > c) return 0;
    final delta = _b.getInt16(inicios + 2 * segmentos + 2 * lo);
    final desvios = inicios + 4 * segmentos;
    final desvio = _b.getUint16(desvios + 2 * lo);
    if (desvio == 0) return (c + delta) & 0xFFFF;
    final endereco = desvios + 2 * lo + desvio + 2 * (c - inicio);
    if (endereco + 2 > _b.lengthInBytes) return 0;
    final g = _b.getUint16(endereco);
    return g == 0 ? 0 : (g + delta) & 0xFFFF;
  }

  int _cmap12(int c) {
    final t = _cmap;
    final grupos = _b.getUint32(t + 12);
    var lo = 0, hi = grupos - 1;
    while (lo <= hi) {
      final meio = (lo + hi) >> 1;
      final r = t + 16 + 12 * meio;
      final inicio = _b.getUint32(r), fim = _b.getUint32(r + 4);
      if (c < inicio) {
        hi = meio - 1;
      } else if (c > fim) {
        lo = meio + 1;
      } else {
        return _b.getUint32(r + 8) + (c - inicio);
      }
    }
    return 0;
  }

  // ------------------------------------------------------------- hmtx

  double avancoDoGlifo(int glifo) {
    final hmtx = _tabelas['hmtx']!;
    final i = math.min(glifo, _metricasH - 1);
    final r = hmtx.offset + 4 * i;
    if (r + 2 > hmtx.offset + hmtx.tamanho) return _unidadesPorEm * 0.5;
    return _b.getUint16(r).toDouble();
  }

  // ------------------------------------------------------------- glyf

  @override
  GlifoDaFonte? glifoDoCaractere(int codigo) {
    final g = glifoDe(codigo);
    if (g == 0) return null;
    return glifo(g);
  }

  /// O glifo de indice [indice], guardado depois da primeira leitura.
  ///
  /// UM GLIFO QUEBRADO SAI VAZIO, e nao derruba a fonte: o texto perde
  /// uma letra em vez de perder todas.
  GlifoDaFonte glifo(int indice) => _porGlifo[indice] ??= () {
    List<ContornoDoGlifo> contornos;
    try {
      contornos = _paraTrechos(_pontosDoGlifo(indice, 0));
    } catch (_) {
      contornos = const [];
    }
    return GlifoDaFonte(contornos: contornos, avanco: avancoDoGlifo(indice));
  }();

  /// Onde o glifo mora dentro de `glyf`: (inicio, fim).
  (int, int) _faixaDoGlifo(int indice) {
    if (indice < 0 || indice >= _glifos) return (0, 0);
    final loca = _tabelas['loca']!.offset;
    final int a, z;
    if (_formatoDoLoca == 0) {
      a = _b.getUint16(loca + 2 * indice) * 2;
      z = _b.getUint16(loca + 2 * indice + 2) * 2;
    } else {
      a = _b.getUint32(loca + 4 * indice);
      z = _b.getUint32(loca + 4 * indice + 4);
    }
    final glyf = _tabelas['glyf']!;
    if (z <= a || z > glyf.tamanho) return (0, 0);
    return (glyf.offset + a, glyf.offset + z);
  }

  /// Os contornos crus: pontos com a marca "sobre a curva".
  List<List<_Ponto>> _pontosDoGlifo(int indice, int profundidade) {
    // Composto que aponta para si mesmo (ou uma cadeia sem fim) nao pode
    // travar o quadro: oito niveis e mais do que qualquer acento usa.
    if (profundidade > 8) return const [];
    final (inicio, fim) = _faixaDoGlifo(indice);
    if (fim - inicio < 10) return const [];
    final contornos = _b.getInt16(inicio);
    if (contornos >= 0) return _glifoSimples(inicio, fim, contornos);
    return _glifoComposto(inicio, fim, profundidade);
  }

  List<List<_Ponto>> _glifoSimples(int inicio, int fim, int contornos) {
    if (contornos == 0) return const [];
    var p = inicio + 10;
    final finais = <int>[];
    for (var i = 0; i < contornos; i++) {
      finais.add(_b.getUint16(p));
      p += 2;
    }
    final total = finais.last + 1;
    final instrucoes = _b.getUint16(p);
    p += 2 + instrucoes;
    final bandeiras = Uint8List(total);
    for (var i = 0; i < total;) {
      if (p >= fim) throw RangeError('bandeiras');
      final f = _b.getUint8(p++);
      bandeiras[i++] = f;
      if (f & 0x08 != 0) {
        var repete = _b.getUint8(p++);
        while (repete-- > 0 && i < total) {
          bandeiras[i++] = f;
        }
      }
    }
    final xs = Float64List(total), ys = Float64List(total);
    var x = 0;
    for (var i = 0; i < total; i++) {
      final f = bandeiras[i];
      if (f & 0x02 != 0) {
        final d = _b.getUint8(p++);
        x += f & 0x10 != 0 ? d : -d;
      } else if (f & 0x10 == 0) {
        x += _b.getInt16(p);
        p += 2;
      }
      xs[i] = x.toDouble();
    }
    var y = 0;
    for (var i = 0; i < total; i++) {
      final f = bandeiras[i];
      if (f & 0x04 != 0) {
        final d = _b.getUint8(p++);
        y += f & 0x20 != 0 ? d : -d;
      } else if (f & 0x20 == 0) {
        y += _b.getInt16(p);
        p += 2;
      }
      ys[i] = y.toDouble();
    }
    if (p > fim) throw RangeError('coordenadas');
    final out = <List<_Ponto>>[];
    var comeco = 0;
    for (final ultimo in finais) {
      if (ultimo < comeco || ultimo >= total) break;
      out.add([
        for (var i = comeco; i <= ultimo; i++)
          _Ponto(xs[i], ys[i], bandeiras[i] & 0x01 != 0),
      ]);
      comeco = ultimo + 1;
    }
    return out;
  }

  /// GLIFO COMPOSTO: um "a" com um acento em cima, cada peca com a sua
  /// transformacao. E como a maioria das fontes desenha "ç", "ã", "é".
  List<List<_Ponto>> _glifoComposto(int inicio, int fim, int profundidade) {
    var p = inicio + 10;
    final out = <List<_Ponto>>[];
    while (true) {
      final bandeiras = _b.getUint16(p);
      final componente = _b.getUint16(p + 2);
      p += 4;
      final palavras = bandeiras & 0x0001 != 0;
      final xy = bandeiras & 0x0002 != 0;
      int arg1, arg2;
      if (palavras) {
        arg1 = xy ? _b.getInt16(p) : _b.getUint16(p);
        arg2 = xy ? _b.getInt16(p + 2) : _b.getUint16(p + 2);
        p += 4;
      } else {
        arg1 = xy ? _b.getInt8(p) : _b.getUint8(p);
        arg2 = xy ? _b.getInt8(p + 1) : _b.getUint8(p + 1);
        p += 2;
      }
      var a = 1.0, bb = 0.0, c = 0.0, d = 1.0;
      if (bandeiras & 0x0008 != 0) {
        a = d = _f2dot14(p);
        p += 2;
      } else if (bandeiras & 0x0040 != 0) {
        a = _f2dot14(p);
        d = _f2dot14(p + 2);
        p += 4;
      } else if (bandeiras & 0x0080 != 0) {
        a = _f2dot14(p);
        bb = _f2dot14(p + 2);
        c = _f2dot14(p + 4);
        d = _f2dot14(p + 6);
        p += 8;
      }
      final filho = _pontosDoGlifo(componente, profundidade + 1);
      final transformado = [
        for (final contorno in filho)
          [
            for (final q in contorno)
              _Ponto(a * q.x + c * q.y, bb * q.x + d * q.y, q.naCurva),
          ],
      ];
      double dx, dy;
      if (xy) {
        dx = arg1.toDouble();
        dy = arg2.toDouble();
        // SCALED_COMPONENT_OFFSET sem UNSCALED: o deslocamento tambem
        // passa pela matriz (o jeito da Apple). O padrao e nao passar.
        if (bandeiras & 0x0800 != 0 && bandeiras & 0x1000 == 0) {
          final ox = dx, oy = dy;
          dx = a * ox + c * oy;
          dy = bb * ox + d * oy;
        }
      } else {
        // Encaixe por PONTOS: o ponto arg1 do que ja foi montado encosta
        // no ponto arg2 da peca nova.
        final ja = [for (final k in out) ...k];
        final novos = [for (final k in transformado) ...k];
        if (arg1 < ja.length && arg2 < novos.length) {
          dx = ja[arg1].x - novos[arg2].x;
          dy = ja[arg1].y - novos[arg2].y;
        } else {
          dx = 0;
          dy = 0;
        }
      }
      for (final contorno in transformado) {
        out.add([
          for (final q in contorno) _Ponto(q.x + dx, q.y + dy, q.naCurva),
        ]);
      }
      if (bandeiras & 0x0020 == 0 || p >= fim) break;
    }
    return out;
  }

  double _f2dot14(int p) => _b.getInt16(p) / 16384.0;

  /// Dos pontos crus aos trechos. Dois pontos fora da curva seguidos
  /// guardam, entre eles, um ponto sobre a curva que o arquivo nao
  /// escreve: e assim que o TrueType economiza bytes.
  static List<ContornoDoGlifo> _paraTrechos(List<List<_Ponto>> brutos) {
    final out = <ContornoDoGlifo>[];
    for (final pts in brutos) {
      final n = pts.length;
      if (n < 2) continue;
      final k = pts.indexWhere((q) => q.naCurva);
      double ix, iy;
      final List<_Ponto> sequencia;
      if (k < 0) {
        // So pontos de controle (um circulo perfeito, por exemplo).
        ix = (pts[n - 1].x + pts[0].x) / 2;
        iy = (pts[n - 1].y + pts[0].y) / 2;
        sequencia = pts;
      } else {
        ix = pts[k].x;
        iy = pts[k].y;
        sequencia = [...pts.sublist(k + 1), ...pts.sublist(0, k)];
      }
      final trechos = <TrechoDoContorno>[];
      _Ponto? controle;
      for (final q in sequencia) {
        if (q.naCurva) {
          trechos.add(
            controle == null
                ? TrechoDoContorno.reta(q.x, q.y)
                : TrechoDoContorno.curva(controle.x, controle.y, q.x, q.y),
          );
          controle = null;
        } else if (controle == null) {
          controle = q;
        } else {
          final mx = (controle.x + q.x) / 2, my = (controle.y + q.y) / 2;
          trechos.add(TrechoDoContorno.curva(controle.x, controle.y, mx, my));
          controle = q;
        }
      }
      trechos.add(
        controle == null
            ? TrechoDoContorno.reta(ix, iy)
            : TrechoDoContorno.curva(controle.x, controle.y, ix, iy),
      );
      out.add(ContornoDoGlifo(ix, iy, trechos));
    }
    return out;
  }

  // ---------------------------------------------------------- kerning

  @override
  double kerningEntre(int esquerdo, int direito) {
    final g1 = glifoDe(esquerdo), g2 = glifoDe(direito);
    if (g1 == 0 || g2 == 0) return 0;
    return kerningDosGlifos(g1, g2);
  }

  /// O KERNING DE UM PAR DE GLIFOS.
  ///
  /// GPOS primeiro, porque e onde a fonte moderna guarda o kerning — e
  /// quando ela tem GPOS, a tabela `kern` antiga fica so por
  /// compatibilidade (e o que o HarfBuzz faz). Qualquer defeito na
  /// leitura vira zero: kerning errado e feio, texto sem aparecer e pior.
  double kerningDosGlifos(int g1, int g2) =>
      _kernCache[(g1 << 16) | g2] ??= () {
        try {
          final lookups = _lookupsDeKern ??= _acharLookupsDeKern();
          if (lookups.isNotEmpty) {
            var total = 0.0;
            for (final l in lookups) {
              total += _aplicarLookupDePar(l, g1, g2) ?? 0;
            }
            return total;
          }
          return _kernAntigo(g1, g2);
        } catch (_) {
          return 0.0;
        }
      }();

  List<int> _acharLookupsDeKern() {
    final gpos = _tabelas['GPOS'];
    if (gpos == null) return const [];
    final base = gpos.offset;
    final listaDeRecursos = base + _b.getUint16(base + 6);
    final listaDeLookups = base + _b.getUint16(base + 8);
    final indices = <int>{};
    final recursos = _b.getUint16(listaDeRecursos);
    for (var i = 0; i < recursos; i++) {
      final r = listaDeRecursos + 2 + 6 * i;
      final ehKern =
          _b.getUint8(r) == 0x6B &&
          _b.getUint8(r + 1) == 0x65 &&
          _b.getUint8(r + 2) == 0x72 &&
          _b.getUint8(r + 3) == 0x6E;
      if (!ehKern) continue;
      final recurso = listaDeRecursos + _b.getUint16(r + 4);
      final quantos = _b.getUint16(recurso + 2);
      for (var j = 0; j < quantos; j++) {
        indices.add(_b.getUint16(recurso + 4 + 2 * j));
      }
    }
    final totalDeLookups = _b.getUint16(listaDeLookups);
    return [
      for (final i in indices.toList()..sort())
        if (i < totalDeLookups)
          listaDeLookups + _b.getUint16(listaDeLookups + 2 + 2 * i),
    ];
  }

  /// A primeira subtabela que conhece o par decide; nulo = nenhuma.
  double? _aplicarLookupDePar(int lookup, int g1, int g2) {
    final tipo = _b.getUint16(lookup);
    final subtabelas = _b.getUint16(lookup + 4);
    for (var s = 0; s < subtabelas; s++) {
      var sub = lookup + _b.getUint16(lookup + 6 + 2 * s);
      var tipoDaSub = tipo;
      if (tipo == 9) {
        tipoDaSub = _b.getUint16(sub + 2);
        sub += _b.getUint32(sub + 4);
      }
      if (tipoDaSub != 2) continue;
      final r = _ajusteDePar(sub, g1, g2);
      if (r != null) return r;
    }
    return null;
  }

  double? _ajusteDePar(int sub, int g1, int g2) {
    final formato = _b.getUint16(sub);
    final cobertura = _cobertura(sub + _b.getUint16(sub + 2), g1);
    if (cobertura < 0) return null;
    final valor1 = _b.getUint16(sub + 4), valor2 = _b.getUint16(sub + 6);
    final tamanho1 = _tamanhoDoValor(valor1);
    final tamanho2 = _tamanhoDoValor(valor2);
    if (formato == 1) {
      if (cobertura >= _b.getUint16(sub + 8)) return null;
      final conjunto = sub + _b.getUint16(sub + 10 + 2 * cobertura);
      final quantos = _b.getUint16(conjunto);
      final passo = 2 + tamanho1 + tamanho2;
      var lo = 0, hi = quantos - 1;
      while (lo <= hi) {
        final meio = (lo + hi) >> 1;
        final r = conjunto + 2 + meio * passo;
        final segundo = _b.getUint16(r);
        if (segundo == g2) return _avancoX(r + 2, valor1);
        if (segundo < g2) {
          lo = meio + 1;
        } else {
          hi = meio - 1;
        }
      }
      return null;
    }
    if (formato == 2) {
      final classe1 = _classe(sub + _b.getUint16(sub + 8), g1);
      final classe2 = _classe(sub + _b.getUint16(sub + 10), g2);
      final classes1 = _b.getUint16(sub + 12);
      final classes2 = _b.getUint16(sub + 14);
      if (classe1 >= classes1 || classe2 >= classes2) return 0;
      final r =
          sub + 16 + (classe1 * classes2 + classe2) * (tamanho1 + tamanho2);
      return _avancoX(r, valor1);
    }
    return null;
  }

  static int _tamanhoDoValor(int formato) {
    var bits = formato & 0xFF, n = 0;
    while (bits != 0) {
      n += bits & 1;
      bits >>= 1;
    }
    return 2 * n;
  }

  /// O XAdvance do registro de valor (0 quando o formato nao traz).
  double _avancoX(int registro, int formato) {
    if (formato & 0x0004 == 0) return 0;
    final antes =
        (formato & 0x0001 != 0 ? 2 : 0) + (formato & 0x0002 != 0 ? 2 : 0);
    return _b.getInt16(registro + antes).toDouble();
  }

  int _cobertura(int t, int g) {
    final formato = _b.getUint16(t);
    if (formato == 1) {
      var lo = 0, hi = _b.getUint16(t + 2) - 1;
      while (lo <= hi) {
        final meio = (lo + hi) >> 1;
        final v = _b.getUint16(t + 4 + 2 * meio);
        if (v == g) return meio;
        if (v < g) {
          lo = meio + 1;
        } else {
          hi = meio - 1;
        }
      }
      return -1;
    }
    if (formato == 2) {
      var lo = 0, hi = _b.getUint16(t + 2) - 1;
      while (lo <= hi) {
        final meio = (lo + hi) >> 1;
        final r = t + 4 + 6 * meio;
        final inicio = _b.getUint16(r), fim = _b.getUint16(r + 2);
        if (g < inicio) {
          hi = meio - 1;
        } else if (g > fim) {
          lo = meio + 1;
        } else {
          return _b.getUint16(r + 4) + g - inicio;
        }
      }
    }
    return -1;
  }

  int _classe(int t, int g) {
    final formato = _b.getUint16(t);
    if (formato == 1) {
      final primeiro = _b.getUint16(t + 2), quantos = _b.getUint16(t + 4);
      if (g < primeiro || g >= primeiro + quantos) return 0;
      return _b.getUint16(t + 6 + 2 * (g - primeiro));
    }
    if (formato == 2) {
      var lo = 0, hi = _b.getUint16(t + 2) - 1;
      while (lo <= hi) {
        final meio = (lo + hi) >> 1;
        final r = t + 4 + 6 * meio;
        final inicio = _b.getUint16(r), fim = _b.getUint16(r + 2);
        if (g < inicio) {
          hi = meio - 1;
        } else if (g > fim) {
          lo = meio + 1;
        } else {
          return _b.getUint16(r + 4);
        }
      }
    }
    return 0;
  }

  /// A tabela `kern` antiga (formato 0, horizontal), de fonte sem GPOS.
  double _kernAntigo(int g1, int g2) {
    final kern = _tabelas['kern'];
    if (kern == null) return 0;
    final base = kern.offset;
    if (_b.getUint16(base) != 0) return 0; // versao da Apple: fica de fora
    final tabelas = _b.getUint16(base + 2);
    var p = base + 4;
    var total = 0.0;
    final chave = (g1 << 16) | g2;
    for (var t = 0; t < tabelas; t++) {
      final tamanho = _b.getUint16(p + 2);
      final cobertura = _b.getUint16(p + 4);
      final horizontal = cobertura & 0x1 != 0;
      final minimo = cobertura & 0x2 != 0;
      final cruzado = cobertura & 0x4 != 0;
      final substitui = cobertura & 0x8 != 0;
      if (cobertura >> 8 == 0 && horizontal && !minimo && !cruzado) {
        final pares = _b.getUint16(p + 6);
        var lo = 0, hi = pares - 1;
        while (lo <= hi) {
          final meio = (lo + hi) >> 1;
          final r = p + 14 + 6 * meio;
          final k = (_b.getUint16(r) << 16) | _b.getUint16(r + 2);
          if (k == chave) {
            final v = _b.getInt16(r + 4).toDouble();
            total = substitui ? v : total + v;
            break;
          }
          if (k < chave) {
            lo = meio + 1;
          } else {
            hi = meio - 1;
          }
        }
      }
      if (tamanho < 6) break;
      p += tamanho;
    }
    return total;
  }
}

class _Ponto {
  const _Ponto(this.x, this.y, this.naCurva);
  final double x;
  final double y;
  final bool naCurva;
}
