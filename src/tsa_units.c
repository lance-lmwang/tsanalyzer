#include "tsa_units.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tsa_log.h"

static uint64_t parse_with_unit(const char* s, const char** units, uint64_t* factors, int count, uint64_t def_factor) {
    char* endptr;
    double val = strtod(s, &endptr);
    while (*endptr == ' ' || *endptr == '\t') endptr++;

    if (*endptr == '\0') return (uint64_t)(val * def_factor);

    for (int i = 0; i < count; i++) {
        if (strncasecmp(endptr, units[i], strlen(units[units[i] ? i : 0])) == 0) {
            return (uint64_t)(val * factors[i]);
        }
    }

    tsa_warn("units", "Unknown unit in string: %s", s);
    return (uint64_t)(val * def_factor);
}

uint64_t tsa_units_to_bitrate(const char* s) {
    const char* units[] = {"gbps", "mbps", "kbps", "g", "m", "k", "bps"};
    uint64_t factors[] = {1000000000ULL, 1000000ULL, 1000ULL, 1000000000ULL, 1000000ULL, 1000ULL, 1ULL};
    return parse_with_unit(s, units, factors, 7, 1ULL);
}

uint64_t tsa_units_to_ns(const char* s) {
    const char* units[] = {"h", "min", "ms", "us", "ns", "s"};
    uint64_t factors[] = {3600000000000ULL, 60000000000ULL, 1000000ULL, 1000ULL, 1ULL, 1000000000ULL};
    return parse_with_unit(s, units, factors, 6, 1000000000ULL);
}

uint64_t tsa_units_to_size(const char* s) {
    const char* units[] = {"g", "m", "k", "b"};
    uint64_t factors[] = {1024ULL * 1024 * 1024, 1024ULL * 1024, 1024ULL, 1ULL};
    return parse_with_unit(s, units, factors, 4, 1ULL);
}

bool tsa_units_to_bool(const char* s) {
    if (strcasecmp(s, "on") == 0 || strcasecmp(s, "true") == 0 || strcasecmp(s, "yes") == 0 || strcmp(s, "1") == 0)
        return true;
    return false;
}
