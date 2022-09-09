
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <filesystem>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <pshpack1.h>
struct DDS_PIXELFORMAT {
  uint32_t dwSize  = 32;
  uint32_t dwFlags = 0x1 | 0x4 | 0x40; // DDPF_ALPHAPIXELS | DDPF_FOURCC | DDPF_RGB
  uint32_t dwFourCC = 0x00000074;      // R32G32B32A32_FLOAT
  uint32_t dwRGBBitCount = 0;
  uint32_t dwRBitMask = 0;
  uint32_t dwGBitMask = 0;
  uint32_t dwBBitMask = 0;
  uint32_t dwABitMask = 0;
};

struct DDS_HEADER {
  uint32_t dwMagic = 0x20534444;
  uint32_t dwSize = 124;
  uint32_t dwFlags = 0x0F | 0x1000 | 0x20000;
  uint32_t dwHeight;
  uint32_t dwWidth;
  uint32_t dwPitchOrLinearSize;
  uint32_t dwDepth = 1;
  uint32_t dwMipMapCount = 1;
  uint32_t dwReserved1[11] = { 0 };
  DDS_PIXELFORMAT ddspf;
  uint32_t dwCaps  = 0x400000 | 0x1000; // DDSCAPS_MIPMAP| DDSCAPS_TEXTURE
  uint32_t dwCaps2 = 0;
  uint32_t dwCaps3 = 0;
  uint32_t dwCaps4 = 0;
  uint32_t dwReserved2 = 0;
};

struct KTXHeader {
  char identifier[12];
  char endianness[4] = { 1, 2, 3, 4 };
  uint32_t glType = 0;
  uint32_t glTypeSize = 0;
  uint32_t glFormat = 0;
  uint32_t glInternalFormat = 0;
  uint32_t glBaseInternalFormat = 0;
  uint32_t pixelWidth = 0;
  uint32_t pixelHeight= 0;
  uint32_t pixelDepth = 0;
  uint32_t numberOfArrayElements = 0;
  uint32_t numberOfFaces = 1;
  uint32_t numberOfMipmapLevels = 1;
  uint32_t bytesOfKeyValueData = 0;
};

#include <poppack.h>


namespace fs = std::filesystem;

struct GlobalConfig {
  fs::path inputPath;
  fs::path outputPath;
  std::string name;
  std::string type;
};
GlobalConfig gConfig;

void printHelp() {
  std::cout << "Parameters\n";
  std::cout << "  --in  (*.obj ファイルを配置しているフォルダ)\n";
  std::cout << "  --out (*.obj 出力フォルダの指定)\n";
  std::cout << "  --name (出力するファイル名)\n";
  std::cout << "  --type (dds || ktx)\n";
}

int argumentParser(const std::vector<std::string>& args) {

  gConfig.outputPath = ".";
  if (auto itr = std::find(args.begin(), args.end(), "--out"); itr != args.end()) {
    if (itr + 1 == args.end()) {
      std::cerr << "invalid argument\n";
      return -1;
    }
    gConfig.outputPath = *(++itr);
  }

  if (auto itr = std::find(args.begin(), args.end(), "--in"); itr != args.end()) {
    if (itr + 1 == args.end()) {
      std::cerr << "invalid argument\n";
      return -1;
    }
    auto inputPath = *(++itr);
    if (!fs::exists(inputPath)) {
      std::cerr << "Cannot read input dir.\n";
      return -1;
    }
    gConfig.inputPath = inputPath;
  }
  if (auto itr = std::find(args.begin(), args.end(), "--name"); itr != args.end()) {
    if (itr + 1 == args.end()) {
      std::cerr << "invalid argument\n";
      return -1;
    }
    gConfig.name = *(++itr);
  }
  if (auto itr = std::find(args.begin(), args.end(), "--type"); itr != args.end()) {
    if (itr + 1 == args.end()) {
      std::cerr << "invalid argument\n";
      return -1;
    }
    auto type = *(++itr);
    if (type == "dds" || type == "ktx") {
      gConfig.type = type;
    } else {
      std::cerr << "invalid type\n";
      return -1;
    }
  }

  return 0;
}


int processConvert() {
  // 全*.objファイルの読み取り.
  const auto inputPath = gConfig.inputPath;
  auto searchPathFilter = inputPath / "*.obj";
  char searchPath[4096];
  strcpy_s(searchPath, searchPathFilter.string().c_str());

  WIN32_FIND_DATAA findData{};
  HANDLE hFind = FindFirstFileA(searchPath, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    return -1;
  }

  std::vector<fs::path> fileList;
  do {
    auto filePath = inputPath / findData.cFileName;
    fileList.push_back(filePath);
  } while (FindNextFileA(hFind, &findData) != FALSE);
  if (GetLastError() != ERROR_NO_MORE_FILES) {
    FindClose(hFind);
    return -1; // 何らかのエラー.
  }
  FindClose(hFind);

  std::sort(fileList.begin(), fileList.end());


  // 全ファイルをロードして、展開後の頂点数のチェック.
  struct FileInfo {
    tinyobj::attrib_t attrib{};
    std::vector<tinyobj::shape_t> shapes{};
  };
  std::vector<FileInfo> fileInfo;
  fileInfo.reserve(fileList.size());

  for (auto& file : fileList) {
    std::cout << "read file : " << file.filename() << "\n";

    tinyobj::attrib_t attrib{};
    std::string err;
    std::vector<tinyobj::shape_t> shapes;
    auto result = tinyobj::LoadObj(&attrib, &shapes, nullptr, &err, file.string().c_str());
    if (!result) {
      std::cerr << "Error!!!\n" << err << std::endl;
      return -1;
    }
    fileInfo.emplace_back(std::move(FileInfo{ attrib, shapes }));
  }

  std::cout << "checking ...\n";
  uint32_t maxFlattenIndexCount = 0;
  for (auto file : fileInfo) {
    uint32_t shapeCount = uint32_t(file.shapes.size());
    uint32_t indexCount = 0;
    for (uint32_t i = 0; i < shapeCount; ++i) {
      indexCount += uint32_t(file.shapes[i].mesh.indices.size());
    }
    if (indexCount > maxFlattenIndexCount) {
      maxFlattenIndexCount = indexCount;
    }
  }

  if (maxFlattenIndexCount > 16384) {
    fprintf(stderr, "Error: データが大きすぎます (%d).\n", maxFlattenIndexCount);
    return -1;
  }

  // データを配列に格納する.
  // いわゆる画像データ本体に相当する.
  struct Element {
    float x, y, z, w;
  };
  std::vector<Element> vertPositions, vertNormals;
  const auto animationCount = fileInfo.size();
  vertPositions.reserve(animationCount * maxFlattenIndexCount);
  vertNormals.reserve(animationCount* maxFlattenIndexCount);

  for (const auto& file : fileInfo) {
    for (const auto& shape : file.shapes) {
      int storeCount = 0;
      for (const auto& index : shape.mesh.indices) {
        auto v0 = file.attrib.vertices[3 * index.vertex_index + 0];
        auto v1 = file.attrib.vertices[3 * index.vertex_index + 1];
        auto v2 = file.attrib.vertices[3 * index.vertex_index + 2];

        auto n0 = file.attrib.normals[3 * index.normal_index + 0];
        auto n1 = file.attrib.normals[3 * index.normal_index + 1];
        auto n2 = file.attrib.normals[3 * index.normal_index + 2];

        vertPositions.emplace_back(Element{ v0, v1, v2, 1.0 });
        vertNormals.emplace_back(Element{ n0, n1, n2, 1.0 });
        ++storeCount;
      }
      int remain = maxFlattenIndexCount - storeCount;
      for (int i = 0; i < remain; ++i) {

      }
      Element noUseData{ 0,0,0,0};
      vertPositions.insert(vertPositions.end(), remain, noUseData);
      vertNormals.insert(vertNormals.end(), remain, noUseData);
    }
  }
  std::cout << "vertexCount(Max): " << maxFlattenIndexCount << ", animationCount: " << animationCount << std::endl;

  // ファイルへ出力.
  if (gConfig.name.empty()) {
    gConfig.name = inputPath.filename().string();
  }
  auto outputPath = gConfig.outputPath / gConfig.name;

  if (gConfig.type == "dds") {
    auto outputPathPos = outputPath; outputPathPos.replace_extension("ptex.dds");
    auto outputPathNrm = outputPath; outputPathNrm.replace_extension("ntex.dds");

    {
      DDS_HEADER headerDDS{};
      headerDDS.dwWidth = maxFlattenIndexCount;
      headerDDS.dwHeight = animationCount;
      std::ofstream outfile(outputPathPos, std::ios::binary);
      outfile.write(reinterpret_cast<char*>(&headerDDS), sizeof(headerDDS));
      outfile.write(reinterpret_cast<char*>(vertPositions.data()), vertPositions.size() * sizeof(Element));
    }

    {
      DDS_HEADER headerDDS{};
      headerDDS.dwWidth = maxFlattenIndexCount;
      headerDDS.dwHeight = animationCount;
      std::ofstream outfile(outputPathNrm, std::ios::binary);
      outfile.write(reinterpret_cast<char*>(&headerDDS), sizeof(headerDDS));
      outfile.write(reinterpret_cast<char*>(vertNormals.data()), vertNormals.size() * sizeof(Element));
    }
  }
  if (gConfig.type == "ktx") {
    auto outputPathPos = outputPath; outputPathPos.replace_extension("ptex.ktx");
    auto outputPathNrm = outputPath; outputPathNrm.replace_extension("ntex.ktx");

    const unsigned char ktxIdentifier[] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    uint32_t imageSize = sizeof(float) * 4 * maxFlattenIndexCount * animationCount;
    {
      KTXHeader headerKTX{};
      memcpy(headerKTX.identifier, ktxIdentifier, sizeof(ktxIdentifier));
      headerKTX.glInternalFormat = 0x8814 /*GL_RGBA32F*/;
      headerKTX.pixelWidth = maxFlattenIndexCount;
      headerKTX.pixelHeight = animationCount;

      std::ofstream outfile(outputPathPos, std::ios::binary);
      outfile.write(reinterpret_cast<char*>(&headerKTX), sizeof(headerKTX));
      outfile.write(reinterpret_cast<char*>(&imageSize), sizeof(imageSize));
      outfile.write(reinterpret_cast<char*>(vertPositions.data()), vertPositions.size() * sizeof(Element));
    }
    {
      KTXHeader headerKTX{};
      memcpy(headerKTX.identifier, ktxIdentifier, sizeof(ktxIdentifier));
      headerKTX.glInternalFormat = 0x8814 /*GL_RGBA32F*/;
      headerKTX.pixelWidth = maxFlattenIndexCount;
      headerKTX.pixelHeight = animationCount;

      std::ofstream outfile(outputPathNrm, std::ios::binary);
      outfile.write(reinterpret_cast<char*>(&headerKTX), sizeof(headerKTX));
      outfile.write(reinterpret_cast<char*>(&imageSize), sizeof(imageSize));
      outfile.write(reinterpret_cast<char*>(vertNormals.data()), vertNormals.size() * sizeof(Element));
    }
  }

  return 0;
}

int main(int argc, char* argv[])
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    if (argumentParser(args) < 0) {
      printHelp();
      return -1;
    }

    if (processConvert() < 0) {
      std::cout << "変換処理に失敗しました.\n";
      return -1;
    }
    
    std::cout << "変換処理が完了しました.\n";
    return 0;
}
