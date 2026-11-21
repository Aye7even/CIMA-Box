#include "pcspecs.h"

#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi.h>
#else
#include <unistd.h>
#endif

PcSpecs readPcSpecs() {
    PcSpecs pc;
    pc.threads = int(std::thread::hardware_concurrency());
#ifdef _WIN32
    MEMORYSTATUSEX mem;
    ZeroMemory(&mem, sizeof(mem));
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) pc.ramBytes = mem.ullTotalPhys;
    // The graphics cards Windows knows, any make. The one with the most memory of its own is the
    // one a game (and a model) would use; a chip built into the processor has almost none.
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; i < 16 && SUCCEEDED(factory->EnumAdapters1(i, &adapter)); ++i) {
            DXGI_ADAPTER_DESC1 d;
            if (adapter && SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                uint64_t(d.DedicatedVideoMemory) > pc.gpuBytes) {
                pc.gpuBytes = uint64_t(d.DedicatedVideoMemory);
                char name[512];
                const int n = WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, int(sizeof(name)), nullptr, nullptr);
                pc.gpu = n > 0 ? std::string(name) : std::string();
            }
            if (adapter) adapter->Release();
            adapter = nullptr;
        }
        factory->Release();
        pc.known = pc.ramBytes > 0;
    }
    if (pc.gpuBytes < (uint64_t(1) << 30)) {  // under 1 GB of its own: not a card a model can use
        pc.gpu.clear();
        pc.gpuBytes = 0;
    }
#else
    const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && size > 0) pc.ramBytes = uint64_t(pages) * uint64_t(size);
#endif
    return pc;
}

int rateLocalFit(const PcSpecs& pc, int memoryGb) {
    if (!pc.known) return kFitUnknown;
    const double gpuGb = double(pc.gpuBytes) / 1073741824.0, ramGb = double(pc.ramBytes) / 1073741824.0;
    // Windows reports a little under the card's nominal size (an "8 GB" card as 7.9), hence the half.
    if (gpuGb + 0.5 >= memoryGb + 4) return kFitGood;   // the model, and room left for a game
    if (gpuGb + 0.5 >= memoryGb + 1) return kFitTight;  // fits, but a game wants that memory too
    if (ramGb + 0.5 >= memoryGb + 8 && pc.threads >= 6) return kFitSlow;  // only on the processor
    return kFitNo;
}
