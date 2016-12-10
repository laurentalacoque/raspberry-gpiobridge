gatekeeper: gatekeeper.c
	gcc -o gatekeeper -g gatekeeper.c -lwiringPi -lconfig -lmicrohttpd -lsqlite3
install: gatekeeper gatekeeperdmn
	install -o root -g root -m 755 gatekeeper /usr/bin/gatekeeper
	install -d -o root -g root -m 755 /etc/gatekeeper/
	install -o root -g root -m 755 gatekeeper.cfg /etc/gatekeeper/gatekeeper.cfg
	#install -o root -g root -m 755 gatekeeperdmn /etc/init.d/gatekeeperdmn
	#update-rc.d gatekeeperdmn defaults
install-dep :
	sudo apt-get install libconfig-dev
	sudo apt-get install libmicrohttpd-dev
	sudo apt-get install libsqlite3-dev
