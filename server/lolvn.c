#include <stdio.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>


#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "os_generic.h"

int internal_pass_frame_to_ethernet( uint8_t * frame, int len );
#define CONNECTION_PACKET_SEND internal_pass_frame_to_ethernet

#include "connection.h"

//#define USETAP ifname

int USETAP = 0;
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

int64_t eth_tx_fail;
int64_t eth_tx_bytes;
int64_t eth_tx_cnt;

int64_t eth_rx_fail;
int64_t eth_rx_bytes;
int64_t eth_rx_cnt;

int64_t eth_rx_pass;
int64_t eth_rx_filt;
int64_t eth_rx_mcst;

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
			if( memcmp( frame+6, mactable[i], 6 ) == 0 )
				break;
		if( i == MACTABLESIZE )
		{
			memcpy( mactable[mactableinsert], frame+6, 6 );
			mactableinsert = (mactableinsert+1)&(MACTABLESIZE-1);
			printf( "Adding MAC %02x:%02x:%02x:%02x:%02x:%02x [%d]\n",frame[6], frame[7], frame[8], frame[9], frame[10], frame[11], mactableinsert );
		}
	}

	if( connected_raw )
	{
		int r;
		if( USETAP )
		{
			r = write( rawsock, frame, len );
		}
		else
		{
#if 1
			r = send( rawsock, frame, len, MSG_NOSIGNAL );
#else
			struct msghdr msg = { 0 };
			struct iovec iov[1] = { 0 };
			msg.msg_flags = 0;
			msg.msg_name = 0;
			msg.msg_namelen = 0;
			iov[0].iov_base = frame;
			iov[0].iov_len = len;
			msg.msg_iov = iov;
			msg.msg_iovlen = 1;
			r = sendmsg( rawsock, &msg, 0 );
#endif
		}

		if( r <= 0 )
			eth_tx_fail++;
		else
		{
			eth_tx_cnt++;
			eth_tx_bytes += len;
		}
	}
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

	printf( "HOST: %s\n", host_status );
	printf( "TXC:%10ld TXB:%10ld RXC:%10ld RXB:%10ld\n",
		conn_cnt_tx, conn_cnt_txb, conn_cnt_rx, conn_cnt_rxb );
	printf( "TXF:%10ld RXF:%10ld DECF%10ld EXTR%10ld\n", conn_cnt_txfail, conn_cnt_rxfail, conn_cnt_decfail, conn_cnt_rxextra );
	printf( "JMTC%10ld JMTF%10ld JMRC%10ld JMRF%10ld\n",
		conn_cnt_jumbs_tx, conn_cnt_jumbs_tx_fail, conn_cnt_jumbs_rx, conn_cnt_jumbs_rx_fail );

	printf( "RLOS%10ld ROOO%10ld RDUP%10ld LOSS%3ld%%\n",
		conn_cnt_rxloss, conn_cnt_rxooo, conn_cnt_rxdup,
		(100*conn_cnt_rxloss)/(conn_cnt_rx+conn_cnt_rxloss+1) );


	printf( "MTI:%10ld\n", mactableinsert);

	printf( "STATUS: %s\n", local_status );
	printf( "TXF:%10ld TXC:%10ld TXB:%10ld\n", eth_tx_fail, eth_tx_cnt, eth_tx_bytes );
	printf( "RXF:%10ld RXC:%10ld RXB:%10ld\n", eth_rx_fail, eth_rx_cnt, eth_rx_bytes );
	printf( "PASS%10ld FILT%10ld MCST%10ld\n", eth_rx_pass, eth_rx_filt, eth_rx_mcst );
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
			if( USETAP )
			{
				rawsock = open("/dev/net/tun", O_RDWR);
				if (rawsock < 0) {
					fprintf( stderr, "Can't open /dev/net/tun\n" );
					close( rawsock );
					continue;
				}

				struct ifreq ifr;
				memset(&ifr, 0, sizeof(ifr));
				strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
				// IFF_NO_CARRIER in flags keeps the link down on open
				// TUNSETCARRIER to change the link later
				ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
				int ret = ioctl(rawsock, TUNSETIFF, &ifr);
				if (ret < 0) {
					fprintf( stderr, "Can't TUNSETIFF\n" );
					close( rawsock );
					continue;
				}
			}
			else
			{
				rawsock = socket( PF_PACKET, SOCK_RAW, htons(ETH_P_ALL) ); // or AF_PACKET/IPPROTO_RAW / PF_PACKET/htons(ETH_P_ALL)
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
					continue;
				}
				struct ifreq ifr;
				memset(&ifr, 0, sizeof(struct ifreq));

				// BAD
				//ifr.ifr_flags |= IFF_PROMISC;
				//ioctl(rawsock, SIOCSIFFLAGS, &ifr);

				// https://stackoverflow.com/questions/10070008/reading-from-pf-packet-sock-raw-with-read-misses-packets MAYBE?
				struct packet_mreq mr = { 0 };
				memset(&mr, 0, sizeof(mr));
				mr.mr_ifindex = socket_address.sll_ifindex;
				mr.mr_type = PACKET_MR_PROMISC;
				if (setsockopt(rawsock, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0) {
					fprintf( stderr, "Error setting packet membership\n" );
				}


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
			}


			setsockopt(rawsock, SOL_SOCKET, SO_SNDBUFFORCE, &(int){1}, sizeof(int));
			setsockopt(rawsock, SOL_SOCKET, SO_RCVBUFFORCE, &(int){1}, sizeof(int));
			setsockopt(rawsock, SOL_SOCKET, SO_SNDBUF, &(int){1024*1024*10}, sizeof(int));
			setsockopt(rawsock, SOL_SOCKET, SO_RCVBUF, &(int){1024*1024*10}, sizeof(int));

			connected_raw = 1;
		}
		//usleep(10000);

		static uint8_t hwbuffer[10000] __attribute__((aligned(64)));
		int rs;
		if( USETAP )
		{
			rs = read( rawsock, (uint8_t*)&hwbuffer, sizeof( hwbuffer ) );
		}
		else
		{
			struct msghdr msg = { 0 };
			struct iovec iov[1] = { 0 };
			msg.msg_flags = 0;
			msg.msg_name = 0;
			msg.msg_namelen = 0;
			iov[0].iov_base = &hwbuffer;
			iov[0].iov_len = sizeof( hwbuffer ) - 1;
			msg.msg_iov = iov;
			msg.msg_iovlen = 1;

			rs = recvmsg( rawsock, &msg, 0 );
		}
		if( rs < 0 )
		{
			eth_rx_fail++;
			shutdown( rawsock, SHUT_RDWR );
			continue;
		}

		eth_rx_bytes+=rs;
		eth_rx_cnt++;

		if( rs >= 16 )
		{
			//int same_addr = 
			//	memcmp( mymac, hwbuffer, 6 ) == 0
			//	||
			//	memcmp( mymac, hwbuffer+6, 6 ) == 0
			//;


			int allow = 0;
			if( hwbuffer[0] & 1 )
			{
				allow = 1;
				eth_rx_mcst++;
			}
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
			{
				//printf("RS: %d\n",rs );
				ConnectionSendPacket( hwbuffer, rs );
				eth_rx_pass++;
			}
			else
			{
				eth_rx_filt++;
			}
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
	if( strncmp( ifname, "tap", 3 ) == 0 ) USETAP = 1;
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

