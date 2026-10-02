#include "aurea/effects/CubeLut.hpp"
#include "aurea/project/FileIO.hpp"
#include <sstream>
#include <locale>
#include <cmath>

namespace aurea {
Result<CubeLut> parse_cube_lut(std::string_view text) {
    if (text.empty() || text.size() > kMaxCubeFileBytes) return Status{Errc::InvalidArgument, "LUT vazio ou maior que 32 MB"};
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    CubeLut out;
    std::istringstream file{std::string(text)};
    file.imbue(std::locale::classic());
    std::string line;
    bool data = false, minimum = false, maximum = false;
    usize expected = 0;
    auto finite = [](f32 v) { return std::isfinite(v) && std::abs(v) <= 65504.f; };
    while (std::getline(file, line)) {
        if (line.size() > 4096) return Status{Errc::InvalidArgument, "linha do LUT longa demais"};
        const usize comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream row(line); row.imbue(std::locale::classic());
        std::string token; if (!(row >> token)) continue;
        if (token == "TITLE" && !data) {
            std::getline(row, out.title); continue;
        }
        if (token == "LUT_3D_SIZE" || token == "LUT_1D_SIZE") {
            if (data || out.size || !(row >> out.size)) return Status{Errc::UnsupportedFormat, "LUT combinado ou tamanho invalido"};
            out.dimensions = token == "LUT_1D_SIZE" ? 1u : 3u;
            if (out.size < 2 || out.size > (out.dimensions == 1 ? 65536u : 65u))
                return Status{Errc::UnsupportedFormat, "LUT 3D suporta 2 a 65 pontos; 1D, 2 a 65536"};
            expected = out.dimensions == 1 ? out.size : usize(out.size) * out.size * out.size;
            out.values.reserve(expected);
        } else if (token == "DOMAIN_MIN" || token == "DOMAIN_MAX") {
            const bool isMin = token == "DOMAIN_MIN";
            bool& found = isMin ? minimum : maximum;
            Vec3& v = isMin ? out.domainMin : out.domainMax;
            if (data || found || !(row >> v.x >> v.y >> v.z) || !finite(v.x) || !finite(v.y) || !finite(v.z))
                return Status{Errc::InvalidArgument, "dominio do LUT invalido"};
            found = true;
        } else if (token == "LUT_3D_INPUT_RANGE" || token == "LUT_1D_INPUT_RANGE") {
            f32 lo = 0, hi = 0;
            if (data || minimum || maximum || !(row >> lo >> hi) || !finite(lo) || !finite(hi))
                return Status{Errc::InvalidArgument, "dominio do LUT invalido"};
            out.domainMin = {lo, lo, lo}; out.domainMax = {hi, hi, hi}; minimum = maximum = true;
        } else {
            row.clear(); row.str(line);
            Vec4 v{0, 0, 0, 1};
            if (!out.size || out.values.size() >= expected || !(row >> v.x >> v.y >> v.z)
                || !finite(v.x) || !finite(v.y) || !finite(v.z))
                return Status{Errc::InvalidArgument, "dados do LUT invalidos"};
            out.values.push_back(v); data = true;
        }
        if (row >> token) return Status{Errc::InvalidArgument, "colunas inesperadas no LUT"};
    }
    if (!expected || out.values.size() != expected) return Status{Errc::InvalidArgument, "LUT incompleto"};
    if (!(out.domainMax.x > out.domainMin.x && out.domainMax.y > out.domainMin.y && out.domainMax.z > out.domainMin.z))
        return Status{Errc::InvalidArgument, "dominio do LUT sem intervalo"};
    return out;
}
Result<CubeLut> read_cube_lut(const std::string& path) {
    std::vector<u8> bytes;
    if (!fileio::read_all(path, bytes, kMaxCubeFileBytes)) return Status{Errc::IoError, "nao foi possivel ler o LUT (limite 32 MB)"};
    return parse_cube_lut(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
}
