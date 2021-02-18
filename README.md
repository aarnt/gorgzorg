## GorgZorg is a simple CLI network file transfer tool

Do you need to copy files or folders between Unix machines but don't want to create shares/memorize complex syntax?

So, this is the tool you've been looking for.

### What you need to compile GorgZorg

* QMake or CMake
* Qt6/Qt5 toolkit

### How to compile GorgZorg using QMake
```
$git clone https://github.com/aarnt/gorgzorg
$cd gorgzorg
$qmake-qt6 (or qmake-qt5)
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

    -c <IP>: Set GorgZorg server IP to connect to
    -d <path>: Set directory in which received files are saved
    -g <pathToGorg>: Set a filename or path to gorg (send)
    -h: Show this help
    -p <portnumber>: Set port to connect or listen to connections (default is 10000)
    -q: Quit zorging after transfer is complete
    -tar: Use tar to archive contents of path
    -v: Verbose mode. When gorging, show speed. When zorging, show bytes received
    -y: When zorging, automatically accept any incoming file/path
    -z [IP]: Enter Zorg mode (listen to connections). If IP is ommited, GorgZorg will guess it
    -zip: Use gzip to compress contents of path


### Examples

```
#Send file /home/user/Projects/gorgzorg/LICENSE to IP 10.0.1.60 on port 45400
gorgzorg -c 10.0.1.60 -g /home/user/Projects/gorgzorg/LICENSE -p 45400

#Send contents of Test directory to IP 192.168.1.1 on (default) port 10000
gorgzorg -c 192.168.1.1 -g Test  

#Send archived contents of Crucial directory to IP 172.16.20.21
gorgzorg -c 172.16.20.21 -g Crucial -tar

#Send contents of filter expression in a gziped tarball to IP 192.168.0.100
gorgzorg -c 192.168.0.100 -g '/home/user/Documents/*.txt' -zip

#Start a GorgZorg server on address 192.168.10.16:20000 using directory 
#"/home/user/gorgzorg_files" to save received files
gorgzorg -p 20000 -z 192.168.10.16 -d ~/gorgzorg_files

#Start a GorgZorg server on address 172.16.11.43 on (default) port 10000
#Always accept transfers and quit just after receiving one
gorgzorg -z 172.16.11.43 -y -q
```
