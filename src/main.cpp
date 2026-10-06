#include "Types.hpp"
#include "Application.hpp"
#include "terrain/TerrainGenerator.hpp"

#include <exception>
#include "Console.hpp"
#include <string_view>
#include <charconv>

int main(int argc, char** argv) {
    bool smokeTest = false;
    bool sunlightDemo = false;
    bool blockLightDemo = false;
    bool transparencyDemo = false;
    voxel::UInt64 seed = voxel::terrain::TerrainGenerator::DefaultSeed;
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
                voxel::console::error("Seed must be an unsigned 64-bit decimal integer.");
                return 1;
            }
            seedSeen = true;
        } else {
            voxel::console::info("Usage: voxel_game [--seed UINT64] [--smoke-test] [--sunlight-demo] [--block-light-demo] [--transparency-demo]");
            return argument == "--help" ? 0 : 1;
        }
    }

    try {
        voxel::Application app;
        app.run(smokeTest, seed, sunlightDemo, blockLightDemo, transparencyDemo);
        return 0;
    } catch (const std::exception& error) {
        voxel::console::error("VoxelGame: ", error.what());
        return 1;
    }
}
