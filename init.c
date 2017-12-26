
#include <Rttarget.h>



/**************************************************************************/
/*                                                                        */
/*  File: INIT.C                                 Copyright (c) 1998,2006  */
/*  Version: 5.0                                 On Time Informatik GmbH  */
/*                                                                        */
/*                                                                        */
/*                                      On Time        /////////////----- */
/*                                    Informatik GmbH /////////////       */
/* --------------------------------------------------/////////////        */
/*                                  Real-Time and System Software         */
/*                                                                        */
/**************************************************************************/

#include <Rttarget.h>
#include <Rtfiles.h>

/*
#define RTF_BUFFER_SIZE    512  // size of each sector buffer
#define RTCD_BUFFER_SIZE  2048  // size of each sector buffer for CDs
#define RTF_MAX_DRIVES      16  // max logical drives, can be set up to 32
#define RTF_MAX_FILES       16  // max open files, can be set to any value >= 2
#define RTF_MAX_BUFFERS    256  // number of sector buffers
#define RTCD_MAX_BUFFERS    16  // number of sector buffers for CDs
#define RTF_L2CACHE_SIZE    16  // second level cache size in 512 bytes sectors
*/
/*
#include <Rtfdata.c>            // replace default tables with our own
*/
// support one floppy disk, four IDE disks, four SATA ports and 4 AHCI ports.

//static RTFDrvFLPYData Floppy[1] = {0};
static RTFDrvIDEData  IDE[2] = { 0 };
//static RTFDrvIDEData  SATA[4]   = {0};
//static RTFDrvAHCIData AHCI[4]   = {0};


// In this demo, lots of directory and FAT data is read and written.
// For best performance, we will enable RTF_DEVICE_LAZY_WRITE. In most
// user application, this is probably not what you want. The RTFiles-32
// Reference Manual explains in detail what these device flags mean.

//#define DEVICE_FLAGS RTF_DEVICE_LAZY_WRITE

// The RTFiles-32 device list. RTFiles-32 will scan this device listed at
// program startup to mount disk volumes.

RTFDevice RTFDeviceList[] = {
	// IDE primary master and slave
	{ RTF_DEVICE_FDISK , 1/*0*/, 0,        &RTFDrvIDE,    IDE + 0 },
	/*{ RTF_DEVICE_FDISK , 1, 0,        &RTFDrvIDE,    IDE + 1 },*/
	{ 0 }    // end of list
};

// The following example would be used if you use a single DiskOnChip and
// no IDE or floppy disks:
//
// static RTFDrvDOCData Disk0Data;
// RTFDevice RTFDeviceList[] = {
//    { RTF_DEVICE_FDISK , 0, 0, &RTFDrvDOC, &Disk0Data },
//    { 0 }
// };
//
// This example would also require an entry in the
// RTTarget-32 configuration file such as:
//
//   Region DiskOnChip D0000h 8k Device ReadWrite

// The exported Init function below is executed before the run-time system
// startup code.


#ifdef _MSC_VER
	__declspec(dllexport) void          InitFunction(void)
#else
	void __export Init(void)
#endif
	{
		RTSetFlags(RT_MM_VIRTUAL | RT_CLOSE_FIND_HANDLES, 1);
		//RTSetFlags( RT_DBG_OUT_TO_HOST, 1 );
		RTSetFlags(RT_DBG_OUT_NONE, 1);
		RTSetFlags(RT_HEAP_MIN_BLOCK_SIZE_64, 1);  //линия кеша на lx800 32 байта, но пусть будет 64 на всяк случай


		RTCMOSExtendHeap();            // get as much memory as we can
		RTCMOSSetSystemTime();         // get the right date and time
									   // RTEmuInit();                   // only if you need it
	}
