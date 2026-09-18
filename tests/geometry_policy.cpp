#include "headless_container.h"
#include "check.h"
#include <cmath>
#include <iostream>
struct recorder : headless_container
{
    std::vector<position> texts, fills, borders, images;
    std::vector<float>    requested_sizes, border_widths;
    uint_ptr              create_font(const font_description& d, const document*, font_metrics* m) override
    {
        requested_sizes.push_back(d.size.value());
        *m           = {};
        m->font_size = d.size;
        m->height    = 16;
        m->ascent    = 12;
        m->descent   = 4;
        m->x_height  = 8;
        m->ch_width  = 8;
        return 1;
    }
    void    delete_font(uint_ptr) override {}
    pixel_t text_width(const char* t, uint_ptr) override
    {
        return strlen(t) * 8.25f;
    }
    void draw_text(uint_ptr, const char*, uint_ptr, web_color, const position& p) override
    {
        texts.push_back(p);
    }
    void draw_solid_fill(uint_ptr, const background_layer& l, const web_color&) override
    {
        fills.push_back(l.border_box);
    }
    void draw_borders(uint_ptr, const litehtml::borders& b, const position& p, bool) override
    {
        borders.push_back(p);
        border_widths.push_back(b.top.width.value());
    }
    void load_image(const char*, const char*, bool) override {}
    void get_image_size(const char*, const char*, size& s) override
    {
        s = {10, 10};
    }
    void draw_image(uint_ptr, const background_layer& l, const std::string&, const std::string&) override
    {
        images.push_back(l.origin_box);
    }
};
struct fractional_recorder : recorder
{
    void round_paint_position(position&) const override {}
};
void near(float a, float b)
{
    if(std::abs(a - b) > .001f)
    {
        std::cerr << a << " != " << b << "\n";
        std::abort();
    }
}
void test(recorder& host, bool fractional)
{
    auto doc = document::createFromString(
        "<style>html,body{margin:0;padding:0}#b{position:absolute;left:10.25px;top:20.75px;width:30.5px;height:10.25px;"
        "background:red;border:1px solid "
        "black}#t{position:absolute;left:60.25px;top:20.75px}img{position:absolute;left:100.25px;top:20.75px;width:10."
        "5px;height:10.25px}</style><div id=b></div><div id=t>abc</div><img src=test>",
        &host);
    doc->render(400);
    const auto h = doc->height();
    position   clip(0, 0, 400, 400);
    doc->draw(0, 0, 0, &clip);
    CHECK(host.fills.size() == 1 && host.borders.size() == 1 && host.images.size() == 1 && host.texts.size() == 1);
    near(host.fills[0].x, fractional ? 10.25f : 10.f);
    near(host.fills[0].y, fractional ? 20.75f : 21.f);
    near(host.fills[0].width, fractional ? 32.5f : 33.f);
    near(host.fills[0].height, fractional ? 12.25f : 12.f);
    near(host.borders[0].x, host.fills[0].x);
    near(host.borders[0].y, host.fills[0].y);
    near(host.texts[0].x, fractional ? 60.25f : 60.f);
    near(host.images[0].x, fractional ? 100.25f : 100.f);
    near(host.images[0].width, fractional ? 10.5f : 11.f);
    near(doc->height(), h);
    host.texts.clear();
    host.images.clear();
    host.borders.clear();
    host.fills.clear();
    doc->draw(0, .25f, .25f, &clip);
    near(host.fills[0].x, fractional ? 10.5f : 11.f);
    near(host.texts[0].x, fractional ? 60.5f : 61.f);
}

struct policy_recorder : recorder
{
    bool paint, fonts, edges;
    policy_recorder(bool p, bool f, bool e) :
        paint(p),
        fonts(f),
        edges(e)
    {
    }
    void round_paint_position(position& p) const override
    {
        if(!paint)
        {
            recorder::round_paint_position(p);
        }
    }
    pixel_t resolve_font_size(pixel_t size) const override
    {
        return fonts ? size : recorder::resolve_font_size(size);
    }
    pixel_t resolve_border_width(pixel_t width) const override
    {
        return edges ? width : recorder::resolve_border_width(width);
    }
};
void layout_policy(bool paint, bool fonts, bool edges)
{
    policy_recorder h(paint, fonts, edges);
    auto            doc =
        document::createFromString("<style>html,body{margin:0;padding:0}div{font-size:12.5px;width:10px;height:10px;"
                                   "padding:1em;border:.5px solid black;background:red}</style><div>x</div>",
                                   &h);
    position    clip(0, 0, 400, 400);
    const float expectedFont = fonts ? 12.5f : 13.f, expectedBorder = edges ? .5f : 1.f;
    const float outer = 10 + 2 * expectedFont + 2 * expectedBorder;
    for(auto width : {400.f, 201.5f, 400.f})
    {
        h.fills.clear();
        h.borders.clear();
        h.border_widths.clear();
        doc->render(width);
        doc->draw(0, 0, 0, &clip);
        CHECK(h.fills.size() == 1 && h.border_widths.size() == 1);
        near(h.requested_sizes.back(), expectedFont);
        near(h.border_widths[0], expectedBorder);
        near(h.fills[0].width, outer);
        near(doc->height(), outer);
    }
}
struct double_density_recorder : recorder
{
    pixel_t resolve_font_size(pixel_t size) const override
    {
        return std::round(size.value() * 2) / 2;
    }
    pixel_t resolve_border_width(pixel_t width) const override
    {
        return recorder::resolve_border_width(width * 2) / 2;
    }
    void round_paint_position(position& p) const override
    {
        p.x      = std::round(p.x.value() * 2) / 2;
        p.y      = std::round(p.y.value() * 2) / 2;
        p.width  = std::round(p.width.value() * 2) / 2;
        p.height = std::round(p.height.value() * 2) / 2;
    }
};
void device_grid_policy()
{
    double_density_recorder h;
    auto                    doc =
        document::createFromString("<style>html,body{margin:0;padding:0}div{font-size:12.3px;width:10px;height:10px;"
                                   "padding:1em;border:.7px solid black;background:red}</style><div>x</div>",
                                   &h);
    doc->render(400);
    position clip(0, 0, 400, 400);
    doc->draw(0, .3f, .3f, &clip);
    near(h.requested_sizes.back(), 12.5f);
    near(h.border_widths.back(), .5f);
    near(doc->height(), 36.f);
    near(h.fills.back().x, .5f);
    near(h.fills.back().width, 36.f);
}
int main()
{
    recorder            legacy;
    fractional_recorder precise;
    test(legacy, false);
    test(precise, true);
    for(bool p : {false, true})
    {
        for(bool f : {false, true})
        {
            for(bool e : {false, true})
            {
                layout_policy(p, f, e);
            }
        }
    }
    device_grid_policy();
    std::cout << "PASS: independent geometry policies, repeated layout and device-grid resolution\n";
}
