// What this PC has, to say honestly which local models it can run well.
#pragma once
#include <cstdint>
#include <string>

struct PcSpecs {
    bool known = false;       // false: couldn't be read (then nothing is rated)
    std::string gpu;          // the strongest separate graphics card ("" = none found)
    uint64_t gpuBytes = 0;    // its own memory
    uint64_t ramBytes = 0;    // the PC's memory
    int threads = 0;          // processor threads
};
PcSpecs readPcSpecs();

// How a local model that needs `memoryGb` of free memory would do here, next to a game.
enum LocalFit { kFitUnknown = 0, kFitGood, kFitTight, kFitSlow, kFitNo };
int rateLocalFit(const PcSpecs& pc, int memoryGb);
