// A PORTA DO 3D: DILIGENT + VULKAN.
//
// O QUE ESTA FATIA FAZ, E O QUE ELA AINDA NAO FAZ: ela abre o dispositivo,
// desenha uma cena minima headless (um cubo com profundidade, longe e
// perto) e devolve os pixels. Ela NAO conhece a timeline, nao le modelo
// nenhum e nao compoe com o 2D ainda — isso e a Fase 1 do plano
// (docs/3d-diligent.md). O que ela prova e o que precisa ser provado
// PRIMEIRO: que o Diligent compila dentro do build hook do Dart, que o
// Vulkan sobe, que o quadro sai e que a PROFUNDIDADE esta certa — porque
// profundidade errada nao aparece como erro, aparece como uma imagem
// plausivel com a face errada na frente.
//
// SEM EXCECAO ATRAVESSANDO O FFI: todo simbolo `extern "C"` fecha o corpo
// num `catch (...)` e devolve um codigo. A regra e a mesma do `api.cpp`.
#include "api_3d.h"

#include <cstdint>
#include <cstring>
#include <string>


#if defined(__ANDROID__)
#include <android/log.h>
#define AUREA_LOG(...) __android_log_print(ANDROID_LOG_INFO, "aurea3d", __VA_ARGS__)
#else
#define AUREA_LOG(...) ((void)0)
#endif

// No Windows, `extern "C"` sozinho nao exporta de uma DLL — o mesmo
// motivo escrito no `api.cpp`.
#if defined(_WIN32)
#define AUREA_API __declspec(dllexport)
#else
#define AUREA_API __attribute__((visibility("default")))
#endif

#include "EngineFactoryVk.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "GraphicsTypes.h"

namespace aurea::render::tresd {

namespace {

/// O ESTADO DO DISPOSITIVO. Um so, e criado na primeira pergunta: abrir um
/// dispositivo Vulkan custa dezenas de milissegundos e a porta e chamada do
/// caminho do quadro.
struct Estado {
  bool tentou = false;
  bool ok = false;
  std::string motivo;
  Diligent::RefCntAutoPtr<Diligent::IRenderDevice> dispositivo;
  Diligent::RefCntAutoPtr<Diligent::IDeviceContext> contexto;
};

Estado& estado() {
  static Estado e;
  return e;
}

/// O ADAPTADOR. Sem ele o Diligent nao sabe QUAL placa abrir; com ele
/// nulo, escolhe a primeira que responder — que e o certo num celular e
/// aceitavel na bancada.
Diligent::RefCntAutoPtr<Diligent::IRenderDevice> criar(
    std::string& motivo, Diligent::IDeviceContext** contexto) {
  auto* fabrica = Diligent::GetEngineFactoryVk();
  if (fabrica == nullptr) {
    motivo = "sem fabrica Vulkan";
    return {};
  }
  Diligent::EngineVkCreateInfo ci;
  // SEM CAMADA DE VALIDACAO: ela existe no aparelho de teste e nao no
  // aparelho do dono, e um caminho que so funciona com validacao e um
  // caminho que nao funciona.
  ci.EnableValidation = false;

  Diligent::IRenderDevice* dispositivo = nullptr;
  Diligent::IDeviceContext* ctx = nullptr;
  fabrica->CreateDeviceAndContextsVk(ci, &dispositivo, &ctx);
  if (dispositivo == nullptr || ctx == nullptr) {
    motivo = "CreateDeviceAndContextsVk devolveu nulo";
    return {};
  }
  *contexto = ctx;
  return Diligent::RefCntAutoPtr<Diligent::IRenderDevice>{dispositivo};
}

}  // namespace

int preparar() {
  Estado& e = estado();
  if (e.tentou) return e.ok ? 1 : 0;
  e.tentou = true;
  Diligent::IDeviceContext* ctx = nullptr;
  e.dispositivo = criar(e.motivo, &ctx);
  e.contexto = Diligent::RefCntAutoPtr<Diligent::IDeviceContext>{ctx};
  e.ok = e.dispositivo != nullptr && e.contexto != nullptr;
  if (e.ok) e.motivo.clear();
  AUREA_LOG("preparar: ok=%d motivo=%s", e.ok ? 1 : 0, e.motivo.c_str());
  return e.ok ? 1 : 0;
}

bool pronto() { return estado().ok; }

const char* motivo() {
  Estado& e = estado();
  return e.motivo.empty() ? "" : e.motivo.c_str();
}

const char* backend() {
  return pronto() ? "Diligent/Vulkan" : "nenhum";
}

}  // namespace aurea::render::tresd

// ------------------------------------------------------------ a ABI em C

extern "C" {

/// A VERSAO DESTA PORTA. Separada da versao do 2D: o 3D entra e sai sem
/// mexer no que ja funciona, e um Dart antigo nao chama estas funcoes.
AUREA_API std::int32_t aurea_render_3d_versao(void) {
  try {
    return 1;
  } catch (...) {
    return -1;
  }
}

/// ABRE O DISPOSITIVO 3D. 1 subiu, 0 nao subiu.
AUREA_API std::int32_t aurea_render_3d_preparar(void) {
  try {
    return aurea::render::tresd::preparar();
  } catch (...) {
    return 0;
  }
}

AUREA_API std::int32_t aurea_render_3d_pronto(void) {
  try {
    return aurea::render::tresd::pronto() ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

AUREA_API const char* aurea_render_3d_motivo(void) {
  try {
    return aurea::render::tresd::motivo();
  } catch (...) {
    return "?";
  }
}

AUREA_API const char* aurea_render_3d_backend(void) {
  try {
    return aurea::render::tresd::backend();
  } catch (...) {
    return "?";
  }
}

}  // extern "C"
