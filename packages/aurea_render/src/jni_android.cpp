// A PORTA JNI — COMO A JANELA NATIVA CHEGA AO C++.
//
// ================================ POR QUE JNI, E NAO FFI ==============
// O `ANativeWindow` nasce de um `jobject` (`android.view.Surface`), e
// `ANativeWindow_fromSurface` exige um `JNIEnv*`. O `dart:ffi` NAO TEM
// COMO FORNECER UM: nao ha handle de JVM no mundo do Dart, e forjar um
// seria mentir para a JVM.
//
// Entao a divisao e esta, e ela e a correta: a JANELA atravessa por JNI
// (uma vez, quando a superficie nasce), e TUDO O RESTO — apresentar
// quadro, redimensionar, ler estado — atravessa por FFI, no caminho
// quente, onde o custo importa. O que passa por JNI e o que so acontece
// quando a janela muda.
//
// ================================ O REFERENCIAO DA JANELA =============
// `ANativeWindow_fromSurface` TOMA UMA REFERENCIA. A superficie Vulkan
// toma a dela ao ser criada. Assim que `anexar` volta, a referencia desta
// funcao e solta — se ela ficasse, a janela nunca seria liberada e cada
// ida ao segundo plano vazaria uma. A ORDEM E OBRIGATORIA: criar a
// superficie PRIMEIRO, soltar DEPOIS.
#include "vulkan_superficie.h"

#if defined(__ANDROID__)

#include <jni.h>

#include <cstdio>
#include <string>

#include <android/log.h>
#include <android/native_window_jni.h>

namespace {
constexpr const char* kEtiqueta = "AureaVulkan";
}  // namespace

extern "C" {

/// ANEXA A JANELA DO `SurfaceProducer`. Devolve 1 quando deu certo.
JNIEXPORT jint JNICALL
Java_com_aurea_aurea_RenderNativo_anexarNativo(JNIEnv* env, jclass /*classe*/,
                                         jobject superficie, jint largura,
                                         jint altura) {
  // NENHUMA EXCECAO ATRAVES DO JNI: uma excecao C++ que chega do outro
  // lado nao vira erro do Kotlin, e o processo morre sem mensagem. Mesma
  // regra do `api.cpp`, pelo mesmo motivo.
  try {
    if (env == nullptr || superficie == nullptr) return 0;
    ANativeWindow* janela = ANativeWindow_fromSurface(env, superficie);
    if (janela == nullptr) return 0;

    const std::string erro = aurea::render::PreviewVulkan::instancia().anexar(
        janela, static_cast<std::uint32_t>(largura),
        static_cast<std::uint32_t>(altura));

    // A SUPERFICIE VULKAN JA SEGUROU A DELA (ou nao criou nada, e ai a
    // nossa e a unica — e soltar aqui e o certo nos dois casos).
    ANativeWindow_release(janela);

    if (!erro.empty()) {
      __android_log_print(ANDROID_LOG_WARN, kEtiqueta, "anexar: %s",
                          erro.c_str());
      return 0;
    }
    return 1;
  } catch (...) {
    return 0;
  }
}

JNIEXPORT void JNICALL
Java_com_aurea_aurea_RenderNativo_desanexarNativo(JNIEnv* /*env*/,
                                            jclass /*classe*/) {
  try {
    aurea::render::PreviewVulkan::instancia().desanexar();
  } catch (...) {
  }
}

/// O ESTADO EM TEXTO, para o Kotlin poder registrar o motivo no log do
/// sistema sem passar pelo Dart — e o caminho que existe quando o Dart
/// ainda nem subiu.
JNIEXPORT jstring JNICALL
Java_com_aurea_aurea_RenderNativo_estadoNativo(JNIEnv* env, jclass /*classe*/) {
  try {
    const auto& p = aurea::render::PreviewVulkan::instancia();
    const auto e = p.estatisticas();
    char texto[256];
    std::snprintf(texto, sizeof(texto),
                  "estado=%d motivo=%s %ux%u formato=%u imagens=%u "
                  "apresentados=%u recriacoes=%u out_of_date=%u suboptimal=%u "
                  "falhas=%u descartados=%u",
                  p.estado(), p.motivo().c_str(), e.largura, e.altura,
                  e.formato, e.imagens, e.quadros_apresentados, e.recriacoes,
                  e.out_of_date, e.suboptimal, e.falhas, e.quadros_descartados);
    return env->NewStringUTF(texto);
  } catch (...) {
    return env->NewStringUTF("estado=erro");
  }
}

}  // extern "C"

#endif  // __ANDROID__
