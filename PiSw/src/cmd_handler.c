// Command Handler for Bus Raider
// Rob Dobson 2018

#include "cmd_handler.h"
#include "bare_metal_pi_zero.h"
#include "utils.h"
#include "uart.h"
#include "ee_printf.h"

#define MAX_SREC_DATA_LEN 200

// States while decoding a line of Motorola SREC format
typedef enum
{
	CMDHANDLER_STATE_INIT,
	CMDHANDLER_STATE_RECTYPE,
	CMDHANDLER_STATE_LEN,
	CMDHANDLER_STATE_ADDR,
	CMDHANDLER_STATE_DATA,
	CMDHANDLER_STATE_CHECKSUM
} CmdHandler_State;

// Type of Motorola SREC record
typedef enum
{
	CMDHANDLER_RECTYPE_DATA,
	CMDHANDLER_RECTYPE_START,
	CMDHANDLER_RECTYPE_COUNT
} CmdHandler_RecType;

// Destination for data
// - records prefixed with S (i.e. regular SRECs) are considered bootloader records
// - records prefixed with T are considered target program/data
typedef enum
{
	CMDHANDLER_DEST_BOOTLOAD,
	CMDHANDLER_DEST_TARGET
} CmdHandler_Dest;

// State vars
CmdHandler_State __cmdHandler_state = 0;
CmdHandler_Dest __cmdHandler_dest = CMDHANDLER_DEST_TARGET;
CmdHandler_RecType __cmdHandler_recType = CMDHANDLER_RECTYPE_DATA;
uint32_t __cmdHandler_checksum = 0;
uint32_t __cmdHandler_entryAddr = 0;
int __cmdHandler_addrBytes = 0;
int __cmdHandler_fieldCtr = 0;
int __cmdHandler_byteCtr = 0;
int __cmdHandler_len = 0;
int __cmdHandler_dataLen = 0;
int __cmdHandler_byteIdx = 0;
uint32_t __cmdHandler_addr = 0;
uint8_t __cmdHandler_byte = 0;
uint8_t __cmdHandler_data[MAX_SREC_DATA_LEN];
int __cmdHandler_lastCharInvalid = 0;
int __cmdHandler_debugChCount = 0;
int __cmdHandler_errCode = 0;

// Handler for received data
TCmdHandlerDataBlockCallback* __cmdHandler_pDataBlockCallback;

// Init the destinations for SREC and TREC records
void cmdHandler_init(TCmdHandlerDataBlockCallback* pDataBlockCallback)
{
	__cmdHandler_pDataBlockCallback = pDataBlockCallback;
	__cmdHandler_state = CMDHANDLER_STATE_INIT;
	__cmdHandler_debugChCount = 0;
	__cmdHandler_errCode = CMDHANDLER_RET_OK;
}

void cmdHandler_clearError()
{
	__cmdHandler_errCode = CMDHANDLER_RET_OK;
	__cmdHandler_debugChCount = 0;
}

int cmdHandler_getError()
{
	return __cmdHandler_errCode;
}

// Convert char to nybble
uint8_t chToNybble(int ch)
{
	if ((ch < '0') || ((ch > '9') && (ch < 'A')) || ((ch >'F') && (ch < 'a')) || (ch >'f'))
	{
		__cmdHandler_errCode = CMDHANDLER_RET_INVALID_NYBBLE;
#ifdef DEBUG_SREC_RX
		ee_printf("Nybble invalid %02x count %d\n", ch, __cmdHandler_debugChCount);
#endif
	}
    if (ch > '9')
    	ch -= 7;
    return ch & 0xF;
}

// Handle a single char
CmdHandler_Ret cmdHandler_handle_char(int ch)
{
	__cmdHandler_debugChCount++;
	// Handle based on state
	switch(__cmdHandler_state)
	{
		case CMDHANDLER_STATE_INIT:
		{
			if ((ch == 'S') || (ch == 'T'))
			{
				__cmdHandler_checksum = 0;
				__cmdHandler_state = CMDHANDLER_STATE_RECTYPE;
				__cmdHandler_dest = (ch == 'S') ? CMDHANDLER_DEST_BOOTLOAD : CMDHANDLER_DEST_TARGET;
				__cmdHandler_lastCharInvalid = 0;
			}
			// else if ((ch == 'g') || (ch == 'G'))
			// {
			// 	// Go to start address
			// 	__cmdHandler_lastCharInvalid = 0;
			// 	utils_goto(__cmdHandler_entryAddr);
			// }
			else
			{
				#ifdef DEBUG_SREC_RX
					if ((ch != '\n') && (ch != '\r'))
						if (!__cmdHandler_lastCharInvalid)
						{
							ee_printf("I...%02x, count %d\n",ch,__cmdHandler_debugChCount);
							__cmdHandler_lastCharInvalid = 1;
						}
				#endif
				return CMDHANDLER_RET_IGNORED;
			}
			break;
		}
		case CMDHANDLER_STATE_RECTYPE:
		{
			__cmdHandler_fieldCtr = 0;
			__cmdHandler_len = 0;
			__cmdHandler_byteCtr = 0;
			// Get number of bytes in address - S1/S5/S9 == 2bytes, S2/S6/S8 == 3bytes, S3/S7 == 4 bytes
			__cmdHandler_addrBytes = (ch & 0x03) + 1;
			if ((ch & 0x0f) == 8)
				__cmdHandler_addrBytes = 3;
			else if ((ch & 0x0f) == 9)
				__cmdHandler_addrBytes = 2;
			// Handle record type
			switch(ch)
			{
				case '1':
				case '2':
				case '3': { __cmdHandler_recType = CMDHANDLER_RECTYPE_DATA; __cmdHandler_state = CMDHANDLER_STATE_LEN; break; }
				case '5':
				case '6': { __cmdHandler_recType = CMDHANDLER_RECTYPE_COUNT; __cmdHandler_state = CMDHANDLER_STATE_LEN; break; }
				case '7':
				case '8':
				case '9': { __cmdHandler_recType = CMDHANDLER_RECTYPE_START; __cmdHandler_state = CMDHANDLER_STATE_LEN; break; }
				default:
					{ 
						#ifdef DEBUG_SREC_RX
							ee_printf("RECTYPE INVALID %d", ch);
						#endif
						__cmdHandler_state = CMDHANDLER_STATE_INIT;
						__cmdHandler_errCode = CMDHANDLER_RET_INVALID_RECTYPE;
						return CMDHANDLER_RET_INVALID_RECTYPE;
					}
			}
			break;
		}
		case CMDHANDLER_STATE_LEN:
		{
			// Build length from nybbles
			__cmdHandler_len = (__cmdHandler_len << 4) + chToNybble(ch);
			__cmdHandler_fieldCtr++;
			// Check if done
			if (__cmdHandler_fieldCtr == 2)
			{
				// Now ready for address
				__cmdHandler_checksum += __cmdHandler_len;
				__cmdHandler_fieldCtr = 0;
				__cmdHandler_addr = 0;
				__cmdHandler_byte = 0;
				__cmdHandler_state = CMDHANDLER_STATE_ADDR;
			}
			break;
		}
		case CMDHANDLER_STATE_ADDR:
		{
			// Build address from bytes
			__cmdHandler_byte = (__cmdHandler_byte << 4) + chToNybble(ch);
			__cmdHandler_fieldCtr++;
			// Address and Checksum
			if (__cmdHandler_fieldCtr % 2 == 0)
			{
				__cmdHandler_addr = (__cmdHandler_addr << 8) + __cmdHandler_byte;
				__cmdHandler_checksum += __cmdHandler_byte & 0xff;
				__cmdHandler_byte = 0;
			}
			// Done?
			if (__cmdHandler_fieldCtr == __cmdHandler_addrBytes * 2)
			{
				// Check if entry point record
				if (__cmdHandler_recType == CMDHANDLER_RECTYPE_START)
				{
					// Set entry point
					__cmdHandler_entryAddr = __cmdHandler_addr;
				}
				// Check if data record
				__cmdHandler_dataLen = 0;
				if (__cmdHandler_recType == CMDHANDLER_RECTYPE_DATA)
					__cmdHandler_dataLen = __cmdHandler_len - (__cmdHandler_fieldCtr/2) - 1;
				// Check for data following
				if (__cmdHandler_dataLen > 0)
				{
					__cmdHandler_state = CMDHANDLER_STATE_DATA;
				}
				else
				{
					// Ready for checksum
					__cmdHandler_state = CMDHANDLER_STATE_CHECKSUM;
				}
				// New field starting
				__cmdHandler_byte = 0;
				__cmdHandler_byteIdx = 0;
				__cmdHandler_fieldCtr = 0;
			}
			break;
		}
		case CMDHANDLER_STATE_DATA:
		{
			// Build address from bytes
			__cmdHandler_byte = (__cmdHandler_byte << 4) + chToNybble(ch);
			__cmdHandler_fieldCtr++;
			// Check if byte complete
			if (__cmdHandler_fieldCtr % 2 == 0)
			{
				// Checksum
				__cmdHandler_checksum += __cmdHandler_byte & 0xff;
				// Store to appropriate place
				if (__cmdHandler_byteIdx < MAX_SREC_DATA_LEN)
				{
					__cmdHandler_data[__cmdHandler_byteIdx] = __cmdHandler_byte;
				}
				// if (__cmdHandler_dest == CMDHANDLER_DEST_BOOTLOAD)
				// {
				// 	if (__cmdHandler_addr + __cmdHandler_byteIdx < __cmdHandler_srec_maxlen)
				// 	{
				// 		__pCmdHandler_srec_base[__cmdHandler_addr + __cmdHandler_byteIdx] = __cmdHandler_byte;
				// 	}
				// }
				// else
				// {
				// 	if (__cmdHandler_addr + __cmdHandler_byteIdx < __cmdHandler_trec_maxlen)
				// 	{
				// 		__pCmdHandler_trec_base[__cmdHandler_addr + __cmdHandler_byteIdx] = __cmdHandler_byte;
				// 	}

				// }
				// Next byte
				__cmdHandler_byteIdx++;
				__cmdHandler_byte = 0;
				// Check for end
				if (__cmdHandler_byteIdx >= __cmdHandler_dataLen)
				{
					// Check for checksum
					__cmdHandler_state = CMDHANDLER_STATE_CHECKSUM;
					__cmdHandler_fieldCtr = 0;
					__cmdHandler_byte = 0;
				}
			}
			break;
		}
		case CMDHANDLER_STATE_CHECKSUM:
		{
			// Build checksum from nybbles
			__cmdHandler_byte = (__cmdHandler_byte << 4) + chToNybble(ch);
			__cmdHandler_fieldCtr++;
			// Check if done
			if (__cmdHandler_fieldCtr == 2)
			{
				// Go back to initial state
				__cmdHandler_state = CMDHANDLER_STATE_INIT;
				// Check if checksum correct
				if (__cmdHandler_byte != ((~__cmdHandler_checksum) & 0xff))
				{
					return CMDHANDLER_RET_CHECKSUM_ERROR;
				}
				else
				{
					// Callback on new data
					if (__cmdHandler_recType == CMDHANDLER_RECTYPE_DATA)
						if (__cmdHandler_pDataBlockCallback)
							__cmdHandler_pDataBlockCallback(__cmdHandler_addr, __cmdHandler_data, __cmdHandler_dataLen, __cmdHandler_dest);
					return CMDHANDLER_RET_LINE_COMPLETE;
				}
			}
			break;
		}
	}
	return __cmdHandler_errCode;
}