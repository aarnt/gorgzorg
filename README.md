## GorgZorg is a simple CLI network file transfer tool

Do you need to copy files or folders between Unix machines but don't want to create shares?
So, this is the tool you have been waiting for.

### What you need to compile GorgZorg

* QMake or CMake
* Qt5 toolkit

### How to compile GorgZorg using QMake
```
$git clone https://github.com/aarnt/gorgzorg
$cd gorgzorg
$qmake-qt5
$make
```

### How to compile GorgZorg using CMake

```
$git clone https://github.com/aarnt/gorgzorg
$cd gorgzorg
$cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
$make
```

### How to use GorgZorg

    -h: Show this help
    -v: Verbose mode. When gorging, show speed. When zorging, show bytes received
    -c <IP>: Set IP or name to connect to
    -d <ms>: Set delay to wait between file transfers (in ms, default is 100)
    -tar: Use tar to archive contents of relative path
    -g <relativepath>: Set a relative filename or relative path to gorg (send)
    -p <portnumber>: Set port to connect or listen to connections (default is 10000)
    -z [IP]: Enter Zorg mode (listen to connections). If IP is ommited, GorgZorg will guess it


### Examples

```
#Send contents of Test directory to IP 192.168.1.1
gorgzorg -c 192.168.1.1 -g Test  

#Send archived contents of Crucial directory to IP 172.16.20.21
gorgzorg -c 172.16.20.21 -g Crucial -tar

#Start a GorgZorg server on address 192.168.10.16:20000
gorgzorg -p 20000 -z 192.168.10.16
```
