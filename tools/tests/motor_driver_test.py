"""Compile the actual motor driver with RMT fakes; no motor is operated."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
STUBS = r'''
#include <algorithm>
#include <iterator>
#include <functional>
#include <cstdint>
#include <cassert>
#include <vector>
#define LOG_BLE(...) ((void)0)
#define DEBUG_ENABLE_LOADCELL_MOCK 0
#define HW_GRINDER_SETTLING_TIME_MS 100
using gpio_num_t=int;
using esp_err_t=int;
using rmt_channel_handle_t=void*;
using rmt_encoder_handle_t=void*;
constexpr int ESP_OK=0, ESP_ERR_TIMEOUT=1, RMT_CLK_SRC_DEFAULT=0, GPIO_MODE_OUTPUT=0;
struct rmt_symbol_word_t { uint32_t duration0:15; uint32_t level0:1; uint32_t duration1:15; uint32_t level1:1; };
struct rmt_tx_channel_config_t { int gpio_num,clk_src,resolution_hz,mem_block_symbols,trans_queue_depth; };
struct rmt_copy_encoder_config_t {};
struct rmt_transmit_config_t { int loop_count=0; };
int tx_error=0, completion=ESP_ERR_TIMEOUT, enable_error=0, disable_error=0;
bool active=false;
const rmt_symbol_word_t* payload=nullptr;
size_t count=0;
int loops=0;
int gpio_low_calls=0;
unsigned long millis(){return 100;}
int gpio_reset_pin(int){return 0;}
int gpio_set_direction(int,int){return 0;}
int gpio_set_level(int,int value){assert(value==0);++gpio_low_calls;return 0;}
int rmt_new_tx_channel(const rmt_tx_channel_config_t*,void** c){*c=(void*)1;return 0;}
int rmt_new_copy_encoder(const rmt_copy_encoder_config_t*,void** e){*e=(void*)2;return 0;}
int rmt_enable(void*){return enable_error;}
int rmt_disable(void*){if(disable_error)return disable_error; active=false;return 0;}
int rmt_encoder_reset(void*){assert(!active);return 0;}
int rmt_del_channel(void*){return 0;}
int rmt_del_encoder(void*){assert(!active);return 0;}
int rmt_transmit(void*,void*,const void* data,size_t bytes,const rmt_transmit_config_t* config){
    assert(!active);
    payload=static_cast<const rmt_symbol_word_t*>(data);count=bytes/sizeof(*payload);loops=config->loop_count;
    if(tx_error)return tx_error;
    active=true;completion=ESP_ERR_TIMEOUT;return 0;
}
int rmt_tx_wait_all_done(void*,int timeout){assert(timeout==0);if(completion==0)active=false;return completion;}
enum class UIGrindEvent { BACKGROUND_CHANGE };
enum class GrindPhase { IDLE };
struct GrindEventData {
 UIGrindEvent event;GrindPhase phase;float current_weight;int progress_percent;
 const char* phase_display_text;bool show_taring_text;bool background_active;
};
'''
CASES = r'''
int main(){
 Grinder motor;motor.init(16);assert(motor.is_initialized());
 for(uint32_t ms=1;ms<=2064;++ms){
  motor.start_pulse_rmt(ms);assert(motor.is_grinding());assert(loops==0);
  assert(!motor.is_pulse_complete()); // queued, not started: must remain active
  uint32_t high=0,low=0;bool ended=false;
  for(size_t i=0;i<count;++i){
   uint32_t durations[]={payload[i].duration0,payload[i].duration1};
   uint32_t levels[]={payload[i].level0,payload[i].level1};
   for(int half=0;half<2;++half){
    if(durations[half]==0){assert(ended);continue;}
    if(levels[half]){assert(!ended);high+=durations[half];}
    else{ended=true;low+=durations[half];}
   }
  }
  assert(high==ms*1000 && low==1);
  completion=ESP_OK;assert(motor.is_pulse_complete());assert(!motor.is_grinding());
 }
 motor.start();assert(motor.is_grinding() && loops==-1);
 assert(payload[0].level0==1 && payload[0].level1==1);
 motor.start_pulse_rmt(100); // cancels old transmission before modifying payload
 assert(motor.is_grinding() && loops==0);
 motor.start();motor.stop();assert(!motor.is_grinding());
 for(uint32_t invalid: {0U,2065U,UINT32_MAX}){
  motor.start_pulse_rmt(invalid);assert(!motor.is_grinding());
 }
 tx_error=-1;motor.start();assert(!motor.is_grinding());
 motor.start_pulse_rmt(100);assert(!motor.is_grinding());tx_error=0;
 motor.start_pulse_rmt(100);completion=-1;assert(motor.is_pulse_complete());assert(!motor.is_grinding());
 motor.start();disable_error=-1;int previous=gpio_low_calls;motor.stop();
 assert(!motor.is_initialized() && !motor.is_grinding() && gpio_low_calls>previous);
 motor.start();assert(!motor.is_grinding());
 disable_error=0;active=false;enable_error=-1;
 Grinder failed;failed.init(16);assert(!failed.is_initialized());
}
'''


def source_without_includes(path):
    return "\n".join(line for line in path.read_text().splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class MotorDriverTest(unittest.TestCase):
    def test_actual_driver(self):
        code = (STUBS + source_without_includes(ROOT / "src/hardware/grinder.h")
                + "\n" + source_without_includes(ROOT / "src/hardware/grinder.cpp")
                + "\n" + CASES)
        with tempfile.TemporaryDirectory(prefix="smart-grind-motor-test-") as folder:
            source = Path(folder) / "test.cpp"
            binary = Path(folder) / "test"
            source.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
