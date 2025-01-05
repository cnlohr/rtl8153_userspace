#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "netinterface.h"


int recv_cb( struct netinterface_t * iface, uint8_t * packet, int packetlen )
{
	printf( "ALen: %d\n", packetlen );
	int i;
	for( i = 0; i < packetlen; i += 16 )
	{
		int j;
		for( j = 0; j < 16; j++ )
		{
			int ij = i + j;
			int b = ( ij < packetlen ) ? packet[ij] : -1;
			if( b >= 0 )
				printf( "%02x ", b );
			else
				printf( "   " );
		}
		printf( "  " );
		for( j = 0; j < 16; j++ )
		{
			int ij = i + j;
			int b = ( ij < packetlen ) ? packet[ij] : -1;
			if( b > 31 && b < 128 )
				printf( "%c", b );
			else
				printf( "." );
		}
		printf( "\n" );
	}
	return 0;
}

int media_change_cb( struct netinterface_t * iface, enum netinterface_rate rate, enum netinterface_duplex duplex )
{
	printf( "Media Event: %d %d\n", rate, duplex );
	uint8_t test_packet[64] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xab, 0xcd, 0x00, 0x01, 0x02, 0x03, 0x08, 0x80, 0x45 };
	iface->send( iface, test_packet, sizeof( test_packet ) );
	return 0;
}


int main()
{
	netinterface * iface = netinterface_create( "rtl8153", 0, 3 );
	if( !iface )
	{
		fprintf( stderr, "Error: Couldn't find soft interface\n" );
		return -5;
	}

	iface->opaque = 0;
	iface->recv_cb = recv_cb;
	iface->media_change_cb = media_change_cb;
	printf( "%s\n", iface->descriptor );
	while( !iface->poll( iface, 100000 ) )
	{

	}

	iface->teardown( iface );

	return 0;
}
