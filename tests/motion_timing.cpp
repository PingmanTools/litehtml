#include "litehtml/motion_timing.h"
#include "motion_math.h"
#include "litehtml/css_parser.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

using namespace litehtml;

static void require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static bool near(double actual, double expected, double tolerance = 1e-6)
{
    return std::abs(actual - expected) < tolerance;
}

int main()
{
    for (const auto* keyword : {"linear", "ease", "ease-in", "ease-out", "ease-in-out"})
    {
        auto timing = motion_timing::parse(std::string(keyword));
        require(timing.has_value(), "keyword not parsed");
        require(timing->sample(0) == 0 && timing->sample(1) == 1, "endpoint mismatch");
        require(timing->sample(-.2) == 0 && timing->sample(1.2) == 1, "progress clamp mismatch");
    }
    require(near(motion_timing::parse(std::string("linear"))->sample(.23), .23), "linear timing");
    require(near(motion_timing::parse(std::string("ease"))->sample(.5), .8024033876), "ease reference value");
    require(near(motion_timing::parse(std::string("ease-in-out"))->sample(.5), .5), "symmetric midpoint");
    auto flat = motion_timing::cubic_bezier(0, 1, 0, 1);
    const double t = std::cbrt(1e-12);
    require(near(flat->sample(1e-12), 1 - std::pow(1 - t, 3), 1e-10), "flat endpoint inversion");
    require(near(motion_timing::cubic_bezier(1, 0, 0, 1)->sample(.5), .5), "flat interior inversion");
    require(motion_timing::cubic_bezier(.2, 2, .8, 2)->sample(.5) > 1, "overshoot was clipped");
    require(motion_timing::parse(std::string(" CUBIC-BEZIER( .25, /**/ .1, .25, 1 ) ")).has_value(), "CSS tokens/comments/case");
    auto tokens = normalize(std::string("ease-out"), f_componentize | f_remove_whitespace);
    require(motion_timing::parse(tokens.front()).has_value(), "component token API");
    for (const auto* invalid : {"steps(2)", "step-start", "linear(0,1)", "ease extra", "cubic-bezier(-.1,0,1,1)",
                               "cubic-bezier(0,0,1.1,1)", "cubic-bezier(0 0 1 1)", "cubic-bezier(0,0,1)",
                               "cubic-bezier(0,0,1,1,2)", "cubic-bezier(0%,0,1,1)", "cubic-bezier(0,0,1,1);"})
        require(!motion_timing::parse(std::string(invalid)), "invalid timing accepted");
    require(!motion_timing::cubic_bezier(0, std::numeric_limits<double>::infinity(), 1, 1), "nonfinite control accepted");
    bool rejected = false;
    try { flat->sample(std::numeric_limits<double>::quiet_NaN()); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "nonfinite progress accepted");

    const double largest = std::numeric_limits<double>::max();
    require(motion_detail::lerp(-largest, largest, .5) == 0, "opposite endpoints overflowed");
    require(motion_detail::lerp(largest, largest, .5) == largest, "equal endpoints overflowed");
    require(motion_detail::lerp(1e20, 1, 1) == 1, "endpoint cancellation");
    require(motion_detail::lerp(1, 1e20, 0) == 1, "start endpoint cancellation");
    require(motion_detail::lerp(2, 6, -.25) == 1, "backward extrapolation");
    require(motion_detail::lerp(6, 2, 1.25) == 1, "descending extrapolation");
    for(const auto endpoints : {std::pair<double, double>{1, 1e20}, {1e20, 1}, {-largest, largest}})
    {
        double previous = endpoints.first;
        for(int step = 1; step <= 100; ++step)
        {
            const double value = motion_detail::lerp(endpoints.first, endpoints.second, step / 100.0);
            require(std::isfinite(value), "finite endpoints produced nonfinite interpolation");
            require(endpoints.second > endpoints.first ? value >= previous : value <= previous, "nonmonotonic interpolation");
            previous = value;
        }
    }
    require(interpolate_scalar(2, 6, .25) == 3, "scalar interpolation");
    require(interpolate_scalar(2, 6, 1.25) == 7, "scalar overshoot");
    auto same = interpolate_length(css_length(10, css_units_percentage), css_length(30, css_units_percentage), .5);
    require(same && same->val() == 20 && same->units() == css_units_percentage, "percentage units lost");
    const css_length pixels(10), relative(50, css_units_percentage);
    require(!interpolate_length(pixels, relative, .5), "mixed lengths implicitly converted");
    auto resolved = interpolate_length(pixels, relative, .5, [](const css_length& length) -> std::optional<float>
    { return length.units() == css_units_percentage ? length.val() * 2 : length.val(); });
    require(resolved && resolved->val() == 55 && resolved->units() == css_units_px, "mixed resolved lengths");
    require(!interpolate_length(pixels, relative, .5, [](const css_length&) -> std::optional<float> { return {}; }), "unresolved length accepted");
    require(!interpolate_length(css_length::predef_value(1), pixels, .5), "keyword interpolated numerically");
    require(interpolate_length(css_length::predef_value(1), css_length::predef_value(1), .5)->predef() == 1, "equal keywords rejected");

    const web_color opaque_red(255, 0, 0), clear_blue(0, 0, 255, 0);
    auto blended = interpolate_color(opaque_red, clear_blue, .5);
    require(blended && blended->red == 255 && blended->blue == 0 && blended->alpha == 128, "alpha must be premultiplied");
    require(interpolate_color(opaque_red, clear_blue, 0).value() == opaque_red, "color start endpoint");
    require(interpolate_color(opaque_red, clear_blue, 1).value() == clear_blue, "color end endpoint");
    require(interpolate_color(web_color(255, 0, 0, 0), clear_blue, .5)->alpha == 0, "transparent interpolation");
    require(!interpolate_color(web_color(true), opaque_red, .5), "currentColor resolved without context");
    require(interpolate_color(web_color(0, 0, 0), opaque_red, 1.5)->red == 255, "color overshoot not saturated");
    const auto partial = interpolate_color(web_color(255, 0, 0, 128), web_color(0, 0, 255), .5);
    require(partial && partial->red == 85 && partial->blue == 170 && partial->alpha == 192, "partially transparent endpoints");
    require(interpolate_color(web_color(0, 0, 0), opaque_red, std::numeric_limits<double>::max())->red == 255, "extreme color extrapolation");
    require(!interpolate_length(css_length(0), css_length(2), std::numeric_limits<double>::max()), "overflowing length accepted");
    std::puts("motion timing/interpolation: PASS");
}
