#include "html.h"
#include "motion_state.h"
#include "html_tag.h"
#include "document.h"
#include "document_container.h"
#include "render_item.h"
#include "motion_timing.h"
#include <cmath>
#include <map>
#include <set>
#include <variant>
#include <array>

namespace litehtml
{
    namespace
    {
        const std::array<string_id, 10> properties{
            _transform_, _opacity_, _color_, _background_color_, _left_, _top_, _right_, _bottom_, _width_, _height_};
        struct transform_value
        {
            motion_transform_list            list;
            std::shared_ptr<transform_value> from, to;
            double                           progress = 0;
            bool                             matrix(const motion_length_context& context, motion_matrix& out) const
            {
                if(!from)
                {
                    return list.matrix(context, out);
                }
                if(!from->from && !to->from)
                {
                    return motion_transform_list::interpolate(from->list, to->list, static_cast<float>(progress),
                                                              context, out);
                }
                motion_matrix a, b;
                return from->matrix(context, a) && to->matrix(context, b) &&
                       motion_matrix::interpolate(a, b, static_cast<float>(progress), out);
            }
        };
        using transform_ptr = std::shared_ptr<transform_value>;
        using value         = std::variant<float, web_color, css_length, transform_ptr>;
        struct values
        {
            std::array<std::pair<string_id, value>, properties.size()> items;
            values()
            {
                for(size_t i = 0; i < properties.size(); ++i) items[i].first = properties[i];
            }
            value& operator[](string_id id)
            {
                return items[std::find(properties.begin(), properties.end(), id) - properties.begin()].second;
            }
            const value& at(string_id id) const
            {
                return items[std::find(properties.begin(), properties.end(), id) - properties.begin()].second;
            }
            auto begin() const { return items.begin(); }
            auto end() const { return items.end(); }
        };
        transform_ptr make_transform(const motion_transform_list& list)
        {
            auto result  = std::make_shared<transform_value>();
            result->list = list;
            return result;
        }
        bool horizontal_translation(const motion_transform_list& list)
        {
            return std::all_of(list.operations.begin(), list.operations.end(), [](const auto& op) {
                return op.kind == motion_transform_kind::translate &&
                    !op.lengths[1].is_predefined() && op.lengths[1].val() == 0 &&
                    !op.lengths[2].is_predefined() && op.lengths[2].val() == 0;
            });
        }
        bool horizontal_translation(const transform_ptr& value)
        {
            return value->from ? horizontal_translation(value->from) && horizontal_translation(value->to) :
                horizontal_translation(value->list);
        }
        bool equal_length(const css_length& a, const css_length& b)
        {
            return a.is_predefined() == b.is_predefined() &&
                   (a.is_predefined() ? a.predef() == b.predef() : a.units() == b.units() && std::equal_to<float>{}(a.val(), b.val()));
        }
        bool equal_transform(const motion_transform_list& a, const motion_transform_list& b)
        {
            if(a.operations.size() != b.operations.size())
            {
                return false;
            }
            for(size_t i = 0; i < a.operations.size(); ++i)
            {
                const auto& x = a.operations[i];
                const auto& y = b.operations[i];
                if(x.kind != y.kind || x.matrix_value.values != y.matrix_value.values || x.numbers != y.numbers)
                {
                    return false;
                }
                for(size_t j = 0; j < 3; ++j)
                {
                    if(!equal_length(x.lengths[j], y.lengths[j]))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        bool equal_value(const value& a, const value& b)
        {
            if(a.index() != b.index())
            {
                return false;
            }
            if(auto x = std::get_if<float>(&a))
            {
                return std::equal_to<float>{}(*x, std::get<float>(b));
            }
            if(auto x = std::get_if<web_color>(&a))
            {
                return *x == std::get<web_color>(b);
            }
            if(auto x = std::get_if<css_length>(&a))
            {
                return equal_length(*x, std::get<css_length>(b));
            }
            const auto& x = std::get<transform_ptr>(a);
            const auto& y = std::get<transform_ptr>(b);
            return x == y || (!x->from && !y->from && equal_transform(x->list, y->list));
        }
        value property(const css_properties& css, string_id id)
        {
            switch(id)
            {
            case _transform_:
                return make_transform(css.get_transform());
            case _opacity_:
                return css.get_opacity();
            case _color_:
                return css.get_color();
            case _background_color_:
                return css.get_bg().m_color;
            case _left_:
                return css.get_offsets().left;
            case _top_:
                return css.get_offsets().top;
            case _right_:
                return css.get_offsets().right;
            case _bottom_:
                return css.get_offsets().bottom;
            case _width_:
                return css.get_width();
            default:
                return css.get_height();
            }
        }
        values extract(const css_properties& css)
        {
            values result;
            for(auto id : properties)
            {
                result[id] = property(css, id);
            }
            return result;
        }
        motion_length_context length_context(html_tag& element)
        {
            motion_length_context result;
            const auto viewport = element.get_document()->motion_viewport();
            result.viewport_width  = viewport.width.value();
            result.viewport_height = viewport.height.value();
            const auto& css        = element.css();
            result.font_size       = css.get_font_size().value();
            result.x_height        = css.get_font_metrics().x_height.value();
            result.zero_advance    = css.get_font_metrics().ch_width.value();
            auto root              = element.get_document()->root();
            result.root_font_size  = root ? root->css().get_font_size().value() : result.font_size;
            result.box_width       = result.viewport_width;
            result.box_height      = result.viewport_height;
            std::function<float(const element::ptr&, bool)> extent = [&](const element::ptr& node, bool vertical) -> float {
                if(!node) return vertical ? result.viewport_height : result.viewport_width;
                const auto parent_basis = extent(node->parent(), vertical);
                const auto& length = vertical ? node->css().get_height() : node->css().get_width();
                float resolved;
                if(motion_resolve_length(length, parent_basis, result, resolved)) return resolved;
                if(auto render = node->get_render_item())
                {
                    const auto measured = vertical ? render->pos().height.value() : render->pos().width.value();
                    if(measured > 0) return measured;
                }
                return parent_basis;
            };
            result.box_width = extent(element.parent(), false);
            result.box_height = extent(element.parent(), true);
            return result;
        }
        value interpolate(const value& a, const value& b, double progress, string_id id,
                          const motion_length_context& context)
        {
            if(a.index() != b.index())
            {
                return progress < .5 ? a : b;
            }
            if(auto x = std::get_if<float>(&a))
            {
                return interpolate_scalar(*x, std::get<float>(b), progress);
            }
            if(auto x = std::get_if<web_color>(&a))
            {
                auto color = interpolate_color(*x, std::get<web_color>(b), progress);
                return color ? value(*color) : progress < .5 ? a : b;
            }
            if(auto x = std::get_if<css_length>(&a))
            {
                const bool vertical = id == _top_ || id == _bottom_ || id == _height_;
                auto       length   = interpolate_length(
                    *x, std::get<css_length>(b), progress, [&](const css_length& input) -> std::optional<float> {
                        float output;
                        if(motion_resolve_length(input, vertical ? context.box_height : context.box_width, context,
                                                         output))
                        {
                            return output;
                        }
                        return std::nullopt;
                    });
                return length ? value(*length) : progress < .5 ? a : b;
            }
            auto result      = std::make_shared<transform_value>();
            result->from     = std::get<transform_ptr>(a);
            result->to       = std::get<transform_ptr>(b);
            result->progress = progress;
            return result;
        }
        bool interpolable(const value& a, const value& b)
        {
            if(a.index() != b.index())
            {
                return false;
            }
            if(auto x = std::get_if<css_length>(&a))
            {
                return !x->is_predefined() && !std::get<css_length>(b).is_predefined();
            }
            return true;
        }
        void apply_value(css_properties& css, string_id id, const value& input)
        {
            switch(id)
            {
            case _transform_:
                break;
            case _opacity_:
                css.set_opacity(std::clamp(std::get<float>(input), 0.f, 1.f));
                break;
            case _color_:
                css.set_color(std::get<web_color>(input));
                break;
            case _background_color_:
                {
                    const auto color = std::get<web_color>(input);
                    if(css.get_bg().m_color == color) break;
                    auto background    = css.get_bg();
                    background.m_color = color;
                    css.set_bg(background);
                    break;
                }
            case _width_:
            case _height_:
                {
                    auto length = std::get<css_length>(input);
                    if(!length.is_predefined() && length.val() < 0)
                    {
                        length.set_value(0, length.units());
                    }
                    if(id == _width_)
                    {
                        css.set_width(length);
                    } else
                    {
                        css.set_height(length);
                    }
                    break;
                }
            default:
                {
                    auto        offsets = css.get_offsets();
                    const auto& length  = std::get<css_length>(input);
                    if(id == _left_)
                    {
                        offsets.left = length;
                    } else if(id == _top_)
                    {
                        offsets.top = length;
                    } else if(id == _right_)
                    {
                        offsets.right = length;
                    } else
                    {
                        offsets.bottom = length;
                    }
                    css.set_offsets(offsets);
                    break;
                }
            }
        }
        std::optional<value> frame_value(const style& frame, string_id id, html_tag& element,
                                         const css_properties& base)
        {
            const auto& raw = frame.get_property(id);
            if(raw.is<invalid>())
            {
                return std::nullopt;
            }
            if(raw.is<inherit>())
            {
                if(auto parent = element.parent())
                {
                    return property(parent->css(), id);
                }
                if(id == _opacity_)
                {
                    return value(1.f);
                }
                if(id == _transform_)
                {
                    return value(make_transform({}));
                }
                if(id == _color_)
                {
                    return value(web_color::black);
                }
                if(id == _background_color_)
                {
                    return value(web_color::transparent);
                }
                return value(css_length::predef_value());
            }
            if(raw.is<float>())
            {
                return raw.get<float>();
            }
            if(raw.is<web_color>())
            {
                auto color = raw.get<web_color>();
                if(color.is_current_color)
                {
                    color = base.get_color();
                }
                return color;
            }
            if(raw.is<css_length>())
            {
                auto length = raw.get<css_length>();
                element.get_document()->cvt_units(length, base.get_font_metrics(), 0_px);
                return length;
            }
            if(raw.is<motion_transform_list>())
            {
                return make_transform(raw.get<motion_transform_list>());
            }
            return std::nullopt;
        }
        struct animation_clock
        {
            motion_animation spec;
            double           started = 0, paused_at = 0, paused_total = 0;
            double           local(double now) const
            {
                return ((spec.play_state == motion_play::paused ? paused_at : now) - started - paused_total) / 1000 -
                       spec.delay;
            }
            bool active(double now) const
            {
                return !spec.is_none && spec.play_state != motion_play::paused &&
                       local(now) < spec.duration * spec.iterations;
            }
            std::optional<double> progress(double now) const
            {
                const auto elapsed   = local(now);
                const auto total     = spec.duration * spec.iterations;
                double     iteration = 0, fraction = 0;
                if(elapsed < 0)
                {
                    if(spec.fill_mode != motion_fill::backwards && spec.fill_mode != motion_fill::both)
                    {
                        return std::nullopt;
                    }
                } else if(spec.duration <= 0 || elapsed >= total)
                {
                    if(spec.fill_mode != motion_fill::forwards && spec.fill_mode != motion_fill::both)
                    {
                        return std::nullopt;
                    }
                    if(spec.iterations > 0)
                    {
                        double count = std::isfinite(spec.iterations) ? spec.iterations : 1;
                        iteration    = std::ceil(count) - 1;
                        fraction     = count - iteration;
                    }
                } else
                {
                    auto position = elapsed / spec.duration;
                    iteration     = std::floor(position);
                    fraction      = position - iteration;
                }
                bool reverse = spec.direction == motion_direction::reverse ||
                               spec.direction == motion_direction::alternate_reverse;
                if(spec.direction == motion_direction::alternate ||
                   spec.direction == motion_direction::alternate_reverse)
                {
                    if(std::fmod(iteration, 2) >= 1)
                    {
                        reverse = !reverse;
                    }
                }
                return reverse ? 1 - fraction : fraction;
            }
        };
        struct transition_clock
        {
            value             from, to, reversing_start;
            motion_transition spec;
            double            started = 0, shortening = 1;
            double            progress(double now) const
            {
                return spec.duration <= 0 ? 1
                                          : std::clamp(((now - started) / 1000 - spec.delay) / spec.duration, 0.0, 1.0);
            }
            bool active(double now) const
            {
                return (now - started) / 1000 < spec.delay + spec.duration;
            }
        };
    } // namespace

    struct motion_state::implementation
    {
        css_properties                        base;
        bool                                  initialized = false, inherited_color = true;
        std::set<string_id>                   important;
        std::vector<animation_clock>          animations;
        std::map<string_id, transition_clock> transitions;
        transform_ptr                         current_transform = make_transform({});

        values sample(html_tag& element, double now) const
        {
            auto underlying = extract(base);
            if(inherited_color)
            {
                if(auto parent = element.parent())
                {
                    underlying[_color_] = parent->css().get_color();
                }
            }
            std::optional<motion_length_context> context;
            const auto mix = [&](const value& from, const value& to, double progress, string_id id) {
                if(std::holds_alternative<css_length>(from) && !context) context = length_context(element);
                return interpolate(from, to, progress, id, context ? *context : motion_length_context{});
            };
            for(const auto& animation : animations)
            {
                if(animation.spec.is_none)
                {
                    continue;
                }
                auto frames   = element.get_document()->find_keyframes(animation.spec.name);
                auto progress = animation.progress(now);
                if(!frames || !progress || frames->frames.empty())
                {
                    continue;
                }
                // Custom properties require element-specific substitution.
                std::optional<std::map<double, style>> substituted;
                for(const auto& [offset, frame] : frames->frames)
                {
                    if(frame.has_variables())
                    {
                        substituted = frames->frames;
                        for(auto& [key, resolved] : *substituted) resolved.subst_vars(&element);
                        break;
                    }
                }
                const auto& resolved_frames = substituted ? *substituted : frames->frames;
                for(auto id : properties)
                {
                    if(important.count(id))
                    {
                        continue;
                    }
                    struct point
                    {
                        double offset;
                        value item;
                        motion_timing timing;
                    };
                    point left{0, underlying.at(id), animation.spec.timing};
                    point right{1, underlying.at(id), animation.spec.timing};
                    bool found = false;
                    for(const auto& [offset, frame] : resolved_frames)
                    {
                        auto item = frame_value(frame, id, element, base);
                        if(!item) continue;
                        found = true;
                        auto timing = animation.spec.timing;
                        const auto& raw_timing = frame.get_property(_animation_timing_function_);
                        if(raw_timing.is<motion_timings>() && !raw_timing.get<motion_timings>().empty())
                            timing = raw_timing.get<motion_timings>()[0];
                        if(offset <= *progress) left = {offset, *item, timing};
                        if(offset >= *progress)
                        {
                            right = {offset, *item, timing};
                            break;
                        }
                    }
                    if(!found) continue;
                    if(left.offset == right.offset)
                        underlying[id] = left.item;
                    else
                        underlying[id] = mix(left.item, right.item,
                            left.timing.sample((*progress - left.offset) / (right.offset - left.offset)), id);
                }
            }
            for(const auto& [id, transition] : transitions)
            {
                underlying[id] = mix(transition.from, transition.to,
                                     transition.spec.timing.sample(transition.progress(now)), id);
            }
            return underlying;
        }
    };

    motion_state::motion_state() :
        m_impl(std::make_unique<implementation>())
    {
    }
    motion_state::~motion_state() = default;

    void motion_state::style_changed(html_tag& element, const style& declarations)
    {
        auto&        state    = *m_impl;
        const double now      = element.get_document()->time();
        auto         computed = element.css();
        const auto& transition_specs = computed.get_motion().transitions;
        const bool can_transition = std::any_of(transition_specs.begin(), transition_specs.end(),
            [](const auto& spec) { return spec.duration > 0; });
        if(!can_transition)
        {
            state.transitions.clear();
        }
        if(state.initialized && can_transition)
        {
            auto current = state.sample(element, now);
            auto before  = extract(state.base);
            auto after   = extract(computed);
            for(auto id : properties)
            {
                const motion_transition* spec = nullptr;
                for(const auto& candidate : computed.get_motion().transitions)
                {
                    if(candidate.property == "all" || candidate.property == _s(id))
                    {
                        spec = &candidate;
                    }
                }
                if(!spec || spec->duration <= 0)
                {
                    state.transitions.erase(id);
                    continue;
                }
                if(equal_value(before.at(id), after.at(id)))
                {
                    continue;
                }
                if(!interpolable(current.at(id), after.at(id)) || equal_value(current.at(id), after.at(id)))
                {
                    state.transitions.erase(id);
                    continue;
                }
                auto   adjusted        = *spec;
                double shortening      = 1;
                auto   reversing_start = current.at(id);
                if(auto old = state.transitions.find(id);
                   old != state.transitions.end() && equal_value(old->second.reversing_start, after.at(id)))
                {
                    shortening = std::clamp(
                        std::abs(old->second.spec.timing.sample(old->second.progress(now)) * old->second.shortening +
                                 1 - old->second.shortening),
                        0.0, 1.0);
                    adjusted.duration *= shortening;
                    if(adjusted.delay < 0)
                    {
                        adjusted.delay *= shortening;
                    }
                    reversing_start = old->second.to;
                }
                state.transitions.insert_or_assign(
                    id, transition_clock{current.at(id), after.at(id), reversing_start, adjusted, now, shortening});
            }
        }
        std::vector<animation_clock> next;
        std::vector<bool>            used(state.animations.size(), false);
        const auto&                  specs = computed.get_motion().animations;
        next.resize(specs.size());
        for(size_t i = specs.size(); i-- > 0;)
        {
            auto& clock     = next[i];
            clock.spec      = specs[i];
            clock.started   = now;
            clock.paused_at = now;
            for(size_t j = state.animations.size(); j-- > 0;)
            {
                const auto& prior = state.animations[j];
                if(used[j] || prior.spec.name != specs[i].name || prior.spec.is_none != specs[i].is_none)
                {
                    continue;
                }
                used[j] = true;
                clock   = prior;
                if(prior.spec.play_state != specs[i].play_state)
                {
                    if(specs[i].play_state == motion_play::paused)
                    {
                        clock.paused_at = now;
                    } else
                    {
                        clock.paused_total += now - clock.paused_at;
                    }
                }
                clock.spec = specs[i];
                break;
            }
        }
        state.animations  = std::move(next);
        state.base        = std::move(computed);
        state.initialized = true;
        state.important.clear();
        for(auto id : properties)
        {
            if(declarations.get_property(id).m_important)
            {
                state.important.insert(id);
            }
        }
        const auto& color       = declarations.get_property(_color_);
        state.inherited_color   = color.is<invalid>() || color.is<inherit>();
        state.current_transform = make_transform(state.base.get_transform());
    }

    void motion_state::apply(html_tag& element)
    {
        auto& state = *m_impl;
        if(!state.initialized)
        {
            return;
        }
        if(state.transitions.empty() &&
           std::all_of(state.animations.begin(), state.animations.end(),
                       [](const auto& animation) { return animation.spec.is_none; }))
        {
            if(state.inherited_color)
            {
                if(auto parent = element.parent())
                {
                    element.css_w().set_color(parent->css().get_color());
                }
            }
            return;
        }
        auto now        = element.get_document()->time();
        auto sampled    = state.sample(element, now);
        // Sampling includes base values for properties whose motion has ended.
        for(const auto& [id, item] : sampled)
        {
            apply_value(element.css_w(), id, item);
        }
        state.current_transform = std::get<transform_ptr>(sampled.at(_transform_));
        for(auto it = state.transitions.begin(); it != state.transitions.end();)
        {
            if(!it->second.active(now))
            {
                it = state.transitions.erase(it);
            } else
            {
                ++it;
            }
        }
    }

    bool motion_state::active(const document& doc) const
    {
        const auto& state = *m_impl;
        for(const auto& animation : state.animations)
        {
            if(animation.active(doc.time()))
            {
                if(auto frames = doc.find_keyframes(animation.spec.name); frames && !frames->frames.empty())
                {
                    return true;
                }
            }
        }
        for(const auto& [id, transition] : state.transitions)
        {
            if(transition.active(doc.time()))
            {
                return true;
            }
        }
        return false;
    }

    double motion_state::next_delay(const document& doc, double cadence, bool& paint_only, bool& vertical_fixed) const
    {
        double delay = -1;
        paint_only = true;
        vertical_fixed = horizontal_translation(m_impl->base.get_transform()) &&
            horizontal_translation(m_impl->current_transform);
        const auto include = [&](double next) {
            next = std::max(0.0, next);
            delay = delay < 0 ? next : std::min(delay, next);
        };
        const auto is_paint = [](string_id id) {
            return id == _opacity_ || id == _color_ || id == _background_color_;
        };
        for(const auto& animation : m_impl->animations)
        {
            if(!animation.active(doc.time())) continue;
            const auto* frames = doc.find_keyframes(animation.spec.name);
            if(!frames || frames->frames.empty()) continue;
            const auto elapsed = animation.local(doc.time());
            include(elapsed < 0 ? -elapsed * 1000 :
                std::min(cadence, (animation.spec.duration * animation.spec.iterations - elapsed) * 1000));
            for(const auto& [offset, frame] : frames->frames)
                for(auto id : properties)
                {
                    const auto& raw = frame.get_property(id);
                    if(is_paint(id) || raw.is<invalid>()) continue;
                    paint_only = false;
                    if(id != _transform_ || !raw.is<motion_transform_list>() ||
                        !horizontal_translation(raw.get<motion_transform_list>())) vertical_fixed = false;
                }
        }
        for(const auto& [id, transition] : m_impl->transitions)
        {
            if(!transition.active(doc.time())) continue;
            const auto elapsed = (doc.time() - transition.started) / 1000 - transition.spec.delay;
            include(elapsed < 0 ? -elapsed * 1000 : std::min(cadence, (transition.spec.duration - elapsed) * 1000));
            if(!is_paint(id))
            {
                paint_only = false;
                if(id != _transform_ || !horizontal_translation(std::get<transform_ptr>(transition.from)) ||
                    !horizontal_translation(std::get<transform_ptr>(transition.to))) vertical_fixed = false;
            }
        }
        return delay;
    }

    bool motion_state::has_transform() const
    {
        const auto& transform = *m_impl->current_transform;
        // Interpolated transforms must be resolved even when the base style is none.
        return transform.from || !transform.list.operations.empty();
    }

    bool motion_state::transform(const motion_length_context& context, motion_matrix& out) const
    {
        return m_impl->current_transform->matrix(context, out);
    }
} // namespace litehtml
