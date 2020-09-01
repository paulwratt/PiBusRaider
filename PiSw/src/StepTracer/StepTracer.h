// Bus Raider
// Rob Dobson 2019

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include "../System/RingBufferPosn.h"
#include "../TargetBus/TargetRegisters.h"
#include "../TargetBus/BusAccess.h"
#include "../CommandInterface/CommandHandler.h"
#include "libz80/z80.h"

// #define STEP_VAL_WITHOUT_HW_MANAGER 1
#ifndef STEP_VAL_WITHOUT_HW_MANAGER
#include "../Hardware/HwManager.h"
#endif

class McManager;

// Stats
class StepTracerStats
{
public:
    StepTracerStats()
    {
        clear();
    }
    void clear()
    {
        isrCalls = 0;
        errors = 0;
        instructionCount = 0;
    }
    uint32_t isrCalls;
    uint32_t errors;
    uint32_t instructionCount;
};

// Exceptions
class StepTracerException
{
public:
    uint32_t addr;
    uint32_t expectedAddr;
    uint32_t dataFromZ80;
    uint32_t expectedData;
    uint32_t dataToZ80;
    uint32_t flags;
    uint32_t expectedFlags;
    uint32_t stepCount;
};

// Trace
class StepTracerTrace
{
public:
    uint32_t addr;
    uint32_t busData;
    uint32_t returnedData;
    uint32_t flags;
    uint32_t traceCount;
};

// Cycle info
class StepTracerCycle
{
public:
    uint32_t addr;
    uint32_t data;
    uint32_t flags;
};

// Tracer
class StepTracer
{
public:
    StepTracer(CommandHandler& _commandHandler, McManager& mcManager);
    void init();

    // Control
    void start(bool logging, bool recordAll, bool compareToEmulated, bool primeFromMem);
    void stop(bool logging);
    void stopAll(bool logging);
    
    // Service
    void service();

    // Stats
    StepTracerStats& getStats();
    
private:

    // Z80 CPU context
    Z80Context _cpu_z80;

    // Flags
    bool _logging;
    bool _recordAll;
    bool _compareToEmulated;
    bool _recordIsHoldingTarget;

    // Memory system
#ifdef STEP_VAL_WITHOUT_HW_MANAGER
    uint8_t* _pTracerMemory;
    uint32_t _tracerMemoryLen;
#endif

    // Singleton instance
    static StepTracer* _pThisInstance;

    // Command Handler
    CommandHandler& _commandHandler;

    // Machine manager
    McManager& _mcManager;

    // Bus socket we're attached to
    int _busSocketId;

    // Comms socket we're attached to
    int _commsSocketId;

    // Handle messages (telling us to start/stop)
    static bool handleRxMsgStatic(void* pObject, const char* pCmdJson, const uint8_t* pParams, int paramsLen,
                    char* pRespJson, int maxRespLen);
    bool handleRxMsg(const char* pCmdJson, const uint8_t* pParams, int paramsLen,
                    char* pRespJson, int maxRespLen);

    // Get status
    void getStatus(char* pRespJson, int maxRespLen, const char* statusIdxStr);

    // Get trace
    void getTraceLong(char* pRespJson, int maxRespLen);
    void getTraceBin();

    // Reset complete callback
    static void busActionCompleteStatic(void* pObject, BR_BUS_ACTION actionType, BR_BUS_ACTION_REASON reason);
    void busActionComplete(BR_BUS_ACTION actionType, BR_BUS_ACTION_REASON reason);
    void resetComplete();

    // Wait interrupt handler
    static void handleWaitInterruptStatic(void* pObject, uint32_t addr, uint32_t data, 
            uint32_t flags, uint32_t& retVal);
    void handleWaitInterrupt(uint32_t addr, uint32_t data, 
            uint32_t flags, uint32_t& retVal);

    // Memory and IO functions
    static byte mem_read(int param, ushort address, void* pTracer);
    static void mem_write(int param, ushort address, byte data, void* pTracer);
    static byte io_read(int param, ushort address, void* pTracer);
    static void io_write(int param, ushort address, byte data, void* pTracer);

    // Record step information for instruction
    static const int MAX_STEP_CYCLES_FOR_INSTR = 10;
    StepTracerCycle _stepCycles[MAX_STEP_CYCLES_FOR_INSTR];
    int _stepCycleCount;
    int _stepCyclePos;

    // Exception list
    static const int NUM_DEBUG_VALS = 20;
    volatile StepTracerException _exceptions[NUM_DEBUG_VALS];
    RingBufferPosn _exceptionsPosn;

    // Execution trace list
    static const int NUM_TRACE_VALS = 1000;
    static const int MIN_SPACES_IN_TRACES = 50;
    volatile StepTracerTrace _traces[NUM_TRACE_VALS];
    RingBufferPosn _tracesPosn;

    // Execution trace format
    #pragma pack(push, 1)
    struct TraceBinHeaderFormat
    {
        uint32_t idx;
    };
    struct TraceBinElemFormat
    {
        uint16_t addr;
        uint8_t busData;
        uint8_t retData;
        uint8_t flags;
    };
    #pragma pack(pop)
    static const int MAX_TRACE_MSG_BUF_ELEMS_MAX = 2000;

    // Tx chars available in tx buffer for bin frame transmission
    static const int MIN_TX_AVAILABLE_FOR_BIN_FRAME = 16000;

    // Active
    bool _isActive;

    // Stats
    StepTracerStats _stats;

    // Service count
    int _serviceCount;

    // Prime from memory pending
    bool _primeFromMemPending;
};
