#include "TestFramework.hpp"
#include "aurea/export/UpscaleColor.hpp"
#include <array>
#include <vector>
using namespace aurea;

AUREA_TEST(UpscaleColor, Bt709LimitedRangeAndTilesMatchWholeImage) {
    constexpr u32 w = 8, h = 4;
    std::vector<u8> rgb(w * h * 3);
    const std::array<std::array<u8, 3>, 8> colors{{{0,0,0},{255,255,255},{255,0,0},{0,255,0},
                                                {0,0,255},{63,63,63},{128,96,160},{235,180,80}}};
    for (u32 y = 0; y < h; ++y) for (u32 x = 0; x < w; ++x)
        for (u32 c = 0; c < 3; ++c) rgb[(y*w+x)*3+c] = colors[(y/2)*4+x/2][c];
    std::vector<u8> whole(w*h*3/2), tiled(whole.size(), 99), restored;
    AUREA_CHECK(ai::rgb_tile_to_nv12_709(rgb.data(), w*3, 0,0,w,h,w,h,whole.data()));
    for (u32 y = 0; y < h; y += 2) for (u32 x = 0; x < w; x += 2)
        AUREA_CHECK(ai::rgb_tile_to_nv12_709(rgb.data()+(y*w+x)*3, w*3,x,y,2,2,w,h,tiled.data()));
    AUREA_CHECK(whole == tiled);
    AUREA_CHECK(whole[0] == 16); AUREA_CHECK(whole[2] == 235);
    ai::nv12_to_rgb709(whole.data(), whole.data()+w*h,w,h,restored);
    for (usize i=0; i<rgb.size(); ++i) AUREA_CHECK(std::abs(int(rgb[i])-int(restored[i])) <= 2);
    const auto before = tiled;
    AUREA_CHECK(!ai::rgb_tile_to_nv12_709(rgb.data(),w*3,1,0,2,2,w,h,tiled.data()));
    AUREA_CHECK(!ai::rgb_tile_to_nv12_709(rgb.data(),w*3,w,0,2,2,w,h,tiled.data()));
    AUREA_CHECK(tiled == before);
}

AUREA_TEST(UpscaleColor, RoundedNeuralOutputPreservesFullFrameAndChromaGeometry) {
    for (bool portrait : {false, true}) {
        const u32 sw = portrait ? 8 : 12, sh = portrait ? 12 : 8;
        const u32 dw = portrait ? 6 : 10, dh = portrait ? 10 : 6;
        std::vector<u8> input(sw*sh*3/2), output(dw*dh*3/2, 0);
        for (u32 y=0; y<sh; ++y) for (u32 x=0; x<sw; ++x) input[y*sw+x] = u8(16+x*10+y*5);
        for (u32 y=0; y<sh/2; ++y) for (u32 x=0; x<sw/2; ++x) {
            input[sw*sh+y*sw+x*2] = u8(70+x*12+y*4);
            input[sw*sh+y*sw+x*2+1] = u8(190-x*8-y*6);
        }
        AUREA_CHECK(ai::resize_nv12_709(input.data(),sw,sh,output.data(),dw,dh));
        for (u32 y=0; y<dh; ++y) for (u32 x=0; x<dw; ++x) {
            const double sx=(x+.5)*sw/dw-.5, sy=(y+.5)*sh/dh-.5;
            AUREA_CHECK(std::abs(int(output[y*dw+x])-int(std::lround(16+sx*10+sy*5))) <= 1);
        }
        // Right/bottom content moves into the output; simply taking its top-left
        // rectangle (the former callback) cannot satisfy these samples.
        AUREA_CHECK(output[dw*dh-1] > input[(dh-1)*sw+dw-1]);
        for (u32 y=0; y<dh/2; ++y) for (u32 x=0; x<dw/2; ++x) {
            const double sx=(x+.5)*sw/dw-.5, sy=(y+.5)*sh/dh-.5;
            AUREA_CHECK(std::abs(int(output[dw*dh+y*dw+x*2])-int(std::lround(70+sx*12+sy*4))) <= 1);
            AUREA_CHECK(std::abs(int(output[dw*dh+y*dw+x*2+1])-int(std::lround(190-sx*8-sy*6))) <= 1);
        }
        auto inPlace=input;
        AUREA_CHECK(ai::resize_nv12_709(inPlace.data(),sw,sh,inPlace.data(),dw,dh));
        AUREA_CHECK(std::equal(output.begin(),output.end(),inPlace.begin()));
        const auto unchanged=inPlace;
        AUREA_CHECK(!ai::resize_nv12_709(inPlace.data(),sw,sh,inPlace.data(),sw+2,sh));
        AUREA_CHECK(inPlace==unchanged);
        std::vector<u8> identity(input.size());
        AUREA_CHECK(ai::resize_nv12_709(input.data(),sw,sh,identity.data(),sw,sh));
        AUREA_CHECK(identity==input);
        AUREA_CHECK(!ai::resize_nv12_709(input.data(),sw,sh,identity.data(),dw-1,dh));
    }
}

AUREA_TEST(UpscaleColor, InPlaceResizeMatchesSeparateBufferAcrossRoundedExtents) {
    for (u32 sw=2; sw<=32; sw+=2) for (u32 sh=2; sh<=24; sh+=2) {
        std::vector<u8> input(sw*sh*3/2);
        for (usize i=0; i<input.size(); ++i) input[i]=u8((i*73+i/7*19)%256);
        for (u32 dx=0; dx<=6 && dx<sw; dx+=2) for (u32 dy=0; dy<=6 && dy<sh; dy+=2) {
            const u32 dw=sw-dx,dh=sh-dy;
            std::vector<u8> expected(dw*dh*3/2), actual=input;
            AUREA_CHECK(ai::resize_nv12_709(input.data(),sw,sh,expected.data(),dw,dh));
            AUREA_CHECK(ai::resize_nv12_709(actual.data(),sw,sh,actual.data(),dw,dh));
            AUREA_CHECK(std::equal(expected.begin(),expected.end(),actual.begin()));
        }
    }
}
