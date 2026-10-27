// Esqueleto humanoide canônico: mapeamento por nome e pose de repouso
// (ver rig/Humanoid.hpp).
#include "aurea/rig/Humanoid.hpp"

#include "aurea/core/Trackball.hpp"
#include "aurea/rig/Ik.hpp"
#include "aurea/scene3d/SceneAsset.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace aurea::rig {
namespace {

using B = HumanoidBone;

enum class Side : u8 { Center, Left, Right };

struct Parsed {
    Side side = Side::Center;
    std::string core;   ///< sem prefixo, sem lado, sem separadores, minúsculo
};

bool is_sep(char c) noexcept { return c == ' ' || c == '_' || c == '.' || c == '-' || c == ':' || c == '|'; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool starts_with(const std::string& s, const char* p) noexcept { return s.rfind(p, 0) == 0; }

Parsed parse_name(const std::string& original) {
    Parsed out;
    // Namespace / caminho: "mixamorig:LeftArm", "Armature|Hips".
    std::string name = original;
    const usize cut = name.find_last_of(":|");
    if (cut != std::string::npos && cut + 1 < name.size()) name = name.substr(cut + 1);
    // Prefixo "aurea:" já cortado acima. Lado colado em camelCase (DAZ: lHand, rShin).
    bool camelSide = false;
    if (name.size() >= 3 && (name[0] == 'l' || name[0] == 'r') && std::isupper(static_cast<unsigned char>(name[1]))) {
        out.side = name[0] == 'l' ? Side::Left : Side::Right;
        name = name.substr(1);
        camelSide = true;
    }
    std::string s = lower(name);
    static const char* const prefixes[] = {"bip001", "bip01", "bip02", "biped", "j_bip_", "def-", "def_", "org-",
                                           "mch-", "bn_", "bone_", "jnt_", "cc_base_", "rig_", "b_", "valvebiped.bip01"};
    for (const char* p : prefixes)
        if (starts_with(s, p)) { s = s.substr(std::strlen(p)); break; }
    // Fichas por separador.
    std::vector<std::string> tokens;
    std::string cur;
    for (const char c : s) {
        if (is_sep(c)) { if (!cur.empty()) tokens.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) tokens.push_back(cur);
    std::string core;
    for (const std::string& t : tokens) {
        if (!camelSide && (t == "l" || t == "left" || t == "lft" || t == "lf")) { out.side = Side::Left; continue; }
        if (!camelSide && (t == "r" || t == "right" || t == "rt" || t == "rgt")) { out.side = Side::Right; continue; }
        if (t == "c") continue;   // VRM: J_Bip_C_Hips
        core += t;
    }
    if (out.side == Side::Center) {
        if (const usize p = core.find("left"); p != std::string::npos) { out.side = Side::Left; core.erase(p, 4); }
        else if (const usize q = core.find("right"); q != std::string::npos) { out.side = Side::Right; core.erase(q, 5); }
    }
    out.core = core;
    return out;
}

bool any_of(const std::string& s, std::initializer_list<const char*> list) noexcept {
    for (const char* x : list) if (s == x) return true;
    return false;
}

bool spine_like(const std::string& core) noexcept {
    if (starts_with(core, "spine")) {
        for (usize i = 5; i < core.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(core[i]))) return false;
        return true;
    }
    return any_of(core, {"chest", "upperchest", "abdomen", "abdomenlower", "abdomenupper", "chestlower", "chestupper",
                         "torso", "back", "ribcage"});
}

/// Osso de membro pelo núcleo do nome (o lado vem à parte).
bool limb_bone(const std::string& core, Side side, B& out) noexcept {
    if (side == Side::Center) return false;
    const bool l = side == Side::Left;
    if (any_of(core, {"upperarm", "arm", "uparm", "shldrbend", "armupper", "upperarm01"}))
        { out = l ? B::LeftUpperArm : B::RightUpperArm; return true; }
    if (any_of(core, {"forearm", "lowerarm", "lowarm", "forearmbend", "elbow", "armlower", "forearm01"}))
        { out = l ? B::LeftLowerArm : B::RightLowerArm; return true; }
    if (any_of(core, {"hand", "wrist"})) { out = l ? B::LeftHand : B::RightHand; return true; }
    if (any_of(core, {"upleg", "thigh", "upperleg", "thighbend", "legupper", "hip", "thigh01"}))
        { out = l ? B::LeftUpperLeg : B::RightUpperLeg; return true; }
    if (any_of(core, {"leg", "calf", "shin", "lowerleg", "knee", "leglower", "calf01"}))
        { out = l ? B::LeftLowerLeg : B::RightLowerLeg; return true; }
    if (any_of(core, {"foot", "ankle"})) { out = l ? B::LeftFoot : B::RightFoot; return true; }
    return false;
}

i32 depth_of(const scene3d::SceneAsset& a, i32 n) noexcept {
    i32 d = 0;
    for (i32 p = a.nodes[static_cast<usize>(n)].parent; p >= 0 && d < 4096; p = a.nodes[static_cast<usize>(p)].parent) ++d;
    return d;
}

bool is_ancestor(const scene3d::SceneAsset& a, i32 anc, i32 n) noexcept {
    if (anc < 0 || n < 0) return false;
    i32 guard = 0;
    for (i32 p = a.nodes[static_cast<usize>(n)].parent; p >= 0 && guard < 4096; p = a.nodes[static_cast<usize>(p)].parent, ++guard)
        if (p == anc) return true;
    return false;
}

} // namespace

bool HumanoidMap::usable() const noexcept {
    static constexpr B need[] = {B::Hips, B::Head,
                                 B::LeftUpperArm, B::LeftLowerArm, B::LeftHand, B::RightUpperArm, B::RightLowerArm, B::RightHand,
                                 B::LeftUpperLeg, B::LeftLowerLeg, B::LeftFoot, B::RightUpperLeg, B::RightLowerLeg, B::RightFoot};
    for (const B b : need) if ((*this)[b] < 0) return false;
    return true;
}

bool bone_from_name(const std::string& name, HumanoidBone& out) noexcept {
    for (usize i = 0; i < kHumanoidBoneCount; ++i)
        if (name == bone_name(static_cast<B>(i))) { out = static_cast<B>(i); return true; }
    return false;
}

HumanoidMap map_by_names(const scene3d::SceneAsset& asset) {
    HumanoidMap map;
    const usize n = asset.nodes.size();
    // Candidatas: juntas das skins e nós do AutoRig.
    std::vector<u8> joint(n, 0);
    for (const scene3d::Skin& s : asset.skins)
        for (const i32 j : s.joints) if (j >= 0 && static_cast<usize>(j) < n) joint[static_cast<usize>(j)] = 1;
    const std::string prefix = kAutoRigNodePrefix;
    bool aureaRig = false;
    for (usize i = 0; i < n; ++i) {
        const std::string& nm = asset.nodes[i].name;
        if (nm.rfind(prefix, 0) != 0) continue;
        B b;
        if (bone_from_name(nm.substr(prefix.size()), b)) { map.joint[static_cast<usize>(b)] = static_cast<i32>(i); aureaRig = true; }
    }
    if (aureaRig) return map;
    std::vector<i32> spines;
    std::array<i32, kHumanoidBoneCount> depth{};
    depth.fill(1 << 30);
    auto offer = [&](B b, i32 node) {
        const i32 d = depth_of(asset, node);
        if (d < depth[static_cast<usize>(b)]) { depth[static_cast<usize>(b)] = d; map.joint[static_cast<usize>(b)] = node; }
    };
    for (usize i = 0; i < n; ++i) {
        if (!joint[i]) continue;
        const Parsed p = parse_name(asset.nodes[i].name);
        if (p.core.empty()) continue;
        // Pontas e auxiliares nunca são o osso (HeadTop_End, hand_twist, ik_foot…).
        if (p.core.find("end") != std::string::npos || p.core.find("twist") != std::string::npos
            || p.core.find("roll") != std::string::npos || p.core.find("ik") == 0 || p.core.find("pole") != std::string::npos
            || p.core.find("target") != std::string::npos || p.core.find("ctrl") != std::string::npos)
            continue;
        const i32 node = static_cast<i32>(i);
        B b;
        if (limb_bone(p.core, p.side, b)) { offer(b, node); continue; }
        if (p.side != Side::Center) continue;
        if (any_of(p.core, {"hips", "pelvis", "hip", "root_hips", "cog"})) { offer(B::Hips, node); continue; }
        if (any_of(p.core, {"neck", "neck1", "neck01", "neckbase", "necklower"})) { offer(B::Neck, node); continue; }
        if (any_of(p.core, {"head", "head1", "head01"})) { offer(B::Head, node); continue; }
        if (spine_like(p.core)) spines.push_back(node);
    }
    // Coluna e peito: os "spine" entre o quadril e o pescoço (ou os braços).
    const i32 hips = map[B::Hips];
    i32 top = map[B::Neck] >= 0 ? map[B::Neck] : map[B::LeftUpperArm] >= 0 ? map[B::LeftUpperArm] : map[B::Head];
    std::vector<i32> chain;
    for (const i32 s : spines)
        if ((hips < 0 || is_ancestor(asset, hips, s)) && (top < 0 || is_ancestor(asset, s, top))) chain.push_back(s);
    std::sort(chain.begin(), chain.end(), [&](i32 a, i32 b) { return depth_of(asset, a) < depth_of(asset, b); });
    if (!chain.empty()) map.joint[static_cast<usize>(B::Spine)] = chain.front();
    if (chain.size() >= 2) map.joint[static_cast<usize>(B::Chest)] = chain.back();
    // O quadril sem nome conhecido: o pai comum das coxas.
    if (map[B::Hips] < 0 && map[B::LeftUpperLeg] >= 0) {
        const i32 p = asset.nodes[static_cast<usize>(map[B::LeftUpperLeg])].parent;
        if (p >= 0 && is_ancestor(asset, p, map[B::RightUpperLeg])) map.joint[static_cast<usize>(B::Hips)] = p;
    }
    // Hierarquia: cada osso mapeado tem de descender do pai canônico mapeado.
    for (usize i = 1; i < kHumanoidBoneCount; ++i) {
        const i32 node = map.joint[i];
        if (node < 0) continue;
        B p = parent_of(static_cast<B>(i));
        while (p != B::Count && map[p] < 0) p = parent_of(p);
        if (p != B::Count && !is_ancestor(asset, map[p], node)) map.joint[i] = -1;
    }
    return map;
}

HumanoidRest rest_pose(const scene3d::SceneAsset& asset, const HumanoidMap& map) {
    HumanoidRest rest;
    rest.rotation.fill(Quat::identity());
    rest.localRotation.fill(Quat::identity());
    rest.parentRotation.fill(Quat::identity());
    const std::vector<Mat4> world = asset.rest_world_matrices();
    for (usize i = 0; i < kHumanoidBoneCount; ++i) {
        const i32 node = map.joint[i];
        if (node < 0 || static_cast<usize>(node) >= world.size()) continue;
        rest.present[i] = 1;
        rest.position[i] = matrix_position(world[static_cast<usize>(node)]);
        rest.rotation[i] = matrix_rotation(world[static_cast<usize>(node)]);
        rest.localRotation[i] = asset.nodes[static_cast<usize>(node)].rotation;
        const i32 parent = asset.nodes[static_cast<usize>(node)].parent;
        if (parent >= 0 && static_cast<usize>(parent) < world.size()) {
            rest.parentRotation[i] = matrix_rotation(world[static_cast<usize>(parent)]);
            if (static_cast<HumanoidBone>(i) == HumanoidBone::Hips) {
                Mat4 lin = world[static_cast<usize>(parent)];
                lin.col[3] = Vec4{0, 0, 0, 1};
                (void)inverse(lin, rest.hipsParentInverse);
            }
        }
    }
    if (asset.bounds.valid()) rest.height = asset.bounds.extent().y;
    if (!(rest.height > 0.0f) && rest.present[0] && rest.present[static_cast<usize>(B::Head)])
        rest.height = (rest.position[static_cast<usize>(B::Head)].y - rest.position[0].y) * 2.0f;
    return rest;
}

Quat offset_from_world_delta(const HumanoidRest& rest, HumanoidBone b, Quat deltaParent, Quat deltaBone) noexcept {
    if (b >= HumanoidBone::Count) return Quat::identity();
    const Quat r = rest.rotation[static_cast<usize>(b)];
    return (conj(r) * conj(deltaParent) * deltaBone * r).normalized();
}

Vec3 hips_local_translation(const HumanoidRest& rest, Vec3 modelDelta) noexcept {
    const Vec4 v = rest.hipsParentInverse * Vec4{modelDelta, 0.0f};
    return Vec3{v.x, v.y, v.z};
}

Vec3 euler_deg_from_quat(Quat q, Vec3 nearDeg) noexcept {
    return trackball::euler_deg_from_quat(q.normalized(), nearDeg);
}

} // namespace aurea::rig
