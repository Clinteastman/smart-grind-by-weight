"""Exercise the production panel-off parsing block with old and current forms."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from settings_persistence_test import method

ROOT = Path(__file__).resolve().parents[2]


class SettingsCompatibilityTest(unittest.TestCase):
    def test_old_forms_preserve_panel_settings(self):
        source = (ROOT / "src/network/device_api.cpp").read_text()
        handler = source.split("esp_err_t DeviceApi::queue_settings_update(", 1)[1]
        required = handler.split("static const char* required[] = {", 1)[1].split("};", 1)[0]
        self.assertNotIn('"display_off_enabled"', required)
        self.assertNotIn('"display_off_delay_s"', required)
        block = handler.split("    const long screensaver_idle_timeout_s =", 1)[1]
        block = "const long screensaver_idle_timeout_s =" + block.split(
            "    const std::string screensaver_style", 1)[0]
        resolve = source.split("    DeviceSettingsUpdate settings = update;", 1)[1].split(
            "    Preferences* grinder =", 1)[0]
        form_bool = method(source, "bool form_bool(")
        code = r'''
#include <map>
#include <string>
#include <cstdint>
#include <cassert>
#include "system/string_utils.h"
''' + form_bool + r'''
struct Settings {
 uint16_t screensaver_idle_timeout_s=0;
 uint8_t screensaver_startup_timeout_s=0;
 bool display_off_enabled=false;
 uint16_t display_off_delay_s=3600;
 bool has_display_off_enabled=true,has_display_off_delay_s=true;
};
struct ScreensaverTimingSettings {bool display_off_enabled;uint16_t display_off_delay_s;};
namespace ScreensaverSettings {
 ScreensaverTimingSettings saved{true,7200};
 ScreensaverTimingSettings load_timing(){return saved;}
}
struct Request {
 std::map<std::string,std::string> fields{{"screensaver_idle_timeout_s",{"300"}},
                                          {"screensaver_startup_timeout_s",{"3"}}};
};
Settings parse(Request* request){
 // Mirrors the form-field accessors queue_settings_update builds over the body.
 auto has=[request](const char* name){return request->fields.count(name)>0;};
 auto value=[request](const char* name){return request->fields.at(name);};
 Settings settings;
''' + block + r'''
 return settings;
}
Settings apply(Settings settings){
''' + resolve + r'''
 ScreensaverSettings::saved={settings.display_off_enabled,settings.display_off_delay_s};
 return settings;
}
int main(){
 Request old;
 auto settings=apply(parse(&old));
 assert(settings.display_off_enabled && settings.display_off_delay_s==7200);
 assert(settings.screensaver_idle_timeout_s==300 && settings.screensaver_startup_timeout_s==3);
 old.fields["display_off_enabled"]={"0"};
 settings=apply(parse(&old));
 assert(!settings.display_off_enabled && settings.display_off_delay_s==7200);
 old.fields.erase("display_off_enabled");
 ScreensaverSettings::saved.display_off_enabled=true;
 old.fields["display_off_delay_s"]={"30"};
 settings=apply(parse(&old));
 assert(settings.display_off_enabled && settings.display_off_delay_s==30);
 old.fields["display_off_enabled"]={"false"};
 settings=apply(parse(&old));
 assert(!settings.display_off_enabled && settings.display_off_delay_s==30);
 old.fields["display_off_enabled"]={"true"};
 settings=parse(&old);assert(settings.display_off_enabled);
 ScreensaverSettings::saved={false,3600};
 Request another_old;settings=apply(parse(&another_old));
 assert(!settings.display_off_enabled && settings.display_off_delay_s==3600);
 // Queue both before applying either: old form must preserve the newer save.
 old.fields["display_off_enabled"]={"true"};old.fields["display_off_delay_s"]={"7200"};
 auto current=parse(&old);auto legacy=parse(&another_old);
 apply(current);settings=apply(legacy);
 assert(settings.display_off_enabled && settings.display_off_delay_s==7200);
}
'''
        with tempfile.TemporaryDirectory(prefix="smart-grind-settings-test-") as folder:
            cpp = Path(folder) / "test.cpp"
            binary = Path(folder) / "test"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
