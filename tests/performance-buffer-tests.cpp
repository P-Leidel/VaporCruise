#include "render/performance-buffer.h"
#include <cmath>
#include <iostream>
using namespace vaporcruise::render;
int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { std::cerr << "FAIL: " << name << '\n'; ++failures; } };
    PerformanceBuffer buffer; buffer.Configure(2);
    const size_t first = buffer.Add({0, .1, 6});
    const size_t second = buffer.Add({7, .2, 5});
    check(first == 0 && second == 1 && buffer.Rows().size() == 2, "preallocated captures retain two real frame measurements");
    check(buffer.Add({14, .3, 4}) == PerformanceBuffer::InvalidIndex && buffer.Dropped() == 1 && buffer.Rows().size() == 2,
          "overflow remains bounded and counts dropped records");
    check(!buffer.SetGpu(2, 1), "late GPU result cannot index beyond retained frames");
    check(buffer.SetGpu(first, 2.5) && buffer.Rows()[0].gpuMs == 2.5 && std::isnan(buffer.Rows()[1].gpuMs),
          "delayed GPU duration updates its original frame and leaves unavailable samples blank");
    buffer.Configure(1);
    FrameMeasurement far;
    far.travelCell = (std::int64_t{1} << 55) + 3;
    far.terrainSection = far.travelCell / 32;
    far.terrainRowsBuilt = 4;
    buffer.Add(far);
    check(buffer.Rows()[0].travelCell == far.travelCell && buffer.Rows()[0].terrainSection == far.terrainSection &&
          buffer.Rows()[0].terrainRowsBuilt == 4, "world diagnostics retain exact distant cells and bounded row work");
    return failures ? 1 : 0;
}
