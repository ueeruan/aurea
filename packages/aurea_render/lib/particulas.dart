/// A PORTA DART DO MOTOR DE PARTICULAS.
///
/// O QUE ATRAVESSA A PONTE E O LOTE — nao pixels. O C++ simula, resolve a
/// trajetoria e devolve instancias (posicao, tamanho, cor, forma); quem
/// desenha decide o que fazer com elas. Mandar pixel pronto da nuvem
/// custaria um quadro RGBA inteiro por quadro e obrigaria a compor a
/// nuvem DUAS vezes, uma de cada lado.
///
/// O MESMO BUFFER SERVE TODOS OS QUADROS. [LoteDeParticulas] reserva uma
/// vez, com o tamanho que o C++ responde para aqueles parametros, e
/// reusa — nao ha alocacao por quadro no caminho quente.
// PARTE DE `aurea_render.dart`, E NAO UMA BIBLIOTECA PROPRIA.
//
// Os `@Native` so resolvem dentro do arquivo cujo `assetName` o build hook
// declarou (`aurea_render.dart`): um segundo arquivo com `@Native` cai no
// "No asset with id ... found" e o sintoma e a biblioteca inteira parecer
// ausente. Como `part`, os dois arquivos sao a MESMA biblioteca e o
// vinculo com o asset continua valendo.
part of 'aurea_render.dart';

/// ==================== O QUE O DART MONTA ====================
///
/// TODOS OS CAMPOS DE 4 BYTES, inclusive os que sao booleanos: um `bool`
/// de um byte no meio de floats obrigaria o Dart a declarar o mesmo
/// preenchimento, e um alinhamento diferente deslocaria todos os campos
/// seguintes em silencio. O C++ declara a mesma struct em
/// `api_particulas.cpp`, e ha teste conferindo o tamanho contra os dois
/// lados.
final class _ParticulasC extends Struct {
  @Uint32()
  external int emissor;

  @Float()
  external double centroX;
  @Float()
  external double centroY;
  @Float()
  external double centroZ;
  @Float()
  external double largura;
  @Float()
  external double altura;
  @Float()
  external double profundidade;
  @Float()
  external double raio;
  @Float()
  external double linhaX;
  @Float()
  external double linhaY;
  @Float()
  external double linhaZ;
  @Float()
  external double taxaDeNascimento;
  @Float()
  external double vidaS;
  @Float()
  external double vidaVariacao;
  @Uint32()
  external int maximo;
  @Uint32()
  external int semente;
  @Float()
  external double velocidade;
  @Float()
  external double direcaoGraus;
  @Float()
  external double aberturaGraus;
  @Uint32()
  external int modoDeEmissao;
  @Float()
  external double gravidade;
  @Float()
  external double ventoX;
  @Float()
  external double ventoY;
  @Float()
  external double ventoZ;
  @Float()
  external double arrasto;
  @Float()
  external double turbulencia;
  @Float()
  external double turbulenciaEscala;
  @Float()
  external double turbulenciaVelocidade;
  @Float()
  external double atracao;
  @Float()
  external double atracaoX;
  @Float()
  external double atracaoY;
  @Float()
  external double atracaoZ;
  @Float()
  external double tamanho;
  @Float()
  external double tamanhoVariacao;
  @Uint32()
  external int tamanhoNaVida;
  @Float()
  external double opacidade;
  @Float()
  external double opacidadeVariacao;
  @Uint32()
  external int opacidadeNaVida;
  @Uint32()
  external int corInicio;
  @Uint32()
  external int corFim;
  @Uint32()
  external int temCorFim;
  @Uint32()
  external int forma;
  @Float()
  external double giroGrausS;
  @Float()
  external double brilho;
  @Uint32()
  external int cintilar;
  @Float()
  external double rastro;
  @Uint32()
  external int faiscas;
  @Float()
  external double faiscaVidaS;
  @Float()
  external double faiscaHeranca;
  @Float()
  external double faiscaVelocidade;
  @Float()
  external double faiscaTamanho;
  @Float()
  external double faiscaInicio;
  @Float()
  external double focal;
  @Float()
  external double rotacaoXGraus;
  @Float()
  external double rotacaoYGraus;
  @Float()
  external double rotacaoZGraus;
}

@Native<Uint32 Function()>(
  symbol: 'aurea_render_particulas_tamanho_parametros',
  isLeaf: true,
)
external int _tamanhoParametros();

@Native<Uint32 Function()>(
  symbol: 'aurea_render_particulas_tamanho_instancia',
  isLeaf: true,
)
external int _tamanhoInstancia();

@Native<Uint32 Function(Pointer<_ParticulasC>)>(
  symbol: 'aurea_render_particulas_tamanho_do_lote',
  isLeaf: true,
)
external int _tamanhoDoLote(Pointer<_ParticulasC> p);

@Native<
  Uint32 Function(Pointer<_ParticulasC>, Double, Pointer<Float>, Uint32)
>(symbol: 'aurea_render_particulas_gerar', isLeaf: true)
external int _gerar(
  Pointer<_ParticulasC> p,
  double tempoS,
  Pointer<Float> saida,
  int capacidade,
);

@Native<
  Uint64 Function(Pointer<Uint8>, Uint32, Uint32, Pointer<Float>, Uint32, Float)
>(symbol: 'aurea_render_particulas_pintar', isLeaf: true)
external int _pintar(
  Pointer<Uint8> alvo,
  int largura,
  int altura,
  Pointer<Float> lote,
  int quantas,
  double opacidade,
);

@Native<Uint32 Function(Uint32, Uint32)>(
  symbol: 'aurea_render_particulas_teto',
  isLeaf: true,
)
external int _teto(int nivel, int pedido);

@Native<Uint32 Function()>(
  symbol: 'aurea_render_particulas_quantos_presets',
  isLeaf: true,
)
external int _quantosPresets();

@Native<Pointer<Utf8> Function(Uint32)>(
  symbol: 'aurea_render_particulas_preset_nome',
  isLeaf: true,
)
external Pointer<Utf8> _presetNome(int indice);

@Native<Int32 Function(Uint32, Pointer<_ParticulasC>)>(
  symbol: 'aurea_render_particulas_preset',
  isLeaf: true,
)
external int _presetSobre(int indice, Pointer<_ParticulasC> p);

@Native<Int32 Function(Uint32, Pointer<_ParticulasC>)>(
  symbol: 'aurea_render_particulas_preset_novo',
  isLeaf: true,
)
external int _presetNovo(int indice, Pointer<_ParticulasC> p);

// ------------------------------------------------------------- os tipos

/// DE ONDE AS PARTICULAS NASCEM. Os numeros sao contrato com o C++.
enum EmissorDeParticulas {
  caixa(0),
  ponto(1),
  esfera(2),
  linha(3),
  anel(4);

  const EmissorDeParticulas(this.valorNoNucleo);
  final int valorNoNucleo;
}

enum FormaDaParticula {
  esfera(0),
  estrela(1),
  risco(2),
  nuvem(3),
  quadrado(4),
  anel(5);

  const FormaDaParticula(this.valorNoNucleo);
  final int valorNoNucleo;
}

enum ModoDeEmissao {
  cone(0),
  esfera(1),
  radial(2);

  const ModoDeEmissao(this.valorNoNucleo);
  final int valorNoNucleo;
}

enum TamanhoNaVida {
  fixo(0),
  cresce(1),
  encolhe(2),
  sobeEDesce(3);

  const TamanhoNaVida(this.valorNoNucleo);
  final int valorNoNucleo;
}

enum OpacidadeNaVida {
  entraESai(0),
  some(1),
  aparece(2),
  fixa(3);

  const OpacidadeNaVida(this.valorNoNucleo);
  final int valorNoNucleo;
}

/// OS PARAMETROS DA NUVEM, na lingua do motor.
///
/// [corInicio] e [corFim] sao 0xRRGGBBAA — a mesma ordem que o C++ le. A
/// cor final so entra quando [temCorFim] esta ligado: interpolar quando as
/// duas cores sao iguais e trabalho por zero de imagem.
class ParametrosDeParticulas {
  ParametrosDeParticulas({
    this.emissor = EmissorDeParticulas.caixa,
    this.centroX = 0,
    this.centroY = 0,
    this.centroZ = 0,
    this.largura = 980,
    this.altura = 980,
    this.profundidade = 1400,
    this.raio = 490,
    this.linhaX = 1,
    this.linhaY = 0,
    this.linhaZ = 0,
    this.taxaDeNascimento = 0,
    this.vidaS = 4,
    this.vidaVariacao = 0,
    this.maximo = 512,
    this.semente = 7,
    this.velocidade = 40,
    this.direcaoGraus = -90,
    this.aberturaGraus = 360,
    this.modoDeEmissao = ModoDeEmissao.cone,
    this.gravidade = 0,
    this.ventoX = 0,
    this.ventoY = 0,
    this.ventoZ = 0,
    this.arrasto = 0,
    this.turbulencia = 0,
    this.turbulenciaEscala = 300,
    this.turbulenciaVelocidade = 1,
    this.atracao = 0,
    this.atracaoX = 0,
    this.atracaoY = 0,
    this.atracaoZ = 0,
    this.tamanho = 26,
    this.tamanhoVariacao = 0.5,
    this.tamanhoNaVida = TamanhoNaVida.fixo,
    this.opacidade = 1,
    this.opacidadeVariacao = 0,
    this.opacidadeNaVida = OpacidadeNaVida.entraESai,
    this.corInicio = 0xFFFF3B52,
    this.corFim = 0xFFFF3B52,
    this.temCorFim = false,
    this.forma = FormaDaParticula.estrela,
    this.giroGrausS = 0,
    this.brilho = 0.25,
    this.cintilar = true,
    this.rastro = 0,
    this.faiscas = 0,
    this.faiscaVidaS = 0.7,
    this.faiscaHeranca = 0.35,
    this.faiscaVelocidade = 60,
    this.faiscaTamanho = 0.45,
    this.faiscaInicio = 0,
    this.focal = 1200,
    this.rotacaoXGraus = 0,
    this.rotacaoYGraus = 0,
    this.rotacaoZGraus = 0,
  });

  EmissorDeParticulas emissor;
  double centroX, centroY, centroZ;
  double largura, altura, profundidade, raio;
  double linhaX, linhaY, linhaZ;
  double taxaDeNascimento;
  double vidaS;
  double vidaVariacao;
  int maximo;
  int semente;
  double velocidade;
  double direcaoGraus;
  double aberturaGraus;
  ModoDeEmissao modoDeEmissao;
  double gravidade;
  double ventoX, ventoY, ventoZ;
  double arrasto;
  double turbulencia;
  double turbulenciaEscala;
  double turbulenciaVelocidade;
  double atracao;
  double atracaoX, atracaoY, atracaoZ;
  double tamanho;
  double tamanhoVariacao;
  TamanhoNaVida tamanhoNaVida;
  double opacidade;
  double opacidadeVariacao;
  OpacidadeNaVida opacidadeNaVida;
  int corInicio;
  int corFim;
  bool temCorFim;
  FormaDaParticula forma;
  double giroGrausS;
  double brilho;
  bool cintilar;
  double rastro;
  int faiscas;
  double faiscaVidaS;
  double faiscaHeranca;
  double faiscaVelocidade;
  double faiscaTamanho;
  double faiscaInicio;
  double focal;
  double rotacaoXGraus, rotacaoYGraus, rotacaoZGraus;

  ParametrosDeParticulas clonar() => ParametrosDeParticulas()
    ..emissor = emissor
    ..centroX = centroX
    ..centroY = centroY
    ..centroZ = centroZ
    ..largura = largura
    ..altura = altura
    ..profundidade = profundidade
    ..raio = raio
    ..linhaX = linhaX
    ..linhaY = linhaY
    ..linhaZ = linhaZ
    ..taxaDeNascimento = taxaDeNascimento
    ..vidaS = vidaS
    ..vidaVariacao = vidaVariacao
    ..maximo = maximo
    ..semente = semente
    ..velocidade = velocidade
    ..direcaoGraus = direcaoGraus
    ..aberturaGraus = aberturaGraus
    ..modoDeEmissao = modoDeEmissao
    ..gravidade = gravidade
    ..ventoX = ventoX
    ..ventoY = ventoY
    ..ventoZ = ventoZ
    ..arrasto = arrasto
    ..turbulencia = turbulencia
    ..turbulenciaEscala = turbulenciaEscala
    ..turbulenciaVelocidade = turbulenciaVelocidade
    ..atracao = atracao
    ..atracaoX = atracaoX
    ..atracaoY = atracaoY
    ..atracaoZ = atracaoZ
    ..tamanho = tamanho
    ..tamanhoVariacao = tamanhoVariacao
    ..tamanhoNaVida = tamanhoNaVida
    ..opacidade = opacidade
    ..opacidadeVariacao = opacidadeVariacao
    ..opacidadeNaVida = opacidadeNaVida
    ..corInicio = corInicio
    ..corFim = corFim
    ..temCorFim = temCorFim
    ..forma = forma
    ..giroGrausS = giroGrausS
    ..brilho = brilho
    ..cintilar = cintilar
    ..rastro = rastro
    ..faiscas = faiscas
    ..faiscaVidaS = faiscaVidaS
    ..faiscaHeranca = faiscaHeranca
    ..faiscaVelocidade = faiscaVelocidade
    ..faiscaTamanho = faiscaTamanho
    ..faiscaInicio = faiscaInicio
    ..focal = focal
    ..rotacaoXGraus = rotacaoXGraus
    ..rotacaoYGraus = rotacaoYGraus
    ..rotacaoZGraus = rotacaoZGraus;
}

/// ==================== O LOTE ====================
///
/// UM BUFFER DE FLOATS, RESERVADO UMA VEZ.
///
/// O TAMANHO E O PIOR CASO daqueles parametros — o C++ responde quantas
/// instancias cabem considerando particulas, rastros e faiscas. Reservar
/// pelo pior caso e o que evita realocar quando a qualidade sobe.
///
/// OS CAMPOS DE CADA INSTANCIA, na ordem do C++ (contrato de ABI):
///   x, y, tamanho, angulo, r, g, b, a, profundidade, forma,
///   caudaX, caudaY, u, brilho, variacao
class LoteDeParticulas {
  /// O TAMANHO E O DA INSTANCIA INTEIRA, e nao o de um float.
  ///
  /// O C++ escreve `capacidade` instancias de [flutuantesPorInstancia]
  /// floats cada — reservar so `capacidade` floats e a forma mais direta
  /// de transbordar um buffer que existe. O buffer e de FLOATS (e nao um
  /// `Struct` do Dart) porque e ele que atravessa para o `Pointer<Float>`
  /// do C++ sem conversao.
  LoteDeParticulas(this.parametros)
    : _memoria = calloc<Float>(
        _tamanhoDoLoteSeguro(parametros) * flutuantesPorInstancia,
      ) {
    _capacidade = _tamanhoDoLoteSeguro(parametros);
    _bytes = _memoria.cast<Uint8>().asTypedList(
      _capacidade * flutuantesPorInstancia * 4,
    );
    _floats = _bytes.buffer.asFloat32List(
      0,
      _capacidade * flutuantesPorInstancia,
    );
  }

  static int _tamanhoDoLoteSeguro(ParametrosDeParticulas p) {
    final ptr = _paraC(p);
    try {
      final n = _tamanhoDoLote(ptr);
      return n < 0 ? 0 : n;
    } finally {
      calloc.free(ptr);
    }
  }

  /// Quantos floats tem uma instancia. Contrato com o C++.
  static const int flutuantesPorInstancia = 15;

  final ParametrosDeParticulas parametros;
  final Pointer<Float> _memoria;
  late final Uint8List _bytes;
  late final Float32List _floats;
  late final int _capacidade;
  int _quantas = 0;

  /// Quantas instancias o ultimo [gerar] escreveu.
  int get quantas => _quantas;

  /// Quantas cabem. Zero significa que a biblioteca nao respondeu.
  int get capacidade => _capacidade;

  /// O ENDERECO NATIVO DO BUFFER.
  ///
  /// E a prova direta de que o lote NAO realocou entre quadros — o
  /// `identical` do `Float32List` nao serve: a lista e uma view sobre
  /// memoria externa, e o `ByteBuffer` dela e um objeto novo a cada
  /// leitura. O endereco, nao.
  int get endereco => _memoria.address;

  /// Os floats crus: `_quantas * flutuantesPorInstancia` deles.
  Float32List get floats =>
      Float32List.view(_floats.buffer, 0, _quantas * flutuantesPorInstancia);

  /// LE UM CAMPO DE UMA INSTANCIA, pelo nome da coluna.
  double campo(int indice, int coluna) =>
      _floats[indice * flutuantesPorInstancia + coluna];

  /// SIMULA NO TEMPO [tempoS] E PREENCHE O LOTE. Devolve quantas
  /// instancias ficaram prontas.
  int gerar(double tempoS) {
    if (_capacidade == 0 || _memoria == nullptr) return 0;
    final ptr = _paraC(parametros);
    try {
      final n = _gerar(ptr, tempoS, _memoria, _capacidade);
      _quantas = n < 0 ? 0 : (n > _capacidade ? _capacidade : n);
      return _quantas;
    } finally {
      calloc.free(ptr);
    }
  }

  /// PINTA ESTE LOTE NUM BUFFER RGBA8 PREMULTIPLICADO DO TAMANHO DA
  /// COMPOSICAO. E o caminho de referencia (exportacao e teste); o preview
  /// desenha o lote direto, sem passar por pixel.
  int pintar(Uint8List alvo, int largura, int altura, {double opacidade = 1}) {
    if (_quantas == 0 || _memoria == nullptr) return 0;
    final p = calloc<Uint8>(alvo.length);
    if (p == nullptr) return 0;
    try {
      p.asTypedList(alvo.length).setAll(0, alvo);
      final tocados = _pintar(p, largura, altura, _memoria, _quantas, opacidade);
      alvo.setAll(0, p.asTypedList(alvo.length));
      return tocados;
    } finally {
      calloc.free(p);
    }
  }

  void liberar() {
    if (_memoria != nullptr) calloc.free(_memoria);
  }

  static Pointer<_ParticulasC> _paraC(ParametrosDeParticulas p) {
    final ptr = calloc<_ParticulasC>();
    final c = ptr.ref;
    c
      ..emissor = p.emissor.valorNoNucleo
      ..centroX = p.centroX
      ..centroY = p.centroY
      ..centroZ = p.centroZ
      ..largura = p.largura
      ..altura = p.altura
      ..profundidade = p.profundidade
      ..raio = p.raio
      ..linhaX = p.linhaX
      ..linhaY = p.linhaY
      ..linhaZ = p.linhaZ
      ..taxaDeNascimento = p.taxaDeNascimento
      ..vidaS = p.vidaS
      ..vidaVariacao = p.vidaVariacao
      ..maximo = p.maximo
      ..semente = p.semente
      ..velocidade = p.velocidade
      ..direcaoGraus = p.direcaoGraus
      ..aberturaGraus = p.aberturaGraus
      ..modoDeEmissao = p.modoDeEmissao.valorNoNucleo
      ..gravidade = p.gravidade
      ..ventoX = p.ventoX
      ..ventoY = p.ventoY
      ..ventoZ = p.ventoZ
      ..arrasto = p.arrasto
      ..turbulencia = p.turbulencia
      ..turbulenciaEscala = p.turbulenciaEscala
      ..turbulenciaVelocidade = p.turbulenciaVelocidade
      ..atracao = p.atracao
      ..atracaoX = p.atracaoX
      ..atracaoY = p.atracaoY
      ..atracaoZ = p.atracaoZ
      ..tamanho = p.tamanho
      ..tamanhoVariacao = p.tamanhoVariacao
      ..tamanhoNaVida = p.tamanhoNaVida.valorNoNucleo
      ..opacidade = p.opacidade
      ..opacidadeVariacao = p.opacidadeVariacao
      ..opacidadeNaVida = p.opacidadeNaVida.valorNoNucleo
      ..corInicio = p.corInicio
      ..corFim = p.corFim
      ..temCorFim = p.temCorFim ? 1 : 0
      ..forma = p.forma.valorNoNucleo
      ..giroGrausS = p.giroGrausS
      ..brilho = p.brilho
      ..cintilar = p.cintilar ? 1 : 0
      ..rastro = p.rastro
      ..faiscas = p.faiscas
      ..faiscaVidaS = p.faiscaVidaS
      ..faiscaHeranca = p.faiscaHeranca
      ..faiscaVelocidade = p.faiscaVelocidade
      ..faiscaTamanho = p.faiscaTamanho
      ..faiscaInicio = p.faiscaInicio
      ..focal = p.focal
      ..rotacaoXGraus = p.rotacaoXGraus
      ..rotacaoYGraus = p.rotacaoYGraus
      ..rotacaoZGraus = p.rotacaoZGraus;
    return ptr;
  }
}

/// ==================== O MOTOR ====================
abstract final class MotorDeParticulasRender {
  /// A BIBLIOTECA CARREGOU E A ABI BATE.
  ///
  /// O TAMANHO DAS DUAS STRUCTS E CONFERIDO ANTES DE QUALQUER USO: um
  /// campo novo de um lado so deslocaria todos os seguintes, e o sintoma
  /// seria "a nuvem nao obedece" — nao um erro. Melhor nao usar.
  static bool get disponivel {
    try {
      if (_tamanhoParametros() != sizeOf<_ParticulasC>()) return false;
      if (_tamanhoInstancia() != LoteDeParticulas.flutuantesPorInstancia * 4) {
        return false;
      }
      return _quantosPresets() > 0;
    } catch (_) {
      return false;
    }
  }

  /// QUANTAS INSTANCIAS CABEM nestes parametros — o tamanho do lote.
  static int tamanhoDoLote(ParametrosDeParticulas p) {
    final ptr = LoteDeParticulas._paraC(p);
    try {
      return _tamanhoDoLote(ptr);
    } finally {
      calloc.free(ptr);
    }
  }

  /// O TETO DE PARTICULAS do nivel de qualidade. E um TETO: um campo
  /// pequeno nao cresce por causa dele.
  static int teto(int nivel, int pedido) {
    try {
      return _teto(nivel, pedido);
    } catch (_) {
      return pedido;
    }
  }

  /// OS NOMES DOS PRESETS, na ordem que o C++ responde.
  static List<String> get nomesDosPresets {
    try {
      return [
        for (var i = 0; i < _quantosPresets(); i++)
          _presetNome(i).toDartString(),
      ];
    } catch (_) {
      return const [];
    }
  }

  /// OS PARAMETROS DE UM PRESET, do zero.
  static ParametrosDeParticulas? preset(int indice) {
    final ptr = calloc<_ParticulasC>();
    try {
      if (_presetNovo(indice, ptr) != 0) return null;
      return _deC(ptr.ref);
    } finally {
      calloc.free(ptr);
    }
  }

  /// APLICA UM PRESET SOBRE O QUE JA EXISTE — o centro, a lente, as
  /// rotacoes e a semente de quem chamou ficam.
  static ParametrosDeParticulas? aplicarPreset(
    int indice,
    ParametrosDeParticulas atual,
  ) {
    final ptr = LoteDeParticulas._paraC(atual);
    try {
      if (_presetSobre(indice, ptr) != 0) return null;
      return _deC(ptr.ref);
    } finally {
      calloc.free(ptr);
    }
  }

  static ParametrosDeParticulas _deC(_ParticulasC c) =>
      ParametrosDeParticulas()
        ..emissor = EmissorDeParticulas.values[c.emissor]
        ..centroX = c.centroX
        ..centroY = c.centroY
        ..centroZ = c.centroZ
        ..largura = c.largura
        ..altura = c.altura
        ..profundidade = c.profundidade
        ..raio = c.raio
        ..linhaX = c.linhaX
        ..linhaY = c.linhaY
        ..linhaZ = c.linhaZ
        ..taxaDeNascimento = c.taxaDeNascimento
        ..vidaS = c.vidaS
        ..vidaVariacao = c.vidaVariacao
        ..maximo = c.maximo
        ..semente = c.semente
        ..velocidade = c.velocidade
        ..direcaoGraus = c.direcaoGraus
        ..aberturaGraus = c.aberturaGraus
        ..modoDeEmissao = ModoDeEmissao.values[c.modoDeEmissao]
        ..gravidade = c.gravidade
        ..ventoX = c.ventoX
        ..ventoY = c.ventoY
        ..ventoZ = c.ventoZ
        ..arrasto = c.arrasto
        ..turbulencia = c.turbulencia
        ..turbulenciaEscala = c.turbulenciaEscala
        ..turbulenciaVelocidade = c.turbulenciaVelocidade
        ..atracao = c.atracao
        ..atracaoX = c.atracaoX
        ..atracaoY = c.atracaoY
        ..atracaoZ = c.atracaoZ
        ..tamanho = c.tamanho
        ..tamanhoVariacao = c.tamanhoVariacao
        ..tamanhoNaVida = TamanhoNaVida.values[c.tamanhoNaVida]
        ..opacidade = c.opacidade
        ..opacidadeVariacao = c.opacidadeVariacao
        ..opacidadeNaVida = OpacidadeNaVida.values[c.opacidadeNaVida]
        ..corInicio = c.corInicio
        ..corFim = c.corFim
        ..temCorFim = c.temCorFim != 0
        ..forma = FormaDaParticula.values[c.forma]
        ..giroGrausS = c.giroGrausS
        ..brilho = c.brilho
        ..cintilar = c.cintilar != 0
        ..rastro = c.rastro
        ..faiscas = c.faiscas
        ..faiscaVidaS = c.faiscaVidaS
        ..faiscaHeranca = c.faiscaHeranca
        ..faiscaVelocidade = c.faiscaVelocidade
        ..faiscaTamanho = c.faiscaTamanho
        ..faiscaInicio = c.faiscaInicio
        ..focal = c.focal
        ..rotacaoXGraus = c.rotacaoXGraus
        ..rotacaoYGraus = c.rotacaoYGraus
        ..rotacaoZGraus = c.rotacaoZGraus;
}
