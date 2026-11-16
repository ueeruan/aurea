// =============================================================================
//  Aurea / engine / EngineMcpTools.hpp  (interno do motor, não é API pública)
//
//  AureaMCP fase C (docs/aureamcp/DESIGN.md §2.2): as ferramentas que deixam um
//  agente LER e ESCREVER com precisão, sem depender só do run_ae_script —
//  detalhe da camada, keyframes de uma propriedade, valor/keyframe/easing,
//  efeitos (lista, pôr, tirar, ordem, parâmetros), presets, fontes e estilo do
//  texto, marcas e área de trabalho. Os esquemas JSON moram na tabela de
//  EngineAssistant.cpp (tools_json); `Engine::assistant_call` despacha para cá
//  o que não é dele. Mesma regra nas duas plataformas: as interfaces só
//  repassam o nome e os argumentos.
//
//  Leitura: sob o `modelMutex_` do motor (passado por quem chama, que é membro
//  do Engine). Escrita: só por comandos (`apply_command`, com desfazer) e pela
//  API pública do motor — nunca com o mutex preso (ela o pega sozinha).
// =============================================================================
#pragma once

#include <mutex>
#include <string>

namespace aurea {
class Engine;
namespace json { struct Value; }

namespace mcp_tools {

/// Executa a ferramenta `name`. Devolve false quando o nome não é desta tabela
/// (quem chama segue com as dele); true com o JSON de resposta em `out`
/// ({"ok":true,...} ou {"ok":false,"error":"..."}).
bool call(Engine& engine, std::mutex& model, const std::string& name, const json::Value& args, std::string& out) noexcept;

} // namespace mcp_tools
} // namespace aurea
