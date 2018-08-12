//#include <stdio.h>
#include <SPI.h>
#include <string.h> // strncmp()

#define DEBUG 0
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
    union {
        uint64_t qWord;
        struct {
            uint32_t hi;
            uint32_t lo;
        } dWord;
        uint8_t byte[8];
    } data;
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

typedef struct {
    union {
        uint32_t dWord;
        uint16_t word[2];
        uint8_t byte[4];
    } ocr;
    SdType type;
    SdResponse response;
} SdCard;

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
    RESET       = 0x40 | 0,     // CMD0: reset the sd card (force it to go to the idle state)
    INIT        = 0x40 | 1,     // CMD1: starts an initiation of the card
    GET_OP      = 0x40 | 8,     // CMD8: request the sd card's support of the provided host's voltage ranges
    GET_STATUS  = 0x04 | 13,    // CMD13: get status register
    CHG_BLK_LEN = 0x40 | 16,    // CMD16: change block length (only effective in SDSC cards)
    READ_BLK    = 0x40 | 17,    // CMD17: read a single block of data
    READ_BLKS   = 0x40 | 18,    // CMD18: read many blocks of data until CMD12 is sent
    WRITE_BLK   = 0x40 | 24,    // CMD24: write a single block of data
    WRITE_BLKS  = 0x40 | 25,    // CMD25: write many blocks of data until CMD12 (?) is sent
    DEL_FROM    = 0x40 | 32,    // CMD32: set address of the start block for deletion
    DEL_TO      = 0x40 | 33,    // CMD33: set address of the end block for deletion
    DEL         = 0x40 | 38,    // CMD38: begin deletion from the block range specified by the [DEL_FROM : DEL_TO] commands
    ACBEGIN     = 0x40 | 55,    // CMD55: signals the start of an application-specific command
    GETOCR      = 0x40 | 58,    // CMD58: request data from the operational conditions register
    ACINIT      = 0x40 | 41     // ACMD41: application-specific version of CMD1 (must precede with CMD55)
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



//int getCrc(uint64_t fiveBytes){
//  int crc = 0x00;
//  int generator = ((fiveBytes >> 7) & 0x01) + ((fiveBytes >> 3) & 0x01) + 1;
//  int mx = 0x00;
//
//  // Calculate M(x)
//  for (int i = 0; i < 40; i++) {
//    int a = (fiveBytes >> i) & 0x01;  // get the current (i'th) bit
//    int b = (fiveBytes & (0x01 << (39-i))) >> (39-i);
//    mx += a*b;
//  }
//
//  // Calculate the CRC7 code
//  crc = (mx * ((fiveBytes >> 7) & 0x01)) % generator;
//  return crc;
//}



// @function        sendCmd()
// @description     sends a command frame to the SD Card and sets the response
// @parameter       (SdCommand) sdc                 the command code to send as the first byte
//                  (uint32_t) arg                  the 4-byte argument for the command
//                  ~(uint8_t*) responseBuffer      pointer to a buffer to house the response frame
//                  ~(int) delay                    delay (ms) between asserting CS and sending a command frame
//                  ~(bool) keepAlive               determines whether to de-assert CS after receiving response
// @returns         on success: the number of bytes in the response
//                  on error: -1
int sendCmd(SdCommand sdc, uint32_t arg, uint8_t responseBuffer[] = NULL, int delay = 0, bool keepAlive = false) {
    SdResponseType resType;
    int resLen = 0;
    int crc = 0x00;
    int tries = 0;
    uint8_t bitOffset = 0;  // determines the distance of the response's 0 bit from the MSB place
    uint8_t tempByte = 0;

    // Determine the response type of the set command
    switch (sdc) {
        case GARBAGE: resType = R1; break;
        case RESET: resType = R1; break;
        case INIT: resType = R1; break;
        case GET_OP: resType = R7; break;
        case GET_STATUS: resType = R2; break;
        case ACBEGIN: resType = R1; break;
        case ACINIT: resType = R1; break;
        case GETOCR: resType = R3; break;
        case CHG_BLK_LEN: resType = R1; break;
        case READ_BLK: resType = R1; break;
        case READ_BLKS: resType = R1; break;
        case WRITE_BLK: resType = R1; break;
        case WRITE_BLKS: resType = R1; break;
        case DEL_FROM: resType = R1; break;
        case DEL_TO: resType = R1; break;
        case DEL: resType = R1b; break;
        default:
            #if DEBUG
            Serial.println("Error: unknown response type. Aborting...");
            #endif
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



// @function        initializeSdCard()
// @description     runs the SD Card initialization sequence
// @parameter       (SdCard*) sd - a pointer to the SD Card structure
// @returns         on init success: true
//                  on init failure: false
bool initializeSdCard (SdCard* sd) {
    //SdResponse res;
    //SdType sdType;
    int tries = 0;
    bool cardIsIdle = false;



    // Reset the card and force it to go to idle state at <400kHz with a CMD0 + (active-low) CS
    #if DEBUG
    Serial.println("Sending SD Card to Idle State...");
    #endif
    sd->response.length = sendCmd(RESET, 0x00, sd->response.data.byte, 100, true);            // Reset the SD card

    #if DEBUG
    Serial.print("    Response: ");
    printResponse(sd->response.data.byte, sd->response.length, HEX);
    #endif



    // Reset the card again to trigger SPI mode
    #if DEBUG
    Serial.print("Initializing SPI mode...");
    #endif
    do {
        tries++;
        #if DEBUG
        Serial.print("Attempt #");
        Serial.println(tries);
        #endif
        sd->response.length = sendCmd(RESET, 0x00, sd->response.data.byte, 100, true);            // Reset the SD card
        

        // Check if R1 response frame's bit 1 is set (to ensure that card is in idle state)
        if (sd->response.data.byte[0] & 0x01 == 0x01) {
            // If it is, we can move on; otherwise, keep trying for a set amount of tries
            cardIsIdle = true;
        }
        delay(1000);
    } while (tries < BUS_TIMEOUT && !cardIsIdle);
    #if DEBUG
    Serial.print("    Response: ");
    printResponse(sd->response.data.byte, sd->response.length, HEX);
    #endif
    if (tries >= BUS_TIMEOUT) {
        #if DEBUG
        Serial.println("Error: failed to initiate SPI mode within timeout. Aborting...");
        #endif
        sd->response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd->response.data.byte); // set CS high
        return false;
    }



    // Send the host's supported voltage (3.3V) and ask if the card supports it
    #if DEBUG
    Serial.println("Checking Current SD Card Voltage Level...");
    #endif
    unsigned int checkPattern = 0xAB;
    uint64_t supportedVoltage = 0x00000001;
    sd->response.length = sendCmd(GET_OP, (supportedVoltage << 8) | checkPattern, sd->response.data.byte, 100, true);

    #if DEBUG
    Serial.print("    Response: ");
    printResponse(sd->response.data.byte, sd->response.length, HEX);
    #endif
    if (sd->response.data.byte[4] != checkPattern) {
        // If the last byte is not an exact echo of the LSB of the GET_OP command's argument, this
        // response is invalid
        #if DEBUG
        Serial.println("Error: response integrity check failed. Aborting...");
        #endif
        sd->response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd->response.data.byte); // set CS high
        return false;
    } else if (sd->response.data.byte[3] & (unsigned int) supportedVoltage == 0x00) {
        // If the 2nd-to-last byte of the reponse AND'ed with our host device's supported voltage
        // range is 0x00, the SD card doesn't support our device's operating voltage
        #if DEBUG
        Serial.println("Fatal Error: unsupported voltage in use. Aborting...");
        #endif
        sd->response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd->response.data.byte); // set CS high
        return false;
    }



    /*
    // (Optional) Ask for the card's supported voltage ranges
    #if DEBUG
    Serial.println("Checking SD Card's supported voltage ranges...");
    #endif
    sd->response.length = sendCmd(GETOCR, 0x00, sd->response.data.byte, 100, true);             // Read the OCR register

    #if DEBUG
    Serial.print("    Response: ");
    printResponse(sd->response.data.byte, sd->response.length, HEX);
    #endif
    if (res.response.qWord & (1 << SdOcr::IS_3_2V_TO_3_3V) || res.response.qWord & (1 << SdOcr::IS_3_2V_TO_3_3V)) {
        #if DEBUG
        Serial.println("Host voltage range supported by SD Card");
        #endif
    } else {
        #if DEBUG
        Serial.println("SD Card voltage range support unconfirmed. Aborting...");
        #endif
        sd->response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd->response.data.byte); // set CS high
        return false;
    }
    */



    // Signal that the next command is a special, application-specific command
    /*
    #if DEBUG
    Serial.println("Begin application-specific command...");
    #endif
    sd->response.length = sendCmd(ACBEGIN, 0x00, sd->response.data.byte, 0, true);

    #if DEBUG
    Serial.print("    Response:");
    printResponse(res.response, sd->response.length, HEX);
    #endif
    if (sd->response.data.byte[0] & 0x01 != 0x01) {
        #if DEBUG
        Serial.println("Error: SD Card failed to acknowledge. Aborting...");
        #endif
        sd->response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd->response.data.byte); // set CS high
        return false;
    }
    */



    // Indicate that the host supports SDHC/SDXC and wait for card to shift out of idle state
    #if DEBUG
    Serial.println("Expressing High-Capacity SD Card Support...");
    #endif
    tries = 0;
    do {
        //sd->response.length = sendCmd(ACINIT, 0x40000000, sd->response.data.byte, 100, true);     // New standard of sending host's op. conds.
        sd->response.length = sendCmd(INIT, 0x40000000, sd->response.data.byte, 100, true);       // Send host's operating conditions
        
        tries++;
    } while (tries < BUS_TIMEOUT && sd->response.data.byte[0] & 0x01);
    #if DEBUG
    Serial.print("    Response: ");
    printResponse(sd->response.data.byte, sd->response.length, HEX);
    #endif
    if (tries == BUS_TIMEOUT) {
        #if DEBUG
        Serial.println("Error: SD Card timed out. Aborting...");
        #endif
        sd->response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd->response.data.byte); // set CS high
        return false;
    }



    // After card is ready, acquire card capacity info using GETOCR a second time
    #if DEBUG
    Serial.println("Reading Card Capacity Information...");
    #endif
    sd->response.length = sendCmd(GETOCR, 0x00, sd->response.data.byte, 100, true);                   // Read CCS
    #if DEBUG
    Serial.print("    Response: ");
    printResponse(sd->response.data.byte, sd->response.length, HEX);
    #endif
    if (sd->response.data.byte[1] & 0x40) {
        #if DEBUG
        Serial.println("SD Card is HC/XC"); // The card is either high or extended capacity
        #endif
        sd->type = SDHC;
    } else {
        #if DEBUG
        Serial.println("SD Card is SC");    // The card is standard capacity
        #endif
        sd->type = SDSC;
    }

    // Store OCR information
    for (int i = 0; i < 4; i++) {
        sd->ocr.byte[i] = sd->response.data.byte[i+1];   // ensure OCR doesn't capture the R1 section of the response
    }

    return true;
}



void processCmd(char* cmd) {
    if (strncmp(cmd, "help", CMDMAX) == 0) {

        printHelp();

    } else if (strncmp(cmd, "echo", CMDMAX) == 0) {

        Serial.println("echo");

    } else if (strncmp(cmd, "test", CMDMAX) == 0) {
        SdCard sd;

        // Initialize the SD Card and store its properties in the sd variable
        if (!initializeSdCard(&sd)) {
            Serial.println("SD Card Init Failed...");
            return;
        } else {

            // SD Card init success
            //Serial.println("SD Card Init Complete!\nSD Card Properties:");

            // Print SD Card info and supported operating conditions
            /*
            Serial.print("    OCR: ");
            printResponse(sd.ocr.byte, 4, HEX);
            Serial.println("        Supported Voltages:");
            const uint32_t shiftedBit = 1;
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_DUAL_VOLTAGE)) {
                Serial.println("Dual-Voltage Supported");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_1_8V_OK)) {
                Serial.println("1.8V Switching Supported");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_2_7V_TO_2_8V)) {
                Serial.println("2.7V - 2.8V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_2_8V_TO_2_9V)) {
                Serial.println("2.8V - 2.9V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_2_9V_TO_3_0V)) {
                Serial.println("2.9V - 3.0V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_3_0V_TO_3_1V)) {
                Serial.println("3.0V - 3.1V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_3_1V_TO_3_2V)) {
                Serial.println("3.1V - 3.2V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_3_2V_TO_3_3V)) {
                Serial.println("3.2V - 3.3V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_3_3V_TO_3_4V)) {
                Serial.println("3.3V - 3.4V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_3_4V_TO_3_5V)) {
                Serial.println("3.4V - 3.5V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_3_5V_TO_3_6V)) {
                Serial.println("3.5V - 3.6V");
            }
            if (sd.ocr.dWord & (shiftedBit << SdOcr::IS_UHS2)) {
                Serial.println("Ultra-High-Speed II Supported");
            }
            
            Serial.print("    Card Capacity Type: ");
            switch (sd.type) {
                case SDHC:
                    Serial.println("SDHC/SDXC");
                    break;
                case SDSC:
                    Serial.println("SDSC");
                    break;
            }
            */
        }
    
        // TODO: Test Read from SD card
    
        // TODO: Test Write to SD card
    
        // TODO: Test Delete from SD card
    
        // TEST: Write garbage
        for (int i = 0; i < 9; i++) {
            sd.response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd.response.data.byte, 0, true);
        }
        sd.response.length = sendCmd(GARBAGE, 0xFFFFFFFF, sd.response.data.byte, 0);

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
