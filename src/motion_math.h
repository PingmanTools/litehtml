#ifndef LITEHTML_MOTION_MATH_H
#define LITEHTML_MOTION_MATH_H

#include <algorithm>

namespace litehtml
{
    namespace motion_detail
    {
        inline double lerp(double from, double to, double progress)
        {
            // Opposite signs can overflow the endpoint difference.
            if((from <= 0 && to >= 0) || (from >= 0 && to <= 0))
            {
                return (1 - progress) * from + progress * to;
            }
            if(progress == 1) return to;
            const double value = from + progress * (to - from);
            return (progress > 1) == (to > from) ? std::max(to, value) : std::min(to, value);
        }
    }
}

#endif
