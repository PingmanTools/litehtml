#include "headless_container.h"
#include "check.h"
#include <stdexcept>

struct host_failure : std::runtime_error
{
    host_failure() : std::runtime_error("element creation failed") {}
};

struct failing_container : headless_container
{
    bool fail = false;
    element::ptr create_element(const char* tag, const string_map&, const document::ptr&) override
    {
        if(fail && std::strcmp(tag, "span") == 0) throw host_failure();
        return nullptr;
    }
};

template<class F> void expect_failure(F action)
{
    bool caught = false;
    try { action(); }
    catch(const host_failure&) { caught = true; }
    CHECK(caught);
}

int main()
{
    failing_container host;
    const char* html = "<div><span>content</span></div>";
    for(int i = 0; i < 16; ++i)
    {
        host.fail = true;
        expect_failure([&] { document::createFromString(html, &host); });
        expect_failure([&] {
            const std::string reparse = std::string(1100, ' ') + "<meta charset=utf-8><div><span>caf\xc3\xa9</span></div>";
            document::createFromString(reparse, &host);
        });
        host.fail = false;
        auto doc = document::createFromString("<body><div id=target>original</div></body>", &host);
        doc->render(400);
        auto target = doc->root()->select_one("#target");
        CHECK(target);
        host.fail = true;
        for(bool replace : {false, true})
        {
            expect_failure([&] { doc->append_children_from_string(*target, html, replace); });
        }
        host.fail = false;
        doc->append_children_from_string(*target, "<span>replacement</span>", true);
        doc->render(400);
        CHECK(doc->height() > 0_px);
    }
}
