#include "html.h"
#include "motion_style.h"
#include "style.h"
#include "css_parser.h"
#include "css_properties.h"
#include "html_tag.h"
#include <cmath>
#include <limits>
#include <cstddef>

namespace litehtml
{
    namespace
    {
        bool global_keyword(const std::string& value)
        {
            auto text = lowcase(value);
            return text == "inherit" || text == "initial" || text == "unset" || text == "revert" ||
                   text == "revert-layer";
        }
        bool name_token(const css_token& token, std::string& name, bool strings)
        {
            if(strings && token.type == STRING)
            {
                name = token.str();
                return true;
            }
            if(token.type != IDENT || global_keyword(token.ident()) || lowcase(token.ident()) == "default")
            {
                return false;
            }
            name = token.name();
            return true;
        }
        bool time_token(const css_token& token, double& seconds, bool nonnegative)
        {
            if(token.type != DIMENSION || !std::isfinite(token.n.number))
            {
                return false;
            }
            auto unit = lowcase(token.unit());
            if(unit != "s" && unit != "ms")
            {
                return false;
            }
            seconds = token.n.number * (unit == "ms" ? .001 : 1);
            return !nonnegative || seconds >= 0;
        }
        bool iteration_token(const css_token& token, double& value)
        {
            if(lowcase(token.ident()) == "infinite")
            {
                value = std::numeric_limits<double>::infinity();
                return true;
            }
            if(token.type != NUMBER || !std::isfinite(token.n.number) || token.n.number < 0)
            {
                return false;
            }
            value = token.n.number;
            return true;
        }
        int keyword_index(const css_token& token, std::initializer_list<const char*> values)
        {
            auto name  = lowcase(token.ident());
            int  index = 0;
            for(auto value : values)
            {
                if(name == value)
                {
                    return index;
                }
                ++index;
            }
            return -1;
        }
        bool origin_value(css_token_vector tokens, length_vector& result, bool allow_z)
        {
            if(tokens.empty() || tokens.size() > (allow_z ? 3u : 2u))
            {
                return false;
            }
            css_length z(0);
            if(tokens.size() == 3)
            {
                if(!z.from_token(tokens.back(), f_length) || !std::isfinite(z.val()))
                {
                    return false;
                }
                tokens.pop_back();
            }
            auto axis = [](const css_token& token) {
                auto value = lowcase(token.ident());
                return value == "left" || value == "right" ? 1 : value == "top" || value == "bottom" ? 2 : 0;
            };
            auto coordinate = [](const css_token& token, int wanted, css_length& output) {
                auto value = lowcase(token.ident());
                if(value == "center")
                {
                    output = css_length(50, css_units_percentage);
                    return true;
                }
                if((wanted == 1 && (value == "left" || value == "right")) ||
                   (wanted == 2 && (value == "top" || value == "bottom")))
                {
                    output = css_length(value == "left" || value == "top" ? 0.f : 100.f, css_units_percentage);
                    return true;
                }
                return output.from_token(token, f_length_percentage) && std::isfinite(output.val());
            };
            css_length x(50, css_units_percentage), y(50, css_units_percentage);
            if(tokens.size() == 1)
            {
                if(!coordinate(tokens[0], axis(tokens[0]) == 2 ? 2 : 1, axis(tokens[0]) == 2 ? y : x))
                {
                    return false;
                }
            } else
            {
                if(axis(tokens[0]) == 2 || axis(tokens[1]) == 1)
                {
                    if(tokens[0].type != IDENT || tokens[1].type != IDENT)
                    {
                        return false;
                    }
                    std::swap(tokens[0], tokens[1]);
                }
                if(!coordinate(tokens[0], 1, x) || !coordinate(tokens[1], 2, y))
                {
                    return false;
                }
            }
            result = {x, y};
            if(allow_z)
            {
                result.push_back(z);
            }
            return true;
        }
    } // namespace

    void motion_properties::assemble()
    {
        animations.clear();
        transitions.clear();
        for(size_t i = 0; i < animation_names.size(); ++i)
        {
            motion_animation value;
            value.name       = animation_names[i].value;
            value.is_none    = animation_names[i].is_none;
            value.duration   = animation_durations[i % animation_durations.size()];
            value.delay      = animation_delays[i % animation_delays.size()];
            value.iterations = animation_iterations[i % animation_iterations.size()];
            value.timing     = animation_timings[i % animation_timings.size()];
            value.direction  = static_cast<motion_direction>(animation_directions[i % animation_directions.size()]);
            value.fill_mode  = static_cast<motion_fill>(animation_fills[i % animation_fills.size()]);
            value.play_state = static_cast<motion_play>(animation_plays[i % animation_plays.size()]);
            animations.push_back(value);
        }
        for(size_t i = 0; i < transition_properties.size(); ++i)
        {
            motion_transition value;
            value.property = transition_properties[i];
            value.duration = transition_durations[i % transition_durations.size()];
            value.delay    = transition_delays[i % transition_delays.size()];
            value.timing   = transition_timings[i % transition_timings.size()];
            transitions.push_back(value);
        }
    }

    bool style::parse_motion_property(string_id name, const css_token_vector& tokens, bool important)
    {
        if(name < _animation_ || name > _backface_visibility_)
        {
            return false;
        }
        const std::vector<string_id> animation_ids{_animation_name_,
                                                   _animation_duration_,
                                                   _animation_delay_,
                                                   _animation_timing_function_,
                                                   _animation_iteration_count_,
                                                   _animation_direction_,
                                                   _animation_fill_mode_,
                                                   _animation_play_state_};
        const std::vector<string_id> transition_ids{_transition_property_, _transition_duration_, _transition_delay_,
                                                    _transition_timing_function_};
        motion_properties            defaults;
        props_map                    parsed;
        auto set     = [&](string_id id, const auto& value) { parsed[id] = property_value(value, important); };
        auto initial = [&](string_id id) {
            switch(id)
            {
            case _opacity_:
                set(id, defaults.opacity);
                break;
            case _transform_:
                set(id, defaults.transform);
                break;
            case _transform_origin_:
                set(id, defaults.transform_origin);
                break;
            case _perspective_:
                set(id, defaults.perspective);
                break;
            case _perspective_origin_:
                set(id, defaults.perspective_origin);
                break;
            case _transform_style_:
                set(id, defaults.transform_style);
                break;
            case _backface_visibility_:
                set(id, defaults.backface_visibility);
                break;
            case _animation_name_:
                set(id, defaults.animation_names);
                break;
            case _animation_duration_:
                set(id, defaults.animation_durations);
                break;
            case _animation_delay_:
                set(id, defaults.animation_delays);
                break;
            case _animation_timing_function_:
                set(id, defaults.animation_timings);
                break;
            case _animation_iteration_count_:
                set(id, defaults.animation_iterations);
                break;
            case _animation_direction_:
                set(id, defaults.animation_directions);
                break;
            case _animation_fill_mode_:
                set(id, defaults.animation_fills);
                break;
            case _animation_play_state_:
                set(id, defaults.animation_plays);
                break;
            case _transition_property_:
                set(id, defaults.transition_properties);
                break;
            case _transition_duration_:
                set(id, defaults.transition_durations);
                break;
            case _transition_delay_:
                set(id, defaults.transition_delays);
                break;
            case _transition_timing_function_:
                set(id, defaults.transition_timings);
                break;
            default:
                break;
            }
        };
        auto commit = [&]() {
            for(const auto& [id, value] : parsed)
            {
                add_parsed_property(id, value);
            }
        };
        auto ident = tokens.size() == 1 ? lowcase(tokens[0].ident()) : "";
        if(global_keyword(ident))
        {
            // Revert needs cascade-origin information, which this declaration map does not retain.
            if(ident == "revert" || ident == "revert-layer")
            {
                return true;
            }
            auto ids = name == _animation_    ? animation_ids
                       : name == _transition_ ? transition_ids
                                              : std::vector<string_id>{name};
            for(auto id : ids)
            {
                if(ident == "inherit")
                {
                    set(id, inherit());
                } else
                {
                    initial(id);
                }
            }
            commit();
            return true;
        }
        if(name == _opacity_)
        {
            if(tokens.size() != 1 || (tokens[0].type != NUMBER && tokens[0].type != PERCENTAGE) ||
               !std::isfinite(tokens[0].n.number))
            {
                return true;
            }
            set(name, std::clamp(tokens[0].n.number / (tokens[0].type == PERCENTAGE ? 100.f : 1.f), 0.f, 1.f));
        } else if(name == _transform_)
        {
            motion_transform_list value;
            if(!motion_transform_list::parse(tokens, value))
            {
                return true;
            }
            set(name, value);
        } else if(name == _transform_origin_ || name == _perspective_origin_)
        {
            length_vector value;
            if(!origin_value(tokens, value, name == _transform_origin_))
            {
                return true;
            }
            set(name, value);
        } else if(name == _perspective_)
        {
            css_length value = css_length::predef_value();
            if(ident != "none" && (tokens.size() != 1 || !value.from_token(tokens[0], f_length | f_positive) ||
                                   !std::isfinite(value.val())))
            {
                return true;
            }
            set(name, value);
        } else if(name == _transform_style_ || name == _backface_visibility_)
        {
            int value = tokens.size() != 1          ? -1
                        : name == _transform_style_ ? keyword_index(tokens[0], {"flat", "preserve-3d"})
                                                    : keyword_index(tokens[0], {"visible", "hidden"});
            if(value < 0)
            {
                return true;
            }
            set(name, value);
        } else if(name == _animation_ || name == _transition_)
        {
            motion_properties values;
            values.animation_names.clear();
            values.animation_durations.clear();
            values.animation_delays.clear();
            values.animation_timings.clear();
            values.animation_iterations.clear();
            values.animation_directions.clear();
            values.animation_fills.clear();
            values.animation_plays.clear();
            values.transition_properties.clear();
            values.transition_durations.clear();
            values.transition_delays.clear();
            values.transition_timings.clear();
            for(const auto& item : parse_comma_separated_list(tokens))
            {
                if(item.empty())
                {
                    return true;
                }
                motion_animation  animation;
                motion_transition transition;
                bool              duration_seen = false, delay_seen = false, timing_seen = false, name_seen = false;
                bool              iterations_seen = false, direction_seen = false, fill_seen = false, play_seen = false;
                for(const auto& token : item)
                {
                    double number;
                    if(time_token(token, number, false))
                    {
                        if(!duration_seen)
                        {
                            if(number < 0)
                            {
                                return true;
                            }
                            animation.duration = transition.duration = number;
                            duration_seen                            = true;
                        } else if(!delay_seen)
                        {
                            animation.delay = transition.delay = number;
                            delay_seen                         = true;
                        } else
                        {
                            return true;
                        }
                        continue;
                    }
                    if(!timing_seen)
                    {
                        if(auto timing = motion_timing::parse(token))
                        {
                            animation.timing = transition.timing = *timing;
                            timing_seen                          = true;
                            continue;
                        }
                    }
                    if(name == _animation_)
                    {
                        if(!iterations_seen && iteration_token(token, number))
                        {
                            animation.iterations = number;
                            iterations_seen      = true;
                            continue;
                        }
                        int index;
                        if(!direction_seen &&
                           (index = keyword_index(token, {"normal", "reverse", "alternate", "alternate-reverse"})) >= 0)
                        {
                            animation.direction = static_cast<motion_direction>(index);
                            direction_seen      = true;
                            continue;
                        }
                        if(!fill_seen && (index = keyword_index(token, {"none", "forwards", "backwards", "both"})) >= 0)
                        {
                            animation.fill_mode = static_cast<motion_fill>(index);
                            fill_seen           = true;
                            continue;
                        }
                        if(!play_seen && (index = keyword_index(token, {"running", "paused"})) >= 0)
                        {
                            animation.play_state = static_cast<motion_play>(index);
                            play_seen            = true;
                            continue;
                        }
                    }
                    std::string value;
                    if(name_seen || !name_token(token, value, name == _animation_))
                    {
                        return true;
                    }
                    animation.name      = value;
                    animation.is_none   = token.type == IDENT && lowcase(value) == "none";
                    transition.property = value.substr(0, 2) == "--" ? value : lowcase(value);
                    name_seen           = true;
                }
                values.animation_names.push_back({animation.name, animation.is_none});
                values.animation_durations.push_back(animation.duration);
                values.animation_delays.push_back(animation.delay);
                values.animation_timings.push_back(animation.timing);
                values.animation_iterations.push_back(animation.iterations);
                values.animation_directions.push_back(static_cast<int>(animation.direction));
                values.animation_fills.push_back(static_cast<int>(animation.fill_mode));
                values.animation_plays.push_back(static_cast<int>(animation.play_state));
                values.transition_properties.push_back(transition.property);
                values.transition_durations.push_back(transition.duration);
                values.transition_delays.push_back(transition.delay);
                values.transition_timings.push_back(transition.timing);
            }
            if(name == _animation_)
            {
                set(_animation_name_, values.animation_names);
                set(_animation_duration_, values.animation_durations);
                set(_animation_delay_, values.animation_delays);
                set(_animation_timing_function_, values.animation_timings);
                set(_animation_iteration_count_, values.animation_iterations);
                set(_animation_direction_, values.animation_directions);
                set(_animation_fill_mode_, values.animation_fills);
                set(_animation_play_state_, values.animation_plays);
            } else
            {
                if(values.transition_properties.size() > 1 &&
                   std::find(values.transition_properties.begin(), values.transition_properties.end(), "none") !=
                       values.transition_properties.end())
                {
                    return true;
                }
                set(_transition_property_, values.transition_properties);
                set(_transition_duration_, values.transition_durations);
                set(_transition_delay_, values.transition_delays);
                set(_transition_timing_function_, values.transition_timings);
            }
        } else
        {
            motion_numbers           numbers;
            motion_timings           timings;
            std::vector<std::string> names;
            motion_names             animation_names;
            std::vector<int>         keywords;
            for(const auto& item : parse_comma_separated_list(tokens))
            {
                if(item.size() != 1)
                {
                    return true;
                }
                const auto& token = item[0];
                double      number;
                int         index;
                switch(name)
                {
                case _animation_duration_:
                case _transition_duration_:
                case _animation_delay_:
                case _transition_delay_:
                    if(!time_token(token, number, name == _animation_duration_ || name == _transition_duration_))
                    {
                        return true;
                    }
                    numbers.push_back(number);
                    break;
                case _animation_iteration_count_:
                    if(!iteration_token(token, number))
                    {
                        return true;
                    }
                    numbers.push_back(number);
                    break;
                case _animation_name_:
                case _transition_property_:
                    {
                        std::string value;
                        if(!name_token(token, value, name == _animation_name_))
                        {
                            return true;
                        }
                        if(name == _animation_name_)
                        {
                            animation_names.push_back({value, token.type == IDENT && lowcase(value) == "none"});
                        } else
                        {
                            names.push_back(value.substr(0, 2) == "--" ? value : lowcase(value));
                        }
                        break;
                    }
                case _animation_timing_function_:
                case _transition_timing_function_:
                    {
                        auto timing = motion_timing::parse(token);
                        if(!timing)
                        {
                            return true;
                        }
                        timings.push_back(*timing);
                        break;
                    }
                case _animation_direction_:
                    index = keyword_index(token, {"normal", "reverse", "alternate", "alternate-reverse"});
                    if(index < 0)
                    {
                        return true;
                    }
                    keywords.push_back(index);
                    break;
                case _animation_fill_mode_:
                    index = keyword_index(token, {"none", "forwards", "backwards", "both"});
                    if(index < 0)
                    {
                        return true;
                    }
                    keywords.push_back(index);
                    break;
                case _animation_play_state_:
                    index = keyword_index(token, {"running", "paused"});
                    if(index < 0)
                    {
                        return true;
                    }
                    keywords.push_back(index);
                    break;
                default:
                    return true;
                }
            }
            if(name == _transition_property_ && names.size() > 1 &&
               std::find(names.begin(), names.end(), "none") != names.end())
            {
                return true;
            }
            if(!numbers.empty())
            {
                set(name, numbers);
            } else if(!timings.empty())
            {
                set(name, timings);
            } else if(!animation_names.empty())
            {
                set(name, animation_names);
            } else if(!names.empty())
            {
                set(name, names);
            } else if(!keywords.empty())
            {
                set(name, keywords);
            }
        }
        commit();
        return true;
    }

    void css_properties::compute_motion(const html_tag* el)
    {
        const motion_properties defaults;
        m_motion.opacity   = el->get_property<decltype(m_motion.opacity)>(_opacity_, false, defaults.opacity,
                                                                          offsetof(css_properties, m_motion) +
                                                                              offsetof(motion_properties, opacity));
        m_motion.transform = el->get_property<decltype(m_motion.transform)>(_transform_, false, defaults.transform,
                                                                            offsetof(css_properties, m_motion) +
                                                                                offsetof(motion_properties, transform));
        m_motion.transform_origin = el->get_property<decltype(m_motion.transform_origin)>(
            _transform_origin_, false, defaults.transform_origin,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, transform_origin));
        m_motion.perspective = el->get_property<decltype(m_motion.perspective)>(
            _perspective_, false, defaults.perspective,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, perspective));
        m_motion.perspective_origin = el->get_property<decltype(m_motion.perspective_origin)>(
            _perspective_origin_, false, defaults.perspective_origin,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, perspective_origin));
        m_motion.transform_style = el->get_property<decltype(m_motion.transform_style)>(
            _transform_style_, false, defaults.transform_style,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, transform_style));
        m_motion.backface_visibility = el->get_property<decltype(m_motion.backface_visibility)>(
            _backface_visibility_, false, defaults.backface_visibility,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, backface_visibility));
        m_motion.animation_names = el->get_property<decltype(m_motion.animation_names)>(
            _animation_name_, false, defaults.animation_names,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_names));
        m_motion.animation_durations = el->get_property<decltype(m_motion.animation_durations)>(
            _animation_duration_, false, defaults.animation_durations,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_durations));
        m_motion.animation_delays = el->get_property<decltype(m_motion.animation_delays)>(
            _animation_delay_, false, defaults.animation_delays,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_delays));
        m_motion.animation_timings = el->get_property<decltype(m_motion.animation_timings)>(
            _animation_timing_function_, false, defaults.animation_timings,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_timings));
        m_motion.animation_iterations = el->get_property<decltype(m_motion.animation_iterations)>(
            _animation_iteration_count_, false, defaults.animation_iterations,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_iterations));
        m_motion.animation_directions = el->get_property<decltype(m_motion.animation_directions)>(
            _animation_direction_, false, defaults.animation_directions,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_directions));
        m_motion.animation_fills = el->get_property<decltype(m_motion.animation_fills)>(
            _animation_fill_mode_, false, defaults.animation_fills,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_fills));
        m_motion.animation_plays = el->get_property<decltype(m_motion.animation_plays)>(
            _animation_play_state_, false, defaults.animation_plays,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, animation_plays));
        m_motion.transition_properties = el->get_property<decltype(m_motion.transition_properties)>(
            _transition_property_, false, defaults.transition_properties,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, transition_properties));
        m_motion.transition_durations = el->get_property<decltype(m_motion.transition_durations)>(
            _transition_duration_, false, defaults.transition_durations,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, transition_durations));
        m_motion.transition_delays = el->get_property<decltype(m_motion.transition_delays)>(
            _transition_delay_, false, defaults.transition_delays,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, transition_delays));
        m_motion.transition_timings = el->get_property<decltype(m_motion.transition_timings)>(
            _transition_timing_function_, false, defaults.transition_timings,
            offsetof(css_properties, m_motion) + offsetof(motion_properties, transition_timings));
        m_motion.assemble();
    }
} // namespace litehtml
