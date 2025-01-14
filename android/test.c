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

volatile int clockhead = 0;
#define LOGHIST 4096
int datatx[LOGHIST];
int datarx[LOGHIST];
double histtimes[LOGHIST];

#define CONNECTION_PACKET_SEND( x, y ) { SoftRNDISSend( 0, x, y ); __atomic_fetch_add( &datarx[clockhead], y, __ATOMIC_SEQ_CST ); }
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


void DrawGraph( int sox, int soy, int w, int h, int * history, uint32_t linecolor, const char * title )
{
	CNFGColor( 0x303030FF );
	CNFGTackRectangle( sox, soy, sox + w, soy + h);

	int window = w - 20;
	int i;
	int maxtx = 1;
	int first = 0;
	for( i = 0; i < window; i++ )
	{
		int idx = (clockhead - i + LOGHIST)%LOGHIST;
		int hist = history[idx];
		if( i == 0 ) first = hist;
		if( hist > maxtx ) maxtx = hist;
	}
	CNFGColor( linecolor );
	int idx = (clockhead - 0 + LOGHIST)%LOGHIST;
	double last = h - first * (h-20) / maxtx - 10;
	double dNow = OGGetAbsoluteTime();
	double dBytesOverOneSecond = first;
	for( i = 1; i < window; i++ )
	{
		dBytesOverOneSecond += (dNow - histtimes[idx] < 1 ) ? history[idx] : 0;
		idx = (idx - 1 + LOGHIST) % LOGHIST;
		double dy = h - history[idx] * (h-20) / maxtx - 10;
		CNFGTackSegment( i + 10+sox, last + soy + 10, i + 11+sox, dy+soy+10 );
		last = dy;
	}

	CNFGColor( 0xffffffff );
	CNFGPenX = sox + 4;
	CNFGPenY = soy + 4;
	char cts[1024];
	snprintf( cts, sizeof(cts), "%.3f MBit/s %s\n", dBytesOverOneSecond*8/1000000.0, title );
	CNFGDrawText( cts, 9 );	
}


char media_status[1024];

void SoftRNDISReceived( void * iface, uint8_t * packet, int packetlen )
{
	__atomic_fetch_add( &datatx[clockhead], packetlen, __ATOMIC_SEQ_CST );
//	printf( "ALen: %d\n", packetlen );
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

void DrawPromptNumber( int x, int y, int s, const char * prompt, int64_t n )
{
	char st[1024];
	snprintf( st, 1023, "%s%ld", prompt, n ); 
	CNFGPenX = x; CNFGPenY = y;
	CNFGDrawText( st, s );
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

	{
		// Demand wake lock
		const struct JNINativeInterface * env = 0;
		const struct JNINativeInterface ** envptr = &env;
		const struct JNIInvokeInterface ** jniiptr = gapp->activity->vm;
		const struct JNIInvokeInterface * jnii = *jniiptr;

		jnii->AttachCurrentThread( jniiptr, &envptr, NULL);
		env = (*envptr);
		jclass activityClass = env->FindClass( envptr, "android/app/NativeActivity");
		jobject lNativeActivity = gapp->activity->clazz;
		jobject activity = gapp->activity->clazz;

		// PowerManager pm = mContext.getSystemService(PowerManager.class);
		jclass ClassContext = env->FindClass( envptr, "android/content/Context" );
		jfieldID lid_POWER_SERVICE = env->GetStaticFieldID( envptr, ClassContext, "POWER_SERVICE", "Ljava/lang/String;" );
		jobject POWER_SERVICE = env->GetStaticObjectField( envptr, ClassContext, lid_POWER_SERVICE );
		jmethodID MethodgetSystemService = env->GetMethodID( envptr, ClassContext, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;" );
		jobject powerService = env->CallObjectMethod( envptr, lNativeActivity, MethodgetSystemService, POWER_SERVICE);
			//Actually returns an android/hardware/usb/UsbManager

		jstring string_usb_app = env->NewStringUTF( envptr, "usb_app" );
		jclass ClassPM = env->FindClass( envptr, "android/os/PowerManager" );
		//DumpObjectClassProperties( powerService );
		// newWakeLock( int, java.lang.String ) -> android.os.PowerManager$WakeLock
		jmethodID MethodnewWakeLock = env->GetMethodID( envptr, ClassPM, "newWakeLock", "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;" );
		jobject wakeLock = env->CallObjectMethod( envptr, powerService, MethodnewWakeLock,
			1 /* PARTIAL_WAKE_LOCK */,
			string_usb_app );
		jclass ClassWakeLock = env->FindClass( envptr, "android/os/PowerManager$WakeLock" );
		jmethodID MethodAcquire = env->GetMethodID( envptr, ClassWakeLock, "acquire", "()V" );

		DumpObjectClassProperties( wakeLock );
		env->CallVoidMethod( envptr, wakeLock, MethodAcquire );

		JAVA_CALL_DETACH

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

	double dTimeSinceAdvance = OGGetAbsoluteTime();

	while(!terminate && CNFGHandleInput())
	{
		iframeno++;

		double advanceDelta = ThisTime - dTimeSinceAdvance;
		if( advanceDelta > 0.1 )
		{
			histtimes[clockhead] = ThisTime;
			clockhead = ( clockhead + 1) % LOGHIST;
			datatx[clockhead] = 0;
			datarx[clockhead] = 0;
			if( advanceDelta > 1 ) { dTimeSinceAdvance = ThisTime; } else { dTimeSinceAdvance += 0.1; }
		}

		if( suspended ) { usleep(50000); continue; }

		ThisTime = OGGetAbsoluteTime();

		CNFGClearFrame();
		CNFGColor( 0xFFFFFFFF );
		CNFGGetDimensions( &screenx, &screeny );

		// Mesh in background
		CNFGSetLineWidth( 7 );
		char st[4096];
		snprintf( st, sizeof(st), "%s", SoftRNDISStatusBuffer );
		CNFGPenX = 20; CNFGPenY = 120;
		CNFGDrawText( media_status, 9 );
		DrawPromptNumber(   20, 170+  0, 9, "TX C:", rtk_dbg_ctr_send_cnt );
		DrawPromptNumber(  520, 170+  0, 9, "TX B:", rtk_dbg_ctr_send_bytes );
		DrawPromptNumber(   20, 170+ 50, 9, "RX C:", rtk_dbg_ctr_recv_cnt );
		DrawPromptNumber(  520, 170+ 50, 9, "RX B:", rtk_dbg_ctr_recv_bytes );
		DrawPromptNumber(   20, 170+100, 9, "TX F:", rtk_dbg_ctr_send_fault );
		DrawPromptNumber(  520, 170+100, 9, "RX F:", rtk_dbg_ctr_recv_fault );
		DrawPromptNumber(   20, 170+150, 9, "PARF:", rtk_dbg_ctr_parse_fault );
		DrawPromptNumber(  520, 170+150, 9, "TX R:", rtk_dbg_ctr_send_retry );
		DrawPromptNumber(   20, 170+200, 9, "REST:", rtk_dbg_restarts );
		CNFGPenX = 20; CNFGPenY = 420;
		CNFGDrawText( st, 9 );

		int sox = 20;
		int soy = 650;
		if( screenx > screeny )
		{
			sox = 1000;
			soy = 120;
		}
		CNFGPenX = sox; CNFGPenY = soy;
		CNFGDrawText( host_status, 9 );
		DrawPromptNumber( sox+  0, soy+ 50, 9, "TX C:", conn_cnt_tx );
		DrawPromptNumber( sox+500, soy+ 50, 9, "TX B:", conn_cnt_txb );
		DrawPromptNumber( sox+  0, soy+100, 9, "RX C:", conn_cnt_rx );
		DrawPromptNumber( sox+500, soy+100, 9, "RX B:", conn_cnt_rxb );
		DrawPromptNumber( sox+  0, soy+150, 9, "TX F:", conn_cnt_txfail );
		DrawPromptNumber( sox+500, soy+150, 9, "RX F:", conn_cnt_rxfail );
		DrawPromptNumber( sox+  0, soy+200, 9, "DECF:", conn_cnt_decfail );
		DrawPromptNumber( sox+500, soy+200, 9, "JMTC:", conn_cnt_jumbs_tx );
		DrawPromptNumber( sox+  0, soy+250, 9, "JMTF:", conn_cnt_jumbs_tx_fail );
		DrawPromptNumber( sox+500, soy+250, 9, "JMRC:", conn_cnt_jumbs_rx );
		DrawPromptNumber( sox+  0, soy+300, 9, "JMRF:", conn_cnt_jumbs_rx_fail );
		DrawPromptNumber( sox+500, soy+300, 9, "RLOS:", conn_cnt_rxloss );
		DrawPromptNumber( sox+  0, soy+350, 9, "ROOO:", conn_cnt_rxooo );
		DrawPromptNumber( sox+500, soy+350, 9, "RDUP:", conn_cnt_rxdup );
		DrawPromptNumber( sox+  0, soy+400, 9, "LOSS%", (100*conn_cnt_rxloss)/(conn_cnt_rx+conn_cnt_rxloss) );
		DrawPromptNumber( sox+500, soy+400, 9, "EXTR:", conn_cnt_rxextra );


		if( screenx > screeny )
		{
			DrawGraph( 10, 650, 1000, 300, datatx, 0x00ff00ff, "TX" );
		}
		else
		{
			DrawGraph( 20, 1200, 1000, 300, datatx, 0x00ff00ff, "TX" );
		}

		if( screenx > screeny )
		{
			DrawGraph( 1100, 650, 1000, 300, datarx, 0xff00ffff, "RX" );
		}
		else
		{
			DrawGraph( 20, 1600, 1000, 300, datarx, 0xff00ffff, "RX" );
		}


		CNFGSetLineWidth( 2 );
		// Square behind text
		//CNFGColor( 0x303030ff );
		//CNFGTackRectangle( 600, 0, 950, 350);

		frames++;
		//On Android, CNFGSwapBuffers must be called, and CNFGUpdateScreenWithBitmap does not have an implied framebuffer swap.
		CNFGSwapBuffers();


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

