gatekeeper: gatekeeper.c
	gcc -o gatekeeper -g gatekeeper.c -lwiringPi -lconfig -lmicrohttpd -lsqlite3
install-dep :
	sudo apt-get install libconfig-dev
	sudo apt-get install libmicrohttpd-dev
	sudo apt-get install libsqlite3-dev
