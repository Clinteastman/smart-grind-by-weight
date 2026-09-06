"""Run production freshness/sampling and stop-guard code with a simulated ADC."""
from pathlib import Path
import ast
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StaleScaleTest(unittest.TestCase):
    def test_history_consumer_labels(self):
        expected = {0: "COMPLETE", 1: "TIMEOUT", 2: "OVERSHOOT",
                    3: "MAX_PULSES", 4: "SCALE_ERROR", 255: "UNKNOWN"}
        for name in ("grind_report.py", "grind_report_orig.py"):
            tree = ast.parse((ROOT / "tools/streamlit-reports" / name).read_text())
            assignment = next(node for node in tree.body if isinstance(node, ast.Assign)
                              and any(isinstance(target, ast.Name) and
                                      target.id == "TERMINATION_REASON_MAP"
                                      for target in node.targets))
            self.assertEqual(ast.literal_eval(assignment.value), expected, name)

        manager = (ROOT / "src/bluetooth/manager.cpp").read_text()
        labels = "const char* term_names[]" + manager.split("const char* term_names[]", 1)[1].split(
            "\n\n", 1)[0]
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
const char* label(uint8_t reason) {
    struct { uint8_t termination_reason; } session{reason};
''' + labels + r'''
    return term_name;
}
int main() {
    const char* expected[] = {"COMPLETED", "TIMEOUT", "OVERSHOOT", "MAX_PULSES", "SCALE_ERROR"};
    for (unsigned reason = 0; reason <= 255; ++reason)
        assert(std::strcmp(label(reason), reason < 5 ? expected[reason] : "UNKNOWN") == 0);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "labels.cpp"
            binary = Path(folder) / "labels"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_saved_scale_reason_is_not_timeout(self):
        controller = (ROOT / "src/controllers/grind_controller.cpp").read_text()
        logger = (ROOT / "src/logging/grind_logging.cpp").read_text()
        header = (ROOT / "src/logging/grind_logging.h").read_text()
        terminal = controller.split("bool GrindController::queue_terminal_session() {", 1)[1].split(
            "\n}\n", 1)[0]
        reason_enum = "enum class GrindTerminationReason" + header.split(
            "enum class GrindTerminationReason", 1)[1].split("};", 1)[0] + "};"
        classify = "GrindTerminationReason classify_termination_reason" + logger.split(
            "GrindTerminationReason classify_termination_reason", 1)[1].split(
            "\n}\n", 1)[0] + "\n}\n"
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include "src/controllers/grind_session_result.h"
''' + reason_enum + classify + r'''
struct FlashOpRequest {
 enum { END_GRIND_SESSION }; int operation_type;
 char result_string[32]; float final_weight; uint8_t pulse_count;
};
struct Logger { bool is_logging_active() { return true; } } grind_logger;
enum class GrindPhase { COMPLETED, TIMEOUT };
struct Controller {
 GrindSessionResult last_session_result_;
 GrindPhase phase=GrindPhase::TIMEOUT;
 bool accept=false;
 bool session_end_flash_queued=false;
 float final_weight=12; uint8_t pulse_attempts=2;
 FlashOpRequest stored{};
 bool queue_flash_operation(const FlashOpRequest& request) { stored=request; return accept; }
 bool terminal() {
''' + terminal + r'''
 }
};
int main() {
 Controller scale{GrindSessionResult::SCALE_ERROR}; scale.terminal();
 Controller timeout{GrindSessionResult::TIMEOUT}; timeout.terminal();
 assert(std::strcmp(scale.stored.result_string,"SCALE_ERROR")==0);
 assert(std::strcmp(timeout.stored.result_string,"TIMEOUT")==0);
 assert(classify_termination_reason(scale.stored.result_string)==GrindTerminationReason::SCALE_ERROR);
 assert(classify_termination_reason(timeout.stored.result_string)==GrindTerminationReason::TIMEOUT);
 assert(static_cast<uint8_t>(GrindTerminationReason::SCALE_ERROR)==4);
 assert(!is_completed_grind_result(GrindSessionResult::SCALE_ERROR));
 assert(!scale.session_end_flash_queued);
 scale.accept=true;
 assert(scale.terminal());
 assert(std::strcmp(scale.stored.result_string,"SCALE_ERROR")==0);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "reason.cpp"
            binary = Path(folder) / "reason"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(ROOT),
                            str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

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
        diagnostics = sensor_cpp.split("uint32_t WeightSensor::get_adc_headroom_counts() const", 1)[1].split(
            "// Primary weight readings", 1)[0]
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
 std::atomic<int32_t> diagnostic_raw_adc_{-1};
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
 uint32_t get_adc_headroom_counts() const;
 bool is_adc_near_saturation() const;
 bool has_recent_sample() const
''' + freshness + r'''
};
bool WeightSensor::sample_and_feed_filter()
''' + sampling + '\nuint32_t WeightSensor::get_adc_headroom_counts() const' + diagnostics + r'''
enum class GrindMode {WEIGHT,TIME,MANUAL};
enum class GrindPhase {PRIME,PREDICTIVE,PULSE_EXECUTE,FINAL_SETTLING,PURGE_CONFIRM,COMPLETED,TIMEOUT};
#include "src/controllers/grind_session_result.h"
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
 assert(!sensor.is_adc_near_saturation());
 assert(sensor.sample_and_feed_filter());assert(sensor.has_recent_sample());
 clock_ms=499;assert(sensor.has_recent_sample());clock_ms=500;assert(!sensor.has_recent_sample());
 // Both supported sample rates remain fresh; failed reads cannot refresh time.
 for(uint32_t interval:{12U,100U}){
  for(int i=0;i<20;++i){clock_ms+=interval;assert(sensor.sample_and_feed_filter());assert(sensor.has_recent_sample());}
 }
 sensor.read_ok=false;clock_ms+=500;assert(!sensor.sample_and_feed_filter());assert(!sensor.has_recent_sample());
 sensor.read_ok=true;sensor.raw=-1;assert(!sensor.sample_and_feed_filter());assert(!sensor.has_recent_sample());
 // Rail faults must neither refresh time nor progress a tare.
 sensor.doTare=true;
 for(int32_t raw:{0,0xFFFF,0xFFFFFF-0xFFFF,0xFFFFFF}){
  sensor.raw=raw;clock_ms+=100;
  const auto before=sensor.last_sample_ms_.load();
  assert(!sensor.sample_and_feed_filter());
  assert(sensor.is_adc_near_saturation());
  assert(sensor.get_adc_headroom_counts()==uint32_t(min(raw,0xFFFFFF-raw)));
  assert(sensor.last_sample_ms_.load()==before && sensor.tareTimes==0);
  assert(!sensor.has_recent_sample());
 }
 sensor.doTare=false;
 for(int32_t raw:{0x10000,0xFFFFFF-0x10000}){
  sensor.raw=raw;assert(sensor.sample_and_feed_filter());assert(sensor.has_recent_sample());
  assert(!sensor.is_adc_near_saturation());
 }
 sensor.raw=0x800000;clock_ms=UINT32_MAX-100;assert(sensor.sample_and_feed_filter());
 clock_ms=398;assert(sensor.has_recent_sample());clock_ms=399;assert(!sensor.has_recent_sample());
 for(auto phase:{GrindPhase::PRIME,GrindPhase::PREDICTIVE,GrindPhase::PULSE_EXECUTE,
                 GrindPhase::FINAL_SETTLING,GrindPhase::PURGE_CONFIRM}){
  Motor motor;Controller c;c.weight_sensor=&sensor;c.grinder=&motor;c.phase=phase;c.cycle();
  assert(!motor.running && !c.phase_work_ran && c.phase==GrindPhase::TIMEOUT);
  assert(c.last_session_result_==GrindSessionResult::SCALE_ERROR && c.error=="Scale disconnected");
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
                            "-I", str(ROOT), str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
