## GorgZorg is a simple network file transfer tool

Compilation:

```
$git clone https://github.com/aarnt/gorgzorg
$cd gorgzorg
$cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
$make
```

Usage:

    -h: Show this help
    -c <IP>: Set IP or name to connect to
    -d <ms>: Set delay to wait between file transfers (in ms, default is 100)
    -tar: Use tar to archive contents of relative path
    -g <relativepath>: Set a relative filename or relative path to gorg (send)
    -p <portnumber>: Set port to connect or listen to connections (default is 10000)
    -z [IP]: Enter Zorg mode (listen to connections). If IP is ommited, GorgZorg will guess it


Examples:

```
#Send contents of Test directory to IP 192.168.0.1
gorgzorg -c 192.168.0.1 -g Test  

#Start listening on port 20000 with address 192.168.10.16
gorgzorg -p 20000 -z 192.168.10.16
```
