nanows
======

A nano HTTP web server made to learn sockets.

Usage
-----

Compile it with gcc or clang, there is a simple Makefile you may use.

    make

By default it will be compiled on `./bin/nanows`. To serve a given dir:

    ./bin/nanows -l 8080 -s /path/to/dir/

That will run the server on port 8080 and serve the `/path/to/dir/` directory.

Default port is `5001` and path to serve `.`. If you have `nanows` on your PATH
you can go to any dir and type

    nanows

That it will serve that dir. Note that no protection is made not to serve files
outside that directory yet.

License
-------

Distributed under GPL, read the [LICENSE file](LICENSE) for more information.
