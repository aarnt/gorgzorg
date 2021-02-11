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
    -c: Set IP or name to connect to
    -d: Set delay to wait between file transfers (in ms, default is 100)
    -g: Set a relative filename or relative path to gorg (send)
    -p: Set port to connect or listen to connections (default is 10000)
    -z: Enter Zorg mode (listen to connections)


Examples:

```
#Send contents of Test directory to IP 192.168.0.1
gorgzorg -c 192.168.0.1 -g Test  

#Start listening on port 20000
gorgzorg -p 20000 -z i
 ```
