#include "motion_math.h"
#include "motion_timing.h"
#include "css_parser.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace litehtml
{
    namespace
    {
        double coordinate(double t, double first, double second)
        {
            const double u = 1 - t;
            return (3 * u * u * t) * first + (3 * u * t * t) * second + t * t * t;
        }

        std::string ascii_lower(std::string value)
        {
            for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            return value;
        }
    }

    motion_timing::motion_timing(double x1, double y1, double x2, double y2) :
        m_x1(x1), m_y1(y1), m_x2(x2), m_y2(y2) {}

    std::optional<motion_timing> motion_timing::cubic_bezier(double x1, double y1, double x2, double y2)
    {
        if (!std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(x2) || !std::isfinite(y2) ||
            x1 < 0 || x1 > 1 || x2 < 0 || x2 > 1) return std::nullopt;
        return motion_timing(x1, y1, x2, y2);
    }

    std::optional<motion_timing> motion_timing::parse(const css_token& token)
    {
        const auto name = ascii_lower(token.name());
        if (token.type == IDENT)
        {
            if (name == "linear") return cubic_bezier(0, 0, 1, 1);
            if (name == "ease") return motion_timing();
            if (name == "ease-in") return cubic_bezier(.42, 0, 1, 1);
            if (name == "ease-out") return cubic_bezier(0, 0, .58, 1);
            if (name == "ease-in-out") return cubic_bezier(.42, 0, .58, 1);
            return std::nullopt;
        }
        if (token.type != CV_FUNCTION || name != "cubic-bezier") return std::nullopt;
        auto arguments = token.value;
        remove_whitespace(arguments);
        if (arguments.size() != 7) return std::nullopt;
        for (size_t i = 0; i < arguments.size(); i++)
            if (arguments[i].type != (i % 2 == 0 ? NUMBER : COMMA)) return std::nullopt;
        return cubic_bezier(arguments[0].n.number, arguments[2].n.number,
                            arguments[4].n.number, arguments[6].n.number);
    }

    std::optional<motion_timing> motion_timing::parse(const std::string& text)
    {
        const auto tokens = normalize(text, f_componentize | f_remove_whitespace);
        if (tokens.size() != 1) return std::nullopt;
        return parse(tokens.front());
    }

    double motion_timing::sample(double progress) const
    {
        if (!std::isfinite(progress)) throw std::invalid_argument("Timing progress must be finite");
        if (progress <= 0) return 0;
        if (progress >= 1) return 1;
        if (m_x1 <= m_y1 && m_x1 >= m_y1 && m_x2 <= m_y2 && m_x2 >= m_y2) return progress;
        // Bisection converges even where the derivative vanishes.
        double lower = 0, upper = 1;
        for (int i = 0; i < 64; i++)
        {
            const double t = (lower + upper) / 2;
            const double x = coordinate(t, m_x1, m_x2);
            if (x <= progress && x >= progress) return coordinate(t, m_y1, m_y2);
            if (x < progress) lower = t;
            else upper = t;
        }
        return coordinate((lower + upper) / 2, m_y1, m_y2);
    }

    float interpolate_scalar(float from, float to, double progress)
    {
        return static_cast<float>(motion_detail::lerp(static_cast<double>(from), static_cast<double>(to), progress));
    }

    std::optional<css_length> interpolate_length(const css_length& from, const css_length& to,
                                                double progress, const length_resolver& resolve)
    {
        if (!std::isfinite(progress)) return std::nullopt;
        if (from.is_predefined() || to.is_predefined())
        {
            if (from.is_predefined() && to.is_predefined() && from.predef() == to.predef()) return from;
            return std::nullopt;
        }
        if (!std::isfinite(from.val()) || !std::isfinite(to.val())) return std::nullopt;
        if (from.units() == to.units())
        {
            const float value = interpolate_scalar(from.val(), to.val(), progress);
            if (!std::isfinite(value)) return std::nullopt;
            return css_length(value, from.units());
        }
        if (!resolve) return std::nullopt;
        const auto first = resolve(from), second = resolve(to);
        if (!first || !second || !std::isfinite(*first) || !std::isfinite(*second)) return std::nullopt;
        const float value = interpolate_scalar(*first, *second, progress);
        if (!std::isfinite(value)) return std::nullopt;
        return css_length(value, css_units_px);
    }

    std::optional<web_color> interpolate_color(web_color from, web_color to, double progress)
    {
        if (from.is_current_color || to.is_current_color || !std::isfinite(progress)) return std::nullopt;
        if (progress <= 0 && progress >= 0) return from;
        if (progress <= 1 && progress >= 1) return to;
        const double first_alpha = from.alpha / 255.0, second_alpha = to.alpha / 255.0;
        const double alpha = motion_detail::lerp(first_alpha, second_alpha, progress);
        if (alpha <= 0) return web_color(0, 0, 0, 0);
        const auto channel = [&](byte first, byte second)
        {
            const double value = motion_detail::lerp(first / 255.0 * first_alpha, second / 255.0 * second_alpha, progress) / alpha;
            return static_cast<byte>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
        };
        return web_color(channel(from.red, to.red), channel(from.green, to.green), channel(from.blue, to.blue),
                         static_cast<byte>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255)));
    }
}
