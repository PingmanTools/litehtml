#include "test_container.h"
#include "Font.h"
#include "../check.h"

struct tracked_font : Font
{
    bool& destroyed;
    explicit tracked_font(bool& flag) : destroyed(flag) {}
    ~tracked_font() { destroyed = true; }
    pixel_t text_width(string) override { return 0; }
    void draw_text(canvas&, string, color, int, int) override {}
};

int main()
{
    test_container host(100, 100, "");
    bool destroyed = false;
    host.delete_font(reinterpret_cast<uint_ptr>(new tracked_font(destroyed)));
    CHECK(destroyed);
    host.delete_font(0);
}
