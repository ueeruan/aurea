#include "gerenciador_de_shaders.h"

#include <span>

namespace aurea::render {

std::string GerenciadorDeShaders::chave(Backend b,
                                        const DescricaoDoShader& d) {
  // A CHAVE CARREGA A VERSAO E O BACKEND. O mesmo efeito tem fonte
  // diferente em Metal e em Vulkan; guardar as duas sob o mesmo nome
  // faria a segunda compilacao servir o programa da primeira.
  std::string k;
  k.reserve(d.nome.size() + 24);
  k += nome_do_backend(b);
  k += '\x1f';
  k += d.nome;
  k += '\x1f';
  k += std::to_string(static_cast<int>(d.etapa));
  k += '\x1f';
  k += std::to_string(d.versao);
  return k;
}

Resulta<std::uint32_t> GerenciadorDeShaders::obter(const DescricaoDoShader& d) {
  if (d.nome.empty() || d.fonte.empty()) {
    // CONTA COMO FALHA. Um shader registrado sem fonte e um erro de quem
    // chamou, e o placar tem de mostrar: sem isto, um pre-aquecimento que
    // nao compilou nada ficaria indistinguivel de um que nao foi pedido.
    ++stats_.falhas;
    return Erro::argumento;
  }

  const std::string k = chave(backend_, d);
  const auto it = cache_.find(k);
  if (it != cache_.end()) {
    // MESMA CHAVE, FONTE DIFERENTE: alguem editou o shader sem subir a
    // versao. Servir o programa velho daria um efeito errado e um bug
    // impossivel de achar; melhor recusar e dizer o nome.
    if (it->second.fonte != d.fonte) {
      ++stats_.falhas;
      return Erro::ja_existe;
    }
    ++stats_.reaproveitados;
    return it->second.id;
  }

  auto compilado = compilador_.compilar(d, backend_);
  if (compilado.tem_erro()) {
    ++stats_.falhas;
    return compilado.erro();
  }

  Entrada e;
  e.shader.nome = d.nome;
  e.shader.versao = d.versao;
  e.shader.etapa = d.etapa;
  e.shader.backend = backend_;
  e.shader.carga = std::move(compilado).valor();
  e.fonte = d.fonte;
  e.id = proximo_id_++;

  ids_[e.id] = k;
  cache_.emplace(k, std::move(e));
  ++stats_.compilados;
  ++stats_.vivos;
  return proximo_id_ - 1;
}

Resulta<std::uint32_t> GerenciadorDeShaders::pre_aquecer(
    std::span<const DescricaoDoShader> descricoes) {
  std::uint32_t prontos = 0;
  for (const DescricaoDoShader& d : descricoes) {
    // FALHA DE UM NAO DERRUBA O LOTE: pre-aquecer e adiantar trabalho, e
    // um shader que nao compila sera um erro visivel no lugar onde ele e
    // usado. Abortar o lote inteiro esconderia os que deram certo.
    if (obter(d).tem_valor()) ++prontos;
  }
  return prontos;
}

bool GerenciadorDeShaders::conferir_versao(std::string_view nome,
                                           std::uint32_t versao,
                                           std::string_view fonte) const
    noexcept {
  for (const auto& [k, e] : cache_) {
    (void)k;
    if (e.shader.nome != nome) continue;
    if (e.shader.versao != versao) continue;
    return e.fonte == fonte;
  }
  // NAO ESTA NO CACHE: nao ha o que conferir, e nao e defeito.
  return true;
}

const Shader* GerenciadorDeShaders::achar(std::uint32_t id) const noexcept {
  const auto pos = ids_.find(id);
  if (pos == ids_.end()) return nullptr;
  const auto it = cache_.find(pos->second);
  return it == cache_.end() ? nullptr : &it->second.shader;
}

EstatisticasDeShaders GerenciadorDeShaders::estatisticas() const noexcept {
  return stats_;
}

void GerenciadorDeShaders::soltar_tudo() noexcept {
  cache_.clear();
  ids_.clear();
  stats_.vivos = 0;
  // A COMPILACAO JA FEITA CONTINUA CONTADA: depois de uma perda de
  // contexto o placar de compilados nao volta a zero, senao o relatorio
  // de "quantas compilacoes este play custou" mentiria.
  proximo_id_ = 1;
}

}  // namespace aurea::render
