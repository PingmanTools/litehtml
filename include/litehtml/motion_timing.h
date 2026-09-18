#ifndef LITEHTML_MOTION_TIMING_H
#define LITEHTML_MOTION_TIMING_H

#include "css_length.h"
#include "web_color.h"
#include <functional>
#include <optional>

namespace litehtml
{
    class motion_timing
    {
        double m_x1 = .25, m_y1 = .1, m_x2 = .25, m_y2 = 1;
        motion_timing(double x1, double y1, double x2, double y2);

      public:
        motion_timing() = default;
        static std::optional<motion_timing> cubic_bezier(double x1, double y1, double x2, double y2);
        static std::optional<motion_timing> parse(const css_token& token);
        static std::optional<motion_timing> parse(const std::string& text);
        // Bezier output may overshoot [0, 1].
        double sample(double progress) const;
    };

    float interpolate_scalar(float from, float to, double progress);

    // Resolve lengths to CSS pixels in the property's context.
    using length_resolver = std::function<std::optional<float>(const css_length&)>;
    // Mixed units require a resolver; matching units remain unchanged.
    std::optional<css_length> interpolate_length(const css_length& from, const css_length& to,
                                               double progress, const length_resolver& resolve = {});
    // Resolve currentColor before interpolating in premultiplied encoded sRGB.
    std::optional<web_color> interpolate_color(web_color from, web_color to, double progress);
}

#endif
