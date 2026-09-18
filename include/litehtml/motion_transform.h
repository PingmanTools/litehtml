#ifndef LITEHTML_MOTION_TRANSFORM_H
#define LITEHTML_MOTION_TRANSFORM_H

#include "css_length.h"
#include <array>
#include <string>
#include <vector>

namespace litehtml
{
    struct motion_components
    {
        std::array<float, 3> translation{0, 0, 0};
        std::array<float, 3> scale{1, 1, 1};
        std::array<float, 3> shear{0, 0, 0};
        std::array<float, 4> rotation{0, 0, 0, 1};
        std::array<float, 4> perspective{0, 0, 0, 1};
        float                homogeneous_scale = 1;
    };

    struct motion_matrix
    {
        // Column vectors and CSS matrix3d() share this column-major representation.
        std::array<float, 16> values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        static motion_matrix  translation(float x, float y, float z = 0);
        static motion_matrix  scaling(float x, float y, float z = 1);
        static motion_matrix  rotation(float x, float y, float z, float radians);
        static motion_matrix  skew(float x_radians, float y_radians = 0);
        static motion_matrix  perspective(float distance);
        motion_matrix         operator*(const motion_matrix& right) const;
        bool                  inverse(motion_matrix& out) const;
        bool                  decompose(motion_components& out) const;
        static motion_matrix  recompose(const motion_components& components);
        static bool interpolate(const motion_matrix& from, const motion_matrix& to, float progress, motion_matrix& out);
        bool        finite() const;
    };

    struct motion_angle
    {
        float       radians = 0;
        static bool parse(const css_token& token, motion_angle& out);
        static bool parse(const std::string& text, motion_angle& out);
    };

    struct motion_length_context
    {
        float box_width       = 0;
        float box_height      = 0;
        float font_size       = 16;
        float root_font_size  = 16;
        float x_height        = 8;
        float zero_advance    = 8;
        float viewport_width  = 0;
        float viewport_height = 0;
    };

    bool motion_resolve_length(const css_length& length, float percentage_basis, const motion_length_context& context,
                               float& out);

    enum class motion_transform_kind
    {
        matrix,
        translate,
        scale,
        rotate,
        skew,
        perspective
    };

    struct motion_transform
    {
        motion_transform_kind     kind = motion_transform_kind::matrix;
        motion_matrix             matrix_value;
        std::array<css_length, 3> lengths{};
        std::array<float, 4>      numbers{};
        bool                      matrix(const motion_length_context& context, motion_matrix& out) const;
    };

    struct motion_transform_list
    {
        std::vector<motion_transform> operations;
        static bool                   parse(const std::string& text, motion_transform_list& out);
        static bool                   parse(const css_token_vector& tokens, motion_transform_list& out);
        bool                          matrix(const motion_length_context& context, motion_matrix& out) const;
        // Matching primitives retain rotation winding; unmatched suffixes use matrix decomposition.
        static bool interpolate(const motion_transform_list& from, const motion_transform_list& to, float progress,
                                const motion_length_context& context, motion_matrix& out);
    };
} // namespace litehtml

#endif
