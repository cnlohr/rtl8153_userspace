// XXX TODO
// 1. Fix logic for receiving packets from a stream.
// 2. See if we can figure out what voodoo Android does before bringing the chip online?
// For some reason we need to wait a few seconds.

#include "os_generic.h"
#include <errno.h>
#include <asset_manager.h>
#include <asset_manager_jni.h>
#include <android_native_app_glue.h>
#include <string.h>
#include <linux/ioctl.h>
#include "android_soft_rtk.h"

const int VID = 0x0bda;
const int PID = 0x8153;

int64_t rtk_dbg_ctr_send_fault = 0;
int64_t rtk_dbg_ctr_send_retry = 0;
int64_t rtk_dbg_ctr_recv_fault = 0;
int64_t rtk_dbg_ctr_parse_fault = 0;
int64_t rtk_dbg_ctr_send_cnt = 0;
int64_t rtk_dbg_ctr_recv_cnt = 0;
int64_t rtk_dbg_ctr_send_bytes = 0;
int64_t rtk_dbg_ctr_recv_bytes = 0;
int64_t rtk_dbg_restarts = 0;

int g_toggle_id; // togles between 0 and 1, not sure why you have to go back and forth.

#define USBFS_DISCONNECT_CLAIM_IF_DRIVER	0x01
#define USBFS_DISCONNECT_CLAIM_EXCEPT_DRIVER	0x02

#ifndef _IOC_READ
# define _IOC_READ	2U
#endif
#define USBFS_MAXDRIVERNAME		255
#define IOCTL_USBFS_CLAIMINTERFACE	_IOR('U', 15, unsigned int)
#define IOCTL_USBFS_DISCONNECT_CLAIM	_IOR('U', 27, struct usbfs_disconnect_claim)
#define USBDEVFS_BULK              _IOWR('U', 2, struct usbdevfs_bulktransfer)
#define USBDEVFS_CONTROL           _IOWR('U', 0, struct usbdevfs_ctrltransfer)
#define IOCTL_USBFS_SETINTERFACE	_IOR('U', 4, struct usbfs_setinterface)
#define IOCTL_USBFS_SETCONFIGURATION	_IOR('U', 5, unsigned int)


struct usbfs_setinterface {
	/* keep in sync with usbdevice_fs.h:usbdevfs_setinterface */
	unsigned int interface;
	unsigned int altsetting;
};

char SoftRNDISStatusBuffer[2048];
int shadow_bmsr;
double dTimeOfLastMediaCheck;
double dTimeOfUSBFail;
double dTimeOfLastAsk;
jobject deviceConnection = 0;
int deviceConnectionFD = 0;
char deviceFile[PATH_MAX];
extern struct android_app * gapp;

// In this file.
static int Internal_Control_Transfer( void * handle, int bmRequestType, int bRequest, int wValue, int wIndex, uint8_t * data, int size, int timeout );
static int Internal_BulkRequest( void * handle, int ep, uint8_t * data, int len, int timeout );


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

#define PLA_IDR			0xc000
#define PLA_RCR			0xc010
#define PLA_RCR1		0xc012
#define PLA_RMS			0xc016

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
// It's been pretty heavily once-overed'd.  But still would potentially be entangled.

static int get_registers( void * handle, uint16_t value, uint16_t index, uint16_t size, void *data )
{
	return Internal_Control_Transfer( handle, RTL8152_REQT_READ, RTL8152_REQ_GET_REGS, value, index, data, size, 500);
}

static int set_registers( void * handle, uint16_t value, uint16_t index, uint16_t size, void *data )
{
	return Internal_Control_Transfer( handle, RTL8152_REQT_WRITE, RTL8152_REQ_SET_REGS, value, index, data, size, 500);
}

static int pla_read_word(void * iface, uint16_t index)
{
	uint8_t shift = index & 2;
	uint32_t tmp;
	int ret = get_registers( iface,
				index & ~3,
				MCU_TYPE_PLA | ( BYTE_EN_WORD << shift ),
				sizeof(tmp), (uint8_t*)&tmp );
	if( ret < 0 )
		return ret;
	else
		return (ltohl(tmp) >> (shift*8)) & 0xffff;
}

static int pla_read_dword(void * iface, uint16_t index)
{
	if( index & 3 )
	{
		fprintf( stderr, "Invalid read of dword %04x\n", index );
		return -93;
	}
	uint32_t tmp;
	int ret = get_registers( iface,
				index,
				MCU_TYPE_PLA,
				sizeof(tmp), (uint8_t*)&tmp );
	if( ret < 0 )
		return ret;
	else
		return (ltohl(tmp));
}

static int pla_write_word(void * iface, uint16_t index, uint32_t data)
{
	uint8_t shift = index & 2;
	uint32_t tmp;
	int ret = get_registers(iface, index & ~3,
		MCU_TYPE_PLA | (BYTE_EN_WORD<<shift), sizeof(tmp), (uint8_t*)&tmp );
	if (ret < 0)
		return ret;
	data = ((data & 0xffff)<<shift) | ( ltohl(tmp) & ~(0xffff<<shift));
	tmp = htoll(data);
	ret = set_registers(iface, index,
		MCU_TYPE_PLA | (BYTE_EN_WORD<<shift), sizeof(tmp), (uint8_t*)&tmp );
	return ( ret < 0 ) ? ret : (ret != sizeof(tmp));
}

static int pla_write_dword(void * iface, uint16_t index, uint32_t data)
{
	if( index & 3 )
	{
		fprintf( stderr, "Error: invalid write to %04x\n", index );
		return -93;
	}
	data = htoll(data);
	int ret = set_registers(iface, index,
		MCU_TYPE_PLA, sizeof(data), (uint8_t*)&data );
	return ret;
}

static int r8153_ecm_mdio_read(void * iface, int reg)
{
	int ret = pla_write_word( iface, OCP_BASE, 0xa000 );
	if (ret < 0)
		return ret;
	else
		return pla_read_word( iface, 0xb400 + reg * 2 );
}

static int r8153_ecm_mdio_write(void * iface, int reg, int val)
{
	int ret = pla_write_word(iface, OCP_BASE, 0xa000);
	if (ret < 0)
		return ret;
	else
		return pla_write_word(iface, 0xb400 + reg * 2, val);
}



//XXX TODO: Rewrite these
#define BYTE_EN_DWORD		0xff
#define BYTE_EN_WORD		0x33
#define BYTE_EN_BYTE		0x11
#define BYTE_EN_SIX_BYTES	0x3f
#define BYTE_EN_START_MASK	0x0f
#define BYTE_EN_END_MASK	0xf0
static int generic_ocp_write(void *tp, uint16_t index, uint16_t byteen,
			     uint16_t size, void *data, uint16_t type)
{
	int ret;
	uint16_t byteen_start, byteen_end, byen;
	uint16_t limit = 512;

//	if (test_bit(RTL8152_UNPLUG, &tp->flags))
//		return -ENODEV;

	/* both size and indix must be 4 bytes align */
	if ((size & 3) || !size || (index & 3) || !data) {
		fprintf( stderr, "Malformed request (1)\n" );
		//WARN_ON_ONCE(1);
		return -EPERM;
	}

	if ((uint32_t)index + (uint32_t)size > 0xffff)
	{
		fprintf( stderr, "Malformed request (2)\n" );
		return -EPERM;
	}

	byteen_start = byteen & BYTE_EN_START_MASK;
	byteen_end = byteen & BYTE_EN_END_MASK;

	byen = byteen_start | (byteen_start << 4);

	/* Split the first DWORD if the byte_en is not 0xff */
	if (byen != BYTE_EN_DWORD) {
		ret = set_registers(tp, index, type | byen, 4, data);
		if (ret < 0)
			goto error1;

		index += 4;
		data += 4;
		size -= 4;
	}

	if (size) {
		byen = byteen_end | (byteen_end >> 4);

		/* Split the last DWORD if the byte_en is not 0xff */
		if (byen != BYTE_EN_DWORD)
			size -= 4;

		while (size) {
			if (size > limit) {
				ret = set_registers(tp, index,
						    type | BYTE_EN_DWORD,
						    limit, data);
				if (ret < 0)
					goto error1;

				index += limit;
				data += limit;
				size -= limit;
			} else {
				ret = set_registers(tp, index,
						    type | BYTE_EN_DWORD,
						    size, data);
				if (ret < 0)
					goto error1;

				index += size;
				data += size;
				size = 0;
				break;
			}
		}

		/* Set the last DWORD */
		if (byen != BYTE_EN_DWORD)
			ret = set_registers(tp, index, type | byen, 4, data);
	}

error1:
	if (ret == -ENODEV)
	{
		fprintf( stderr, "ERROR: ENODEV\n" );
		//rtl_set_unplug(tp);
	}

	if (ret < 0)
		fprintf( stderr,
			"write type=0x%04x, index=0x%04x fail %d\n",
			type | byteen, index, ret);

	return ret;
}

static int ocp_write_word(void *tp, uint16_t type, uint16_t index, uint32_t data)
{
	uint16_t byen = BYTE_EN_WORD;
	uint8_t shift = index & 2;
	uint32_t mask = 0xffff;
	__le32 tmp;
	int ret;

	data &= mask;

	if (shift) {
		byen <<= shift;
		data <<= (shift * 8);
		index &= ~3;
	}

	tmp = htoll(data);

	ret = generic_ocp_write(tp, index, byen, sizeof(tmp), &tmp, type);
	//WARN_ON_ONCE(ret != sizeof(tmp));

	return ret;
}



static int generic_ocp_read(void *tp, uint16_t index, uint16_t size,
			    void *data, uint16_t type)
{
	uint16_t limit = 64;
	int ret = 0;

//	if (test_bit(RTL8152_UNPLUG, &tp->flags))
	//	return -ENODEV;

	/* both size and indix must be 4 bytes align */
	if ((size & 3) || !size || (index & 3) || !data) {
		//WARN_ON_ONCE(1);
		return -EPERM;
	}

	if ((uint32_t)index + (uint32_t)size > 0xffff)
		return -EPERM;

	while (size) {
		if (size > limit) {
			ret = get_registers(tp, index, type, limit, data);
			if (ret < 0)
				break;

			index += limit;
			data += limit;
			size -= limit;
		} else {
			ret = get_registers(tp, index, type, size, data);
			if (ret < 0)
				break;

			index += size;
			data += size;
			size = 0;
			break;
		}
	}

//	if (ret == -ENODEV)
		//rtl_set_unplug(tp);

	if (ret < 0)
		fprintf( stderr,
			"read type=0x%04x, index=0x%04x fail %d\n",
			type, index, ret);

	return ret;
}
static int ocp_read_word(void *tp, uint16_t type, uint16_t index, uint32_t *value)
{
	uint16_t byen = BYTE_EN_WORD;
	uint8_t shift = index & 2;
	uint32_t tmp;
	uint32_t data;
	int ret;

	index &= ~3;
	byen <<= shift;

	ret = generic_ocp_read(tp, index, sizeof(tmp), &tmp, type | byen);
//	WARN_ON_ONCE(ret != sizeof(tmp));

	data = ltohl(tmp);
	data >>= (shift * 8);
	data &= 0xffff;

	*value = (uint16_t)data;

	return ret;
}
// Ok done with GPL land.










static int Internal_Control_Transfer( void * handle, int bmRequestType, int bRequest, int wValue, int wIndex, uint8_t * data, int size, int timeout )
{
	struct usbdevfs_ctrltransfer {
		__u8   bRequestType;
		__u8   bRequest;
		__u16  wValue;
		__u16  wIndex;
		__u16  wLength;
		__u32  timeout;  /* in milliseconds */
		void   *data;
	};

	struct usbdevfs_ctrltransfer ct = {
		.bRequestType = bmRequestType,
		.bRequest = bRequest,
		.wValue = wValue,
		.wIndex = wIndex,
		.data = data,
		.wLength = size,
		.timeout = timeout
	};
	errno = 0;
	//printf( "CONTROL STARTING: %d -> %d\n", ct.timeout, ct.wLength );
	int r = ioctl(deviceConnectionFD, USBDEVFS_CONTROL, &ct);
	//printf( "CONTROL REPLY: (DEBUG REMOVE ME): %d %d %s\n", r, errno, strerror( errno ) );
	return r;
}



static int Internal_BulkRequest( void * handle, int ep, uint8_t * data, int len, int timeout )
{
	struct usbdevfs_bulktransfer {
		unsigned int ep;
		unsigned int len;
		unsigned int timeout; /* in milliseconds */
		void __user *data;
	};

	struct usbdevfs_bulktransfer bt = {
		.ep = ep,
		.len = len,
		.timeout = timeout,
		.data = data,
	};
	errno = 0;
#if 0
	if( 0 && ep == 0x02 )
	{
		int j;
		for( j = 0; j < len; j++ )
		{
			printf( "%02x%c", data[j], ((j & 0x1f) == 0x1f || j == len-1) ? '\n' : ' ' );
		}
		printf( "BULK STARTING: %02x %d -> %d [%02x %02x]\n", bt.ep, bt.timeout, bt.len, ((uint8_t*)bt.data)[0], ((uint8_t*)bt.data)[1] );
	}
#endif
	errno = 0;
	int r = ioctl(deviceConnectionFD, USBDEVFS_BULK, &bt);
#if 0
	if( 0 && ep == 0x02 )
	{
		printf( "BULK REPLY: (DEBUG REMOVE ME): %d %d %s\n", r, errno, strerror( errno ) );
	}
#endif
	if( errno == 19 )
	{
		SoftRNDISClose();
	}
	return r;
}

static int Internal_SoftRNDISHandleRX( int timeout )
{
	void * iface = 0;
	int i;

	if( !deviceConnectionFD ) return 0;

	static uint8_t packetbuffer[12000];
	static int lastptr = 0;

#define DGISIZE 8192
	errno = 0;
	int r = Internal_BulkRequest( iface, 0x81, packetbuffer + lastptr, DGISIZE, 1 );
	if( r >= 0 )
	{
		int declenn;
		lastptr += r;
		do
		{
			declenn = packetbuffer[0] + (packetbuffer[1]<<8);
#if 0
			printf( "Packet x: %d %d %d\n", r, lastptr, declenn );
			for( i = 0; i < 24; i++ )
			{
				printf( "%02x ", packetbuffer[i] );
			}
			printf( "\n" );
#endif
			if( declenn > 10000 )
			{
				rtk_dbg_ctr_parse_fault++;
				lastptr = 0;
				break;
			}
			if( lastptr < declenn + 24 ) break; // 24 = wacky Realtek header size.
			SoftRNDISReceived( 0, packetbuffer + 24, declenn );
			rtk_dbg_ctr_recv_bytes += declenn;
			rtk_dbg_ctr_recv_cnt++;

			int to_remove = declenn+24;

			// Round up if possible.
			int to_remove_roundup = (to_remove + 8) & ~7;
			if( lastptr >= to_remove_roundup ) to_remove = to_remove_roundup;

			for( i = 0; i < lastptr - to_remove; i++ )
			{
				packetbuffer[i] = packetbuffer[i+to_remove];
			}
			//printf( "Processed / remove: %d %d\n", lastptr, to_remove );
			lastptr -= to_remove;
		} while( 1 );
		if( lastptr > sizeof( packetbuffer ) - DGISIZE || declenn > sizeof( packetbuffer ) - DGISIZE )
		{
			lastptr = 0;
		}
	}
	else
	{
		if( errno != 110 )
			rtk_dbg_ctr_recv_fault++;
	}
	return 0;
}


int SoftRNDISCheckMedia()
{
	void * iface = 0;
	uint16_t bmsr = r8153_ecm_mdio_read( iface, MII_BMSR );
	if( bmsr != shadow_bmsr )
	{
		enum netinterface_rate rate = NIF_NORATE;
		enum netinterface_duplex duplex = NIF_NODUPLEX;

		if( bmsr & BMSR_LSTATUS )
		{
			if( bmsr & BMSR_10HALF ) { duplex = NIF_HALF; rate = NIF_10M; }
			if( bmsr & BMSR_10FULL ) { duplex = NIF_FULL; rate = NIF_10M; }
			if( bmsr & BMSR_100HALF ) { duplex = NIF_HALF; rate = NIF_100M; }
			if( bmsr & BMSR_100FULL ) { duplex = NIF_FULL; rate = NIF_100M; }
			uint16_t s1000 = r8153_ecm_mdio_read( iface, MII_STAT1000 );
			if( s1000 & 0x0800 )  { duplex = NIF_FULL; rate = NIF_1000M; }
		}

		SoftRNDISChangeMediaState( iface, rate, duplex );

		shadow_bmsr = bmsr;
	}

	return 0;
}

int SoftRNDISClose()
{
	close( deviceConnectionFD );
	deviceConnectionFD = 0;
	return 0;
}

int SoftRNDISConnect()
{
	int i;
	void * iface = 0; // for later

	if( deviceConnectionFD ) return 0;

	char * ats = SoftRNDISStatusBuffer;

//	int SELECTED_CONFIGURATION = 2;
//	int SELECTED_INTERFACE = 1;

	double dNow = OGGetAbsoluteTime();
	//Don't permit 
	if( dNow - dTimeOfUSBFail < 1 ) 
	{
		ats+=sprintf(ats, "Comms failed.  Waiting to reconnect." );
		return -1;
	}

	rtk_dbg_restarts++;

	struct android_app* app = gapp;
	const struct JNINativeInterface * env = 0;
	const struct JNINativeInterface ** envptr = &env;
	const struct JNIInvokeInterface ** jniiptr = app->activity->vm;
	const struct JNIInvokeInterface * jnii = *jniiptr;
	jnii->AttachCurrentThread( jniiptr, &envptr, NULL);
	env = (*envptr);

	// Retrieves NativeActivity.
	jobject lNativeActivity = gapp->activity->clazz;

	//https://stackoverflow.com/questions/13280581/using-android-to-communicate-with-a-usb-hid-device

	//UsbManager manager = (UsbManager)getSystemService(Context.USB_SERVICE);
	jclass ClassContext = env->FindClass( envptr, "android/content/Context" );
	jfieldID lid_USB_SERVICE = env->GetStaticFieldID( envptr, ClassContext, "USB_SERVICE", "Ljava/lang/String;" );
	jobject USB_SERVICE = env->GetStaticObjectField( envptr, ClassContext, lid_USB_SERVICE );

	jmethodID MethodgetSystemService = env->GetMethodID( envptr, ClassContext, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;" );
	jobject manager = env->CallObjectMethod( envptr, lNativeActivity, MethodgetSystemService, USB_SERVICE);
			//Actually returns an android/hardware/usb/UsbManager
	jclass ClassUsbManager = env->FindClass( envptr, "android/hardware/usb/UsbManager" );
	jclass ClassUsbConfiguration = env->FindClass( envptr, "android/hardware/usb/UsbConfiguration" );
	jclass ClassUsbInterface = env->FindClass( envptr, "android/hardware/usb/UsbInterface" );

	//HashMap<String, UsbDevice> deviceList = mManager.getDeviceList();
	jmethodID MethodgetDeviceList = env->GetMethodID( envptr, ClassUsbManager, "getDeviceList", "()Ljava/util/HashMap;" );
	jobject deviceList = env->CallObjectMethod( envptr, manager, MethodgetDeviceList );

	//Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
	jclass ClassHashMap = env->FindClass( envptr, "java/util/HashMap" );
	jmethodID Methodvalues = env->GetMethodID( envptr, ClassHashMap, "values", "()Ljava/util/Collection;" );
	jobject deviceListCollection = env->CallObjectMethod( envptr, deviceList, Methodvalues );
	jclass ClassCollection = env->FindClass( envptr, "java/util/Collection" );
	jmethodID Methoditerator = env->GetMethodID( envptr, ClassCollection, "iterator", "()Ljava/util/Iterator;" );
	jobject deviceListIterator = env->CallObjectMethod( envptr, deviceListCollection, Methoditerator );
	jclass ClassIterator = env->FindClass( envptr, "java/util/Iterator" );

	//while (deviceIterator.hasNext())
	jmethodID MethodhasNext = env->GetMethodID( envptr, ClassIterator, "hasNext", "()Z" );
	jboolean bHasNext = env->CallBooleanMethod( envptr, deviceListIterator, MethodhasNext );

	ats+=sprintf(ats, "Has Devices: %d\n", bHasNext );

	jmethodID Methodnext = env->GetMethodID( envptr, ClassIterator, "next", "()Ljava/lang/Object;" );

	jclass ClassUsbDevice = env->FindClass( envptr, "android/hardware/usb/UsbDevice" );
//	jclass ClassUsbInterface = env->FindClass( envptr, "android/hardware/usb/UsbInterface" );
//	jclass ClassUsbEndpoint = env->FindClass( envptr, "android/hardware/usb/UsbEndpoint" );
	jclass ClassUsbDeviceConnection = env->FindClass( envptr, "android/hardware/usb/UsbDeviceConnection" );
	jmethodID MethodgetDeviceName = env->GetMethodID( envptr, ClassUsbDevice, "getDeviceName", "()Ljava/lang/String;" );
//	jmethodID MethodgetSerialNumber = env->GetMethodID( envptr, ClassUsbDevice, "getSerialNumber", "()Ljava/lang/String;" );
	jmethodID MethodgetConfigurationCount = env->GetMethodID( envptr, ClassUsbDevice, "getConfigurationCount", "()I" );
	jmethodID MethodgetConfiguration = env->GetMethodID( envptr, ClassUsbDevice, "getConfiguration", "(I)Landroid/hardware/usb/UsbConfiguration;" );

	jmethodID MethodgetVendorId = env->GetMethodID( envptr, ClassUsbDevice, "getVendorId", "()I" );
	jmethodID MethodgetProductId = env->GetMethodID( envptr, ClassUsbDevice, "getProductId", "()I" );
	jmethodID MethodgetInterfaceCount = env->GetMethodID( envptr, ClassUsbDevice, "getInterfaceCount", "()I" );
	jmethodID MethodgetInterface = env->GetMethodID( envptr, ClassUsbDevice, "getInterface", "(I)Landroid/hardware/usb/UsbInterface;" );

//	jmethodID MethodgetAlternateSetting = env->GetMethodID( envptr, ClassUsbInterface, "getAlternateSetting", "()I" );
	jmethodID MethodgetInterfaceId = env->GetMethodID( envptr, ClassUsbInterface, "getId", "()I" );
	jmethodID MethodgetInterfaceString = env->GetMethodID( envptr, ClassUsbInterface, "toString", "()Ljava/lang/String;" );
	jmethodID MethodgetInterfaceName = env->GetMethodID( envptr, ClassUsbInterface, "getName", "()Ljava/lang/String;" );

//	jmethodID MethodgetEndpointCount = env->GetMethodID( envptr, ClassUsbInterface, "getEndpointCount", "()I" );
//	jmethodID MethodgetEndpoint = env->GetMethodID( envptr, ClassUsbInterface, "getEndpoint", "(I)Landroid/hardware/usb/UsbEndpoint;" );

//	jmethodID MethodgetAddress = env->GetMethodID( envptr, ClassUsbEndpoint, "getAddress", "()I" );
//	jmethodID MethodgetMaxPacketSize = env->GetMethodID( envptr, ClassUsbEndpoint, "getMaxPacketSize", "()I" );
	jmethodID MethodsetConfiguration = env->GetMethodID( envptr, ClassUsbDeviceConnection, "setConfiguration", "(Landroid/hardware/usb/UsbConfiguration;)Z" );
	jmethodID MethodconfigurationgetId = env->GetMethodID( envptr, ClassUsbConfiguration, "getId", "()I" );

	jmethodID MethodopenDevice = env->GetMethodID( envptr, ClassUsbManager, "openDevice", "(Landroid/hardware/usb/UsbDevice;)Landroid/hardware/usb/UsbDeviceConnection;" );
//	jmethodID Methodclose = env->GetMethodID( envptr, ClassUsbDeviceConnection, "close", "()V" );
	jmethodID MethodrequestPermission = env->GetMethodID( envptr, ClassUsbManager, "requestPermission", "(Landroid/hardware/usb/UsbDevice;Landroid/app/PendingIntent;)V" );
	jmethodID MethodhasPermission = env->GetMethodID( envptr, ClassUsbManager, "hasPermission", "(Landroid/hardware/usb/UsbDevice;)Z" );
	jmethodID MethodclaimInterface = env->GetMethodID( envptr, ClassUsbDeviceConnection, "claimInterface", "(Landroid/hardware/usb/UsbInterface;Z)Z" );
	jmethodID MethodsetInterface = env->GetMethodID( envptr, ClassUsbDeviceConnection, "setInterface", "(Landroid/hardware/usb/UsbInterface;)Z" );

	jmethodID MethodgetFileDescriptor = env->GetMethodID( envptr, ClassUsbDeviceConnection, "getFileDescriptor", "()I" );

	jobject matchingDevice = 0;
//	jobject matchingInterface = 0;

	while( bHasNext )
	{
		//  UsbDevice device = deviceIterator.next();
    	//	Log.i(TAG,"Model: " + device.getDeviceName());
		jobject device = env->CallObjectMethod( envptr, deviceListIterator, Methodnext );
		uint16_t vendorId = env->CallIntMethod( envptr, device, MethodgetVendorId );
		uint16_t productId = env->CallIntMethod( envptr, device, MethodgetProductId );
		jobject tempstr = env->CallObjectMethod( envptr, device, MethodgetDeviceName );
		printf( "DEV: %p %p\n", device, tempstr );
		const char *strdevname = env->GetStringUTFChars(envptr, tempstr, 0);
		ats+=sprintf(ats, "%s,%04x:%04x\n", strdevname,
			vendorId,
			productId );

		if( vendorId == VID && productId == PID )
		{
			strncpy( deviceFile, strdevname, sizeof( deviceFile ) );
			printf( "Matching Device\n" );
			matchingDevice = device;
			env->ReleaseStringUTFChars( envptr, tempstr, strdevname );
			break;
		}
		env->ReleaseStringUTFChars( envptr, tempstr, strdevname );

		bHasNext = env->CallBooleanMethod( envptr, deviceListIterator, MethodhasNext );
	}

	if( !matchingDevice )
	{
		ats+=sprintf(ats, "No matching device\n" );
		return -2;
	}

	printf( "Found Matching Device %p\n", matchingDevice );

	jint numConfigurations = env->CallIntMethod( envptr, matchingDevice, MethodgetConfigurationCount );
	jobject matchingConfiguration = 0;

	for( i = 0; i < numConfigurations; i++ )
	{
		jobject configuration = env->CallObjectMethod( envptr, matchingDevice, MethodgetConfiguration, i );
		int id = env->CallIntMethod( envptr, configuration, MethodconfigurationgetId );
		printf( "Found Configuration with ID: %d\n", id );
		if( id == 2 ) //XXX TODO FIDDLE WITH ME
		{
			matchingConfiguration = configuration;
		}
	}

	if( matchingConfiguration == 0 )
	{
		ats+=sprintf(ats, "No matching configuration\n" );
		printf( "NO MATCHING CONFIGURATION\n" );
		return -9;
	}

	printf( "Found matching configuration\n" );

	int ifaceCount = env->CallIntMethod( envptr, matchingDevice, MethodgetInterfaceCount );
	jobject matchingInterface = 0;
	for( i = 0; i < ifaceCount; i++ )
	{
		jobject interface = env->CallObjectMethod( envptr, matchingDevice, MethodgetInterface, i );
		int id = env->CallIntMethod( envptr, interface, MethodgetInterfaceId );
		jstring ifSt = (jstring)env->CallObjectMethod( envptr, interface, MethodgetInterfaceString);
		jstring ifNa = (jstring)env->CallObjectMethod( envptr, interface, MethodgetInterfaceName);

		const char *st = ifSt ? env->GetStringUTFChars( envptr, ifSt, 0) : "(null)";
		const char *name = ifNa ? env->GetStringUTFChars( envptr, ifNa, 0) : "(null)";

		printf( "  Found interface with id: %d na: %s st: %s\n", id, name, st );

		if( id == g_toggle_id ) //XXX TODO FIDDLE WITH ME  TODO: Switch from 0 to 1, it will not work right then go back to 0.
		{
			matchingInterface = interface;
		}
	}

	int j;

	deviceConnection = env->CallObjectMethod( envptr, manager, MethodopenDevice, matchingDevice );
	if( !deviceConnection )
	{
		dTimeOfUSBFail = dNow;
		goto requestPermission;
	}


	g_toggle_id = !g_toggle_id;
	if( matchingInterface )
	{
		printf( "setting and claiming %d %d\n", deviceConnection, matchingInterface );
		jboolean configOk1 = env->CallBooleanMethod( envptr, deviceConnection,
			MethodclaimInterface, matchingInterface, 1 );
		jboolean configOk0 = env->CallBooleanMethod( envptr, deviceConnection,
			MethodsetInterface, matchingInterface );

		printf( "Set/Claim Interface: %d %d (1 is good)\n", configOk0, configOk1 );
	}
	else
	{
		printf( "WARNING: No matching interace\n" );
	}


	printf( "Matching Configuration: %d\n", matchingConfiguration );

	jboolean configOk = env->CallBooleanMethod( envptr, deviceConnection,
		MethodsetConfiguration, matchingConfiguration );

	printf( "MethodsetConfiguration OK: %d\n", configOk );
	ats+=sprintf(ats, "Config OK: %d\n", configOk );

	deviceConnectionFD = env->CallIntMethod( envptr, deviceConnection, MethodgetFileDescriptor );
	deviceConnection = env->NewGlobalRef(envptr, deviceConnection);

	jnii->DetachCurrentThread( jniiptr );

	printf( "deviceConnectionFD: %d\n", deviceConnectionFD );

	struct usbfs_disconnect_claim {
		unsigned int interface;
		unsigned int flags;
		char driver[USBFS_MAXDRIVERNAME + 1];
	};

	struct usbfs_disconnect_claim dc;
#if 0
		for( j = 0; j < 8; j++ )
		{
			dc.interface = j;
			strcpy(dc.driver, "usbfs");
			dc.flags = USBFS_DISCONNECT_CLAIM_EXCEPT_DRIVER;
			int r = ioctl(deviceConnectionFD, IOCTL_USBFS_DISCONNECT_CLAIM, &dc);

			dc.flags = USBFS_DISCONNECT_CLAIM_IF_DRIVER;
			int r2 = ioctl(deviceConnectionFD, IOCTL_USBFS_DISCONNECT_CLAIM, &dc);
			printf( "DC %d: %d %d\n", j, r, r2 );
		}

	for( j = 0; j < 8; j++ )
	{
		unsigned int iface = j;
		int r3 = ioctl(deviceConnectionFD, IOCTL_USBFS_CLAIMINTERFACE, &iface);
		printf( "CLAIM: %d\n", r3 );
	}

	int config;
	for( config = 0; config < 4; config++ )
	{
		//int config = SELECTED_CONFIGURATION;
		int re = ioctl(deviceConnectionFD, IOCTL_USBFS_SETCONFIGURATION, &config);

		printf( "IOCTL_USBFS_SETCONFIGURATION: %d\n", re );
	}

	for( j = 0; j < 8; j++ )
	{
		unsigned int iface = j;
		int r3 = ioctl(deviceConnectionFD, IOCTL_USBFS_CLAIMINTERFACE, &iface);
		printf( "CLAIM: %d\n", r3 );
	}


	struct usbfs_setinterface setintf;
	setintf.interface = 1;
	setintf.altsetting = 0;
	int re = ioctl(deviceConnectionFD, IOCTL_USBFS_SETINTERFACE, &setintf);
	printf( "IOCTL_USBFS_SETINTERFACE: %d\n", re );
#endif

	for( j = 0; j < 8; j++ )
	{
		unsigned int iface = j;
		int r3 = ioctl(deviceConnectionFD, IOCTL_USBFS_CLAIMINTERFACE, &iface);
		printf( "CLAIM: %d\n", r3 );
	}

//		int as = libusb_set_interface_alt_setting( device_handle, 1 /* Device Configuration */, 1 );
//		int ct = libusb_get_configuration( device_handle, &config );

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

	if( r8153_ecm_mdio_write( iface, MII_CTRL1000, 
		ADVERTISE_1000HALF |
		ADVERTISE_1000FULL  |
		0
	) )
	{
		fprintf( stderr, "error: Failed to configure MII_CTRL1000\n" );
		return -1;
	}

	if( r8153_ecm_mdio_write( iface, MII_BMCR, 
		( r8153_ecm_mdio_read( iface, MII_BMCR ) | BMCR_ANENABLE | BMCR_ANRESTART | BMCR_RESET ) ) 
		& (~(BMCR_PDOWN)) )
	{
		fprintf( stderr, "error: Failed to configure MII_BMCR\n" );
		return -1;
	}

	printf( "ADVERT: %d : %04x\n", MII_ADVERTISE, r8153_ecm_mdio_read(iface, MII_ADVERTISE) );
	printf( "AD1000: %d : %04x\n", MII_CTRL1000, r8153_ecm_mdio_read(iface, MII_CTRL1000) );
	printf( "AD2.5G:   : %08x\n", pla_read_dword(iface, OCP_10GBT_CTRL) );

	// Accept all packets (in CDC-ECM mode)
	Internal_Control_Transfer( iface, 0x21, SET_ETHERNET_PACKET_FILTER, 0x1f, 0, 0, 0, 500);

#define PLA_IDR			0xc000
#define PLA_RCR			0xc010
#define PLA_RCR1		0xc012
#define PLA_RMS			0xc016

#define PLA_TCR1		0xe612 // Has jumbo information and other stuff

//#define PLA_MTPS		0xe615
#define MTPS_JUMBO		(12 * 1024 / 64)
#define RTL8153_MAX_PACKET	9216 /* 9K */

	printf( "OCRW %d\n", ocp_write_word(iface, MCU_TYPE_PLA, PLA_RMS, RTL8153_MAX_PACKET) );
	uint32_t v;
	printf( "OCRW %d\n", ocp_read_word(iface, MCU_TYPE_PLA, PLA_RMS, &v) );
	printf( "OCRW %d\n", v );

	// Does not care about VLAN_ETH_HLEN + ETH_FCS_LEN.
#if 0

#if 1

#define RTL8153_MAX_PACKET	9216 /* 9K */
	printf( "OCP MTU Original: %08x\n", pla_read_word( iface, PLA_RMS ) );
	printf( "OCP MTU: %d\n", pla_write_word( iface, PLA_RMS, RTL8153_MAX_PACKET ) );
	printf( "OCP MTU: %08x\n", pla_read_word( iface, PLA_RMS ) );

#if 1
	//printf( "OCP JUMB: %d\n", pla_write_byte( iface, PLA_MTPS, MTPS_JUMBO) );
	uint32_t TCR1x = pla_read_word( iface, PLA_TCR1 );
	printf( "OCP JUMB ORIG: %08x\n", TCR1x );
	printf( "OCP JUMB: %d\n", pla_write_word( iface, PLA_TCR1, (MTPS_JUMBO<<24) | TCR1x ) );
	TCR1x = pla_read_word( iface, PLA_TCR1 );
	printf( "OCP JUMB: %08x\n", TCR1x );
#endif
#endif


#endif

	return (!deviceConnectionFD)?-5:0;

requestPermission:

	// 	hasPermission(UsbDevice device) 
	if( OGGetAbsoluteTime() - dTimeOfLastAsk < 5 )
	{
		ats+=sprintf(ats, "Asked for permission.  Waiting to ask again." );
	}
	else if( env->CallBooleanMethod( envptr, manager, MethodhasPermission, matchingDevice ) )
	{
		ats+=sprintf(ats, "Has permission - disconnected?" );
	}
	else
	{
		//android.app.PendingIntent currently setting to 0 (null) seems not to cause crashes, but does force lock screen to happen.
		//Because the screen locks we need to do a much more complicated operation, generating a PendingIntent.  See Below.
		//  			env->CallVoidMethod( envptr, manager, MethodrequestPermission, matchingDevice, 0 );

		//This part mimiced off of:
		//https://www.programcreek.com/java-api-examples/?class=android.hardware.usb.UsbManager&method=requestPermission
		// manager.requestPermission(device, PendingIntent.getBroadcast(context, 0, new Intent(MainActivity.ACTION_USB_PERMISSION), 0));
		jclass ClassPendingIntent = env->FindClass( envptr, "android/app/PendingIntent" );
		jclass ClassIntent = env->FindClass(envptr, "android/content/Intent");
		jmethodID newIntent = env->GetMethodID(envptr, ClassIntent, "<init>", "(Ljava/lang/String;)V");
		jstring ACTION_USB_PERMISSION = env->NewStringUTF( envptr, "com.android.recipes.USB_PERMISSION" );
		jobject intentObject = env->NewObject(envptr, ClassIntent, newIntent, ACTION_USB_PERMISSION);

		jmethodID MethodgetBroadcast = env->GetStaticMethodID( envptr, ClassPendingIntent, "getBroadcast", 
			"(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;" );
		jobject pi = env->CallStaticObjectMethod( envptr, ClassPendingIntent, MethodgetBroadcast, lNativeActivity, 0, intentObject, 0 );

		//This actually requests permission.
		env->CallVoidMethod( envptr, manager, MethodrequestPermission, matchingDevice, pi );
		dTimeOfLastAsk = OGGetAbsoluteTime();
	}

    if (env->ExceptionCheck(envptr)) {
        env->ExceptionDescribe(envptr);
        env->ExceptionClear(envptr);
		dTimeOfUSBFail = dNow;
        printf( "Permissions Failed\n" );
    } else {
        printf( "Permissions Passed\n" );
    }
	return -5;
}

int SoftRNDISTick( int timeout )
{
	if( !deviceConnectionFD )
	{
		SoftRNDISConnect();
		if( !deviceConnectionFD ) usleep(100000);
	}

	while( Internal_SoftRNDISHandleRX( timeout ) );

	double dNow = OGGetAbsoluteTime();

	if( dNow - dTimeOfLastMediaCheck > 1 )
	{
		SoftRNDISCheckMedia();
		dTimeOfLastMediaCheck = dNow;
	}

	return 0;
}

int SoftRNDISSend( void * handle, uint8_t * buffer, int packetlen )
{
	if( !deviceConnectionFD ) return -3;

#if 0
	// From the drivers here: https://www.realtek.com/Download/List?cate_id=585

struct tx_desc {
	__le32 opts1;
#define TX_FS			BIT(31) /* First segment of a packet */
#define TX_LS			BIT(30) /* Final segment of a packet */
#define LGSEND			BIT(29)
#define GTSENDV4		BIT(28)
#define GTSENDV6		BIT(27)
#define GTTCPHO_SHIFT		18
#define GTTCPHO_MAX		0x7fU
#define TX_LEN_MAX		0x3ffffU

	__le32 opts2;
#define UDP_CS			BIT(31) /* Calculate UDP/IP checksum */
#define TCP_CS			BIT(30) /* Calculate TCP/IP checksum */
#define IPV4_CS			BIT(29) /* Calculate IPv4 checksum */
#define IPV6_CS			BIT(28) /* Calculate IPv6 checksum */
#define MSS_SHIFT		17
#define MSS_MAX			0x7ffU
#define TCPHO_SHIFT		17
#define TCPHO_MAX		0x7ffU
#define TX_VLAN_TAG		BIT(16)
};
#endif

	const static uint8_t header_fix[] = {
		0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00,
	};
	int tries = 0;
	int fulllen = packetlen+sizeof(header_fix);
	uint8_t sendpak[fulllen];
	memcpy( sendpak, header_fix, sizeof(header_fix) );
	memcpy( sendpak + sizeof header_fix, buffer, packetlen );
	int ofs;
	sendpak[0] = packetlen & 0xff;
	sendpak[1] = packetlen >> 8;
#define TXBUFSIZE 8192
	for( ofs = 0; ofs < fulllen; ofs += TXBUFSIZE )
	{
		int tosend = fulllen - ofs;
		if( tosend > TXBUFSIZE ) tosend = TXBUFSIZE;
		//printf( "Sending: %d\n", tosend );
		retry:;
		int r = Internal_BulkRequest( handle, 0x02, sendpak + ofs, tosend, 100 );
		if( !r )
		{
			fprintf( stderr, "Fault sending.\n" );
			if( tries++ < 4 )
			{
				rtk_dbg_ctr_send_retry++;
				goto retry;
			}
			tries = 0;
			rtk_dbg_ctr_send_fault++;
		}
		rtk_dbg_ctr_send_bytes += (ofs == 0) ? ( tosend - sizeof(header_fix) ) : tosend;
		tries = 0;
	}
	rtk_dbg_ctr_send_cnt++;
	return 0;
}


