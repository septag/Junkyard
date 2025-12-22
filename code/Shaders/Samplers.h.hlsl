// Global Immutable samplers
// Sync with R_SAMPLERS_DESC in Render.cpp
#define SAMPLERS_DESCRIPTOR_SET 2

[vk::binding(0, SAMPLERS_DESCRIPTOR_SET)]
SamplerState SamplerBilinear;

[vk::binding(1, SAMPLERS_DESCRIPTOR_SET)]
SamplerState SamplerTrilinear;

[vk::binding(2, SAMPLERS_DESCRIPTOR_SET)]
SamplerComparisonState SamplerBilinearLess;

