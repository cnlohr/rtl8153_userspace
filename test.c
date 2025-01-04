#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "netinterface.h"


int recv_cb( struct netinterface_t * iface, uint8_t * packet, int packetlen )
{
	printf( "RX: %d\n", packetlen );
	int i;
	for( i = 0; i < packetlen; i++ )
	{
		printf( "%02x%c", packet[i], (i & 0x1f == 0x1f) ? '\n' : ' ' );
	}
	if( i & 0x1f != 0x1f )
		printf( "\n" );
	return 0;
}

int media_change_cb( struct netinterface_t * iface, enum netinterface_rate rate, enum netinterface_duplex duplex )
{
	printf( "Media Event: %d %d\n", rate, duplex );
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
	while( iface->poll( iface, 0 ) )
	{

	}

	iface->teardown( iface );

	return 0;
}
