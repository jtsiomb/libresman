/*
libresman - a multithreaded resource data file manager.
Copyright (C) 2014-2016  John Tsiombikas <nuclear@member.fsf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef NOWATCH
#include "resman_impl.h"

int resman_init_file_monitor(struct resman *rman)
{
	return 0;
}

void resman_destroy_file_monitor(struct resman *rman)
{
}

int resman_start_watch(struct resman *rman, struct resource *res)
{
	return 0;
}

void resman_stop_watch(struct resman *rman, struct resource *res)
{
}

void resman_check_watch(struct resman *rman)
{
}

#else
int resman_filewatch_dummy_silence_empty_file_warning;
#endif
