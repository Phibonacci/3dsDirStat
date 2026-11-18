// sizes.c - byte size formatting
#include "sizes.h"

#include <stdio.h>

int format_size(uint64_t bytes, char *out, size_t n)
{
    static const char *units[] = { "B", "KB", "MB", "GB", "TB", "PB" };

    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        unit++;
    }

    if (unit == 0)
        return snprintf(out, n, "%llu B", (unsigned long long)bytes);
    // Three significant digits: 1.23 GB, 12.3 GB, 123 GB.
    if (value < 10.0)  return snprintf(out, n, "%.2f %s", value, units[unit]);
    if (value < 100.0) return snprintf(out, n, "%.1f %s", value, units[unit]);
    return snprintf(out, n, "%.0f %s", value, units[unit]);
}
