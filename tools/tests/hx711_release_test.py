"""Run the production HX711 conversion against a clocked GPIO double."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HX711ReleaseTest(unittest.TestCase):
    def test_post_conversion_release(self):
        source = (ROOT / "src/hardware/hx711_driver.cpp").read_text()
        methods = source[source.index("bool HX711Driver::is_ready()"):
                         source.index("int32_t HX711Driver::get_raw_data()")]
        code = r'''
#include <cassert>
#include <cstdint>
#define LOW 0
#define HIGH 1
#define LOG_BLE(...) ((void)0)
bool ready=true, released=true, irq_enabled=true;
unsigned clocks=0;
uint32_t bits=0;
unsigned long micros(){return 1000;}
void delayMicroseconds(unsigned){}
void noInterrupts(){irq_enabled=false;}
void interrupts(){irq_enabled=true;}
void digitalWrite(uint8_t,int level){if(level==HIGH) ++clocks;}
int digitalRead(uint8_t){
 if(clocks==0) return ready ? LOW : HIGH;
 if(clocks<=24) return (bits>>(24-clocks))&1;
 return released ? HIGH : LOW;
}
class HX711Driver {
public:
 uint8_t sck_pin=1,dout_pin=2,gain=1;
 int32_t last_raw_data=123;
 bool data_ready_flag=false;
 unsigned long conversion_start_time=0,conversion_time=0;
 static constexpr uint8_t SCK_DELAY=1;
 bool is_ready();bool data_waiting_async();bool update_async();bool conversion_24bit();
};
''' + methods + r'''
int main(){
 HX711Driver driver;
 ready=false;assert(!driver.update_async());assert(clocks==0 && irq_enabled);
 ready=true;
 for(uint8_t gain:{1,2,3}){
  driver.gain=gain;
  for(uint32_t value:{0U,0x123456U,0xFFFFFFU}){
   clocks=0;bits=value;released=true;
   assert(driver.update_async());assert(clocks==24U+gain && irq_enabled);
   assert(driver.last_raw_data==int32_t(value^0x800000));
   const auto previous=driver.last_raw_data;
   clocks=0;bits=0;released=false;
   assert(!driver.update_async());assert(irq_enabled && !driver.data_ready_flag);
   assert(driver.last_raw_data==previous);
  }
 }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "hx711.cpp"
            binary = Path(folder) / "hx711"
            cpp.write_text("#include <initializer_list>\n" + code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
