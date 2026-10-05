#include "Application.hpp"
#include "terrain/TerrainGenerator.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <charconv>

int main(int argc, char** argv) {
    bool smokeTest = false;
    bool sunlightDemo = false;
    bool blockLightDemo = false;
    bool transparencyDemo = false;
    std::uint64_t seed = voxel::terrain::TerrainGenerator::DefaultSeed;
    bool seedSeen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--smoke-test") smokeTest = true;
        else if (argument == "--sunlight-demo") sunlightDemo = true;
        else if (argument == "--block-light-demo") blockLightDemo = true;
        else if (argument == "--transparency-demo") transparencyDemo = true;
        else if (argument == "--seed" && !seedSeen && i + 1 < argc) {
            const std::string_view value(argv[++i]);
            const auto [end, error] = std::from_chars(value.data(), value.data()+value.size(), seed);
            if (error != std::errc{} || end != value.data()+value.size()) {
                std::cerr << "Seed must be an unsigned 64-bit decimal integer.\n";
                return 1;
            }
            seedSeen = true;
        } else {
            std::cout << "Usage: voxel_game [--seed UINT64] [--smoke-test] [--sunlight-demo] [--block-light-demo] [--transparency-demo]\n";
            return argument == "--help" ? 0 : 1;
        }
    }

    try {
        voxel::Application app;
        app.run(smokeTest, seed, sunlightDemo, blockLightDemo, transparencyDemo);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "VoxelGame: " << error.what() << '\n';
        return 1;
    }
}
