libresman
---------

About
-----
Libresman is a standalone C library for managing the resources (data files) used
by applications. Mainly targeted towards games or similar graphics applications,
resman supports multithreaded background loading, and continuous file monitoring
to reload modified data files automatically.

Asynchronous loading of resources, is performed by calling user-supplied
loading, and completion callbacks. The user issues a request for a resource, and
libresman dispatches a worker thread to load it by calling the load callback.
After the load callback returns, resman schedules a completion callback to be
called synchronously in the context of the caller's thread, the next time the
application calls `resman_poll`.

The split load/completion callback mechanism can be used to safely create or
update OpenGL objects from the resource data. I/O, parsing and
decoding/decompressing of data files can be performed by the load callback in a
background thread, while the final calls to supply the data to OpenGL, which can
only be safely performed by the thread which created the OpenGL context
initially, can be delegated to the completion callback.

Libresman comes with an example OpenGL thumbnail viewer program, to demonstrate
how to use the library. 


License
-------
Copyright (C) 2014-2019 John Tsiombikas <nuclear@member.fsf.org>

libresman is free software, released under the terms of the GNU Lesser General
Public License v3 (or at your option, any later version published by the Free
Software Foundation). Read COPYING and COPYING.LESSER for details.


Build
-----
To build and install `libresman` on UNIX, run the usual:

    ./configure
    make
    make install

See `./configure --help` for build-time options. 

To build on windows, use msys2/mingw and follow the UNIX instructions above.

To cross-compile for windows with mingw-w64, try the following incantation:

    ./configure --prefix=/usr/i686-w64-mingw32
    make CC=i686-w64-mingw32-gcc AR=i686-w64-mingw32-ar sys=mingw
    make install sys=mingw
