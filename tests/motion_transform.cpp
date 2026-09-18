#include "check.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <litehtml/motion_transform.h>
#include <random>

using namespace litehtml;
namespace
{
    bool near(float a, float b, float tolerance = 0.0005f)
    {
        return std::abs(a - b) <= tolerance * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
    }
    void same(const motion_matrix& a, const motion_matrix& b, float tolerance = 0.0005f)
    {
        for(std::size_t i = 0; i < 16; ++i)
        {
            if(!near(a.values[i], b.values[i], tolerance))
            {
                std::cerr << "matrix mismatch at " << i << ": " << a.values[i] << " vs " << b.values[i] << '\n';
            }
            CHECK(near(a.values[i], b.values[i], tolerance));
        }
    }
    motion_transform_list parse(const std::string& text)
    {
        motion_transform_list result;
        CHECK(motion_transform_list::parse(text, result));
        return result;
    }
    motion_matrix evaluate(const std::string& text, const motion_length_context& context = {})
    {
        motion_matrix result;
        CHECK(parse(text).matrix(context, result));
        return result;
    }
    motion_matrix mix(const std::string& a, const std::string& b, float t, const motion_length_context& context = {})
    {
        motion_matrix result;
        CHECK(motion_transform_list::interpolate(parse(a), parse(b), t, context, result));
        return result;
    }
} // namespace

int main()
{
    const float         pi = std::acos(-1.f);
    const motion_matrix identity;
    same(motion_matrix::translation(10, 20) * motion_matrix::scaling(2, 3),
         evaluate("translate(10px,20px) scale(2,3)"));
    auto css2d = evaluate("matrix(2,3,4,5,6,7)");
    CHECK(near(css2d.values[0], 2) && near(css2d.values[1], 3) && near(css2d.values[4], 4));
    CHECK(near(css2d.values[5], 5) && near(css2d.values[12], 6) && near(css2d.values[13], 7));
    auto rotate = evaluate("rotate(90deg)");
    CHECK(near(rotate.values[0], 0) && near(rotate.values[1], 1) && near(rotate.values[4], -1));
    same(evaluate("rotateX(90deg)"), motion_matrix::rotation(1, 0, 0, pi / 2));
    same(evaluate("rotateY(90deg)"), motion_matrix::rotation(0, 1, 0, pi / 2));
    same(evaluate("rotateZ(90deg)"), rotate);
    same(evaluate("rotate3d(0,0,2,90deg)"), rotate);
    same(evaluate("rotate3d(0,0,0,90deg)"), identity);
    same(evaluate("SCALE3D(2,3,4)"), motion_matrix::scaling(2, 3, 4));
    same(evaluate("scaleX(2) scaleY(3) scaleZ(4)"), motion_matrix::scaling(2, 3, 4));
    same(evaluate("translateX(2px) translateY(3px) translateZ(4px)"), motion_matrix::translation(2, 3, 4));
    same(evaluate("skewX(45deg)"), motion_matrix::skew(pi / 4));
    same(evaluate("skewY(45deg)"), motion_matrix::skew(0, pi / 4));
    CHECK(near(evaluate("perspective(100px)").values[11], -.01f));
    CHECK(near(evaluate("perspective(0)").values[11], -1));
    same(evaluate("matrix3d(1,0,0,0,0,1,0,0,0,0,1,-0.01,2,3,4,0.96)"),
         evaluate("perspective(100px) translate3d(2px,3px,4px)"));

    motion_length_context context;
    context.box_width       = 200;
    context.box_height      = 80;
    context.font_size       = 12;
    context.root_font_size  = 20;
    context.x_height        = 5;
    context.zero_advance    = 7;
    context.viewport_width  = 1000;
    context.viewport_height = 500;
    same(evaluate("translate(50%,2em)", context), motion_matrix::translation(100, 24));
    same(evaluate("translate3d(1rem,3ex,2ch)", context), motion_matrix::translation(20, 15, 14));
    same(evaluate("translate(10vw,10vh)", context), motion_matrix::translation(100, 50));
    same(evaluate("translate(10vmin,10vmax)", context), motion_matrix::translation(50, 100));
    same(evaluate("translate(1in,72pt)", context), motion_matrix::translation(96, 96));
    same(evaluate("translate(2.54cm,25.4mm)", context), motion_matrix::translation(96, 96));
    same(evaluate("translate(6pc,96px)", context), motion_matrix::translation(96, 96));
    motion_angle angle;
    CHECK(motion_angle::parse("0.5turn", angle) && near(angle.radians, pi));
    CHECK(motion_angle::parse("100grad", angle) && near(angle.radians, pi / 2));
    CHECK(motion_angle::parse("1rad", angle) && near(angle.radians, 1));
    CHECK(motion_angle::parse("0", angle) && near(angle.radians, 0));
    CHECK(!motion_angle::parse("3", angle));

    std::mt19937                          random(71265);
    std::uniform_real_distribution<float> coordinate(-20, 20), radians(-pi, pi), scale(.2f, 4), shear(-.4f, .4f);
    for(int i = 0; i < 250; ++i)
    {
        motion_components original;
        original.translation = {coordinate(random), coordinate(random), coordinate(random)};
        original.scale       = {scale(random) * (i % 2 ? -1 : 1), scale(random), scale(random)};
        original.shear       = {shear(random), shear(random), shear(random)};
        original.perspective = {.001f, -.002f, -.003f, 1};
        const auto rotation_matrix =
            motion_matrix::rotation(coordinate(random), coordinate(random), coordinate(random), radians(random));
        motion_components rotation_parts;
        CHECK(rotation_matrix.decompose(rotation_parts));
        original.rotation    = rotation_parts.rotation;
        const auto    matrix = motion_matrix::recompose(original);
        motion_matrix inverse;
        CHECK(matrix.inverse(inverse));
        same(matrix * inverse, identity);
        same(inverse * matrix, identity);
        motion_components parts;
        CHECK(matrix.decompose(parts));
        same(motion_matrix::recompose(parts), matrix);
        motion_matrix interpolated;
        CHECK(motion_matrix::interpolate(identity, matrix, 0, interpolated));
        same(interpolated, identity);
        CHECK(motion_matrix::interpolate(identity, matrix, 1, interpolated));
        same(interpolated, matrix);
    }
    for(int i = 0; i < 100; ++i)
    {
        const auto matrix = motion_matrix::translation(coordinate(random), coordinate(random)) *
                            motion_matrix::rotation(0, 0, 1, radians(random)) * motion_matrix::skew(shear(random)) *
                            motion_matrix::scaling(scale(random) * (i % 2 ? -1 : 1), scale(random));
        motion_components decomposition;
        CHECK(matrix.decompose(decomposition));
        same(matrix, motion_matrix::recompose(decomposition));
    }
    for(float a : {pi, -pi, pi / 2, -pi / 2})
    {
        auto              matrix = motion_matrix::rotation(1, 2, 3, a);
        motion_components parts;
        CHECK(matrix.decompose(parts));
        same(matrix, motion_matrix::recompose(parts));
    }
    motion_components parts;
    auto              homogeneous = motion_matrix::translation(3, 4, 5);
    for(auto& v : homogeneous.values)
    {
        v *= 2;
    }
    CHECK(homogeneous.decompose(parts));
    same(homogeneous, motion_matrix::recompose(parts));

    motion_matrix output    = motion_matrix::translation(42, 0);
    const auto    unchanged = output;
    CHECK(!motion_matrix::scaling(0, 1).inverse(output));
    same(output, unchanged);
    CHECK(!motion_matrix::scaling(0, 1).decompose(parts));
    auto bad      = identity;
    bad.values[0] = std::numeric_limits<float>::infinity();
    CHECK(!bad.inverse(output) && !bad.decompose(parts));
    CHECK(!motion_matrix::interpolate(identity, bad, .5f, output));
    CHECK(!motion_matrix::interpolate(identity, identity, std::numeric_limits<float>::quiet_NaN(), output));

    same(mix("matrix(1,0,0,1,0,0)", "matrix(-1,0,0,1,0,0)", .5f), motion_matrix::scaling(0, 1));
    same(mix("matrix(1,0,0,1,0,0)", "matrix(1,0,0,-1,0,0)", .5f), motion_matrix::scaling(1, 0));
    auto reflection_mix = mix("matrix(-1,0,0,1,0,0)", "matrix(1,0,0,-1,0,0)", .5f);
    CHECK(near(
        reflection_mix.values[0] * reflection_mix.values[5] - reflection_mix.values[1] * reflection_mix.values[4], -1));
    same(mix("translate(10px) rotate(0)", "translate(30px) scale(3)", .5f),
         motion_matrix::translation(20, 0) * motion_matrix::scaling(2, 2));
    same(mix("none", "translate(20px) scale(3)", .5f),
         motion_matrix::translation(10, 0) * motion_matrix::scaling(2, 2));
    same(mix("rotate(0)", "rotate(720deg)", .25f), motion_matrix::rotation(0, 0, 1, pi));
    same(mix("none", "rotate(720deg)", .25f), motion_matrix::rotation(0, 0, 1, pi));
    same(mix("translate(0px)", "translate(50%,50%)", .5f, context), motion_matrix::translation(50, 20));
    CHECK(near(mix("perspective(100px)", "perspective(200px)", .5f).values[11], -.0075f));
    CHECK(near(mix("none", "perspective(100px)", .5f).values[11], -.005f));
    same(mix("rotateX(0)", "rotateY(90deg)", .5f), motion_matrix::rotation(0, 1, 0, pi / 4));
    same(mix("translate(0px)", "translate(10px)", 1.5f), motion_matrix::translation(15, 0));
    for(const auto* text : {"", "none rotate(0)", "rotate(1)", "translate(1px,,2px)", "translate(1px,)",
                            "translateZ(1%)", "matrix(1,0,0,1,0)", "matrix3d(1)", "scale(1px)", "rotate3d(1,0,0)",
                            "perspective(-1px)", "perspective(10%)", "translate(1px 2px)", "translate(calc(1px + 2%))",
                            "rotate(0.00001)", "translate(0.00001)", "rotate(1e40deg)", "unknown(1)", "scale()"})
    {
        auto preserved = parse("translate(42px)");
        CHECK(!motion_transform_list::parse(text, preserved));
        CHECK(preserved.matrix(context, output));
        same(output, motion_matrix::translation(42, 0));
    }
    std::cout << "motion_transform: inverse/decomposition roundtrips, CSS functions, "
                 "relative units, interpolation and invalid-input checks passed\n";
}
