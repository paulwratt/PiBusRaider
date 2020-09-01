
// The following code from
// https://barrgroup.com/Embedded-Systems/How-To/Memory-Test-Suite-C

#include <stdint.h>
#include <stddef.h>

uint8_t memTestDataBus(uint32_t address, BusAccess& busAccess);

uint32_t memTestAddressBus(uint32_t baseAddress, unsigned long nBytes, BusAccess& busAccess);

uint32_t memTestDevice(uint32_t baseAddress, unsigned long nBytes, BusAccess& busAccess);

// int memTestAll(uint32_t baseAddress, uint32_t numBytes, BusAccess& busAccess);

