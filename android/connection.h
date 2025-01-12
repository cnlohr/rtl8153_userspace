#ifndef _CONNECTION_H
#define _CONNECTION_H

#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>


#include "crc32.h"
#include "chacha20.h"

#define TCP_MODE

#define FORCE_MTU_SPLIT 1200
#define MAX_FORCE_MTU_SPLIT 12000

extern volatile int terminate;

char hostname[960];
int portnum_net_order;
uint32_t sharedkey[8];

int64_t conn_cnt_tx;
int64_t conn_cnt_rx;
int64_t conn_cnt_txfail;
int64_t conn_cnt_txb;
int64_t conn_cnt_rxb;
int64_t conn_cnt_rxfail;
int64_t conn_cnt_rxextra;
int64_t conn_cnt_rxloss;
int64_t conn_cnt_rxooo;
int64_t conn_cnt_rxdup;
int64_t conn_cnt_decfail;
int64_t conn_cnt_jumbs_tx;
int64_t conn_cnt_jumbs_rx;
int64_t conn_cnt_jumbs_tx_fail;
int64_t conn_cnt_jumbs_rx_fail;
char host_status[1024];

int host_socket = 0;
int host_connected = 0;
int global_counter = 0;
uint32_t nonce[3];
struct sockaddr_in lastpeer;
int is_server = 0;

struct sockaddr_in server;

#define HEADER_SIZE 24

struct packetStructureMaxPL
{
	uint32_t nonce[3];
	uint32_t counter;
	uint32_t crc;
	uint16_t len;
	uint8_t  construct;
	uint8_t  reserved;
	uint32_t payload[MAX_FORCE_MTU_SPLIT/4+15];
} __attribute__((packed));

struct packetStructureMinPL
{
	uint32_t nonce[3];
	uint32_t counter;
	uint32_t crc;
	uint16_t len;
	uint8_t  construct;
	uint8_t  reserved;
	uint32_t payload[0];
} __attribute__((packed));

static og_thread_t InitializeConnectionAndThread( const char * configfile );
static int ConnectionSendPacket( uint8_t * packet, int packetlen );
static void * ConnectionThread( void * c );


static og_thread_t InitializeConnectionAndThread( const char * configfile )
{
	FILE * f = fopen( configfile, "rb" );
	if( !f )
	{
		sprintf( host_status, "Error: cannot open /sdcard/lolvn" );
		return 0;
	}
	if( !fgets( hostname, sizeof( hostname )-1, f ) || strlen( hostname ) < 1 )
	{
		sprintf( host_status, "Error: cannot process hostname" );
		return 0;
	}

	if( hostname[strlen(hostname)-1] == '\n' )
		hostname[strlen(hostname)-1] = 0;

	if( hostname[0] == '-' && hostname[1] == 0 )
		is_server = 1;

	if( is_server ) printf( "Is Server\n" ); else printf( "Is Client\n" );

	char tmpport[16];
	if( !fgets( tmpport, sizeof( tmpport )-1, f ) )
	{
		sprintf( host_status, "Error: cannot process portnum" );
		return 0;
	}
	portnum_net_order = htons( atoi( tmpport ) );
	if( portnum_net_order == 0 )
	{
		sprintf( host_status, "Error: cannot interpret portnum" );
		return 0;
	}
	int dread = fread( sharedkey, 1, 32, f );
	if( dread < 1 )
	{
		sprintf( host_status, "Error: cannot get key" );
		return 0;
	}

	f = fopen( "/dev/random", "rb" );
	dread = fread( nonce, sizeof( nonce ), 1, f );
	if( dread != 1 )
	{
		sprintf( host_status, "Error: could not get nonce" );
		return 0;
	}

	nonce[1] = OGGetAbsoluteTime();

	printf( "Net Base Setup\n" );

	return OGCreateThread( ConnectionThread, 0 );
}

static int InternalSendPacket( uint8_t * packet, int packetlen, int construct );

static int ConnectionSendPacket( uint8_t * packet, int packetlen )
{
	if( !host_connected ) return -1;

	if( packetlen <= FORCE_MTU_SPLIT )
	{
		int r = InternalSendPacket( packet, packetlen, 0 );
		if( r < 0 )	
			conn_cnt_txfail++;
		else
			conn_cnt_tx++;
	}
	else
	{
		int i;
		int packs = (packetlen-1) / FORCE_MTU_SPLIT + 1;
		for( i = 0; i < packs; i++ )
		{
			int last = i == (packs-1);
			int remain = packetlen - i * FORCE_MTU_SPLIT;
			if( remain > FORCE_MTU_SPLIT ) remain = FORCE_MTU_SPLIT;
			int r = InternalSendPacket( packet + i * FORCE_MTU_SPLIT, remain, last ? 1 : (i+2) );
			if( r < 0 )
			{
				conn_cnt_jumbs_tx_fail++;
				return r;
			}
			conn_cnt_tx++;
		}
		conn_cnt_jumbs_tx++;
	}
	conn_cnt_txb += packetlen; 
	return packetlen;
}

static int InternalSendPacket( uint8_t * packet, int packetlen, int construct )
{
	uint32_t dtos[((packetlen+63)+HEADER_SIZE)/4];
	dtos[0] = nonce[0];
	dtos[1] = nonce[1];
	dtos[2] = nonce[2];
	dtos[3] = __atomic_fetch_add( &global_counter, 1, __ATOMIC_SEQ_CST );
	dtos[4] = crc32comp( 0xffffffff, packet, packetlen );
	dtos[5] = packetlen | (construct<<16);
	memcpy( dtos + HEADER_SIZE/4, packet, packetlen );

	uint32_t chachastate[16];
	chacha_init( chachastate, sharedkey, dtos[3], dtos );
	int need_blocks = ((packetlen) + 63) / 64;
	int i;
	for( i = 0; i < need_blocks; i++ )
	{
		chacha_block( chachastate, chachastate );
		int ipl = i * 16 + 6;
		int j;
		for( j = 0; j < 16; j++ )
			dtos[ipl+j] ^= chachastate[j];
	}

	int tlen = packetlen + HEADER_SIZE;
	//printf( "%d/%d\n", ((packetlen+63)+HEADER_SIZE), tlen );
#ifdef TCP_MODE
	return send( host_socket, dtos, tlen, MSG_NOSIGNAL );
#else
	if( is_server && lastpeer.sin_port )
		return sendto( host_socket, dtos, tlen, MSG_NOSIGNAL, (struct sockaddr*)&lastpeer, sizeof(lastpeer) );
	else
		return send( host_socket, dtos, tlen, MSG_NOSIGNAL );
#endif
}

static void * ConnectionThread( void * c )
{
	uint32_t last_cnt = 0;
	while( !terminate )
	{
		if( !host_connected )
		{
			usleep( 100000 );
			if( host_socket ) close( host_socket );
			host_socket = 0;
			host_connected = 0;

#ifdef TCP_MODE
			host_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
#else
			host_socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
#endif
			if( !host_socket )
			{
				sprintf( host_status, "Could not create socket" );
				continue;
			}

			if( is_server )
			{
				// Server Mode
				server = (struct sockaddr_in){
					.sin_family      = AF_INET,
					.sin_port        = portnum_net_order,
					.sin_addr.s_addr = htonl( INADDR_ANY ),
				};

				if (bind(host_socket, (struct sockaddr *)&server, sizeof(server)) < 0)
				{
					fprintf( stderr, "Error: Could not listen (bind)\n" );
					sprintf( host_status, "Could not listen (bind)" );
					close( host_socket );
					continue;
				}

#ifdef TCP_MODE
				if ( listen(host_socket, 1 ) < 0 )
				{
					fprintf( stderr, "Error: Could not listen (listen)\n" );
					sprintf( host_status, "Could not listen (listen)" );
					close( host_socket );
					continue;
				}

				struct sockaddr addr;
				socklen_t addrlen = sizeof(addr);
				int hsbu = host_socket;
				host_socket = accept( host_socket, &addr, &addrlen);
				if( host_socket < -1 )
				{
					fprintf( stderr, "Error: Could not accept\n" );
					sprintf( host_status, "Could not accept" );
					close( hsbu );
					continue;
				}
#endif

				struct timeval tv = { .tv_sec = 120 };

				setsockopt(host_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

				setsockopt(host_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
				setsockopt(host_socket, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int));


				printf( "Server Connected\n" );
			}
			else
			{
				struct hostent * hostnm = gethostbyname( hostname );
				if( !hostnm )
				{
					snprintf( host_status, sizeof(host_status)-1, "Cannot find host(%s)", hostname );
					close( host_socket );
					continue;
				}

				server = (struct sockaddr_in){
					.sin_family      = AF_INET,
					.sin_port        = portnum_net_order,
					.sin_addr.s_addr = *((unsigned long *)hostnm->h_addr),
				};

				struct timeval tv = { .tv_sec = 120 };

				setsockopt(host_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

				if (connect(host_socket, (struct sockaddr *)&server, sizeof(server)) < 0)
				{
					sprintf( host_status, "Could not connect" );
					close( host_socket );
					continue;
				}

				printf( "Client Connected\n" );
			}

			setsockopt(host_socket, SOL_SOCKET, SO_SNDBUFFORCE, &(int){1}, sizeof(int));
			setsockopt(host_socket, SOL_SOCKET, SO_RCVBUFFORCE, &(int){1}, sizeof(int));
			setsockopt(host_socket, SOL_SOCKET, SO_SNDBUF, &(int){1024*1024*10}, sizeof(int));
			setsockopt(host_socket, SOL_SOCKET, SO_RCVBUF, &(int){1024*1024*10}, sizeof(int));

		}

		host_connected = 1;
		sprintf( host_status, "Established" );

		struct packetStructureMaxPL recvbuffer __attribute__((aligned(64)));
#ifdef TCP_MODE
		uint8_t * bptr = (uint8_t *)&recvbuffer;
		int bptrofs = 0;
		int headersize = HEADER_SIZE;
		int r;
		do
		{
			r = recv( host_socket, bptr + bptrofs, headersize - bptrofs, MSG_NOSIGNAL );
			printf( "R: %d %d %d %d\n", r, host_socket, bptrofs, headersize );
			if( r <= 0 ) break;
		} while( bptrofs < headersize );

		if( r >= 0 )
		{
			bptr += headersize;
			bptrofs = 0;
			int size = recvbuffer.len;
			printf( "Looking for %d\n", size );
			do
			{
				r = recv( host_socket, bptr + bptrofs, size - bptrofs, MSG_NOSIGNAL );
				printf( "RA: %d %d %d %d\n", r, host_socket, bptrofs, size );
				if( r <= 0 ) break;
			} while( bptrofs < size );
		}
		if( r <= 0 ) r = -1000;
#else
		socklen_t sz = sizeof( lastpeer );

		int r = recvfrom( host_socket, &recvbuffer, sizeof( recvbuffer ), MSG_NOSIGNAL,
			(struct sockaddr *)&lastpeer, &sz );
#endif
		if( r <= 0 )
		{
			host_connected = 0;
			conn_cnt_rxfail++;
			shutdown( host_socket, SHUT_RDWR );
			continue;
		}

		int plen = recvbuffer.len;
		if( plen <= r - HEADER_SIZE )
		{
			if( plen < r - HEADER_SIZE )
			{
				conn_cnt_rxextra++;
			}
			uint32_t chachastate[16];

			chacha_init( chachastate, sharedkey, recvbuffer.counter, recvbuffer.nonce );

			int need_blocks = ((r - 20) + 63) / 64;
			int i;
			for( i = 0; i < need_blocks; i++ )
			{
				chacha_block( chachastate, chachastate );
				int ipl = i * 16;
				int j;
				for( j = 0; j < 16; j++ )
					recvbuffer.payload[ipl+j] ^= chachastate[j];
			}

			uint32_t checkcrc = crc32comp( 0xffffffff, (uint8_t*)recvbuffer.payload, plen );

			if( checkcrc == recvbuffer.crc )
			{

				int cntdiff = recvbuffer.counter - last_cnt;
				printf( "CNT: %d %d\n", recvbuffer.counter, cntdiff ); 

				if( cntdiff > 1 )
				{
					if( cntdiff > 1000 ) cntdiff = 1000;
					conn_cnt_rxloss += cntdiff;
				}
				else if( cntdiff < 0)
				{
					conn_cnt_rxooo++;
				}
				else if( cntdiff == 0 )
				{
					conn_cnt_rxdup++;
				}
				last_cnt = recvbuffer.counter;


				static uint8_t constructed_jumbo[12000];
				static int constructed_jumbo_place = 0;
				static uint32_t constructed_last_cnt = 0;
				static int constructed_invalid = 1;

				if( recvbuffer.construct == 0 )
				{
					// Nothing to do with jumbo frames?  Just send.
					CONNECTION_PACKET_SEND( (uint8_t*)recvbuffer.payload, plen );
					conn_cnt_rxb += plen;
					conn_cnt_rx++;
					if( constructed_invalid == 0 )
						conn_cnt_jumbs_rx_fail++;
					constructed_invalid = 1;
				}
				else
				{
					//printf( "CONST: %d (%d,%d) %d\n", recvbuffer.construct, recvbuffer.counter, constructed_last_cnt+1, plen);
					// Otherwise we are dealing with a constructed frame.
					if( recvbuffer.construct == 2 )
					{
						// Start constructing the jumbo frame with 2.
						constructed_invalid = 0;
						constructed_jumbo_place = 0;
						constructed_last_cnt = recvbuffer.counter-1;
					}

					if( !constructed_invalid &&
						(uint32_t)(constructed_last_cnt+1) == recvbuffer.counter &&
						constructed_jumbo_place + plen < sizeof( constructed_jumbo) )
					{
						memcpy( constructed_jumbo + constructed_jumbo_place,
							recvbuffer.payload, plen );
						constructed_jumbo_place += plen;
					}
					else
					{
						constructed_invalid = 1;
						conn_cnt_jumbs_rx_fail++;
					}
					constructed_last_cnt = recvbuffer.counter;

					if( recvbuffer.construct == 1 )
					{
						// Finish with 0
						if( !constructed_invalid )
						{
							CONNECTION_PACKET_SEND( (uint8_t*)constructed_jumbo, constructed_jumbo_place );
							conn_cnt_rxb += plen;
							constructed_invalid = 1;
							conn_cnt_rx++;
							conn_cnt_jumbs_rx++;
						}
					}
				}
			}
			else
			{
				uint8_t * ady = (uint8_t*)recvbuffer.payload;
				fprintf( stderr, "Bad CRC %08x != %08x [%02x %02x %02x %02x %02x %02x]\n", checkcrc, recvbuffer.crc, 
					ady[0], ady[1], ady[2], ady[3], ady[4], ady[5] );
				conn_cnt_decfail++;
			}
		}
	}
	return 0;
}




#endif

