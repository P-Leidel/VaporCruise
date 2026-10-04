#include "scene/background-artwork.h"
#include "render/camera-projection.h"
#include <iostream>
int main() {
    std::cout << "{\"texture_bytes\":" << vaporcruise::scene::GetBackgroundArtwork().GpuBytes() << ",\"camera\":";
    vaporcruise::render::SerializeCamera(std::cout,vaporcruise::render::DefaultCameraProjection());
    std::cout << "}\n";
}
