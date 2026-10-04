#pragma once
#include "scene/scene-frame-snapshot.h"
namespace vaporcruise::render {
struct RenderInput { scene::SceneFrameSnapshot frame; float hintOpacity=0; float uiScale=1; };
}
