// dllmain.cpp
#include "gapless_fix.h"

CAppModule _Module;

DECLARE_COMPONENT_VERSION(
    "Gapless Fix",
    "1.0",
    "Utilities -> Gapless Fix\n"
    "Adds silence to complete CD frames (588 samples).\n"
    "Output: WAV files, tags and album art preserved.\n"
    "Settings: Preferences -> Advanced -> Tools -> Gapless Fix."
)

VALIDATE_COMPONENT_FILENAME("foo_gapless.dll")