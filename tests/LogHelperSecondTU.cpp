// Second translation unit for LoggerTest's shared-state check: logging state declared in
// LogHelper.hpp must be one object per program, not one per translation unit.
#include "LOGGER_MACROS.hpp"

const void* logHelperLoggerAddressFromSecondTU() { return &logger::loggerObj; }
const void* logHelperMutexAddressFromSecondTU() { return &logger::raceMtx; }
const void* logHelperOpsAddressFromSecondTU() { return &logger::loggingOps; }
