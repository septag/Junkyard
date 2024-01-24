#pragma once

#include "../Core/Base.h"

API void frameInfoInitialize();
API void frameInfoRelease();

API void frameInfoRender(float dt, bool *pOpen = nullptr);

