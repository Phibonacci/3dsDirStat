// main.c - 3DS homebrew entry point (WinDirStat-like disk usage treemap)
#include <3ds.h>

#include "ui.h"

int main(void)
{
    // Folder recursion limit. Sizes are only exact for folders scanned all the
    // way down (e.g. "Nintendo 3DS" title data sits about 7 levels deep), so this
    // is a safety limit rather than a speed trade-off.
    const int maxScanDepth = 48;

    ui_run(maxScanDepth);
    return 0;
}
