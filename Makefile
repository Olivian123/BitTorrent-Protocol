build:
	mpicc -o BitTorrent_Protocol BitTorrent_Protocol.c -pthread -Wall

clean:
	rm -rf BitTorrent_Protocol
