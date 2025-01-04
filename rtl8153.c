// Extremely primitive RTL8153 driver, specifically using CDC-ECM mode (Ethernet Control Mode)
// Please note that this does only the BARE MINIMUM to make it work.
//

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <libusb-1.0/libusb.h>

#include "netinterface.h"

int rtl8153_nr;
libusb_context *rtl8153_usb_context = NULL;

// For the ubiquitous RTL8153 gigabit ethernet adapter.
// 0bda:8153

#define RTL8153_VID 0x0bda
#define RTL8153_PID 0x8153
#define CDC_CONFIG_MODE 2
#define USB_BUFFERS 32
#define BUFFERSIZE 1024
#define IN_ENDPOINT 0x81
#define OUT_ENDPOINT 0x2

typedef struct netinterface_rtl8153_t
{
	int (*recv_cb)( struct netinterface_t * iface, uint8_t * packet, int packetlen );
	int (*media_change_cb)( struct netinterface_t * iface, enum netinterface_rate rate, enum netinterface_duplex duplex );
	void * opaque;
	void (*teardown)( struct netinterface_t * iface );
	int (*poll)( struct netinterface_t * iface, int microseconds_to_poll );
	int (*send)( struct netinterface_t * iface, uint8_t * packet, int packetlen );
	int fault;
	char * descriptor;
	libusb_device_handle * device_handle;
	struct libusb_transfer * xfers_in[USB_BUFFERS];
	struct libusb_transfer * xfers_out[USB_BUFFERS];
	uint8_t buffer_in[USB_BUFFERS][BUFFERSIZE] __attribute__((aligned(128)));
	uint8_t buffer_out[USB_BUFFERS][BUFFERSIZE] __attribute__((aligned(128)));
} netinterface_rtl8153;


//////////////////////////////////////////////////////////////////////////////
#if 0
/* 1000BASE-T Control register */
#define RTL_ADVERTISED_10_HALF			BIT(0)
#define RTL_ADVERTISED_10_FULL			BIT(1)
#define RTL_ADVERTISED_100_HALF			BIT(2)
#define RTL_ADVERTISED_100_FULL			BIT(3)
#define RTL_ADVERTISED_1000_HALF		BIT(4)
#define RTL_ADVERTISED_1000_FULL		BIT(5)
#define RTL_ADVERTISED_2500_FULL		BIT(6)

#define MII_ADVERTISE       0x04        /* Advertisement control reg   */

// 8153-specific
#define OCP_BASE_MII		0xa400
#endif

// from mii.h

#define MII_BMCR		0x00	/* Basic mode control register */
#define MII_BMSR		0x01	/* Basic mode status register  */
#define MII_PHYSID1		0x02	/* PHYS ID 1                   */
#define MII_PHYSID2		0x03	/* PHYS ID 2                   */
#define MII_ADVERTISE		0x04	/* Advertisement control reg   */
#define MII_LPA			0x05	/* Link partner ability reg    */
#define MII_EXPANSION		0x06	/* Expansion register          */
#define MII_CTRL1000		0x09	/* 1000BASE-T control          */
#define MII_STAT1000		0x0a	/* 1000BASE-T status           */
#define	MII_MMD_CTRL		0x0d	/* MMD Access Control Register */
#define	MII_MMD_DATA		0x0e	/* MMD Access Data Register */
#define MII_ESTATUS		0x0f	/* Extended Status             */
#define MII_DCOUNTER		0x12	/* Disconnect counter          */
#define MII_FCSCOUNTER		0x13	/* False carrier counter       */
#define MII_NWAYTEST		0x14	/* N-way auto-neg test reg     */
#define MII_RERRCOUNTER		0x15	/* Receive error counter       */
#define MII_SREVISION		0x16	/* Silicon revision            */
#define MII_RESV1		0x17	/* Reserved...                 */
#define MII_LBRERROR		0x18	/* Lpback, rx, bypass error    */
#define MII_PHYADDR		0x19	/* PHY address                 */
#define MII_RESV2		0x1a	/* Reserved...                 */
#define MII_TPISTATUS		0x1b	/* TPI status for 10mbps       */
#define MII_NCONFIG		0x1c	/* Network interface config    */

/* Basic mode control register. */
#define BMCR_RESV		0x003f	/* Unused...                   */
#define BMCR_SPEED1000		0x0040	/* MSB of Speed (1000)         */
#define BMCR_CTST		0x0080	/* Collision test              */
#define BMCR_FULLDPLX		0x0100	/* Full duplex                 */
#define BMCR_ANRESTART		0x0200	/* Auto negotiation restart    */
#define BMCR_ISOLATE		0x0400	/* Isolate data paths from MII */
#define BMCR_PDOWN		0x0800	/* Enable low power state      */
#define BMCR_ANENABLE		0x1000	/* Enable auto negotiation     */
#define BMCR_SPEED100		0x2000	/* Select 100Mbps              */
#define BMCR_LOOPBACK		0x4000	/* TXD loopback bits           */
#define BMCR_RESET		0x8000	/* Reset to default state      */
#define BMCR_SPEED10		0x0000	/* Select 10Mbps               */

/* Basic mode status register. */
#define BMSR_ERCAP		0x0001	/* Ext-reg capability          */
#define BMSR_JCD		0x0002	/* Jabber detected             */
#define BMSR_LSTATUS		0x0004	/* Link status                 */
#define BMSR_ANEGCAPABLE	0x0008	/* Able to do auto-negotiation */
#define BMSR_RFAULT		0x0010	/* Remote fault detected       */
#define BMSR_ANEGCOMPLETE	0x0020	/* Auto-negotiation complete   */
#define BMSR_RESV		0x00c0	/* Unused...                   */
#define BMSR_ESTATEN		0x0100	/* Extended Status in R15      */
#define BMSR_100HALF2		0x0200	/* Can do 100BASE-T2 HDX       */
#define BMSR_100FULL2		0x0400	/* Can do 100BASE-T2 FDX       */
#define BMSR_10HALF		0x0800	/* Can do 10mbps, half-duplex  */
#define BMSR_10FULL		0x1000	/* Can do 10mbps, full-duplex  */
#define BMSR_100HALF		0x2000	/* Can do 100mbps, half-duplex */
#define BMSR_100FULL		0x4000	/* Can do 100mbps, full-duplex */
#define BMSR_100BASE4		0x8000	/* Can do 100mbps, 4k packets  */

/* Advertisement control register. */
#define ADVERTISE_SLCT		0x001f	/* Selector bits               */
#define ADVERTISE_CSMA		0x0001	/* Only selector supported     */
#define ADVERTISE_10HALF	0x0020	/* Try for 10mbps half-duplex  */
#define ADVERTISE_1000XFULL	0x0020	/* Try for 1000BASE-X full-duplex */
#define ADVERTISE_10FULL	0x0040	/* Try for 10mbps full-duplex  */
#define ADVERTISE_1000XHALF	0x0040	/* Try for 1000BASE-X half-duplex */
#define ADVERTISE_100HALF	0x0080	/* Try for 100mbps half-duplex */
#define ADVERTISE_1000XPAUSE	0x0080	/* Try for 1000BASE-X pause    */
#define ADVERTISE_100FULL	0x0100	/* Try for 100mbps full-duplex */
#define ADVERTISE_1000XPSE_ASYM	0x0100	/* Try for 1000BASE-X asym pause */
#define ADVERTISE_100BASE4	0x0200	/* Try for 100mbps 4k packets  */
#define ADVERTISE_PAUSE_CAP	0x0400	/* Try for pause               */
#define ADVERTISE_PAUSE_ASYM	0x0800	/* Try for asymetric pause     */
#define ADVERTISE_RESV		0x1000	/* Unused...                   */
#define ADVERTISE_RFAULT	0x2000	/* Say we can detect faults    */
#define ADVERTISE_LPACK		0x4000	/* Ack link partners response  */
#define ADVERTISE_NPAGE		0x8000	/* Next page bit               */
  
#define 	ADVERTISE_1000FULL   0x0200 /* Advertise 1000BASE-T full duplex */
#define 	ADVERTISE_1000HALF   0x0100 /* Advertise 1000BASE-T half duplex */

#define OCP_10GBT_CTRL		0xa5d4

/* AN 10GBASE-T control register. */
#define MDIO_AN_10GBT_CTRL_ADVFSRT2_5G	0x0020	/* Advertise 2.5GBASE-T fast retrain */
#define MDIO_AN_10GBT_CTRL_ADV2_5G	0x0080	/* Advertise 2.5GBASE-T */
#define MDIO_AN_10GBT_CTRL_ADV5G	0x0100	/* Advertise 5GBASE-T */
#define MDIO_AN_10GBT_CTRL_ADV10G	0x1000	/* Advertise 10GBASE-T */

// Header definitions from r8152.h, in the linux kernel.
#define RTL8152_REQ_GET_REGS	0x05
#define RTL8152_REQ_SET_REGS	0x05
#define RTL8152_REQT_READ	0xc0
#define RTL8152_REQT_WRITE	0x40
#define BYTE_EN_DWORD			0xff
#define BYTE_EN_WORD			0x33
#define BYTE_EN_BYTE			0x11
#define BYTE_EN_SIX_BYTES		0x3f
#define BYTE_EN_START_MASK		0x0f
#define MCU_TYPE_PLA			0x0100
#define MCU_TYPE_USB			0x0000


#define OCP_PHY_STATUS		0xa420
#define OCP_PHY_STATE		0xa708

#define OCP_BASE		0xe86c

#  if __BYTE_ORDER == __LITTLE_ENDIAN
#define ltohl(x)      (x)
#define htoll(x)      (x)
#else
#define ltohl(x)     __bswap_32 (x)
#define htoll(x)     __bswap_32 (x)
#endif

	// Based on https://cscott.net/usb_dev/data/devclass/usbcdc11.pdf
enum cdc_commands
{
	SEND_ENCAPSULATED_COMMAND = 0x00,
	GET_ENCAPSULATED_RESPONSE = 0x01,
	SET_COMM_FEATURE = 0x02,
	GET_COMM_FEATURE = 0x03,
	CLEAR_COMM_FEATURE = 0x04,
	SET_AUX_LINE_STATE = 0x10,
	SET_HOOK_STATE = 0x11,
	PULSE_SETUP = 0x12,
	SEND_PULSE = 0x13,
	SET_PULSE_TIME = 0x14,
	RING_AUX_JACK = 0x15,
	SET_LINE_CODING = 0x20,
	GET_LINE_CODING = 0x21,
	SET_CONTROL_LINE_STATE = 0x22,
	SEND_BREAK = 0x23,
	SET_RINGER_PARMS = 0x30,
	GET_RINGER_PARMS = 0x31,
	SET_OPERATION_PARMS = 0x32,
	GET_OPERATION_PARMS = 0x33,
	SET_LINE_PARMS = 0x34,
	GET_LINE_PARMS = 0x35,
	DIAL_DIGITS = 0x36,
	SET_UNIT_PARAMETER = 0x37,
	GET_UNIT_PARAMETER = 0x38,
	CLEAR_UNIT_PARAMETER = 0x39,
	GET_PROFILE = 0x3A,
	SET_ETHERNET_MULTICAST_FILTERS = 0x40,
	SET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER = 0x41,
	GET_ETHERNET_POWER_MANAGEMENT_PATTERN_FILTER = 0x42,
	SET_ETHERNET_PACKET_FILTER = 0x43,
	GET_ETHERNET_STATISTIC = 0x44,
	SET_ATM_DATA_FORMAT = 0x50,
	GET_ATM_DEVICE_STATISTICS = 0x51,
	SET_ATM_DEFAULT_VC = 0x52,
	GET_ATM_VC_STATISTICS = 0x53,
	GET_NTB_PARAMETERS = 0x80,
	GET_NET_ADDRESS = 0x81,
	SET_NET_ADDRESS = 0x82,
	GET_NTB_FORMAT = 0x83,
	SET_NTB_FORMAT = 0x84,
	GET_NTB_INPUT_SIZE = 0x85,
	SET_NTB_INPUT_SIZE = 0x86,
	GET_MAX_DATAGRAM_SIZE = 0x87,
	SET_MAX_DATAGRAM_SIZE = 0x88,
	GET_CRC_MODE = 0x89,
	SET_CRC_MODE = 0x8A,
};


//////////////////////////////////////////////////////////////////////////////

// This code is "based on" r8153_ecm.c which is GPL licensed, from the Linux kernel.
// But, I kinda rewrote it, so it's writing it to the "spec" but not really the same way.

static int get_registers( netinterface_rtl8153 * iface, uint16_t value, uint16_t index, uint16_t size, void *data )
{
	return libusb_control_transfer( iface->device_handle, RTL8152_REQT_READ, RTL8152_REQ_GET_REGS, value, index, data, size, 500);
}

static int set_registers( netinterface_rtl8153 * iface, uint16_t value, uint16_t index, uint16_t size, void *data )
{
	return libusb_control_transfer( iface->device_handle, RTL8152_REQT_WRITE, RTL8152_REQ_SET_REGS, value, index, data, size, 500);
}

static int pla_read_word(netinterface_rtl8153 * iface, uint16_t index)
{
	uint8_t shift = index & 2;
	uint32_t tmp;
	int ret = libusb_control_transfer( iface->device_handle, RTL8152_REQT_READ, RTL8152_REQ_GET_REGS,
				index & ~3,
				MCU_TYPE_PLA | ( BYTE_EN_WORD << shift ),
				(uint8_t*)&tmp, sizeof(tmp), 500 );
	if( ret < 0 )
		return ret;
	else
		return (ltohl(tmp) >> (shift*8)) & 0xffff;
}

static int pla_read_dword(netinterface_rtl8153 * iface, uint16_t index)
{
	if( index & 3 )
	{
		fprintf( stderr, "Invalid read of dword %04x\n", index );
		return -93;
	}
	uint32_t tmp;
	int ret = libusb_control_transfer( iface->device_handle, RTL8152_REQT_READ, RTL8152_REQ_GET_REGS,
				index,
				MCU_TYPE_PLA,
				(uint8_t*)&tmp, sizeof(tmp), 500 );
	if( ret < 0 )
		return ret;
	else
		return (ltohl(tmp));
}

static int pla_write_word(netinterface_rtl8153 * iface, uint16_t index, uint32_t data)
{
	uint8_t shift = index & 2;
	uint32_t tmp;
	int ret = libusb_control_transfer(iface->device_handle, RTL8152_REQT_READ, RTL8152_REQ_GET_REGS, index & ~3,
		MCU_TYPE_PLA | (BYTE_EN_WORD<<shift), (uint8_t*)&tmp, sizeof(tmp), 500);
	if (ret < 0)
		return ret;
	data = ((data & 0xffff)<<shift) | ( ltohl(tmp) & ~(0xffff<<shift));
	tmp = htoll(data);
	ret = libusb_control_transfer(iface->device_handle, RTL8152_REQT_WRITE, RTL8152_REQ_SET_REGS, index,
		MCU_TYPE_PLA | (BYTE_EN_WORD<<shift), (uint8_t*)&tmp, sizeof(tmp), 500);
	return ( ret < 0 ) ? ret : (ret != sizeof(tmp));
}

static int pla_write_dword(netinterface_rtl8153 * iface, uint16_t index, uint32_t data)
{
	if( index & 3 )
	{
		fprintf( stderr, "Error: invalid write to %04x\n", index );
		return -93;
	}
	data = htoll(data);
	int ret = libusb_control_transfer(iface->device_handle, RTL8152_REQT_WRITE, RTL8152_REQ_GET_REGS, index,
		MCU_TYPE_PLA, (uint8_t*)&data, sizeof(data), 500);
	return ret;
}

static int r8153_ecm_mdio_read(netinterface_rtl8153 * iface, int reg)
{
	int ret = pla_write_word( iface, OCP_BASE, 0xa000 );
	if (ret < 0)
		return ret;
	else
		return pla_read_word( iface, 0xb400 + reg * 2 );
}

static int r8153_ecm_mdio_write(netinterface_rtl8153 * iface, int reg, int val)
{
	int ret = pla_write_word(iface, OCP_BASE, 0xa000);
	if (ret < 0)
		return ret;
	else
		return pla_write_word(iface, 0xb400 + reg * 2, val);
}


// Ok done with GPL land.

static void cb_in( struct libusb_transfer *transfer )
{
	netinterface_rtl8153 * iface = (netinterface_rtl8153 *)transfer->user_data;
	if( transfer->status == LIBUSB_TRANSFER_COMPLETED )
	{
		int alen = transfer->actual_length;
		uint8_t * buffer = transfer->buffer;
		printf( "ALen: %d\n", alen );
		int i;
		for( i = 0; i < alen; i += 16 )
		{
			int j;
			for( j = 0; j < 16; j++ )
			{
				int ij = i + j;
				int b = ( ij < alen ) ? buffer[ij] : -1;
				if( b >= 0 )
					printf( "%02x ", b );
				else
					printf( "   " );
			}
			printf( "  " );
			for( j = 0; j < 16; j++ )
			{
				int ij = i + j;
				int b = ( ij < alen ) ? buffer[ij] : -1;
				if( b > 31 && b < 128 )
					printf( "%c", b );
				else
					printf( "." );
			}
			printf( "\n" );
		}
	}
	else
	{
		fprintf( stderr, "Transfer failed: Status %d\n", transfer->status );
	}

//	libusb_fill_bulk_transfer( transfer, iface->device_handle, IN_ENDPOINT, transfer->buffer, BUFFERSIZE, cb_in, iface, 0);
	transfer->status = 0;
	transfer->timeout = 0; // From here on out go for forever.
	int r = libusb_submit_transfer( transfer );
	if( r != 0 )
	{
		fprintf( stderr, "Error pushing transfer back.\n" );
		iface->fault = r;
	}
}

void rtl8153_teardown( netinterface_rtl8153 * iface )
{
	rtl8153_nr--;

	// TODO: Unallocate all transfers.

	if( iface->descriptor ) free( iface->descriptor );
	libusb_close( iface->device_handle );
	if( rtl8153_nr <= 0 )
	{
		rtl8153_nr = 0;
		libusb_exit(rtl8153_usb_context);
	}
}

int rtl8153_poll( netinterface_rtl8153 * iface, int microseconds_to_poll )
{
}

int rtl8153_send( netinterface_rtl8153 * iface, uint8_t * packet, int packetlen )
{
}


static int rtl8153_internal_start_device( netinterface_rtl8153 * iface )
{
	libusb_device_handle * dh = iface->device_handle;

	uint8_t dummy = 0;

	int i;


	if( r8153_ecm_mdio_write( iface, MII_ADVERTISE, 
		ADVERTISE_PAUSE_ASYM |
		ADVERTISE_PAUSE_CAP |
		ADVERTISE_100FULL |
		ADVERTISE_100HALF |
		ADVERTISE_10FULL |
		ADVERTISE_10HALF |
		ADVERTISE_CSMA |
		0
	) )
	{
		fprintf( stderr, "Error: failed to configure ethernet card advertisements\n" );
		return -1;
	}

	r8153_ecm_mdio_write( iface, MII_CTRL1000, 
		ADVERTISE_1000HALF |
		ADVERTISE_1000FULL  |
		0
	);

	//pla_write_word( iface, OCP_10GBT_CTRL, pla_read_word(iface, OCP_10GBT_CTRL ) | MDIO_AN_10GBT_CTRL_ADV2_5G );

	r8153_ecm_mdio_write( iface, MII_BMCR, 
		( r8153_ecm_mdio_read( iface, MII_BMCR ) | BMCR_ANENABLE | BMCR_ANRESTART | BMCR_RESET ) ) 
		& (~(BMCR_PDOWN))
	;

	printf( "ADVERT: %d : %04x\n", MII_ADVERTISE, r8153_ecm_mdio_read(iface, MII_ADVERTISE) );
	printf( "AD1000: %d : %04x\n", MII_CTRL1000, r8153_ecm_mdio_read(iface, MII_CTRL1000) );
	printf( "AD2.5G:   : %08x\n", pla_read_dword(iface, OCP_10GBT_CTRL) );

	// Accept all packets
	libusb_control_transfer( iface->device_handle, 0x21, SET_ETHERNET_PACKET_FILTER, 0x1f, 0, 0, 0, 500);

	struct timeval tvzero = { 0 };

	for( i = 0; i < USB_BUFFERS * 2; i++ )
	{
		struct libusb_transfer * x;
		x = iface->xfers_in[i] = libusb_alloc_transfer( 0 );
		libusb_fill_bulk_transfer( x, dh, IN_ENDPOINT, iface->buffer_in[i], BUFFERSIZE, cb_in, iface, 0);
		libusb_handle_events_timeout(rtl8153_usb_context, &tvzero );
		int r = libusb_submit_transfer(x);
		usleep(1000);
		if( r < 0 )
		{
			fprintf( stderr, "Error: Can't submit in transfer error: %d\n", r );
		}

		x = iface->xfers_out[i] = libusb_alloc_transfer( 0 );
		libusb_fill_bulk_transfer( x, dh, OUT_ENDPOINT, iface->buffer_out[i], BUFFERSIZE, 0, iface, 1000);
	}

#if 1
	for( i = 0; i < 65536; i++ )
	{
		//int r= libusb_control_transfer( iface->device_handle, RTL8152_REQT_READ, 0x31, i, 0, 0, 0, 500);
		//int r= libusb_control_transfer( iface->device_handle, RTL8152_REQT_READ, 0x31, i, 0, 0, 0, 500);
		//int r = libusb_control_transfer( iface->device_handle, 0x21|0x80, i, 0, 0, 0, 0, 500);
		//if( r != -9 )
		//printf( "%02x:%d\n", i, r );

		//uint32_t rd[10] = { 0xffffffff, 0xffffffff };
		//int r = libusb_control_transfer( iface->device_handle, 0x21|0x80, i, 0, 0, rd, sizeof(rd), 500);
		//if( r != -9 )
		//	printf( "%02x: %d\n", i, r );
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 1000;
		libusb_handle_events_timeout(rtl8153_usb_context, &tv );
	}
	while(1)
	{

	}
#endif

#if 0
	#define MCU_TYPE_PLA			0x0100
	#define PLA_RCR			0xc010

#define PLA_MAR			0xcd00
/* PLA_RCR */
#define RCR_AAP			0x00000001
#define RCR_APM			0x00000002
#define RCR_AM			0x00000004
#define RCR_AB			0x00000008
#define RCR_ACPT_ALL		(RCR_AAP | RCR_APM | RCR_AM | RCR_AB)


	while(1)
	{
		printf( "PHYS: %08x BMSR: %04x\n", pla_read_dword( iface, OCP_PHY_STATE ), r8153_ecm_mdio_read( iface, MII_BMSR ) );
		struct timeval tv;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		libusb_handle_events_timeout(rtl8153_usb_context, &tv );

		uint32_t ocp_data;
		ocp_data = pla_read_dword(iface, MCU_TYPE_PLA | PLA_RCR);
		printf( "OCP_DATA %08x\n", ocp_data );
		ocp_data = RCR_ACPT_ALL;
		ocp_data |= RCR_AB | RCR_APM | RCR_AM | RCR_AAP | RCR_AM;

		printf( "MAR: %08x %08x\n", pla_read_dword( iface, PLA_MAR ), pla_read_dword( iface, PLA_MAR+4) );
		pla_write_dword(iface, PLA_MAR+0, 0xffffffff );
		pla_write_dword(iface, PLA_MAR+4, 0xffffffff );
		printf( "OCP_DATA_OUT %08x\n", ocp_data );
		pla_write_dword(iface, MCU_TYPE_PLA | PLA_RCR, ocp_data);

	}
#endif

	return 0;
}

netinterface * netinterface_create_rtl8153( const char * descriptor, int debuglevel )
{
	libusb_device **usb_devices = NULL;

	// Initialize USB context
	if( !rtl8153_usb_context )
	{
		int result = libusb_init( &rtl8153_usb_context );
		if( result < 0 )
		{
			fprintf( stderr, "Error: Unable to initialise libusb" );
			return 0;
		}

		if( debuglevel )
		{
			libusb_set_option( rtl8153_usb_context, LIBUSB_OPTION_LOG_LEVEL, debuglevel );
		}
	}

	// Get USB device list
	int device_count = libusb_get_device_list( rtl8153_usb_context, &usb_devices );
	if( device_count == 0 )
	{
		fprintf( stderr, "Error: could not get any devices\n" );
	}

	int i;
	for( i = 0; i < device_count; i++ )
	{
		struct libusb_device_descriptor device_descriptor;
		struct libusb_device_handle *device_handle = NULL;
		libusb_device * device = usb_devices[i];

		// Get USB device descriptor
		int result = libusb_get_device_descriptor(device, &device_descriptor);
		if (result < 0) {
			// Couldn't get device handle.
			continue;
		}

		// Only print our devices
		if( RTL8153_VID != device_descriptor.idVendor || RTL8153_PID != device_descriptor.idProduct )
		{
			continue;
		}

		// Attempt to open the device
		int open_result = libusb_open(device, &device_handle);
		if( open_result < 0 )
		{
			fprintf( stderr, "Error: Could not open device\n" );
			libusb_close(device_handle);
			continue;
		}

		char serial[256];
		int sl = libusb_get_string_descriptor_ascii( device_handle, device_descriptor.iSerialNumber, serial, sizeof(serial)-1 );
		if( sl >= 0)
		{
			serial[sl] = 0;
		}
		else
		{
			serial[0] = 0;
		}

		libusb_set_auto_detach_kernel_driver( device_handle, 0 );
		int detach = libusb_detach_kernel_driver( device_handle, 0 );
		int config;
		int claim0 = libusb_claim_interface( device_handle, 0 );
		int claim1 = libusb_claim_interface( device_handle, 1 );
		int ce = libusb_set_configuration( device_handle, CDC_CONFIG_MODE );
		int as = libusb_set_interface_alt_setting( device_handle, 1 /* Device Configuration */, 1 );
		int ct = libusb_get_configuration( device_handle, &config );
		printf( "AS/CL0/1/CT: %d/%d/%d/%d\n", as, claim0, claim1, ct );
		if( as < 0 || claim1 != 0 || config != CDC_CONFIG_MODE )
		{
			fprintf( stderr, "Error: Could not claim interface (Detach Error %d / Claim Error: %d/%d / AS: %d)\n", detach, claim0, claim1, as );
			libusb_close(device_handle);
			continue;
		}

		netinterface_rtl8153 * ret = calloc( sizeof( netinterface_rtl8153 ), 1 );
		ret->recv_cb = 0;
		ret->media_change_cb = 0;
		ret->descriptor = 0;
		ret->opaque = 0;
		ret->teardown = (void (*)( struct netinterface_t * ))rtl8153_teardown;
		ret->poll = (int (*)( struct netinterface_t * , int))rtl8153_poll;
		ret->send = (int (*)( struct netinterface_t * , uint8_t *, int))rtl8153_send;
		ret->device_handle = device_handle;
		ret->descriptor = strdup( serial );
		libusb_free_device_list(usb_devices, 1);

		if( rtl8153_internal_start_device( ret ) )
		{
			free( ret->descriptor );
			free( ret );
			return 0;
		}

		rtl8153_nr++;
		return (netinterface*)ret;
	}

	libusb_free_device_list(usb_devices, 1);
	return 0;
}

static_assert( (intptr_t)&(((netinterface_rtl8153*)0)->descriptor) == (intptr_t)&((netinterface*)0)->descriptor, "structure headers incompatible" );

REGISTER_INTERFACE( rtl8153 );
