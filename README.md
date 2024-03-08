# BitTorrent-Protocol

MPI File Sharing System
    The system consists of a tracker process and multiple peer
processes. The tracker keeps track of the files available in the
system, and peers download files from each other using multiple
threads.

Overview
    The system has two types of processes: tracker and peer. The
tracker maintains a list of files available in the system and
coordinates file transfers between peers. Each peer has its own
set of files and can request files from other peers.

Tracker:
    Manages file-sharing coordination between peers. Sends the
list of files to a destination process. The idea of implementantion
is simple, after initailisaition the tracker is in a "listening" 
state, practically the code is run in a wille(1) until all the 
download threads send "STOP" signal. Untill it stops, it tests if
any updates were sent, if there were it updates it's files list.
After that it waits for request messages, in form of a file name. 
Upon receiving a files name, it schearces for all the peers that
have the requested file and send an array with them and an array
with waht chunks they got.

Peer:
    After initialization, and receiving the start signal, the client
works 2 jobs, on one part it asks for chunks = download_thread and on
the other it provides chuncks = downloadt_thread. The download_thread
works until it got all of it's wanted files. It asks for the seeders
and what chunks they have, after that it requests 10 chunks from one
of them. It then updates the tracker of what chunks it got and
contiunes the shearch. The upload 

   There are utility functions for memory allocation, deallocation,
and communication.

Communication Channels
INIT_CHANNEL (1): Initialization channel for sending 
                  the list of files.
TRACKER_DTH_CHANNEL (0): Communication channel between 
                         tracker and download threads.
DTH_UTH_CHANNEL (2): Communication channel between 
                     download and upload threads.
UPDATE_CHANNEL (6): Channel for updating the tracker with 
                    the received files.
FINISH_CHANNEL (4): Signal channel to finish the upload 
                    thread.
ACK_CHANNEL (5): Acknowledgment channel.
