// Sonda de modelo externo: importa o arquivo de AUREA_PROBE_MODEL pela fachada
// (o mesmo caminho do celular) e imprime o resultado. Sem a variável, não faz
// nada — serve para reproduzir no host o modelo que um usuário mandou.
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aurea;

AUREA_TEST(ModelProbe, ImportsTheFileFromTheEnvironment) {
    const char* path = std::getenv("AUREA_PROBE_MODEL");
    if (!path || !*path) return;
    const char* budget = std::getenv("AUREA_PROBE_BUDGET_MB");
    EngineConfig cfg;
    cfg.workerCount = 2;
    cfg.memoryBudgetBytes = (budget ? std::strtoull(budget, nullptr, 10) : 1024ull) * 1024 * 1024;
    cfg.disableAutosave = true;
    Engine e;
    AUREA_CHECK(e.initialize(cfg).ok());
    AUREA_CHECK(e.new_project(1080, 1920, 30, "probe").ok());
    ModelImport request;
    request.path = path;
    const auto imported = e.import_model(request);
    std::printf("\n  modelo: %s\n  resultado: %s (%s)\n", path, imported.ok() ? "ok" : "falhou",
                imported.ok() ? "" : imported.status().message().data());
    AUREA_CHECK(imported.ok());
}
