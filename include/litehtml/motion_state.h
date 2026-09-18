#ifndef LITEHTML_MOTION_STATE_H
#define LITEHTML_MOTION_STATE_H
#include "motion_transform.h"
#include <memory>
namespace litehtml
{
    class html_tag;
    class style;
    class document;
    class motion_state
    {
        struct implementation;
        std::unique_ptr<implementation> m_impl;

      public:
        motion_state();
        ~motion_state();
        void style_changed(html_tag& element, const style& declarations);
        void apply(html_tag& element);
        bool active(const document& doc) const;
        double next_delay(const document& doc, double cadence, bool& paint_only, bool& vertical_fixed) const;
        bool has_transform() const;
        bool transform(const motion_length_context& context, motion_matrix& out) const;
    };
} // namespace litehtml
#endif
