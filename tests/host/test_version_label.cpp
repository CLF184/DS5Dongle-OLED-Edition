// src/version_label.h: the OLED's version text.
#include <cstring>
#include "version_label.h"
#include "test.h"

TEST(version_label_strips_suffix_keeps_metadata) {
    constexpr auto a = version_label::make("DS5 B. ", "v0.9.5-dev.4-oled-edition");
    CHECK(std::strcmp(a.s, "DS5 B. v0.9.5-dev.4") == 0);
    CHECK_EQ(a.n, 19);  // exactly the Status title's space
    constexpr auto b = version_label::make("", "v0.9.3-oled-edition+3.a1b2c3d.dirty");
    CHECK(std::strcmp(b.s, "v0.9.3+3.a1b2c3d.dirty") == 0);
    constexpr auto c = version_label::make("", "v0.0.0-oled-edition+unknown");
    CHECK(std::strcmp(c.s, "v0.0.0+unknown") == 0);
    constexpr auto d = version_label::make("x", "dev");
    CHECK(std::strcmp(d.s, "xdev") == 0);
    CHECK_EQ(d.n, 4);
}
