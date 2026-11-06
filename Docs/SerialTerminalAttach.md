## Attaching a Serial Terminal 

A Serial Terminal is essential for debugging the system while it is running. Serial debugging involves sending debug output from the system over the serial port to a connected terminal.
By default, the XenevaOS kernel and user space send various debug outputs over the serial port. A Serial Terminal can be used to view the debug output from XenevaOS.

## NOTE
XenevaOS sends all kernel panic information over the serial port. It is necessary to set up a serial debugging environment to get meaningful insights into panic messages.

## Prerequisites

- _"vmwaregateway.exe"_ is a VMware utility often used for debugging guest operating systems. You can obtain it from [here](https://www.l4ka.org/159.php).
- _HyperTerminal_ is a terminal emulation program that allows users to connect to remote systems using serial ports, modems, or TCP/IP networks. Alternatively, you can use PuTTY, Tera Term, or RealTerm as an alternative to _HyperTerminal_. 

## Starting up Serial Terminal
- Open command prompt on your host machine
- Run _'vmwaregateway.exe'_ with the `/t` option:
```vmwaregateway.exe /t```
- Now open HyperTerminal and give a suitable name for
the session, for example "debug" and press 'OK'
- The _'Connect to'_ dialogue should open up automatically; type __"localhost"__ in the Host Address field.
- Set the Port Number to 567 and Connect Using to _TCP/IP(Winsock)_.
- HyperTerminal is now ready to be used. To start serial debugging, follow the steps below to connect the virtual machine to the serial terminal.

## Connecting to Serial Terminal in VMware 
- Make sure VMware Workstation is installed on your host machine.
- Go to the virtual machine settings in VMware 
- Add a new serial port
- Select "Use Named Pipe".
- Type the name for the pipe, _"\\.\pipe\vmwaredebug"_
- Set the "Near end" to _"This end is the client"_
- Set the "Far end" to _"The other end is an application"_
- Click "OK" to finish the process

## Connecting to Serial Terminal in VirtualBox
- Make sure VirtualBox is installed on your host machine.
- Go to the virtual machine settings in Virtual Box
- Go to Serial Port and open the Port 1 tab (_by default Port 1 tab is opened_)
- Check the _'Enable Serial Port'_
- Set the Port Number to __'COM1'__ and Port Mode to __Host Pipe__
- Check _'Connect to existing pipe/socket'_
- Type _"\\.\pipe\vmwaredebug"_ in Path/Address

## Conclusion
Please make sure all steps are performed correctly to connect the Serial Terminal to the virtual machine. This is a reliable process to monitor XenevaOS's behavior and diagnose issues. Any kernel exceptions or panics will only be visible over the Serial Terminal. Setting it up properly provides valuable insights into XenevaOS's debug outputs.

