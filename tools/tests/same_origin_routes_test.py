"""Cross-origin writes are refused by http::same_origin, and every mutating route uses it."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from controller_serialization_test import function

ROOT = Path(__file__).resolve().parents[2]
HTTP_SUPPORT = (ROOT / "src/network/http_support.cpp").read_text()

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
struct httpd_req_t { bool cross_origin = false; int status = 0; std::string body; };
esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return ESP_OK; }
namespace http {
using Handler = std::function<esp_err_t(httpd_req_t*)>;
esp_err_t send(httpd_req_t* r, int status, const char*, const std::string& body) {
    assert(!r->status); r->status = status; r->body = body; return ESP_OK;
}
bool origin_allowed(httpd_req_t* r) { return !r->cross_origin; }
''' + "\n".join(function(HTTP_SUPPORT, signature) for signature in (
    "std::string json_escape(",
    "esp_err_t send_json(",
    "esp_err_t send_error(",
    "Handler same_origin(",
)) + r'''
}

int main() {
    int calls = 0;
    const http::Handler guarded = http::same_origin([&calls](httpd_req_t* r) {
        ++calls;
        return http::send(r, 200, "text/plain", "ok");
    });

    // A page on another site is answered 403 and never reaches the handler.
    httpd_req_t foreign;
    foreign.cross_origin = true;
    assert(guarded(&foreign) == ESP_OK && foreign.status == 403 && calls == 0);
    assert(foreign.body == "{\"error\":\"Request origin is not allowed\"}");

    // The grinder's own page, and tools that send no Origin, go through.
    httpd_req_t own;
    assert(guarded(&own) == ESP_OK && own.status == 200 && own.body == "ok" && calls == 1);
}
'''

ROUTE = re.compile(r'http::route\(\s*server_,\s*"([^"]+)",\s*(HTTP_\w+),\s*([^\n]*)')

# Every route that changes device state. A new mutating route must be added
# here and wrapped in http::same_origin, so a hostile page on the LAN cannot
# submit a form POST to it: multipart and form posts need no CORS preflight.
MUTATING_ROUTES = {
    ("/api/v1/ota", "HTTP_POST"),
    ("/api/v1/ota/prepare", "HTTP_POST"),
    ("/api/v1/ota/github", "HTTP_POST"),
    ("/api/v1/screensaver/image", "HTTP_POST"),
    ("/api/v1/screensaver/image", "HTTP_DELETE"),
    ("/api/v1/profile", "HTTP_POST"),
    ("/api/v1/settings", "HTTP_POST"),
}


class SameOriginRoutesTest(unittest.TestCase):
    def test_cross_origin_requests_are_refused_before_the_handler(self):
        with tempfile.TemporaryDirectory() as folder:
            cpp, binary = Path(folder) / "origin.cpp", Path(folder) / "origin"
            cpp.write_text(HARNESS)
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_every_mutating_route_is_same_origin(self):
        found = {}
        for path in ("src/network/device_web_server.cpp", "src/network/device_api.cpp"):
            for uri, method, handler in ROUTE.findall((ROOT / path).read_text()):
                if method != "HTTP_GET":
                    found[(uri, method)] = handler.strip()
        self.assertEqual(set(found), MUTATING_ROUTES)
        for route, handler in sorted(found.items()):
            self.assertTrue(handler.startswith("http::same_origin("),
                            f"{route} is not origin-checked: {handler}")


if __name__ == "__main__":
    unittest.main()
