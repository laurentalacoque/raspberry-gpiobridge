gatekeeper: gatekeeper.c
	gcc -o gatekeeper gatekeeper.c -lwiringPi
install-dep :
	sudo apt-get install libconfig-dev
