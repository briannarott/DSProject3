#ifndef MEMBERSHIP_H
#define MEMBERSHIP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

// Membership Service - PARTS 1, 2, and 3


#define MAX_PEERS 10 // maximum number of peers is 10 
#define PORT 8080 // port for communication
#define MAX_BUFFER 1024 // maximum buffer size for messages

#define HEARTBEAT_INTERVAL 2 // sends heartbeats every 2 seconds
#define HEARTBEAT_TIMEOUT 4 // peer is unreachable after 4 seconds - twice the heartbeat interval 
#define UDP_PORT 8081 // diff port for UDP heartbeats

// defines types of messages exchanged between peers during updates to membership 
enum MessageType {
	JOIN = 1, // sent by new peer to request joining the group 
	REQ = 2, // sent by leader to existing members when processing a join 
	OK = 3, // sent by members to acknowledge a REQ message
	NEWVIEW = 4, // sent by leader to all members to update membership list 
	HEARTBEAT = 5 // PART2: each peer will broadcast a heartbeat message
};

// represents operations types that modify membership list
enum OperationType {
	ADD = 1, // op to add a new peer to list
	DEL = 2, // op to remove/delete a peer from list
	PENDING = 3, // op is waiting to be completed
	NOTHING = 4 // no op is pending 
};

// represents a message that can be sent between peers 
struct Message {
	enum MessageType type; // type of message (JOIN, REQ, OK, NEWVIEW)
	enum OperationType op_type; // type of op (ADD, DEL, PENDING, NOTHING)
	int request_id; // unique id for this request 
	int view_id; // current view number to track total order 
	int peer_id; // id of the peer this message is about 
	int member_count; // number of members in the current view
	int members[MAX_PEERS]; // array of member ids in current view 
};

// tracks an ongoing operations like adding a peer until all acknowledgments are received 
struct PendingOperation {
	int active; // flag indicating whether there's a pending op
	int request_id; // id of the pending request 
	enum OperationType type; // type of op (ADD or DEL)
	int peer_id; // peer being added/removed
	int ok_count; // number of OK responses received 
	int expected_oks; // number of OK responses needed to continue 
};

// maintains the state of a peer, including id, membership list, and leader 
struct PeerState { 
	int peer_id; // id of this peer
	int view_id; // current view number 
	int leader_id; //  id of current leader 
	int member_count; // number of current members 
	int members[MAX_PEERS]; // array of current member IDs
	char hostnames[MAX_PEERS][256]; // hostnames of all possible peers 
	int is_leader; // flag indicates if this peer is the leader or not 
	int next_request_id; // next request id to be used 
	struct PendingOperation pending_op; // tracks an ongoing operation 

	time_t last_heartbeat[MAX_PEERS]; // PART2: last time heartbeat was received from each peer
	int peer_active[MAX_PEERS]; // PART2: flag to track whether each peer is active 
	pthread_mutex_t state_mutex; // PART2: mutex to protect state updates
	int shutdown_flag; // PART2: flag to signal the 3 heartbeat related threads to exit 

	int failed_peer_id; // ID of the peer that was detected as failed 
	int has_failed_peer; // flag indicating whether a peer failure needs to be handled
};


// functions for part 1: add a peer to the membership list 
void init_state(char* hostsfile, int delay); // inits peer's state from hostsfile and adds init delay
void start_peer(); // starts peer's operation - either as a leader or normal member 
void handle_join(); // handles sending a JOIN message from a new peer to leader
void send_message(struct Message* msg, const char* hostname); // sends message to specific hostname (peer)
void* receive_messages(void* arg); // listens for and handles incoming messages 
void print_membership(); // prints current membership state in given format 

// functions for part 2: failure detector
void start_heartbeat_thread(); // inits and starts 3 heartbeat related threads 
void* send_heartbeats(void* arg); // thread that sends a UDP heartbeat message to all active peers in the membership list, runs forever and sends heartbeats every 2 secs 
void* monitor_heartbeats(void* arg); // thread that monitors in case of missing heartbeats 
void send_udp_heartbeat(int peer_id); // sends UDP heartbeat msg to specific peer 
void* receive_heartbeats(void* arg); // thread that listens for incoming heartbeats
void crash_peer(int delay); // simulates a crash after a delay 

// functions for part 3: delete a peer from the membership list 
void remove_member(int peer_id);
void handle_failed_peer(); 

#endif 