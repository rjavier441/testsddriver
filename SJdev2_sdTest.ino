//#include <stdio.h>
#include <SPI.h>
#include <string.h> // strncmp()

#define CMDMAX 50
#define BUS_TIMEOUT 50

// TODO: Ensure you know which voltage level the SPI card reader's chip operates on BEFORE connecting it!
//       Answer: 3.3V (is a safe bet?)

// Arduino UNO has these pins hard-locked to these functions:
//cosnt int pinSCK = 13;
//const int pinMISO = 12;
//const int pinMOSI = 11;
const int pinCS = 10;

typedef struct {
    uint8_t response[6];
    uint8_t length;
} SdResponse;

typedef enum {
    R1,
    R1b,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7
} SdResponseType;

typedef enum {
    SDSC,
    SDHC,
    SDXC
} SdType;

typedef enum {
    IS_DUAL_VOLTAGE   = 7,   // OCR bit 7: Supports Low Voltage Ranges AND High Voltage Ranges
    IS_2_7V_TO_2_8V   = 15,  // OCR bit 15: Supports 2.7V-2.8V
    IS_2_8V_TO_2_9V   = 16,  // OCR bit 16: Supports 2.8V-2.9V
    IS_2_9V_TO_3_0V   = 17,  // OCR bit 17: Supports 2.9V-3.0V
    IS_3_0V_TO_3_1V   = 18,  // OCR bit 18: Supports 3.0V-3.1V
    IS_3_1V_TO_3_2V   = 19,  // OCR bit 19: Supports 3.1V-3.2V
    IS_3_2V_TO_3_3V   = 20,  // OCR bit 20: Supports 3.2V-3.3V
    IS_3_3V_TO_3_4V   = 21,  // OCR bit 21: Supports 3.3V-3.4V
    IS_3_4V_TO_3_5V   = 22,  // OCR bit 22: Supports 3.4V-3.5V
    IS_3_5V_TO_3_6V   = 23,  // OCR bit 23: Supports 3.5V-3.6V
    IS_1_8V_OK        = 24,  // OCR bit 24: Switching to 1.8V is accepted (S18A)
    IS_UHS2           = 29,  // OCR bit 29: Card is UHS-II Compatible
    CCS               = 30   // OCR bit 30: Card Capacity (Info Availability) Status
} SdOcr;

typedef enum {    // NOTE: OR'ing of b0100_0000 is meant to conform with the SD protocol (i.e. first bits must be b01)
    GARBAGE     = 0xFF,         // Garbage command; instructs sendCmd() to send out the data and even the checksum as 0xFF
    RESET       = 0x40 | 0x00,  // 0: reset the sd card (force it to go to the idle state)
    INIT        = 0x40 | 0x01,  // 1: starts an initiation of the card
    GETOP       = 0x40 | 0x08,  // 8: request the sd card's support of the provided host's voltage ranges
    GETSTATUS   = 0x04 | 13,    // 13: get status register
    ACBEGIN     = 0x40 | 0x37,  // 55: the leading command for/signals the start of an application-specific command
    ACINIT      = 0x40 | 0x29,  // 41: newer version of CMD1: starts an initiation of the card
    GETOCR      = 0x40 | 0x3A,  // 58: request data from the operational conditions register
    CHGBL       = 0x40 | 0x10,  // 16: change block length (only effective in SDSC cards)
    READ        = 0x40 | 0x11,  // 17: read a block of data
    WRITE       = 0x40 | 0x18,  // 24: write a block of data
    DELFROM     = 0x40 | 0x20,  // 32: signal the start block for deletion
    DELTO       = 0x40 | 0x21,  // 33: signal the end block for deletion
    DEL         = 0x40 | 0x26   // 38: begin deletion from the block range specified by the [DELFROM : DELTO] commands
} SdCommand;



typedef enum {
  READY,
  PROCESSING,
  IDLE
} State;
char cmd[CMDMAX] = {};
State cs = IDLE;
State ns = IDLE;



void gotoNextState() {
  cs = ns;
}



void printHelp () {
  Serial.println("SJdev2_sdTest Commands:");
  Serial.println("  End all commands by pressing enter");
  Serial.println("\t'echo': Echoes 'echo' back to you");
  Serial.println("\t'help': Prints this help prompt");
  Serial.println("\t'test': Runs the SD Card test script (Currently works on Transcend SD Cards ONLY!!!)");
}



void printResponse (uint8_t* buf, int len, int type = 0) {

    for (int i = 0; i < len; i++) {

        // Print according to the response type
        switch (type) {
            case BIN:           // Binary
                Serial.print("0b"); // add the binary prefix
                Serial.print(buf[i], BIN);
                break;
            case HEX:           // Hexadecimal
                Serial.print("0x"); // add the hexadecimal prefix
                Serial.print(buf[i], HEX);
                break;
            case OCT:           // Octal
                Serial.print("0o"); // add the octal prefix
                Serial.print(buf[i], OCT);
                break;
            default:            // Default
                Serial.println(buf[i]);
                break;
        }

        if (i != len-1) {
            Serial.print(", ");
        }
    }

    // End with a newline
    Serial.println("");

}



// BEGIN polulu CRC generator
unsigned char CRCPoly = 0x89;  // the value of our CRC-7 polynomial
unsigned char CRCTable[256];
 
void GenerateCRCTable()
{
    int i, j;
 
    // generate a table value for all 256 possible byte values
    for (i = 0; i < 256; i++)
    {
        CRCTable[i] = (i & 0x80) ? i ^ CRCPoly : i;
        for (j = 1; j < 8; j++)
        {
            CRCTable[i] <<= 1;
            if (CRCTable[i] & 0x80)
                CRCTable[i] ^= CRCPoly;
        }
    }
}
 
 
// adds a message byte to the current CRC-7 to get a the new CRC-7
unsigned char CRCAdd(unsigned char CRC, unsigned char message_byte)
{
    return CRCTable[(CRC << 1) ^ message_byte];
}
 
 
// returns the CRC-7 for a message of "length" bytes
unsigned char getCRC(unsigned char message[], int length)
{
    int i;
    unsigned char CRC = 0;
 
    for (i = 0; i < length; i++)
        CRC = CRCAdd(CRC, message[i]);
 
    return CRC;
}
// END polulu CRC generator



int getCrc(uint64_t fiveBytes){
  int crc = 0x00;
  int generator = ((fiveBytes >> 7) & 0x01) + ((fiveBytes >> 3) & 0x01) + 1;
  int mx = 0x00;

  // Calculate M(x)
  for (int i = 0; i < 40; i++) {
    int a = (fiveBytes >> i) & 0x01;  // get the current (i'th) bit
    int b = (fiveBytes & (0x01 << (39-i))) >> (39-i);
    mx += a*b;
  }

  // Calculate the CRC7 code
  crc = (mx * ((fiveBytes >> 7) & 0x01)) % generator;
  return crc;
}



// @returns     on success: the number of bytes in the response
//              on error: -1
int sendCmd(SdCommand sdc, uint32_t arg, uint8_t responseBuffer[] = NULL, int delay = 0, bool keepAlive = false) {
    SdResponseType resType;
    int resLen = 0;
    int crc = 0x00;
    int tries = 0;
    //uint8_t responseQueue[6];
    uint8_t bitOffset = 0;  // determines the distance of the response's 0 bit from the MSB place
    uint8_t tempByte = 0;

    // Determine the response type of the set command
    switch (sdc) {
        case GARBAGE: resType = R1; break;
        case RESET: resType = R1; break;
        case INIT: resType = R1; break;
        case GETOP: resType = R7; break;
        case GETSTATUS: resType = R2; break;
        case ACBEGIN: resType = R1; break;
        case ACINIT: resType = R1; break;
        case GETOCR: resType = R3; break;
        case CHGBL: resType = R1; break;
        case READ: resType = R1; break;
        case WRITE: resType = R1; break;
        case DELFROM: resType = R1; break;
        case DELTO: resType = R1; break;
        case DEL: resType = R1b; break;
        default:
            Serial.println("Error: unknown response type. Aborting...");
            return -1;
            break;
    }

    // Calculate the 7-bit CRC (i.e. CRC7) using the SD card standard's algorithm
    unsigned char msg[5] = {
        (unsigned char) sdc,
        (unsigned char) (arg >> 24),
        (unsigned char) (arg >> 16),
        (unsigned char) (arg >> 8),
        (unsigned char) (arg >> 0)
    };
    crc = (int) getCRC(msg, 5);
    if (sdc == GARBAGE) {
        crc = 0xFF;
    }

    // Initialize the SPI bus to these settings before talking (since SD cards begin at a lower frequency than 4kHz)
    SPI.beginTransaction( SPISettings(300000, MSBFIRST, SPI_MODE0) );

    // Select the SD Card
    digitalWrite(pinCS, LOW);

    // If desired, wait a bit before talking
    int i = 0;
    while(i < delay){
        i++;
    }

    // Send the desired command frame to the SD card board
    /*
    responseQueue[0] = (uint8_t) SPI.transfer( sdc );                          // begin by transfering the command byte
    responseQueue[1] = (uint8_t) SPI.transfer( (int) (arg >> 24) & 0xFF );     // send argument byte [31:24]
    responseQueue[2] = (uint8_t) SPI.transfer( (int) (arg >> 16) & 0xFF );     // send argument byte [23:16]
    responseQueue[3] = (uint8_t) SPI.transfer( (int) (arg >> 8) & 0xFF );      // send argument byte [15:8]
    responseQueue[4] = (uint8_t) SPI.transfer( (int) (arg >> 0) & 0xFF );      // send argument byte [7:0]
    responseQueue[5] = (uint8_t) SPI.transfer( (int) (crc << 1) | 0x01 );      // send 7-bit CRC and LSB stop addr (as b1)
    */
    SPI.transfer( sdc );                          // begin by transfering the command byte
    SPI.transfer( (int) (arg >> 24) & 0xFF );     // send argument byte [31:24]
    SPI.transfer( (int) (arg >> 16) & 0xFF );     // send argument byte [23:16]
    SPI.transfer( (int) (arg >> 8) & 0xFF );      // send argument byte [15:8]
    SPI.transfer( (int) (arg >> 0) & 0xFF );      // send argument byte [7:0]
    SPI.transfer( (int) (crc << 1) | 0x01 );      // send 7-bit CRC and LSB stop addr (as b1)

    // TODO: Write garbage while waiting for a response
    tempByte = (uint8_t) SPI.transfer(0xFF);    // send at least 1 byte of garbage before checking for a response
    while (tries++ < BUS_TIMEOUT) {
        tempByte = (uint8_t) SPI.transfer(0xFF);
        if (tempByte != 0xFF) {
            // TODO: Determine the offset, since the first byte of a response will always be 0.
            while (tempByte & (0x80 >> bitOffset)) {
                bitOffset++;
            }
            break;
        }
        tries++;
    }

    // Determine response length (in bytes) based on response type
    switch (resType) {
        case R1: resLen = 1; break;
        case R1b: resLen = 1; break;
        case R2: resLen = 2; break;
        case R3: resLen = 5; break;
        case R7: resLen = 5; break;
        default:
            Serial.println("Error: response unsupported in SPI mode. Aborting...");
            return -1;
            break;
    }

    // Acquire the response
    uint64_t tempResponse = 0;
    int bytesToRead = (bitOffset > 0) ? resLen + 1 : resLen;    // read an extra 8 bits since the response was offset
    while (bytesToRead-- > 0) {
        tempResponse = tempResponse << 8;   // make space for the next byte
        tempResponse |= tempByte;
        tempByte = SPI.transfer(0xFF);
    }
    tempResponse = tempResponse >> bitOffset;   // compensate for the bit offset

    // Only write to the response buffer if it is provided
    if (responseBuffer != NULL) {
        /*
        responseBuffer[0] = responseQueue[0];   // MSB
        responseBuffer[1] = responseQueue[1];
        responseBuffer[2] = responseQueue[2];
        responseBuffer[3] = responseQueue[3];
        responseBuffer[4] = responseQueue[4];
        responseBuffer[5] = responseQueue[5];
        */
        for (int i = 0; i < resLen; i++) {
            responseBuffer[i] = (uint8_t) ( tempResponse >> 8 * (resLen - 1 - i) );
        }
    }

    // Only end the transaction if keepAlive isn't requested
    if (!keepAlive) {
    digitalWrite(pinCS, HIGH);                              // deselect the SPI comm board
        SPI.endTransaction();                                   // Re-enable interrupts for the SPI bus
    }
    return resLen;
}



void processCmd(char* cmd) {
    if (strncmp(cmd, "help", CMDMAX) == 0) {

        printHelp();

    } else if (strncmp(cmd, "echo", CMDMAX) == 0) {

        Serial.println("echo");

    } else if (strncmp(cmd, "test", CMDMAX) == 0) {
        SdResponse res;
        SdType sdType;
        int tries = 0;
        bool cardIsIdle = false;



        // Reset the card and force it to go to idle state at <400kHz with a CMD0 + (active-low) CS
        Serial.println("Sending SD Card to Idle State...");
        res.length = sendCmd(RESET, 0x00, res.response, 100, true);            // Reset the SD card
        //res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);      // Send Garbage to acquire response
        Serial.print("    Response: ");
        printResponse(res.response, res.length, HEX);



        // Reset the card again to trigger SPI mode
        Serial.print("Initializing SPI mode...");
        do {
            tries++;
            Serial.print("Attempt #");
            Serial.println(tries);
            res.length = sendCmd(RESET, 0x00, res.response, 100, true);            // Reset the SD card
            //res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);      // Send Garbage to acquire response
    
            // Check if R1 response frame's bit 1 is set (to ensure that card is in idle state)
            if (res.response[0] & 0x01 == 0x01) {
                // If it is, we can move on; otherwise, keep trying for a set amount of tries
                cardIsIdle = true;
            }
            delay(1000);
        } while (tries < BUS_TIMEOUT && !cardIsIdle);
        Serial.print("    Response: ");
        printResponse(res.response, res.length, HEX);
        if (tries >= BUS_TIMEOUT) {
            Serial.println("Error: failed to initiate SPI mode within timeout. Aborting...");
            res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response); // set CS high
            return;
        }



        // Send the host's supported voltage (3.3V) and ask if the card supports it
        Serial.println("Checking Current SD Card Voltage Level...");
        unsigned int checkPattern = 0xAB;
        uint64_t supportedVoltage = 0x00000001;
        res.length = sendCmd(GETOP, (supportedVoltage << 8) | checkPattern, res.response, 100, true);
        //res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);      // Send Garbage to acquire response
        Serial.print("    Response: ");
        printResponse(res.response, res.length, HEX);
        if (res.response[4] != checkPattern) {
            // If the last byte is not an exact echo of the LSB of the GETOP command's argument, this
            // response is invalid
            Serial.println("Error: response integrity check failed. Aborting...");
            res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response); // set CS high
            return;
        } else if (res.response[3] & (unsigned int) supportedVoltage == 0x00) {
            // If the 2nd-to-last byte of the reponse AND'ed with our host device's supported voltage
            // range is 0x00, the SD card doesn't support our device's operating voltage
            Serial.println("Fatal Error: unsupported voltage in use. Aborting...");
            res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response); // set CS high
            return;
        }



        /*
        // (Optional) Ask for the card's supported voltage ranges
        Serial.println("Checking SD Card's supported voltage ranges...");
        res.length = sendCmd(GETOCR, 0x00, res.response, 100, true);             // Read the OCR register
        //res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);      // Send Garbage to acquire response
        Serial.print("    Response: ");
        printResponse(res.response, res.length, HEX);
        uint32_t sdOcr = (uint32_t) res.response[5] << 0 |
                         (uint32_t) res.response[4] << 8 |
                         (uint32_t) res.response[3] << 16 |
                         (uint32_t) res.response[2] << 24;
        if (sdOcr & (1 << SdOcr::IS_3_2V_TO_3_3V) || sdOcr & (1 << SdOcr::IS_3_2V_TO_3_3V)) {
            Serial.println("Host voltage range supported by SD Card");
        } else {
            Serial.println("SD Card voltage range support unconfirmed. Aborting...");
            res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response); // set CS high
            return;
        }
        */



        // Signal that the next command is a special, application-specific command
//        Serial.println("Begin application-specific command...");
//        res.length = sendCmd(ACBEGIN, 0x00, res.response, 0, true);
//        //res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);      // Send Garbage to acquire response
//        Serial.print("    Response:");
//        printResponse(res.response, res.length, HEX);
//        if (res.response[0] & 0x01 != 0x01) {
//            Serial.println("Error: SD Card failed to acknowledge. Aborting...");
//            res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response); // set CS high
//            return;
//        }
    
    
    
        // Indicate that the host supports SDHC/SDXC and wait for card to shift out of idle state
        Serial.println("Expressing High-Capacity SD Card Support...");
        tries = 0;
        do {
            //res.length = sendCmd(ACINIT, 0x40000000, res.response, 100, true);     // New standard of sending host's op. conds.
            res.length = sendCmd(INIT, 0x40000000, res.response, 100, true);       // Send host's operating conditions
            //res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);      // Send Garbage to acquire response
            tries++;
        } while (tries < BUS_TIMEOUT && res.response[0] & 0x01);
        Serial.print("    Response: ");
        printResponse(res.response, res.length, HEX);
        if (tries == BUS_TIMEOUT) {
            Serial.println("Error: SD Card timed out. Aborting...");
            res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response); // set CS high
            return;
        }
    
    
    
        // After card is ready, acquire card capacity info using GETOCR a second time
        Serial.println("Reading Card Capacity Information...");
        res.length = sendCmd(GETOCR, 0x00, res.response, 100, true);                   // Read CCS
        //res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);      // Send Garbage to acquire response
        Serial.print("    Response: ");
        printResponse(res.response, res.length, HEX);
        if (res.response[1] & (1 << SdOcr::CCS)) {
            Serial.println("SD Card is HC/XC"); // The card is either high or extended capacity
            sdType = SDHC;
        } else {
            Serial.println("SD Card is SC");    // The card is standard capacity
            sdType = SDSC;
        }
    
        // TODO: Test Read from SD card
    
        // TODO: Test Write to SD card
    
        // TODO: Test Delete from SD card
    
        // TEST: Write garbage
        for (int i = 0; i < 9; i++) {
            res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0, true);
        }
        res.length = sendCmd(GARBAGE, 0xFFFFFFFF, res.response, 0);

    } else if (strncmp(cmd, "test2", CMDMAX) == 0) {

        unsigned char msg[5] = {0x48, 0x00, 0x00, 0x00, 0xAA};
        Serial.println("Executing Test 2...");
        Serial.println(getCRC(msg, 5), BIN);

    } else {

        Serial.println("Unrecognized command!");

    }
}



void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  // BEGIN polulu
  GenerateCRCTable();
  // END polulu

  // Activate CS pins (all other pins are activated by SPI
  pinMode(pinCS, OUTPUT);
  digitalWrite(pinCS, HIGH);  // deactivate CS at start
  SPI.begin();
  printHelp();
}



void loop() {
  switch (cs) {
    case IDLE: {
        Serial.print("CMD>");
        ns = READY;
        break;
      }
    case READY: {
        // Acquire command input
        if (Serial.available() > 0) {
          int end = Serial.readBytesUntil('\n', cmd, CMDMAX);
          if (end < CMDMAX) cmd[end] = '\0';

          // After acquiring command, proceed to process that command
          ns = (PROCESSING);
        }
        break;
      }
    case PROCESSING: {
        Serial.println(cmd);

        processCmd(cmd);

        // END state
        ns = (IDLE);
        break;
      }
  }

  gotoNextState();
}
