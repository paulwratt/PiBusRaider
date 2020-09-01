// Bus Raider
// Rob Dobson 2019

#pragma once

#include <circle/logger.h>

// Severity
#define LOG_PANIC   0
#define LOG_ERROR	1
#define LOG_WARNING	2
#define LOG_NOTICE	3
#define LOG_DEBUG	4
#define LOG_VERBOSE 4

#ifdef __cplusplus
extern "C" {
#endif

#define LogWrite(tag, severity, format, ... ) CLogger::Get()->Write(tag, (TLogSeverity)severity, format, ##__VA_ARGS__ )
// // extern void ee_dump_mem(unsigned char* start_addr, unsigned char* end_addr);

// extern void LogSetLevel(int severity);
// extern void LogWrite(const char* pSource, // short name of module
//     unsigned Severity, // see above
//     const char* pMessage, ...); // uses printf format options

// typedef void LogOutStrFnType(const char* pStr);
// extern void LogSetOutFn(LogOutStrFnType* pOutFn);

// typedef void LogOutMsgFnType(const char* pSeverity, const char* pSource, const char* pMsg);
// extern void LogSetOutMsgFn(LogOutMsgFnType* pOutFn);

// extern void LogDumpMemory(const char* fromSource, int logLevel, unsigned char* start_addr, unsigned char* end_addr);

#ifdef __cplusplus
}
#endif
