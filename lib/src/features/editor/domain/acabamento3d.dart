import 'dart:math' as math;
import 'dart:ui';

import 'element3d.dart';
import 'modelo_do_texto3d.dart';

/// O ACABAMENTO DE UM OBJETO 3D — O MESMO SISTEMA DE MATERIAIS DO TEXTO 3D.
///
/// O QUE ISTO SUBSTITUI: o elemento 3D nativo tinha um `material` inteiro
/// (0 solido, 1 brilhante, 2 vidro, 3 metal, 4 fosco) e um degrade de tres
/// cores. Nenhum desses cinco ERA um material: "metal" nao dizia quanto de
/// metal nem quao rugoso, "brilhante" era um degrade iridescente que nao
/// respondia a luz nenhuma, e "vidro" era uma transparencia. O resultado e
/// que um cubo de "metal" e uma esfera de "metal" saiam com o mesmo
/// aspecto chapado, e nenhum dos dois lembrava o texto 3D — que ao lado
/// deles parecia de outro aplicativo.
///
/// O QUE ENTRA: os METAIS DO TEXTO 3D, os mesmos objetos
/// (`materiaisDoTexto3D`), com metalico e rugosidade de verdade, sombreados
/// pelo MESMO mapa de ambiente (`environmentColor`) que o elemento ja
/// usava. Um cubo de cromo e uma letra de cromo agora saem do mesmo
/// material, do mesmo ambiente e da mesma conta — que e o pedido:
/// aparencia consistente entre o texto 3D e os modelos.
enum AcabamentoDoElemento3D {
  /// A COR DA CAMADA, SEM METAL. E o comportamento de sempre: um projeto
  /// salvo antes disto abrir exatamente igual.
  corLisa,

  ouro,
  cromo,
  acoEscovado,
  brancoFosco,
}

String nomeDoAcabamento(AcabamentoDoElemento3D a) => switch (a) {
  AcabamentoDoElemento3D.corLisa => 'Cor lisa',
  AcabamentoDoElemento3D.ouro => 'Ouro',
  AcabamentoDoElemento3D.cromo => 'Cromo',
  AcabamentoDoElemento3D.acoEscovado => 'Aço escovado',
  AcabamentoDoElemento3D.brancoFosco => 'Branco fosco',
};

/// O ACABAMENTO A PARTIR DO NOME GRAVADO, com o de sempre como resposta
/// para um nome desconhecido: um projeto de uma versao mais nova abre com
/// o acabamento neutro em vez de nao abrir.
AcabamentoDoElemento3D acabamentoPorNome(String? nome) {
  if (nome == null) return AcabamentoDoElemento3D.corLisa;
  for (final a in AcabamentoDoElemento3D.values) {
    if (nomeDoAcabamento(a) == nome ||
        a.name == nome ||
        nomeDoAcabamentoArquivo(a) == nome) {
      return a;
    }
  }
  return AcabamentoDoElemento3D.corLisa;
}

/// O NOME NO ARQUIVO — em ASCII, sem acento e sem espaco. O rotulo da
/// tela pode mudar de texto amanha; este nao.
String nomeDoAcabamentoArquivo(AcabamentoDoElemento3D a) => a.name;

/// O ESTILO DO TEXTO 3D por tras de cada acabamento. Nulo em [corLisa],
/// que nao tem metal nenhum.
EstiloDoTexto3D? estiloDoAcabamento(AcabamentoDoElemento3D a) => switch (a) {
  AcabamentoDoElemento3D.corLisa => null,
  AcabamentoDoElemento3D.ouro => EstiloDoTexto3D.ouro,
  AcabamentoDoElemento3D.cromo => EstiloDoTexto3D.cromo,
  AcabamentoDoElemento3D.acoEscovado => EstiloDoTexto3D.acoEscovado,
  AcabamentoDoElemento3D.brancoFosco => EstiloDoTexto3D.brancoFosco,
};

/// O MATERIAL: as tres grandezas que o sombreamento precisa.
///
/// [rugosidade] e a unica que muda o DESENHO do brilho — metalico diz
/// quanto da cor vem do reflexo, e a rugosidade diz o quao apertado ele e.
class MaterialDoElemento3D {
  const MaterialDoElemento3D({
    required this.cor,
    required this.metalico,
    required this.rugosidade,
    this.sombreamentoLegado = false,
  });

  final Color cor;
  final double metalico;
  final double rugosidade;

  /// O SOMBREAMENTO DE SEMPRE — o unico caminho que a cor lisa usa.
  ///
  /// Ele nao e o mesmo calculo dos metais, e nao por descuido: o pintor
  /// antigo sombreava com `0.34 + 0.66 * |dot|`, o MODULO do produto
  /// escalar. Ou seja, uma face de costas para a luz saia tao clara
  /// quanto uma de frente — e um projeto salvo antes disto conta com
  /// isso. Trocar por `max(0, dot)` mudaria a cara de todo solido ja
  /// existente, e "abrir identico" era a promessa.
  final bool sombreamentoLegado;

  /// O EXPOENTE ESPECULAR DO BRINH-PHONG a partir da rugosidade.
  ///
  /// E a conversao classica (`m = 2/rug^4 - 2`): rugosidade zero da um
  /// ponto de luz quase puntiforme, rugosidade 1 espalha por toda a face.
  /// Ela existe para o brilho do cromo ser do cromo e o do aço escovado
  /// ser do aço — sem ela, "rugosidade" seria so um numero guardado.
  double get expoenteEspecular {
    final r = rugosidade.clamp(0.03, 1.0);
    return (2.0 / math.pow(r, 4) - 2.0).clamp(2.0, 4096.0).toDouble();
  }
}

/// O MATERIAL DE UM ACABAMENTO, sobre a cor da camada.
///
/// [corLisa] devolve a propria cor da camada com metalico zero — o
/// comportamento anterior, intacto. Os outros vem da FRENTE do material
/// do texto 3D: e a parte que da a cor do objeto, e as outras duas
/// (chanfro e lateral) existem para a letra ter quinas, coisa que um
/// solido fechado ja tem por geometria.
MaterialDoElemento3D materialDoElemento3D(
  AcabamentoDoElemento3D acabamento,
  Color corDaCamada,
) {
  final estilo = estiloDoAcabamento(acabamento);
  if (estilo == null) {
    return MaterialDoElemento3D(
      cor: corDaCamada,
      metalico: 0,
      rugosidade: 0.85,
      sombreamentoLegado: true,
    );
  }
  final m = materiaisDoTexto3D(estilo).first;
  final c = (m['color'] as List).cast<num>();
  return MaterialDoElemento3D(
    cor: Color.from(
      alpha: 1,
      red: c[0].toDouble(),
      green: c[1].toDouble(),
      blue: c[2].toDouble(),
    ),
    metalico: (m['metallic'] as num).toDouble(),
    rugosidade: (m['roughness'] as num).toDouble(),
  );
}

/// A LUZ DA CENA. Uma so, fixa, vinda de cima/esquerda/frente — a mesma
/// que o elemento 3D sempre teve. Nao e um defeito ser fixa: com duas
/// luzes animadas o custo por face dobra num pintor de CPU, e o que a
/// pessoa procura num solido e ler a FORMA, e nao a hora do dia.
const double luzX = -0.37;
const double luzY = -0.55;
const double luzZ = -0.75;

/// A MESMA LUZ, num record — para quem quer passar as tres de uma vez.
const (double, double, double) luzDoElemento3D = (luzX, luzY, luzZ);

/// A COR DE UMA FACE, com o material, a luz e o ambiente.
///
/// A CONTA, na ordem em que ela acontece:
///   1. DIFUSA — o quanto a face olha para a luz. Metal nao tem difusa
///      (a luz nao entra nele, ela quica), entao ela pesa `1 - metalico`;
///   2. ESPECULAR — o ponto de luz. A direcao do meio entre a luz e a
///      vista, elevada ao expoente da rugosidade; metal tinge o ponto com
///      a propria cor, o que e o que separa ouro de plastico dourado;
///   3. AMBIENTE — a direcao espelhada da vista, lida no mapa. E daqui
///      que vem a cor do metal polido, e por isso esta etapa nao depende
///      da luz: um cromo numa sala escura continua cromo.
///
/// [nx], [ny], [nz] sao a normal de MUNDO e [ambiente] e o mapa que o
/// elemento ja usava — o mesmo do texto 3D e dos solidos.
Color corDaFaceDoElemento3D({
  required MaterialDoElemento3D material,
  required EnvironmentKind ambiente,
  required double nx,
  required double ny,
  required double nz,
}) {
  final bruto = _normalizar(nx, ny, nz);
  if (bruto == null) return material.cor;
  // A FACE VIRADA PARA A CAMERA E A QUE CONTA. O pintor desenha os dois
  // lados (nao ha descarte de costas), e uma normal que aponta para dentro
  // daria um sombreamento invisivel.
  final sinal = bruto.$3 < 0 ? 1.0 : -1.0;
  final ux = bruto.$1 * sinal;
  final uy = bruto.$2 * sinal;
  final uz = bruto.$3 * sinal;

  final lambert = ux * -luzX + uy * -luzY + uz * -luzZ;

  // O CAMINHO LEGADO: a cor da camada, e so ela.
  if (material.sombreamentoLegado) {
    final shade = 0.34 + 0.66 * lambert.abs();
    final cc = material.cor;
    return Color.from(
      alpha: cc.a,
      red: (cc.r * shade).clamp(0.0, 1.0),
      green: (cc.g * shade).clamp(0.0, 1.0),
      blue: (cc.b * shade).clamp(0.0, 1.0),
    );
  }

  // 1. DIFUSA. O piso de ambiente evita que a face de costas para a luz
  // fique preta: no mundo real ela recebe o que o chao devolve.
  final difusa =
      (0.30 + 0.70 * math.max(0.0, lambert)) * (1 - material.metalico);

  // 2. ESPECULAR. A vista esta em -Z (a camera olha a cena); a direcao do
  // meio entre a luz e a vista e o que define o ponto de brilho.
  const vx = 0.0, vy = 0.0, vz = -1.0;
  final metade = _normalizar(luzX + vx, luzY + vy, luzZ + vz);
  var especular = 0.0;
  if (metade != null) {
    final nh = math.max(
      0.0,
      ux * metade.$1 + uy * metade.$2 + uz * metade.$3,
    );
    especular = math.pow(nh, material.expoenteEspecular).toDouble();
  }
  // O PESO DO ESPECULAR nao cai com a rugosidade: um metal fosco tem um
  // ponto de luz LARGO e fraco, e nao nenhum. Multiplicar pelo inverso da
  // rugosidade apagava o brilho do branco fosco — que e justamente o que
  // faz ele parecer fosco e nao papel.
  final pesoEspecular = 0.35 + 0.65 * material.metalico;

  // 3. AMBIENTE. A direcao espelhada da vista pela normal:
  //    R = 2*(n.V)*n - V,  com V = (0,0,-1).
  //
  // O MAPA TEM Y PARA CIMA e a malha tem Y para baixo — dai o `-uy` na
  // componente que entra no `environmentColor`, que e o mesmo cuidado que
  // o elemento 3D ja tomava.
  final nv = -uz;
  final (er, eg, eb) = environmentColor(
    ambiente,
    2 * nv * ux,
    -(2 * nv * uy),
    2 * nv * uz + 1,
    sunX: luzX,
    sunY: -luzY,
    sunZ: luzZ,
    // A RUGOSIDADE ESPALHA O SOL: superficie lisa concentra o reflexo da
    // luz num ponto; rugosa o abre numa faixa. E o unico lugar onde a
    // rugosidade muda o desenho do ambiente.
    sunSharp: 120 * (1 - material.rugosidade).clamp(0.05, 1.0) + 3,
    sunGain: 0.9,
  );
  // FRESNEL: a borda reflete mais que o meio, sempre. E o que da volume
  // a uma esfera lisa — sem ele, o meio e a borda ficam com a mesma cor
  // e o objeto parece um adesivo.
  final cosseno = nv.clamp(0.0, 1.0);
  final fresnel = (0.04 + 0.96 * math.pow(1 - cosseno, 5).toDouble())
      .clamp(0.0, 1.0);
  // METAL POLIDO E QUASE SO ESPELHO; METAL RUGOSO REFLETE ESPALHADO E
  // FRACO. O peso abaixo e o `1 - rugosidade` com um piso, para uma
  // superficie muito rugosa ainda receber a cor do ambiente em vez de
  // virar um preto fosco.
  final pesoAmbiente =
      material.metalico *
          (0.35 + 0.65 * (1 - material.rugosidade)) *
          (0.30 + 0.70 * fresnel) +
      // UM DIELETRICO TAMBEM REFLETE, so que pouco: 4% de Fresnel.
      0.06 * fresnel * (1 - material.metalico);

  // A COR DO ESPECULAR: metal tinge o proprio brilho, dielétrico devolve
  // branco. E o que separa ouro de plastico dourado — os dois tem a mesma
  // cor difusa, e so um tem o ponto de luz dourado.
  final cc = material.cor;
  double tingir(double canal) =>
      material.metalico * canal + (1 - material.metalico);

  return Color.from(
    alpha: cc.a,
    red: (cc.r * difusa + er * pesoAmbiente + especular * pesoEspecular * tingir(cc.r))
        .clamp(0.0, 1.0),
    green: (cc.g * difusa + eg * pesoAmbiente + especular * pesoEspecular * tingir(cc.g))
        .clamp(0.0, 1.0),
    blue: (cc.b * difusa + eb * pesoAmbiente + especular * pesoEspecular * tingir(cc.b))
        .clamp(0.0, 1.0),
  );
}

(double, double, double)? _normalizar(double x, double y, double z) {
  final l = math.sqrt(x * x + y * y + z * z);
  if (l < 1e-9) return null;
  return (x / l, y / l, z / l);
}
