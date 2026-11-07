#pragma once

// Os nomes que o motor dá ao que ele cria (camadas, máscaras, grupos). A interface
// passa os textos no idioma do app; sem isso ficam os de sempre, em português.
#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace aurea {

enum class DefaultName : unsigned {
    Text, Text3D, Particles, Particles3D, Captions, Tracking, TrackedCamera, Drawing, Freehand,
    ReflectiveFloor, Mask, Animator, Path, Stroke, Outline, Frame, Group, Rectangle, Ellipse,
    Polygon, Star, Count
};

namespace detail {
inline std::mutex& default_names_mutex() { static std::mutex m; return m; }
inline std::array<std::string, static_cast<std::size_t>(DefaultName::Count)>& default_names() {
    static std::array<std::string, static_cast<std::size_t>(DefaultName::Count)> names{
        "Texto", "Texto 3D", "Partículas", "Partículas 3D", "Legendas", "Rastreio", "Câmera rastreada",
        "Desenho", "Desenho à mão livre", "Reflective Floor", "Mascara", "Animador", "Caminho", "Traço",
        "Contorno", "Quadro", "Grupo", "Retângulo", "Elipse", "Polígono", "Estrela"};
    return names;
}
}  // namespace detail

inline std::string default_name(DefaultName id) {
    std::lock_guard<std::mutex> lock(detail::default_names_mutex());
    return detail::default_names()[static_cast<std::size_t>(id)];
}

/// Uma lista na ordem de DefaultName; lista de outro tamanho é ignorada.
inline void set_default_names(const std::vector<std::string>& names) {
    if (names.size() != static_cast<std::size_t>(DefaultName::Count)) return;
    std::lock_guard<std::mutex> lock(detail::default_names_mutex());
    for (std::size_t i = 0; i < names.size(); ++i) if (!names[i].empty()) detail::default_names()[i] = names[i];
}

}  // namespace aurea
