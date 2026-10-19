#include "plugin/asr.h"
// A C translation unit must be able to consume the public ABI.
_Static_assert(sizeof(((plugin_asr_config*)0)->abi_version) == 4, "ABI version width");
int asr_c_header_test(void) { return PLUGIN_ASR_ABI_VERSION; }
