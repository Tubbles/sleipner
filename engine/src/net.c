#include "net.h"

#include "strv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DECIMAL_BASE 10
#define IPV4_OCTET_MAX 255
#define IPV4_OCTET_MAX_DIGITS 3

/* Parse a single decimal octet (0-255) from a Strv. Rejects an empty
 * view, non-digit characters, and values that overflow a byte. */
static bool parse_octet(Strv text, uint8_t *out)
{
    if (text.len == 0 || text.len > IPV4_OCTET_MAX_DIGITS) {
        return false;
    }
    uint32_t value = 0;
    for (size_t index = 0; index < text.len; index++) {
        char digit = text.ptr[index];
        if (digit < '0' || digit > '9') {
            return false;
        }
        value = (value * DECIMAL_BASE) + (uint32_t)(digit - '0');
        if (value > IPV4_OCTET_MAX) {
            return false;
        }
    }
    *out = (uint8_t)value;
    return true;
}

bool net_addr_from_ipv4_string(const char *ipv4_string, uint16_t port, NetAddr *out)
{
    Strv remaining = strv_from_cstr(ipv4_string);
    uint8_t octets[4] = {0};
    for (int index = 0; index < 3; index++) {
        Strv piece = strv_split(&remaining, '.');
        if (!parse_octet(piece, &octets[index])) {
            return false;
        }
    }
    if (!parse_octet(remaining, &octets[3])) {
        return false;
    }

    uint32_t host = 0;
    for (int index = 0; index < 4; index++) {
        host = (host << 8) | octets[index];
    }
    *out = net_addr_make(host, port);
    return true;
}
