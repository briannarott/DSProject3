#include "membership.h"

/*
Membership Service - Part 1: Add a peer to the membership list 
Part 2: Failure Detector - each peer will implement a failure detector 
Part 3: Delete a peer from the membership list
*/


struct PeerState state; // global var to store peer details

static int last_view_id = -1; // tracks last view_id to avoid duplicate prints

// PART 2 ------------------------------------------------------------------------------------------

// starts heartbeat related threads 
void start_heartbeat_thread() {
	pthread_t send_thread;
	pthread_t receive_thread;
	pthread_t monitor_thread;
    
    pthread_mutex_init(&state.state_mutex, NULL); // init mutex for thread safety
    
    // init heartbeat tracking
    time_t current_time = time(NULL);
    for (int i = 0; i < MAX_PEERS; i++) {
        state.last_heartbeat[i] = current_time;
        state.peer_active[i] = 0;
        
        // marks peers in membership list as active
        for (int j = 0; j < state.member_count; j++) {
            if (state.members[j] == i + 1) {
                state.peer_active[i] = 1;
                break;
            }
        }
    }
    
    // creates threads for heartbeat 
    pthread_create(&send_thread, NULL, send_heartbeats, NULL);
    pthread_create(&receive_thread, NULL, receive_heartbeats, NULL);
    pthread_create(&monitor_thread, NULL, monitor_heartbeats, NULL);
}

// thread that sends heartbeats to all members periodically 
void* send_heartbeats(void* arg) {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);  // opens socket once here 
    if (sock < 0) {
        perror("Error creating socket");
        return NULL;
    }

	while (1) {
        pthread_mutex_lock(&state.state_mutex); // locks mutex for consistent state
        
        // sends heartbeat to all active peers in membership list
        for (int i = 0; i < state.member_count; i++) {
            int peer_id = state.members[i];

            if (peer_id != state.peer_id && state.peer_active[peer_id - 1]) { // but don't send to itself & only if peer is still active 
                send_udp_heartbeat(peer_id);
            }
        }
        
        pthread_mutex_unlock(&state.state_mutex);
        sleep(HEARTBEAT_INTERVAL); // waits 2 seconds before sending again
    }
	close(sock); // closes socket when thread exits 
    return NULL;
}

// sends UDP heartbeat msg to specific peer 
void send_udp_heartbeat(int peer_id) {
	int sock = socket(AF_INET, SOCK_DGRAM, 0); // creates UDP socket
    
    struct Message heartbeat_msg = {
        .type = HEARTBEAT,
        .peer_id = state.peer_id,
        .view_id = state.view_id
    };
    
    struct hostent* host = gethostbyname(state.hostnames[peer_id - 1]);
	if (!host) {  // PART2: check if gethostbyname failed
        //fprintf(stderr, "Error: Could not resolve hostname for peer %d\n", peer_id);
        close(sock);
        return;
    }
    
    // sets up addr struct
    struct sockaddr_in peer_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_PORT),
        .sin_addr = *((struct in_addr*)host->h_addr)
    };

    sendto(sock, &heartbeat_msg, sizeof(struct Message), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr)); // sends UDP heartbeat
    close(sock);
}
    
// thread that listens for incoming heartbeats
void* receive_heartbeats(void* arg) {
	int sock = socket(AF_INET, SOCK_DGRAM, 0); // creates UDP socket
    
    // sets up addre struct
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,  // listens on all interfaces
        .sin_port = htons(UDP_PORT)
    };
    
    bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)); // binds socket to address
    
    while (!state.shutdown_flag) {
        struct Message heartbeat_msg;
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        
        recvfrom(sock, &heartbeat_msg, sizeof(struct Message), 0, (struct sockaddr*)&peer_addr, &addr_len); // receives heartbeat
        
        // processes only if it's a heartbeat message
        if (heartbeat_msg.type == HEARTBEAT) {
            pthread_mutex_lock(&state.state_mutex);
            
            // updates last heartbeat timestamp for this peer
            int peer_id = heartbeat_msg.peer_id;
            if (peer_id >= 1 && peer_id <= MAX_PEERS) {
                state.last_heartbeat[peer_id - 1] = time(NULL);
                state.peer_active[peer_id - 1] = 1;
            }
            
            pthread_mutex_unlock(&state.state_mutex);
        }
    }
    close(sock);
    return NULL;
}

// thread that monitors in case of missing heartbeats 
void* monitor_heartbeats(void* arg) {
	while (!state.shutdown_flag) {
        time_t current_time = time(NULL);
        
        pthread_mutex_lock(&state.state_mutex);

	    // checks for shutdown flag
        if (state.shutdown_flag) {
            pthread_mutex_unlock(&state.state_mutex);
            break;
        }
        
        // checks each peer in the membership list
        for (int i = 0; i < state.member_count; i++) {
            int peer_id = state.members[i];
            
            if (peer_id != state.peer_id && state.peer_active[peer_id - 1]) {
                // checks if heartbeat timeout exceeded
                if (current_time - state.last_heartbeat[peer_id - 1] > HEARTBEAT_TIMEOUT) {
                    // marks peer as inactive
                    state.peer_active[peer_id - 1] = 0;
                    
                    // prints failure detection message
                    if (peer_id == state.leader_id) {
                        fprintf(stderr, "{peer_id:%d, view_id: %d, leader: %d, message:\"peer %d (leader) unreachable\"}\n", state.peer_id, state.view_id, state.leader_id, peer_id);
                    } else {
                        fprintf(stderr, "{peer_id:%d, view_id: %d, leader: %d, message:\"peer %d unreachable\"}\n", state.peer_id, state.view_id, state.leader_id, peer_id);
                    }
                }
            }
        }
        
        pthread_mutex_unlock(&state.state_mutex);
        sleep(1); // checks every second
    }
    return NULL;
}

// simulates a crash after a delay 
void crash_peer(int delay) {
	if (delay > 0) {
        sleep(delay);
    }

	// signals threads to exit
    pthread_mutex_lock(&state.state_mutex);
    state.shutdown_flag = 1;
    pthread_mutex_unlock(&state.state_mutex);

    
    // prints crash message before exiting
    fprintf(stderr, "{peer_id:%d, view_id: %d, leader: %d, message:\"crashing\"}\n", state.peer_id, state.view_id, state.leader_id);
    
	sleep(1); // waits briefly so threads can clean up 
	pthread_mutex_destroy(&state.state_mutex); // destroys mutex
	exit(0);
}
   


// PART 1 (and PART 2) -----------------------------------------------------------------------------------------------

// initializes peer's state from given hostsfile and adds init delay
void init_state(char* hostsfile, int delay) {
	FILE* file = fopen(hostsfile, "r"); // opens hostsfile with list of all possible peers
	
	// read hostnames from file and stores them - line number + 1 = peer_id
	char hostname[256];
	int line = 0;
	while (fgets(hostname, sizeof(hostname), file) && line < MAX_PEERS) {
        	hostname[strcspn(hostname, "\n")] = 0;  // removes newline char
        	strcpy(state.hostnames[line], hostname); // stores hostname
        	line++;
   	 }
    fclose(file);

	// gets this peer's own hostname to determine ID
	char own_hostname[256];
	gethostname(own_hostname, sizeof(own_hostname));

	// finds own peer_id by matching hostname with hostsfile
	for (int i = 0; i < line; i++) {
    	if (strcmp(own_hostname, state.hostnames[i]) == 0) {
        	state.peer_id = i + 1;  // peer IDs start at 1
        	break;
    	}
    }

    pthread_mutex_init(&state.state_mutex, NULL); // init mutex
	state.shutdown_flag = 0; // init 
    time_t current_time = time(NULL); // init heartbeat tracking
    for (int i = 0; i < MAX_PEERS; i++) {
        state.last_heartbeat[i] = current_time;
        state.peer_active[i] = 0;
    }
	
	// inits peer's state vars
    state.view_id = 0; // init view starts at 0
    state.leader_id = 1; // first peer aka peer_id 1 is leader ALWAYS
    state.is_leader = (state.peer_id == 1); // checks if this peer is leader
    state.next_request_id = 1; // starts request IDs at 1
	state.peer_active[state.peer_id - 1] = 1; // marks itself as active
    
    // leader starts with itself in membership list and other peers start with empty list
    state.member_count = state.is_leader ? 1 : 0;
    if (state.is_leader) {
        state.members[0] = state.peer_id;
		state.member_count = 1;
		print_membership(); // only print init state if leader 
    }

    // if delay given, wait before starting
    if (delay > 0) {
        sleep(delay);
    }
}

// starts peer's operation - either leader or normal member 
void start_peer() {
    // creates thread to handle incoming messages
    pthread_t receive_thread;
    pthread_create(&receive_thread, NULL, receive_messages, NULL);

	start_heartbeat_thread(); // starts heartbeats

	/*
	when a peer starts it will contact the leader by sending a JOIN message
	*/
    if (!state.is_leader) { // if not leader, begin JOIN protocol
        handle_join();
    }

    pthread_join(receive_thread, NULL); // waits for receive thread to finish - but runs forever 
}

// handles sending a JOIN message to leader, requesting to be added to membership list 
void handle_join() {
    // creates JOIN message to send to leader
    struct Message join_msg = {
        .type = JOIN,
        .peer_id = state.peer_id,
        .view_id = state.view_id
    };

    send_message(&join_msg, state.hostnames[0]); // sends JOIN message to leader aka first hostname in file
}

// sends message to specific hostname
void send_message(struct Message* msg, const char* hostname) {
    int sock = socket(AF_INET, SOCK_STREAM, 0); // creates TCP socket
    struct hostent* host = gethostbyname(hostname); // hostname to IP address

    // sets up address structure for connection
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr = *((struct in_addr*)host->h_addr)
    };

    connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)); // connects to the target peer

    send(sock, msg, sizeof(struct Message), 0); // sends message 
    close(sock); // and closes connection
}

// helper to add a new member to membership list 
void add_member(int peer_id) {
	// checks if member already exists
	for (int i = 0; i < state.member_count; i++) {
        if (state.members[i] == peer_id) return;
    }
    
    state.members[state.member_count++] = peer_id; // adds new member

	// sorts members for consistent output
    for (int i = 0; i < state.member_count - 1; i++) {
        for (int j = 0; j < state.member_count - i - 1; j++) {
            if (state.members[j] > state.members[j + 1]) {
                int temp = state.members[j];
                state.members[j] = state.members[j + 1];
                state.members[j + 1] = temp;
            }
        }
    }
}

// helper to send NEWVIEW to all current members + a new member
void broadcast_newview(int new_peer_id) {
    struct Message newview_msg = {
        .type = NEWVIEW,
		.view_id = state.view_id,
        // .view_id = state.view_id + 1,
        .member_count = state.member_count,
    };
    
    memcpy(newview_msg.members, state.members, sizeof(int) * state.member_count); // copies current membership
    
    // sends to all current members
    for (int i = 0; i < state.member_count; i++) {
        send_message(&newview_msg, state.hostnames[state.members[i] - 1]);
    }
    
    // sends to new peer if not already in list
    if (new_peer_id > 0) {
        send_message(&newview_msg, state.hostnames[new_peer_id - 1]);
    }
}


// handles incoming messages 
void* receive_messages(void* arg) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0); // creates TCP socket for receiving 

    // sets up address structure for binding
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,  // listens on all interfaces
        .sin_port = htons(PORT)
    };

    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)); // binds socket to address

    listen(server_sock, 10); // starts listening for connections

    // main message receiving loop
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len); // accepts incoming connection

        // receives message from connection
        struct Message msg;
        recv(client_sock, &msg, sizeof(struct Message), 0);
        
        // handles message based on its type
        switch (msg.type) {
			/*
			Leader first sends a REQ msgs to all current peers in membership list, then waits for OK responses, then sends NEWVIEW to all mems including new one 
			*/ 
            case JOIN:
                if (state.is_leader) {
					// if leader is alone, directly add new mem - "LEADER does not need to send the REQ message and wait for OK messages"
					if (state.member_count == 1) {
						add_member(msg.peer_id);
						state.view_id++;
						print_membership();
						broadcast_newview(msg.peer_id);
					} // otherwise need to get OK msgs from existing mems
					else {
						// inti pending op - leader sends REQ message to all existing members
						state.pending_op.active = 1;
						state.pending_op.request_id = state.next_request_id++;
						state.pending_op.type = ADD;
						state.pending_op.peer_id = msg.peer_id;
						state.pending_op.ok_count = 0;
						state.pending_op.expected_oks = state.member_count - 1; // doesn't count leader
						
						// sends REQ to all current members except self
						struct Message req_msg = {
							.type = REQ,
							.op_type = ADD,
							.request_id = state.pending_op.request_id,
							.view_id = state.view_id,
							.peer_id = msg.peer_id
						};
						
						for (int i = 0; i < state.member_count; i++) {
							if (state.members[i] != state.peer_id) {
								send_message(&req_msg, state.hostnames[state.members[i] - 1]);
							}
						}
            		}
        		}
                break;

			// member needs to save the op and send OK back to leader
			/*
			IMPLEMENT: when peers receive a REQ, each peer must save the op he must perform
			*/
            case REQ: 
				if (!state.is_leader) {
					 // saves the operation that needs to be performed - "Each peer saves the operation he must perform"
                    state.pending_op.active = 1;
                    state.pending_op.request_id = msg.request_id;
                    state.pending_op.type = msg.op_type;
                    state.pending_op.peer_id = msg.peer_id;

					// DELETE - debug output to confirm operation was saved
					//fprintf(stderr, "DEBUG: {peer_id:%d, saved_op: request_id=%d, op_type=%d, peer_id=%d}\n", state.peer_id, state.pending_op.request_id, state.pending_op.type, state.pending_op.peer_id);

            		// sends OK back to leader containing request id and current view id
					struct Message ok_msg = {
						.type = OK,
						.request_id = state.pending_op.request_id,
						//.request_id = msg.request_id,
						.view_id = msg.view_id
					};
            	send_message(&ok_msg, state.hostnames[state.leader_id - 1]);
        		}
                break;

			/* leader needs to first track OK responses from all alive peers then when all messages received, leader increments 
			 view id, adds new peer to mem list and sends NEWVIEW message that contains new view id and new mem list 
			 to all mems including new peer */
            case OK:
				 if (state.is_leader && state.pending_op.active) {
					// verifies this OK matches pending op 
					if (msg.request_id == state.pending_op.request_id && msg.view_id == state.view_id) {
						state.pending_op.ok_count++;
						
						// if all expected OKs are received, update membership and broadcast 
						if (state.pending_op.ok_count >= state.pending_op.expected_oks) {
							add_member(state.pending_op.peer_id); // adds new member
							state.view_id++;
							print_membership();
							broadcast_newview(state.pending_op.peer_id); // broadcasts new view to all members
							state.pending_op.active = 0; // clears pending op
						}
					}
				}
                break;

			// all peers need to update view_id and mem list & print new mem info
            case NEWVIEW: 
				if (msg.view_id > state.view_id) { // only process newer views 
					pthread_mutex_lock(&state.state_mutex);

					if (state.is_leader) { 
						state.view_id = msg.view_id;
						state.member_count = msg.member_count;
						memcpy(state.members, msg.members, sizeof(int) * msg.member_count);
					} else {
						state.view_id = msg.view_id; // updates local state with new view
						state.member_count = msg.member_count;
						memcpy(state.members, msg.members, sizeof(int) * msg.member_count);
                    	// state.pending_op.active = 0; // clears pending operation since it is now complete
						print_membership();
					}

					// PART 2 IMPLEMENTATION: updates active peers based on new membership
					for (int i = 0; i < MAX_PEERS; i++) {
						state.peer_active[i] = 0; // resets all to inactive
					}
					
					// marks members in the new list as active
					for (int i = 0; i < state.member_count; i++) {
						int peer_id = state.members[i];
						if (peer_id >= 1 && peer_id <= MAX_PEERS) {
							state.peer_active[peer_id - 1] = 1;
							state.last_heartbeat[peer_id - 1] = time(NULL); // resets heartbeat timer
						}
					}
					pthread_mutex_unlock(&state.state_mutex);
				}
                break; 
        }
        close(client_sock); // closes connection after handling message
    }
    return NULL;
}

 // prints current membership state in given format 
void print_membership() {
	if (state.view_id == last_view_id) return; // avoid dupes
	last_view_id = state.view_id;

    // prints membership info in required format - 
    // {peer_id:<ID>, view_id: <VIEW_ID>, leader: <LEADER_ID>, memb_list: [<COMMA_SEPARATED_MEMBERS>]}
    fprintf(stderr, "{peer_id:%d, view_id: %d, leader: %d, memb_list: [", state.peer_id, state.view_id, state.leader_id);
    
    // prints list of members separated by a comma 
    for (int i = 0; i < state.member_count; i++) {
        fprintf(stderr, "%d%s", state.members[i], 
                i < state.member_count - 1 ? "," : "");
    }
    
    fprintf(stderr, "]}\n");
}


// main functionality 
int main(int argc, char* argv[]) {
    // vars to store command line arguments
    char* hostsfile = NULL; // path to file containing hostnames
    int delay = 0; // init delay before starting protocol
	int crash_delay = 0; // delay before crashing where 0 means no crash 
    int opt; // for parsing command line options

	// parses command line args 
    while ((opt = getopt(argc, argv, "h:d:c:t")) != -1) {
        switch (opt) {
            case 'h':
                hostsfile = optarg;  // stores hostsfile path
                break;
            case 'd':
                delay = atoi(optarg); // converts delay string to int
                break;
			case 'c':
				crash_delay = atoi(optarg); // delays before crashing
				break;
            default:
                fprintf(stderr, "Usage: %s -h hostsfile [-d delay]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
	}

	// ensures hostsfile was provided
    if (hostsfile == NULL) {
        fprintf(stderr, "Hostsfile is required\n");
        exit(EXIT_FAILURE);
    }

    // starts the peer by initializing state and starting protocol
    init_state(hostsfile, delay);

	// if crash delay given, starts a crash timer
    if (crash_delay > 0) {
        pthread_t crash_thread;
        pthread_create(&crash_thread, NULL, (void* (*)(void*))crash_peer, (void*)(long)crash_delay);
    }

    start_peer();

    return 0;
}



/*
TEST CASE 1:
- each process joins 1 by 1, til all processes from config file joined
- leader starts first
- at end you should see for each peer how the membership list and view id changed, EVERY time a NEW peer joined 
- every time a peer joins, all peers in membership should print: {peer_id:<ID>, view_id: <VIEW_ID>, leader: <LEADER_ID>, memb_list: [<COMMA_SEPARATED_MEMBERS>]}
	- where peer id is the id of the local peer, view id is the current view id of local peer and leader is current leader id. 
*/


