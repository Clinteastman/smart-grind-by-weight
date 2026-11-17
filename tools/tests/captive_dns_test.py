"""Run the production captive-portal DNS reply builder against real query packets."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/network/captive_dns.cpp").read_text()
SETUP_ADDRESS = bytes([4, 4, 4, 1])

HARNESS = r'''
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
using UBaseType_t = unsigned;
''' + SOURCE[SOURCE.index("namespace {"):SOURCE.index("}  // namespace") + len("}  // namespace")] + r'''
int main() {
    // stdin: one query packet; stdout: the reply, empty when there is none.
    std::string query((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> reply(query.size() + kAnswerSize);
    const size_t length = build_reply(reinterpret_cast<const uint8_t*>(query.data()), query.size(),
                                      0x04040401, reply.data());
    std::fwrite(reply.data(), 1, length, stdout);
}
'''


def question(name, qtype):
    labels = b"".join(bytes([len(part)]) + part.encode() for part in name.split("."))
    return labels + b"\0" + struct.pack(">HH", qtype, 1)


def query(name="connectivitycheck.gstatic.com", qtype=1, flags=0x0100, questions=1, edns=True):
    """A query as Android sends it: recursion desired, plus an EDNS0 OPT record."""
    opt = b"\0" + struct.pack(">HHIH", 41, 1232, 0, 0)
    header = struct.pack(">HHHHHH", 0x1234, flags, questions, 0, 0, 1 if edns else 0)
    return header + question(name, qtype) * questions + (opt if edns else b"")


class CaptiveDnsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cpp, cls._binary = Path(cls._tmp.name) / "dns.cpp", Path(cls._tmp.name) / "dns"
        cpp.write_text(HARNESS)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-const-variable", "-Wno-unused-function",
                        str(cpp), "-o", str(cls._binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def reply(self, packet):
        return subprocess.run([str(self._binary)], input=packet, capture_output=True,
                              check=True, timeout=10).stdout

    def counts(self, reply):
        return struct.unpack(">HHHH", reply[4:12])

    def test_edns_address_query_gets_a_well_formed_answer(self):
        packet = query()
        reply = self.reply(packet)
        question_end = 12 + len(question("connectivitycheck.gstatic.com", 1))
        self.assertEqual(reply[:2], packet[:2])
        self.assertEqual(struct.unpack(">H", reply[2:4])[0] & 0x8000, 0x8000)
        # One question and one answer; the client's OPT record is not echoed.
        self.assertEqual(self.counts(reply), (1, 1, 0, 0))
        self.assertEqual(reply[12:question_end], packet[12:question_end])
        self.assertEqual(len(reply), question_end + 16)
        self.assertEqual(reply[question_end:question_end + 6], b"\xc0\x0c\x00\x01\x00\x01")
        self.assertEqual(reply[-4:], SETUP_ADDRESS)

    def test_any_query_resolves_to_the_grinder(self):
        reply = self.reply(query(qtype=255, edns=False))
        self.assertEqual(self.counts(reply), (1, 1, 0, 0))
        self.assertEqual(reply[-4:], SETUP_ADDRESS)

    def test_ipv6_query_gets_an_empty_answer_instead_of_silence(self):
        reply = self.reply(query(qtype=28))
        self.assertEqual(self.counts(reply), (1, 0, 0, 0))
        self.assertEqual(len(reply), 12 + len(question("connectivitycheck.gstatic.com", 28)))

    def test_responses_and_unusual_queries_are_ignored(self):
        for name, packet in [("response", query(flags=0x8180)),
                             ("status opcode", query(flags=0x1000)),
                             ("two questions", query(questions=2)),
                             ("truncated", query()[:20]),
                             ("short header", b"\x12\x34\x01")]:
            with self.subTest(name):
                self.assertEqual(self.reply(packet), b"")


if __name__ == "__main__":
    unittest.main()
