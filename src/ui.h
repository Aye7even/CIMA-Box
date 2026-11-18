// The interface: theme, fonts and layout. Knows nothing about audio or networking.
#pragma once
#include "app.h"

// Call once after ImGui::CreateContext(). `scale` is the monitor's content scale (1.0 = 96 dpi).
void uiSetup(float scale);

// Draws one frame and returns what the user asked for.
UiActions uiDraw(UiModel& m);
