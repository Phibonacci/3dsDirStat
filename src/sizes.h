// sizes.h - byte size formatting (platform independent)
#ifndef SIZES_H
#define SIZES_H

#include <stdint.h>
#include <stddef.h>

// Format a byte count into a compact human readable string, e.g. "1.23 GB".
// Writes into `out` (length `n`). Returns the number of characters written.
int format_size(uint64_t bytes, char *out, size_t n);

#endif // SIZES_H
