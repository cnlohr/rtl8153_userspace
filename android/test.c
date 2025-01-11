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

#include "android_soft_rtk.h"
#include "os_generic.h"

#define CNFG_IMPLEMENTATION
#include "rawdraw_sf.h"

#define CONNECTION_PACKET_SEND( x, y ) SoftRNDISSend( 0, x, y )
#include "connection.h"

ALooper * l;
volatile int terminate;
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
	terminate = 1;
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
#if 0
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

	ConnectionSendPacket( packet, packetlen );
}

void SoftRNDISChangeMediaState( void * iface, enum netinterface_rate rate, enum netinterface_duplex duplex )
{
	memset( media_status, 0, sizeof( media_status ) );
	sprintf( media_status, "Media: %s %s\n", 
		(const char * []){ "NO LINK", "10M", "100M", "1000M" }[rate],
		(const char * []){ "", "Half", "Full"}[duplex] );
}

void * EthernetThread( void * v )
{
	while( !terminate )
	{
		SoftRNDISTick( 1000 );
	}
	return 0;
}

int main( int argc, char ** argv )
{
	short screenx, screeny;
	double ThisTime;
	double LastFPSTime = OGGetAbsoluteTime();

	printf( "Starting Up\n" );

	CNFGSetup( "Test Bench", -1, -1 );
	
	HandleWindowTermination = HandleThisWindowTermination;

	printf( "Startup Complete\n" );

	og_thread_t * ethernetThread = OGCreateThread( EthernetThread, 0 );

	while( !AndroidHasPermissions("READ_EXTERNAL_STORAGE") ||
		!AndroidHasPermissions("WRITE_EXTERNAL_STORAGE") ||
		!AndroidHasPermissions("INTERNET") ||
		!AndroidHasPermissions("WRITE_EXTERNAL_STORAGE") )
	{
		CNFGHandleInput();
		CNFGBGColor = 0x500000FF;
		CNFGClearFrame();
		CNFGSwapBuffers();
		if( !AndroidHasPermissions("READ_EXTERNAL_STORAGE") ) AndroidRequestAppPermissions( "READ_EXTERNAL_STORAGE" ); 
		if( !AndroidHasPermissions("WRITE_EXTERNAL_STORAGE") ) AndroidRequestAppPermissions( "WRITE_EXTERNAL_STORAGE" );
		if( !AndroidHasPermissions("INTERNET") ) AndroidRequestAppPermissions( "INTERNET" );
	}

#if 0
	// Me seeing if I can get more permissions.
//android.hardware.usb.host
	{
		const struct JNINativeInterface * env = 0;
		const struct JNINativeInterface ** envptr = &env;
		const struct JNIInvokeInterface ** jniiptr = gapp->activity->vm;
		const struct JNIInvokeInterface * jnii = *jniiptr;

		jnii->AttachCurrentThread( jniiptr, &envptr, NULL);
		env = (*envptr);
		jclass activityClass = env->FindClass( envptr, "android/app/NativeActivity");
		jobject lNativeActivity = gapp->activity->clazz;
		jobject activity = gapp->activity->clazz;

		jclass ClassManifestpermission = env->FindClass( ENVCALL "android/Manifest$permission");
		jfieldID lid_PERM = env->GetStaticFieldID( ENVCALL ClassManifestpermission, "USB_PERMISSION", "Ljava/lang/String;" );
		jstring ls_PERM = (jstring)(env->GetStaticObjectField( ENVCALL ClassManifestpermission, lid_PERM )); 
		jobjectArray perm_array = env->NewObjectArray( ENVCALL 1, env->FindClass( ENVCALL "java/lang/String"), env->NewStringUTF( ENVCALL "" ) );
		env->SetObjectArrayElement( ENVCALL perm_array, 0, ls_PERM );

		jclass ClassActivity = env->FindClass( ENVCALL "android/app/Activity" );
		jmethodID MethodrequestPermissions = env->GetMethodID( ENVCALL ClassActivity, "requestPermissions", "([Ljava/lang/String;I)V" );
		env->CallVoidMethod( ENVCALL activity, MethodrequestPermissions, perm_array, 0 );
		JAVA_CALL_DETACH
	}
#endif

	CNFGBGColor = 0x000040ff;

	og_thread_t * connectionThread = InitializeConnectionAndThread( "/sdcard/lolvn.txt" );

	while(!terminate && CNFGHandleInput())
	{
		iframeno++;

		if( suspended ) { usleep(50000); continue; }

		CNFGClearFrame();
		CNFGColor( 0xFFFFFFFF );
		CNFGGetDimensions( &screenx, &screeny );

		// Mesh in background
		CNFGSetLineWidth( 7 );
		CNFGPenX = 20; CNFGPenY = 120;
		char st[4096];
		snprintf( st, sizeof(st), "%dx%d %d %d %d %d %d %d\n%d %d\niface: %s\n",
			screenx, screeny, lastbuttonx, lastbuttony, lastmotionx, lastmotiony, lastkey, lastkeydown, lastbid, lastmask, SoftRNDISStatusBuffer );
		CNFGDrawText( st, 9 );

		CNFGPenX = 20; CNFGPenY = 520;
		CNFGDrawText( media_status, 9 );
		CNFGPenX = 20; CNFGPenY = 620;
		CNFGDrawText( host_status, 9 );



		CNFGSetLineWidth( 2 );

		// Square behind text
		//CNFGColor( 0x303030ff );
		//CNFGTackRectangle( 600, 0, 950, 350);

		frames++;
		//On Android, CNFGSwapBuffers must be called, and CNFGUpdateScreenWithBitmap does not have an implied framebuffer swap.
		CNFGSwapBuffers();

		ThisTime = OGGetAbsoluteTime();

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

	OGJoinThread( ethernetThread );
	OGJoinThread( connectionThread );

	return(0);
}

