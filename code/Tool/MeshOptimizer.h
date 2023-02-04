#pragma once

#include "../Core/Base.h"

#if CONFIG_TOOLMODE
struct Model;
struct ModelLoadParams;

void meshoptOptimizeModel(Model* model, const ModelLoadParams& modelParams);

namespace _private
{
    void meshoptInitialize();
}

#endif // CONFIG_TOOLMODE

