#ifndef _NETINTERFACE_H
#define _NETINTERFACE_H

#include <stdint.h>

enum netinterface_rate
{
	NIF_NORATE = 0,
	NIF_10M,
	NIF_100M,
	NIF_1000M,
};

enum netinterface_duplex
{
	NIF_NODUPLEX = 0,
	NIF_HALF,
	NIF_FULL,
};

typedef struct netinterface_t
{
	int (*recv_cb)( struct netinterface_t * iface, uint8_t * packet, int packetlen );
	int (*media_change_cb)( struct netinterface_t * iface, enum netinterface_rate rate, enum netinterface_duplex duplex );
	void * opaque;
	void (*teardown)( struct netinterface_t * iface );
	int (*poll)( struct netinterface_t * iface, int microseconds_to_poll );
	int (*send)( struct netinterface_t * iface, uint8_t * packet, int packetlen );
	int fault;
	char * descriptor; // Must be last in chain.
} netinterface;

// Create a new interface, use 0 to indicate "any"
netinterface * netinterface_create( const char * interfacetype, const char * descriptor, int debuglevel );

// Called by REGISTER_INTERFACE
void netinterface_register( const char * interfacetype, netinterface*(*create)( const char *, int ) );

#define REGISTER_INTERFACE( ifn ) \
	inline static void register_interface_##ifn() __attribute__((constructor)); \
	inline static void register_interface_##ifn() { netinterface_register( #ifn, netinterface_create_##ifn ); }

#endif

