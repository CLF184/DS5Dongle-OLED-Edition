// The firmware version as the OLED shows it: FIRMWARE_VERSION without the
// "-oled-edition" suffix (every build of this firmware has it), build metadata
// after '+' kept: "v0.9.5-dev.4-oled-edition+3.a1b2c3d" -> "v0.9.5-dev.4+3.a1b2c3d".
// Done at compile time; pure, so the host tests check it.
#ifndef DS5_BRIDGE_VERSION_LABEL_H
#define DS5_BRIDGE_VERSION_LABEL_H

namespace version_label {

constexpr int kMax = 80;

struct Label {
    char s[kMax]{};
    int n = 0;
};

constexpr Label make(const char *prefix, const char *version) {
    constexpr const char kSuffix[] = "-oled-edition";
    constexpr int kSuffixLen = sizeof(kSuffix) - 1;
    Label out{};
    for (int i = 0; prefix[i] && out.n < kMax - 1; i++) out.s[out.n++] = prefix[i];
    for (int i = 0; version[i] && out.n < kMax - 1;) {
        bool match = true;
        for (int k = 0; k < kSuffixLen; k++) {
            if (version[i + k] != kSuffix[k]) { match = false; break; }
        }
        if (match) { i += kSuffixLen; continue; }
        out.s[out.n++] = version[i++];
    }
    out.s[out.n] = 0;
    return out;
}

}  // namespace version_label

#endif  // DS5_BRIDGE_VERSION_LABEL_H
