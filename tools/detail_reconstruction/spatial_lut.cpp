// Experimental 2x luma residual. The caller supplies the bicubic base and the
// fitted coefficients. No platform API, allocation or persistent frame state.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#if defined(_WIN32)
#define AUREA_LUT_EXPORT __declspec(dllexport)
#else
#define AUREA_LUT_EXPORT __attribute__((visibility("default")))
#endif

namespace {
constexpr int kFeatures = 24;
constexpr int kPhases = 4;

int filter_index(float gx, float gy) {
    const float ax = std::abs(gx), ay = std::abs(gy);
    constexpr float diagonal = 0.41421356f;
    const int direction = ay < diagonal * ax ? 0 : ax < diagonal * ay ? 1 : gx * gy >= 0.0f ? 2 : 3;
    const float gradient = std::max(ax, ay);
    const int strength = gradient < 6.0f / 255 ? 0 : gradient < 27.0f / 255 ? 1 : 2;
    return direction * 3 + strength;
}
} // namespace

extern "C" AUREA_LUT_EXPORT void aurea_detail_lut_2x(const float* low, const float* base, float* out,
                                                     int width, int height, const float* filters) {
    const std::ptrdiff_t pitch = static_cast<std::ptrdiff_t>(width) * 2;
    for (int y = 0; y < height; ++y) {
        std::array<const float*, 5> rows;
        for (int i = 0; i < 5; ++i)
            rows[i] = low + static_cast<std::ptrdiff_t>(std::clamp(y + i - 2, 0, height - 1)) * width;
        for (int x = 0; x < width; ++x) {
            const std::ptrdiff_t pixel = static_cast<std::ptrdiff_t>(y) * 2 * pitch + x * 2;
            const int left = std::max(x - 1, 0), right = std::min(x + 1, width - 1);
            const float gx = (rows[1][right] - rows[1][left] + rows[2][right] - rows[2][left] +
                              rows[3][right] - rows[3][left]) / 3.0f;
            const float gy = (rows[3][left] - rows[1][left] + rows[3][x] - rows[1][x] +
                              rows[3][right] - rows[1][right]) / 3.0f;
            const float* weights = filters + filter_index(gx, gy) * kFeatures * kPhases;
            const float center = rows[2][x];
            std::array<float, kFeatures> patch;
            int f = 0;
            for (int j = 0; j < 5; ++j)
                for (int i = 0; i < 5; ++i)
                    if (j != 2 || i != 2)
                        patch[f++] = rows[j][std::clamp(x + i - 2, 0, width - 1)] - center;
            for (int phase = 0; phase < kPhases; ++phase) {
                float residual = 0;
                for (int i = 0; i < kFeatures; ++i) residual += patch[i] * weights[phase * kFeatures + i];
                const std::ptrdiff_t dst = pixel + (phase / 2) * pitch + phase % 2;
                out[dst] = std::clamp(base[dst] + std::clamp(residual, -0.08f, 0.08f), 0.0f, 1.0f);
            }
        }
    }
}
