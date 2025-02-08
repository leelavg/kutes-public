/*
  Copyright 2010,2023 Karl Robillard

  This file is part of the Boron programming language.

  Boron is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Boron is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with Boron.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <poll.h>
#include <errno.h>
#define INFINITE -1
typedef int DWORD;
#endif

#include "boron.h"
#include "boron_internal.h"


#define MAX_PORTS   16      // LIMIT: Maximum ports wait can handle.


typedef struct
{
    const UCell* cell;
    UPortDevice* dev;
}
PortInfo;


typedef struct
{
#ifdef _WIN32
    DWORD portCount;
    DWORD timeout;
    HANDLE handles[ MAX_PORTS ];
    PortInfo ports[ MAX_PORTS ];
#else
    int portCount;
    int timeout;
    struct pollfd readfds[ MAX_PORTS ];
    const UCell* ports[ MAX_PORTS ];
#endif
}
WaitInfo;


static void _waitOnPort( UThread* ut, WaitInfo* wi, const UCell* portC )
{
    int fd;
    PORT_SITE(dev, pbuf, portC);
    if( dev )
    {
        assert( wi->portCount < MAX_PORTS );
#ifdef _WIN32
        fd = dev->waitFD( pbuf, wi->handles + wi->portCount );
        if( fd > -1 )
        {
            int i = wi->portCount++;
            wi->ports[i].cell = portC;
            wi->ports[i].dev  = dev;
        }
#else
        fd = dev->waitFD( pbuf );
        if( fd > -1 )
        {
            int i = wi->portCount++;
            wi->ports[i] = portC;
            wi->readfds[i].fd = fd;
            wi->readfds[i].events = POLLIN;
        }
#endif
    }
}


static int _fillWaitInfo( UThread* ut, WaitInfo* wi,
                          const UCell* it, const UCell* end )
{
    while( it != end )
    {
        if( ur_is(it, UT_INT) )
        {
            wi->timeout = ur_int(it) * 1000;
        }
        else if( ur_is(it, UT_WORD) )
        {
            const UCell* cell;
            if( ! (cell = ur_wordCell( ut, it )) )
                return UR_THROW;
            if( ur_is(cell, UT_PORT) )
                _waitOnPort( ut, wi, cell );
        }
        else if( ur_is(it, UT_PORT) )
        {
            _waitOnPort( ut, wi, it );
        }
        else if( ur_is(it, UT_DOUBLE) || ur_is(it, UT_TIME) )
        {
            wi->timeout = (DWORD) (ur_double(it) * 1000.0);
        }

        ++it;
    }
    return UR_OK;
}


/*-cf-
    wait
        target  int!/double!/time!/block!/port!
    return: Port ready for reading or none.
    group: io

    Wait for data on ports.
*/
// (target -- port)
CFUNC_PUB( cfunc_wait )
{
    WaitInfo wi;
    DWORD n;

    wi.timeout = INFINITE;
    wi.portCount = 0;

    if( ur_is(a1, UT_BLOCK) )
    {
        UBlockIt bi;
        ur_blockIt( ut, &bi, a1 );
        if( ! _fillWaitInfo( ut, &wi, bi.it, bi.end ) )
            return UR_THROW;
    }
    else
    {
        if( ! _fillWaitInfo( ut, &wi, a1, a1 + 1 ) )
            return UR_THROW;
    }

#ifdef _WIN32
    n = WaitForMultipleObjects( wi.portCount, wi.handles, FALSE, wi.timeout );
    if( n == WAIT_FAILED )
    {
        return ur_error( ut, UR_ERR_INTERNAL,
                         "WaitForMultipleObjects - %d\n", GetLastError() );
    }
    else if( n != WAIT_TIMEOUT )
    {
        n -= WAIT_OBJECT_0;
        if( n < wi.portCount )
        {
            const UCell* portC = wi.ports[ n ].cell;

            // Windows polling is a mess; each type of object requires
            // special handling.
            wi.ports[ n ].dev->waitFD( ur_buffer(portC->port.buf), NULL );

            *res = *portC;
            return UR_OK;
        }
    }
#else
    n = poll( wi.readfds, wi.portCount, wi.timeout );
    if( n == -1 )
        return ur_error(ut, UR_ERR_INTERNAL, "poll - %s\n", strerror(errno));
    if( n )
    {
        int i;
        for (i = 0; i < wi.portCount; ++i)
        {
            if (wi.readfds[i].revents & POLLIN)
            {
                *res = *wi.ports[i];
                return UR_OK;
            }
        }
    }
#endif

    ur_setId( res, UT_NONE );
    return UR_OK;
}


/*EOF*/
