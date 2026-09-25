// =============================================================================
//  Aurea / project / AlightMotion.hpp
//
//  Importação de EFEITOS do Alight Motion. O XML do AM (projeto, elemento
//  exportado ou preset compartilhado) é uma `<scene>` com camadas — `shape`,
//  `text`, `embedScene`, `nullobj`, `audio`, `image`, `video` — e cada camada
//  pode ter filhos `<effect id="com.alightcreative.effects.X">` com
//  `<property name type value>` (fixa) ou `<property name><kf t v e/></property>`
//  (animada). O conversor transforma esses efeitos num preset "effects" do
//  Aurea (formato em Presets.hpp), aplicado pelo caminho de sempre.
//
//  REGRAS (a conversão NUNCA derruba o arquivo por causa de um detalhe):
//   · efeito do AM sem equivalente de verdade no Aurea → `skipped` (id do AM);
//     parâmetro sem equivalente → aviso. Só saem chaves que o registro de
//     efeitos conhece — o leitor de preset recusaria o arquivo inteiro.
//   · valor fora da faixa do parâmetro do Aurea → preso na faixa, com aviso.
//   · keyframe: o `t` do AM é NORMALIZADO (0..1) na duração da camada e a
//     curva `e` descreve a CHEGADA no keyframe (no Aurea a curva mora no
//     keyframe que começa o trecho). Vira quadro com a duração da camada
//     (startTime/endTime em ms, ou o totalTime da cena) e o fps da cena (30
//     sem fps). Sem duração conhecida, a propriedade fica PARADA no valor de
//     t = 0, com aviso.
//   · VÁRIAS camadas com efeito: vale a PRIMEIRA (ordem do arquivo, grupo
//     antes dos filhos) que tiver ao menos um efeito aproveitável; as outras
//     viram um aviso. Juntar pilhas de camadas diferentes numa só daria um
//     visual que nenhuma delas tinha.
//   · pacote .zip/.amproj: o maior XML com `<scene` dentro é o projeto (as
//     entradas "stored" e "deflate" são lidas; zip64 e criptografia, não).
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aurea {

class EffectRegistry;

namespace presets {

/// O que a conversão fez — para a pessoa saber o que precisa refazer à mão.
struct AlightImportReport {
    u32 mapped = 0;                     ///< efeitos convertidos
    std::vector<std::string> skipped;   ///< ids do AM sem equivalente
    std::vector<std::string> warnings;  ///< parâmetros perdidos, valores presos...
    std::string error;                  ///< por que não houve preset (vazio = houve)
    std::string layer;                  ///< rótulo da camada de onde vieram os efeitos
    std::string name;                   ///< nome do preset (título da cena, rótulo ou "Alight Motion")
};

/// `data` = texto XML ou bytes de um .zip/.amproj. Devolve o preset "effects"
/// em JSON (vazio = nada aproveitável; `report.error` diz o porquê).
/// `registry` nulo = efeitos embutidos do Aurea.
[[nodiscard]] std::string import_alight_motion(std::string_view data, const EffectRegistry* registry,
                                               AlightImportReport& report);

/// Envelope para as pontes: {"preset": "<json>", "name", "layer", "mapped": n,
/// "skipped": [...], "warnings": [...], "error": "..."}.
[[nodiscard]] std::string alight_import_envelope(const std::string& presetJson, const AlightImportReport& report);

/// Conferência da tabela de conversão contra o registro: chaves e ids de
/// parâmetro que não existem (vazio = tabela em dia). Para os testes.
[[nodiscard]] std::vector<std::string> alight_mapping_problems(const EffectRegistry& registry);

} // namespace presets
} // namespace aurea
