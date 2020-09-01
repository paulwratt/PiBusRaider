// Bus Raider
// Rob Dobson 2019

#include "BusController.h"
#include "../System/PiWiring.h"
#include "../System/lowlib.h"
#include "../System/ee_sprintf.h"
#include "../System/logging.h"
#include "../System/rdutils.h"
#include "../TargetBus/TargetTracker.h"
#include "../Machines/McManager.h"
#include "../Hardware/HwManager.h"
#include "../TargetBus/BusAccess.h"
#include "../CommandInterface/CommandHandler.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Variables
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Module name
static const char FromBusController[] = "BusController";

// This instance
BusController* BusController::_pThisInstance = NULL;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Constructor
BusController::BusController(CommandHandler& commandHandler, McManager& mcManager) :
        _commandHandler(commandHandler), _mcManager(mcManager)
{
    _pThisInstance = this;
    // Sockets
    _busSocketId = -1;
    _commsSocketId = -1;
    // Synchronous memory access
    _memAccessPending = false;
    _memAccessWrite = false;
    _memAccessDataLen = 0;
    _memAccessAddr = 0;
    _memAccessIo = false;
    _memAccessRdWrErrCount = 0;
    _memAccessRdWrTest = false;
    _stepCompletionPending = false;
    _targetTrackerResetPending = false;
}

void BusController::init()
{
    // Connect to the bus socket
    BusAccess& busAccess = _mcManager.getBusAccess();
    if (_busSocketId < 0)
        _busSocketId = busAccess.busSocketAdd( 
            true,
            BusController::handleWaitInterruptStatic,
            BusController::busActionCompleteStatic,
            false,
            false,
            // Reset
            false,
            0,
            // NMI
            false,
            0,
            // IRQ
            false,
            0,
            false,
            BR_BUS_ACTION_GENERAL,
            false,
            this);

    // Connect to the comms socket
    if (_commsSocketId < 0)
        _commsSocketId = _commandHandler.commsSocketAdd(this, true, BusController::handleRxMsgStatic, NULL, NULL);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Received message handler
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusController::handleRxMsgStatic(void* pObject, const char* pCmdJson, 
                const uint8_t* pParams, int paramsLen,
                char* pRespJson, int maxRespLen)
{
    if (!pObject)
        return false;
    return ((BusController*)pObject)->handleRxMsg(pCmdJson, pParams, paramsLen, pRespJson, maxRespLen);
}

bool BusController::handleRxMsg(const char* pCmdJson, 
                [[maybe_unused]]const uint8_t* pParams, [[maybe_unused]]int paramsLen,
                [[maybe_unused]]char* pRespJson, [[maybe_unused]]int maxRespLen)
{
    // Get the command string from JSON
    static const int MAX_CMD_NAME_STR = 50;
    char cmdName[MAX_CMD_NAME_STR+1];
    if (!jsonGetValueForKey("cmdName", pCmdJson, cmdName, MAX_CMD_NAME_STR))
        return false;

    // Check for memory/IO read
    BusAccess& busAccess = _mcManager.getBusAccess();
    TargetTracker& targetTracker = _mcManager.getTargetTracker();
    if (strcasecmp(cmdName, "Rd") == 0)
    {
        // LogWrite(FromBusController, LOG_DEBUG, "Rd %s", pCmdJson);

        // Get params
        uint32_t addr = 0;
        uint32_t dataLen = 0;
        uint32_t isIo = false;
        if (!getArg("addr", 1, pCmdJson, addr))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        if (!getArg("len", 2, pCmdJson, dataLen))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        if (!getArg("isIo", 3, pCmdJson, isIo))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        // Get data using bus access
        if ((dataLen <= 0) || (dataLen > MAX_MEM_BLOCK_READ_WRITE))
        {
            strlcpy(pRespJson, "\"err\":\"LenTooLong\"", maxRespLen);
            return true;
        }
        // Synchronous memory read
        uint8_t pData[MAX_MEM_BLOCK_READ_WRITE];
        BR_RETURN_TYPE rslt = BusController::blockAccessSync(addr, pData, dataLen, isIo, false);
        if (rslt != BR_OK)
        {
            strlcpy(pRespJson, "\"err\":\"fail\"", maxRespLen);
            return true;
        }
        // Format data to send
        static const int MAX_MEM_READ_RESP = MAX_MEM_BLOCK_READ_WRITE*2+100; 
        char jsonResp[MAX_MEM_READ_RESP];
        ee_sprintf(jsonResp, "\"err\":\"ok\",\"len\":%d,\"addr\":\"0x%04x\",\"isIo\":%d,\"data\":\"", dataLen, addr, isIo);
        int pos = strlen(jsonResp);
        for (uint32_t i = 0; i < dataLen; i++)
        {
            ee_sprintf(jsonResp+pos, "%02x", pData[i]);
            pos+=2;
        }
        strlcat(jsonResp, "\"", MAX_MEM_READ_RESP);
        strlcpy(pRespJson, jsonResp, maxRespLen);

        // LogWrite(FromBusController, LOG_DEBUG, "Rd result ok, lastByte 0x%c%c", jsonResp[strlen(jsonResp)-3], jsonResp[strlen(jsonResp)-2]);

        return true;
    }
    else if (strcasecmp(cmdName, "Wr") == 0)
    {
        // LogWrite(FromBusController, LOG_DEBUG, "Wr %s", pCmdJson);
        
        // Get params
        uint32_t addr = 0;
        uint32_t dataLen = 0;
        uint32_t isIo = false;
        if (!getArg("addr", 1, pCmdJson, addr))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        if (!getArg("len", 2, pCmdJson, dataLen))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        if (!getArg("isIo", 3, pCmdJson, isIo))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        // Get data using bus access
        if ((dataLen <= 0) || (dataLen > MAX_MEM_BLOCK_READ_WRITE) || (dataLen > (uint32_t)paramsLen))
        {
            strlcpy(pRespJson, "\"err\":\"LenTooLong\"", maxRespLen);
            return true;
        }
        // Synchronous memory write
        BR_RETURN_TYPE rslt = BusController::blockAccessSync(addr, const_cast<uint8_t*>(pParams), dataLen, isIo, true);
        if (rslt != BR_OK)
        {
            strlcpy(pRespJson, "\"err\":\"fail\"", maxRespLen);
            return true;
        }
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "testRdWr") == 0)
    {
        //         if (_pThisInstance->_memAccessReadPending)
        // {
        //     _pThisInstance->_memAccessRdWrPending = false;
        //     for (int i = 0; i < 1000; i++)
        //     {
        //         uint8_t writeBuf[MAX_MEM_BLOCK_READ_WRITE];
        //         for (int j = 0; j < MAX_MEM_BLOCK_READ_WRITE; j++)
        //             writeBuf[j] = rand() & 0xff;
        //         busAccess.blockWrite(0x3c00, writeBuf, MAX_MEM_BLOCK_READ_WRITE, false, false);

        //         uint8_t readBuf[MAX_MEM_BLOCK_READ_WRITE];
        //         busAccess.blockRead(0x3c00, readBuf, MAX_MEM_BLOCK_READ_WRITE, false, false);

        //         if (memcmp(writeBuf, readBuf, MAX_MEM_BLOCK_READ_WRITE) != 0)
        //         {
        //             if (_pThisInstance->_memAccessRdWrErrCount == 0)
        //             {
        //                 int errPos = 0;
        //                 for (int k = 0; k < MAX_MEM_BLOCK_READ_WRITE; k++)
        //                 {
        //                     if (writeBuf[k] != readBuf[k])
        //                     {
        //                         errPos = k;
        //                         break;
        //                     }
        //                 }
        //                 // digitalWrite(8, 1);
        //                 // microsDelay(100);
        //                 // digitalWrite(8, 0);
        //                 ee_sprintf(_pThisInstance->_memAccessRdWrErrStr, "POS %d WR %02x %02x %02x %02x RD %02x %02x %02x %02x", 
        //                             errPos,
        //                             writeBuf[errPos], writeBuf[errPos+1], writeBuf[errPos+2], writeBuf[errPos+3],
        //                             readBuf[errPos+0], readBuf[errPos+1], readBuf[errPos+2], readBuf[errPos+3]);
        //             }
        //             _pThisInstance->_memAccessRdWrErrCount++;
        //         }
        //     }
        // }

        // // Request the bus
        // _pThisInstance->_memAccessRdWrPending = true;
        // _pThisInstance->_memAccessDataLen = MAX_MEM_BLOCK_READ_WRITE;
        // _pThisInstance->_memAccessAddr = 0xc000;
        // _pThisInstance->_memAccessIo = false;
        // _pThisInstance->_memAccessRdWrErrCount = 0;
        // _pThisInstance->_memAccessRdWrErrStr[0] = 0;
        // busAccess.targetReqBus(_busSocketId, BR_BUS_ACTION_GENERAL);
        // // Now enter a loop to wait for the bus action to complete
        // static const uint32_t MAX_WAIT_FOR_BUS_ACCESS_US = 50000;
        // uint32_t busAccessReqStart = micros();
        // while(!isTimeout(micros(), busAccessReqStart, MAX_WAIT_FOR_BUS_ACCESS_US))
        // {
        //     // Finished?
        //     if (!_pThisInstance->_memAccessRdWrPending)
        //                     break;
        //     // Service the bus access
        //     busAccess.service();
        //                 }
        // // Check if completed ok
        // if (_pThisInstance->_memAccessRdWrPending)
        // {
        //     _pThisInstance->_memAccessRdWrPending = false;
        //     ee_sprintf(pRespJson, "\"err\":\"fail\"");
        //     // digitalWrite(8,1);
        //             // microsDelay(100);
        //     // digitalWrite(8,0);
        //     return true;
        //     }
        // if (maxRespLen > 200)
        // {
        //     ee_sprintf(pRespJson, "\"err\":\"%s\",\"errs\":%d,\"errStr\":\"%s\"",
        //             _pThisInstance->_memAccessRdWrErrCount > 0 ? "fail" : "ok", 
        //             _pThisInstance->_memAccessRdWrErrCount, 
        //             _pThisInstance->_memAccessRdWrErrStr);
        //     LogWrite(FromBusController, LOG_DEBUG, pRespJson);
        // }
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusControlOn") == 0)
    {
        busAccess.rawBusControlEnable(true);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusControlOff") == 0)
    {
        busAccess.rawBusControlEnable(false);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusWaitClear") == 0)
    {
        busAccess.rawBusControlClearWait();
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusWaitDisable") == 0)
    {
        busAccess.rawBusControlWaitDisable();
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusClockEnable") == 0)
    {
        busAccess.rawBusControlClockEnable(true);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusClockDisable") == 0)
    {
        busAccess.rawBusControlClockEnable(false);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusTake") == 0)
    {
        busAccess.rawBusControlTakeBus();
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusRelease") == 0)
    {
        busAccess.rawBusControlReleaseBus();
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusSetAddress") == 0)
    {
        // LogWrite(FromBusController, LOG_DEBUG, "rawBusAddress %s", pCmdJson);
        // Get address
        uint32_t addr = 0;
        if (!getArg("addr", 1, pCmdJson, addr))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        busAccess.rawBusControlSetAddress(addr);
        // LogWrite(FromBusController, LOG_DEBUG, "rawBusAddress addr %04x", addr);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusGetData") == 0)
    {
        busAccess.rawBusControlReadPIB();
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusSetData") == 0)
    {
        // Get data
        uint32_t data = 0;
        if (!getArg("data", 1, pCmdJson, data))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        LogWrite(FromBusController, LOG_DEBUG, "SET data %02x", data);
        busAccess.rawBusControlSetData(data);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusWritePIB") == 0)
    {
        // Get data
        uint32_t data = 0;
        if (!getArg("data", 1, pCmdJson, data))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        busAccess.rawBusControlWritePIB(data);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusSetPin") == 0)
    {
        // Get pin and value
        uint32_t pin = 0;
        if (!getArg("pin", 1, pCmdJson, pin, NULL, 0, true))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        uint32_t value = 0;
        if (!getArg("value", 2, pCmdJson, value))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        busAccess.rawBusControlSetPin(pin, value);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusGetPin") == 0)
    {
        // Get pin and value
        uint32_t pin = 0;
        if (!getArg("pin", 1, pCmdJson, pin, NULL, 0, true))
        {
            strlcpy(pRespJson, "\"err\":\"InvArgs\"", maxRespLen);
            return true;
        }
        busAccess.rawBusControlGetPin(pin);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusGetLines") == 0)
    {
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusSetLine") == 0)
    {
        // Handle it
        busLineHandler(pCmdJson);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusControlMuxSet") == 0)
    {
        // Handle it
        muxLineHandler(pCmdJson);
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "rawBusMuxClear") == 0)
    {
        busAccess.rawBusControlMuxClear();
        busLinesRead(pRespJson, maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "busStatus") == 0)
    {
        // Get bus status
        BusAccessStatusInfo statusInfo;
        busAccess.getStatus(statusInfo);
        strlcpy(pRespJson, statusInfo.getJson(), maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "busStatusClear") == 0)
    {
        // Clear bus status
        busAccess.clearStatus();
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "busInit") == 0)
    {
        // Get bus status
        busAccess.busAccessReset();
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "targetReset") == 0)
    {
        LogWrite(FromBusController, LOG_DEBUG, "Target Reset");
        // Get bus status
        busAccess.targetReqReset(_busSocketId);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "targetBusReq") == 0)
    {
        LogWrite(FromBusController, LOG_DEBUG, "Target Bus Req");
        // Get bus status
        busAccess.targetReqBus(_busSocketId, BR_BUS_ACTION_GENERAL);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "targetBusRel") == 0)
    {
        LogWrite(FromBusController, LOG_DEBUG, "Target Bus Req");
        // Get bus status
        busAccess.targetReqBus(_busSocketId, BR_BUS_ACTION_GENERAL);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "clockHzGet") == 0)
    {
        // Get clock
        uint32_t actualHz = busAccess.clockCurFreqHz();
        ee_sprintf(pRespJson, "\"err\":\"ok\",\"clockHz\":\"%d\"", actualHz);
        // LogWrite(FromBusController, LOG_DEBUG, "clockHzGet %s", pRespJson);
        return true;
    }
    else if (strcasecmp(cmdName, "clockHzSet") == 0)
    {
        // LogWrite(FromBusController, LOG_DEBUG, "clockHzSet cmd %s params %s %02x %02x %02x %02x", pCmdJson, pParams, pParams[0], pParams[1], pParams[2], pParams[3]);
        // Get clock rate required (may be in cmdJson or paramsJson)
        static const int MAX_CMD_PARAM_STR = 50;
        char paramVal[MAX_CMD_PARAM_STR+1];
        if (!jsonGetValueForKey("clockHz", pCmdJson, paramVal, MAX_CMD_PARAM_STR))
            if (!jsonGetValueForKey("clockHz", (const char*)pParams, paramVal, MAX_CMD_PARAM_STR))
                return false;
        uint32_t clockRateHz = strtoul(paramVal, NULL, 10);
        // LogWrite(FromBusController, LOG_DEBUG, "clockHzSet %d", clockRateHz);

        // Set clock
        busAccess.clockSetFreqHz(clockRateHz);

        // Get clock
        uint32_t actualHz = busAccess.clockCurFreqHz();
        ee_sprintf(pRespJson, "\"err\":\"ok\",\"clockHz\":\"%d\"", actualHz);
        return true;
    }
    else if (strcasecmp(cmdName, "waitHoldOn") == 0)
    {
        // Hold
        busAccess.waitHold(_busSocketId, true);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "waitHoldOff") == 0)
    {
        // Hold
        busAccess.waitHold(_busSocketId, false);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "waitRelease") == 0)
    {
        // Release
        busAccess.waitRelease();
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "waitMemoryOn") == 0)
    {
        // Wait on memory
        busAccess.waitOnMemory(_busSocketId, true);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "waitMemoryOff") == 0)
    {
        // Wait on memory
        busAccess.waitOnMemory(_busSocketId, false);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "waitIOOn") == 0)
    {
        // Wait on IO
        busAccess.waitOnIO(_busSocketId, true);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "waitIOOff") == 0)
    {
        // Wait on IO
        busAccess.waitOnIO(_busSocketId, false);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "targetTrackerOn") == 0)
    {
        // Get clock rate required
        static const int MAX_CMD_PARAM_STR = 50;
        char paramVal[MAX_CMD_PARAM_STR+1];
        bool reset = true;
        if (jsonGetValueForKey("reset", pCmdJson, paramVal, MAX_CMD_PARAM_STR))
        {
            reset = strtol(paramVal, NULL, 10) != 0;
        }
        // Turn target tracker on
        targetTracker.enable(true, false);
        if (reset)
        {
            targetTracker.targetReset();
            _targetTrackerResetPending = true;
        }
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "targetTrackerOff") == 0)
    {
        // Turn target tracker on
        targetTracker.enable(false, false);
        LogWrite(FromBusController, LOG_DEBUG, "TargettrackerOff");
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "stepInto") == 0)
    {
        // Turn target tracker on
        targetTracker.stepInto();
        // LogWrite(FromBusController, LOG_DEBUG, "TargettrackerStep %s", pCmdJson);
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        _stepCompletionPending = true;
        return true;
    }
    else if (strcasecmp(cmdName, "stepRun") == 0)
    {
        // Turn target tracker on
        targetTracker.stepRun();
        LogWrite(FromBusController, LOG_DEBUG, "TargettrackerRun");
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "getRegs") == 0)
    {
        char regsStr[200];
        targetTracker.getRegsFormatted(regsStr, sizeof(regsStr));
        strlcpy(pRespJson, "\"err\":\"ok\",\"regs\":\"", maxRespLen);
        strlcat(pRespJson, regsStr, maxRespLen);
        strlcat(pRespJson, "\"", maxRespLen);
        return true;
    }
    else if (strcasecmp(cmdName, "waitCycleUs") == 0)
    {
        // Get params
        static const int MAX_CMD_PARAM_STR = 50;
        char paramVal[MAX_CMD_PARAM_STR+1];
        if (!jsonGetValueForKey("cycleUs", pCmdJson, paramVal, MAX_CMD_PARAM_STR))
            return false;
        // Turn target tracker on
        busAccess.waitSetCycleUs(strtoul(paramVal, NULL, 10));
        // LogWrite(FromBusController, LOG_DEBUG, "Wait cycle set to %d", strtoul(paramVal, NULL, 10));
        strlcpy(pRespJson, "\"err\":\"ok\"", maxRespLen);
        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers and handlers
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool BusController::getArg(const char* argName, int argNum, const char* pCmdJson, uint32_t& value, 
            char* pOutStr, uint32_t maxOutStrLen, bool forceDecimal)
{
    // Get from request string if possible
    static const int MAX_CMD_REQ_STR = 200;
    char reqStr[MAX_CMD_REQ_STR+1];
    if (jsonGetValueForKey("reqStr", pCmdJson, reqStr, MAX_CMD_REQ_STR))
    {
        const char* pStr = reqStr;
        // See if there are enough slashes
        for (int i = 0; i < argNum+2; i++)
        {
            pStr = strstr(pStr, "/");
            if (!pStr)
                break;
            pStr += 1;
        }
        // LogWrite(FromBusController, LOG_DEBUG, "argName %s, argNum %d, reqStr %s, strPtr %u slashPtr %u slashStr %s", 
        //             argName, argNum, reqStr, reqStr, pStr, pStr ? pStr : "NULL");
        if (pStr)
        {
            // Extract hex number from here
            if (strlen(pStr) > 0)
            {
                if (pOutStr)
                    strlcpy(pOutStr, pStr, maxOutStrLen);
                if (forceDecimal)
                    value = strtoul(pStr, NULL, 10);
                else
                    value = strtoul(pStr, NULL, 16);
                return true;
            }
        }
    }
    // Get from params
    static const int MAX_CMD_PARAM_STR = 50;
    char paramVal[MAX_CMD_PARAM_STR+1];
    char argNameDec[MAX_CMD_PARAM_STR+1];
    strlcpy(argNameDec, argName, MAX_CMD_PARAM_STR);
    strlcat(argNameDec, "Dec", MAX_CMD_PARAM_STR);
    if (jsonGetValueForKey(argName, pCmdJson, paramVal, MAX_CMD_PARAM_STR))
    {
        if (pOutStr)
            strlcpy(pOutStr, paramVal, maxOutStrLen);
        if (forceDecimal)
            value = strtoul(paramVal, NULL, 10);
        else
            value = strtoul(paramVal, NULL, 16);
        return true;
    }
    if (jsonGetValueForKey(argNameDec, pCmdJson, paramVal, MAX_CMD_PARAM_STR))
    {
        if (pOutStr)
            strlcpy(pOutStr, paramVal, maxOutStrLen);
        value = strtoul(paramVal, NULL, 10);
        return true;
    }
    return false;
}

bool BusController::busLineHandler(const char* pCmdJson)
{
    static const int MAX_CMD_PARAM_STR = 50;
    uint32_t lineNum_DoNotUse;
    char lineName[MAX_CMD_PARAM_STR];
    if (!getArg("line", 1, pCmdJson, lineNum_DoNotUse, lineName, MAX_CMD_PARAM_STR))
    {
        return false;
    }
    char* pStrEnd = strstr(lineName, "/");
    if (pStrEnd)
        *pStrEnd = 0;
    uint32_t busValue = 0;
    if (!getArg("value", 2, pCmdJson, busValue))
    {
        return false;
    }
    BusAccess& busAccess = _mcManager.getBusAccess();
    LogWrite(FromBusController, LOG_DEBUG, "bussetline %s %d", lineName, busValue);
    if (strcasecmp(lineName, "WR") == 0)
    {
        busAccess.rawBusControlSetPin(BR_WR_BAR, busValue);
    }
    else if (strcasecmp(lineName, "RD") == 0)
    {
        busAccess.rawBusControlSetPin(BR_RD_BAR, busValue);
    }
    else if (strcasecmp(lineName, "MREQ") == 0)
    {
        busAccess.rawBusControlSetPin(BR_MREQ_BAR, busValue);
    }
    else if (strcasecmp(lineName, "IORQ") == 0)
    {
        busAccess.rawBusControlSetPin(BR_IORQ_BAR, busValue);
    }
    else if (strcasecmp(lineName, "PAGE") == 0)
    {
        busAccess.rawBusControlSetPin(BR_IORQ_BAR, busValue);
    }
    else if (strcasecmp(lineName, "DATA_DIR_IN") == 0)
    {
        busAccess.rawBusControlSetPin(BR_DATA_DIR_IN, busValue);
    }
    else if (strcasecmp(lineName, "IORQ_WAIT_EN") == 0)
    {
        busAccess.rawBusControlSetPin(BR_IORQ_WAIT_EN, busValue);
    }
    else if (strcasecmp(lineName, "MREQ_WAIT_EN") == 0)
    {
        busAccess.rawBusControlSetPin(BR_MREQ_WAIT_EN, busValue);
    }
    else if (strcasecmp(lineName, "HADDR_CK") == 0)
    {
        busAccess.rawBusControlSetPin(BR_HADDR_CK, busValue);
    }
    else if (strcasecmp(lineName, "PUSH_ADDR") == 0)
    {
        if (busAccess.getHwVersion() == 17)
            busAccess.rawBusControlSetPin(BR_V17_PUSH_ADDR_BAR, busValue);
    }
    else if (strcasecmp(lineName, "CLOCK") == 0)
    {
        busAccess.rawBusControlSetPin(BR_CLOCK_PIN, busValue);
    }
    else if (strcasecmp(lineName, "M1") == 0)
    {
        busAccess.rawBusControlSetPin(BR_V20_M1_BAR, busValue);
    }
    return true;
}

bool BusController::muxLineHandler(const char* pCmdJson)
{
    static const int MAX_CMD_PARAM_STR = 50;
    uint32_t lineNum_DoNotUse;
    char lineName[MAX_CMD_PARAM_STR];
    if (!getArg("line", 1, pCmdJson, lineNum_DoNotUse, lineName, MAX_CMD_PARAM_STR))
    {
        return false;
    }
    char* pStrEnd = strstr(lineName, "/");
    if (pStrEnd)
        *pStrEnd = 0;
    LogWrite(FromBusController, LOG_DEBUG, "muxSet %s", lineName);
    BusAccess& busAccess = _mcManager.getBusAccess();
    if (strcasecmp(lineName, "LADDR_CLK") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_LADDR_CLK);
    }
    else if (strcasecmp(lineName, "LADDR_CLR_BAR") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_LADDR_CLR_BAR_LOW);
    }
    else if (strcasecmp(lineName, "DATA_OE_BAR") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_DATA_OE_BAR_LOW);
    }
    else if (strcasecmp(lineName, "RESET") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_RESET_Z80_BAR_LOW);
    }
    else if (strcasecmp(lineName, "IRQ") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_IRQ_BAR_LOW);
    }
    else if (strcasecmp(lineName, "NMI") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_NMI_BAR_LOW);
    }
    else if (strcasecmp(lineName, "LADDR_OE_BAR") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_LADDR_OE_BAR);
    }
    else if (strcasecmp(lineName, "HADDR_OE_BAR") == 0)
    {
        busAccess.rawBusControlMuxSet(BR_MUX_HADDR_OE_BAR);
    }
    return true;
}

void BusController::busLinesRead(char* pRespJson, [[maybe_unused]]int maxRespLen)
{
    // Read bus lines
    BusAccess& busAccess = _mcManager.getBusAccess();
    uint32_t lines = busAccess.rawBusControlReadRaw();
    bool m1Val = lines & BR_V20_M1_BAR_MASK; 
    if (busAccess.getHwVersion() == 17)
        m1Val = (lines & BR_V17_M1_PIB_BAR_MASK);
    ee_sprintf(pRespJson, "\"err\":\"ok\",\"raw\":\"%02x\",\"pib\":\"%02x\",\"ctrl\":\"%c%c%c%c%c\"",
            busAccess.rawBusControlReadRaw(), 
            (busAccess.rawBusControlReadRaw() >> BR_DATA_BUS) & 0xff,
            (lines & BR_MREQ_BAR_MASK) ? 'M' : '.', 
            (lines & BR_IORQ_BAR_MASK) ? 'I' : '.', 
            (lines & BR_RD_BAR_MASK) ? 'R' : '.', 
            (lines & BR_WR_BAR_MASK) ? 'W' : '.',
            m1Val ? '1' : '.'
            );
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks/Hooks
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BusController::busActionCompleteStatic(void* pObject, BR_BUS_ACTION actionType, BR_BUS_ACTION_REASON reason)
{
    if (!pObject)
        return;
    ((BusController*)pObject)->busActionComplete(actionType, reason);
}

void BusController::busActionComplete([[maybe_unused]]BR_BUS_ACTION actionType, [[maybe_unused]] BR_BUS_ACTION_REASON reason)
{
    HwManager& hwManager = _mcManager.getHwManager();
    if ((actionType == BR_BUS_ACTION_BUSRQ) && _memAccessPending)
    {
        if (!_memAccessWrite)
        {
            // Read
            hwManager.blockRead(_memAccessAddr, _memAccessDataBuf, 
                        _memAccessDataLen, false, _memAccessIo, false);
        }
        else
        {
            // Write
            hwManager.blockWrite(_memAccessAddr, _memAccessDataBuf, 
                        _memAccessDataLen, false, _memAccessIo, false);
        }
        _memAccessPending = false;
    }
}

void BusController::handleWaitInterruptStatic(void* pObject, uint32_t addr, uint32_t data, 
        uint32_t flags, uint32_t& retVal)
{
    if (!pObject)
        return;
    ((BusController*)pObject)->handleWaitInterrupt(addr, data, flags, retVal);
}

void BusController::handleWaitInterrupt([[maybe_unused]] uint32_t addr, [[maybe_unused]] uint32_t data, 
        [[maybe_unused]] uint32_t flags, [[maybe_unused]] uint32_t& retVal)
{
}

void BusController::service()
{
    // Check for targettracker enable message response
    TargetTracker& targetTracker = _mcManager.getTargetTracker();
    if (_targetTrackerResetPending && targetTracker.isStepPaused())
    {
        _commandHandler.sendUnnumberedMsg("targetTrackerOnDone", "\"err\":\"ok\"");
        _targetTrackerResetPending = false;
    }
    // Check for step completion message response
    if (_stepCompletionPending && targetTracker.isStepPaused())
    {
        _commandHandler.sendUnnumberedMsg("stepIntoDone", "\"err\":\"ok\"");
        _stepCompletionPending = false;
    }
}

BR_RETURN_TYPE BusController::blockAccessSync(uint32_t addr, uint8_t* pData, uint32_t len, bool iorq, bool write)
{
    // Request the bus
    _memAccessPending = true;
    _memAccessWrite = write;
    _memAccessDataLen = len;
    if (_memAccessDataLen > MAX_MEM_BLOCK_READ_WRITE)
        _memAccessDataLen = MAX_MEM_BLOCK_READ_WRITE;
    _memAccessAddr = addr;
    _memAccessIo = iorq;
    if (write)
        memcopyfast(_memAccessDataBuf, pData, len);
    BusAccess& busAccess = _mcManager.getBusAccess();
    busAccess.targetReqBus(_busSocketId, BR_BUS_ACTION_GENERAL);
    // Now enter a loop to wait for the bus action to complete
    static const uint32_t MAX_WAIT_FOR_BUS_ACCESS_US = 50000;
    uint32_t busAccessReqStart = micros();

    while(!isTimeout(micros(), busAccessReqStart, MAX_WAIT_FOR_BUS_ACCESS_US))
    {
        // Finished?
        if (!_memAccessPending)
            break;
        // Service the bus access - the actual read/write operation occurs in a
        // callback during this function call
        busAccess.service();
    }
    // Check if completed ok
    if (_memAccessPending)
    {
        _memAccessPending = false;
        return BR_NO_BUS_ACK;
    }
    if (!write)
        memcopyfast(pData, _memAccessDataBuf, len);
    return BR_OK;
}