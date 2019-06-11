//
// i82559.c
//
//   EBS - RTIP
//
//   Copyright EBSnet, Inc., 1999
//   All rights reserved.
//   This code may not be redistributed in source or linkable object form
//   without the consent of its author.
//
//
//   Module description:
//       This device driver controls the 82558-59 ethernet controllers
//      on the etherexpress pro/100 PCI NIC card
//
//
//
//
//
//       Data Structure Definition:

#define DIAG_SECTION_KERNEL DIAG_SECTION_DRIVER

#include "sock.h"
#include "rtip.h"
#include "rtipext.h"
#include "pci.h"

#if (INCLUDE_I82559)

#ifndef KONTRON_BUG_WORKAROUND
   #define KONTRON_BUG_WORKAROUND  0 // set to 1 if required
#endif

// ********************************************************************
// DEBUG DEFINES
// ********************************************************************

#define DEBUG_I82559 0
#define USE_TIMER    0

// ********************************************************************
// DEFINES
// ********************************************************************

#define RX_RING_SIZE    16  // Must be power of 2
#define RX_RING_MASK    (RX_RING_SIZE-1) // So if x + 1 == SIZE then x & MASK = 0

#define TX_RING_SIZE    4  // Must b power 2
                           // we don't need many since we send only one at a time
#define TX_RING_MASK    (TX_RING_SIZE-1) // So if x + 1 == SIZE then x & MASK = 0

// When reading from the ring buffer alloc a new dcu and copy if <= this
// size. Otherwise submit the DCU from the ring buffer
#define RX_COPY_BREAK   0

// When transmitting, define the number of bytes which should be in the devices
// Tx fifo before transmitting can begin. This value is internally multiplied by 8
// This is the initial value, if transmit underflows are detected, this value will be
// dynamically adjusted up. Valid range is 1 to 0xE0.
#define TX_THRESHOLD    0x20
#define TX_TBDNUMBER  0x01000000ul  // do not change, we transmit 1 buffer per frame
#define TX_EOF        0x00008000ul  // flag bit to indicate full frame in tx buffer

// Control/Status register offsets
#define SCR_STATUS      0
#define SCR_COMMAND     2
#define SCR_POINTER     4
#define SCR_PORT        8
#define SCR_FLASH       0xc
#define SCR_EEPROM      0xe
#define SCR_MDI         0x10

// Status word - upper byte of Status word
#define STATUS_CXTNO    0x8000
#define STATUS_FR       0x4000
#define STATUS_CNA      0x2000
#define STATUS_RNR      0x1000
#define STATUS_MDI      0x0800
#define STATUS_SWI      0x0400
#define STATUS_RESERVED 0x0200
#define STATUS_FCP      0x0100 // 82558/559 only

// Command word mask bits - upper byte of Command word
#define COMMAND_DONE        0x8000
#define COMMAND_RXDONE      0x4000
#define COMMAND_IDLE        0x2000
#define COMMAND_RXSUSPEND   0x1000
#define COMMAND_EARLYRX     0x0800
#define COMMAND_FLOWCTL     0x0400
#define COMMAND_TRIGINT     0x0200
#define COMMAND_MASKALL     0x0100

// Command word CU commands - bits 7-5 (23-20 dword)
#define COMMAND_CUNOP         0x0000
#define COMMAND_CUSTART       0x0010
#define COMMAND_CURESUME      0x0020
#define COMMAND_CUHPQSTRT     0x0030 // 82558/559 only
#define COMMAND_CUSTATSADDR   0x0040
#define COMMAND_CUSTATSSHOW   0x0050
#define COMMAND_CUCMDBASE     0x0060
#define COMMAND_CUSTATSDUMP   0x0070
#define COMMAND_CUSTATICRES   0x00A0 // 82558/559 only
#define COMMAND_CUHPQRESUME   0x00B0 // 82558/559 only

// Command word RU commands - bits 3-0 (18-16 dword)
#define COMMAND_RXNOP         0x0000
#define COMMAND_RXSTART       0x0001
#define COMMAND_RXRESUME      0x0002
#define COMMAND_RXABORT       0x0004
#define COMMAND_RXADDRLOAD    0x0006
#define COMMAND_RXRESNORS     0x0007

// Port interface opcodes
#define COMMAND_PORT_RESET          0
#define COMMAND_PORT_SELF_TEST      1
#define COMMAND_PORT_PARTIAL_RESET  2
#define COMMAND_PORT_DUMP           3
#define COMMAND_PORT_DUMP_WAKE      7 // 82559 only



#define RX_COMPLETE       0x8000
#define RX_OK             0x2000
#define RX_CRC_ERROR      0x0800
#define RX_ALIGN_ERROR    0x0400
#define RX_TOOBIG_ERROR   0x0200
#define RX_DMAOVRN_ERROR  0x0100
#define RX_TOOSHORT_ERROR 0x0080
#define RX_ETH2TYPE       0x0020
#define RX_NOMATCH        0x0004
#define RX_NOIAMATCH      0x0002

#define TX_UNDERRUN     0x1000ul
#define TXCNOOP            0x0ul
#define TX_COMPLETE 0x00008000ul
#define TXCSETIA       0x10000ul
#define TXCCFG         0x20000ul
#define TXCMCAST       0x30000ul
#define TXCXMIT        0x40000ul
#define TXCTDR         0x50000ul
#define TXCDUMP        0x60000ul
#define TXCDIAG        0x70000ul
#define TXCSUSP     0x40000000ul
#define TXCRESUME   0xBFFFFFFFul  // ~TXCSUSP
#define TXCINTR     0x20000000ul
#define TXCFLEX     0x00080000ul

#define STB_TX_GOOD_FRAMES  0
#define STB_COLL16_ERRS     1
#define STB_LATE_COLLS      2
#define STB_UNDERRUNS       3
#define STB_LOST_CARRIER    4
#define STB_DEFERRED        5
#define STB_ONE_COLLS       6
#define STB_MULTI_COLLS     7
#define STB_TOTAL_COLLS     8
#define STB_RX_GOOD_FRAMES  9
#define STB_CRC_ERRS        10
#define STB_ALIGN_ERRS      11
#define STB_RESOURCE_ERRS   12
#define STB_OVERRUN_ERRS    13
#define STB_COLLS_ERRS      14
#define STB_RUNT_ERRS       15
#define STB_DONE_MARKER     16

// PHY codes and capabilities
/* Read from the EEPROM, words 6 and 7 allow for 2 connected phy's. We will only
   deal with the primary phy. The phy address is the low byte of the word, and the
   PHY device ID is in the high byte bits 13-8. The upper bit indicates 10 base T
   only capability (ie-serial vs MII interface). The 82559 has an embedded
   82555 100 base tx PHY (code 0x07). If another PHY device is used, special mii
   requirements may be necessary. Our primary concern is detecting 10 base t only. */

#define TEN_BASET_ONLY 0x8000

#ifndef PHY_DEFAULT
   #define PHY_DEFAULT    0x07 /* Use this phy address when no eeprom is found */
#endif

#define I82559_OUTWORD(ADDR, VAL) OUTWORD((ADDR), (VAL))
#define I82559_INWORD(ADDR) INWORD((ADDR))
#define I82559_OUTDWORD(ADDR, VAL) OUTDWORD((ADDR), (VAL))
#define I82559_INDWORD(ADDR) INDWORD((ADDR))
#define I82559_OUTBYTE(ADDR, VAL) OUTBYTE((ADDR), (VAL))
#define I82559_INBYTE(ADDR) INBYTE((ADDR))

#define iface_to_i82559_softc(X) (PI82559_SOFTC)X->DriverData

typedef struct
{
    volatile dword status;
    dword link;
    dword buffer;
    dword count;
} RCVDSC;

typedef struct
{
    dword count;
    dword link;
    dword buffer;
    dword size;
} RCVBD;

typedef struct
{
    volatile dword status;
    dword link;
    dword pbuffer_address;
    dword count;
    dword buffer_address;
    dword buffer_length;
    dword buffer_address_1;
    dword buffer_length_1;
} TXDSC;

typedef struct _i82559_softc
{
    TXDSC       tx_descs[TX_RING_SIZE];
    RCVBD       rx_bd[RX_RING_SIZE];
    RCVDSC      rx_descs[RX_RING_SIZE];

    DCU         rx_dcus[RX_RING_SIZE];
    DCU         tx_dcus[TX_RING_SIZE];

    TXDSC     * plast_tx;       // Address of last tx or command desc send

    PIFACE      iface;
    dword       ia_iobase;
#define SETUP_FRAME_SIZE (CFG_MCLISTSIZE*6 + 10)
    byte *      i82559_setup;
    dword       i82559_stats[32];   // stats buffer for 559 only 17 are used.
#if USE_TIMER
    EBS_TIMER   timer_info;         // timer information
    dword       cur_ticks;          // incremented every second
    dword       last_rx_ticks;      // saved every time a packet is received
#endif
    int         rx_bug;             // 1 if rx hangs and needs resets if no traffic
    int         cur_rx;             // next rx ring entry to rcv
    int         last_tx_done;       // last tx entry with processing complete
    int         this_tx;            // next tx to use
    word        phy;                // primary phy from eeprom
    word        partner;
    int         flow_ctrl;
    dword       tx_threshold;
    dword       tx_control;
#if USE_TIMER
    dword       advertising;
#endif
    struct ether_statistics stats;
} I82559_SOFTC;
typedef struct _i82559_softc KS_FAR *PI82559_SOFTC;

// ********************************************************************
static RTIP_BOOLEAN i82559_open(PIFACE pi);
static RTIP_BOOLEAN i82559_statistics(PIFACE pi);
static RTIP_BOOLEAN i82559_setmcast(PIFACE pi);
static void i82559_close(PIFACE pi);
static RTIP_BOOLEAN i82559_xmit_done(PIFACE pi, DCU msg, RTIP_BOOLEAN success);
static int i82559_xmit(PIFACE pi, DCU msg);
static int link_status(PIFACE pi);
static void RTTMonSendPacket(void * DriverData, const BYTE * Data, DWORD Len);
static void RTTMonSendDone(void * DriverData, void * SendDesc);


static const rtpci_device_table pci_tab[] = {
 { "825593"   , RTPCI_V_ID_INTEL, 0x1030 },
 { "82801CAM" , RTPCI_V_ID_INTEL, 0x1031 },
 { "82801CAM" , RTPCI_V_ID_INTEL, 0x1032 },
 { "82801CAM" , RTPCI_V_ID_INTEL, 0x1033 },
 { "82801CAM" , RTPCI_V_ID_INTEL, 0x1034 },
 { "82562EH"  , RTPCI_V_ID_INTEL, 0x1035 },
 { "82562EH"  , RTPCI_V_ID_INTEL, 0x1036 },
 { "82801CAM" , RTPCI_V_ID_INTEL, 0x1037 },
 { "82801CAM" , RTPCI_V_ID_INTEL, 0x1038 },
 { "82562ET"  , RTPCI_V_ID_INTEL, 0x1039 },
 { "82562ET"  , RTPCI_V_ID_INTEL, 0x103A },
 { "82562EM"  , RTPCI_V_ID_INTEL, 0x103B },
 { "82562EM"  , RTPCI_V_ID_INTEL, 0x103C },
 { "82801DB"  , RTPCI_V_ID_INTEL, 0x103D },
 { "82801DB"  , RTPCI_V_ID_INTEL, 0x103E },
 { "82801EB"  , RTPCI_V_ID_INTEL, 0x1050 },
 { "82801EB"  , RTPCI_V_ID_INTEL, 0x1051 },
 { "82801EB"  , RTPCI_V_ID_INTEL, 0x1052 },
 { "82801EB"  , RTPCI_V_ID_INTEL, 0x1053 },
 { "82801EB"  , RTPCI_V_ID_INTEL, 0x1055 },
 { "82551QM"  , RTPCI_V_ID_INTEL, 0x1059 },
 { "82562EZ"  , RTPCI_V_ID_INTEL, 0x1064 },
 { "82562EZ"  , RTPCI_V_ID_INTEL, 0x1065 },
 { "82562EM"  , RTPCI_V_ID_INTEL, 0x1066 },
 { "82562EM"  , RTPCI_V_ID_INTEL, 0x1067 },
 { "82562ET"  , RTPCI_V_ID_INTEL, 0x1068 },
 { "82562EM"  , RTPCI_V_ID_INTEL, 0x1069 },
 { "82562GZ"  , RTPCI_V_ID_INTEL, 0x1092 },
 { "82562G"   , RTPCI_V_ID_INTEL, 0x1094 },
 { "82559ER"  , RTPCI_V_ID_INTEL, 0x1209 },
 { "82559"    , RTPCI_V_ID_INTEL, 0x1229 },
 { "82559VE"  , RTPCI_V_ID_INTEL, 0x2449 },
 { "82559B"   , RTPCI_V_ID_INTEL, 0x2459 },
 { "82559C "  , RTPCI_V_ID_INTEL, 0x245D },
 { "82562V"   , RTPCI_V_ID_INTEL, 0x27DC },
 {0}
};

static const byte i82558_config_cmd[] =
{
    22, 0x88, 0, 1, 0, 0, 0x22, 0x03, 1, 0,
    0x2E, 0, 0x60, 0x08, 0x88, 0x68, 0, 0x40, 0xF2, 0x80,
    0x31, 0x05
};

static EDEVTABLE KS_FAR i82559_device =
{
    i82559_open, i82559_close, i82559_xmit, i82559_xmit_done,
    NULLP_FUNC, i82559_statistics, i82559_setmcast,
    I82559_DEVICE, "I82559", MINOR_0, ETHER_IFACE,
    SNMP_DEVICE_INFO(CFG_OID_I82559, CFG_SPEED_I82559)
    CFG_ETHER_MAX_MTU, CFG_ETHER_MAX_MSS,
    CFG_ETHER_MAX_WIN_IN, CFG_ETHER_MAX_WIN_OUT,
    IOADD(0x300), EN(0), EN(5)
};


// ********************************************************************
static void i82559_resume(PI82559_SOFTC sc);
static int i82559_read_eeprom(IOADDRESS ioaddr, PFWORD pbuf);
static RTIP_BOOLEAN i82559_init_rcv_ring(PI82559_SOFTC sc);
static void i82559_timeout (void KS_FAR *vsc);
static int i82559_interrupt(PI82559_SOFTC sc);
static void i82559_set_rcv_mode(PI82559_SOFTC sc);
static word i82559_mdio_read(PI82559_SOFTC sc, int offset);
static void i82559_mdio_write(PI82559_SOFTC sc, int offset, word value);
static void i82559_rcv_ring(PI82559_SOFTC sc);
static void i82559_reset_rcv_ring(PI82559_SOFTC sc);

static void WaitCmdDone(dword iobase)
{
    int i;

    for (i=0; i<100; i++)
       if (I82559_INBYTE(iobase + SCR_COMMAND) == 0)
           return;
}

// ********************************************************************

/**********/
PIFACE save_pi;
DCU my_dcu;
/************/

static RTIP_BOOLEAN i82559_open(PIFACE pi)
{
    int i;
    dword PCILocation;
    PFWORD pbuf;
    PI82559_SOFTC sc;

	/**********/
	save_pi = pi;
	my_dcu = os_alloc_packet(ETHERSIZE, DRIVER_ALLOC);
	/***********/

    #define RTPCI_INTEL_REG_IOBASE  0x14

    if (rtpci_LocateDevice(pci_tab, pi, RTPCI_INTEL_REG_IOBASE, &PCILocation) == -1)
    {
        set_errno(EPROBEFAIL);
        return FALSE;
    }

    if (pi->DriverData == NULL)
        pi->DriverData = PhysMalloc(sizeof(*sc), PACKET_POOL_MALLOC);

    sc = iface_to_i82559_softc(pi);
    tc_memset(sc, 0, sizeof(*sc));
    pi->driver_stats.ether_stats = &sc->stats;
    sc->iface = pi;
    sc->ia_iobase = pi->io_address;
    pi->GetLinkStatus = link_status;

    sc->i82559_setup = PhysMalloc(SETUP_FRAME_SIZE, I82559_MALLOC);
    sc->plast_tx = 0;
    sc->this_tx = 0;
    sc->last_tx_done = 0;

    // Read the eeprom setup data.

    pbuf = (PFWORD) dcu_alloc_core(1024);
    i = i82559_read_eeprom((IOADDRESS)sc->ia_iobase, pbuf);
    if (i <= 0)
    {
       DEBUG_ERROR("i82559: No eeprom detected or reading eeprom failed", 0 , 0, 0);
       sc->phy = PHY_DEFAULT;
       sc->rx_bug = 0;
    }
    else
    {
       sc->phy = pbuf[6];

       /* word 3 low byte contains an indication of wether the multicast setup
          workaround can be omitted at 100 or 10 Mbps (bit set == omit workaround) */
       sc->rx_bug = (pbuf[3] & 0x03) == 3 ? 0 : 1;
    }

    // Words 0 - 3 contain the ethernet address
    memcpy(pi->addr.my_hw_addr, pbuf, 6);
    rtpci_MacAdrValid(pi);  // this function generates a MAC address if the current one is invalid

    dcu_free_core((PFBYTE) pbuf);

    /* reset the device. This will cause the device to require a complete
       reinitialization   */
    I82559_OUTDWORD(sc->ia_iobase + SCR_PORT, COMMAND_PORT_RESET);

    // hook the interrupt service routine
    RTKSetIRQStack(pi->irq_val, 1024);
    RTInstallSharedIRQHandlerEx(pi->irq_val, (RTKIRQHandlerEx)i82559_interrupt, sc);

    if (!i82559_init_rcv_ring(sc))
    {
        DEBUG_ERROR("init_rcv_ring fails == ", DINT1 , (dword) sc, 0);
        PhysFree(sc, sizeof(*sc), 1);
        return(FALSE);
    }

    // Call resume to start things off. Does a complete chip setup
    i82559_resume(sc);

    i82559_set_rcv_mode(sc);

#if USE_TIMER
    if ((sc->phy & TEN_BASET_ONLY) == 0)
        sc->advertising = i82559_mdio_read(sc, 4);

    // Set up a timer to run every three seconds
    sc->cur_ticks = sc->last_rx_ticks = 0;  // watchdog for hung receiver
    sc->timer_info.func = i82559_timeout;   // routine to execute every three seconds
    sc->timer_info.arg = (void KS_FAR *)sc;
    ebs_set_timer(&sc->timer_info, 3, TRUE);
    ebs_start_timer(&sc->timer_info);
#endif

    // No need to wait for the command unit to accept here.
    if ((sc->phy & TEN_BASET_ONLY) == 0)
        i82559_mdio_read(sc, 0);

    // Tell RTTarget-32 Monitor about this
    _rttMonIFaceInstall(sc, PCILocation, RTTMonSendPacket, RTTMonSendDone);

    return TRUE;
}

// ********************************************************************
static void i82559_resume(PI82559_SOFTC sc)
{
    TXDSC * pt;
    TXDSC * ptn;

    // mask off all interrupts while we work
    I82559_OUTWORD((sc->ia_iobase+SCR_COMMAND), COMMAND_MASKALL);

    // reset tx threshhold to initial value
    sc->tx_threshold = TX_THRESHOLD;

    // Make up a tx control word (offset 0x0c in TCB) with current tx threshhold and flags
    sc->tx_control = (sc->tx_threshold << 16) | TX_TBDNUMBER | TX_EOF;

    // Set the device internal RU base register to 0.
    WaitCmdDone(sc->ia_iobase);
    I82559_OUTDWORD((sc->ia_iobase + SCR_POINTER), 0);
    I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_RXADDRLOAD | COMMAND_MASKALL);
    WaitCmdDone(sc->ia_iobase);

    // Set the device internal CU base register to 0.
    I82559_OUTWORD((sc->ia_iobase+SCR_COMMAND), COMMAND_CUCMDBASE | COMMAND_MASKALL);
    WaitCmdDone(sc->ia_iobase); //  wait_for_cmd_done(ioaddr + SCBCmd);

    // Load the address of a block to dump counters.
    I82559_OUTDWORD((sc->ia_iobase + SCR_POINTER), (dword) sc->i82559_stats);
    I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_CUSTATSADDR | COMMAND_MASKALL);
    sc->i82559_stats[STB_DONE_MARKER] = 0;
    WaitCmdDone(sc->ia_iobase);

    // Start the RU by loading the address of the rx descriptors.
    I82559_OUTDWORD((sc->ia_iobase + SCR_POINTER), (dword) (sc->rx_descs + sc->cur_rx));
    I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_RXSTART | COMMAND_MASKALL);
    WaitCmdDone(sc->ia_iobase);

    // Do an initial dump of the stats counters
    I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_CUSTATSDUMP | COMMAND_MASKALL);

    /* Format and execute an Individual Address Setup command.
       Fill the first command with our physical address.     */
    pt  = sc->tx_descs + sc->this_tx;
    sc->this_tx = (sc->this_tx + 1) & TX_RING_MASK;
    ptn = sc->tx_descs + sc->this_tx;

    pt->status = TXCSUSP | TXCSETIA | 0xA000;
    pt->link   = (dword)ptn;
    tc_movebytes(&pt->pbuffer_address, sc->iface->addr.my_hw_addr, 6);
    if (sc->plast_tx)
        sc->plast_tx->status &= ~TXCSUSP;
    sc->plast_tx = pt;

    // Start the chip's Tx process and unmask interrupts.
    WaitCmdDone(sc->ia_iobase);
    I82559_OUTDWORD((sc->ia_iobase + SCR_POINTER), (dword) (sc->tx_descs + sc->last_tx_done));
    I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_CUSTART | COMMAND_FLOWCTL);
}

#if USE_TIMER
// ********************************************************************
static void i82559_timeout(void KS_FAR *vsc)
{
    PI82559_SOFTC sc = (PI82559_SOFTC) vsc;
    word partner;
    int flow_ctrl;
    int doreload = 0;

    // We have MII and lost link beat.
    if (!(sc->phy & TEN_BASET_ONLY))
    {
        partner = i82559_mdio_read(sc, 5);
        if (partner != sc->partner)
        {
            flow_ctrl = sc->advertising & partner & 0x0400 ? 1 : 0;
            sc->partner = partner;
            if (flow_ctrl != sc->flow_ctrl)
            {
                DEBUG_ERROR("i82559 - will reload cause lost link beat", NOVAR, 0, 0);
                sc->flow_ctrl = flow_ctrl;
                doreload = 1;
            }

            // Clear sticky bit.
            i82559_mdio_read(sc, 1);

            // If link beat has returned...
            if (!(i82559_mdio_read(sc, 1) & 0x0004))
            {
                DEBUG_ERROR("i82559 - lost link beat", NOVAR, 0, 0);
            }
        }
    }

    sc->cur_ticks += 1;
    if (sc->rx_bug && ((sc->cur_ticks - sc->last_rx_ticks) > 4))
    {
        sc->last_rx_ticks = sc->cur_ticks;
        doreload = 1;
        DEBUG_ERROR("i82559 - No pkt > 4 secs reset RCV", NOVAR, 0, 0);
    }

    if (doreload)
        i82559_set_rcv_mode(sc);

    ebs_start_timer(&sc->timer_info);
}
#endif

// ********************************************************************
static RTIP_BOOLEAN i82559_statistics(PIFACE pi)                       //__fn__
{
    PETHER_STATS p;
    PI82559_SOFTC sc = iface_to_i82559_softc(pi);

   p = (PETHER_STATS) &(sc->stats);
   UPDATE_SET_INFO(pi,interface_packets_in, p->packets_in)
   UPDATE_SET_INFO(pi,interface_packets_out, p->packets_out)
   UPDATE_SET_INFO(pi,interface_bytes_in, p->bytes_in)
   UPDATE_SET_INFO(pi,interface_bytes_out, p->bytes_out)
   UPDATE_SET_INFO(pi,interface_errors_in, p->errors_in)
   UPDATE_SET_INFO(pi,interface_errors_out, p->errors_out)
   UPDATE_SET_INFO(pi,interface_packets_lost, p->packets_lost)
   return(TRUE);
}

/* Set or clear the multicast filter for this adaptor.
   This is very ugly with Intel chips -- we usually have to execute an
   entire configuration command, plus process a multicast command.
   This is complicated.  We must put a large configuration command and
   an arbitrarily-sized multicast command in the transmit list.
   To minimize the disruption -- the previous command might have already
   loaded the link -- we convert the current command block, normally a Tx
   command, into a no-op and link it to the new command.
*/
// promiscuous
// #define RX_MODE 3
// all multicast
// #define RX_MODE 1

#define RX_MODE 0   // Include multicasts in table

// ********************************************************************
static void i82559_set_rcv_mode(PI82559_SOFTC sc)
{
    TXDSC * pt;
    TXDSC * plast_tx;
    TXDSC * pset;
    PFWORD pparms;
    PFWORD paddrs;
    PFBYTE pdata;
    word w;
    int i;
    int iface_no = sc->iface - ifaces;

    // mask off all interrupts while we work
    // I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_MASKALL);

    pt = sc->tx_descs + sc->this_tx;
    sc->tx_dcus[sc->this_tx] = 0;       // We're not xmitting.

    sc->this_tx = (sc->this_tx + 1) & TX_RING_MASK;

    plast_tx = sc->plast_tx;
    sc->plast_tx = pt;
    pt->status = TXCCFG | TXCSUSP;
    pt->link = (dword)(sc->tx_descs + sc->this_tx);
    pdata = (PFBYTE)&pt->pbuffer_address;

    // Construct a full CmdConfig frame.
    tc_movebytes(pdata, i82558_config_cmd, sizeof(i82558_config_cmd));

    if (sc->phy & TEN_BASET_ONLY)
    {           // Use the AUI port instead.
        pdata[15] |= 0x80;
        pdata[8] = 0;
    }

    if (iface_no < CFG_ETHERNET_MODE_SIZE)
       switch (CFG_ETHERNET_MODE[iface_no])
       {
           case 10:
           case 100:
              pdata[19] = 0;
              break;
           case 20:
           case 200:
              pdata[19] = 1ul << 6;
              break;
       }

    // Trigger the command unit resume.
    WaitCmdDone(sc->ia_iobase);
    plast_tx->status &= ~TXCSUSP;
    I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_CURESUME | COMMAND_FLOWCTL);

    /* Since this is a long frame, we've probably written into the next descriptors. Wait a
       while for the frame to be read */
    //PP: we did not overwrite the next descriptor
    //PP: ks_sleep((word)(ks_ticks_p_sec() / 5));

    // Set up the multicast list
    pset = (TXDSC*) sc->i82559_setup;
    pparms = (PFWORD) &pset->pbuffer_address;
    paddrs = (PFWORD) &sc->iface->mcast.mclist_eth[0];
    w = (word) (sc->iface->mcast.lenmclist*6);
    *pparms++ = w;

    // Copy the multicast addresses as word from the multicast table
    for (i = 0; i < sc->iface->mcast.lenmclist*3; i++)
        *pparms++ = *paddrs++;

    sc->tx_dcus[sc->this_tx] = 0;       // We're not xmitting.
    plast_tx = sc->plast_tx;
    sc->plast_tx = pset;
    plast_tx->link = (dword) (sc->tx_descs + sc->this_tx);
    sc->tx_descs[sc->this_tx].status = TXCNOOP;
    sc->tx_descs[sc->this_tx].link = (dword) pset;
    pset->status = TXCMCAST | TXCSUSP | TXCINTR;

    sc->this_tx = (sc->this_tx + 1) & TX_RING_MASK;

    pset->link = (dword) (sc->tx_descs + sc->this_tx);

    WaitCmdDone(sc->ia_iobase);
    plast_tx->status &= ~TXCSUSP;
    I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_CURESUME | COMMAND_FLOWCTL);

    if (iface_no < CFG_ETHERNET_MODE_SIZE)
       switch (CFG_ETHERNET_MODE[iface_no])
       {
           case 0:
              i82559_mdio_write(sc, 0, 1u << 12);
              break;
           case 10:
              i82559_mdio_write(sc, 0, 0u);
              break;
           case 20:
              i82559_mdio_write(sc, 0, 1u << 8);
              break;
           case 100:
              i82559_mdio_write(sc, 0, 1u << 13);
              break;
           case 200:
              i82559_mdio_write(sc, 0, (1u << 8) | (1u << 13));
              break;
       }
}

// ********************************************************************
static RTIP_BOOLEAN i82559_setmcast(PIFACE pi)      // __fn__
{
    i82559_set_rcv_mode(iface_to_i82559_softc(pi));
    return(TRUE);
}

// ********************************************************************
static void i82559_close(PIFACE pi)                     //__fn__
{

	/*************/
	os_free_packet(my_dcu);
	/************/

    int i;
    PI82559_SOFTC sc = iface_to_i82559_softc(pi);

    // mask off all interrupts
    I82559_OUTWORD(sc->ia_iobase+SCR_COMMAND, COMMAND_MASKALL);

    // reset the device
    I82559_OUTDWORD(sc->ia_iobase+SCR_PORT, COMMAND_PORT_RESET);

    RTRemoveSharedIRQHandlerEx(pi->irq_val, (RTKIRQHandlerEx)i82559_interrupt, sc);

   _rttMonIFaceUninstall(sc);

    PhysFree(sc->i82559_setup, SETUP_FRAME_SIZE, sizeof(byte));

    // free RX ring buffer
    for (i = 0; i < RX_RING_SIZE; i++)
        if (sc->rx_dcus[i])
            os_free_packet(sc->rx_dcus[i]);

    PhysFree(sc, sizeof(*sc), 1);
    pi->DriverData = NULL;
}

// ********************************************************************
static RTIP_BOOLEAN i82559_xmit_done(PIFACE pi, DCU msg, RTIP_BOOLEAN success)
{
    word status;
    PI82559_SOFTC sc = iface_to_i82559_softc(pi);

    if (success)
    {
        // Update total number of successfully transmitted packets.
        sc->stats.packets_out++;
        sc->stats.bytes_out += DCUTOPACKET(msg)->length;
    }
    else
    {
        // error - record statistics
        sc->stats.errors_out++;
        sc->stats.tx_other_errors++;
        status = I82559_INWORD((sc->ia_iobase + SCR_STATUS));
        if ((status & 0x00C0) != 0x0080 && (status & 0x003C) == 0x0010)
        {
            // Only the command unit has stopped.
            I82559_OUTDWORD((sc->ia_iobase + SCR_POINTER), (dword) (sc->tx_descs + sc->last_tx_done));
            I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_CUSTART | COMMAND_FLOWCTL);
        }
        else
        {
            // Reset the Tx and Rx units.
            I82559_OUTDWORD((sc->ia_iobase + SCR_PORT), COMMAND_PORT_RESET);
            ks_sleep(1);  // delay at least 10 microsecond
            i82559_resume(sc);
        }
    }
    return(TRUE);
}

static void do_send(PI82559_SOFTC sc, const void * Data, dword length, DCU msg, unsigned custom)
{
    TXDSC * pt;
    TXDSC * plast_tx;

    if (length < ETHER_MIN_LEN)
       length = ETHER_MIN_LEN;

    _rttMonLockSend(sc);

    pt = sc->tx_descs + sc->this_tx;

    if (!custom && !msg)
       _rttMonSetSendDesc(sc, pt);

    sc->tx_dcus[sc->this_tx] = msg;

    // Try interrupt after compete too
    pt->status              = TXCSUSP | TXCFLEX | TXCXMIT | TXCINTR;

    // Point to the next TX DESC
    sc->this_tx             = (sc->this_tx + 1) & TX_RING_MASK;
    pt->link                = (dword) (sc->tx_descs + sc->this_tx);
    pt->count               = sc->tx_control;
    pt->buffer_address      = (dword) Data;
    pt->pbuffer_address     = (dword) (&pt->buffer_address);
    pt->buffer_length       = length;

    // Resume processing
    plast_tx          = sc->plast_tx;
    sc->plast_tx      = pt;
    plast_tx->status &= ~TXCSUSP;

    WaitCmdDone(sc->ia_iobase);
    I82559_OUTWORD((sc->ia_iobase+SCR_COMMAND), COMMAND_CURESUME | COMMAND_FLOWCTL);

    _rttMonUnlockSend(sc);
}


// ********************************************************************
static int i82559_xmit(PIFACE pi, DCU msg)
{
    do_send(iface_to_i82559_softc(pi), DCUTODATA(msg), DCUTOPACKET(msg)->length, msg, 0);
    return 0;
}

/* ********************************************************************   */
static void RTTMonSendDone(void * DriverData, void * SendDesc)
{
    int i;
    TXDSC * pt;

    if (SendDesc == NULL)
       return;

    pt = SendDesc;

    // wait until it is sent out
    for (i=0; i<100000; i++)
       if ((pt->status & TX_COMPLETE))
          return;
       else
          RTIn(0x80);

    if (i >= 1000000)
        RTBCDisplayString("RTIP/i82559 failed to send packet\n");
}

/* ********************************************************************   */
static void RTTMonSendPacket(void * DriverData, const BYTE * Data, DWORD Len)
{
   do_send(DriverData, Data, Len, NULL, 0);
}



/*******************************************************************************/
void SendFrameRaw(DWORD Len) {
	extern volatile int dcus_cnt;
	dcus_cnt++;

	do_send(iface_to_i82559_softc(save_pi), DCUTODATA(my_dcu), Len, NULL, 1);
}

BYTE* GetFrameBuf() { return DCUTODATA(my_dcu); }
/*******************************************************************************/



// ********************************************************************
static int i82559_interrupt(PI82559_SOFTC sc)
{
    dword status;
    dword tx_status;

    // Get the interrupt reason and acknowledge
    status = I82559_INWORD((sc->ia_iobase+SCR_STATUS));
    if (status == 0)
        return 0;
    I82559_OUTWORD((sc->ia_iobase + SCR_STATUS),(status & 0xfd00));
    if (status & 0x4000)     // Packet received.
        i82559_rcv_ring(sc);
    if (status & 0x1000)     // RCU stopped, must restart
    {
        i82559_reset_rcv_ring(sc);
        sc->cur_rx = 0;
        I82559_OUTDWORD((sc->ia_iobase + SCR_POINTER), (dword) (sc->rx_descs + sc->cur_rx));
        I82559_OUTWORD((sc->ia_iobase + SCR_COMMAND), COMMAND_RXSTART | COMMAND_FLOWCTL);
        DEBUG_ERROR("I82559 RCV ring reset, error code: ", DINT1, status & 0x003c, 0);
        sc->stats.errors_in++;
    }
    // User interrupt, Command/Tx unit interrupt or CU not active.
    if (status & 0xA400)
        while (sc->last_tx_done != sc->this_tx)
        {
            tx_status = sc->tx_descs[sc->last_tx_done].status;
            if (!(tx_status & TX_COMPLETE)) // Check for complete
                break;
            if (tx_status & TX_UNDERRUN)
            {
                if (sc->tx_threshold < 0xE0)
                    sc->tx_threshold += 1;
                sc->tx_control = ((sc->tx_threshold << 16) | TX_TBDNUMBER | TX_EOF);
            }
            if (sc->tx_dcus[sc->last_tx_done])
                ks_invoke_output(sc->iface, 1);
            sc->last_tx_done = (sc->last_tx_done + 1) & TX_RING_MASK;
        }

    return 1;
}

// ********************************************************************
static word i82559_mdio_read(PI82559_SOFTC sc, int offset)
{
    dword l1,l2,l3;
    dword v;
    int i;

    l1 = (dword)offset; l1 <<= 16;
    l2 = (dword) (sc->phy & 0x1f); l2 <<= 21;
    l3 = 0x08000000ul|l1|l2;
    I82559_OUTDWORD(sc->ia_iobase + SCR_MDI,l3);
    for (i = 0; i < 1000; i++)
    {
        v = I82559_INDWORD(sc->ia_iobase + SCR_MDI);
        if (v & 0x10000000ul)
            break;
    }
    if (i == 1000)
    {
        DEBUG_ERROR("i82559_mdio_read failed", NOVAR, 0, 0);
    }
    return (word)(v & 0xffff);
}


// ********************************************************************
static void i82559_mdio_write(PI82559_SOFTC sc, int offset, word value)
{
    int i;

    I82559_OUTDWORD(sc->ia_iobase + SCR_MDI, 0x04000000ul | ((sc->phy & 0x1f) << 21) | (offset << 16) | value);
    for (i = 0; i < 1000; i++)
        if (I82559_INDWORD(sc->ia_iobase + SCR_MDI) & 0x10000000ul)
            break;
    if (i == 1000)
    {
        DEBUG_ERROR("i82559_mdio_write failed", NOVAR, 0, 0);
    }
}


// =================================================================
// =================================================================
// Input ring buffer management code
// =================================================================
// =================================================================

// Initialize the receive ring buffer
static RTIP_BOOLEAN i82559_init_rcv_ring(PI82559_SOFTC sc)
{
    int i, j;

//   disable the cache of the page(s) holding the descriptors
//   this is only required on buggy hardware
//   RTSetPageFlags(sc, 0x10, 0x10);
//   RTSetPageFlags(sc->rx_descs, 0x10, 0x10);

    // allocate the ring buffers
    for (i = 0; i < RX_RING_SIZE; i++)
    {

        // for the flex memory model, allocate the DCU and format the bd list
        sc->rx_dcus[i] = os_alloc_packet_input(ETHERSIZE, DRIVER_ALLOC);

        sc->rx_bd[i].count  = 0ul;
        sc->rx_bd[i].link   = (dword) (sc->rx_bd + i + 1);
        sc->rx_bd[i].buffer = (dword) DCUTODATA(sc->rx_dcus[i]);
        sc->rx_bd[i].size   = ETHERSIZE;

        // format a parallel rfd list
        sc->rx_descs[i].status = 0x00080000ul;
        sc->rx_descs[i].link   = (dword) (sc->rx_descs + i + 1);
        sc->rx_descs[i].buffer = 0xFFFFFFFFul;
        sc->rx_descs[i].count  = 0ul;

        if (!sc->rx_dcus[i])
        {
            DEBUG_ERROR("i82559: Failure allocating RX DCUs", 0, 0, 0);
            for (j = 0; j < i; j++)
                os_free_packet(sc->rx_dcus[j]);
            return(FALSE);
        }
    }
    // terminate the list of bd's and format the RFD
    sc->rx_bd[RX_RING_SIZE-1].link      = (dword) (&sc->rx_bd);

    sc->rx_descs[0].buffer              = (dword) (&sc->rx_bd);
    sc->rx_descs[RX_RING_SIZE-1].status = 0x40080000ul;
    sc->rx_descs[RX_RING_SIZE-1].link   = (dword) (&sc->rx_descs);

    sc->cur_rx = 0;

    return(TRUE);
}

// ********************************************************************
// Check status and receive data from the ring buffer (called from ISR)
static void i82559_rcv_ring(PI82559_SOFTC sc)
{
    RCVDSC * pthis;
    RCVBD * pthisbd;
    dword status;
    dword length;
    DCU  msg, invoke_msg;

    while (1)
    {
        pthis   = sc->rx_descs + sc->cur_rx;
        pthisbd = sc->rx_bd + sc->cur_rx;
        status  = pthis->status;

        if (!(status & RX_COMPLETE)) // quit when we have no more to process
        {
#if KONTRON_BUG_WORKAROUND
            // have a look at the next buffer
            RCVDSC * pnext = sc->rx_descs[(sc->cur_rx + 1) & RX_RING_MASK];
            if (!(pnext->status & RX_COMPLETE))
                break;
            else
            {
                DEBUG_ERROR("I82559 RECOVER SKIPPED FRAME", 0, 0, 0);
                status = RX_OK;
            }
#else
            break;
#endif
        }

        if (status & RX_OK)
        {
            // We've got a good packet
            if (!_rttMonForMonitor(sc, (const void*) pthisbd->buffer))
            {
                length = pthisbd->count & 0x3fff;

                invoke_msg = NULL;
    #if RX_COPY_BREAK > 0
                if (length <= RX_COPY_BREAK)
                {
                    invoke_msg = os_alloc_packet_input(length, DRIVER_ALLOC);
                    if (invoke_msg)
                        tc_movebytes(DCUTODATA(invoke_msg), pthisbd->buffer, length);
                }
                else
    #endif // RX_COPY_BREAK > 0
                {
                    msg = os_alloc_packet_input(ETHERSIZE, DRIVER_ALLOC);
                    if (msg)
                    {
                        // Put the new one in the ring and invoke the old
                        invoke_msg = sc->rx_dcus[sc->cur_rx];
                        sc->rx_dcus[sc->cur_rx] = msg;
                        pthisbd->buffer = (dword) DCUTODATA(sc->rx_dcus[sc->cur_rx]);
                   }
                }
                if (invoke_msg)
                {
                   sc->stats.packets_in++;
                   sc->stats.bytes_in += length - sizeof(struct _ether);
                   DCUTOPACKET(invoke_msg)->length = length;
                   ks_invoke_input(sc->iface, invoke_msg);
				   /********************/
				   
				   extern EPACKET dcus[10];
				   extern volatile int dcus_cnt;
				   extern char dcu_data[10][1600];
				   if (dcus_cnt < 10) {
					   memcpy(&dcus[dcus_cnt], invoke_msg, sizeof(EPACKET));
					   memcpy(&dcu_data[dcus_cnt][0], (const void*)pthisbd->buffer, length);
					   dcus_cnt++;
				   }
				   
				   /*********************/
                }
                else
                {
                    DEBUG_ERROR("I82559 RCV ALLOC FAILED", NOVAR, 0, 0);
                }
            }
        }
        else // !RX_OK - We have some sort of error, record the statistic
        {
            DEBUG_ERROR("I82559 RCV ERROR == ", EBS_INT1, status, 0);
            if (status & RX_CRC_ERROR)
                sc->stats.rx_crc_errors++;
            else if (status & RX_ALIGN_ERROR)
                sc->stats.rx_frame_errors++;
            else if (status & RX_TOOBIG_ERROR)
                sc->stats.rx_frame_errors++;
            else if (status & RX_DMAOVRN_ERROR)
                sc->stats.rx_fifo_errors++;
            else if (status & RX_TOOSHORT_ERROR)
                sc->stats.rx_frame_errors++;
            else
                sc->stats.rx_other_errors++;
            sc->stats.errors_in++;
        }

        // We have to rearm put the descriptor on the end of the ring now
        pthis->status = 0x40080000ul;     // end of list

        // Clear last marker of previous descriptor
        ((BYTE *)&(sc->rx_descs[(sc->cur_rx - 1) & RX_RING_MASK].status))[3] = 0;

#if USE_TIMER
        sc->last_rx_ticks = sc->cur_ticks;  // pulse keepalive
#endif
        sc->cur_rx = (sc->cur_rx + 1) & RX_RING_MASK;   // Add & wrap to 0
    }
}

static void i82559_reset_rcv_ring(PI82559_SOFTC sc)
{
    int i;

    for (i = 0; i < RX_RING_SIZE-1; i++)
        sc->rx_descs[i].status = 0x00080000ul;
    sc->rx_descs[i].status = 0x40080000ul; // end of list
}

// =================================================================
// =================================================================
// eeprom code
// =================================================================
// =================================================================

//  EEPROM control bits.
#define WRITE_ZERO          0x4802
#define WRITE_ONE           0x4806
#define WRITE_ZERO_CLOCK    0x4803
#define WRITE_ONE_CLOCK     0x4807

static dword do_eeprom_cmd(IOADDRESS io_address, dword cmd, int cmd_len)
{
    dword retval = 0;
    dword ltemp;
    dword stemp;
    int i;

    io_address = io_address + SCR_EEPROM;
    // reverse the bits
    ltemp = cmd;
    cmd = 0;
    for (i = 0; i < cmd_len; i++)
    {
        cmd <<= 1;
        if (ltemp & 0x01)
            cmd |= 1;
        ltemp >>= 1;
    }

    // Initiate the sequence
    I82559_OUTWORD(io_address, WRITE_ZERO_CLOCK);
    // Send the command serially and read the results
    for (i = 0; i < cmd_len; i++)
    {
        if (cmd & 1)
            I82559_OUTWORD(io_address, WRITE_ONE);
        else
            I82559_OUTWORD(io_address, WRITE_ZERO);
        I82559_INWORD(io_address);              // just a delay
        if (cmd & 1)
            I82559_OUTWORD(io_address, WRITE_ONE_CLOCK);
        else
            I82559_OUTWORD(io_address, WRITE_ZERO_CLOCK);
        I82559_INWORD(io_address);              // just a delay
        cmd >>= 1;
        stemp = I82559_INWORD(io_address);
        retval = (retval << 1);
        if (stemp & 0x08)
            retval |= 1;
    }
    // Terminate the sequence
    I82559_OUTWORD(io_address,WRITE_ZERO);
    I82559_OUTWORD(io_address,0x4800);
    return retval;
}

// ********************************************************************
// Returns the size of the eeprom or 0 if not found, -1 for eeprom error
static int i82559_read_eeprom(IOADDRESS ioaddr, PFWORD pbuf)
{
    word checksum = 0;
    int i;
    dword read_cmd;
    dword ltemp;
    dword lsize;
    int  size;

    ltemp = 6;
    ltemp <<= 24;
    lsize = do_eeprom_cmd(ioaddr, ltemp, 28);
    if (lsize == 0xfffffff)
     return (0); // no eeprom found
    else if ((lsize & 0xffe0000)  == 0xffe0000)
        size = 0x100;
    else
    {
        size = 0x40;
        ltemp = 6;
        ltemp <<= 22;
    }
    read_cmd = ltemp;

    for (i = 0; i < size; i++)
    {
        ltemp = i; ltemp <<= 16;
        ltemp |= read_cmd;
        *pbuf = (word) do_eeprom_cmd(ioaddr, read_cmd | ltemp, 28);
        checksum += *pbuf++;
    }
    if (checksum != 0xBABA)
    {
        DEBUG_ERROR("Invalid EEPROM checksum", NOVAR, 0, 0);
//        return(-1);
    }
    return(size);
}

//-----------------------------------
static int link_status(PIFACE pi)
{
   int result;
   PI82559_SOFTC sc = iface_to_i82559_softc(pi);
   byte status = I82559_INBYTE(sc->ia_iobase + 0x1D);

   if (!(status & (1<<0)))
      return 0; // link is down
   result = (status & (1<<1)) ? 100 : 10;
   if (status & (1<<2))
      result *= 2;
   return result;
}

// ********************************************************************
// API
// ********************************************************************
int xn_bind_i82559(int minor_number)
{
    return xn_device_table_add(&i82559_device, minor_number);
}

// #define INCLUDE_ADDMCASTADDR

#ifdef INCLUDE_ADDMCASTADDR
int i82559_AddMCastAddr(int iface_no, const byte * EtherMCastAddress)
{
   PIFACE pi = tc_ino2_iface(iface_no, TRUE);

   if (!pi)
      return SOCKET_ERROR;

   if (tc_device_id(iface_no) != I82559_DEVICE)
      return SOCKET_ERROR;

   if (pi->mcast.lenmclist >= CFG_MCLISTSIZE)
       return set_errno(EMCASTFULL);

   OS_CLAIM_IFACE(pi, MCAST1_CLAIM_IFACE);
   memcpy(pi->mcast.mclist_eth + pi->mcast.lenmclist*ETH_ALEN, EtherMCastAddress, ETH_ALEN);
// memcpy(pi->mcast.mclist_ip  + pi->mcast.lenmclist*IP_ALEN , IPMCastAddress,    IP_ALEN);
   pi->mcast.mcast_cnt[pi->mcast.lenmclist] = 1;
   pi->mcast.lenmclist++;
   i82559_setmcast(pi);
   OS_RELEASE_IFACE(pi);
   return 0;
}
#endif

#endif
