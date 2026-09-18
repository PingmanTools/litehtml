#ifndef LITEHTML_MOTION_STYLE_H
#define LITEHTML_MOTION_STYLE_H

#include "motion_timing.h"
#include "motion_transform.h"
#include <string>
#include <vector>

namespace litehtml
{
    using motion_numbers = std::vector<double>;
    using motion_timings = std::vector<motion_timing>;
    enum class motion_direction
    {
        normal,
        reverse,
        alternate,
        alternate_reverse
    };
    enum class motion_fill
    {
        none,
        forwards,
        backwards,
        both
    };
    enum class motion_play
    {
        running,
        paused
    };
    struct motion_name
    {
        std::string value   = "none";
        bool        is_none = true;
    };
    using motion_names = std::vector<motion_name>;
    struct motion_animation
    {
        std::string      name     = "none";
        bool             is_none  = true;
        double           duration = 0, delay = 0, iterations = 1;
        motion_timing    timing;
        motion_direction direction  = motion_direction::normal;
        motion_fill      fill_mode  = motion_fill::none;
        motion_play      play_state = motion_play::running;
    };
    struct motion_transition
    {
        std::string   property = "all";
        double        duration = 0, delay = 0;
        motion_timing timing;
    };
    struct motion_properties
    {
        float                 opacity = 1;
        motion_transform_list transform;
        length_vector transform_origin{css_length(50, css_units_percentage), css_length(50, css_units_percentage),
                                       css_length(0)};
        length_vector perspective_origin{css_length(50, css_units_percentage), css_length(50, css_units_percentage)};
        css_length    perspective     = css_length::predef_value();
        int           transform_style = 0, backface_visibility = 0;
        motion_names  animation_names{motion_name()};
        std::vector<std::string>       transition_properties{"all"};
        motion_numbers                 animation_durations{0}, animation_delays{0}, animation_iterations{1};
        motion_numbers                 transition_durations{0}, transition_delays{0};
        motion_timings                 animation_timings{motion_timing()}, transition_timings{motion_timing()};
        std::vector<int>               animation_directions{0}, animation_fills{0}, animation_plays{0};
        std::vector<motion_animation>  animations;
        std::vector<motion_transition> transitions;
        void                           assemble();
    };
} // namespace litehtml
#endif
