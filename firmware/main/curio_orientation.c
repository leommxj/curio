#include "curio_orientation.h"
#include <math.h>

void curio_orientation_reset(curio_orientation_t *orientation)
{
    orientation->pending = false;
}

bool curio_orientation_update(curio_orientation_t *orientation, float x, float y, float z,
                              uint64_t now, uint16_t *rotation)
{
    float magnitude_sq = x * x + y * y + z * z;
    float ax = fabsf(x), ay = fabsf(y);
    if (!isfinite(magnitude_sq) || magnitude_sq < 0.64f || magnitude_sq > 1.44f ||
        x * x + y * y < 0.25f || (ax < ay * 1.35f && ay < ax * 1.35f)) {
        curio_orientation_reset(orientation);
        return false;
    }
    uint16_t candidate = ax > ay ? (x > 0 ? 90 : 270) : (y > 0 ? 0 : 180);
    if (!orientation->pending || orientation->candidate != candidate || now < orientation->since_ms) {
        orientation->pending = true;
        orientation->candidate = candidate;
        orientation->since_ms = now;
    }
    if (candidate == *rotation || now - orientation->since_ms < 500) return false;
    *rotation = candidate;
    return true;
}
