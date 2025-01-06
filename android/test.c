//Copyright (c) 2011-2020 <>< Charles Lohr - Under the MIT/x11 or NewBSD License you choose.
// NO WARRANTY! NO GUARANTEE OF SUPPORT! USE AT YOUR OWN RISK

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <GLES3/gl3.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android_native_app_glue.h>
#include <android/sensor.h>
#include <byteswap.h>
#include <errno.h>
#include <fcntl.h>

#include "android_soft_rndis.h"
#include "os_generic.h"

#define CNFG_IMPLEMENTATION
#include "rawdraw_sf.h"

ALooper * l;

unsigned frames = 0;
unsigned long iframeno = 0;

void AndroidDisplayKeyboard(int pShow);

int lastbuttonx = 0;
int lastbuttony = 0;
int lastmotionx = 0;
int lastmotiony = 0;
int lastbid = 0;
int lastmask = 0;
int lastkey, lastkeydown;

static int keyboard_up;
uint8_t buttonstate[8];

void HandleKey( int keycode, int bDown )
{
	lastkey = keycode;
	lastkeydown = bDown;
	if( keycode == 10 && !bDown ) { keyboard_up = 0; AndroidDisplayKeyboard( keyboard_up );  }

	if( keycode == 4 ) { AndroidSendToBack( 1 ); } //Handle Physical Back Button.
}

void HandleButton( int x, int y, int button, int bDown )
{
	buttonstate[button] = bDown;
	lastbid = button;
	lastbuttonx = x;
	lastbuttony = y;

	if( bDown ) {
		// keyboard_up = !keyboard_up; AndroidDisplayKeyboard( keyboard_up );
		uint8_t test_packet[64] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xab, 0xcd, 0x00, 0x01, 0x02, 0x03, 0x08, 0x80, 0x45 };
		printf( "SEND: %d\n", SoftRNDISSend( 0, test_packet, sizeof( test_packet ) ) );

	}
}

void HandleMotion( int x, int y, int mask )
{
	lastmask = mask;
	lastmotionx = x;
	lastmotiony = y;
}

extern struct android_app * gapp;

int HandleDestroy()
{
	printf( "Destroying\n" );
	return 0;
}

volatile int suspended;

void HandleSuspend()
{
	suspended = 1;
}

void HandleResume()
{
	suspended = 0;
}

void HandleThisWindowTermination()
{
	suspended = 1;
}

char media_status[1024];

void SoftRNDISReceived( void * iface, uint8_t * packet, int packetlen )
{
	printf( "ALen: %d\n", packetlen );
#if 1
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
#endif
}

void SoftRNDISChangeMediaState( void * iface, enum netinterface_rate rate, enum netinterface_duplex duplex )
{
	sprintf( media_status, "MEDIA: %d %d\n", rate, duplex );
}


int main( int argc, char ** argv )
{
	short screenx, screeny;
	double ThisTime;
	double LastFPSTime = OGGetAbsoluteTime();

	printf( "Starting Up\n" );

	CNFGBGColor = 0x000040ff;
	CNFGSetupFullscreen( "Test Bench", 0 );
	
	HandleWindowTermination = HandleThisWindowTermination;

	printf( "Startup Complete\n" );


	while(1)
	{
		iframeno++;

		CNFGHandleInput();

		if( suspended ) { usleep(50000); continue; }

		CNFGClearFrame();
		CNFGColor( 0xFFFFFFFF );
		CNFGGetDimensions( &screenx, &screeny );

		// Mesh in background
		CNFGSetLineWidth( 7 );
		CNFGPenX = 0; CNFGPenY = 480;
		char st[4096];
		snprintf( st, sizeof(st), "%dx%d %d %d %d %d %d %d\n%d %d\niface: %p\nmedia: %s",
			screenx, screeny, lastbuttonx, lastbuttony, lastmotionx, lastmotiony, lastkey, lastkeydown, lastbid, lastmask, SoftRNDISStatusBuffer, media_status );
		CNFGDrawText( st, 9 );
		CNFGSetLineWidth( 2 );

		// Square behind text
		CNFGColor( 0x303030ff );
		CNFGTackRectangle( 600, 0, 950, 350);

		frames++;
		//On Android, CNFGSwapBuffers must be called, and CNFGUpdateScreenWithBitmap does not have an implied framebuffer swap.
		CNFGSwapBuffers();

		ThisTime = OGGetAbsoluteTime();

		SoftRNDISTick( 1 );
		static double dTimeOfLastSend = 0;
		if( ThisTime - dTimeOfLastSend > 0.5 )
		{
			dTimeOfLastSend = ThisTime;
			//uint8_t test_packet[64] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xab, 0xcd, 0x00, 0x01, 0x02, 0x03, 0x08, 0x80, 0x45 };
			//printf( "SEND: %d\n", SoftRNDISSend( 0, test_packet, sizeof( test_packet ) ) );
		}

		if( ThisTime > LastFPSTime + 1 )
		{
			printf( "FPS: %d\n", frames );
			frames = 0;
			LastFPSTime+=1;
		}
	}

	return(0);
}

