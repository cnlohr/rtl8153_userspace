#ifndef _CONNECTION_H
#define _CONNECTION_H

#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>


#include "crc32.h"
#include "chacha20.h"

extern volatile int terminate;

char hostname[960];
int portnum_net_order;
uint32_t sharedkey[8];

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
	uint32_t len;
	uint32_t payload[9980/4];
} __attribute__((packed));

struct packetStructureMinPL
{
	uint32_t nonce[3];
	uint32_t counter;
	uint32_t crc;
	uint32_t len;
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

static int ConnectionSendPacket( uint8_t * packet, int packetlen )
{
	if( !host_connected ) return -1;

	uint32_t dtos[((packetlen+63)+HEADER_SIZE)/4];
	dtos[0] = nonce[0];
	dtos[1] = nonce[1];
	dtos[2] = nonce[2];
	dtos[3] = __atomic_fetch_add( &global_counter, 1, __ATOMIC_SEQ_CST );
	dtos[4] = crc32comp( 0xffffffff, packet, packetlen );
	dtos[5] = packetlen;
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
	if( is_server && lastpeer.sin_port )
		return sendto( host_socket, dtos, tlen, MSG_NOSIGNAL, (struct sockaddr*)&lastpeer, sizeof(lastpeer) );
	else
		return send( host_socket, dtos, tlen, MSG_NOSIGNAL );
}

static void * ConnectionThread( void * c )
{
	while( !terminate )
	{
		if( !host_connected )
		{
			usleep( 100000 );
			if( host_socket ) close( host_socket );

			if( is_server )
			{
				// Server Mode
				server = (struct sockaddr_in){
					.sin_family      = AF_INET,
					.sin_port        = portnum_net_order,
					.sin_addr.s_addr = htonl( INADDR_ANY ),
				};

				host_socket = 0;
				host_connected = 0;

				host_socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
				if( !host_socket )
				{
					sprintf( host_status, "Could not create socket" );
					continue;
				}

				setsockopt(host_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

				if (bind(host_socket, (struct sockaddr *)&server, sizeof(server)) < 0)
				{
					sprintf( host_status, "Could not listen" );
					continue;
				}

				printf( "Server Connected\n" );
			}
			else
			{
				struct hostent * hostnm = gethostbyname( hostname );
				if( !hostnm )
				{
					snprintf( host_status, sizeof(host_status)-1, "Cannot find host(%s)", hostname );
					continue;
				}

				server = (struct sockaddr_in){
					.sin_family      = AF_INET,
					.sin_port        = portnum_net_order,
					.sin_addr.s_addr = *((unsigned long *)hostnm->h_addr),
				};

				host_socket = 0;
				host_connected = 0;

				host_socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
				if( !host_socket )
				{
					sprintf( host_status, "Could not create socket" );
					continue;
				}

				struct timeval tv = { .tv_sec = 120 };

				setsockopt(host_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

				if (connect(host_socket, (struct sockaddr *)&server, sizeof(server)) < 0)
				{
					sprintf( host_status, "Could not connect" );
					continue;
				}

				printf( "Client Connected\n" );
			}
		}

		host_connected = 1;
		sprintf( host_status, "Established" );

		struct packetStructureMaxPL recvbuffer __attribute__((aligned(64)));
		socklen_t sz = sizeof( lastpeer );
		int r = recvfrom( host_socket, &recvbuffer, sizeof( recvbuffer ), MSG_NOSIGNAL,
			(struct sockaddr *)&lastpeer, &sz );

		if( r < 0 )
		{
			host_connected = 0;
			shutdown( host_socket, SHUT_RDWR );
			continue;
		}

		int plen = recvbuffer.len;
		if( plen <= r - HEADER_SIZE )
		{
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
				CONNECTION_PACKET_SEND( (uint8_t*)recvbuffer.payload, plen );
			}
			else
			{
				uint8_t * ady = (uint8_t*)recvbuffer.payload;
				fprintf( stderr, "Bad CRC %08x != %08x [%02x %02x %02x %02x %02x %02x]\n", checkcrc, recvbuffer.crc, 
					ady[0], ady[1], ady[2], ady[3], ady[4], ady[5] );
			}
		}
	}
	return 0;
}




#endif

