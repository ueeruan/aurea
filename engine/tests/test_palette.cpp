#include "TestFramework.hpp"
#include "aurea/effects/Palette.hpp"
#include <array>

using namespace aurea;

AUREA_TEST(Palette, GimpPaletteReadsColorsAndSkipsHeaderAndDuplicates) {
    const auto colors = parse_gpl_palette("GIMP Palette\nName: Teste\nColumns: 4\n# comentario\n"
                                          "  0   0   0\tPreto\n255 255 255 Branco\n255 255 255 de novo\n15 56 15\n");
    AUREA_CHECK(colors.ok());
    if (!colors.ok()) return;
    AUREA_CHECK_EQ(colors->size(), usize{3});
    AUREA_CHECK_NEAR((*colors)[2].y, 56.f / 255.f, 1e-6f);
    AUREA_CHECK(!parse_gpl_palette("0 0 0\n").ok());   // sem o cabeçalho
}
AUREA_TEST(Palette, LospecHexAcceptsHashAndCarriageReturns) {
    const auto colors = parse_hex_palette("#ff004d\r\n29adff\r\nzzzzzz\r\n");
    AUREA_CHECK(colors.ok());
    if (!colors.ok()) return;
    AUREA_CHECK_EQ(colors->size(), usize{2});
    AUREA_CHECK_NEAR((*colors)[0].x, 1.f, 1e-6f);
    AUREA_CHECK_NEAR((*colors)[1].z, 1.f, 1e-6f);
}
AUREA_TEST(Palette, ImageKeepsUniqueVisibleColorsUpTo256MostFrequent) {
    // 300 cores distintas: a primeira repete 10 vezes, as outras uma vez.
    std::vector<u8> px;
    auto put = [&](u8 r, u8 g, u8 b, u8 a) { px.insert(px.end(), {r, g, b, a}); };
    for (u32 i = 0; i < 10; ++i) put(1, 2, 3, 255);
    for (u32 i = 1; i < 300; ++i) put(u8(i & 255), u8(i >> 8), 9, 255);
    put(200, 200, 200, 0);   // transparente: fica de fora
    const auto colors = palette_from_rgba(px.data(), u32(px.size() / 4), 1);
    AUREA_CHECK(colors.ok());
    if (!colors.ok()) return;
    AUREA_CHECK_EQ(colors->size(), usize{kMaxPaletteColors});
    AUREA_CHECK_NEAR((*colors)[0].x, 1.f / 255.f, 1e-6f);
}
AUREA_TEST(Palette, TableHoldsSrgbThenOklabAndWhiteIsUnitLightness) {
    const CubeLut t = palette_table({Vec3{0, 0, 0}, Vec3{1, 1, 1}});
    AUREA_CHECK_EQ(t.dimensions, 0u);
    AUREA_CHECK_EQ(t.values.size(), usize{4});
    AUREA_CHECK_NEAR(t.values[3].x, 1.f, 1e-3f);
    AUREA_CHECK_NEAR(t.values[3].y, 0.f, 1e-3f);
    AUREA_CHECK(is_palette_path("a/b/Paleta.GPL") && is_palette_path("x.jpeg") && !is_palette_path("look.cube"));
}
