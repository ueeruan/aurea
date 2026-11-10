#include "aurea/vector/ContourTrace.hpp"

namespace aurea::vector {

/// Bézier fechada de uma volta: corta nos cantos vivos (ângulo > 35° numa janela
/// de `win` px) e ajusta cada trecho entre cantos; sem cantos, uma curva só.
BezierPath fit_loop(const Loop& raw, f32 win, f32 error) {
    const usize n = raw.size();
    std::vector<f32> s(n + 1, 0.0f);   // comprimento acumulado (volta fechada)
    for (usize i = 1; i <= n; ++i) s[i] = s[i - 1] + (raw[i % n] - raw[i - 1]).length();
    const f32 total = s[n];
    BezierPath out;
    out.closed = true;
    if (total <= 0.0f) return out;
    auto along = [&](usize i, f32 d) {
        // O ponto a `d` px (com sinal) de i, andando pela volta.
        usize k = i;
        f32 acc = 0.0f;
        for (usize step = 0; step < n; ++step) {
            const usize nk = d > 0.0f ? (k + 1) % n : (k + n - 1) % n;
            acc += (raw[nk] - raw[k]).length();
            k = nk;
            if (acc >= std::fabs(d)) break;
        }
        return raw[k];
    };
    std::vector<f32> angle(n, 0.0f);
    for (usize i = 0; i < n; ++i) {
        const Vec2 u = raw[i] - along(i, -win), w = along(i, win) - raw[i];
        const f32 lu = u.length(), lw = w.length();
        if (lu < 1e-6f || lw < 1e-6f) continue;
        const f32 c = std::clamp(u.dot(w) / (lu * lw), -1.0f, 1.0f);
        angle[i] = std::acos(c);
    }
    constexpr f32 kCorner = 35.0f * 3.14159265f / 180.0f;
    std::vector<usize> corners;
    for (usize i = 0; i < n; ++i) {
        if (angle[i] < kCorner) continue;
        // Máximo local na janela (o canto é um ponto só).
        // Empate (platô): fica o primeiro do platô.
        bool best = true;
        f32 d = 0.0f;
        for (usize k = (i + 1) % n; best && d < win && k != i; k = (k + 1) % n) {
            d += (raw[k] - raw[(k + n - 1) % n]).length();
            if (angle[k] > angle[i]) best = false;
        }
        d = 0.0f;
        for (usize k = (i + n - 1) % n; best && d < win && k != i; k = (k + n - 1) % n) {
            d += (raw[(k + 1) % n] - raw[k]).length();
            if (angle[k] >= angle[i]) best = false;
        }
        if (best) corners.push_back(i);
    }
    if (corners.empty()) {
        Loop closed = raw;
        closed.push_back(raw.front());
        BezierPath p = vector::fit_curve(closed, error, true);
        p.closed = true;
        return p;
    }
    for (usize c = 0; c < corners.size(); ++c) {
        const usize a = corners[c], b = corners[(c + 1) % corners.size()];
        Loop piece;
        for (usize k = a;; k = (k + 1) % n) {
            piece.push_back(raw[k]);
            if (k == b && piece.size() > 1) break;
            if (piece.size() > n + 1) break;
        }
        BezierPath part = vector::fit_curve(piece, error, false);
        if (part.v.size() < 2) continue;
        // O ajuste pode largar o último ponto (passo mínimo): os cantos ficam exatos.
        part.v.front().p = piece.front();
        part.v.back().p = piece.back();
        if (out.v.empty()) {
            out.v = part.v;
        } else {
            out.v.back().out = part.v.front().out;   // o canto: alças independentes
            out.v.insert(out.v.end(), part.v.begin() + 1, part.v.end());
        }
    }
    if (out.v.size() > 1) {
        // O último vértice é o primeiro canto: funde.
        out.v.front().in = out.v.back().in;
        out.v.pop_back();
    }
    return out;
}

bool trace_alpha(const u8* pixels, u32 w, u32 h, const AlphaTraceOptions& options, std::vector<Contour>& out) {
    out.clear();
    if (!pixels || w == 0 || h == 0 || options.stride == 0) return false;
    const usize count = static_cast<usize>(w) * h;
    std::vector<f32> alpha(count), scratch(count);
    for (usize i = 0; i < count; ++i) alpha[i] = static_cast<f32>(pixels[i * options.stride + options.alphaOffset]) / 255.0f;
    // Duas caixas: tira o serrilhado e o ruído do alfa, que derrubariam o chanfro do grupo inteiro.
    const u32 side = std::max(w, h);
    const int radius = std::max(1, static_cast<int>(side / 900u));
    const auto box = [&](bool horizontal) {
        const int n = horizontal ? static_cast<int>(w) : static_cast<int>(h), lines = horizontal ? static_cast<int>(h) : static_cast<int>(w);
        const auto index = [&](int line, int k) { return horizontal ? static_cast<usize>(line) * w + static_cast<usize>(k) : static_cast<usize>(k) * w + static_cast<usize>(line); };
        for (int line = 0; line < lines; ++line) {
            f32 sum = 0.0f;
            for (int k = -radius; k <= radius; ++k) sum += alpha[index(line, std::clamp(k, 0, n - 1))];
            for (int k = 0; k < n; ++k) {
                scratch[index(line, k)] = sum / static_cast<f32>(2 * radius + 1);
                sum += alpha[index(line, std::min(k + radius + 1, n - 1))] - alpha[index(line, std::max(k - radius, 0))];
            }
        }
        alpha.swap(scratch);
    };
    for (int pass = 0; pass < 2; ++pass) { box(true); box(false); }

    const f32 threshold = std::isfinite(options.threshold) ? std::clamp(options.threshold, 0.05f, 0.95f) : 0.5f;
    const f32 cs = std::max(1.0f, static_cast<f32>(side) / 1400.0f);
    const f32 cx = static_cast<f32>(w) * 0.5f, cy = static_cast<f32>(h) * 0.5f;
    const auto sd = [&](Vec2 q) {
        const f32 x = q.x + cx - 0.5f, y = q.y + cy - 0.5f;   // centro do texel
        if (x < 0.0f || y < 0.0f || x > static_cast<f32>(w - 1) || y > static_cast<f32>(h - 1)) return 1.0f;
        const u32 x0 = std::min(static_cast<u32>(x), w - 1), y0 = std::min(static_cast<u32>(y), h - 1);
        const u32 x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const f32 fx = x - static_cast<f32>(x0), fy = y - static_cast<f32>(y0);
        const auto at = [&](u32 i, u32 j) { return alpha[static_cast<usize>(j) * w + i]; };
        const f32 a = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx, b = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
        return threshold - (a + (b - a) * fy);
    };
    std::vector<Loop> loops = trace_zero(sd, Vec2{cx + 3.0f * cs, cy + 3.0f * cs}, cs);
    if (loops.empty()) return false;
    std::sort(loops.begin(), loops.end(), [](const Loop& l, const Loop& r) { return std::fabs(loop_area(l)) > std::fabs(loop_area(r)); });
    const f32 minArea = static_cast<f32>(count) * 0.00015f;
    while (loops.size() > 1 && (loops.size() > options.maxRings || std::fabs(loop_area(loops.back())) < minArea)) loops.pop_back();

    std::vector<BezierPath> paths;
    paths.reserve(loops.size());
    for (const Loop& loop : loops) paths.push_back(fit_loop(loop, cs * 3.0f, std::max(0.2f, cs * 0.5f)));
    f32 tolerance = 0.25f * cs;
    std::vector<Contour> flat;
    for (int attempt = 0; attempt < 8; ++attempt, tolerance *= 1.7f) {
        flat.clear();
        usize points = 0;
        for (const BezierPath& path : paths) {
            if (path.v.size() < 2) continue;
            Contour contour;
            flatten(path, tolerance, contour);
            std::vector<Vec2> clean;
            clean.reserve(contour.pts.size());
            for (Vec2 v : contour.pts) if (clean.empty() || (v - clean.back()).length() > 1e-3f) clean.push_back(v);
            while (clean.size() > 1 && (clean.front() - clean.back()).length() <= 1e-3f) clean.pop_back();
            if (clean.size() < 3) continue;
            contour.pts = std::move(clean);
            contour.closed = true;
            points += contour.pts.size();
            flat.push_back(std::move(contour));
        }
        if (points <= options.maxPoints) break;
    }
    if (flat.empty()) return false;
    for (Contour& contour : flat) for (Vec2& v : contour.pts) v = Vec2{v.x + cx, v.y + cy};
    resolve_fill(flat, FillRule::EvenOdd, out);
    if (out.empty()) out = std::move(flat);   // só o limpador recusou: o contorno cru ainda vale
    return !out.empty();
}

} // namespace aurea::vector
