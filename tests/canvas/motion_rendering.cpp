#include "test_container.h"
#include "litehtml/motion_transform.h"
#include "litehtml/render_item.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>

using namespace litehtml;

static void require(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

class motion_canvas : public test_container
{
  public:
    canvas_ity::canvas* target = nullptr;
    int depth = 0;
    mutable int viewport_calls = 0;
    void get_viewport(position& viewport) const override
    {
        ++viewport_calls;
        test_container::get_viewport(viewport);
    }
    std::vector<web_color> text_colors;
    void draw_text(uint_ptr hdc, const char* text, uint_ptr font, web_color color, const position& box) override
    {
        text_colors.push_back(color);
        test_container::draw_text(hdc, text, font, color, box);
    }
    motion_canvas() : test_container(100, 100, "") {}
    void push_transform(const motion_matrix& matrix) override
    {
        target->save();
        const auto& m = matrix.values;
        target->transform(m[0], m[1], m[4], m[5], m[12], m[13]);
        ++depth;
    }
    void pop_transform() override
    {
        target->restore();
        --depth;
    }
};

static std::array<unsigned char, 4> pixel(const document::ptr& doc, motion_canvas& host,
                                        double milliseconds, int x, int y, float draw_y = 0)
{
    canvas_ity::canvas canvas(100, 100);
    host.target = &canvas;
    host.text_colors.clear();
    doc->set_time(milliseconds);
    doc->render(100);
    position clip(0, 0, 100, 100);
    doc->draw(reinterpret_cast<uint_ptr>(&canvas), 0, draw_y, &clip);
    require(host.depth == 0, "unbalanced transform scopes");
    std::array<unsigned char, 4> result{};
    canvas.get_image_data(result.data(), 1, 1, 4, x, y);
    host.target = nullptr;
    return result;
}

int main()
{
    motion_canvas host;
    const std::string reset = "html,body{margin:0;padding:0;background:transparent}div{width:20px;height:20px;background:red}";
    auto pulse = document::createFromString("<style>" + reset +
        "@keyframes pulse{from{opacity:1}to{opacity:.2}}#box{animation:pulse 1s linear forwards}</style><div id='box'></div>", &host);
    require(pixel(pulse, host, 0, 10, 10)[3] == 255, "pulse initial alpha");
    require(pulse->animations_active(), "pulse not active initially");
    const auto mid = pixel(pulse, host, 500, 10, 10);
    require(std::abs(static_cast<int>(mid[3]) - 153) <= 1, "pulse midpoint alpha");
    require(std::abs(static_cast<int>(pixel(pulse, host, 1000, 10, 10)[3]) - 51) <= 1, "pulse final alpha");
    require(!pulse->animations_active(), "finite pulse never stopped");

    auto translate = document::createFromString("<style>" + reset +
        "@keyframes slide{from{transform:translateX(0px)}to{transform:translateX(40px)}}#box{animation:slide 1s linear forwards}</style><div id='box'></div>", &host);
    require(pixel(translate, host, 0, 10, 10)[3] == 255, "translate initial position");
    require(pixel(translate, host, 500, 5, 10)[3] == 0, "translate left stale pixels");
    require(pixel(translate, host, 500, 30, 10)[3] == 255, "translate midpoint position");
    require(pixel(translate, host, 1000, 50, 10)[3] == 255, "translate final position");
    require(!translate->animations_active(), "finite translate never stopped");

    auto hover = document::createFromString("<style>" + reset +
        "#box{transition:background-color 1s linear}#box:hover{background:blue}</style><div id='box'></div>", &host);
    const auto red = pixel(hover, host, 0, 10, 10);
    require(red[0] == 255 && red[2] == 0, "hover initial red");
    hover->on_mouse_over(10, 10, 10, 10, [](const position&) {});
    pixel(hover, host, 0, 10, 10);
    require(hover->animations_active(), "hover transition did not start");
    const auto purple = pixel(hover, host, 500, 10, 10);
    require(std::abs(static_cast<int>(purple[0]) - 128) <= 1 && std::abs(static_cast<int>(purple[2]) - 128) <= 1, "hover midpoint color");
    const auto blue = pixel(hover, host, 1000, 10, 10);
    require(blue[0] == 0 && blue[2] == 255, "hover final blue");
    require(!hover->animations_active(), "hover transition never stopped");
    auto alternate = document::createFromString("<style>" + reset +
        "@keyframes pulse{from{opacity:1}to{opacity:.2}}#box{animation:pulse 1s linear 2 alternate forwards}</style><div id='box'></div>", &host);
    pixel(alternate, host, 0, 10, 10);
    require(std::abs(static_cast<int>(pixel(alternate, host, 1000, 10, 10)[3]) - 51) <= 1, "alternate iteration boundary");
    require(std::abs(static_cast<int>(pixel(alternate, host, 1500, 10, 10)[3]) - 153) <= 1, "alternate reverse midpoint");
    require(pixel(alternate, host, 2000, 10, 10)[3] == 255 && !alternate->animations_active(), "alternate final fill/active state");

    auto nested = document::createFromString("<style>" + reset +
        "#parent{background:transparent;opacity:.5;transform:translateX(10px)}#child{opacity:.5;transform:translateX(20px)}</style><div id='parent'><div id='child'></div></div>", &host);
    require(pixel(nested, host, 0, 5, 10)[3] == 0, "nested translation left stale pixels");
    require(std::abs(static_cast<int>(pixel(nested, host, 0, 35, 10)[3]) - 64) <= 1, "nested transform/opacity composition");
    auto delayed = document::createFromString("<style>" + reset +
        "@keyframes pulse{from{opacity:1}to{opacity:.2}}#box{opacity:.7;animation:pulse 1s linear 500ms reverse both}</style><div id='box'></div>", &host);
    require(std::abs(static_cast<int>(pixel(delayed, host, 0, 10, 10)[3]) - 51) <= 1, "reverse backwards fill during delay");
    require(delayed->animations_active(), "delayed animation not active");
    require(std::abs(static_cast<int>(pixel(delayed, host, 1000, 10, 10)[3]) - 153) <= 1, "reverse delayed midpoint");
    require(pixel(delayed, host, 1500, 10, 10)[3] == 255 && !delayed->animations_active(), "reverse final fill");

    auto paused = document::createFromString("<style>" + reset +
        "@keyframes pulse{from{opacity:1}to{opacity:.2}}#box{animation:pulse 1s linear forwards}#box:hover{animation-play-state:paused}</style><div id='box'></div>", &host);
    pixel(paused, host, 0, 10, 10);
    const auto before_pause = pixel(paused, host, 300, 10, 10);
    paused->on_mouse_over(10, 10, 10, 10, [](const position&) {});
    require(pixel(paused, host, 800, 10, 10) == before_pause && !paused->animations_active(), "paused animation advanced");
    paused->on_mouse_leave([](const position&) {});
    require(pixel(paused, host, 800, 10, 10) == before_pause, "resume jumped");
    require(std::abs(static_cast<int>(pixel(paused, host, 1000, 10, 10)[3]) - 153) <= 1, "resume elapsed time mismatch");
    require(std::abs(static_cast<int>(pixel(paused, host, 1500, 10, 10)[3]) - 51) <= 1 && !paused->animations_active(), "resumed animation final state");

    auto reversal = document::createFromString("<style>" + reset +
        "#box{transition:background-color 1s linear}#box:hover{background:blue}</style><div id='box'></div>", &host);
    pixel(reversal, host, 0, 10, 10);
    reversal->on_mouse_over(10, 10, 10, 10, [](const position&) {});
    const auto before_reverse = pixel(reversal, host, 400, 10, 10);
    reversal->on_mouse_leave([](const position&) {});
    require(pixel(reversal, host, 400, 10, 10) == before_reverse, "transition reversal discontinuity");
    const auto returning = pixel(reversal, host, 600, 10, 10);
    require(std::abs(static_cast<int>(returning[0]) - 204) <= 1 && std::abs(static_cast<int>(returning[2]) - 51) <= 1, "reversal shortening midpoint");
    require(pixel(reversal, host, 800, 10, 10)[2] == 0 && !reversal->animations_active(), "reversal duration did not shorten");

    auto translated_box = translate->root()->select_one("#box");
    require(translate->root_render()->get_element_by_point(50, 10, 50, 10, nullptr) == translated_box, "translated hit not inverse mapped");
    require(translate->root_render()->get_element_by_point(10, 10, 10, 10, nullptr) != translated_box, "untranslated hit incorrectly retained");
    auto rotated = document::createFromString("<style>" + reset +
        "#box{height:10px;transform-origin:0 0;transform:translate(40px,10px) rotate(90deg)}</style><div id='box'></div>", &host);
    pixel(rotated, host, 0, 35, 20);
    require(rotated->root_render()->get_element_by_point(35, 20, 35, 20, nullptr) == rotated->root()->select_one("#box"), "rotated hit not inverse mapped");
    require(nested->root_render()->get_element_by_point(35, 10, 35, 10, nullptr) == nested->root()->select_one("#child"), "nested hit mapping");
    auto fixed = document::createFromString("<style>" + reset +
        "body{height:300px}#box{position:fixed;left:10px;top:10px;transform:translateX(20px)}</style><div id='box'></div>", &host);
    require(pixel(fixed, host, 0, 35, 15, -100)[3] == 255, "fixed transform draw shifted with scroll");
    require(fixed->root_render()->get_element_by_point(35, 115, 35, 15, nullptr) == fixed->root()->select_one("#box"), "fixed hit ignored client coordinates");

    auto mixed = document::createFromString("<style>" + reset +
        "#outer{width:200px;background:transparent}#inner{width:50%;background:transparent}"
        "@keyframes widthchange{from{width:50%}to{width:100px}}#child{animation:widthchange 1s linear forwards}</style>"
        "<div id='outer'><div id='inner'><div id='child'></div></div></div>", &host);
    require(pixel(mixed, host, 0, 60, 10)[3] == 0, "initial nested percentage width");
    require(pixel(mixed, host, 500, 74, 10)[3] == 255 && pixel(mixed, host, 500, 76, 10)[3] == 0, "mixed-unit midpoint used wrong containing block");
    auto inherited = document::createFromString("<style>" + reset +
        "@keyframes ink{from{color:red}to{color:blue}}#parent{animation:ink 1s linear forwards}</style><div id='parent'><span><span>ink</span></span></div>", &host);
    pixel(inherited, host, 0, 10, 10);
    pixel(inherited, host, 500, 10, 10);
    require(!host.text_colors.empty(), "inherited text was not drawn");
    for (const auto color : host.text_colors)
        require(std::abs(static_cast<int>(color.red) - 128) <= 1 && std::abs(static_cast<int>(color.blue) - 128) <= 1, "animated color did not propagate through nested spans");
    auto sparse_frames = document::createFromString("<style>" + reset +
        "@keyframes sparse{25%{opacity:.4}75%{opacity:.8}50%{width:30px}}"
        "#box{animation:sparse 1s linear forwards}</style><div id='box'></div>", &host);
    require(std::abs(static_cast<int>(pixel(sparse_frames, host, 0, 10, 10)[3]) - 255) <= 1, "implicit initial keyframe");
    require(std::abs(static_cast<int>(pixel(sparse_frames, host, 500, 10, 10)[3]) - 153) <= 1, "sparse per-property interpolation");
    require(pixel(sparse_frames, host, 1000, 10, 10)[3] == 255, "implicit final keyframe");
    auto variable_frames = document::createFromString("<style>" + reset +
        "@keyframes custom{to{opacity:var(--alpha)}}#box{--alpha:.2;animation:custom 1s linear forwards}"
        "#box:hover{--alpha:.6}</style><div id='box'></div>", &host);
    require(std::abs(static_cast<int>(pixel(variable_frames, host, 1000, 10, 10)[3]) - 51) <= 1, "keyframe custom property resolution");
    variable_frames->on_mouse_over(10, 10, 10, 10, [](const position&) {});
    require(std::abs(static_cast<int>(pixel(variable_frames, host, 1000, 10, 10)[3]) - 153) <= 1, "keyframe custom property invalidation");

    const position visible(0, 0, 100, 100);
    auto sleeping = document::createFromString("<style>" + reset +
        "#box{margin-top:500px;animation:pulse 1s linear infinite}@keyframes pulse{to{opacity:.2}}"
        "</style><div id='box'>text</div>", &host);
    pixel(sleeping, host, 0, 10, 10);
    require(sleeping->animations_active() && sleeping->next_animation_delay(visible) < 0,
        "offscreen paint-only motion did not sleep");
    require(sleeping->next_animation_delay(position(0, 500, 100, 100)) > 0,
        "visible paint-only motion did not schedule");
    auto affecting_layout = document::createFromString("<style>" + reset +
        "#box{margin-top:500px;animation:grow 1s linear infinite}@keyframes grow{to{height:50px}}"
        "</style><div id='box'></div>", &host);
    pixel(affecting_layout, host, 0, 10, 10);
    require(affecting_layout->next_animation_delay(visible) > 0, "offscreen layout animation slept");
    auto entering = document::createFromString("<style>" + reset +
        "#box{margin-top:500px;animation:enter 1s linear infinite}@keyframes enter{to{transform:translateY(-500px)}}"
        "</style><div id='box'></div>", &host);
    pixel(entering, host, 0, 10, 10);
    require(entering->next_animation_delay(visible) > 0, "offscreen transform animation slept");
    auto horizontal = document::createFromString("<style>" + reset +
        "#box{margin-top:500px;animation:slide 1s linear infinite}@keyframes slide{to{transform:translateX(150px)}}"
        "</style><div id='box'><a href='#'>text</a></div>", &host);
    pixel(horizontal, host, 0, 10, 10);
    require(horizontal->next_animation_delay(visible) < 0, "vertically offscreen horizontal motion did not sleep");
    require(horizontal->next_animation_delay(position(0, 500, 100, 100)) > 0, "visible horizontal motion slept");
    auto inline_child = document::createFromString("<style>" + reset +
        "#box{margin-top:500px;animation:fade 1s linear infinite}@keyframes fade{to{opacity:.2}}"
        "</style><div id='box'><a href='#'>inline child</a></div>", &host);
    pixel(inline_child, host, 0, 10, 10);
    require(inline_child->next_animation_delay(visible) < 0, "offscreen inline child prevented opacity sleep");
    auto overflow_child = document::createFromString("<style>" + reset +
        "#box{margin-top:500px;position:relative;animation:pulse 1s linear infinite}"
        "#child{position:absolute;top:-500px}@keyframes pulse{to{opacity:.2}}"
        "</style><div id='box'><div id='child'></div></div>", &host);
    pixel(overflow_child, host, 0, 10, 10);
    require(overflow_child->next_animation_delay(visible) > 0, "visible overflow child was culled with parent");
    auto border_visible = document::createFromString("<style>" + reset +
        "#box{position:absolute;top:-10px;height:1px;border-bottom:20px solid red;animation:pulse 1s linear infinite}"
        "@keyframes pulse{to{opacity:.2}}</style><div id='box'></div>", &host);
    pixel(border_visible, host, 0, 10, 5);
    require(border_visible->next_animation_delay(visible) > 0, "visible border excluded from scheduling bounds");
    pixel(delayed, host, 0, 10, 10);
    require(std::abs(delayed->next_animation_delay(visible) - 500) < .001, "delay did not sleep until start");

    std::string plain_html = "<style>html,body{margin:0}div{height:1px}</style>";
    for(int i = 0; i < 10000; ++i) plain_html += "<div></div>";
    auto plain = document::createFromString(plain_html, &host);
    plain->render(100);
    canvas_ity::canvas plain_canvas(100, 100);
    host.target = &plain_canvas;
    const position plain_clip(0, 0, 100, 100);
    const auto measure = [](auto action) {
        const auto start = std::chrono::steady_clock::now();
        for(int i = 0; i < 10; ++i) action();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 10;
    };
    host.viewport_calls = 0;
    const auto render_ms = measure([&] { plain->render(100); });
    const auto render_calls = host.viewport_calls / 10;
    host.viewport_calls = 0;
    const auto pulse_ms = measure([&] {
        const auto frame = plain->render_frame(0, 100, plain_clip);
        require(!frame.active && frame.next_delay < 0, "static frame unexpectedly scheduled");
    });
    require(host.viewport_calls == 10, "static pulse queried viewport per element");
    std::printf("plain 10k: combined pulse %.3f ms\n", pulse_ms);
    host.viewport_calls = 0;
    const auto draw_ms = measure([&] { plain->draw(reinterpret_cast<uint_ptr>(&plain_canvas), 0, 0, &plain_clip); });
    const auto draw_calls = host.viewport_calls / 10;
    host.viewport_calls = 0;
    const auto hit_ms = measure([&] { plain->on_mouse_over(50, 50, 50, 50, [](const position&) {}); });
    const auto hit_calls = host.viewport_calls / 10;
    std::printf("plain 10k: render %.3f ms (%d viewport calls), draw %.3f ms (%d), hover %.3f ms (%d)\n",
                render_ms, render_calls, draw_ms, draw_calls, hit_ms, hit_calls);
    for(const auto& [name, count, effect] : std::vector<std::tuple<const char*, int, const char*>>{
            {"10 opacity", 10, "opacity:.2"}, {"1000 opacity", 1000, "opacity:.2"},
            {"1000 transform", 1000, "transform:translateX(20px)"}})
    {
        std::string html = "<style>html,body{margin:0}div{height:1px}@keyframes move{to{";
        html += effect;
        html += "}}.moving{animation:move 1s linear infinite}</style>";
        for(int i = 0; i < 10000; ++i) html += i < count ? "<div class='moving'></div>" : "<div></div>";
        auto animated = document::createFromString(html, &host);
        animated->render(100);
        double time = 0;
        host.viewport_calls = 0;
        const auto elapsed = measure([&] { animated->set_time(time += 16); animated->render(100); });
        const auto calls = host.viewport_calls / 10;
        host.viewport_calls = 0;
        const auto pulse = measure([&] {
            const auto frame = animated->render_frame(time += 16, 100, plain_clip);
            require(frame.active && frame.next_delay > 0, "visible animation pulse did not schedule");
        });
        require(host.viewport_calls == 10, "animation pulse queried viewport per element");
        std::printf("10k with %s: combined pulse %.3f ms\n", name, pulse);
        host.viewport_calls = 0;
        const auto draw = measure([&] { animated->draw(reinterpret_cast<uint_ptr>(&plain_canvas), 0, 0, &plain_clip); });
        require(calls == 1, "animated render queried viewport per element");
        require(host.viewport_calls <= 10, "animated draw queried viewport per element");
        std::printf("10k with %s: render %.3f ms (%d viewport calls), draw %.3f ms (%d)\n",
                    name, elapsed, calls, draw, host.viewport_calls / 10);
    }
    int width = 100;
    host.viewport_calls = 0;
    const auto resize_ms = measure([&] { host.width = width = width == 100 ? 120 : 100; plain->render(width); });
    require(host.viewport_calls == 10, "resize queried viewport per element");
    host.viewport_calls = 0;
    int scroll = 0;
    const auto scroll_ms = measure([&] { plain->draw(reinterpret_cast<uint_ptr>(&plain_canvas), 0, --scroll, &plain_clip); });
    require(host.viewport_calls == 0, "plain scrolling queried viewport per element");
    std::printf("plain 10k: resize %.3f ms, scroll %.3f ms\n", resize_ms, scroll_ms);
    auto viewport_transform = document::createFromString("<style>" + reset +
        "#box{transform:translateX(50vw)}</style><div id='box'></div>", &host);
    host.width = 100;
    require(pixel(viewport_transform, host, 0, 55, 10)[3] == 255, "viewport transform initial position");
    host.width = 160;
    require(pixel(viewport_transform, host, 0, 55, 10)[3] == 0, "viewport transform used stale width");
    require(pixel(viewport_transform, host, 0, 85, 10)[3] == 255, "viewport transform resize position");
    host.width = 100;
    require(render_calls < 10, "plain render queried viewport per element");
    require(draw_calls == 0, "plain draw queried viewport per element");
    require(hit_calls == 0, "plain hover queried viewport per element");
    host.target = nullptr;
    std::puts("motion rendering: PASS");
}
