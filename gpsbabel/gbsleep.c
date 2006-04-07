/*
    OS abstraction to sleep a given number of milliseconds.

    Copyright (C) 2006 Robert Lipe, robertlipe@usa.net

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA

 */
#include "config.h"

#if __WIN32__

#include <winbase.h>
void
gb_sleep(unsigned long microseconds)
{
	Sleep(microseconds/1000 + 1);
}

#elif defined HAVE_NANOSLEEP

#include <time.h>
gb_sleep(unsigned long microseconds)
{
	struct timespec req;
	req.tv_sec  = 0;
	req.tv_nsec = microseconds * 1000;
	nanosleep(&req, NULL);
}
#endif
