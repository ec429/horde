(HORDE (is-a (server (of HTTP))))

Dispatcher:
Is the initial process, holds the listen()ing socket, has a pipe() to each child produced.
When a request comes in on socket, fork() a 'net' process
When a request comes in over a pipe, fork() whatever process is registered to handle it

This specification is not only incomplete but also outdated; in particular, the following transcript, while a reasonable way of doing things, does not correspond to the current implementation (for instance, path resolution in this case actually triggers a 302; proc doesn't use a sock_un but instead reads/writes the data through disp).

Example Transcript; {} denotes hex-encoding.  Internally generated message data are hex-encoded iff they contain parens or spaces or begin with a '#' (wh. is used to indicate hex-encoding).  User-input data may be hex-encoded unnecessarily.
disp fork() exec(path)
disp fork() exec(stats)
C>disp connect
disp accept()
disp fork() exec(net)
C>net: GET / HTTP/1.0
net>disp: (path {/})
disp>path: (path (from net) {/})
path>disp: (path (to net) {/index.html})
disp>net: (path {/index.html})
net>disp: (proc {/index.html})
disp fork() exec(proc)
disp>proc: (proc (from net) {/index.html})
proc reads index.html; it contains some <?pico> tags and otherwise matches no rules
proc opens a sock_un, /tmp/.horde_[pid] (hereonin 'sock')
proc>disp: (pico {/tmp/.horde_[pid]})
disp fork() exec(pico)
disp>pico: (pico (from proc) {/tmp/.horde_[pid]})
pico>sock>proc: (waiting pico)
proc>sock>pico: (pico {[page html data]})
pico>disp: (uptime)
disp>pico: (uptime {horde: 8 days, 15:37:16 | system: 73 days, 01:49:58})
pico>sock>proc: (pico {[page html data, pico-processed]})
pico closes sock
proc closes sock
proc>disp (proc (to net) {[page html data, pico-processed]})
disp>net (proc {[page html data, pico-processed]})
net>C: the HTTP response, 200 OK and all the data
net>disp (stats (time {[time_t]}) (ip {[IP]}) (status 200) (bytes {[bytes]}) (file {/index.html}) /*...etc...*/ append)
disp>stats (stats (from net) /*...etc...*/ append)
stats>disp (stats (to net) append)
disp>net (stats append)
/* we assume pipelining was not requested */
net>disp (close)
net return()s
stdin>disp: (shutdown)
disp>* SIGHUP
path return()s
proc return()s
stats return()s
pico return()s
disp>stdin: (shutdown (when now))
disp return()s
