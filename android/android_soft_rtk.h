#ifndef _ANDROID_SOFT_RNDIS_H
#define _ANDROID_SOFT_RNDIS_H

extern char SoftRNDISStatusBuffer[2048];

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

// You must provide
void SoftRNDISReceived( void * iface, uint8_t * buffer, int packetlen );
void SoftRNDISChangeMediaState( void * iface, enum netinterface_rate, enum netinterface_duplex );

int SoftRNDISClose();
int SoftRNDISConnect();  // No need to call this.

int SoftRNDISTick( int timeout );
int SoftRNDISSend( void * handle, uint8_t * buffer, int packetlen );

#endif

