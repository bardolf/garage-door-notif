#include "state.h"

// Jedina definice — proto je tady, ne v hlavickach. Linker by jinak hlasil
// multiple definition pri kazdem TU co includuje state.h.
RTC_DATA_ATTR AppState state;
