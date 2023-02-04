#include "Base.h"

#include <time.h>   // time

////////////////////////////////////////////////////////////////////////////////////////////////
// Random
// https://github.com/mattiasgustavsson/libs/blob/master/rnd.h

struct RandomContextCtor
{
    RandomContext ctx;

    RandomContextCtor() 
    {
        ctx = randomCreateContext();
    }
};

static thread_local RandomContextCtor gRandomCtx;

// Convert a randomized uint32_t value to a float value x in the range 0.0f <= x < 1.0f. Contributed by Jonatan Hedborg
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wstrict-aliasing")
static inline float randomFloatNormalized(uint32 value)
{
    uint32 exponent = 127;
    uint32 mantissa = value >> 9;
    uint32 result = (exponent << 23) | mantissa;
    float fresult = *(float*)(&result);
    return fresult - 1.0f;
}
PRAGMA_DIAGNOSTIC_POP()

static inline uint64 randomAvalanche64(uint64 h)
{
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return h;
}

uint32 randomGenSeed(void)
{
    return static_cast<uint32>(time(nullptr));
}

RandomContext randomCreateContext(uint32 seed)
{
    RandomContext ctx = {{0, 0}};
    uint64 value = (((uint64)seed) << 1ull) | 1ull;    // make it odd
    value = randomAvalanche64(value);
    ctx.state[0] = 0ull;
    ctx.state[1] = (value << 1ull) | 1ull;
    randomNewUint(&ctx);
    ctx.state[0] += randomAvalanche64(value);
    randomNewUint(&ctx);
    return ctx;
}

uint32 randomNewUint(RandomContext* ctx)
{
    uint64 oldstate = ctx->state[0];
    ctx->state[0] = oldstate * 0x5851f42d4c957f2dull + ctx->state[1];
    uint32 xorshifted = uint32(((oldstate >> 18ull) ^ oldstate) >> 27ull);
    uint32 rot = uint32(oldstate >> 59ull);
    return (xorshifted >> rot) | (xorshifted << ((-(int)rot) & 31));
}

float randomNewFloat(RandomContext* ctx)
{
    return randomFloatNormalized(randomNewUint(ctx));
}

float randomNewFloatInRange(RandomContext* ctx, float _min, float _max)
{
    ASSERT(_min <= _max);
    
    float r = randomNewFloat(ctx);
    return _min + r*(_max - _min);
}

int randomNewIntInRange(RandomContext* ctx, int _min, int _max)
{
    ASSERT(_min <= _max);
    
    uint32 range = static_cast<uint32>(_max - _min) + 1;
    return _min + static_cast<int>(randomNewUint(ctx) % range);
}

uint32 randomNewUint()
{
    return randomNewUint(&gRandomCtx.ctx);
}

float randomNewFloat()
{
    return randomNewFloat(&gRandomCtx.ctx);
}

float randomNewFloatInRange(float _min, float _max)
{
    return randomNewFloatInRange(&gRandomCtx.ctx, _min, _max);
}

int randomNewIntInRange(int _min, int _max)
{
    return randomNewIntInRange(&gRandomCtx.ctx, _min, _max);
}
