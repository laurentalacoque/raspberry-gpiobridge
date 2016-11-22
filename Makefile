gatekeeper: gatekeeper.c
	gcc -o gatekeeper gatekeeper.c -lwiringPi -lconfig
install-dep :
	sudo apt-get install libconfig-dev
