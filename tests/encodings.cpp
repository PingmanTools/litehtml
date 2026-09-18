#include "litehtml/encodings.h"
#include "check.h"
#include <stdexcept>
#include <string>
using namespace litehtml;
int main()
{
    const std::string unicode = "caf\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80";
    CHECK(decode(unicode, encoding::utf_8) == unicode);
    CHECK(decode(std::string("\xef\xbb\xbf") + unicode, encoding::utf_8) == unicode);
    CHECK(decode(std::string("\xff"), encoding::utf_8) == "\xef\xbf\xbd");
    estring html("<meta charset='windows-1252'><p>caf\xc3\xa9</p>");
    encoding_sniffing_algorithm(html);
#if defined(LITEHTML_UTF8_ONLY) && LITEHTML_UTF8_ONLY
    CHECK(html.encoding == encoding::utf_8 && html.confidence == confidence::certain);
    bool rejected = false;
    try { decode(std::string("\xe9"), encoding::windows_1252); }
    catch(const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { estring utf16("\xff\xfeX"); encoding_sniffing_algorithm(utf16); }
    catch(const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    for(auto input_encoding : {encoding::windows_1252, encoding::utf_16le, encoding::utf_16be})
    {
        rejected = false;
        try { estring explicit_input("text", input_encoding); encoding_sniffing_algorithm(explicit_input); }
        catch(const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
#else
    CHECK(html.encoding == encoding::windows_1252);
    CHECK(decode(std::string("\xe9"), encoding::windows_1252) == "\xc3\xa9");
    CHECK(decode(std::string("\xff\xfeX\0", 4), encoding::utf_16le) == "X");
#endif
}
