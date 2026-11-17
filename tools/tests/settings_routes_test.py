"""Execute production route registration/callbacks against HTTP transport doubles."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from settings_persistence_test import method

ROOT = Path(__file__).resolve().parents[2]


class SettingsRoutesTest(unittest.TestCase):
    def test_routes(self):
        source = (ROOT / "src/network/device_api.cpp").read_text()
        routes = method(source, "void DeviceApi::configure_settings_routes(")
        # The response helpers the routes rely on run as written in production.
        http_source = (ROOT / "src/network/http_support.cpp").read_text()
        helpers = "\n".join(method(http_source, signature) for signature in (
            "std::string json_escape(",
            "esp_err_t send_json(",
            "esp_err_t send_error(",
            "Handler same_origin(",
        ))
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
using httpd_handle_t = void*;
enum httpd_method_t {HTTP_GET = 1, HTTP_POST = 3};
struct httpd_req_t {
 std::map<std::string,std::string> query;  // Already URL-decoded, as http::query_param returns.
 bool origin = true;
 int status = 0; std::string type, body;
 std::map<std::string,std::string> headers;
};
esp_err_t httpd_resp_set_hdr(httpd_req_t* r,const char* k,const char* v){r->headers[k]=v;return ESP_OK;}
namespace http {
using Handler = std::function<esp_err_t(httpd_req_t*)>;
}
struct Server {
 std::map<std::pair<std::string,int>,http::Handler> routes;
 void call(const char* uri,int method,httpd_req_t& r){
  const esp_err_t result=routes.at({uri,method})(&r);assert(result==ESP_OK && r.status);
 }
};
namespace http {
bool route(httpd_handle_t server,const char* uri,httpd_method_t method,Handler handler){
 if(!server||!uri) return false;
 const bool added=static_cast<Server*>(server)->routes.emplace(std::make_pair(uri,method),handler).second;
 assert(added);return added;
}
esp_err_t send(httpd_req_t* r,int status,const char* type,const std::string& body){
 assert(!r->status);r->status=status;r->type=type;r->body=body;return ESP_OK;  // One response per request.
}
bool origin_allowed(httpd_req_t* r){return r->origin;}
''' + helpers + r'''
bool query_param(httpd_req_t* r,const char* key,std::string& value){
 if(!r->query.count(key)) return false;
 value=r->query.at(key);return true;
}
}
struct DeviceApi {
 httpd_handle_t server_ = nullptr;
 std::map<uint32_t,const char*> results;
 int settings_queued=0,profiles_queued=0;
 const char* settings_result(uint32_t id){return results.count(id)?results.at(id):"unknown";}
 esp_err_t queue_profile_selection(httpd_req_t* r){profiles_queued++;return http::send_json(r,202,"{\"accepted\":true}");}
 esp_err_t queue_settings_update(httpd_req_t* r){settings_queued++;return http::send_json(r,202,"{\"accepted\":true}");}
 std::string settings_json(){return "{\"current_profile\":1}";}
 void configure_settings_routes();
};
''' + routes + r'''
int main(){
 Server server;DeviceApi api;api.server_=&server;api.configure_settings_routes();
 assert(server.routes.size()==4);
 for(const char* bad:{"","0","-1","+1","1x"," 1","1.0","4294967296","10000000000"}){
  httpd_req_t r;r.query["id"]=bad;
  server.call("/api/v1/settings/result",HTTP_GET,r);
  assert(r.status==400 && r.type=="application/json");
 }
 httpd_req_t missing;server.call("/api/v1/settings/result",HTTP_GET,missing);
 assert(missing.status==400);
 for(const char* state:{"pending","saved","failed","busy"}){
  api.results[UINT32_MAX]=state;
  httpd_req_t r;r.query["id"]="4294967295";
  server.call("/api/v1/settings/result",HTTP_GET,r);
  assert(r.status==200 && r.headers.at("Cache-Control")=="no-store");
  assert(r.body==std::string("{\"request_id\":4294967295,\"status\":\"")+state+"\"}");
 }
 api.results.clear();httpd_req_t expired;expired.query["id"]="7";
 server.call("/api/v1/settings/result",HTTP_GET,expired);
 assert(expired.status==404 && expired.headers.at("Cache-Control")=="no-store");
 assert(expired.body=="{\"request_id\":7,\"status\":\"unknown\"}");
 httpd_req_t get;server.call("/api/v1/settings",HTTP_GET,get);
 assert(get.status==200 && get.headers.at("Cache-Control")=="no-store");
 for(const char* path:{"/api/v1/settings","/api/v1/profile"}){
  httpd_req_t denied;denied.origin=false;server.call(path,HTTP_POST,denied);
  assert(denied.status==403 && !api.settings_queued && !api.profiles_queued);
 }
 httpd_req_t settings;server.call("/api/v1/settings",HTTP_POST,settings);
 assert(api.settings_queued==1);
 httpd_req_t profile;server.call("/api/v1/profile",HTTP_POST,profile);assert(api.profiles_queued==1);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "routes.cpp", Path(folder) / "routes"
            cpp.write_text(code)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                            "-fsanitize=address,undefined", str(cpp), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    unittest.main()
