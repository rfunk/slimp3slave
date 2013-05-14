slimp3slave
===========

slimp3slave is a console application implementation of a
[SLIMP3 client](http://wiki.slimdevices.com/index.php/SLIMP3).
It was written to avoid the irritating lag that you get when pointing an
MP3 player at the
[Squeezebox server's](http://wiki.slimdevices.com/index.php/Squeezebox_Server)
`http://.../stream.mp3` stream.

slimp3slave makes use of a command line MP3 player that reads from
standard input.  I recommend `madplay`, or failing that `splay`.  `mpg123`
is not recommended as it does not handle partial frames gracefully.

To use slimp3slave, type `make`, then run `slimp3slave` optionally
specifying the server IP address:

    ./slimp3slave -s 1.2.3.4

For more information:

    ./slimp3slave -h

To run slimp3slave with the UI enabled, using the `-l` option.  To start
slimp3slave in its own window, try:

    xterm -geometry 42x5 -e slimp3slave -l -s 1.2.3.4

slimp3slave was originally written as an audio-only client by Paul
Warren <pdw@ex-parrot.com> and has since been extended to include a UI
implentation using curses by Rob Funk <rfunk@funknet.net>.

The original canonical site for slimp3slave is:
<http://www.ex-parrot.com/slimp3slave>

This git repository was created from the slimp3slave CVS repository at:
`:pserver:anonymous@sphinx.mythic-beasts.com:/home/pdw/vcvs/repos`
