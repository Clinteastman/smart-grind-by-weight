"""Verify configurable grind tabs dynamic indexing, mapping, and constraints."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ConfigurableTabsTest(unittest.TestCase):
    def test_ready_screen_tab_indexing_and_constraints(self):
        ready_h = (ROOT / "src/ui/screens/ready_screen.h").read_text()
        code = r'''
#include <cassert>
#include <cstdint>
#include <iostream>

struct Preferences {
    bool manual = true;
    bool single = true;
    bool dbl = true;
    bool custom = true;
    bool getBool(const char* key, bool fallback) {
        if (std::string(key) == "tab_manual") return manual;
        if (std::string(key) == "tab_single") return single;
        if (std::string(key) == "tab_double") return dbl;
        if (std::string(key) == "tab_custom") return custom;
        return fallback;
    }
};

class ReadyScreenDouble {
public:
    bool tab_manual_enabled_ = true;
    bool profile_tab_enabled_[3] = {true, true, true};
    int manual_tab_index_ = 0;
    int profile_tab_indices_[3] = {1, 2, 3};
    int wifi_tab_index_ = 4;
    int menu_tab_index_ = 5;
    int total_tab_count_ = 6;
    bool wifi_tab_enabled_ = true;

    bool is_manual_enabled() const { return tab_manual_enabled_; }
    bool is_profile_enabled(int profile) const {
        if (profile < 0 || profile >= 3) return false;
        return profile_tab_enabled_[profile];
    }

    bool set_tab_config(bool manual, bool single, bool dbl, bool custom) {
        if (!manual && !single && !dbl && !custom) {
            return false;
        }
        tab_manual_enabled_ = manual;
        profile_tab_enabled_[0] = single;
        profile_tab_enabled_[1] = dbl;
        profile_tab_enabled_[2] = custom;
        recalculate_indices();
        return true;
    }

    void recalculate_indices() {
        int idx = 0;
        if (tab_manual_enabled_) {
            manual_tab_index_ = idx++;
        } else {
            manual_tab_index_ = -1;
        }
        for (int p = 0; p < 3; ++p) {
            if (profile_tab_enabled_[p]) {
                profile_tab_indices_[p] = idx++;
            } else {
                profile_tab_indices_[p] = -1;
            }
        }
        if (wifi_tab_enabled_) {
            wifi_tab_index_ = idx++;
        } else {
            wifi_tab_index_ = -1;
        }
        menu_tab_index_ = idx++;
        total_tab_count_ = idx;
    }

    void load_tab_config(Preferences* prefs) {
        if (!prefs) return;
        bool m = prefs->getBool("tab_manual", true);
        bool s = prefs->getBool("tab_single", true);
        bool d = prefs->getBool("tab_double", true);
        bool c = prefs->getBool("tab_custom", true);
        if (!m && !s && !d && !c) {
            m = s = d = c = true;
        }
        set_tab_config(m, s, d, c);
    }

    bool is_manual_tab(int tab) const {
        return tab_manual_enabled_ && tab == manual_tab_index_;
    }
    bool is_profile_tab(int tab) const {
        for (int p = 0; p < 3; ++p) {
            if (profile_tab_enabled_[p] && profile_tab_indices_[p] == tab) return true;
        }
        return false;
    }
    bool is_wifi_tab(int tab) const {
        return wifi_tab_enabled_ && wifi_tab_index_ >= 0 && tab == wifi_tab_index_;
    }
    bool is_menu_tab(int tab) const {
        return tab == menu_tab_index_;
    }
    int profile_index_for_tab(int tab) const {
        for (int p = 0; p < 3; ++p) {
            if (profile_tab_enabled_[p] && profile_tab_indices_[p] == tab) return p;
        }
        return 0;
    }
    int tab_for_profile_index(int profile) const {
        if (profile >= 0 && profile < 3 && profile_tab_enabled_[profile] && profile_tab_indices_[profile] >= 0) {
            return profile_tab_indices_[profile];
        }
        for (int p = 0; p < 3; ++p) {
            if (profile_tab_enabled_[p] && profile_tab_indices_[p] >= 0) return profile_tab_indices_[p];
        }
        if (tab_manual_enabled_ && manual_tab_index_ >= 0) return manual_tab_index_;
        return 0;
    }
};

int main() {
    ReadyScreenDouble rs;

    // 1. Default: all 4 modes enabled
    assert(rs.total_tab_count_ == 6);
    assert(rs.is_manual_tab(0));
    assert(rs.is_profile_tab(1));
    assert(rs.profile_index_for_tab(1) == 0); // Single
    assert(rs.profile_index_for_tab(2) == 1); // Double
    assert(rs.profile_index_for_tab(3) == 2); // Custom
    assert(rs.is_wifi_tab(4));
    assert(rs.is_menu_tab(5));

    // 2. Reject all disabled
    bool ok = rs.set_tab_config(false, false, false, false);
    assert(!ok);
    assert(rs.total_tab_count_ == 6); // Untouched

    // 3. Disable Manual only: 3 profiles remain
    ok = rs.set_tab_config(false, true, true, true);
    assert(ok);
    assert(rs.total_tab_count_ == 5);
    assert(!rs.is_manual_tab(0));
    assert(rs.is_profile_tab(0));
    assert(rs.profile_index_for_tab(0) == 0); // Single is now tab 0
    assert(rs.profile_index_for_tab(1) == 1); // Double is now tab 1
    assert(rs.profile_index_for_tab(2) == 2); // Custom is now tab 2
    assert(rs.is_wifi_tab(3));
    assert(rs.is_menu_tab(4));
    assert(rs.tab_for_profile_index(0) == 0);

    // 4. Single Dose only (Manual only enabled)
    ok = rs.set_tab_config(true, false, false, false);
    assert(ok);
    assert(rs.total_tab_count_ == 3); // Manual, WiFi, Menu
    assert(rs.is_manual_tab(0));
    assert(!rs.is_profile_tab(0));
    assert(rs.is_wifi_tab(1));
    assert(rs.is_menu_tab(2));
    // If asking for profile tab, fallback gracefully to manual
    assert(rs.tab_for_profile_index(1) == 0);

    // 5. Double only enabled (Profile 1 only)
    ok = rs.set_tab_config(false, false, true, false);
    assert(ok);
    assert(rs.total_tab_count_ == 3); // Double (tab 0), WiFi (tab 1), Menu (tab 2)
    assert(rs.is_profile_tab(0));
    assert(rs.profile_index_for_tab(0) == 1);
    assert(rs.tab_for_profile_index(1) == 0);
    assert(rs.tab_for_profile_index(0) == 0); // Fallback to available profile
    assert(rs.is_wifi_tab(1));
    assert(rs.is_menu_tab(2));

    // 6. Test Preferences loader fallback when all 4 are false in storage
    Preferences bad_prefs{false, false, false, false};
    rs.load_tab_config(&bad_prefs);
    assert(rs.total_tab_count_ == 6); // Defaults restored
    assert(rs.is_manual_enabled());
    assert(rs.is_profile_enabled(0));
    assert(rs.is_profile_enabled(1));
    assert(rs.is_profile_enabled(2));

    // 7. Manual and Double only enabled (user configuration)
    ok = rs.set_tab_config(true, false, true, false);
    assert(ok);
    assert(rs.total_tab_count_ == 4); // Manual (0), Double (1), WiFi (2), Menu (3)
    assert(rs.is_manual_tab(0));
    assert(rs.is_profile_tab(1));
    assert(rs.profile_index_for_tab(1) == 1);
    assert(rs.tab_for_profile_index(1) == 1); // Double must map to tab 1, NEVER Menu!
    assert(!rs.is_menu_tab(1));
    // Fallback for disabled profiles must resolve to active Double tab (1)
    assert(rs.tab_for_profile_index(0) == 1);
    assert(rs.tab_for_profile_index(2) == 1);
    assert(rs.is_wifi_tab(2));
    assert(rs.is_menu_tab(3));

    std::cout << "All configurable tabs tests passed!" << std::endl;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "tabs_test.cpp", Path(folder) / "tabs_test"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
