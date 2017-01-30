// vim: expandtab shiftwidth=4 tabstop=4
#include <mint/sysbind.h>
#include <mint/osbind.h>
#include <mint/basepage.h>
#include <mint/ostruct.h>
#include <support.h>

#include <stdint.h>
#include <stdio.h>

#include "stdlib.h"
#include "hdd_if.h"
#include "translated.h"
#include "vt52.h"
#include "main.h"

// ------------------------------------------------------------------
WORD tosVersion;
void getTosVersion(void);

THDif *hdIf;
void getCE_API(void);

WORD dmaBuffer[DMA_BUFFER_SIZE/2];  // declare as WORD buffer to force WORD alignment
BYTE *pDmaBuffer;

BYTE commandShort[CMD_LENGTH_SHORT] = {      0, 'C', 'E', HOSTMOD_TRANSLATED_DISK, 0, 0};
BYTE commandLong [CMD_LENGTH_LONG]  = {0x1f, 0, 'C', 'E', HOSTMOD_TRANSLATED_DISK, 0, 0, 0, 0, 0, 0, 0, 0};

void hdIfCmdAsUser(BYTE readNotWrite, BYTE *cmd, BYTE cmdLength, BYTE *buffer, WORD sectorCount);

//--------------------------
// DEVTYPE values really sent from CE
#define DEVTYPE_OFF         0
#define DEVTYPE_SD          1
#define DEVTYPE_RAW         2
#define DEVTYPE_TRANSLATED  3

// DEVTYPE values used in this app for making other states
#define DEVTYPE_NOTHING     10      // nothing   responded on this ID
#define DEVTYPE_UNKNOWN     11      // something responded on this ID, but it's not CE

BYTE devicesOnBus[8];               // contains DEVTYPE_ for all the xCSI IDs
void scanXCSIbus        (void);
BYTE getCEid            (BYTE anyCEnotOnlySD);
BYTE getSDcardInfo      (BYTE deviceID);
void showSDcardCapacity (void);

struct {
    BYTE  id;                       // assigned ACSI ID
    BYTE  isInit;                   // contains if the SD card is present and initialized
    DWORD SCapacity;                // capacity of the card in sectors
} SDcard;

// ------------------------------------------------------------------
int main( int argc, char* argv[] )
{
    memset(devicesOnBus, 0, sizeof(devicesOnBus));
    pDmaBuffer = (BYTE *) dmaBuffer;
    
    // write some header out
    Clear_home();
    //                |                                        |
    (void) Cconws("\33p   [ CosmosEx SD NOOB configurator ]    \33q\r\n\r\n");

    //            |                                        |
    (void) Cconws("This tool is for inexperienced users and\r\n");
    (void) Cconws("it will automatically partition your    \r\n");
    (void) Cconws("SD card to maximum size allowed on your \r\n");
    (void) Cconws("TOS version, making it DOS compatible   \r\n");
    (void) Cconws("and accessible on ACSI bus.             \r\n");
    (void) Cconws("\r\n");
    (void) Cconws("If you know how to do this by yourself, \r\n");
    (void) Cconws("then you might achieve better results   \r\n");
    (void) Cconws("doing so than with this automatic tool. \r\n");
    (void) Cconws("\r\n");
    (void) Cconws("Press \33p[Q]\33q to quit.\r\n");
    (void) Cconws("Press \33p[C]\33q to continue.\r\n");

    while(1) {
        BYTE key = Cnecin();

        if(key == 'q' || key == 'Q') {  // quit?
            return 0;
        }

        if(key == 'c' || key == 'C') {  // continue?
            break;
        }
    }

    //-------------
    // if user decided to continue...
    Clear_home();
    Supexec(getTosVersion);             // find out TOS version

    //            |                                        |
    // show TOS version
    (void) Cconws("Your TOS version      : ");
    showInt((tosVersion >> 16) & 0xff, 1);
    (void) Cconws(".");
    showInt((tosVersion >>  8) & 0xff, 1);
    showInt((tosVersion      ) & 0xff, 1);
    (void) Cconws("\r\n");

    // show maximum partition size
    (void) Cconws("Maximum partition size: ");

    WORD partitionSizeMB;

    if(tosVersion <= 0x0102) {          // TOS 1.02 and older
        partitionSizeMB =  256;
        (void) Cconws("256 MB\r\n");
    } else if(tosVersion < 0x0400) {    // TOS 1.04 - 3.0x
        partitionSizeMB =  512;
        (void) Cconws("512 MB\r\n");
    } else {                            // TOS 4.0x
        partitionSizeMB = 1024;
        (void) Cconws("1024 MB\r\n");
    } 

    //-------------
    // find CE_DD API in cookie jar
    Supexec(getCE_API);

    (void) Cconws("CosmosEx DD API       : ");
    if(hdIf) {                                  // if CE_DD API was found, good
        (void) Cconws("found\r\n");
    } else {                                    // if CE_DD API wasn't found, fail
        (void) Cconws("not found\r\n\r\n");
        (void) Cconws("\33pPlease run CE_DD.PRG before this tool!\33q\r\n");
        (void) Cconws("Press any key to terminate...\r\n");

        Cnecin();
        return 0;
    }

    //-------------
    // Scan xCSI bus to find CE and CE_SD.
    // Use solo command to get if SD card is inserted, and its capacity.
    BYTE ceId;

    scanXCSIbus();                  // scan xCSI bus and find everything (even non-CE devices)
    ceId = getCEid(TRUE);           // get first ID which belongs to CE

    if(ceId == 0xff) {              // no CE ID found? quit, fail
        (void) Cconws("CosmosEx was not found on bus.\r\n");
        (void) Cconws("Press any key to terminate...\r\n");

        Cnecin();
        return 0;
    }

    BYTE res;

    while(1) {
        res = getSDcardInfo(ceId);  // try to get the info about the card

        if(!res) {                  // failed to get the info about the card? fail
            (void) Cconws("Failed to get card info, fail.\r\n");
            (void) Cconws("Press any key to terminate...\r\n");

            Cnecin();
            return 0;
        }

        (void) Cconws("SD card is inserted   : ");

        if(SDcard.isInit) {             // if got the card, we can quit
            (void) Cconws("YES\r\n");
            break;
        }

        // don't have the card? try again
        (void) Cconws("NO\r\n");
        (void) Cconws("Please insert the SD card in slot.\r\n");

        sleep(1);

        VT52_Del_line();        // delete 2nd line
        VT52_Cur_up();          // go line up
        VT52_Del_line();        // delete 1st line
    }

    (void) Cconws("SD card capacity      : ");
    showSDcardCapacity();

    //-------------
    // if SD is not enabled, assign some xCSI ID to to SD card
    BYTE sdId = getCEid(FALSE);

    if(sdId == 0xff) {          // SD is not enabled on xCSI bus? Configure it!



    }

    // show SD ID to user
    (void) Cconws("SD ID on bus          : ");
    showInt(sdId, 1);
    (void) Cconws("\r\n");

    //-------------
    // TODO: read boot sector from SD card, if if contains some other driver, warn user

    //-------------
    // TODO: warn user that if he will proceed, he will loose data

    //-------------
    // TODO: if continuing, write boot sector and everything needed for partitioning

    //-------------
    // TODO: show message that we're done and we need to reset the ST to apply new settings

    //-------------

    return 0;
}

//--------------------------------------------
void getTosVersion(void)
{
    BYTE  *pSysBase     = (BYTE *) 0x000004F2;
    BYTE  *ppSysBase    = (BYTE *)  ((DWORD )  *pSysBase);          // get pointer to TOS address

    tosVersion          = (WORD  ) *(( WORD *) (ppSysBase + 2));    // TOS +2: TOS version
}

//--------------------------------------------
void getCE_API(void)
{
    // get address of cookie jar
    DWORD *cookieJarAddr    = (DWORD *) 0x05A0;
    DWORD *cookieJar        = (DWORD *) *cookieJarAddr;

    hdIf = NULL;

    if(cookieJar == 0) {                        // no cookie jar? it's an old ST and CE_DD wasn't loaded 
        return;
    }

    DWORD cookieKey, cookieValue;

    while(1) {                                  // go through the list of cookies
        cookieKey   = *cookieJar++;
        cookieValue = *cookieJar++;

        if(cookieKey == 0) {                    // end of cookie list? then cookie not found
            return;
        }

        if(cookieKey == 0x43455049) {           // is it 'CEPI' key? found it, store it, quit
            hdIf = (THDif *) cookieValue;
            return;
        }
    }
}
//--------------------------------------------
BYTE cs_inquiry(BYTE id)
{
    BYTE cmd[CMD_LENGTH_SHORT];
    
    memset(cmd, 0, 6);
    cmd[0] = (id << 5) | (SCSI_CMD_INQUIRY & 0x1f);
    cmd[4] = 32;                                                    // count of bytes we want from inquiry command to be returned
    
    hdIfCmdAsUser(ACSI_READ, cmd, CMD_LENGTH_SHORT, pDmaBuffer, 1); // issue the inquiry command and check the result 
    
    if(!hdIf->success || hdIf->statusByte != SCSI_STATUS_OK) {      // if failed, return FALSE 
        return FALSE;
    }

    return TRUE;
}
//--------------------------------------------
// scan the xCSI bus and detect all the devices that are there
void scanXCSIbus(void)
{
    BYTE i, res, isCE;

    (void) Cconws("Bus scan              : ");

    hdIf->maxRetriesCount = 0;                                  // disable retries - we are expecting that the devices won't answer on every ID

    for(i=0; i<8; i++) {
        res = cs_inquiry(i);                                    // try to read the INQUIRY string

        isCE            = FALSE;                                // it's not CE (at this moment)
        devicesOnBus[i] = DEVTYPE_NOTHING;                      // nothing here
        
        if(res) {                                               // something responded
            if(memcmp(pDmaBuffer + 16, "CosmosEx", 8) == 0) {   // inquiry string contains 'CosmosEx'
                isCE = TRUE;

                if(memcmp(pDmaBuffer + 27, "SD", 2) == 0) {     // it's CosmosEx SD card
                    devicesOnBus[i] = DEVTYPE_SD;
                } else {                                        // it's CosmosEx, but not SD card
                    devicesOnBus[i] = DEVTYPE_TRANSLATED;
                }
            } else if(memcmp(pDmaBuffer + 16, "CosmoSolo", 9) == 0) {   // it's CosmoSolo, that's SD card
                isCE = TRUE;

                devicesOnBus[i] = DEVTYPE_SD;
            } else {                                            // it's not CosmosEx and also not CosmoSolo
                devicesOnBus[i] = DEVTYPE_UNKNOWN;              // we don't know what it is, but it's there
            }        
        }

        if(isCE) {
            (void) Cconws("\33p");
            Cconout(i + '0');                                   // White-on-Black - it's CE
            (void) Cconws("\33q");
        } else {
            Cconout(i + '0');                                   // Black-on-White - not CE or not present
        }
    }

    hdIf->maxRetriesCount = 10;                                 // enable retries
}

//--------------------------------------------------
// go through the already detected devices, and return xCSI ID of the first which is CE 
BYTE getCEid(BYTE anyCEnotOnlySD)
{
    BYTE i;

    for(i=0; i<8; i++) {        // go through the found IDs, if it's CE, return ID
        // if it's SD dev type, we can return it no matter what anyCEnotOnlySD is
        if(devicesOnBus[i] == DEVTYPE_SD) {
            return i;
        }

        // if it's TRANSLATED dev type, we return it only if anyCEnotOnlySD is TRUE
        if(devicesOnBus[i] == DEVTYPE_TRANSLATED && anyCEnotOnlySD) {
            return i;
        }
    }
    
    return 0xff;                // no CE ID found
}

//--------------------------------------------------

BYTE getSDcardInfo(BYTE deviceID)
{
    // issue CS command to get info
    commandLong[0] = (deviceID << 5) | 0x1f;    // SD card device ID
    commandLong[3] = 'S';                       // for CS
    commandLong[5] = TEST_GET_ACSI_IDS;
    commandLong[6] = 0;                         // don't reset SD error counters

    hdIfCmdAsUser(ACSI_READ, commandLong, CMD_LENGTH_LONG, pDmaBuffer, 1);

    if(!hdIf->success || hdIf->statusByte != 0) {   // if command failed...
        return FALSE;
    }

    SDcard.id       = pDmaBuffer[9];                // ID
    SDcard.isInit   = pDmaBuffer[10];               // is init
    
    SDcard.SCapacity  = pDmaBuffer[15];             // get SD card capacity
    SDcard.SCapacity  = SDcard.SCapacity << 8;
    SDcard.SCapacity |= pDmaBuffer[16];
    SDcard.SCapacity  = SDcard.SCapacity << 8;
    SDcard.SCapacity |= pDmaBuffer[17];
    SDcard.SCapacity  = SDcard.SCapacity << 8;
    SDcard.SCapacity |= pDmaBuffer[18];
    
    return TRUE;
}

//--------------------------------------------------
void showSDcardCapacity(void)
{
    DWORD capacityMB = SDcard.SCapacity >> 11;      // sectors into MegaBytes

    if(capacityMB < 1024) {                         // less than 1 GB? show on 4 digits
        int length = 4;

        if(capacityMB <= 999) {                     // if it's less than 1000, show on 3 digits
            length = 3;
        }

        if(capacityMB <= 99) {                      // if it's less than 100, show on 2 digits
            length = 2;
        }

        if(capacityMB <= 9) {                       // if it's less than 10, show on 1 digits
            length = 1;
        }

        showInt(capacityMB, length);
        (void) Cconws(" MB\r\n");
    } else {                                        // more than 1 GB?
        int capacityGB      = capacityMB / 1024;    // GB part
        int capacityRest    = capacityMB % 1024;    // MB part

        capacityRest = capacityRest / 100;          // get only 100s of MB part 

        int length = (capacityGB <= 9) ? 1 : 2;     // if it's bellow 10, show 1 digit, otherwise show 2 digits

        showInt(capacityGB, length);                // show GB part
        (void) Cconws(".");
        showInt(capacityRest, 1);                   // show 100s of MB part

        (void) Cconws(" GB\r\n");
    }
}

//--------------------------------------------------
// global variables, later used for calling hdIfCmdAsSuper
BYTE __readNotWrite, __cmdLength;
WORD __sectorCount;
BYTE *__cmd, *__buffer;

void hdIfCmdAsSuper(void)
{
    // this should be called through Supexec()
    (*hdIf->cmd)(__readNotWrite, __cmd, __cmdLength, __buffer, __sectorCount);
}

void hdIfCmdAsUser(BYTE readNotWrite, BYTE *cmd, BYTE cmdLength, BYTE *buffer, WORD sectorCount)
{
    // store params to global vars
    __readNotWrite  = readNotWrite;
    __cmd           = cmd;
    __cmdLength     = cmdLength;
    __buffer        = buffer;
    __sectorCount   = sectorCount;    
    
    // call the function which does the real work, and uses those global vars
    Supexec(hdIfCmdAsSuper);
}
//--------------------------------------------------
