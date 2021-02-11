## GorgZorg is a simple network file transfer tool

Usage:


    -h: Show this help
    -c: Set IP or name to connect to
    -d: Set delay to wait between file transfers (in ms, default is 100)
    -g: Set filename or path to gorg (send)
    -p: Set port to connect or listen to connections (default is 10000)
    -z: Enter Zorg mode (listen to connections)


Examples:

```
    gorgzorg -c 192.168.0.1 -g Test  #Send contents of Test directory to 192.168.0.1 IP

    gorgzorg -p 20000 -z  #Start listening on port 20000
```
