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
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
struct String : std::string {
 using std::string::string;
 bool isEmpty() const {return empty();}
};
struct Param {String text; String value() const {return text;}};
struct AsyncWebServerResponse {
 int code; std::string type, body;
 std::map<std::string,std::string> headers;
 void addHeader(const char* k,const char* v){headers[k]=v;}
};
struct AsyncWebServerRequest {
 std::map<std::string,Param> params;
 AsyncWebServerResponse response{};
 bool origin=true;
 bool hasParam(const char* k) const{return params.count(k);}
 Param* getParam(const char* k){return &params.at(k);}
 AsyncWebServerResponse* beginResponse(int code,const char* type,const char* body){
  response={code,type,body,{}};return &response;
 }
 void send(AsyncWebServerResponse* r){assert(r==&response);}
 void send(int code,const char* type,const char* body){beginResponse(code,type,body);}
};
constexpr int HTTP_GET=0,HTTP_POST=1;
struct AsyncURIMatcher {static const char* exact(const char* v){return v;}};
struct AsyncWebServer {
 std::map<std::pair<std::string,int>,std::function<void(AsyncWebServerRequest*)>> routes;
 void on(const char* path,int verb,std::function<void(AsyncWebServerRequest*)> f){
  assert(routes.emplace(std::make_pair(path,verb),f).second);
 }
 void call(const char* path,int verb,AsyncWebServerRequest& r){routes.at({path,verb})(&r);}
};
struct DeviceApi {
 std::map<uint32_t,const char*> results;
 int settings_queued=0,profiles_queued=0;
 const char* settings_result(uint32_t id){return results.count(id)?results.at(id):"unknown";}
 bool websocket_origin_allowed(AsyncWebServerRequest* r){return r->origin;}
 void queue_profile_selection(AsyncWebServerRequest*){profiles_queued++;}
 void queue_settings_update(AsyncWebServerRequest*){settings_queued++;}
 const char* settings_json(){return "{\"current_profile\":1}";}
 void configure_settings_routes(AsyncWebServer*);
};
''' + routes + r'''
int main(){
 DeviceApi api;AsyncWebServer server;api.configure_settings_routes(&server);
 assert(server.routes.size()==4);
 for(const char* bad:{"","0","-1","+1","1x"," 1","1.0","4294967296","10000000000"}){
  AsyncWebServerRequest r;r.params["id"]={String(bad)};
  server.call("/api/v1/settings/result",HTTP_GET,r);
  assert(r.response.code==400 && r.response.type=="application/json");
 }
 AsyncWebServerRequest missing;server.call("/api/v1/settings/result",HTTP_GET,missing);
 assert(missing.response.code==400);
 for(const char* state:{"pending","saved","failed","busy"}){
  api.results[UINT32_MAX]=state;
  AsyncWebServerRequest r;r.params["id"]={"4294967295"};
  server.call("/api/v1/settings/result",HTTP_GET,r);
  assert(r.response.code==200 && r.response.headers.at("Cache-Control")=="no-store");
  assert(r.response.body==std::string("{\"request_id\":4294967295,\"status\":\"")+state+"\"}");
 }
 api.results.clear();AsyncWebServerRequest expired;expired.params["id"]={"7"};
 server.call("/api/v1/settings/result",HTTP_GET,expired);
 assert(expired.response.code==404 && expired.response.headers.at("Cache-Control")=="no-store");
 assert(expired.response.body=="{\"request_id\":7,\"status\":\"unknown\"}");
 AsyncWebServerRequest get;server.call("/api/v1/settings",HTTP_GET,get);
 assert(get.response.code==200 && get.response.headers.at("Cache-Control")=="no-store");
 for(const char* path:{"/api/v1/settings","/api/v1/profile"}){
  AsyncWebServerRequest denied;denied.origin=false;server.call(path,HTTP_POST,denied);
  assert(denied.response.code==403 && !api.settings_queued && !api.profiles_queued);
 }
 AsyncWebServerRequest accepted;server.call("/api/v1/settings",HTTP_POST,accepted);
 assert(api.settings_queued==1);
 server.call("/api/v1/profile",HTTP_POST,accepted);assert(api.profiles_queued==1);
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
