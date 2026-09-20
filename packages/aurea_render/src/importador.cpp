// O IMPORTADOR: DO ARQUIVO PARA O `Modelo` DO AUREA.
//
// ---------------------------------------------------------------
// A DECISAO QUE MANDA EM TUDO AQUI: EM QUE ESPACO OS VERTICES FICAM.
// ---------------------------------------------------------------
//
// Um vertice de modelo esqueletico nao esta no espaco do mundo: ele esta
// no espaco em que a malha foi MODELADA. A matriz de um osso so leva o
// vertice ao lugar certo se multiplicada pela ligacao inversa daquele osso
// (`globalDoOsso * ligacaoInversa`), e essa conta so fecha quando o
// vertice, a ligacao e o osso falam do mesmo espaco de repouso.
//
// No glTF isso e explicito: a transformacao do no onde a malha pendura e
// IGNORADA pela especificacao quando a malha tem `skin`. Nos outros
// formatos nem sempre e. Entao a importacao faz uma coisa so, e faz uma
// vez: TIRA A TRANSFORMACAO DO NO DA MALHA DE DENTRO DOS VERTICES. Depois
// disso os vertices estao no espaco da ligacao em todos os formatos, e o
// renderizador usa a mesma conta sem precisar saber de onde o arquivo veio.
//
// O preco e uma passada a mais sobre os vertices na importacao. O ganho e
// nao ter um caso especial no caminho do quadro — onde errar apareceria
// como um modelo dobrado, e nao como um erro.
#include "importador.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

// O ASSIMP JA EMBUTE O `stb_image` E O DECODIFICADOR DE PNG/JPEG VEM
// DELE. Usar o `stb_image.h` cru daria SIMBOLO DUPLICADO (o Assimp compila
// a implementacao dentro dele, com os nomes prefixados); este cabecalho e
// o que aplica o mesmo prefixo `assimp_stbi_` nas nossas chamadas.
#include "Common/StbCommon.h"

namespace aurea::render::tresd {

namespace {

using Assimp::Importer;

/// AS BANDEIRAS DE IMPORTACAO, COM O PORQUE DE CADA UMA.
///
/// NAO ESTAO AQUI, E DE PROPOSITO:
///  - `aiProcess_PreTransformVertices` achata a arvore de nos na malha.
///    Perfeito para desenhar um objeto parado, e FATAL aqui: sem arvore
///    nao ha animacao de no nem esqueleto.
///  - `aiProcess_FlipUVs`: o glTF e a Vulkan ja usam a origem no canto
///    superior esquerdo, que e a que o `stb_image` produz. Virar aqui
///    viraria de volta no shader.
constexpr unsigned kProcessos =
    aiProcess_Triangulate |           // o renderizador desenha triangulo
    aiProcess_SortByPType |           // separa linha e ponto do solido
    aiProcess_CalcTangentSpace |      // o mapa de normais precisa
    aiProcess_GenSmoothNormals |      // so age quando o arquivo nao traz
    aiProcess_JoinIdenticalVertices | // menos vertice, menos memoria
    aiProcess_LimitBoneWeights |      // 4 ossos por vertice, que e o teto
    aiProcess_ValidateDataStructure | // arquivo corrompido para cedo
    aiProcess_RemoveRedundantMaterials;

[[nodiscard]] float para_float(std::uint8_t v) noexcept {
  return static_cast<float>(v) * (1.0F / 255.0F);
}

/// A COR DO ASSIMP VIRA A DO AUREA (ARGB, 8 BITS POR CANAL). Sem
/// prender: o Assimp pode devolver 1.2 num arquivo mal formado, e um
/// `uint8_t` estourado daria a volta e transformaria branco em preto.
[[nodiscard]] std::uint32_t cor_de_assimp(const aiColor4D& c) noexcept {
  const auto q = [](float v) {
    const float p = v < 0.0F ? 0.0F : (v > 1.0F ? 1.0F : v);
    return static_cast<std::uint32_t>(p * 255.0F + 0.5F);
  };
  return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

[[nodiscard]] Vec3 de_assimp(const aiVector3D& v) noexcept {
  return {v.x, v.y, v.z};
}

[[nodiscard]] Quat de_assimp(const aiQuaternion& q) noexcept {
  return {q.x, q.y, q.z, q.w};
}

[[nodiscard]] Mat4 de_assimp(const aiMatrix4x4& m) noexcept {
  // O ASSIMP GUARDA POR LINHA, e o nosso `Mat4` guarda por coluna. A
  // troca e literal — `a1` e a coluna 1 da linha 0 — e escrever errado
  // aqui da uma transposta silenciosa, que num esqueleto aparece como o
  // modelo explodido.
  Mat4 r;
  r.m[0] = m.a1; r.m[4] = m.a2; r.m[8]  = m.a3; r.m[12] = m.a4;
  r.m[1] = m.b1; r.m[5] = m.b2; r.m[9]  = m.b3; r.m[13] = m.b4;
  r.m[2] = m.c1; r.m[6] = m.c2; r.m[10] = m.c3; r.m[14] = m.c4;
  r.m[3] = m.d1; r.m[7] = m.d2; r.m[11] = m.d3; r.m[15] = m.d4;
  return r;
}

/// DO ASSIMP PARA O NOSSO TRS — E A MATRIZ SAI DAS MESMAS TRES PARCELAS.
///
/// Decompor duas vezes (uma para o TRS, outra para a matriz local) daria
/// duas respostas que podem nao bater: com escala negativa o Assimp
/// escolhe um dos dois quaternions equivalentes, e a matriz ficaria com um
/// giro que o TRS nao tem. Como a pose de repouso monta a matriz A PARTIR
/// do TRS, aqui as duas coisas nascem juntas e nunca divergem.
void preencher_trs(const aiMatrix4x4& m, No& n) {
  aiVector3D pos, esc;
  aiQuaternion rot;
  m.Decompose(esc, rot, pos);
  n.posicao = de_assimp(pos);
  n.rotacao = de_assimp(rot);
  n.escala = de_assimp(esc);
  n.local = Mat4::de_trs(n.posicao, n.rotacao, n.escala);
  n.tem_matriz = true;
}

/// A TEXTURA NEUTRA (§43).
///
/// UM CINZA MEDIO, E NAO O AZUL DE DEPURACAO. O azul denuncia o erro na
/// bancada e vai junto para o aparelho do dono quando ninguem repara —
/// aqui a textura que falhou simplesmente nao aparece, e o material segue
/// com a cor que o arquivo declara. O mapa de normais tem o seu proprio:
/// um plano apontando para +Z, que deixa a superficie lisa em vez de
/// tingida de cinza.
[[nodiscard]] Textura textura_neutra(bool para_normal, bool srgb) {
  Textura t;
  t.nome = para_normal ? "<normal-neutro>" : "<neutro>";
  t.largura = 2;
  t.altura = 2;
  t.srgb = srgb;
  t.tem_alfa = false;
  t.pixels.assign(2 * 2 * 4, 0);
  for (int i = 0; i < 4; ++i) {
    if (para_normal) {
      t.pixels[static_cast<std::size_t>(i) * 4 + 0] = 128;
      t.pixels[static_cast<std::size_t>(i) * 4 + 1] = 128;
      t.pixels[static_cast<std::size_t>(i) * 4 + 2] = 255;
    } else {
      t.pixels[static_cast<std::size_t>(i) * 4 + 0] = 255;
      t.pixels[static_cast<std::size_t>(i) * 4 + 1] = 255;
      t.pixels[static_cast<std::size_t>(i) * 4 + 2] = 255;
    }
    t.pixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
  }
  return t;
}

/// O CATALOGO DE TEXTURAS DO ARQUIVO.
///
/// UMA TEXTURA CARREGADA UMA VEZ. O mesmo arquivo pode ser o mapa de cor
/// de dez materiais; carregar dez vezes custaria dez vezes a memoria e dez
/// vezes o upload (§6, §20). A chave e o nome que o arquivo usa para
/// apontar para ela.
class CatalogoDeTexturas {
 public:
  CatalogoDeTexturas(const aiScene& cena, std::string pasta,
                     const OpcoesDeImportacao& opcoes, RelatoDaImportacao* relato)
      : cena_(cena), pasta_(std::move(pasta)), opcoes_(opcoes), relato_(relato) {}

  /// O INDICE DA TEXTURA, OU -1 quando nao ha. `srgb` diz se o conteudo e
  /// cor (sRGB) ou dado linear — e o MESMO arquivo pode ser pedido nos
  /// dois papeis, entao os dois papeis guardam entradas separadas.
  std::int32_t obter(const std::string& caminho, bool srgb, bool para_normal) {
    if (caminho.empty()) return -1;
    const std::string chave = (srgb ? "s|" : "l|") + caminho;
    const auto achado = por_chave_.find(chave);
    if (achado != por_chave_.end()) return achado->second;

    Textura t;
    bool ok = false;
    if (caminho[0] == '*') {
      ok = embutida(caminho, para_normal, t);
    } else {
      ok = do_disco(caminho, para_normal, t);
    }
    if (!ok) {
      if (!opcoes_.textura_neutra_quando_faltar) {
        por_chave_[chave] = -1;
        return -1;
      }
      if (relato_ != nullptr && relato_->avisos < 8) {
        if (relato_->avisos == 0) relato_->primeiro_aviso = "textura ausente: " + caminho;
        ++relato_->avisos;
      }
      t = textura_neutra(para_normal, srgb);
    }
    t.srgb = srgb;
    const auto indice = static_cast<std::int32_t>(texturas_.size());
    texturas_.push_back(std::move(t));
    por_chave_[chave] = indice;
    return indice;
  }

  [[nodiscard]] std::vector<Textura>&& levar() noexcept {
    return std::move(texturas_);
  }

 private:
  /// A TEXTURA QUE VEM DENTRO DO PROPRIO ARQUIVO (o caso do GLB). O
  /// Assimp marca isso com altura zero e guarda no lugar dela o TAMANHO
  /// DOS BYTES COMPRIMIDOS.
  bool embutida(const std::string& caminho, bool para_normal, Textura& saida) {
    const int indice = std::atoi(caminho.c_str() + 1);
    if (indice < 0 || static_cast<unsigned>(indice) >= cena_.mNumTextures) return false;
    const aiTexture* tex = cena_.mTextures[indice];
    if (tex == nullptr) return false;

    if (tex->mHeight == 0) {
      int l = 0, a = 0, canais = 0;
      stbi_uc* pixels = stbi_load_from_memory(
          reinterpret_cast<const stbi_uc*>(tex->pcData),
          static_cast<int>(tex->mWidth), &l, &a, &canais, 4);
      if (pixels == nullptr) return false;
      saida.largura = static_cast<std::uint32_t>(l);
      saida.altura = static_cast<std::uint32_t>(a);
      saida.pixels.assign(pixels, pixels + static_cast<std::size_t>(l) * a * 4);
      stbi_image_free(pixels);
      saida.tem_alfa = canais == 4 && tem_alfa_de_verdade(saida.pixels);
      return true;
    }
    // Textura crua: o Assimp ja a entrega em ARGB8.
    saida.largura = tex->mWidth;
    saida.altura = tex->mHeight;
    const std::size_t n = static_cast<std::size_t>(tex->mWidth) * tex->mHeight;
    saida.pixels.resize(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
      const aiTexel& p = tex->pcData[i];
      saida.pixels[i * 4 + 0] = p.r;
      saida.pixels[i * 4 + 1] = p.g;
      saida.pixels[i * 4 + 2] = p.b;
      saida.pixels[i * 4 + 3] = p.a;
    }
    (void)para_normal;
    saida.tem_alfa = tem_alfa_de_verdade(saida.pixels);
    return true;
  }

  /// A TEXTURA QUE MORA AO LADO DO ARQUIVO (o caso do GLTF com arquivos
  /// soltos, do OBJ com o `.mtl` e do FBX antigo).
  bool do_disco(const std::string& caminho, bool para_normal, Textura& saida) {
    (void)para_normal;
    std::string tentativa = caminho;
    // O arquivo pode citar o caminho com a barra do Windows ou com a do
    // Unix, e o modelo pode ter vindo de outro sistema.
    std::replace(tentativa.begin(), tentativa.end(), '\\', '/');
    const std::size_t barra = tentativa.find_last_of('/');
    const std::string so_o_nome =
        barra == std::string::npos ? tentativa : tentativa.substr(barra + 1);

    const std::string tentativas[3] = {pasta_ + "/" + tentativa,
                                       pasta_ + "/" + so_o_nome, tentativa};
    for (const std::string& caminho_real : tentativas) {
      int l = 0, a = 0, canais = 0;
      stbi_uc* pixels = stbi_load(caminho_real.c_str(), &l, &a, &canais, 4);
      if (pixels == nullptr) continue;
      saida.largura = static_cast<std::uint32_t>(l);
      saida.altura = static_cast<std::uint32_t>(a);
      saida.pixels.assign(pixels, pixels + static_cast<std::size_t>(l) * a * 4);
      stbi_image_free(pixels);
      saida.tem_alfa = canais == 4 && tem_alfa_de_verdade(saida.pixels);
      return true;
    }
    return false;
  }

  /// UM CANAL ALFA TODO EM 255 NAO E ALFA. Tratar assim evita uma
  /// passada de transparencia inteira num material opaco — que e o mesmo
  /// desenho, com a ordenacao errada.
  [[nodiscard]] static bool tem_alfa_de_verdade(
      const std::vector<std::uint8_t>& rgba) noexcept {
    for (std::size_t i = 3; i < rgba.size(); i += 4) {
      if (rgba[i] != 255) return true;
    }
    return false;
  }

  const aiScene& cena_;
  std::string pasta_;
  const OpcoesDeImportacao& opcoes_;
  RelatoDaImportacao* relato_;
  std::vector<Textura> texturas_;
  std::unordered_map<std::string, std::int32_t> por_chave_;
};

/// A COR DE UM CANAL DE MATERIAL. O glTF e os formatos antigos usam
/// chaves diferentes para a mesma coisa, e o Assimp traduz so uma parte:
/// procurar nas duas e o que faz um FBX antigo sair com a cor certa em vez
/// de branco.
[[nodiscard]] bool booleano_do_material(const aiMaterial& m, const char* chave,
                                        unsigned tipo, unsigned indice,
                                        bool padrao) {
  int v = 0;
  if (m.Get(chave, tipo, indice, v) == AI_SUCCESS) return v != 0;
  return padrao;
}

[[nodiscard]] bool real_do_material(const aiMaterial& m, const char* chave,
                                    unsigned tipo, unsigned indice, float& saida) {
  return m.Get(chave, tipo, indice, saida) == AI_SUCCESS;
}

/// UM REAL QUE TALVEZ NAO EXISTA. `real_do_material` engoliria a ausencia
/// no valor padrao, e aqui a diferenca entre "o arquivo disse zero" e "o
/// arquivo nao disse nada" decide o material inteiro. Sem `[[nodiscard]]`:
/// ler para um padrao que so muda quando a chave existe e um uso legitimo.
bool real_do_material(const aiMaterial& m, const char* chave, float& saida) {
  return m.Get(chave, 0, 0, saida) == AI_SUCCESS;
}

[[nodiscard]] std::string texto_do_material(const aiMaterial& m,
                                            const char* chave, unsigned tipo,
                                            unsigned indice) {
  aiString s;
  if (m.Get(chave, tipo, indice, s) == AI_SUCCESS) return s.C_Str();
  return {};
}

/// O CAMINHO DE UMA TEXTURA NO MATERIAL. O glTF nomeia os canais de um
/// jeito (BASE_COLOR, METALNESS, DIFFUSE_ROUGHNESS) e o resto do mundo
/// usa os nomes antigos (DIFFUSE, SPECULAR). Procurar nos dois e o que faz
/// um OBJ com `.mtl` texturizado aparecer texturizado.
[[nodiscard]] std::string caminho_da_textura(const aiMaterial& m,
                                             aiTextureType moderno,
                                             aiTextureType antigo) {
  aiString s;
  if (m.GetTexture(moderno, 0, &s) == AI_SUCCESS && s.length > 0) return s.C_Str();
  if (antigo != aiTextureType_NONE && m.GetTexture(antigo, 0, &s) == AI_SUCCESS &&
      s.length > 0) {
    return s.C_Str();
  }
  return {};
}

/// A ARVORE DE NOS VIRA UMA LISTA PLANA COM O INDICE DO PAI. A lista e a
/// mesma ordem em que o arquivo declara, e a raiz e o indice 0 — a pose
/// depende disso para achar de onde descer.
void achatar_nos(const aiNode* no, std::int32_t pai,
                 std::vector<No>& saida,
                 std::unordered_map<std::string, std::int32_t>& por_nome) {
  const auto indice = static_cast<std::int32_t>(saida.size());
  No n;
  n.nome = no->mName.C_Str();
  n.pai = pai;
  // A POSICAO, A ROTACAO E A ESCALA SAEM DA MATRIZ e ficam guardadas
  // separadas. Sao elas que a animacao sobrescreve: um clipe que anima
  // apenas a rotacao precisa da translacao do arquivo para nao colar o no
  // na origem.
  preencher_trs(no->mTransformation, n);
  saida.push_back(std::move(n));
  if (!por_nome.count(saida.back().nome)) por_nome[saida.back().nome] = indice;

  for (unsigned i = 0; i < no->mNumChildren; ++i) {
    achatar_nos(no->mChildren[i], indice, saida, por_nome);
  }
}

/// QUAIS NOS DESENHAM QUAL MALHA. O Assimp guarda isso no no
/// (`mMeshes[]`), e nao na malha. Sem esse mapa, uma malha usada por tres
/// nos seria desenhada uma vez so — o caso classico de um arquivo que
/// repete a mesma cadeira.
void mapear_nos_das_malhas(const aiNode* no,
                           std::int32_t& proximo,
                           std::unordered_map<unsigned, std::vector<std::int32_t>>& saida) {
  // O CONTADOR ANDA NA MESMA ORDEM DO `achatar_nos` (pre-ordem, comecando
  // em zero). Contar diferente daria um mapa deslocado — e o defeito seria
  // o modelo certo desenhado no lugar de outro.
  const std::int32_t indice = proximo++;
  for (unsigned i = 0; i < no->mNumMeshes; ++i) {
    saida[no->mMeshes[i]].push_back(indice);
  }
  for (unsigned i = 0; i < no->mNumChildren; ++i) {
    mapear_nos_das_malhas(no->mChildren[i], proximo, saida);
  }
}

/// A TRANSFORMACAO GLOBAL DE CADA NO, NA POSE DE REPOUSO. E o que a
/// importacao usa para tirar o no da malha de dentro dos vertices.
void globais_de_repouso(const std::vector<No>& nos, std::vector<Mat4>& saida) {
  saida.assign(nos.size(), Mat4::identidade());
  for (std::size_t i = 0; i < nos.size(); ++i) {
    const std::int32_t pai = nos[i].pai;
    saida[i] = pai >= 0 && static_cast<std::size_t>(pai) < i
                   ? saida[static_cast<std::size_t>(pai)] * nos[i].local
                   : nos[i].local;
  }
}

}  // namespace

// ------------------------------------------------------------------ corpo

namespace {

/// TUDO O QUE A IMPORTACAO PRECISA SABER, NUM LUGAR SO.
struct Contexto {
  const aiScene* cena = nullptr;
  const OpcoesDeImportacao* opcoes = nullptr;
  RelatoDaImportacao* relato = nullptr;
  std::unique_ptr<CatalogoDeTexturas> texturas;
  std::vector<Mat4> globais;
  std::unordered_map<std::string, std::int32_t> no_por_nome;
  std::unordered_map<std::string, std::int32_t> osso_por_no;
  std::unordered_map<unsigned, std::vector<std::int32_t>> nos_da_malha;
  Modelo modelo;
};

void converter_materiais(Contexto& ctx) {
  const aiScene& cena = *ctx.cena;
  ctx.modelo.materiais.reserve(cena.mNumMaterials);
  for (unsigned i = 0; i < cena.mNumMaterials; ++i) {
    const aiMaterial& am = *cena.mMaterials[i];
    Material m;
    aiString nome;
    if (am.Get(AI_MATKEY_NAME, nome) == AI_SUCCESS) m.nome = nome.C_Str();

    aiColor4D base(1.0F, 1.0F, 1.0F, 1.0F);
    // O glTF escreve o fator em BASE_COLOR; o resto do mundo em DIFFUSE.
    if (am.Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS) {
      am.Get(AI_MATKEY_COLOR_DIFFUSE, base);
    }
    m.cor_base = Cor::de_argb(cor_de_assimp(base));

    // ------------------------------------------------ metalico e rugosidade
    //
    // O PADRAO DO glTF E metalico=1, E ISSO E UMA ARMADILHA PARA OS OUTROS
    // FORMATOS. Um OBJ com `.mtl` ou um FBX antigo chegam aqui sem nenhum
    // campo de PBR; tratados como metal perfeito, viram um espelho fosco
    // que nao se parece em nada com o arquivo original. Entao o padrao
    // depende de o arquivo TER FALADO de PBR:
    //
    //  - falou: os numeros dele, e o que faltar cai no padrao do glTF;
    //  - nao falou: um dieletrico comum (metalico 0), com a rugosidade
    //    tirada do brilho especular quando o formato antigo o declara.
    float metalico = 0.0F;
    float rugosidade = 0.6F;
    bool tem_metalico = real_do_material(am, AI_MATKEY_METALLIC_FACTOR, metalico);
    const bool tem_rugosidade = real_do_material(am, AI_MATKEY_ROUGHNESS_FACTOR, rugosidade);
    if (tem_metalico || tem_rugosidade) {
      // O material e PBR: quem nao veio usa o padrao do glTF.
      if (!tem_metalico) metalico = 1.0F;
      if (!tem_rugosidade) rugosidade = 1.0F;
    } else {
      float brilho = 0.0F;
      if (real_do_material(am, AI_MATKEY_SHININESS, brilho) && brilho > 0.0F) {
        // O EXPOENTE DE BLINN VIRA RUGOSIDADE pelo mesmo ajuste que o
        // proprio glTF sugere para converter material antigo: um expoente
        // alto e um brilho pequeno e apertado. Sem isso, todo OBJ com
        // `.mtl` sairia igualmente fosco.
        const float r = std::sqrt(2.0F / (brilho + 2.0F));
        rugosidade = prender(r, 0.04F, 1.0F);
      }
    }
    m.metalico = prender(metalico, 0.0F, 1.0F);
    m.rugosidade = prender(rugosidade, 0.04F, 1.0F);

    aiColor4D emi(0.0F, 0.0F, 0.0F, 1.0F);
    am.Get(AI_MATKEY_COLOR_EMISSIVE, emi);
    m.emissivo = Cor::de_argb(cor_de_assimp(emi));
    // A FORCA VEM DE UMA EXTENSAO (KHR_materials_emissive_strength), e o
    // Assimp a expoe como "$mat.emissiveIntensity". Sem ela, 1 — e nunca
    // zero, que apagaria o brilho de um material que o arquivo acendeu.
    m.forca_emissiva = 1.0F;
    real_do_material(am, AI_MATKEY_EMISSIVE_INTENSITY, m.forca_emissiva);

    m.textura_cor = ctx.texturas->obter(
        caminho_da_textura(am, aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE),
        true, false);
    m.textura_normal = ctx.texturas->obter(
        caminho_da_textura(am, aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS),
        false, true);
    // O METALICO-RUGOSIDADE PACKADO: verde e a rugosidade, azul o metalico.
    // E um dado LINEAR, e nao cor — converter duas vezes escureceria o
    // material inteiro (§6).
    m.textura_metalico_rugosidade = ctx.texturas->obter(
        caminho_da_textura(am, aiTextureType_METALNESS,
                           aiTextureType_DIFFUSE_ROUGHNESS),
        false, false);
    m.textura_emissiva = ctx.texturas->obter(
        caminho_da_textura(am, aiTextureType_EMISSION_COLOR, aiTextureType_EMISSIVE),
        true, false);
    m.textura_oclusao = ctx.texturas->obter(
        caminho_da_textura(am, aiTextureType_AMBIENT_OCCLUSION,
                           aiTextureType_LIGHTMAP),
        false, false);

    m.face_dupla = booleano_do_material(am, AI_MATKEY_TWOSIDED, false);

    // O MODO DE ALFA. O glTF diz explicitamente; quando nao diz, a
    // presenca de um mapa de opacidade denuncia o modo.
    const std::string modo = texto_do_material(am, AI_MATKEY_GLTF_ALPHAMODE);
    if (modo == "MASK") {
      m.modo = Material::Modo::mascarado;
    } else if (modo == "BLEND") {
      m.modo = Material::Modo::transparente;
    } else {
      aiString opacidade;
      const bool tem_opacidade =
          am.GetTexture(aiTextureType_OPACITY, 0, &opacidade) == AI_SUCCESS &&
          opacidade.length > 0;
      const bool alfa_na_cor = base.a < 0.999F;
      m.modo = (tem_opacidade || alfa_na_cor) ? Material::Modo::transparente
                                              : Material::Modo::opaco;
    }
    m.alfa_corte = 0.5F;
    real_do_material(am, AI_MATKEY_GLTF_ALPHACUTOFF, m.alfa_corte);

    ctx.modelo.materiais.push_back(std::move(m));
  }
}

/// A FAIXA DA MALHA — QUE E UMA SO.
///
/// O ASSIMP JA SEPARA POR MATERIAL: cada `aiMesh` tem um `mMaterialIndex`
/// e mais nada. O `aiProcess_SortByPType` quebra a malha em uma por tipo
/// de primitiva, e o importador de origem (o glTF, por exemplo) ja entrega
/// uma malha por primitiva, que e uma por material. Entao nao ha o que
/// agrupar aqui: a faixa e a lista de indices inteira, apontando para o
/// material da malha.
///
/// A `Faixa` CONTINUA EXISTINDO como estrutura propria porque o
/// renderizador raciocina por ela — e um dia um tipo de malha pode ter
/// mais de uma (o morph, o LOD). Montar a lista agora, mesmo com um
/// elemento, evita ter de mudar o desenho depois.
void montar_faixa(const aiMesh& malha, std::vector<std::uint32_t>& indices,
                  std::vector<Faixa>& faixas, const Modelo& modelo) {
  // As faces do Assimp ja sao triangulos (`aiProcess_Triangulate`), mas
  // uma face degenerada ou de outro tipo ainda pode aparecer num arquivo
  // mal formado; desenhar 2 ou 4 indices viraria lixo na tela.
  for (unsigned f = 0; f < malha.mNumFaces; ++f) {
    const aiFace& face = malha.mFaces[f];
    if (face.mNumIndices != 3) continue;
    indices.push_back(face.mIndices[0]);
    indices.push_back(face.mIndices[1]);
    indices.push_back(face.mIndices[2]);
  }
  if (indices.empty()) return;

  Faixa fa;
  fa.primeiro_indice = 0;
  fa.quantidade = static_cast<std::uint32_t>(indices.size());
  fa.base_do_vertice = 0;
  fa.material = static_cast<std::int32_t>(malha.mMaterialIndex);
  if (fa.material >= 0 &&
      static_cast<std::size_t>(fa.material) < modelo.materiais.size()) {
    const Material& m = modelo.materiais[static_cast<std::size_t>(fa.material)];
    fa.transparente = m.modo == Material::Modo::transparente ? 1 : 0;
    fa.mascarado = m.modo == Material::Modo::mascarado ? 1 : 0;
    fa.face_dupla = m.face_dupla ? 1 : 0;
  }
  faixas.push_back(fa);
}

void converter_malhas(Contexto& ctx) {
  const aiScene& cena = *ctx.cena;
  Modelo& modelo = ctx.modelo;
  modelo.malhas.reserve(cena.mNumMeshes);

  for (unsigned mi = 0; mi < cena.mNumMeshes; ++mi) {
    const aiMesh& am = *cena.mMeshes[mi];
    if ((am.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) continue;
    if (am.mNumVertices == 0 || am.mNumFaces == 0) continue;

    Malha malha;
    malha.nome = am.mName.C_Str();
    malha.vertices.reserve(am.mNumVertices);

    const bool tem_normal = am.HasNormals();
    const bool tem_uv0 = am.HasTextureCoords(0);
    const bool tem_uv1 = am.HasTextureCoords(1);
    const bool tem_tangente = am.HasTangentsAndBitangents();
    const bool tem_cor = am.HasVertexColors(0);

    for (unsigned v = 0; v < am.mNumVertices; ++v) {
      Vertice vertice;
      vertice.posicao = de_assimp(am.mVertices[v]);
      vertice.normal = tem_normal ? geo::normalizado(de_assimp(am.mNormals[v]))
                                  : Vec3{0.0F, 1.0F, 0.0F};
      if (tem_uv0) vertice.uv0 = {am.mTextureCoords[0][v].x, am.mTextureCoords[0][v].y};
      if (tem_uv1) vertice.uv1 = {am.mTextureCoords[1][v].x, am.mTextureCoords[1][v].y};
      if (tem_tangente) {
        const aiVector3D t = am.mTangents[v];
        const aiVector3D b = am.mBitangents[v];
        const Vec3 tangente = geo::normalizado(de_assimp(t));
        // O SINAL DA BITANGENTE E O QUE O MAPA DE NORMAIS PRECISA: sem
        // ele a superficie sai espelhada, e o defeito so aparece em
        // modelo com detalhe assimetrico.
        const float sinal =
            geo::ponto(geo::cruzado(vertice.normal, tangente), de_assimp(b)) < 0.0F
                ? -1.0F
                : 1.0F;
        vertice.tangente = {tangente.x, tangente.y, tangente.z, sinal};
      }
      if (tem_cor) vertice.cor = cor_de_assimp(am.mColors[0][v]);
      malha.vertices.push_back(vertice);
    }

    // ---------------------------------------------------------- esqueleto
    // OS NOS QUE DESENHAM ESTA MALHA, direto do arquivo. Uma malha sem
    // esqueleto desenhada por cinco nos vira cinco desenhos da MESMA
    // geometria — que e a instancia de graca (§19). Uma malha deformada
    // nao pendura em no nenhum: quem a posiciona sao os ossos.
    {
      const auto achado = ctx.nos_da_malha.find(mi);
      if (achado != ctx.nos_da_malha.end()) malha.nos = achado->second;
    }
    if (am.HasBones() && am.mNumBones > 0) {
      malha.esqueletica = true;
      for (unsigned bi = 0; bi < am.mNumBones; ++bi) {
        const aiBone& osso = *am.mBones[bi];
        const std::string nome = osso.mName.C_Str();
        std::int32_t slot = -1;
        const auto achado = ctx.osso_por_no.find(nome);
        if (achado != ctx.osso_por_no.end()) {
          slot = achado->second;
        } else {
          slot = static_cast<std::int32_t>(modelo.ossos.size());
          Osso o;
          o.nome = nome;
          o.ligacao_inversa = de_assimp(osso.mOffsetMatrix);
          const auto no = ctx.no_por_nome.find(nome);
          if (no != ctx.no_por_nome.end()) {
            o.no = no->second;
            const No& n = modelo.nos[static_cast<std::size_t>(o.no)];
            o.pai = n.pai;
            o.posicao = n.posicao;
            o.rotacao = n.rotacao;
            o.escala = n.escala;
            if (modelo.no_e_osso.empty()) modelo.no_e_osso.assign(modelo.nos.size(), 0);
            if (static_cast<std::size_t>(o.no) < modelo.no_e_osso.size()) {
              modelo.no_e_osso[static_cast<std::size_t>(o.no)] = 1;
            }
            modelo.ordem_dos_ossos.push_back(o.no);
          }
          modelo.ossos.push_back(std::move(o));
          ctx.osso_por_no[nome] = slot;
        }
        for (unsigned w = 0; w < osso.mNumWeights; ++w) {
          const aiVertexWeight& p = osso.mWeights[w];
          if (p.mVertexId >= malha.vertices.size()) continue;
          Vertice& vertice = malha.vertices[p.mVertexId];
          for (int k = 0; k < 4; ++k) {
            if (vertice.pesos[k] == 0.0F) {
              vertice.ossos[k] = static_cast<std::uint16_t>(slot);
              vertice.pesos[k] = p.mWeight;
              break;
            }
          }
        }
      }

      // ---------------------------------------------- espaco da ligacao
      // Aqui os vertices deixam de estar no espaco do no e passam a estar
      // no espaco da LIGACAO (ver o bloco no topo do arquivo).
      const std::int32_t no = malha.nos.empty() ? 0 : malha.nos.front();
      Mat4 inverso;
      if (static_cast<std::size_t>(no) < ctx.globais.size() &&
          geo::inverter(ctx.globais[static_cast<std::size_t>(no)], inverso)) {
        for (Vertice& vertice : malha.vertices) {
          vertice.posicao = geo::transformar_ponto(inverso, vertice.posicao);
          vertice.normal = geo::normalizado(geo::transformar_vetor(inverso, vertice.normal));
        }
      }
      // A malha deformada perde a lista de nos: quem a posiciona e o
      // esqueleto, e o no so serve para tirar a transformacao de dentro
      // dos vertices (acima).
      malha.nos.clear();
    }

    // ---------------------------------------------------------- indices
    malha.indices.reserve(am.mNumFaces * 3);
    montar_faixa(am, malha.indices, malha.faixas, modelo);
    // UMA MALHA SEM TRIANGULO NENHUM NAO ENTRA. Ela contaria vertice,
    // ocuparia memoria e nao desenharia nada — e o contador de triangulos
    // mentiria para quem for medir o custo da cena.
    if (malha.indices.empty()) continue;

    // ------------------------------------------------------------- caixa
    for (const Vertice& vertice : malha.vertices) malha.limites.incluir(vertice.posicao);

    modelo.bytes_de_malha += malha.vertices.size() * sizeof(Vertice) +
                             malha.indices.size() * sizeof(std::uint32_t);
    modelo.vertices += static_cast<std::uint32_t>(malha.vertices.size());
    modelo.triangulos += static_cast<std::uint32_t>(malha.indices.size() / 3);
    modelo.malhas.push_back(std::move(malha));
  }
}

void converter_animacoes(Contexto& ctx) {
  const aiScene& cena = *ctx.cena;
  Modelo& modelo = ctx.modelo;
  if (ctx.opcoes->sem_animacao) return;
  modelo.animacoes.reserve(cena.mNumAnimations);

  for (unsigned ai = 0; ai < cena.mNumAnimations; ++ai) {
    const aiAnimation& aa = *cena.mAnimations[ai];
    // O ASSIMP CONTA EM TICKS. Sem a taxa declarada, 25 quadros por
    // segundo e a convencao que o proprio Assimp adota — usar 1 daria
    // uma animacao mil vezes mais lenta, e o defeito pareceria travamento.
    const double tps = aa.mTicksPerSecond > 0.0 ? aa.mTicksPerSecond : 25.0;
    Animacao anim;
    anim.nome = aa.mName.C_Str();

    for (unsigned ci = 0; ci < aa.mNumChannels; ++ci) {
      const aiNodeAnim& canal = *aa.mChannels[ci];
      const auto achado = ctx.no_por_nome.find(canal.mNodeName.C_Str());
      if (achado == ctx.no_por_nome.end()) continue;
      const std::int32_t no = achado->second;
      if (modelo.no_e_osso.empty()) modelo.no_e_osso.assign(modelo.nos.size(), 0);
      if (static_cast<std::size_t>(no) < modelo.no_e_osso.size() &&
          modelo.no_e_osso[static_cast<std::size_t>(no)] != 0) {
        anim.esqueletica = true;
      }

      if (canal.mNumPositionKeys > 0) {
        TrilhaDeVetor t;
        t.alvo = no;
        t.chaves.reserve(canal.mNumPositionKeys);
        for (unsigned k = 0; k < canal.mNumPositionKeys; ++k) {
          const aiVectorKey& c = canal.mPositionKeys[k];
          t.chaves.push_back({c.mTime / tps, de_assimp(c.mValue)});
        }
        anim.posicoes.push_back(std::move(t));
        anim.duracao = std::max(anim.duracao, canal.mPositionKeys[canal.mNumPositionKeys - 1].mTime / tps);
      }
      if (canal.mNumRotationKeys > 0) {
        TrilhaDeQuat t;
        t.alvo = no;
        t.chaves.reserve(canal.mNumRotationKeys);
        for (unsigned k = 0; k < canal.mNumRotationKeys; ++k) {
          const aiQuatKey& c = canal.mRotationKeys[k];
          t.chaves.push_back({c.mTime / tps, de_assimp(c.mValue)});
        }
        anim.rotacoes.push_back(std::move(t));
        anim.duracao = std::max(anim.duracao, canal.mRotationKeys[canal.mNumRotationKeys - 1].mTime / tps);
      }
      if (canal.mNumScalingKeys > 0) {
        TrilhaDeVetor t;
        t.alvo = no;
        t.chaves.reserve(canal.mNumScalingKeys);
        for (unsigned k = 0; k < canal.mNumScalingKeys; ++k) {
          const aiVectorKey& c = canal.mScalingKeys[k];
          t.chaves.push_back({c.mTime / tps, de_assimp(c.mValue)});
        }
        anim.escalas.push_back(std::move(t));
        anim.duracao = std::max(anim.duracao, canal.mScalingKeys[canal.mNumScalingKeys - 1].mTime / tps);
      }
    }

    // O MORPH NAO TEM CANAL DE NO: ele e um peso por alvo de forma, e o
    // Assimp o expoe como canais com o nome do alvo. Fica registrado como
    // peso do modelo inteiro.
    for (unsigned mi = 0; mi < aa.mNumMorphMeshChannels; ++mi) {
      const aiMeshMorphAnim& cm = *aa.mMorphMeshChannels[mi];
      for (unsigned k = 0; k < cm.mNumKeys; ++k) {
        for (unsigned w = 0; w < cm.mKeys[k].mNumValuesAndWeights; ++w) {
          anim.duracao = std::max(anim.duracao,
                                  cm.mKeys[k].mTime / tps);
          (void)w;
        }
      }
    }

    if (anim.duracao <= 0.0) {
      // Um clipe sem chave nenhuma ainda e um clipe: dura o que a lista
      // de canais disser, e zero so quando nao ha nada.
      anim.duracao = aa.mDuration > 0.0 ? aa.mDuration / tps : 0.0;
    }
    if (!anim.posicoes.empty() || !anim.rotacoes.empty() || !anim.escalas.empty()) {
      modelo.animacoes.push_back(std::move(anim));
    }
  }
}

}  // namespace

// ---------------------------------------------------------------- a pose

namespace {

/// PROCURA BINARIA NA CHAVE. As chaves de um clipe vem ordenadas pelo
/// tempo, e um clipe de 30 segundos com dez ossos tem milhares delas:
/// varrer a lista por osso a cada quadro custa mais do que amostrar.
template <typename Chave>
[[nodiscard]] std::size_t achar_intervalo(const std::vector<Chave>& chaves,
                                          double tempo) noexcept {
  std::size_t lo = 0, hi = chaves.size();
  while (lo < hi) {
    const std::size_t meio = (lo + hi) / 2;
    if (chaves[meio].tempo <= tempo) {
      lo = meio + 1;
    } else {
      hi = meio;
    }
  }
  return lo == 0 ? 0 : lo - 1;
}

template <typename Chave>
[[nodiscard]] double fracao(const std::vector<Chave>& chaves, std::size_t i,
                            double tempo) noexcept {
  if (i + 1 >= chaves.size()) return 0.0;
  const double a = chaves[i].tempo;
  const double b = chaves[i + 1].tempo;
  if (b - a < 1e-9) return 0.0;
  const double t = (tempo - a) / (b - a);
  return t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
}

[[nodiscard]] Vec3 amostrar_vetor(const std::vector<ChaveDeVetor>& chaves,
                                  double tempo) noexcept {
  const std::size_t i = achar_intervalo(chaves, tempo);
  const float t = static_cast<float>(fracao(chaves, i, tempo));
  return geo::misturar(chaves[i].valor, chaves[i + 1 < chaves.size() ? i + 1 : i].valor, t);
}

/// SLERP. A INTERPOLACAO DE ROTACAO NAO PODE SER POR COMPONENTE: entre
/// dois giros quase opostos a interpolacao linear passa pelo meio, encolhe
/// o quaternion e o osso atravessa o corpo. Aqui os dois sinais sao
/// alinhados antes, e o arco curto e o escolhido — que e o que faz o
/// membro girar para o lado certo.
[[nodiscard]] Quat amostrar_quat(const std::vector<ChaveDeQuat>& chaves,
                                 double tempo) noexcept {
  const std::size_t i = achar_intervalo(chaves, tempo);
  if (i + 1 >= chaves.size()) return chaves[i].valor;
  const Quat& a = chaves[i].valor;
  Quat b = chaves[i + 1].valor;
  const float t = static_cast<float>(fracao(chaves, i, tempo));
  float cosseno = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (cosseno < 0.0F) {
    b = Quat{-b.x, -b.y, -b.z, -b.w};
    cosseno = -cosseno;
  }
  if (cosseno > 0.9995F) {
    const Quat r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                 a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    const float n = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (n < 1e-8F) return a;
    return Quat{r.x / n, r.y / n, r.z / n, r.w / n};
  }
  const float theta = std::acos(cosseno);
  const float seno = std::sin(theta);
  const float wa = std::sin((1.0F - t) * theta) / seno;
  const float wb = std::sin(t * theta) / seno;
  return Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb,
              a.w * wa + b.w * wb};
}

/// AVALIA A ARVORE DE NOS E DEVOLVE AS MATRIZES GLOBAIS. `local` sai com a
/// transformacao local ja resolvida (repouso ou animada), que e o que o
/// teste de animacao inspeciona sem precisar desmontar matriz.
void avaliar_nos(const Modelo& modelo, const std::vector<Vec3>* posicoes,
                 const std::vector<Quat>* rotacoes,
                 const std::vector<Vec3>* escalas, std::vector<Mat4>& globais) {
  const std::size_t n = modelo.nos.size();
  globais.assign(n, Mat4::identidade());
  for (std::size_t i = 0; i < n; ++i) {
    const No& no = modelo.nos[i];
    const Vec3 p = posicoes != nullptr ? (*posicoes)[i] : no.posicao;
    const Quat r = rotacoes != nullptr ? (*rotacoes)[i] : no.rotacao;
    const Vec3 e = escalas != nullptr ? (*escalas)[i] : no.escala;
    const Mat4 local = Mat4::de_trs(p, r, e);
    const std::int32_t pai = no.pai;
    // A LISTA VEM EM ORDEM DE ARQUIVO, e o pai sempre aparece antes do
    // filho nela. O teste cobre o contrario para nao depender do acaso.
    globais[i] = (pai >= 0 && static_cast<std::size_t>(pai) < i)
                     ? globais[static_cast<std::size_t>(pai)] * local
                     : local;
  }
}

void preencher_pose(const Modelo& modelo, const std::vector<Mat4>& globais,
                    Pose& saida) {
  saida.nos = globais;
  saida.ossos.resize(modelo.ossos.size());
  for (std::size_t i = 0; i < modelo.ossos.size(); ++i) {
    const Osso& osso = modelo.ossos[i];
    // A MATRIZ DE PELE: leva o vertice do espaco da ligacao para onde ele
    // esta agora. Sem a ligacao inversa o modelo aparece dobrado sobre a
    // origem, e o defeito nao parece erro de conta.
    saida.ossos[i] = (osso.no >= 0 && static_cast<std::size_t>(osso.no) < globais.size())
                         ? globais[static_cast<std::size_t>(osso.no)] * osso.ligacao_inversa
                         : Mat4::identidade();
  }
  saida.vazia = false;
}

}  // namespace

void pose_de_repouso(const Modelo& modelo, Pose& saida) {
  std::vector<Mat4> globais;
  // A ARVORE DO ARQUIVO JA TEM A POSE DE REPOUSO: os nos guardam posicao,
  // rotacao e escala do arquivo, e nao uma identidade. Usar identidade
  // aqui colocaria todo osso na origem e o modelo viraria uma bola.
  avaliar_nos(modelo, nullptr, nullptr, nullptr, globais);
  preencher_pose(modelo, globais, saida);
  saida.posicoes.resize(modelo.nos.size());
  saida.rotacoes.resize(modelo.nos.size());
  saida.escalas.resize(modelo.nos.size());
  for (std::size_t i = 0; i < modelo.nos.size(); ++i) {
    saida.posicoes[i] = modelo.nos[i].posicao;
    saida.rotacoes[i] = modelo.nos[i].rotacao;
    saida.escalas[i] = modelo.nos[i].escala;
  }
}

double duracao_da_animacao(const Modelo& modelo, std::int32_t animacao) noexcept {
  if (animacao < 0 || static_cast<std::size_t>(animacao) >= modelo.animacoes.size()) {
    return 0.0;
  }
  return modelo.animacoes[static_cast<std::size_t>(animacao)].duracao;
}

void amostrar_pose(const Modelo& modelo, std::int32_t animacao, double tempo,
                   Pose& saida) {
  const std::size_t n = modelo.nos.size();
  if (animacao < 0 || static_cast<std::size_t>(animacao) >= modelo.animacoes.size() ||
      n == 0) {
    pose_de_repouso(modelo, saida);
    return;
  }
  const Animacao& anim = modelo.animacoes[static_cast<std::size_t>(animacao)];

  // O TEMPO E PRENDIDO NAS PONTAS. Quem decide se o clipe cicla e a
  // timeline (§12): aqui, pedir 40 s de um clipe de 3 s devolve o ultimo
  // quadro, e nao um quadro aleatorio.
  const double t = tempo < 0.0 ? 0.0 : (anim.duracao > 0.0 && tempo > anim.duracao
                                            ? anim.duracao
                                            : tempo);

  std::vector<Vec3> posicoes(n);
  std::vector<Quat> rotacoes(n);
  std::vector<Vec3> escalas(n);
  for (std::size_t i = 0; i < n; ++i) {
    posicoes[i] = modelo.nos[i].posicao;
    rotacoes[i] = modelo.nos[i].rotacao;
    escalas[i] = modelo.nos[i].escala;
  }
  for (const TrilhaDeVetor& trilha : anim.posicoes) {
    if (trilha.alvo >= 0 && static_cast<std::size_t>(trilha.alvo) < n && !trilha.chaves.empty()) {
      posicoes[static_cast<std::size_t>(trilha.alvo)] = amostrar_vetor(trilha.chaves, t);
    }
  }
  for (const TrilhaDeQuat& trilha : anim.rotacoes) {
    if (trilha.alvo >= 0 && static_cast<std::size_t>(trilha.alvo) < n && !trilha.chaves.empty()) {
      rotacoes[static_cast<std::size_t>(trilha.alvo)] = amostrar_quat(trilha.chaves, t);
    }
  }
  for (const TrilhaDeVetor& trilha : anim.escalas) {
    if (trilha.alvo >= 0 && static_cast<std::size_t>(trilha.alvo) < n && !trilha.chaves.empty()) {
      escalas[static_cast<std::size_t>(trilha.alvo)] = amostrar_vetor(trilha.chaves, t);
    }
  }

  std::vector<Mat4> globais;
  avaliar_nos(modelo, &posicoes, &rotacoes, &escalas, globais);
  preencher_pose(modelo, globais, saida);
  saida.posicoes = std::move(posicoes);
  saida.rotacoes = std::move(rotacoes);
  saida.escalas = std::move(escalas);
}

// ------------------------------------------------------------- entradas

namespace {

/// A CONVERSAO, UMA VEZ SO. As duas entradas (arquivo e memoria) diferem
/// apenas em como os bytes chegam ao Assimp; da arvore para dentro o
/// caminho e o mesmo. Duas copias divergiriam — e a que ninguem testa
/// seria a que o app usa.
Resulta<Modelo> montar(const aiScene& cena, const std::string& pasta,
                       const OpcoesDeImportacao& opcoes,
                       std::uint64_t bytes_do_arquivo, RelatoDaImportacao* relato) {
  Contexto ctx;
  ctx.cena = &cena;
  ctx.opcoes = &opcoes;
  ctx.relato = relato;
  ctx.texturas = std::make_unique<CatalogoDeTexturas>(cena, pasta, opcoes, relato);

  achatar_nos(cena.mRootNode, -1, ctx.modelo.nos, ctx.no_por_nome);
  {
    std::int32_t proximo = 0;
    mapear_nos_das_malhas(cena.mRootNode, proximo, ctx.nos_da_malha);
  }
  // A ESCALA DO CHAMADOR ENTRA NA RAIZ, e nao nos vertices: assim o
  // modelo inteiro — inclusive os ossos — fica na mesma unidade. Escalar
  // so a malha deixaria o esqueleto em metros e a pele em pixels, que e o
  // modelo desmontado na primeira animacao.
  if (opcoes.escala != 1.0F && !ctx.modelo.nos.empty()) {
    const Vec3 e{opcoes.escala, opcoes.escala, opcoes.escala};
    const Mat4 escala = Mat4::de_escala(e);
    ctx.modelo.nos[0].local = escala * ctx.modelo.nos[0].local;
    ctx.modelo.nos[0].posicao = geo::transformar_ponto(escala, ctx.modelo.nos[0].posicao);
    ctx.modelo.nos[0].escala = {ctx.modelo.nos[0].escala.x * opcoes.escala,
                                ctx.modelo.nos[0].escala.y * opcoes.escala,
                                ctx.modelo.nos[0].escala.z * opcoes.escala};
  }
  globais_de_repouso(ctx.modelo.nos, ctx.globais);

  converter_materiais(ctx);
  converter_malhas(ctx);
  converter_animacoes(ctx);

  ctx.modelo.texturas = ctx.texturas->levar();
  for (const Textura& t : ctx.modelo.texturas) {
    ctx.modelo.bytes_de_textura += t.pixels.size();
  }

  if (relato != nullptr) {
    relato->bytes_do_arquivo = bytes_do_arquivo;
    relato->malhas = static_cast<std::uint32_t>(ctx.modelo.malhas.size());
    relato->materiais = static_cast<std::uint32_t>(ctx.modelo.materiais.size());
    relato->texturas = static_cast<std::uint32_t>(ctx.modelo.texturas.size());
    relato->nos = static_cast<std::uint32_t>(ctx.modelo.nos.size());
    relato->ossos = static_cast<std::uint32_t>(ctx.modelo.ossos.size());
    relato->animacoes = static_cast<std::uint32_t>(ctx.modelo.animacoes.size());
    relato->triangulos = ctx.modelo.triangulos;
    relato->vertices = ctx.modelo.vertices;
    relato->bytes_em_memoria = ctx.modelo.bytes();
  }
  // UM ARQUIVO QUE NAO DEU MALHA NENHUMA NAO E UM MODELO. O editor
  // prefere dizer "nao suportado" a pendurar uma camada 3D que desenha
  // nada e nao explica por que (§43).
  if (ctx.modelo.malhas.empty()) return Erro::nao_suportado;
  return std::move(ctx.modelo);
}

}  // namespace

Resulta<Modelo> importar(const std::string& caminho,
                         const OpcoesDeImportacao& opcoes,
                         RelatoDaImportacao* relato) {
  std::FILE* f = std::fopen(caminho.c_str(), "rb");
  if (f == nullptr) return Erro::nao_existe;
  std::fseek(f, 0, SEEK_END);
  const long tamanho = std::ftell(f);
  std::fclose(f);
  if (tamanho < 0) return Erro::nao_existe;
  if (static_cast<std::uint64_t>(tamanho) > opcoes.teto_de_bytes_do_arquivo) {
    return Erro::orcamento_estourado;
  }

  const std::size_t barra = caminho.find_last_of("/\\");
  const std::string pasta = barra == std::string::npos ? "." : caminho.substr(0, barra);

  // O ASSIMP E LOCAL A ESTA FUNCAO, e e por isso que a arvore dele nao
  // sobrevive ao import (§4): o que sai daqui e o `Modelo`, que nao
  // depende dele para nada.
  Importer importador;
  const aiScene* cena = importador.ReadFile(caminho, kProcessos);
  if (cena == nullptr) {
    // O MOTIVO DO ASSIMP VAI PARA O RELATO. Sem ele, "modelo invalido" no
    // app nao diz se o arquivo esta corrompido, se falta uma textura
    // externa ou se o formato nao esta compilado neste binario.
    if (relato != nullptr) relato->primeiro_aviso = importador.GetErrorString();
    return Erro::argumento;
  }
  return montar(*cena, pasta, opcoes, static_cast<std::uint64_t>(tamanho), relato);
}

Resulta<Modelo> importar_memoria(const std::uint8_t* bytes, std::size_t tamanho,
                                 const std::string& extensao,
                                 const OpcoesDeImportacao& opcoes,
                                 RelatoDaImportacao* relato) {
  if (bytes == nullptr || tamanho == 0) return Erro::argumento;
  if (tamanho > opcoes.teto_de_bytes_do_arquivo) return Erro::orcamento_estourado;

  Importer importador;
  const aiScene* cena = importador.ReadFileFromMemory(
      bytes, tamanho, kProcessos, extensao.empty() ? nullptr : extensao.c_str());
  if (cena == nullptr) {
    if (relato != nullptr) relato->primeiro_aviso = importador.GetErrorString();
    return Erro::argumento;
  }
  // Sem pasta: um arquivo vindo da memoria nao tem vizinhos no disco, e as
  // texturas externas dele nao existem. As embutidas (GLB) funcionam.
  return montar(*cena, ".", opcoes, tamanho, relato);
}

}  // namespace aurea::render::tresd
