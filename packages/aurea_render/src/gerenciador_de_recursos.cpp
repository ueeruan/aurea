#include "gerenciador_de_recursos.h"

#include <algorithm>
#include <limits>

namespace aurea::render {

namespace {
/// OS BYTES QUE UMA DESCRICAO PROMETE. Quem pede informa; se informar
/// zero, derivamos do tamanho para nao contabilizar um alvo RGBA como
/// zero bytes e furar o orcamento pela porta de tras.
std::uint64_t bytes_da_descricao(const DescricaoDoRecurso& d) noexcept {
  if (d.bytes != 0) return d.bytes;
  const std::uint64_t p =
      static_cast<std::uint64_t>(d.largura) * static_cast<std::uint64_t>(d.altura);
  switch (d.tipo) {
    case TipoDeRecurso::textura:
    case TipoDeRecurso::alvo:
      return p * 4;  // RGBA8
    case TipoDeRecurso::buffer:
      return p;
  }
  return p;
}
}  // namespace

GerenciadorDeRecursos::GerenciadorDeRecursos(
    std::uint64_t orcamento_bytes) noexcept
    : orcamento_bytes_(orcamento_bytes) {}

bool GerenciadorDeRecursos::cabe(std::uint64_t bytes) const noexcept {
  if (orcamento_bytes_ == 0) return true;  // sem teto (so em teste)
  if (bytes > orcamento_bytes_) return false;
  return bytes_ + bytes <= orcamento_bytes_;
}

Resulta<AlcaDeRecurso> GerenciadorDeRecursos::criar(
    const DescricaoDoRecurso& d) {
  if (d.largura == 0 || d.altura == 0) {
    if (d.tipo != TipoDeRecurso::buffer) {
      ++recusados_;
      return Erro::argumento;
    }
  }
  const std::uint64_t bytes = bytes_da_descricao(d);
  // NAO CABE NEM COM A CASA VAZIA: recusar agora e melhor do que aceitar,
  // despejar tudo que existe e mesmo assim estourar — o despejo do que
  // estava em uso e irreversivel dentro do quadro.
  if (orcamento_bytes_ != 0 && bytes > orcamento_bytes_) {
    ++recusados_;
    return Erro::orcamento_estourado;
  }
  if (!abrir_espaco(bytes)) {
    ++recusados_;
    return Erro::orcamento_estourado;
  }

  auto r = std::make_shared<Recurso>();
  r->id = proximo_id_++;
  r->descricao = d;
  r->descricao.bytes = bytes;
  r->ultimo_uso = quadro_;

  const IdDeRecurso id = r->id;
  const auto [it, inseriu] = recursos_.emplace(id, std::move(r));
  (void)inseriu;  // o id e novo por construcao: nao ha colisao possivel
  bytes_ += bytes;
  pico_bytes_ = std::max(pico_bytes_, bytes_);
  ++criados_;
  return AlcaDeRecurso{it->second};
}

Resulta<AlcaDeRecurso> GerenciadorDeRecursos::alvo(std::uint32_t largura,
                                                   std::uint32_t altura) {
  if (largura == 0 || altura == 0) return Erro::argumento;

  // O CACHE DE QUADRO: procura um alvo do MESMO tamanho que ninguem
  // esteja usando. Reaproveitar evita uma alocacao de GPU por quadro —
  // que e exatamente o que faz um editor esquentar.
  IdDeRecurso melhor = kRecursoInvalido;
  std::uint64_t mais_antigo = std::numeric_limits<std::uint64_t>::max();
  for (const auto& [id, r] : recursos_) {
    if (r->descricao.tipo != TipoDeRecurso::alvo) continue;
    if (r->descricao.largura != largura || r->descricao.altura != altura) {
      continue;
    }
    // `use_count() == 1` = so o mapa segura: ninguem esta lendo.
    if (r.use_count() != 1) continue;
    if (r->ultimo_uso < mais_antigo) {
      mais_antigo = r->ultimo_uso;
      melhor = id;
    }
  }
  if (melhor != kRecursoInvalido) {
    const auto& r = recursos_.at(melhor);
    r->ultimo_uso = quadro_;
    ++reaproveitados_;
    return AlcaDeRecurso{r};
  }

  DescricaoDoRecurso d;
  d.tipo = TipoDeRecurso::alvo;
  d.largura = largura;
  d.altura = altura;
  d.bytes = static_cast<std::uint64_t>(largura) * altura * 4;
  return criar(d);
}

AlcaDeRecurso GerenciadorDeRecursos::usar(IdDeRecurso id) {
  const auto it = recursos_.find(id);
  if (it == recursos_.end()) return AlcaDeRecurso{};
  it->second->ultimo_uso = quadro_;
  return AlcaDeRecurso{it->second};
}

bool GerenciadorDeRecursos::abrir_espaco(std::uint64_t bytes) noexcept {
  if (cabe(bytes)) return true;
  if (orcamento_bytes_ == 0) return true;
  while (!cabe(bytes)) {
    // O MAIS VELHO QUE NINGUEM SEGURA.
    auto alvo = recursos_.end();
    std::uint64_t mais_antigo = std::numeric_limits<std::uint64_t>::max();
    for (auto it = recursos_.begin(); it != recursos_.end(); ++it) {
      if (it->second.use_count() != 1) continue;  // em uso: nao sai
      if (it->second->ultimo_uso < mais_antigo) {
        mais_antigo = it->second->ultimo_uso;
        alvo = it;
      }
    }
    if (alvo == recursos_.end()) return false;  // tudo em uso
    bytes_ -= alvo->second->bytes();
    recursos_.erase(alvo);
    ++despejados_;
  }
  return true;
}

std::uint32_t GerenciadorDeRecursos::aparar() noexcept {
  if (orcamento_bytes_ == 0) return 0;
  std::uint32_t saiu = 0;
  while (bytes_ > orcamento_bytes_) {
    // ACHA O MAIS VELHO QUE NINGUEM SEGURA.
    auto alvo = recursos_.end();
    std::uint64_t mais_antigo = std::numeric_limits<std::uint64_t>::max();
    for (auto it = recursos_.begin(); it != recursos_.end(); ++it) {
      if (it->second.use_count() != 1) continue;  // em uso: nao sai
      if (it->second->ultimo_uso < mais_antigo) {
        mais_antigo = it->second->ultimo_uso;
        alvo = it;
      }
    }
    // NADA PODE SAIR. Sair do laco e a resposta honesta: insistir daria
    // laco infinito, e despejar o que esta em uso daria corrupcao.
    if (alvo == recursos_.end()) break;
    bytes_ -= alvo->second->bytes();
    recursos_.erase(alvo);
    ++saiu;
    ++despejados_;
  }
  return saiu;
}

void GerenciadorDeRecursos::definir_orcamento(std::uint64_t bytes) noexcept {
  orcamento_bytes_ = bytes;
  aparar();
}

std::uint64_t GerenciadorDeRecursos::bytes_em_uso() const noexcept {
  return bytes_;
}

EstatisticasDeRecursos GerenciadorDeRecursos::estatisticas() const noexcept {
  EstatisticasDeRecursos e;
  e.bytes_em_uso = bytes_;
  e.bytes_orcamento = orcamento_bytes_;
  e.bytes_pico = pico_bytes_;
  e.vivos = static_cast<std::uint32_t>(recursos_.size());
  e.despejados = despejados_;
  e.criados = criados_;
  e.reaproveitados = reaproveitados_;
  e.recusados = recusados_;
  return e;
}

void GerenciadorDeRecursos::soltar_tudo() noexcept {
  // O MAPA ESQUECE, E A CARGA MORRE COM A ULTIMA ALCA. Se alguem ainda
  // segura um recurso, o `shared_ptr` mantem o objeto vivo e o backend
  // libera a memoria quando a ultima alca morrer — e nao agora, com o
  // compositor lendo. E por isso que a carga e possuida pelo `Recurso` e
  // nao pelo mapa.
  recursos_.clear();
  bytes_ = 0;
}

}  // namespace aurea::render
