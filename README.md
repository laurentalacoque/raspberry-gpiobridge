# raspberry-gpiobridge
## What it is
This is a raspberry daemon that helps keep track of my Somfy freevia motor state.

The motor M1+- signals are connected to raspberry GPIO 0/1 (Through optocoupler)

This daemon keeps track of the gate state (open, opening, closed, closing), logs theses states in a sqlite3 database, push them to a web server (only GET requests) and starts and embedded webserver to report the current state (pull).

## Hardware
You will need handmade hardware for this to work: you should connect your somfy motor output (i.e. the part that goes to the motor) to a dedicated board (see Raspberry-gate-interface.png file in the source for inspiration). Caution: this was made for a motor output of +-20V, you should adapt the 1k7 input resistor to your own motor output voltage.

## Internal webserver pages
### /gateState
This page contains a single digit (0/1) to report if the state is closed/open

### /gateStateFull
This page contains a single digit (0-4) to report the gate state

0: unknown

1: opening

2: open

3: closing

4: closed

#### /gateStateHuman
This page contains a human readable state for the gate state.

## Push URLs
Apart from pulling some information from the raspberry, you can push events to a webserver or home automation box

### configuration
For push operations to work, you need to indicate the push host, port and URL in the configuration file and launch `gatekeeper -c <pathtoconfiguration>`

Say you want to report (simple) states to host 192.168.0.200 on port 80 with an url `/logdata?key=mystate&value=<thenewvalue>`

The `<thenewvalue>` part is easily replaced by the new value placeholder `@@`
```
pushHost           = "192.168.0.200"; // host to push to
pushPort           = 80; //push port
pushCoarseEvents   = 1; //should we push coarse events? (open, closed), use '@@' as placeholder for the value
PCEURL             = "/logdata?key=mystate&value=@@";
```

### jeedom configuration
Let's take an example. Suppose you've got a jeedom box on the same network at adress 192.168.0.200

You already configured a virtual element "Gate State" and added two informations lines "state" (binary) and "full state" (numeric)

You can check the ids of both informations on the left of the virtual element configuration page.
Let's say that "state" has id 350 and "full state" has id 351.

You'll also need to get your API key from the configuration page.

Then change the configuration file to this :

```
pushHost           = "192.168.0.200"; // host to push to
pushPort           = 80; //push port
pushDetailedEvents = 1; //should we push detailed events? (opening, open, closing, closed), use '@@' as placeholder for the value
PDEURL             = "/core/api/jeeApi.php?api=<YOURAPIKEY>&type=virtual&id=351&value=@@";
pushCoarseEvents   = 1; //should we push coarse events? (open, closed), use '@@' as placeholder for the value
PCEURL             = "/core/api/jeeApi.php?api=<YOURAPIKEY>&type=virtual&id=367&value=@@";
```
