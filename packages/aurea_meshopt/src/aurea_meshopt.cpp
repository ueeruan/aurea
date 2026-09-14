#include "meshoptimizer.h"
#include <cstdint>
#include <cstring>
#include <vector>
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default"))) __attribute__((used))
#endif

// Every operation validates its inputs and writes to caller-owned memory.
// A failure (bad index, allocation) returns 0 and never leaves a partially
// modified mesh behind: in-place operations work on a copy first.

static bool indices_ok(const uint32_t* indices, uint32_t count, uint32_t vertices) {
  if (!indices || !count || count % 3 || !vertices) return false;
  for (uint32_t i = 0; i < count; ++i) if (indices[i] >= vertices) return false;
  return true;
}

// Separate output guarantees failure never changes the caller's mesh.
extern "C" API int aurea_meshopt_cache(uint32_t* indices, uint32_t count, uint32_t vertices) {
  if (!indices_ok(indices, count, vertices)) return 0;
  try {
    std::vector<uint32_t> result(count);
    meshopt_optimizeVertexCache(result.data(), indices, count, vertices);
    std::memcpy(indices, result.data(), size_t(count) * sizeof(uint32_t));
    return 1;
  } catch (...) { return 0; }
}

// WELD: vertices equal in EVERY stream (position, normal, uv, joints...)
// become one. Rewrites [indices] and fills [remap] (old -> new, ~0 for
// unused). Returns the new vertex count, 0 on failure.
extern "C" API uint32_t aurea_meshopt_weld(uint32_t* indices, uint32_t index_count,
                                           uint32_t vertex_count, const float** streams,
                                           const uint32_t* components, uint32_t stream_count,
                                           uint32_t* remap) {
  if (!indices_ok(indices, index_count, vertex_count) || !streams || !components ||
      !stream_count || !remap)
    return 0;
  try {
    std::vector<meshopt_Stream> s(stream_count);
    for (uint32_t i = 0; i < stream_count; ++i) {
      if (!streams[i] || !components[i] || components[i] > 64) return 0;
      s[i].data = streams[i];
      s[i].size = components[i] * sizeof(float);
      s[i].stride = components[i] * sizeof(float);
    }
    std::vector<uint32_t> table(vertex_count);
    size_t unique = meshopt_generateVertexRemapMulti(table.data(), indices, index_count,
                                                     vertex_count, s.data(), stream_count);
    std::vector<uint32_t> out(index_count);
    meshopt_remapIndexBuffer(out.data(), indices, index_count, table.data());
    std::memcpy(indices, out.data(), size_t(index_count) * sizeof(uint32_t));
    std::memcpy(remap, table.data(), size_t(vertex_count) * sizeof(uint32_t));
    return uint32_t(unique);
  } catch (...) { return 0; }
}

// VERTEX FETCH: vertices renumbered in the order the triangles use them
// (after the vertex cache pass this keeps memory access linear). Unused
// vertices are dropped. Returns the new vertex count, 0 on failure.
extern "C" API uint32_t aurea_meshopt_fetch(uint32_t* indices, uint32_t index_count,
                                            uint32_t vertex_count, uint32_t* remap) {
  if (!indices_ok(indices, index_count, vertex_count) || !remap) return 0;
  try {
    std::vector<uint32_t> table(vertex_count);
    size_t unique =
        meshopt_optimizeVertexFetchRemap(table.data(), indices, index_count, vertex_count);
    std::vector<uint32_t> out(index_count);
    meshopt_remapIndexBuffer(out.data(), indices, index_count, table.data());
    std::memcpy(indices, out.data(), size_t(index_count) * sizeof(uint32_t));
    std::memcpy(remap, table.data(), size_t(vertex_count) * sizeof(uint32_t));
    return uint32_t(unique);
  } catch (...) { return 0; }
}

// SIMPLIFY into [destination] (capacity index_count). With [normals] the
// simplifier also protects shading; [sloppy] collapses topology freely
// (for when the regular simplifier cannot reach the target). Returns the
// resulting index count (0 on failure) and the relative error.
extern "C" API uint32_t aurea_meshopt_simplify(const uint32_t* indices, uint32_t index_count,
                                               const float* positions, uint32_t vertex_count,
                                               const float* normals, uint32_t target_index_count,
                                               float target_error, uint32_t options,
                                               uint32_t sloppy, uint32_t* destination,
                                               float* result_error) {
  if (!indices_ok(indices, index_count, vertex_count) || !positions || !destination) return 0;
  try {
    float error = 0;
    size_t count;
    if (sloppy) {
      count = meshopt_simplifySloppy(destination, indices, index_count, positions, vertex_count,
                                     3 * sizeof(float), nullptr, target_index_count,
                                     target_error, &error);
    } else if (normals) {
      static const float weights[3] = {0.5f, 0.5f, 0.5f};
      count = meshopt_simplifyWithAttributes(destination, indices, index_count, positions,
                                             vertex_count, 3 * sizeof(float), normals,
                                             3 * sizeof(float), weights, 3, nullptr,
                                             target_index_count, target_error, options, &error);
    } else {
      count = meshopt_simplify(destination, indices, index_count, positions, vertex_count,
                               3 * sizeof(float), target_index_count, target_error, options,
                               &error);
    }
    if (result_error) *result_error = error;
    return uint32_t(count);
  } catch (...) { return 0; }
}

// DECODE glTF EXT_meshopt_compression / KHR_meshopt_compression data.
// mode: 0 ATTRIBUTES, 1 TRIANGLES, 2 INDICES. filter: 0 NONE,
// 1 OCTAHEDRAL, 2 QUATERNION, 3 EXPONENTIAL, 4 COLOR. Returns 1 on success.
extern "C" API int aurea_meshopt_decode(uint32_t mode, uint32_t filter, uint8_t* destination,
                                        uint32_t count, uint32_t stride, const uint8_t* buffer,
                                        uint32_t buffer_size) {
  if (!destination || !buffer || !count || !stride || stride > 256) return 0;
  try {
    int rc;
    switch (mode) {
      case 0: rc = meshopt_decodeVertexBuffer(destination, count, stride, buffer, buffer_size); break;
      case 1:
        if (stride != 2 && stride != 4) return 0;
        rc = meshopt_decodeIndexBuffer(destination, count, stride, buffer, buffer_size);
        break;
      case 2:
        if (stride != 2 && stride != 4) return 0;
        rc = meshopt_decodeIndexSequence(destination, count, stride, buffer, buffer_size);
        break;
      default: return 0;
    }
    if (rc != 0) return 0;
    if (mode == 0) {
      switch (filter) {
        case 0: break;
        case 1: meshopt_decodeFilterOct(destination, count, stride); break;
        case 2: meshopt_decodeFilterQuat(destination, count, stride); break;
        case 3: meshopt_decodeFilterExp(destination, count, stride); break;
        case 4: meshopt_decodeFilterColor(destination, count, stride); break;
        default: return 0;
      }
    }
    return 1;
  } catch (...) { return 0; }
}

// ENCODERS, the other half of the codec: used by tests to build a
// compressed glTF, and by tools. Return the encoded size (0 on failure).
extern "C" API uint32_t aurea_meshopt_encode_bound(uint32_t kind, uint32_t count, uint32_t extra) {
  return kind == 0 ? uint32_t(meshopt_encodeVertexBufferBound(count, extra))
                   : uint32_t(meshopt_encodeIndexBufferBound(count, extra));
}

extern "C" API uint32_t aurea_meshopt_encode_vertex(uint8_t* out, uint32_t out_size,
                                                    const uint8_t* vertices, uint32_t count,
                                                    uint32_t stride) {
  if (!out || !vertices || !count || !stride) return 0;
  try {
    return uint32_t(meshopt_encodeVertexBuffer(out, out_size, vertices, count, stride));
  } catch (...) { return 0; }
}

extern "C" API uint32_t aurea_meshopt_encode_index(uint8_t* out, uint32_t out_size,
                                                   const uint32_t* indices, uint32_t count) {
  if (!out || !indices || !count || count % 3) return 0;
  try {
    return uint32_t(meshopt_encodeIndexBuffer(out, out_size, indices, count));
  } catch (...) { return 0; }
}
