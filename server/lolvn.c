#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>

#include "os_generic.h"

int internal_pass_frame_to_ethernet( uint8_t * frame, int len );
#define CONNECTION_PACKET_SEND internal_pass_frame_to_ethernet

#include "connection.h"

char local_status[1024];
volatile int terminate;

int rawsock;
int connected_raw;
const char * ifname;
// Based on https://stackoverflow.com/a/41724877/2926815;
struct sockaddr_ll socket_address;

#define MACTABLESIZE 128
uint8_t mactable[MACTABLESIZE][6];
int mactableinsert = 0;

void intHandler(int dummy) {
	terminate = 1;
}

int internal_pass_frame_to_ethernet( uint8_t * frame, int len )
{
#if 0
	printf( "IPFP: %d [%02x %02x %02x %02x %02x %02x <- %02x %02x %02x %02x %02x %02x]\n", len,
		frame[0], frame[1], frame[2], frame[3], frame[4], frame[5],
		frame[6], frame[7], frame[8], frame[9], frame[10], frame[11]  );
#endif

	if( !(frame[0] & 1) )
	{
		int i;
		for( i = 0; i < MACTABLESIZE; i++ )
			if( memcmp( frame, mactable[i], 6 ) == 0 )
				break;
		if( i == MACTABLESIZE )
		{
			memcpy( mactable[mactableinsert], frame+6, 6 );
			mactableinsert = (mactableinsert+1)&MACTABLESIZE;
		}
	}

	if( connected_raw )
		return send( rawsock, frame, len, MSG_NOSIGNAL );
	else
		return -1;
}

void PrintStatus( void )
{
/*
	time_t rawtime;
	struct tm * timeinfo;

	time ( &rawtime );
	timeinfo = localtime ( &rawtime );

	printf( "%s: %s: %s\n", asctime (timeinfo), local_status, host_status );*/

	printf( "STATUS: %s\n", local_status );
	printf( "HOST: %s\n", host_status );
	printf( "TXC:%10ld TXB:%10ld RXC:%10ld RXB:%10ld\n",
		conn_cnt_tx, conn_cnt_txb, conn_cnt_rxm, conn_cnt_rxb );
	printf( "TXF:%10ld RXF:%10ld DECF%10ld\n", conn_cnt_txfail, conn_cnt_rxfail, conn_cnt_decfail );
	printf( "JMTC%10ld JMTF%10ld JMRC%10ld JMRF%10ld\n",
		conn_cnt_jumbs_tx, conn_cnt_jumbs_tx_fail, conn_cnt_jumbs_rx, conn_cnt_jumbs_rx_fail );


}

int ifindex_find(const char name[IFNAMSIZ], int sockfd)
{
	struct ifreq if_idx;
	memset(&if_idx, 0, sizeof(struct ifreq));
	strncpy(if_idx.ifr_name, name, IFNAMSIZ-1);
	if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
	{
		fprintf( stderr, "Error: can't get index to interface [%s]\n", name );
		return -1;
	}

	return if_idx.ifr_ifindex;
}


void * HardwareThread( void * v )
{
	uint8_t mymac[6];
	while( !terminate )
	{
		if( !connected_raw )
		{
			usleep(100000);
			if( rawsock ) close( rawsock );
			rawsock = socket( PF_PACKET, SOCK_RAW, 0 ); // or AF_PACKET/IPPROTO_RAW / PF_PACKET/htons(ETH_P_ALL)
			fprintf( stderr, "Binding to interface %s\n", ifname );
			socket_address.sll_ifindex = ifindex_find( ifname, rawsock );
			socket_address.sll_halen = 0;
			socket_address.sll_family = AF_PACKET;
			socket_address.sll_protocol = htons(ETH_P_ALL);
			socket_address.sll_hatype = 0;
			//socket_address.sll_pkttype = PACKET_HOST;

			if ( bind( rawsock, (const struct sockaddr*)&socket_address, sizeof( socket_address ) ) < 0 )
			{
				fprintf( stderr, "Cannot bind.\n" );
				close( rawsock );
			}
			struct ifreq ifr;
			memset(&ifr, 0, sizeof(struct ifreq));
			//ifr.ifr_flags |= IFF_PROMISC;
			//ioctl(rawsock, SIOCSIFFLAGS, &ifr);

			memset(&ifr, 0, sizeof(struct ifreq));
			strncpy(ifr.ifr_name, ifname, IFNAMSIZ-1);
			if( ioctl( rawsock, SIOCGIFHWADDR, &ifr ) < 0 )
			{
				fprintf( stderr, "Error: Couldn't get self-MAC\n" );
				close( rawsock );
				rawsock = 0;
				continue;
			}

			memcpy( mymac, (char*)ifr.ifr_hwaddr.sa_data, 6 );
			int rv = setsockopt( rawsock, SOL_SOCKET, SO_BINDTODEVICE, ifname, strlen(ifname) );
			printf( "SO_BINDTODEVICE: %d / dev index: %d / MAC: \n", rv, socket_address.sll_ifindex );

			connected_raw = 1;
		}
		//usleep(10000);

		static uint8_t hwbuffer[10000] __attribute__((aligned(64)));
		int rs = recv( rawsock, hwbuffer, sizeof( hwbuffer ), 0 );
		if( rs < 0 )
		{
			shutdown( rawsock, SHUT_RDWR );
			continue;
		}

		if( rs > 12 )
		{
			//int same_addr = 
			//	memcmp( mymac, hwbuffer, 6 ) == 0
			//	||
			//	memcmp( mymac, hwbuffer+6, 6 ) == 0
			//;


			int allow = 0;
			if( hwbuffer[0] & 1 ) allow = 1;
			else
			{
				int i;
				for( i = 0; i < MACTABLESIZE; i++ )
					if( memcmp( hwbuffer, mactable[i], 6 ) == 0 )
					{
						allow = 1;
						break;
					}
			}

#if 0
			printf( "_XP: %d %02x %02x %02x %02x %02x %02x FROM %02x %02x %02x %02x %02x %02x %d\n", rs,
				hwbuffer[0], hwbuffer[1], hwbuffer[2], hwbuffer[3], hwbuffer[4], hwbuffer[5],
				hwbuffer[6], hwbuffer[7], hwbuffer[8], hwbuffer[9], hwbuffer[10], hwbuffer[11],
				same_addr );
#endif
			if( allow )
				ConnectionSendPacket( hwbuffer, rs );
		}
	}
	return 0;
}

int main( int argc, char ** argv )
{
	if( argc != 2 )
	{
		fprintf( stderr, "Error: Need params: [ethernet device to bind]\n" );
		return -5;
	}

	ifname = argv[1];
	og_thread_t * connectionThread = InitializeConnectionAndThread( "lolvn.txt" );
	if( !connectionThread )
	{
		PrintStatus();
		exit( -1 );
	}

	og_thread_t * hardwareThread = OGCreateThread( HardwareThread, 0 );


	while( !terminate )
	{
		PrintStatus();
		int i;
		for( i = 0; i < 1; i++ )
		{
			sleep(1);
			if( terminate ) break;
		}
	}

	OGJoinThread( connectionThread );
	OGJoinThread( hardwareThread );
	return -1;
}

