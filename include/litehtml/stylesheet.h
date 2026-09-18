#ifndef LITEHTML_STYLESHEET_H
#define LITEHTML_STYLESHEET_H

#include <utility>

#include "css_selector.h"
#include "css_tokenizer.h"

namespace litehtml
{

    // https://www.w3.org/TR/cssom-1/#css-declarations
    struct raw_declaration
    {
        using vector = std::vector<raw_declaration>;

        // property name
        std::string name;
        // default value is specified here to get rid of gcc warning "missing initializer for member"
        css_token_vector value;

        bool important = false;

        operator bool() const
        {
            return name != "";
        }
    };

    // intermediate half-parsed rule that is used internally by the parser
    class raw_rule
    {
      public:
        using ptr    = std::shared_ptr<raw_rule>;
        using vector = std::vector<ptr>;

        enum rule_type
        {
            qualified,
            at
        };

        raw_rule(rule_type type, std::string name = {}) :
            type(type),
            name(std::move(name))
        {
        }

        rule_type type;
        // An at-rule has a name, a prelude consisting of a list of component values, and an optional block consisting
        // of a simple {} block.
        std::string name;
        // https://www.w3.org/TR/css-syntax-3/#qualified-rule
        // A qualified rule has a prelude consisting of a list of component values, and a block consisting of a simple
        // {} block. Note: Most qualified rules will be style rules, where the prelude is a selector and the block a
        // list of declarations.
        css_token_vector prelude;
        css_token        block;
    };

    struct motion_keyframes
    {
        std::string                name;
        std::map<double, style>    frames;
        media_query_list_list::ptr media;
    };

    class css
    {
        css_selector::vector          m_selectors;
        std::vector<motion_keyframes> m_keyframes;

      public:
        const motion_keyframes*              find_keyframes(const std::string& name) const;
        const std::vector<motion_keyframes>& keyframes() const
        {
            return m_keyframes;
        }
        const css_selector::vector& selectors() const
        {
            return m_selectors;
        }

        template <class Input>
        void parse_css_stylesheet(const Input& input, const std::string& baseurl, const std::shared_ptr<document>& doc,
                                  const media_query_list_list::ptr& media = nullptr, bool top_level = true);

        void sort_selectors();

      private:
        void parse_keyframes_rule(const raw_rule::ptr& rule, const std::string& baseurl,
                                  const std::shared_ptr<document>& doc, const media_query_list_list::ptr& media);
        bool parse_style_rule(const raw_rule::ptr& rule, const std::string& baseurl,
                              const std::shared_ptr<document>& doc, const media_query_list_list::ptr& media);
        void parse_import_rule(const raw_rule::ptr& rule, const std::string& baseurl,
                               const std::shared_ptr<document>& doc, const media_query_list_list::ptr& media);
        void add_selector(const css_selector::ptr& selector);
    };

    inline void css::add_selector(const css_selector::ptr& selector)
    {
        selector->m_order = static_cast<int>(m_selectors.size());
        m_selectors.push_back(selector);
    }

} // namespace litehtml

#endif // LITEHTML_STYLESHEET_H
