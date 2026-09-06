"""Run production freshness/sampling and stop-guard code with a simulated ADC."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StaleScaleTest(unittest.TestCase):
    def test_sampling_and_controller_guard(self):
        sensor_cpp = (ROOT / "src/hardware/WeightSensor.cpp").read_text()
        sensor_h = (ROOT / "src/hardware/WeightSensor.h").read_text()
        controller = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        sampling = sensor_cpp.split("bool WeightSensor::sample_and_feed_filter()", 1)[1].split(
            "float WeightSensor::get_saved_calibration_factor", 1)[0]
        freshness = sensor_h.split("    bool has_recent_sample() const", 1)[1].split(
            "    float get_calibration_factor", 1)[0]
        guard = controller.split("    // Check before any phase can start/restart", 1)[1].split(
            "    if (control_loop_paused_)", 1)[0]
        guard = guard[guard.index("    if (mode =="):]
        code = r'''
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <string>
using std::min;
#define LOG_BLE(...) ((void)0)
uint32_t clock_ms=0;
uint32_t millis(){return clock_ms;}
constexpr int32_t kAdcSaturationMargin=0xFFFF,kAdcMaximumRaw=0xFFFFFF;
struct Filter {void add_sample(int32_t,uint32_t){} int32_t get_smoothed_raw(int){return 42;}};
class WeightSensor {
public:
 bool adc_driver=true,fault=false,waiting=true,read_ok=true;
 int32_t raw=0x800000;
 std::atomic<bool> has_sample_{false},tare_initialized_{false};
 std::atomic<uint32_t> last_sample_ms_{0};
 Filter raw_filter;
 bool doTare=false,tareStatus=false,data_available=false;
 int tareTimes=0,DATA_SET=18;
 int32_t tare_offset=0,current_raw_adc=0;
 float current_weight=0;
 bool has_hardware_fault(){return fault;}
 bool data_waiting_async(){return waiting;}
 bool update_async(){return read_ok;}
 int32_t get_raw_adc_data(){return raw;}
 float raw_to_weight(int32_t){return 1;}
 void update_temperature_if_available(){}
 bool sample_and_feed_filter();
 bool has_recent_sample() const
''' + freshness + r'''
};
bool WeightSensor::sample_and_feed_filter()
''' + sampling + r'''
enum class GrindMode {WEIGHT,TIME,MANUAL};
enum class GrindPhase {PRIME,PREDICTIVE,PULSE_EXECUTE,FINAL_SETTLING,PURGE_CONFIRM,COMPLETED,TIMEOUT};
enum class GrindSessionResult {UNKNOWN,ERROR};
struct Motor {bool running=true;void stop(){running=false;}};
struct Controller {
 GrindMode mode=GrindMode::WEIGHT;
 GrindPhase phase=GrindPhase::PREDICTIVE,timeout_phase{};
 GrindSessionResult last_session_result_=GrindSessionResult::UNKNOWN;
 WeightSensor* weight_sensor;Motor* grinder;
 float final_weight=0;bool phase_work_ran=false;std::string error;
 void set_error_message(const char* text){error=text;}
 void queue_log_message(const char*){}
 struct Loop {float current_weight=12;} loop_data;
 void switch_phase(GrindPhase next,Loop){phase=next;}
 void cycle(){
''' + guard + r'''
  phase_work_ran=true;
 }
};
int main(){
 WeightSensor sensor;assert(!sensor.has_recent_sample());
 assert(sensor.sample_and_feed_filter());assert(sensor.has_recent_sample());
 clock_ms=499;assert(sensor.has_recent_sample());clock_ms=500;assert(!sensor.has_recent_sample());
 // Both supported sample rates remain fresh; failed reads cannot refresh time.
 for(uint32_t interval:{12U,100U}){
  for(int i=0;i<20;++i){clock_ms+=interval;assert(sensor.sample_and_feed_filter());assert(sensor.has_recent_sample());}
 }
 sensor.read_ok=false;clock_ms+=500;assert(!sensor.sample_and_feed_filter());assert(!sensor.has_recent_sample());
 sensor.read_ok=true;sensor.raw=-1;assert(!sensor.sample_and_feed_filter());assert(!sensor.has_recent_sample());
 sensor.raw=0x800000;clock_ms=UINT32_MAX-100;assert(sensor.sample_and_feed_filter());
 clock_ms=398;assert(sensor.has_recent_sample());clock_ms=399;assert(!sensor.has_recent_sample());
 for(auto phase:{GrindPhase::PRIME,GrindPhase::PREDICTIVE,GrindPhase::PULSE_EXECUTE,
                 GrindPhase::FINAL_SETTLING,GrindPhase::PURGE_CONFIRM}){
  Motor motor;Controller c;c.weight_sensor=&sensor;c.grinder=&motor;c.phase=phase;c.cycle();
  assert(!motor.running && !c.phase_work_ran && c.phase==GrindPhase::TIMEOUT);
  assert(c.last_session_result_==GrindSessionResult::ERROR && c.error=="Scale disconnected");
  sensor.sample_and_feed_filter();c.cycle();assert(c.phase==GrindPhase::TIMEOUT && !motor.running);
  clock_ms+=500;
 }
 for(auto mode:{GrindMode::TIME,GrindMode::MANUAL}){
  Motor motor;Controller c;c.weight_sensor=nullptr;c.grinder=&motor;c.mode=mode;c.cycle();
  assert(motor.running && c.phase_work_ran);
 }
 Motor motor;Controller c;c.weight_sensor=&sensor;c.grinder=&motor;c.phase=GrindPhase::COMPLETED;c.cycle();
 assert(c.phase==GrindPhase::COMPLETED);
}
'''
        with tempfile.TemporaryDirectory(prefix="smart-grind-stale-test-") as folder:
            cpp = Path(folder) / "test.cpp"
            binary = Path(folder) / "test"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
