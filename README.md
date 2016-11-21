# raspberry-gpiobridge
This httpd/http bridge monitors and controls the state of a Somfy Freevia gate controler (as well as a doorbell state) and keeps the gate current state (closed, opening, open, closing). It can be controlled and checked through http requests.
This code is really tied to the hardware architecture I'm using and requires Somfy BUS, M1+ and M1- signals connected to the raspberry GPIO through optocouplers.
