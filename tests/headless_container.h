#pragma once
#include <litehtml.h>
#include <cstring>
using namespace litehtml;

struct headless_container : document_container
{
    uint_ptr create_font(const font_description& d, const document*, font_metrics* m) override
    {
        *m = {};
        m->font_size = d.size;
        m->height = d.size;
        m->ascent = d.size * .75f;
        m->descent = d.size * .25f;
        m->x_height = d.size * .5f;
        m->ch_width = d.size * .5f;
        return 1;
    }
    void delete_font(uint_ptr) override {}
    pixel_t text_width(const char* text, uint_ptr) override { return std::strlen(text) * 8.f; }
    pixel_t pt_to_px(float pt) const override { return pt * 96.f / 72.f; }
    pixel_t get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "test"; }
    void get_viewport(position& p) const override { p = {0, 0, 400, 400}; }
    void get_media_features(media_features& m) const override
    {
        m = {};
        m.type = media_type_screen;
        m.width = m.device_width = 400;
        m.height = m.device_height = 400;
        m.resolution = 96;
    }
    element::ptr create_element(const char*, const string_map&, const document::ptr&) override { return nullptr; }
    void draw_text(uint_ptr, const char*, uint_ptr, web_color, const position&) override {}
    void draw_list_marker(uint_ptr, const list_marker&) override {}
    void load_image(const char*, const char*, bool) override {}
    void get_image_size(const char*, const char*, size& s) override { s = {10, 10}; }
    void draw_image(uint_ptr, const background_layer&, const std::string&, const std::string&) override {}
    void draw_solid_fill(uint_ptr, const background_layer&, const web_color&) override {}
    void draw_linear_gradient(uint_ptr, const background_layer&, const background_layer::linear_gradient&) override {}
    void draw_radial_gradient(uint_ptr, const background_layer&, const background_layer::radial_gradient&) override {}
    void draw_conic_gradient(uint_ptr, const background_layer&, const background_layer::conic_gradient&) override {}
    void draw_borders(uint_ptr, const borders&, const position&, bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char*) override {}
    void link(const document::ptr&, const element::ptr&) override {}
    void on_anchor_click(const char*, const element::ptr&) override {}
    void on_mouse_event(const element::ptr&, mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(std::string&, text_transform) override {}
    void import_css(std::string&, const std::string&, std::string&) override {}
    void set_clip(const position&, const border_radiuses&) override {}
    void del_clip() override {}
    void get_language(std::string&, std::string&) const override {}
};
