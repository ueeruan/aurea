// =============================================================================
//  Aurea / assistant / Assistant.hpp
//
//  0.4.6: o ASSISTENTE de IA do editor. O modelo (OpenRouter, chave do
//  usuário) conversa nas interfaces; o que ele pode FAZER mora aqui, igual nas
//  duas plataformas: a lista de ferramentas (formato "tools" da API da
//  OpenAI) e as instruções de sistema. Cada chamada de ferramenta vira
//  `Engine::assistant_call(nome, argumentos JSON)`, que devolve JSON.
//
//  A ferramenta principal é `run_ae_script`: ExtendScript do After Effects no
//  interpretador com sandbox do motor (AeScript/AeScriptDom) — camadas,
//  propriedades, keyframes, efeitos, máscaras, expressões e texto, cada
//  execução UM passo de desfazer. As outras leem o projeto e cobrem o que o
//  script não faz (desfazer, cabeçote, seleção, vistas, abrir painéis).
// =============================================================================
#pragma once

#include <string>

namespace aurea::assistant {

/// As ferramentas (array JSON "tools" da API de chat da OpenAI/OpenRouter).
[[nodiscard]] std::string tools_json();

/// Instruções de sistema. `language` = idioma do app ("pt-BR", "en"...): o
/// assistente responde nele. `appVersion` vai junto para o modelo saber o que existe.
[[nodiscard]] std::string system_prompt(const std::string& language, const std::string& appVersion);

/// Ferramentas que as INTERFACES executam (o motor só valida e devolve
/// {"ok":true,"ui":"<nome>",...}): hoje, "open_panel".
inline constexpr const char* kUiOpenPanel = "open_panel";

} // namespace aurea::assistant
