#include "captive_dns.h"

#include <lwip/sockets.h>

#include <cstring>
#include <vector>

#include "../config/constants.h"

namespace {

constexpr uint16_t kDnsPort = 53;
constexpr size_t kMaxQuerySize = 512;
constexpr uint32_t kAnswerTtlSeconds = 60;
constexpr uint32_t kTaskStackSize = 3072;
constexpr UBaseType_t kTaskPriority = 1;

// A DNS header is six 16-bit fields; only the flags and counts are rewritten.
constexpr size_t kHeaderSize = 12;
constexpr size_t kFlagsOffset = 2;
constexpr size_t kQuestionCountOffset = 4;
constexpr size_t kAnswerCountOffset = 6;
constexpr size_t kAuthorityCountOffset = 8;
constexpr size_t kAdditionalCountOffset = 10;
constexpr uint16_t kResponseFlag = 0x8000;
constexpr uint16_t kOpcodeMask = 0x7800;
// The answer record: a name pointer, type, class, TTL, length and the address.
constexpr size_t kAnswerSize = 16;
constexpr uint16_t kTypeA = 1;
constexpr uint16_t kTypeAny = 255;

void write_be16(uint8_t* buffer, uint16_t value) {
    buffer[0] = static_cast<uint8_t>(value >> 8);
    buffer[1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t read_be16(const uint8_t* buffer) {
    return static_cast<uint16_t>((buffer[0] << 8) | buffer[1]);
}

/** Length of the QNAME plus QTYPE and QCLASS, or 0 when malformed. */
size_t question_length(const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const uint8_t label = data[offset];
        if (label == 0) {
            // Root label, then QTYPE and QCLASS.
            return offset + 1 + 4 <= size ? offset + 5 : 0;
        }
        // Compression pointers are not valid in a question section.
        if ((label & 0xC0) != 0) return 0;
        offset += label + 1;
    }
    return 0;
}

/**
 * Build the reply to one query into `reply`, which must hold `size` +
 * kAnswerSize bytes. An A or ANY question resolves to `address`; any other
 * type gets an empty answer, so a phone asking for AAAA does not wait for a
 * timeout. Only the question is echoed: a client's EDNS0 OPT record is
 * dropped and the counts say so, as the Arduino DNSServer did.
 *
 * @return the reply length, or 0 when the packet is not a single standard query.
 */
size_t build_reply(const uint8_t* query, size_t size, uint32_t address, uint8_t* reply) {
    if (size < kHeaderSize) return 0;
    const uint16_t flags = read_be16(query + kFlagsOffset);
    if ((flags & (kResponseFlag | kOpcodeMask)) != 0) return 0;
    if (read_be16(query + kQuestionCountOffset) != 1) return 0;
    const size_t question_size = question_length(query + kHeaderSize, size - kHeaderSize);
    if (question_size == 0) return 0;

    size_t offset = kHeaderSize + question_size;
    std::memcpy(reply, query, offset);
    // Standard response, recursion available, no error.
    write_be16(reply + kFlagsOffset, 0x8180);
    write_be16(reply + kAuthorityCountOffset, 0);
    write_be16(reply + kAdditionalCountOffset, 0);

    const uint16_t qtype = read_be16(query + offset - 4);
    if (qtype != kTypeA && qtype != kTypeAny) {
        write_be16(reply + kAnswerCountOffset, 0);
        return offset;
    }
    write_be16(reply + kAnswerCountOffset, 1);

    // Answer: a pointer back to the question's name, then A/IN, TTL, 4 bytes.
    write_be16(reply + offset, 0xC000 | kHeaderSize);
    offset += 2;
    write_be16(reply + offset, kTypeA);
    offset += 2;
    write_be16(reply + offset, 1);  // class IN
    offset += 2;
    write_be16(reply + offset, static_cast<uint16_t>(kAnswerTtlSeconds >> 16));
    offset += 2;
    write_be16(reply + offset, static_cast<uint16_t>(kAnswerTtlSeconds & 0xFFFF));
    offset += 2;
    write_be16(reply + offset, 4);  // address length
    offset += 2;
    reply[offset++] = static_cast<uint8_t>((address >> 24) & 0xFF);
    reply[offset++] = static_cast<uint8_t>((address >> 16) & 0xFF);
    reply[offset++] = static_cast<uint8_t>((address >> 8) & 0xFF);
    reply[offset++] = static_cast<uint8_t>(address & 0xFF);
    return offset;
}

}  // namespace

bool CaptiveDns::start(uint32_t address) {
    if (task_) return true;

    address_ = address;
    stop_requested_ = false;

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ < 0) {
        LOG_BLE("[DNS] Could not create socket\n");
        return false;
    }

    sockaddr_in bind_address = {};
    bind_address.sin_family = AF_INET;
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_address.sin_port = htons(kDnsPort);
    if (bind(socket_, reinterpret_cast<sockaddr*>(&bind_address), sizeof(bind_address)) < 0) {
        LOG_BLE("[DNS] Could not bind port %u\n", kDnsPort);
        close(socket_);
        socket_ = -1;
        return false;
    }

    // A receive timeout lets the task notice a stop request promptly.
    timeval timeout = {};
    timeout.tv_sec = 1;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (xTaskCreate(task_entry, "captive_dns", kTaskStackSize, this, kTaskPriority, &task_) !=
        pdPASS) {
        LOG_BLE("[DNS] Could not start responder task\n");
        close(socket_);
        socket_ = -1;
        task_ = nullptr;
        return false;
    }

    LOG_BLE("[DNS] Captive portal responder listening\n");
    return true;
}

void CaptiveDns::stop() {
    if (!task_) return;
    stop_requested_ = true;
    // The task closes the socket and clears task_ on its way out.
    while (task_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    LOG_BLE("[DNS] Captive portal responder stopped\n");
}

void CaptiveDns::task_entry(void* context) {
    static_cast<CaptiveDns*>(context)->serve();
}

void CaptiveDns::serve() {
    std::vector<uint8_t> query(kMaxQuerySize);
    std::vector<uint8_t> reply(kMaxQuerySize + kAnswerSize);

    while (!stop_requested_) {
        sockaddr_in from = {};
        socklen_t from_length = sizeof(from);
        const int received = recvfrom(socket_, query.data(), query.size(), 0,
                                     reinterpret_cast<sockaddr*>(&from), &from_length);
        if (received <= 0) continue;

        const size_t length =
            build_reply(query.data(), static_cast<size_t>(received), address_, reply.data());
        if (length == 0) continue;
        sendto(socket_, reply.data(), length, 0, reinterpret_cast<sockaddr*>(&from), from_length);
    }

    close(socket_);
    socket_ = -1;
    TaskHandle_t self = task_;
    task_ = nullptr;
    vTaskDelete(self);
}
