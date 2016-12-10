# raspberry-gpiobridge
## What it is
This is a raspberry daemon that helps keep track of my Somfy freevia motor state.

The motor M1+- signals are connected to raspberry GPIO 0/1 (Through optocoupler)

This daemon keeps track of the gate state (open, opening, closed, closing), logs theses states in a sqlite3 database, push them to a web server (only GET requests) and starts and embedded webserver to report the current state (pull).

## Internal webserver pages
# /gateState
This page contains a single digit (0/1) to report if the state is closed/open

# /gateStateFull
This page contains a single digit (0-4) to report the gate state

0: unknown

1: opening

2: open

3: closing

4: closed

# /gateStateHuman
This page contains a human readable state for the gate state.
