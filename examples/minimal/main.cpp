// Minimal zero-Qt consumer of the installed qlitemd core SDK.
//
// Usage:
//   qlitemd_minimal [document.md] [--assets <dir>]
//
// Prints the body HTML to stdout and, when --assets is given, writes every
// formula/Mermaid SVG there using its stable asset id as the file name.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "md_pipeline.h"

namespace {

constexpr const char* kSample =
    "# qlitemd core\n\n"
    "Zero Qt, three worker lanes, SVG assets.\n\n"
    "Inline $x^2$ and a diagram:\n\n"
    "```mermaid\n"
    "graph TD; A-->B;\n"
    "```\n\n"
    "```cpp\n"
    "int main() { return 0; }\n"
    "```\n";

bool ReadFile(const std::string& path, std::string* text) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  *text = buffer.str();
  return true;
}

bool WriteAsset(const std::string& directory, const MdAsset& asset) {
  const std::filesystem::path path =
      std::filesystem::path(directory) / (asset.id + ".svg");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    std::cerr << "cannot write " << path.string() << "\n";
    return false;
  }
  output.write(asset.image.svg.data(),
               static_cast<std::streamsize>(asset.image.svg.size()));
  return output.good();
}

}  // namespace

int main(int argc, char** argv) {
  std::string markdown = kSample;
  std::string asset_directory;
  std::string document_path;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--assets" && i + 1 < argc) {
      asset_directory = argv[++i];
    } else if (document_path.empty()) {
      document_path = argv[i];
    } else {
      std::cerr << "usage: qlitemd_minimal [document.md] [--assets <dir>]\n";
      return 2;
    }
  }
  if (!document_path.empty() && !ReadFile(document_path, &markdown)) {
    std::cerr << "cannot read " << document_path << "\n";
    return 1;
  }

  MdHtmlPipeline pipeline;
  const MdHtmlResult result = pipeline.Render(std::move(markdown));
  if (!result.ok) {
    std::cerr << "render failed: " << result.error << "\n";
    return 1;
  }
  std::cout.write(result.html.data(),
                  static_cast<std::streamsize>(result.html.size()));
  std::cout << "\n";

  if (!asset_directory.empty()) {
    std::error_code error;
    std::filesystem::create_directories(asset_directory, error);
    if (error) {
      std::cerr << "cannot create " << asset_directory << ": "
                << error.message() << "\n";
      return 1;
    }
    for (const MdAsset& asset : result.assets) {
      if (!WriteAsset(asset_directory, asset)) {
        return 1;
      }
    }
  }
  return 0;
}
