#include "os_generic.h"
#include <errno.h>
#include <asset_manager.h>
#include <asset_manager_jni.h>
#include <android_native_app_glue.h>
#include <linux/ioctl.h>

extern jobject deviceConnection;
extern int deviceFD;
extern int deviceConnectionFD;
extern char deviceFile[PATH_MAX];

typedef struct libusb_context_t
{
	int dummy;
} libusb_context;

typedef struct libusb_device_handle_t
{
	int i;
} libusb_device_handle;

struct libusb_transfer
{
	void * user_data;
	int status;
	int actual_length;
	uint8_t * buffer;
	int buffer_len;
	int endpoint;
	void (*cb)( struct libusb_transfer *);
	int timeout;
	libusb_device_handle * dh;
};


#define USBFS_DISCONNECT_CLAIM_IF_DRIVER	0x01
#define USBFS_DISCONNECT_CLAIM_EXCEPT_DRIVER	0x02

#ifndef _IOC_READ
# define _IOC_READ	2U
#endif
#define USBFS_MAXDRIVERNAME		255
#define _IOR(type,nr,argtype)		_IOC(_IOC_READ,(type),(nr),(_IOC_TYPECHECK(argtype)))
#define IOCTL_USBFS_CLAIMINTERFACE	_IOR('U', 15, unsigned int)
#define IOCTL_USBFS_DISCONNECT_CLAIM	_IOR('U', 27, struct usbfs_disconnect_claim)
#define USBDEVFS_BULK              _IOWR('U', 2, struct usbdevfs_bulktransfer)
#define IOCTL_USBFS_SETINTERFACE	_IOR('U', 4, struct usbfs_setinterface)
#define IOCTL_USBFS_SETCONFIGURATION	_IOR('U', 5, unsigned int)


struct usbfs_setinterface {
	/* keep in sync with usbdevice_fs.h:usbdevfs_setinterface */
	unsigned int interface;
	unsigned int altsetting;
};


#define LIBUSB_TRANSFER_COMPLETED 0

extern char debug_status[4096];

int ConnectAndroidUSB( int vid, int pid, libusb_device_handle ** device_handle, char * serial, int seriallen );

// normal .c

double dTimeOfUSBFail;
double dTimeOfLastAsk;
jobject deviceConnection = 0;
int deviceConnectionFD = 0;
int deviceFD = 0;
char deviceFile[PATH_MAX];
extern struct android_app * gapp;

jmethodID MethodbulkTransfer;
jmethodID MethodcontrolTransfer;

jobject matchingEpTX = 0;
jobject matchingEpRX = 0;

extern char debug_status[4096];

int libusb_control_transfer( libusb_device_handle * dh, int bmRequestType, int bRequest, int lValue, int lIndex, uint8_t * data, int size, int timeout )
{
	struct android_app* app = gapp;
	const struct JNINativeInterface * env = 0;
	const struct JNINativeInterface ** envptr = &env;
	const struct JNIInvokeInterface ** jniiptr = app->activity->vm;
	const struct JNIInvokeInterface * jnii = *jniiptr;
	jnii->AttachCurrentThread( jniiptr, &envptr, NULL);
	env = (*envptr);

	jbyteArray ba = 0;
	if( size )
	{
		ba = env->NewByteArray( envptr, size );
		env->SetByteArrayRegion(envptr, ba, 0, size, data );
	}
	int controlReply = env->CallIntMethod( envptr, deviceConnection, MethodcontrolTransfer,
		bmRequestType, bRequest, lValue, lIndex, ba, size, timeout );

	if( ba )
	{
		env->GetByteArrayRegion(envptr, ba, 0, size, data );
		env->DeleteLocalRef(envptr, ba);
	}

	return controlReply;
}

int libusb_submit_transfer( struct libusb_transfer * xfer )
{

	struct usbdevfs_bulktransfer {
		unsigned int ep;
		unsigned int len;
		unsigned int timeout; /* in milliseconds */
		void __user *data;
	};

	struct usbdevfs_bulktransfer bt = {
		.ep = xfer->endpoint,
		.len = xfer->buffer_len,
		.timeout = xfer->timeout,
		.data = xfer->buffer,
	};
	errno = 0;
	printf( "BULK STARTING: %02x %d\n", bt.ep, bt.timeout );
	int r = ioctl(deviceConnectionFD, USBDEVFS_BULK, &bt);
	printf( "BULK REPLY: (DEBUG REMOVE ME): %d %d %s\n", r, errno, strerror( errno ) );

	return -1;



#if 0
	struct android_app* app = gapp;
	const struct JNINativeInterface * env = 0;
	const struct JNINativeInterface ** envptr = &env;
	const struct JNIInvokeInterface ** jniiptr = app->activity->vm;
	const struct JNIInvokeInterface * jnii = *jniiptr;
	jnii->AttachCurrentThread( jniiptr, &envptr, NULL);
	env = (*envptr);

	jobject epo = 0;
	if( xfer->endpoint == 0x81 )
		epo = matchingEpRX;
	else if( xfer->endpoint == 0x02 )
		epo = matchingEpTX;
	else
	{
		fprintf( stderr, "Invalid endpoint\n" );
		return -4;
	}

	jbyteArray ba = env->NewByteArray( envptr, xfer->buffer_len );
	env->SetByteArrayRegion(envptr, ba, 0, xfer->buffer_len, xfer->buffer );
printf( "BULK XFER: %02x %p %p %d %d\n", xfer->endpoint, ba, epo, xfer->buffer_len, xfer->timeout );
	errno = 0;
	int xferred = env->CallIntMethod( envptr, deviceConnection, MethodbulkTransfer, epo, ba, xfer->buffer_len, xfer->timeout );

	if( xfer->endpoint & 0x80 ) env->GetByteArrayRegion(envptr, ba, 0, xfer->buffer_len, xfer->buffer );
	env->DeleteLocalRef(envptr, ba);

	printf( "BULK REPLY: (DEBUG REMOVE ME): %d %d %s\n", xferred, errno, strerror( errno ) );
	
	xfer->status = (xferred>=0)?0:-55;
	xfer->actual_length = (xferred>=0)?xferred:0;
	if( ( xfer->endpoint & 0x80 ) && xfer->cb )
		xfer->cb( xfer );
	return xferred;
#endif
}

int libusb_fill_bulk_transfer( struct libusb_transfer * xfer, libusb_device_handle * dh, int endpoint, uint8_t * buffer, int len, void (*cb)( struct libusb_transfer *), void * user_data, int timeout )
{
	xfer->dh = dh;
	xfer->endpoint = endpoint;
	xfer->buffer = buffer;
	xfer->buffer_len = len;
	xfer->user_data = user_data;
	xfer->cb = cb;
	xfer->timeout = timeout;
	return 0;
}


int libusb_cancel_transfer( struct libusb_transfer * transfer )
{
	// STUB
	return -1;
}

int libusb_free_transfer( struct libusb_transfer * transfer )
{
	// STUB
	return -1;
}

int libusb_close( libusb_device_handle * dh )
{
	// STUB
	return -1;
}

int libusb_exit( libusb_context * ctx )
{
	// STUB
	return -1;
}

int libusb_handle_events_timeout( libusb_context * ctx, struct timeval * tv )
{
	// TODO
	return 0;
}

struct libusb_transfer * libusb_alloc_transfer( int isos )
{
	struct libusb_transfer * ret;
	ret = calloc( sizeof( *ret ), 1 );
	return ret;
}


int ConnectAndroidUSB( int vid, int pid, libusb_device_handle ** device_handle, char * serial, int seriallen )
{
	char * ats = debug_status;

	int SELECTED_CONFIGURATION = 2;
	int SELECTED_INTERFACE = 1;

	//Don't permit 
	if( OGGetAbsoluteTime() - dTimeOfUSBFail < 1 ) 
	{
		ats+=sprintf(ats, "Comms failed.  Waiting to reconnect." );
		return -1;
	}

	struct android_app* app = gapp;
	const struct JNINativeInterface * env = 0;
	const struct JNINativeInterface ** envptr = &env;
	const struct JNIInvokeInterface ** jniiptr = app->activity->vm;
	const struct JNIInvokeInterface * jnii = *jniiptr;
	jnii->AttachCurrentThread( jniiptr, &envptr, NULL);
	env = (*envptr);

	// Retrieves NativeActivity.
	jobject lNativeActivity = gapp->activity->clazz;

	//https://stackoverflow.com/questions/13280581/using-android-to-communicate-with-a-usb-hid-device

	//UsbManager manager = (UsbManager)getSystemService(Context.USB_SERVICE);
	jclass ClassContext = env->FindClass( envptr, "android/content/Context" );
	jfieldID lid_USB_SERVICE = env->GetStaticFieldID( envptr, ClassContext, "USB_SERVICE", "Ljava/lang/String;" );
	jobject USB_SERVICE = env->GetStaticObjectField( envptr, ClassContext, lid_USB_SERVICE );

	jmethodID MethodgetSystemService = env->GetMethodID( envptr, ClassContext, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;" );
	jobject manager = env->CallObjectMethod( envptr, lNativeActivity, MethodgetSystemService, USB_SERVICE);
			//Actually returns an android/hardware/usb/UsbManager
	jclass ClassUsbManager = env->FindClass( envptr, "android/hardware/usb/UsbManager" );
	jclass ClassUsbConfiguration = env->FindClass( envptr, "android/hardware/usb/UsbConfiguration" );

	//HashMap<String, UsbDevice> deviceList = mManager.getDeviceList();
	jmethodID MethodgetDeviceList = env->GetMethodID( envptr, ClassUsbManager, "getDeviceList", "()Ljava/util/HashMap;" );
	jobject deviceList = env->CallObjectMethod( envptr, manager, MethodgetDeviceList );

	//Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
	jclass ClassHashMap = env->FindClass( envptr, "java/util/HashMap" );
	jmethodID Methodvalues = env->GetMethodID( envptr, ClassHashMap, "values", "()Ljava/util/Collection;" );
	jobject deviceListCollection = env->CallObjectMethod( envptr, deviceList, Methodvalues );
	jclass ClassCollection = env->FindClass( envptr, "java/util/Collection" );
	jmethodID Methoditerator = env->GetMethodID( envptr, ClassCollection, "iterator", "()Ljava/util/Iterator;" );
	jobject deviceListIterator = env->CallObjectMethod( envptr, deviceListCollection, Methoditerator );
	jclass ClassIterator = env->FindClass( envptr, "java/util/Iterator" );

	//while (deviceIterator.hasNext())
	jmethodID MethodhasNext = env->GetMethodID( envptr, ClassIterator, "hasNext", "()Z" );
	jboolean bHasNext = env->CallBooleanMethod( envptr, deviceListIterator, MethodhasNext );

	ats+=sprintf(ats, "Has Devices: %d\n", bHasNext );

	jmethodID Methodnext = env->GetMethodID( envptr, ClassIterator, "next", "()Ljava/lang/Object;" );

	jclass ClassUsbDevice = env->FindClass( envptr, "android/hardware/usb/UsbDevice" );
	jclass ClassUsbInterface = env->FindClass( envptr, "android/hardware/usb/UsbInterface" );
	jclass ClassUsbEndpoint = env->FindClass( envptr, "android/hardware/usb/UsbEndpoint" );
	jclass ClassUsbDeviceConnection = env->FindClass( envptr, "android/hardware/usb/UsbDeviceConnection" );
	jmethodID MethodgetDeviceName = env->GetMethodID( envptr, ClassUsbDevice, "getDeviceName", "()Ljava/lang/String;" );
	jmethodID MethodgetSerialNumber = env->GetMethodID( envptr, ClassUsbDevice, "getSerialNumber", "()Ljava/lang/String;" );
	jmethodID MethodgetConfigurationCount = env->GetMethodID( envptr, ClassUsbDevice, "getConfigurationCount", "()I" );
	jmethodID MethodgetConfiguration = env->GetMethodID( envptr, ClassUsbDevice, "getConfiguration", "(I)Landroid/hardware/usb/UsbConfiguration;" );

	jmethodID MethodgetVendorId = env->GetMethodID( envptr, ClassUsbDevice, "getVendorId", "()I" );
	jmethodID MethodgetProductId = env->GetMethodID( envptr, ClassUsbDevice, "getProductId", "()I" );
	jmethodID MethodgetInterfaceCount = env->GetMethodID( envptr, ClassUsbDevice, "getInterfaceCount", "()I" );
	jmethodID MethodgetInterface = env->GetMethodID( envptr, ClassUsbDevice, "getInterface", "(I)Landroid/hardware/usb/UsbInterface;" );

	jmethodID MethodgetAlternateSetting = env->GetMethodID( envptr, ClassUsbInterface, "getAlternateSetting", "()I" );
	jmethodID MethodgetInterfaceId = env->GetMethodID( envptr, ClassUsbInterface, "getId", "()I" );
	jmethodID MethodgetInterfaceString = env->GetMethodID( envptr, ClassUsbInterface, "toString", "()Ljava/lang/String;" );
	jmethodID MethodgetInterfaceName = env->GetMethodID( envptr, ClassUsbInterface, "getName", "()Ljava/lang/String;" );

	jmethodID MethodgetEndpointCount = env->GetMethodID( envptr, ClassUsbInterface, "getEndpointCount", "()I" );
	jmethodID MethodgetEndpoint = env->GetMethodID( envptr, ClassUsbInterface, "getEndpoint", "(I)Landroid/hardware/usb/UsbEndpoint;" );

	jmethodID MethodgetAddress = env->GetMethodID( envptr, ClassUsbEndpoint, "getAddress", "()I" );
	jmethodID MethodgetMaxPacketSize = env->GetMethodID( envptr, ClassUsbEndpoint, "getMaxPacketSize", "()I" );
	jmethodID MethodsetConfiguration = env->GetMethodID( envptr, ClassUsbDeviceConnection, "setConfiguration", "(Landroid/hardware/usb/UsbConfiguration;)Z" );

	jmethodID MethodopenDevice = env->GetMethodID( envptr, ClassUsbManager, "openDevice", "(Landroid/hardware/usb/UsbDevice;)Landroid/hardware/usb/UsbDeviceConnection;" );
	jmethodID Methodclose = env->GetMethodID( envptr, ClassUsbDeviceConnection, "close", "()V" );
	jmethodID MethodrequestPermission = env->GetMethodID( envptr, ClassUsbManager, "requestPermission", "(Landroid/hardware/usb/UsbDevice;Landroid/app/PendingIntent;)V" );
	jmethodID MethodhasPermission = env->GetMethodID( envptr, ClassUsbManager, "hasPermission", "(Landroid/hardware/usb/UsbDevice;)Z" );
	jmethodID MethodclaimInterface = env->GetMethodID( envptr, ClassUsbDeviceConnection, "claimInterface", "(Landroid/hardware/usb/UsbInterface;Z)Z" );
	jmethodID MethodsetInterface = env->GetMethodID( envptr, ClassUsbDeviceConnection, "setInterface", "(Landroid/hardware/usb/UsbInterface;)Z" );
	jmethodID MethodgetFileDescriptor = env->GetMethodID( envptr, ClassUsbDeviceConnection, "getFileDescriptor", "()I" );
	MethodbulkTransfer = env->GetMethodID( envptr, ClassUsbDeviceConnection, "bulkTransfer", "(Landroid/hardware/usb/UsbEndpoint;[BII)I" );  
	MethodcontrolTransfer = env->GetMethodID( envptr, ClassUsbDeviceConnection, "controlTransfer", "(IIII[BII)I" );  

	jobject matchingDevice = 0;
	jobject matchingInterface = 0;

	while( bHasNext )
	{
		//  UsbDevice device = deviceIterator.next();
    	//	Log.i(TAG,"Model: " + device.getDeviceName());
		jobject device = env->CallObjectMethod( envptr, deviceListIterator, Methodnext );
		uint16_t vendorId = env->CallIntMethod( envptr, device, MethodgetVendorId );
		uint16_t productId = env->CallIntMethod( envptr, device, MethodgetProductId );
		jobject tempstr = env->CallObjectMethod( envptr, device, MethodgetDeviceName );
		printf( "DEV: %p %p\n", device, tempstr );
		const char *strdevname = env->GetStringUTFChars(envptr, tempstr, 0);
		ats+=sprintf(ats, "%s,%04x:%04x\n", strdevname,
			vendorId,
			productId );

		if( vendorId == vid && productId == pid )
		{
			strncpy( deviceFile, strdevname, sizeof( deviceFile ) );
			printf( "Matching Device\n" );
			matchingDevice = device;
			env->ReleaseStringUTFChars( envptr, tempstr, strdevname );
			break;
		}
		env->ReleaseStringUTFChars( envptr, tempstr, strdevname );

		bHasNext = env->CallBooleanMethod( envptr, deviceListIterator, MethodhasNext );
	}

	printf( "Matching Device: %p\n", matchingDevice );

	if( !matchingDevice )
	{
		ats+=sprintf(ats, "No matching device\n" );
		return -2;
	}

	int ifaceCount = env->CallIntMethod( envptr, matchingDevice, MethodgetInterfaceCount );

	printf( "IFACE COUNT: %d\n", ifaceCount );

	jint numConfigurations = env->CallIntMethod( envptr, matchingDevice, MethodgetConfigurationCount );
	jmethodID MethodconfigurationgetId = env->GetMethodID( envptr, ClassUsbConfiguration, "getId", "()I" );
	jobject matchingConfiguration = 0;
	int i;
	int j;

//
// CHARLES: You have to first set config 1, then claim it, then you can switch to config 2, it seems.
	deviceConnection = env->CallObjectMethod( envptr, manager, MethodopenDevice, matchingDevice );
	if( !deviceConnection )
		goto requestPermission;

#if 0
	for( j = 0; j < ifaceCount; j++ )
	{
		jobject interface = env->CallObjectMethod( envptr, matchingDevice, MethodgetInterface, j );
		jint id = env->CallIntMethod( envptr, interface, MethodgetInterfaceId );
		jint alt = env->CallIntMethod( envptr, interface, MethodgetAlternateSetting );
		jobject tempstr = env->CallObjectMethod( envptr, interface, MethodgetInterfaceString );
		jobject tempstrname = env->CallObjectMethod( envptr, interface, MethodgetInterfaceName );

		const char *strinterface = env->GetStringUTFChars(envptr, tempstr, 0);
		const char *strname = tempstrname ? env->GetStringUTFChars(envptr, tempstrname, 0) : 0;
		printf( "INTERFACE: %d: %s\n", j, strname );
		env->ReleaseStringUTFChars( envptr, tempstr, strinterface );
		if( tempstrname ) env->ReleaseStringUTFChars( envptr, tempstrname, strname );
		jboolean claimOk = env->CallBooleanMethod( envptr, deviceConnection,
			MethodclaimInterface, interface, 1 /* Disconnect kernel if needed */ );
		printf( "PRECLAIM %d\n", claimOk );
	}
	env->CallVoidMethod( envptr, deviceConnection, Methodclose );

	for( i = 0; i < numConfigurations; i++ )
	{
		jobject configuration = env->CallObjectMethod( envptr, matchingDevice, MethodgetConfiguration, i );
		int id = env->CallIntMethod( envptr, configuration, MethodconfigurationgetId );
		if( id == 2 ) //XXX
			matchingConfiguration = configuration;
	}

	if( matchingConfiguration == 0 )
	{
		ats+=sprintf(ats, "No matching configuration\n" );
		return -9;
	}

	deviceConnection = env->CallObjectMethod( envptr, manager, MethodopenDevice, matchingDevice );
	if( !deviceConnection )
		goto requestPermission;

	jboolean configOk = env->CallBooleanMethod( envptr, deviceConnection,
		MethodsetConfiguration, matchingConfiguration );

	if( !configOk )
	{
		ats+=sprintf(ats, "warning: setConfiguration BAD\n" );
	}

	for( j = 0; j < ifaceCount; j++ )
	{
		jobject interface = env->CallObjectMethod( envptr, matchingDevice, MethodgetInterface, j );
		jint id = env->CallIntMethod( envptr, interface, MethodgetInterfaceId );
		jint alt = env->CallIntMethod( envptr, interface, MethodgetAlternateSetting );
		jobject tempstr = env->CallObjectMethod( envptr, interface, MethodgetInterfaceString );
		jobject tempstrname = env->CallObjectMethod( envptr, interface, MethodgetInterfaceName );

		const char *strinterface = env->GetStringUTFChars(envptr, tempstr, 0);
		const char *strname = tempstrname ? env->GetStringUTFChars(envptr, tempstrname, 0) : 0;
		ats+=sprintf(ats, "id: %d alt: %d %s\n", id, alt, strname );
		printf( "INTERFACE: %d: %s\n", j, strinterface );
		env->ReleaseStringUTFChars( envptr, tempstr, strinterface );
		if( tempstrname ) env->ReleaseStringUTFChars( envptr, tempstrname, strname );

		// XXX TODO: Select based off of mClass=10,mSubclass=0,mProtocol=0 and mEndpoints
		if( j == 3 ) //XXX
		{
			matchingInterface = interface;
		}

		jboolean preclaimOk = env->CallBooleanMethod( envptr, deviceConnection,
			MethodclaimInterface, interface, 1 /* Disconnect kernel if needed */ );
		ats+=sprintf(ats, "PCA %d\n", preclaimOk );		
	}

	if( !matchingInterface )
	{
		ats+=sprintf(ats, "NO MATCHING INTERFACES\n" );
		return -5;
	}

	// setConfiguration
	// claimInterface
	// setInterface

	jboolean setInterfaceOk = env->CallBooleanMethod( envptr, deviceConnection, MethodsetInterface, matchingInterface );
	if( setInterfaceOk )
		deviceConnectionFD = env->CallIntMethod( envptr, deviceConnection, MethodgetFileDescriptor );

	//Because we want to read and write to an interrupt endpoint, we need to claim the interface - it seems setting interfaces is insufficient here.
	jboolean claimOk = env->CallBooleanMethod( envptr, deviceConnection,
		MethodclaimInterface, matchingInterface, 1 /* Disconnect kernel if needed */ );

	ats+=sprintf(ats, " config: %d  claim: %d set: %d\n", configOk, claimOk, setInterfaceOk );

	//matchingInterface is of type android/hardware/usb/UsbInterface
	int epCount = env->CallIntMethod( envptr, matchingInterface, MethodgetEndpointCount );
	ats+=sprintf(ats, "Found device %d eps\n", epCount );
	for( i = 0; i < epCount; i++ )
	{
		jobject endpoint = env->CallObjectMethod( envptr, matchingInterface, MethodgetEndpoint, i );
		jint epnum = env->CallIntMethod( envptr, endpoint, MethodgetAddress );
		jint mps = env->CallIntMethod( envptr, endpoint, MethodgetMaxPacketSize );

		if( epnum == 0x02 ) matchingEpTX = endpoint;
		if( epnum == 0x81 ) matchingEpRX = endpoint;
		if( epnum == 0x02 || epnum == 0x81 )
		{
			// Open endpoint.
		}
	}			

	if( !matchingEpTX || !matchingEpRX )
	{
		ats+=sprintf(ats, "Could not find eps (%d/%d/%d)\n", matchingDevice, matchingEpTX, matchingEpRX );
		return -2;
	}

	jobject tempstr = env->CallObjectMethod( envptr, matchingDevice, MethodgetSerialNumber );
	if( !tempstr )
	{
		printf( "No tempstr.  Aborting to request permission\n" );
		goto requestPermission;
	}
	const char *strserial = env->GetStringUTFChars(envptr, tempstr, 0);
	int len = strlen( strserial );
	if( len >= seriallen ) len = seriallen-1;
	memcpy( serial, strserial, len );
	serial[len] = 0;
	env->ReleaseStringUTFChars( envptr, tempstr, strserial );

	ats+=sprintf(ats, "DC: %d; CL %d FD %d IF %d\n", !!deviceConnection, claimOk, deviceConnectionFD, setInterfaceOk );

	

	matchingEpTX = env->NewGlobalRef(envptr, matchingEpTX);
	matchingEpRX = env->NewGlobalRef(envptr, matchingEpRX);
#endif
	deviceConnectionFD = env->CallIntMethod( envptr, deviceConnection, MethodgetFileDescriptor );
	deviceConnection = env->NewGlobalRef(envptr, deviceConnection);

	jnii->DetachCurrentThread( jniiptr );

	printf( "deviceConnectionFD: %d\n", deviceConnectionFD );

	struct usbfs_disconnect_claim {
		unsigned int interface;
		unsigned int flags;
		char driver[USBFS_MAXDRIVERNAME + 1];
	};

	struct usbfs_disconnect_claim dc;

		for( j = 0; j < 8; j++ )
		{
			dc.interface = j;
			strcpy(dc.driver, "usbfs");
			dc.flags = USBFS_DISCONNECT_CLAIM_EXCEPT_DRIVER;
			int r = ioctl(deviceConnectionFD, IOCTL_USBFS_DISCONNECT_CLAIM, &dc);

			dc.flags = USBFS_DISCONNECT_CLAIM_IF_DRIVER;
			int r2 = ioctl(deviceConnectionFD, IOCTL_USBFS_DISCONNECT_CLAIM, &dc);
			printf( "DC %d: %d %d\n", j, r, r2 );
		}

	for( j = 0; j < 8; j++ )
	{
		unsigned int iface = j;
		int r3 = ioctl(deviceConnectionFD, IOCTL_USBFS_CLAIMINTERFACE, &iface);
		printf( "CLAIM: %d\n", r3 );
	}


	int config;
	for( config = 0; config < 4; config++ )
	{
		//int config = SELECTED_CONFIGURATION;
		int re = ioctl(deviceConnectionFD, IOCTL_USBFS_SETCONFIGURATION, &config);

		printf( "IOCTL_USBFS_SETCONFIGURATION: %d\n", re );
	}

	for( j = 0; j < 8; j++ )
	{
		unsigned int iface = j;
		int r3 = ioctl(deviceConnectionFD, IOCTL_USBFS_CLAIMINTERFACE, &iface);
		printf( "CLAIM: %d\n", r3 );
	}


	struct usbfs_setinterface setintf;
	setintf.interface = 1;
	setintf.altsetting = 1;
	int re = ioctl(deviceConnectionFD, IOCTL_USBFS_SETINTERFACE, &setintf);
	printf( "IOCTL_USBFS_SETINTERFACE: %d\n", re );

//		int as = libusb_set_interface_alt_setting( device_handle, 1 /* Device Configuration */, 1 );
//		int ct = libusb_get_configuration( device_handle, &config );


	return (!deviceConnectionFD)?-5:0;

requestPermission:

	// 	hasPermission(UsbDevice device) 
	if( OGGetAbsoluteTime() - dTimeOfLastAsk < 5 )
	{
		ats+=sprintf(ats, "Asked for permission.  Waiting to ask again." );
	}
	else if( env->CallBooleanMethod( envptr, manager, MethodhasPermission, matchingDevice ) )
	{
		ats+=sprintf(ats, "Has permission - disconnected?" );
	}
	else
	{
		//android.app.PendingIntent currently setting to 0 (null) seems not to cause crashes, but does force lock screen to happen.
		//Because the screen locks we need to do a much more complicated operation, generating a PendingIntent.  See Below.
		//  			env->CallVoidMethod( envptr, manager, MethodrequestPermission, matchingDevice, 0 );

		//This part mimiced off of:
		//https://www.programcreek.com/java-api-examples/?class=android.hardware.usb.UsbManager&method=requestPermission
		// manager.requestPermission(device, PendingIntent.getBroadcast(context, 0, new Intent(MainActivity.ACTION_USB_PERMISSION), 0));
		jclass ClassPendingIntent = env->FindClass( envptr, "android/app/PendingIntent" );
		jclass ClassIntent = env->FindClass(envptr, "android/content/Intent");
		jmethodID newIntent = env->GetMethodID(envptr, ClassIntent, "<init>", "(Ljava/lang/String;)V");
		jstring ACTION_USB_PERMISSION = env->NewStringUTF( envptr, "com.android.recipes.USB_PERMISSION" );
		jobject intentObject = env->NewObject(envptr, ClassIntent, newIntent, ACTION_USB_PERMISSION);

		jmethodID MethodgetBroadcast = env->GetStaticMethodID( envptr, ClassPendingIntent, "getBroadcast", 
			"(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;" );
		jobject pi = env->CallStaticObjectMethod( envptr, ClassPendingIntent, MethodgetBroadcast, lNativeActivity, 0, intentObject, 0 );

		//This actually requests permission.
		env->CallVoidMethod( envptr, manager, MethodrequestPermission, matchingDevice, pi );
		dTimeOfLastAsk = OGGetAbsoluteTime();
	}

    if (env->ExceptionCheck(envptr)) {
        env->ExceptionDescribe(envptr);
        env->ExceptionClear(envptr);
        printf( "Permissions Failed\n" );
    } else {
        printf( "Permissions Passed\n" );
    }

	return -5;
}











#if 0

#if 0
		libusb_set_auto_detach_kernel_driver( device_handle, 0 );
		int detach = libusb_detach_kernel_driver( device_handle, 0 );
		int ce = libusb_set_configuration( device_handle, CDC_CONFIG_MODE );
		int config;
		int claim0 = libusb_claim_interface( device_handle, 0 );
		int claim1 = libusb_claim_interface( device_handle, 1 );
		int as = libusb_set_interface_alt_setting( device_handle, 1 /* Device Configuration */, 1 );
		int ct = libusb_get_configuration( device_handle, &config );
		printf( "AS/CL0/1/CT: %d/%d/%d/%d\n", as, claim0, claim1, ct );
		if( as < 0 || claim1 != 0 || config != CDC_CONFIG_MODE )
		{
			fprintf( stderr, "Error: Could not claim interface (Detach Error %d / Claim Error: %d/%d / AS: %d / CE: %d)\n",
				detach, claim0, claim1, as, ce );
			libusb_close(device_handle);
			continue;
		}
#endif

#if 0
	//see https://android.googlesource.com/platform/frameworks/base/+/master/core/java/android/hardware/usb/UsbDeviceConnection.java
	//Calls: native_bulk_request -> android_hardware_UsbDeviceConnection_bulk_request -> usb_device_bulk_transfer
	//				UsbEndpoint endpoint, byte[] buffer, int length, int timeout
	//bulkTransfer(UsbEndpoint endpoint, byte[] buffer, int length, int timeout) 

	//UsbDeviceConnection bulkTransfer

	if( matchingEp && matchingDevice )
	{
		//UsbDeviceConnection deviceConnection = manager.openDevice( device )
		deviceConnection = env->CallObjectMethod( envptr, manager, MethodopenDevice, matchingDevice );
		//jint epnum = env->CallIntMethod( envptr, matchingEp, MethodgetAddress );

		if( !deviceConnection )
		{
			// 	hasPermission(UsbDevice device) 

			if( OGGetAbsoluteTime() - dTimeOfLastAsk < 5 )
			{
				ats+=sprintf(ats, "Asked for permission.  Waiting to ask again." );
			}
			else if( env->CallBooleanMethod( envptr, manager, MethodhasPermission, matchingDevice ) )
			{
				ats+=sprintf(ats, "Has permission - disconnected?" );
			}
			else
			{
				//android.app.PendingIntent currently setting to 0 (null) seems not to cause crashes, but does force lock screen to happen.
				//Because the screen locks we need to do a much more complicated operation, generating a PendingIntent.  See Below.
				//  			env->CallVoidMethod( envptr, manager, MethodrequestPermission, matchingDevice, 0 );

				//This part mimiced off of:
				//https://www.programcreek.com/java-api-examples/?class=android.hardware.usb.UsbManager&method=requestPermission
				// manager.requestPermission(device, PendingIntent.getBroadcast(context, 0, new Intent(MainActivity.ACTION_USB_PERMISSION), 0));
				jclass ClassPendingIntent = env->FindClass( envptr, "android/app/PendingIntent" );
				jclass ClassIntent = env->FindClass(envptr, "android/content/Intent");
				jmethodID newIntent = env->GetMethodID(envptr, ClassIntent, "<init>", "(Ljava/lang/String;)V");
				jstring ACTION_USB_PERMISSION = env->NewStringUTF( envptr, "com.android.recipes.USB_PERMISSION" );
				jobject intentObject = env->NewObject(envptr, ClassIntent, newIntent, ACTION_USB_PERMISSION);

				jmethodID MethodgetBroadcast = env->GetStaticMethodID( envptr, ClassPendingIntent, "getBroadcast", 
					"(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;" );
				jobject pi = env->CallStaticObjectMethod( envptr, ClassPendingIntent, MethodgetBroadcast, lNativeActivity, 0, intentObject, 0 );

				//This actually requests permission.
				env->CallVoidMethod( envptr, manager, MethodrequestPermission, matchingDevice, pi );
				dTimeOfLastAsk = OGGetAbsoluteTime();
			}
		}
		else
		{
			//Because we want to read and write to an interrupt endpoint, we need to claim the interface - it seems setting interfaces is insufficient here.
			jboolean claimOk = env->CallBooleanMethod( envptr, deviceConnection, MethodclaimInterface, matchingInterface, 1 );
			//jboolean claimOk = env->CallBooleanMethod( envptr, deviceConnection, MethodsetInterface, matchingInterface );
			//jboolean claimOk = 1;
			if( claimOk )
			{
				deviceConnectionFD = env->CallIntMethod( envptr, deviceConnection, MethodgetFileDescriptor );
			}

			ats+=sprintf(ats, "DC: %p; Claim: %d; FD: %d\n", deviceConnection, claimOk, deviceConnectionFD );
		}

	}
#endif
#endif

