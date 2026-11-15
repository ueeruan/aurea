// Compilado COM exceções (AUREA_IMPORT_BOUNDARIES no CMakeLists): a falta de
// memória vira false aqui, longe dos arquivos -fno-exceptions que chamam.
#include "aurea/core/SafeAlloc.hpp"

#include <new>
#include <stdexcept>

namespace aurea {

bool try_resize_bytes(std::vector<u8>& v, usize bytes) noexcept {
    try {
        v.resize(bytes);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
}

bool try_assign_bytes(std::vector<u8>& dst, const u8* src, usize bytes) noexcept {
    if (!src && bytes) return false;
    try {
        std::vector<u8> copy(src, src + bytes);
        dst.swap(copy);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
}

}  // namespace aurea
