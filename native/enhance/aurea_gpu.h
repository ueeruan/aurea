/*
 * UMA instancia Vulkan do ncnn para a biblioteca inteira.
 *
 * O aprimoramento (Real-ESRGAN) e a interpolacao (RIFE) moram na mesma
 * libaurea_enhance e contam usuarios no mesmo lugar: um motor destruir a
 * instancia enquanto o outro ainda roda derrubaria o processo. Funcoes
 * inline com estaticas locais tem uma unica copia no programa (C++17).
 */
#ifndef AUREA_GPU_H
#define AUREA_GPU_H

#include <mutex>

#include "gpu.h"

namespace aurea_gpu {

inline std::mutex& trava() {
  static std::mutex m;
  return m;
}

inline int& usuarios() {
  static int n = 0;
  return n;
}

/* Dispositivo Vulkan padrao, contando um usuario; nulo sem GPU (e entao
 * nada fica contado). Cada retorno nao nulo pede um liberar(). */
inline ncnn::VulkanDevice* adquirir(int* indice = nullptr) {
  std::lock_guard<std::mutex> g(trava());
  if (usuarios() == 0) ncnn::create_gpu_instance();
  ncnn::VulkanDevice* dev = nullptr;
  if (ncnn::get_gpu_count() > 0) {
    const int i = ncnn::get_default_gpu_index();
    dev = ncnn::get_gpu_device(i);
    if (dev && indice) *indice = i;
  }
  if (dev) {
    ++usuarios();
  } else if (usuarios() == 0) {
    ncnn::destroy_gpu_instance();
  }
  return dev;
}

inline void liberar() {
  std::lock_guard<std::mutex> g(trava());
  if (usuarios() > 0 && --usuarios() == 0) ncnn::destroy_gpu_instance();
}

}  // namespace aurea_gpu

#endif
