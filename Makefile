gatekeeper: gatekeeper.c
	gcc -o gatekeeper -g gatekeeper.c -lwiringPi -lconfig -lmicrohttpd
install-dep :
	sudo apt-get install libconfig-dev
	sudo apt-get install libmicrohttpd-dev
