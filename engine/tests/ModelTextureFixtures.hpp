// =============================================================================
//  Aurea / tests / ModelTextureFixtures.hpp
//
//  Modelos OBJ (+ .mtl) e FBX (ASCII) escritos na hora, com textura EXTERNA,
//  do jeito que chegam do seletor do celular: o app guarda o modelo com outro
//  nome (o hash) e as texturas/.mtl escolhidas junto ficam na MESMA pasta,
//  com o nome do seletor — o caminho gravado no arquivo (pasta de outra
//  máquina, subpasta "textures\") não existe aqui.
// =============================================================================
#pragma once

#include "ImageIO.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace aurea::test_fixtures {

/// Pasta vazia (recriada) com barra no fim.
inline std::string fresh_model_folder(const char* name) {
    std::error_code ec;
    std::filesystem::remove_all(name, ec);
    std::filesystem::create_directories(name, ec);
    return std::string(name) + "/";
}

inline void write_text(const std::string& path, const char* text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    std::fputs(text, f);
    std::fclose(f);
}

/// Textura de cor sólida (8×8), como a imagem que o usuário escolhe.
inline bool write_solid_png(const std::string& path, u8 r, u8 g, u8 b) {
    aurea::test::Image8 img;
    img.width = img.height = 8;
    img.rgba.resize(8 * 8 * 4);
    for (usize i = 0; i < img.rgba.size(); i += 4) {
        img.rgba[i] = r;
        img.rgba[i + 1] = g;
        img.rgba[i + 2] = b;
        img.rgba[i + 3] = 255;
    }
    return aurea::test::write_png(path, img);
}

/// Quadrado 1×1 de frente (+Z) com UV. O .obj grava o .mtl numa subpasta e o
/// .mtl grava a textura num caminho absoluto do Windows de outra máquina.
/// Devolve o caminho do .obj (nome "hash", como o app guarda).
inline std::string write_textured_obj(const std::string& folder) {
    write_text(folder + "a1b2c3.obj",
               "mtllib Materiais/Original.mtl\n"
               "v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0.5 0.5 0\nv -0.5 0.5 0\n"
               "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
               "usemtl tijolo\nf 1/1/1 2/2/1 3/3/1 4/4/1\n");
    return folder + "a1b2c3.obj";
}

/// O .mtl que o usuário escolheu junto (o nome do seletor).
inline void write_picked_mtl(const std::string& folder, const char* textureName) {
    std::string mtl = "newmtl tijolo\nKd 1 1 1\nmap_Kd C:\\Artista\\texturas\\";
    mtl += textureName;
    mtl += "\n";
    write_text(folder + "Original.mtl", mtl.c_str());
}

/// FBX 7.4 ASCII: um quadrado com UV, material phong branco e a textura
/// difusa EXTERNA em "textures\<nome>" (relativo) e "C:\Artista\..." (absoluto).
inline std::string write_textured_fbx(const std::string& folder, const char* textureName) {
    std::string fbx =
        "; FBX 7.4.0 project file\n"
        "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
        "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
        "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n"
        "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
        "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",100\n"
        "\t}\n}\n"
        "Objects:  {\n"
        "\tGeometry: 1000, \"Geometry::Quad\", \"Mesh\" {\n"
        "\t\tVertices: *12 {\n\t\t\ta: -0.5,-0.5,0,0.5,-0.5,0,0.5,0.5,0,-0.5,0.5,0\n\t\t}\n"
        "\t\tPolygonVertexIndex: *4 {\n\t\t\ta: 0,1,2,-4\n\t\t}\n"
        "\t\tGeometryVersion: 124\n"
        "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n"
        "\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
        "\t\t\tUV: *8 {\n\t\t\t\ta: 0,0,1,0,1,1,0,1\n\t\t\t}\n"
        "\t\t\tUVIndex: *4 {\n\t\t\t\ta: 0,1,2,3\n\t\t\t}\n\t\t}\n"
        "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n"
        "\t\t\tMappingInformationType: \"AllSame\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n"
        "\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n"
        "\t\tLayer: 0 {\n\t\t\tVersion: 100\n"
        "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
        "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n"
        "\t\t}\n"
        "\t}\n"
        "\tModel: 2000, \"Model::Quad\", \"Mesh\" {\n\t\tVersion: 232\n\t}\n"
        "\tMaterial: 3000, \"Material::Tijolo\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n"
        "\t\tProperties70:  {\n\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",1,1,1\n\t\t}\n\t}\n"
        "\tTexture: 4000, \"Texture::Tijolo\", \"\" {\n\t\tType: \"TextureVideoClip\"\n\t\tVersion: 202\n"
        "\t\tTextureName: \"Texture::Tijolo\"\n";
    fbx += "\t\tFileName: \"C:\\Artista\\texturas\\";
    fbx += textureName;
    fbx += "\"\n\t\tRelativeFilename: \"textures\\";
    fbx += textureName;
    fbx += "\"\n\t}\n}\n"
           "Connections:  {\n"
           "\tC: \"OO\",2000,0\n"
           "\tC: \"OO\",1000,2000\n"
           "\tC: \"OO\",3000,2000\n"
           "\tC: \"OP\",4000,3000, \"DiffuseColor\"\n"
           "}\n";
    write_text(folder + "d4e5f6.fbx", fbx.c_str());
    return folder + "d4e5f6.fbx";
}

} // namespace aurea::test_fixtures
