#include "motion_math.h"
#include "motion_transform.h"
#include "css_parser.h"
#include "style.h"
#include "html.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace litehtml
{
    namespace
    {
        using vector3 = std::array<double, 3>;
        bool exact_endpoint(float value, float endpoint)
        {
            return value >= endpoint && value <= endpoint;
        }
        bool is_2d(const motion_matrix& matrix)
        {
            for(auto index : {3u, 7u, 8u, 9u, 11u, 14u})
            {
                if(std::abs(matrix.values[index]) > 0)
                {
                    return false;
                }
            }
            return exact_endpoint(matrix.values[10], matrix.values[15]);
        }
        double dot(const vector3& a, const vector3& b)
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }
        double magnitude(const vector3& a)
        {
            return std::sqrt(dot(a, a));
        }
        void divide(vector3& a, double value)
        {
            for(auto& x : a)
            {
                x /= value;
            }
        }
        void subtract(vector3& a, const vector3& b, double factor)
        {
            for(std::size_t i = 0; i < 3; ++i)
            {
                a[i] -= b[i] * factor;
            }
        }
        std::array<float, 4> quaternion_mix(std::array<float, 4> a, std::array<float, 4> b, float t)
        {
            double cosine = 0;
            for(std::size_t i = 0; i < 4; ++i)
            {
                cosine += static_cast<double>(a[i]) * b[i];
            }
            if(cosine < 0)
            {
                cosine = -cosine;
                for(auto& x : b)
                {
                    x = -x;
                }
            }
            double left = 1 - static_cast<double>(t), right = t;
            if(cosine < 0.9995)
            {
                const double angle = std::acos(std::clamp(cosine, -1.0, 1.0));
                left               = std::sin((1 - static_cast<double>(t)) * angle) / std::sin(angle);
                right              = std::sin(static_cast<double>(t) * angle) / std::sin(angle);
            }
            std::array<float, 4> result{};
            double               length = 0;
            for(std::size_t i = 0; i < 4; ++i)
            {
                result[i]  = static_cast<float>(left * a[i] + right * b[i]);
                length    += static_cast<double>(result[i]) * result[i];
            }
            length = std::sqrt(length);
            if(length > 0)
            {
                for(auto& x : result)
                {
                    x = static_cast<float>(x / length);
                }
            }
            return result;
        }
        bool scalar(const css_token& token, float& value)
        {
            if(token.type != NUMBER || !std::isfinite(token.n.number))
            {
                return false;
            }
            value = token.n.number;
            return true;
        }
        bool length(const css_token& token, css_length& value, bool percent = true, bool positive = false)
        {
            if(token.type == NUMBER && !exact_endpoint(token.n.number, 0))
            {
                return false;
            }
            if(!parse_length(token, value, (percent ? f_length_percentage : f_length) | (positive ? f_positive : 0)))
            {
                return false;
            }
            return std::isfinite(value.val());
        }
        bool angle(const css_token& token, float& radians)
        {
            motion_angle value;
            if(!motion_angle::parse(token, value))
            {
                return false;
            }
            radians = value.radians;
            return true;
        }
        bool arguments(const css_token_vector& tokens, css_token_vector& out)
        {
            bool need_value = true;
            for(const auto& token : tokens)
            {
                if(token.type == WHITESPACE)
                {
                    continue;
                }
                if(need_value)
                {
                    if(token.type == COMMA)
                    {
                        return false;
                    }
                    out.push_back(token);
                } else if(token.type != COMMA)
                {
                    return false;
                }
                need_value = !need_value;
            }
            return !need_value;
        }
        motion_transform identity_for(const motion_transform& reference)
        {
            motion_transform result;
            result.kind = reference.kind;
            if(result.kind == motion_transform_kind::scale)
            {
                result.numbers = {1, 1, 1, 0};
            }
            if(result.kind == motion_transform_kind::rotate)
            {
                result.numbers = {reference.numbers[0], reference.numbers[1], reference.numbers[2], 0};
            }
            if(result.kind == motion_transform_kind::perspective)
            {
                result.kind = motion_transform_kind::matrix;
            }
            return result;
        }
        bool suffix_matrix(const motion_transform_list& list, std::size_t first, const motion_length_context& context,
                           motion_matrix& result)
        {
            result = {};
            for(std::size_t i = first; i < list.operations.size(); ++i)
            {
                motion_matrix next;
                if(!list.operations[i].matrix(context, next))
                {
                    return false;
                }
                result = result * next;
            }
            return result.finite();
        }
    } // namespace

    bool motion_matrix::finite() const
    {
        return std::all_of(values.begin(), values.end(), [](float x) { return std::isfinite(x); });
    }
    motion_matrix motion_matrix::translation(float x, float y, float z)
    {
        motion_matrix result;
        result.values[12] = x;
        result.values[13] = y;
        result.values[14] = z;
        return result;
    }
    motion_matrix motion_matrix::scaling(float x, float y, float z)
    {
        motion_matrix result;
        result.values[0]  = x;
        result.values[5]  = y;
        result.values[10] = z;
        return result;
    }
    motion_matrix motion_matrix::rotation(float x, float y, float z, float radians)
    {
        const double norm = magnitude({x, y, z});
        if(norm <= 0)
        {
            return {};
        }
        const double  a = x / norm, b = y / norm, c = z / norm;
        const double  cosine = std::cos(radians), sine = std::sin(radians), v = 1 - cosine;
        motion_matrix result;
        result.values[0]  = static_cast<float>(a * a * v + cosine);
        result.values[1]  = static_cast<float>(b * a * v + c * sine);
        result.values[2]  = static_cast<float>(c * a * v - b * sine);
        result.values[4]  = static_cast<float>(a * b * v - c * sine);
        result.values[5]  = static_cast<float>(b * b * v + cosine);
        result.values[6]  = static_cast<float>(c * b * v + a * sine);
        result.values[8]  = static_cast<float>(a * c * v + b * sine);
        result.values[9]  = static_cast<float>(b * c * v - a * sine);
        result.values[10] = static_cast<float>(c * c * v + cosine);
        return result;
    }
    motion_matrix motion_matrix::skew(float x, float y)
    {
        motion_matrix result;
        result.values[4] = std::tan(x);
        result.values[1] = std::tan(y);
        return result;
    }
    motion_matrix motion_matrix::perspective(float distance)
    {
        motion_matrix result;
        result.values[11] = -1 / std::max(distance, 1.0f);
        return result;
    }
    motion_matrix motion_matrix::operator*(const motion_matrix& right) const
    {
        motion_matrix result;
        for(std::size_t col = 0; col < 4; ++col)
        {
            for(std::size_t row = 0; row < 4; ++row)
            {
                double sum = 0;
                for(std::size_t k = 0; k < 4; ++k)
                {
                    sum += static_cast<double>(values[k * 4 + row]) * right.values[col * 4 + k];
                }
                result.values[col * 4 + row] = static_cast<float>(sum);
            }
        }
        return result;
    }
    bool motion_matrix::inverse(motion_matrix& out) const
    {
        if(!finite())
        {
            return false;
        }
        double work[4][8]{};
        for(std::size_t row = 0; row < 4; ++row)
        {
            for(std::size_t col = 0; col < 4; ++col)
            {
                work[row][col]     = values[col * 4 + row];
                work[row][col + 4] = row == col ? 1 : 0;
            }
        }
        for(std::size_t col = 0; col < 4; ++col)
        {
            std::size_t pivot = col;
            for(std::size_t row = col + 1; row < 4; ++row)
            {
                if(std::abs(work[row][col]) > std::abs(work[pivot][col]))
                {
                    pivot = row;
                }
            }
            if(std::abs(work[pivot][col]) < 1e-15)
            {
                return false;
            }
            if(pivot != col)
            {
                for(std::size_t k = 0; k < 8; ++k)
                {
                    std::swap(work[pivot][k], work[col][k]);
                }
            }
            const double divisor = work[col][col];
            for(double& value : work[col])
            {
                value /= divisor;
            }
            for(std::size_t row = 0; row < 4; ++row)
            {
                if(row == col)
                {
                    continue;
                }
                const double factor = work[row][col];
                for(std::size_t k = 0; k < 8; ++k)
                {
                    work[row][k] -= factor * work[col][k];
                }
            }
        }
        motion_matrix result;
        for(std::size_t row = 0; row < 4; ++row)
        {
            for(std::size_t col = 0; col < 4; ++col)
            {
                result.values[col * 4 + row] = static_cast<float>(work[row][col + 4]);
            }
        }
        if(!result.finite())
        {
            return false;
        }
        out = result;
        return true;
    }

    bool motion_matrix::decompose(motion_components& out) const
    {
        if(!finite() || std::abs(values[15]) < 1e-12f)
        {
            return false;
        }
        motion_components result;
        result.homogeneous_scale = values[15];
        motion_matrix affine     = *this;
        for(auto& x : affine.values)
        {
            x /= result.homogeneous_scale;
        }
        const std::array<float, 4> bottom{affine.values[3], affine.values[7], affine.values[11], affine.values[15]};
        affine.values[3] = affine.values[7] = affine.values[11] = 0;
        affine.values[15]                                       = 1;
        motion_matrix inverse_affine;
        if(!affine.inverse(inverse_affine))
        {
            return false;
        }
        // Factoring M = P * A isolates projection without contaminating the affine rotation.
        for(std::size_t i = 0; i < 4; ++i)
        {
            double value = 0;
            for(std::size_t j = 0; j < 4; ++j)
            {
                value += static_cast<double>(inverse_affine.values[i * 4 + j]) * bottom[j];
            }
            result.perspective[i] = static_cast<float>(value);
        }
        result.translation = {affine.values[12], affine.values[13], affine.values[14]};
        std::array<vector3, 3> columns{};
        for(std::size_t i = 0; i < 3; ++i)
        {
            for(std::size_t j = 0; j < 3; ++j)
            {
                columns[i][j] = affine.values[i * 4 + j];
            }
        }
        double sx = magnitude(columns[0]);
        if(sx < 1e-12)
        {
            return false;
        }
        divide(columns[0], sx);
        double xy = dot(columns[0], columns[1]);
        subtract(columns[1], columns[0], xy);
        double sy = magnitude(columns[1]);
        if(sy < 1e-12)
        {
            return false;
        }
        divide(columns[1], sy);
        xy        /= sy;
        double xz  = dot(columns[0], columns[2]);
        subtract(columns[2], columns[0], xz);
        double yz = dot(columns[1], columns[2]);
        subtract(columns[2], columns[1], yz);
        double sz = magnitude(columns[2]);
        if(sz < 1e-12)
        {
            return false;
        }
        divide(columns[2], sz);
        xz /= sz;
        yz /= sz;
        const vector3 cross{columns[1][1] * columns[2][2] - columns[1][2] * columns[2][1],
                            columns[1][2] * columns[2][0] - columns[1][0] * columns[2][2],
                            columns[1][0] * columns[2][1] - columns[1][1] * columns[2][0]};
        if(dot(columns[0], cross) < 0)
        {
            if(is_2d(*this))
            {
                // A 2D reflection must not introduce a flip of the unrelated Z axis.
                const bool        flip_x = affine.values[0] < affine.values[5];
                const std::size_t axis   = flip_x ? 0 : 1;
                if(flip_x)
                {
                    sx = -sx;
                    xz = -xz;
                } else
                {
                    sy = -sy;
                    yz = -yz;
                }
                xy = -xy;
                for(auto& x : columns[axis])
                {
                    x = -x;
                }
            } else
            {
                sx = -sx;
                sy = -sy;
                sz = -sz;
                for(auto& col : columns)
                {
                    for(auto& x : col)
                    {
                        x = -x;
                    }
                }
            }
        }
        result.scale                = {static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)};
        result.shear                = {static_cast<float>(xy), static_cast<float>(xz), static_cast<float>(yz)};
        const double          trace = columns[0][0] + columns[1][1] + columns[2][2];
        std::array<double, 4> q{};
        if(trace > 0)
        {
            const double factor = std::sqrt(trace + 1) * 2;
            q                   = {(columns[1][2] - columns[2][1]) / factor, (columns[2][0] - columns[0][2]) / factor,
                                   (columns[0][1] - columns[1][0]) / factor, factor / 4};
        } else
        {
            std::size_t i = 0;
            if(columns[1][1] > columns[i][i])
            {
                i = 1;
            }
            if(columns[2][2] > columns[i][i])
            {
                i = 2;
            }
            const std::size_t j = (i + 1) % 3, k = (i + 2) % 3;
            const double      factor = std::sqrt(std::max(0.0, 1 + columns[i][i] - columns[j][j] - columns[k][k])) * 2;
            if(factor < 1e-12)
            {
                return false;
            }
            q[i] = factor / 4;
            q[j] = (columns[i][j] + columns[j][i]) / factor;
            q[k] = (columns[i][k] + columns[k][i]) / factor;
            q[3] = (columns[j][k] - columns[k][j]) / factor;
        }
        double norm = 0;
        for(auto x : q)
        {
            norm += x * x;
        }
        norm = std::sqrt(norm);
        for(std::size_t i = 0; i < 4; ++i)
        {
            result.rotation[i] = static_cast<float>(q[i] / norm);
        }
        out = result;
        return true;
    }

    motion_matrix motion_matrix::recompose(const motion_components& c)
    {
        motion_matrix projection;
        projection.values[3]  = c.perspective[0];
        projection.values[7]  = c.perspective[1];
        projection.values[11] = c.perspective[2];
        projection.values[15] = c.perspective[3];
        const auto&   q       = c.rotation;
        const double  norm    = std::sqrt(static_cast<double>(q[0]) * q[0] + static_cast<double>(q[1]) * q[1] +
                                          static_cast<double>(q[2]) * q[2] + static_cast<double>(q[3]) * q[3]);
        motion_matrix rotate;
        if(norm > 0)
        {
            const float x = static_cast<float>(q[0] / norm), y = static_cast<float>(q[1] / norm);
            const float z = static_cast<float>(q[2] / norm), w = static_cast<float>(q[3] / norm);
            rotate.values[0]  = 1 - 2 * (y * y + z * z);
            rotate.values[1]  = 2 * (x * y + z * w);
            rotate.values[2]  = 2 * (x * z - y * w);
            rotate.values[4]  = 2 * (x * y - z * w);
            rotate.values[5]  = 1 - 2 * (x * x + z * z);
            rotate.values[6]  = 2 * (y * z + x * w);
            rotate.values[8]  = 2 * (x * z + y * w);
            rotate.values[9]  = 2 * (y * z - x * w);
            rotate.values[10] = 1 - 2 * (x * x + y * y);
        }
        motion_matrix shear;
        shear.values[4] = c.shear[0];
        shear.values[8] = c.shear[1];
        shear.values[9] = c.shear[2];
        auto result = projection * translation(c.translation[0], c.translation[1], c.translation[2]) * rotate * shear *
                      scaling(c.scale[0], c.scale[1], c.scale[2]);
        for(auto& x : result.values)
        {
            x *= c.homogeneous_scale;
        }
        return result;
    }
    bool motion_matrix::interpolate(const motion_matrix& from, const motion_matrix& to, float t, motion_matrix& out)
    {
        if(!std::isfinite(t) || !from.finite() || !to.finite())
        {
            return false;
        }
        if(exact_endpoint(t, 0))
        {
            out = from;
            return true;
        }
        if(exact_endpoint(t, 1))
        {
            out = to;
            return true;
        }
        motion_components a, b, value;
        if(!from.decompose(a) || !to.decompose(b))
        {
            return false;
        }
        if(is_2d(from) && is_2d(to) && ((a.scale[0] < 0 && b.scale[1] < 0) || (a.scale[1] < 0 && b.scale[0] < 0)))
        {
            // Equivalent sign choices avoid collapsing both axes between two reflections.
            a.scale[0]   = -a.scale[0];
            a.scale[1]   = -a.scale[1];
            const auto q = a.rotation;
            a.rotation   = {-q[1], q[0], q[3], -q[2]};
        }
        for(std::size_t i = 0; i < 3; ++i)
        {
            value.translation[i] = static_cast<float>(motion_detail::lerp(a.translation[i], b.translation[i], t));
            value.scale[i]       = static_cast<float>(motion_detail::lerp(a.scale[i], b.scale[i], t));
            value.shear[i]       = static_cast<float>(motion_detail::lerp(a.shear[i], b.shear[i], t));
        }
        for(std::size_t i = 0; i < 4; ++i)
        {
            value.perspective[i] = static_cast<float>(motion_detail::lerp(a.perspective[i], b.perspective[i], t));
        }
        value.rotation          = quaternion_mix(a.rotation, b.rotation, t);
        value.homogeneous_scale = static_cast<float>(motion_detail::lerp(a.homogeneous_scale, b.homogeneous_scale, t));
        auto result             = recompose(value);
        if(!result.finite())
        {
            return false;
        }
        out = result;
        return true;
    }

    bool motion_angle::parse(const css_token& token, motion_angle& out)
    {
        float degrees = 0;
        if(token.type == NUMBER && !exact_endpoint(token.n.number, 0))
        {
            return false;
        }
        if(!parse_angle(token, degrees) || !std::isfinite(degrees))
        {
            return false;
        }
        const float radians = degrees * (3.14159265358979323846f / 180);
        if(!std::isfinite(radians))
        {
            return false;
        }
        out.radians = radians;
        return true;
    }
    bool motion_angle::parse(const std::string& text, motion_angle& out)
    {
        auto tokens = normalize(text, f_componentize | f_remove_whitespace);
        return tokens.size() == 1 && parse(tokens[0], out);
    }
    bool motion_resolve_length(const css_length& length, float basis, const motion_length_context& c, float& out)
    {
        if(length.is_predefined() || !std::isfinite(length.val()))
        {
            return false;
        }
        float factor = 0;
        switch(length.units())
        {
        case css_units_none:
        case css_units_px:
            factor = 1;
            break;
        case css_units_percentage:
            factor = basis / 100;
            break;
        case css_units_in:
            factor = 96;
            break;
        case css_units_cm:
            factor = 96.0f / 2.54f;
            break;
        case css_units_mm:
            factor = 96.0f / 25.4f;
            break;
        case css_units_pt:
            factor = 96.0f / 72;
            break;
        case css_units_pc:
            factor = 16;
            break;
        case css_units_em:
            factor = c.font_size;
            break;
        case css_units_rem:
            factor = c.root_font_size;
            break;
        case css_units_ex:
            factor = c.x_height;
            break;
        case css_units_ch:
            factor = c.zero_advance;
            break;
        case css_units_vw:
            factor = c.viewport_width / 100;
            break;
        case css_units_vh:
            factor = c.viewport_height / 100;
            break;
        case css_units_vmin:
            factor = std::min(c.viewport_width, c.viewport_height) / 100;
            break;
        case css_units_vmax:
            factor = std::max(c.viewport_width, c.viewport_height) / 100;
            break;
        default:
            return false;
        }
        const float result = length.val() * factor;
        if(!std::isfinite(result))
        {
            return false;
        }
        out = result;
        return true;
    }
    bool motion_transform::matrix(const motion_length_context& c, motion_matrix& out) const
    {
        motion_matrix result;
        float         x = 0, y = 0, z = 0;
        switch(kind)
        {
        case motion_transform_kind::matrix:
            result = matrix_value;
            break;
        case motion_transform_kind::translate:
            if(!motion_resolve_length(lengths[0], c.box_width, c, x) ||
               !motion_resolve_length(lengths[1], c.box_height, c, y) || !motion_resolve_length(lengths[2], 0, c, z))
            {
                return false;
            }
            result = motion_matrix::translation(x, y, z);
            break;
        case motion_transform_kind::scale:
            result = motion_matrix::scaling(numbers[0], numbers[1], numbers[2]);
            break;
        case motion_transform_kind::rotate:
            result = motion_matrix::rotation(numbers[0], numbers[1], numbers[2], numbers[3]);
            break;
        case motion_transform_kind::skew:
            result = motion_matrix::skew(numbers[0], numbers[1]);
            break;
        case motion_transform_kind::perspective:
            if(!motion_resolve_length(lengths[0], 0, c, x) || x < 0)
            {
                return false;
            }
            result = motion_matrix::perspective(x);
            break;
        }
        if(!result.finite())
        {
            return false;
        }
        out = result;
        return true;
    }

    bool motion_transform_list::parse(const std::string& text, motion_transform_list& out)
    {
        return parse(normalize(text, f_componentize), out);
    }
    bool motion_transform_list::parse(const css_token_vector& input, motion_transform_list& out)
    {
        css_token_vector tokens;
        for(const auto& token : input)
        {
            if(token.type != WHITESPACE)
            {
                tokens.push_back(token);
            }
        }
        if(tokens.size() == 1 && tokens[0].ident() == "none")
        {
            out = {};
            return true;
        }
        if(tokens.empty())
        {
            return false;
        }
        motion_transform_list result;
        for(const auto& token : tokens)
        {
            if(token.type != CV_FUNCTION)
            {
                return false;
            }
            css_token_vector args;
            if(!arguments(token.value, args))
            {
                return false;
            }
            const auto       name  = lowcase(token.name());
            const auto       count = args.size();
            motion_transform op;
            if(name == "matrix" || name == "matrix3d")
            {
                if(count != (name == "matrix" ? 6u : 16u))
                {
                    return false;
                }
                if(count == 16)
                {
                    for(std::size_t i = 0; i < 16; ++i)
                    {
                        if(!scalar(args[i], op.matrix_value.values[i]))
                        {
                            return false;
                        }
                    }
                } else
                {
                    const std::array<std::size_t, 6> indices{0, 1, 4, 5, 12, 13};
                    for(std::size_t i = 0; i < 6; ++i)
                    {
                        if(!scalar(args[i], op.matrix_value.values[indices[i]]))
                        {
                            return false;
                        }
                    }
                }
            } else if(name == "translate" || name == "translatex" || name == "translatey" || name == "translatez" ||
                      name == "translate3d")
            {
                op.kind = motion_transform_kind::translate;
                if(name == "translate")
                {
                    if(count < 1 || count > 2 || !length(args[0], op.lengths[0]))
                    {
                        return false;
                    }
                    if(count == 2 && !length(args[1], op.lengths[1]))
                    {
                        return false;
                    }
                } else if(name == "translate3d")
                {
                    if(count != 3)
                    {
                        return false;
                    }
                    for(std::size_t i = 0; i < 3; ++i)
                    {
                        if(!length(args[i], op.lengths[i], i != 2))
                        {
                            return false;
                        }
                    }
                } else
                {
                    const std::size_t axis = name == "translatex" ? 0 : name == "translatey" ? 1 : 2;
                    if(count != 1 || !length(args[0], op.lengths[axis], axis != 2))
                    {
                        return false;
                    }
                }
            } else if(name == "scale" || name == "scalex" || name == "scaley" || name == "scalez" || name == "scale3d")
            {
                op.kind    = motion_transform_kind::scale;
                op.numbers = {1, 1, 1, 0};
                if(name == "scale")
                {
                    if(count < 1 || count > 2 || !scalar(args[0], op.numbers[0]))
                    {
                        return false;
                    }
                    op.numbers[1] = op.numbers[0];
                    if(count == 2 && !scalar(args[1], op.numbers[1]))
                    {
                        return false;
                    }
                } else if(name == "scale3d")
                {
                    if(count != 3)
                    {
                        return false;
                    }
                    for(std::size_t i = 0; i < 3; ++i)
                    {
                        if(!scalar(args[i], op.numbers[i]))
                        {
                            return false;
                        }
                    }
                } else
                {
                    const std::size_t axis = name == "scalex" ? 0 : name == "scaley" ? 1 : 2;
                    if(count != 1 || !scalar(args[0], op.numbers[axis]))
                    {
                        return false;
                    }
                }
            } else if(name == "rotate" || name == "rotatex" || name == "rotatey" || name == "rotatez" ||
                      name == "rotate3d")
            {
                op.kind = motion_transform_kind::rotate;
                if(name == "rotate3d")
                {
                    if(count != 4)
                    {
                        return false;
                    }
                    for(std::size_t i = 0; i < 3; ++i)
                    {
                        if(!scalar(args[i], op.numbers[i]))
                        {
                            return false;
                        }
                    }
                    if(!angle(args[3], op.numbers[3]))
                    {
                        return false;
                    }
                } else
                {
                    op.numbers[name == "rotatex" ? 0u : name == "rotatey" ? 1u : 2u] = 1;
                    if(count != 1 || !angle(args[0], op.numbers[3]))
                    {
                        return false;
                    }
                }
            } else if(name == "skew" || name == "skewx" || name == "skewy")
            {
                op.kind = motion_transform_kind::skew;
                if(name == "skew")
                {
                    if(count < 1 || count > 2 || !angle(args[0], op.numbers[0]))
                    {
                        return false;
                    }
                    if(count == 2 && !angle(args[1], op.numbers[1]))
                    {
                        return false;
                    }
                } else if(count != 1 || !angle(args[0], op.numbers[name == "skewx" ? 0u : 1u]))
                {
                    return false;
                }
            } else if(name == "perspective")
            {
                op.kind = motion_transform_kind::perspective;
                if(count != 1 || !length(args[0], op.lengths[0], false, true))
                {
                    return false;
                }
            } else
            {
                return false;
            }
            result.operations.push_back(op);
        }
        out = std::move(result);
        return true;
    }

    bool motion_transform_list::matrix(const motion_length_context& context, motion_matrix& out) const
    {
        motion_matrix result;
        if(!suffix_matrix(*this, 0, context, result))
        {
            return false;
        }
        out = result;
        return true;
    }
    bool motion_transform_list::interpolate(const motion_transform_list& from, const motion_transform_list& to, float t,
                                            const motion_length_context& context, motion_matrix& out)
    {
        if(!std::isfinite(t))
        {
            return false;
        }
        if(exact_endpoint(t, 0))
        {
            return from.matrix(context, out);
        }
        if(exact_endpoint(t, 1))
        {
            return to.matrix(context, out);
        }
        motion_matrix result;
        for(std::size_t i = 0; i < std::max(from.operations.size(), to.operations.size()); ++i)
        {
            const auto    a = i < from.operations.size() ? from.operations[i] : identity_for(to.operations[i]);
            const auto    b = i < to.operations.size() ? to.operations[i] : identity_for(from.operations[i]);
            motion_matrix left, right, value;
            if(a.kind != b.kind)
            {
                if(!suffix_matrix(from, i, context, left) || !suffix_matrix(to, i, context, right) ||
                   !motion_matrix::interpolate(left, right, t, value))
                {
                    return false;
                }
                result = result * value;
                break;
            }
            if(!a.matrix(context, left) || !b.matrix(context, right))
            {
                return false;
            }
            switch(a.kind)
            {
            case motion_transform_kind::translate:
                value = motion_matrix::translation(static_cast<float>(motion_detail::lerp(left.values[12], right.values[12], t)),
                                                   static_cast<float>(motion_detail::lerp(left.values[13], right.values[13], t)),
                                                   static_cast<float>(motion_detail::lerp(left.values[14], right.values[14], t)));
                break;
            case motion_transform_kind::scale:
                value = motion_matrix::scaling(static_cast<float>(motion_detail::lerp(a.numbers[0], b.numbers[0], t)),
                                               static_cast<float>(motion_detail::lerp(a.numbers[1], b.numbers[1], t)),
                                               static_cast<float>(motion_detail::lerp(a.numbers[2], b.numbers[2], t)));
                break;
            case motion_transform_kind::rotate:
                {
                    vector3 axis_a{a.numbers[0], a.numbers[1], a.numbers[2]},
                        axis_b{b.numbers[0], b.numbers[1], b.numbers[2]};
                    const double norm_a = magnitude(axis_a), norm_b = magnitude(axis_b);
                    if(norm_a > 0 && norm_b > 0)
                    {
                        divide(axis_a, norm_a);
                        divide(axis_b, norm_b);
                    }
                    const double parallel = dot(axis_a, axis_b);
                    if(norm_a > 0 && norm_b > 0 && std::abs(parallel) > 1 - 1e-7)
                    {
                        value = motion_matrix::rotation(
                            a.numbers[0], a.numbers[1], a.numbers[2],
                            static_cast<float>(motion_detail::lerp(a.numbers[3], parallel < 0 ? -b.numbers[3] : b.numbers[3], t)));
                    } else if(!motion_matrix::interpolate(left, right, t, value))
                    {
                        return false;
                    }
                    break;
                }
            case motion_transform_kind::skew:
                value = motion_matrix::skew(static_cast<float>(motion_detail::lerp(a.numbers[0], b.numbers[0], t)),
                                            static_cast<float>(motion_detail::lerp(a.numbers[1], b.numbers[1], t)));
                break;
            case motion_transform_kind::perspective:
                value.values[11] = static_cast<float>(motion_detail::lerp(left.values[11], right.values[11], t));
                break;
            case motion_transform_kind::matrix:
                if(!motion_matrix::interpolate(left, right, t, value))
                {
                    return false;
                }
                break;
            }
            result = result * value;
        }
        if(!result.finite())
        {
            return false;
        }
        out = result;
        return true;
    }
} // namespace litehtml
