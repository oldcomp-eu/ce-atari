#if !defined(__GEMDOS_ERRNO)
#define __GEMDOS_ERRNO

#define E_NOTHANDLED        0x7f    // return this if the host part didn't handle the command and we should use the original one
#define E_WAITING_FOR_MOUNT 0x7e    // return this if the host will handle the command in a while, after the mount command has been processed

#define RW_ALL_TRANSFERED   0       // return this is all the required data was read / written
#define RW_PARTIAL_TRANSFER 1       // return this if not all of the required data was read / written


// BIOS errors
#define E_OK            0   // 00 No error
#define GENERIC_ERROR   -1  // ff Generic error
#define EDRVNR          -2  // fe Drive not ready
#define EUNCMD          -3  // fd Unknown command
#define E_CRC           -4  // fc CRC error
#define EBADRQ          -5  // fb Bad request
#define E_SEEK          -6  // fa Seek error
#define EMEDIA          -7  // f9 Unknown media
#define ESECNF          -8  // f8 Sector not found
#define EPAPER          -9  // f7 Out of paper
#define EWRITF          -10 // f6 Write fault
#define EREADF          -11 // f5 Read fault
#define EWRPRO          -12 // f4 Device is write protected
#define E_CHNG          -14 // f2 Media change detected
#define EUNDEV          -15 // f1 Unknown device
#define EBADSF          -16 // f0 Bad sectors on format
#define EOTHER          -17 // ef Insert other disk (request)

// GEMDOS errors
#define EINVFN          -32 // e0 Invalid function
#define EFILNF          -33 // df File not found
#define EPTHNF          -34 // de Path not found
#define ENHNDL          -35 // dd No more handles
#define EACCDN          -36 // dc Access denied
#define EIHNDL          -37 // db Invalid handle
#define ENSMEM          -39 // d9 Insufficient memory
#define EIMBA           -40 // d8 Invalid memory block address
#define EDRIVE          -46 // d2 Invalid drive specification
#define ENSAME          -48 // d0 Cross device rename
#define ENMFIL          -49 // cf No more files
#define ELOCKED         -58 // c6 Record is already locked
#define ENSLOCK         -59 // c5 Invalid lock removal request
#define ERANGEERROR     -64 // c0 Range error
#define ENAME_TOOLONG   -64 // c0 Range error
#define EINTRN          -65 // bf Internal error
#define EPLFMT          -66 // be Invalid program load format
#define EGSBF           -67 // bd Memory block growth failure
#define EGDLOOP         -80 // b0 Too many symbolic links

#endif

/************************************************************************/
