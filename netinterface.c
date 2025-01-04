#include <string.h>
#include <stdlib.h>
#include "netinterface.h"

const char ** netinterface_types;
netinterface*(* * netinterface_create_fns)( const char *, int );
int netinterface_num_create_fns = 0;

netinterface * netinterface_create( const char * interfacetype, const char * descriptor, int debuglevel )
{
	int i;
	for( i = 0; i < netinterface_num_create_fns; i++ )
	{
		if( !interfacetype || strcmp( interfacetype, netinterface_types[i] ) == 0 )
		{
			netinterface * tmp = (netinterface_create_fns[i])( descriptor, debuglevel );
			if( tmp ) return tmp;
		}
	} 
	return 0;
}

void netinterface_register( const char * interfacetype, netinterface*(*create)( const char *, int ) )
{
	netinterface_create_fns = realloc( netinterface_create_fns, ((netinterface_num_create_fns) + 1) * sizeof(void*) );
	netinterface_types = realloc( netinterface_types, ((netinterface_num_create_fns) + 1) * sizeof(void*) );
	netinterface_create_fns[netinterface_num_create_fns] = create;
	netinterface_types[netinterface_num_create_fns] = interfacetype;
	netinterface_num_create_fns++;
}

