#include <stdio.h> 
#include <cstdlib>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <time.h>
#include <limits>
#include <queue>
#include <semaphore.h>

#define MYPORT "4950"
#define MANAGERPORT "8000"
#define MAXDATASIZE 4000 // max number of bytes we can get at once 
#define MAX_MESSAGE_SIZE 900
#define MAXNUMNODES 20


using namespace std;

//global variable
char manager_ip_address[100];
int virtual_id = 0;
int msg_flag = 0;
int converged = 0;
map<int, string> ip_address_nodes;
map<int, int> neighbor_cost;
long time_stamp = -1;
map<int, vector<int> > forwarding_tbl;
map<int, vector<int> > old_forwarding_tbl;
int manager_sock = -1;
sem_t start;
sem_t sem_send;
sem_t sem_timer;



typedef struct neighbor_data
{
	int type;
    int neighbor_id_cost[MAXNUMNODES];
	char neighbor_ip_address[MAXNUMNODES][40];
} neighbor_data;

typedef struct routing_data
{
	int node_id;
	int hop_cost[MAXNUMNODES];
} routing_data;

routing_data rData_init;

typedef struct message_data
{	
		int type;
        int source;
        int destination;
        short send_signal;
        short hops_taken[MAXNUMNODES+1];
        char msg[MAX_MESSAGE_SIZE];

    void clean_hops(){
    	hops_taken[0]=-1;
    }

    void debug(){
		printf("#---------message data debug-----------------------");
		printf("source: %d\n",source);
		printf("destination: %d\n",destination);
		for(int i = 0; i < MAXNUMNODES+1;i++){
			printf("hpt[%d]: %d\n", i, hops_taken[i]);
		}
		printf("message: %s\n",msg);
		printf("#---------message data debug-----------------------");
	}
};
queue<message_data> mData;
queue<message_data> mData_inc;//start message
queue<message_data> mData_forward;//forward message		

void restart_timer()
{
	sem_wait(&sem_timer);
	time_stamp = (long)time(0); 
	sem_post(&sem_timer);
}
// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
void  set_hop(message_data * mesg)
{
	int hops_taken_insert_index = -1;

	for(int i = 0; i < MAXNUMNODES+1; i ++){
		if(mesg->hops_taken[i]==-1){
			hops_taken_insert_index = i;

			break;
		}
	}
	if(hops_taken_insert_index == -1)
	{
		printf("error finding next hop index\n");
		printf("START DEBUG\n");
		mesg->debug();
		printf("END DEBUG\n");

	}
	else
	{
		mesg->hops_taken[ hops_taken_insert_index ] = virtual_id;
		mesg->hops_taken[ hops_taken_insert_index + 1 ] = -1;
	}
}
void print_message(message_data * msg){
	printf("from %d to %d hops ", msg->source, msg->destination);
			for(int i = 0; msg->hops_taken[i] != -1 && i<MAXNUMNODES+1; i++){
				printf("%d ",msg->hops_taken[i]);
			}
	//printf("%d ", virtual_id);
	printf("%s",msg->msg);
}

void set_queues(){

	while(!mData.empty())
	{
    	mData_inc.push(mData.front());
    	mData.pop();
	
	}
}

void print_forwarding_table(){
//printf("Forwarding Table:\n");
	map<int, vector<int> >::const_iterator it_map;
	for(it_map = forwarding_tbl.begin(); it_map != forwarding_tbl.end(); ++it_map)
		{
			printf("%d %d %d\n", it_map->first, (it_map->second)[0], (it_map->second)[1]);
		}
}


void * sendMsg(void * param)
{
	queue<message_data> * to_send_queue = (queue<message_data> *) param;
	//sleep(2);

	if(to_send_queue->empty()){ 
		
		printf("holy crap it's null!\n");
		return NULL;
	}
while((to_send_queue->front()).destination == virtual_id){
	to_send_queue->pop();
}

while(!to_send_queue->empty()){



	map<int, vector<int> >::const_iterator nextNode = forwarding_tbl.find((to_send_queue->front().destination));
	int dest = (to_send_queue->front().destination);


	if((nextNode != forwarding_tbl.end()) && ((nextNode->second)[0] != 0))
	{

		map<int, string>::const_iterator ip_it = ip_address_nodes.find((nextNode->second)[0]);
		
		if(ip_it != ip_address_nodes.end())
		{
			
			int port = 5095 + ip_it->first;
			char port_str[50];
			sprintf(port_str, "%d", port);
		
			int sockfd;
    				struct addrinfo hints, *servinfo, *p;
    			int rv;
    			int numbytes;

    			memset(&hints, 0, sizeof hints);
    			hints.ai_family = AF_UNSPEC;
    			hints.ai_socktype = SOCK_DGRAM;
				//printf("sending to port, vmid: %s, %d", port_str, ip_it->first);
	    		if ((rv = getaddrinfo((ip_it->second).c_str(), port_str, &hints, &servinfo)) != 0) {
        			fprintf(stderr, "sendmsg getaddrinfo: %s\n", gai_strerror(rv));
        			exit(1);
    			}
	
	    		// loop through all the results and make a socket
    			for(p = servinfo; p != NULL; p = p->ai_next) {
        			if ((sockfd = socket(p->ai_family, p->ai_socktype,
                			p->ai_protocol)) == -1) {
            				perror("talker: socket");
            				continue;
        			}

       				break;
    			}

    			if (p == NULL) {
        			fprintf(stderr, "talker: failed to bind socket\n");
        			exit(1);
    			}


	 		
			char buf[MAXDATASIZE];

			message_data * messd = &(to_send_queue->front());

	 		if(messd->source == virtual_id){
	 			
	 			messd->clean_hops();
	 			set_hop( messd);
	 			memcpy(buf, messd, sizeof(message_data));
				print_message(messd);
	    		messd->clean_hops();//for next time
				mData.push((*messd));
				
	 		}else {

			
			memcpy(buf, messd, sizeof(message_data));

	 		}

	 		to_send_queue->pop();

	 			
    			if ((numbytes = sendto(sockfd, buf, MAXDATASIZE, 0,
             			p->ai_addr, p->ai_addrlen)) == -1) {
        			perror("talker: sendto");
        			exit(1);
    			}

    			freeaddrinfo(servinfo);

			close(sockfd);
			sleep(1);
		
    	} else {/*
    		set_hop(&mData_inc.front());
    		print_message(&mData_inc.front());
    		mData_inc.front().clean_hops();//for next time
			mData.push(mData_inc.front());*/
    	}
    }

}

	return NULL;	
}


void * recvMsg(void * param)
{	
	sem_wait(&start);
	int port = 5095 + virtual_id;
	char port_str[50];
	sprintf(port_str, "%d", port);
	int sockfd;
    	struct addrinfo hints, *servinfo, *p;
   	int rv;
    	int numbytes;
    	struct sockaddr_storage their_addr;
    	char buf[MAXDATASIZE];
    	socklen_t addr_len;
    	char s[INET6_ADDRSTRLEN];

    	memset(&hints, 0, sizeof hints);
    	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    	hints.ai_socktype = SOCK_DGRAM;
    	hints.ai_flags = AI_PASSIVE; // use my IP

    	if ((rv = getaddrinfo(NULL, port_str, &hints, &servinfo)) != 0) {
        	fprintf(stderr, "recvmsg getaddrinfo: %s\n", gai_strerror(rv));
        	exit(1);
    	}

    	// loop through all the results and bind to the first we can
    	for(p = servinfo; p != NULL; p = p->ai_next) {
        	if ((sockfd = socket(p->ai_family, p->ai_socktype,
                	p->ai_protocol)) == -1) {
            		perror("listener: socket");
            		continue;
        	}

        	int reuse_val = 1;
			if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse_val, sizeof(int)) == -1) {
			    perror("setsockopt");
			    exit(1);
			}        	

        	if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            		close(sockfd);
            		perror("recMsg listener: bind");
            		printf("bind failed with port: %d",port);
        	}

        	break;
    	}

    	if (p == NULL) {
        	fprintf(stderr, "listener: failed to bind socket\n");
        	exit(1);
    	}

    	freeaddrinfo(servinfo);

    	//printf("listener: waiting to recvfrom...\n");

    	addr_len = sizeof their_addr;
    	//printf("I've set up my socket\n");

    	//delete
    	//printf("Starting recv with port, vm id: %s, %d\n", port_str, virtual_id);
	while(1)
	{
		if ((numbytes = recvfrom(sockfd, buf, MAXDATASIZE , 0,
        		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
        		perror("recvfrom");
        		exit(1);
    		}
    		//delete
    		//printf("I've GOTS sOMETHING....\n");
    		/*printf("listener: got packet from %s\n",
        		inet_ntop(their_addr.ss_family,
           		get_in_addr((struct sockaddr *)&their_addr),
            		s, sizeof s));*/
    		//printf("listener: packet is %d bytes long\n", numbytes);
    		//buf[numbytes] = '\0';
    		//printf("listener: packet contains \"%s\"\n", buf);
        message_data msg_recv;
		memcpy(&msg_recv, buf, sizeof(message_data));
		
		set_hop(&msg_recv);
		//printf("MESSAGE RECEIVED: %s\n", msg_recv.msg);
		//if(msg_recv.destination == virtual_id)
		//{


			
			print_message(&msg_recv);
			mData_forward.push(msg_recv);
			pthread_t sendThread;
			pthread_create(&sendThread, NULL, sendMsg, &mData_forward);//param just has to be not null
			

		//}
		//else 
		/*{

		}	*/
	}
    	close(sockfd);
}

void * sendConvergenceSignal()
{
	char buffer[MAXDATASIZE];
	if(send(manager_sock, buffer, MAXDATASIZE, 0) == -1)
		{
			perror("Error sending convergence message to manager");
			printf("socket: %d", manager_sock);
		}
}


void * checkConvergence(void * param)
{
	sem_wait(&start);
	while(1)
	{		sleep(5);
		//printf("yup");

		long curr_time = (long)time(0);
		sem_wait(&sem_timer);
		bool expired = ((curr_time - time_stamp) > 5) && (time_stamp != 0);
		sem_post(&sem_timer);
		if(expired)
		{
			
			//converged
			converged = 1;

			//TODO: print forwarding_tbl in order of destination
			if(forwarding_tbl != old_forwarding_tbl){
				print_forwarding_table();
				old_forwarding_tbl = forwarding_tbl;	
			} 
			//##
			//pthread_t recvThread;
			//pthread_create(&recvThread, NULL, recvMsg, NULL);
			sleep(2);
			/*
			while(!mData.empty())
			{
				//mData_inc.source = mData.front().source;
				//mData_inc.destination = mData.front().destination;
				//strcpy(mData_inc.msg, mData.front().msg);
				mData_inc.push(mData.front());

				//mData.source = -1;
				//mData.destination = -1;
				//strcpy(mData.msg, "");
				//printf("SENDING MSG FROM %d to %d\n", mData_inc.front().source, mData_inc.front().destination);	
				mData.pop();		
	
				//pthread_t sendThread;
				//pthread_create(&sendThread, NULL, sendMsg, NULL);
				//pthread_join(sendThread, NULL);
				//sendMsg(NULL);
				sleep(2);
			}*/

			 sendConvergenceSignal();
			while(converged)//LOL
			{
				sleep(.1); //I put this here because I feel bad for the cpu
				//spin
			}
		}
	}	
	exit(0);
	return NULL;
}


void * sendUDP(void * param)
{

	sleep(1);
sem_wait(&sem_send);
	
	char buf[MAXDATASIZE];
	memcpy(buf, &rData_init, sizeof(routing_data));

	map<int, string>::const_iterator ip_it;
	//print_ip_map(&ip_address_nodes);
	for(ip_it = ip_address_nodes.begin(); ip_it != ip_address_nodes.end(); ++ip_it){
		int port = 4095 + ip_it->first;
		char port_str[50];
		sprintf(port_str, "%d", port);
	
		int sockfd;
    		struct addrinfo hints, *servinfo, *p;
    		int rv;
    		int numbytes;

    		memset(&hints, 0, sizeof hints);
    		hints.ai_family = AF_UNSPEC;
    		hints.ai_socktype = SOCK_DGRAM;

    		if ((rv = getaddrinfo((ip_it->second).c_str(), port_str, &hints, &servinfo)) != 0) {
        		fprintf(stderr, "sendUdp getaddrinfo: %s\n", gai_strerror(rv));
        		exit(1);
    		}

    		// loop through all the results and make a socket
    		for(p = servinfo; p != NULL; p = p->ai_next) {
        		if ((sockfd = socket(p->ai_family, p->ai_socktype,
                		p->ai_protocol)) == -1) {
            			perror("talker: socket");
            			continue;
        		}

       			break;
    		}

    		if (p == NULL) {
        		fprintf(stderr, "talker: failed to bind socket\n");
        		exit(1);
    		}
	 
        
    		if ((numbytes = sendto(sockfd, buf, MAXDATASIZE, 0,
             		p->ai_addr, p->ai_addrlen)) == -1) {
        		perror("talker: sendto");
        		exit(1);
    		}

    		freeaddrinfo(servinfo);

    		//printf("talker: sent %d bytes\n", numbytes);
		close(sockfd);
			sleep(1);
    	}

sem_post(&sem_send);
	return NULL;	
}

void * recvUDP(void * param)
{	
	int port = 4095 + virtual_id;
	char port_str[50];
	sprintf(port_str, "%d", port);
	int sockfd;
    	struct addrinfo hints, *servinfo, *p;
   	int rv;
    	int numbytes;
    	struct sockaddr_storage their_addr;
    	char buf[MAXDATASIZE];
    	socklen_t addr_len;
    	char s[INET6_ADDRSTRLEN];

    	memset(&hints, 0, sizeof hints);
    	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    	hints.ai_socktype = SOCK_DGRAM;
    	hints.ai_flags = AI_PASSIVE; // use my IP

    	if ((rv = getaddrinfo(NULL, port_str, &hints, &servinfo)) != 0) {
        	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        	exit(1);
    	}

    	// loop through all the results and bind to the first we can
    	for(p = servinfo; p != NULL; p = p->ai_next) {
        	if ((sockfd = socket(p->ai_family, p->ai_socktype,
                	p->ai_protocol)) == -1) {
            		perror("listener: socket");
            		continue;
        	}
        	int reuse_val = 1;
			if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse_val, sizeof(int)) == -1) {
			    perror("setsockopt");
			    exit(1);
			}   
        	if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            		close(sockfd);
            		perror("listener: bind");
            		continue;
        	}

        	break;
    	}

    	if (p == NULL) {
        	fprintf(stderr, "listener: failed to bind socket\n");
        	exit(1);
    	}

    	freeaddrinfo(servinfo);

    	//printf("listener: waiting to recvfrom...\n");

    	addr_len = sizeof their_addr;
	while(1)
	{
		if ((numbytes = recvfrom(sockfd, buf, MAXDATASIZE , 0,
        		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
        		perror("recvfrom");
        		exit(1);
    		}

    		/*printf("listener: got packet from %s\n",
        		inet_ntop(their_addr.ss_family,
           		get_in_addr((struct sockaddr *)&their_addr),
            		s, sizeof s));*/
    		//printf("listener: packet is %d bytes long\n", numbytes);
    		//buf[numbytes] = '\0';
    		//printf("listener: packet contains \"%s\"\n", buf);
		routing_data rData_inc;
		memcpy(&rData_inc, buf, sizeof(routing_data));

		//printf("NODE INFO FOR ID: %d\n", rData_inc.node_id);

		int update_flag = 0;
		//update forwarding table
		for(int i = 0; i<MAXNUMNODES; i++)
		{
			map<int, int>::const_iterator weight = neighbor_cost.find(rData_inc.node_id);
			if(weight != neighbor_cost.end())
			{ 
				if(rData_init.hop_cost[i] > (rData_inc.hop_cost[i] + weight->second)) 
				{
					rData_init.hop_cost[i] = rData_inc.hop_cost[i] + weight->second;
					//printf("COST CALC = %d\n", rData_init.hop_cost[i]);
					vector<int> hop_cost;
					hop_cost.push_back(rData_inc.node_id);
					hop_cost.push_back(rData_inc.hop_cost[i] + weight->second);
					forwarding_tbl[i] = hop_cost;

					update_flag = 1;
				}
			}
			else
			{
				perror("Non neighbor trying to communicate");
				exit(1);
			}
		}
		if(update_flag)
		{
			restart_timer();
			pthread_t sendThread;
			pthread_create(&sendThread, NULL, sendUDP, NULL);
		}	
	}
    	close(sockfd);
}

void * communicateWithNodes(void * param)
{
	rData_init.node_id = virtual_id;
	for(int i = 0; i<MAXNUMNODES; i++)
	{
		if(i != virtual_id)
		{
			//set to infinity
			rData_init.hop_cost[i] = 32767;//UINT_MAX; //numeric_limits<int>::max();
		}
		else
		{
			rData_init.hop_cost[i] = 0;
			vector<int> hop_cost;
			hop_cost.push_back(i);
			hop_cost.push_back(0);
			forwarding_tbl[i] = hop_cost;
		}	
	}
	
	sleep(2);
	pthread_t recvThread;
	pthread_create(&recvThread, NULL, recvUDP, NULL);

	pthread_t sendThread;
	pthread_create(&sendThread, NULL, sendUDP, NULL);

	return NULL;
}

void * communicateWithManager(void * param)
{
    int sockfd, numbytes;  
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(manager_ip_address, MANAGERPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "communicateMm getaddrinfo: %s\n", gai_strerror(rv));
		exit(0);
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}
		
		break;
	}
	manager_sock = sockfd;

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		exit(0);
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	
	freeaddrinfo(servinfo); // all done with this structure

	while(1)
	{
		if((numbytes = recv(sockfd, buf, MAXDATASIZE, 0)) > 0)
		{
			//printf("Number of bytes recv = %d\n", numbytes);
			if(virtual_id == 0)
			{
				//assign node new id
				istringstream (buf) >> virtual_id;
				//printf("VIRTUAL ID = %d\n", virtual_id);
				pthread_t nodeThread;	
				pthread_create(&nodeThread, NULL, communicateWithNodes, NULL);
			}
			else if(msg_flag == 0)
            {

				message_data msg_recv;
				memcpy(&msg_recv, buf, sizeof(message_data));
				//print_message(&msg_recv);
				//printf("MESSAGE SOURCE: %d\n", msg_recv.source);
				//printf("MESSAGE DESTINATION: %d\n", msg_recv.destination);
				//printf("MESSAGE PAYLOAD: %s\n ", msg_recv.msg);
				if(msg_recv.source == -1)
					msg_flag = 1;
				else{
					mData.push(msg_recv);
					//printf("GOT PUSHED\n");
				}
					
			}
			else
			{	
				//get neighbor info
				neighbor_data nData;

				memcpy(&nData, buf, sizeof(neighbor_data));


				if(nData.type ==1){//then this was actually a send message signal
					
					//printf("SRC \n");

					set_queues(); //load up the messages to send to neighbors
					pthread_t sendThread;
					pthread_create(&sendThread, NULL, sendMsg, &mData_inc);
					
				} else if(nData.type == 2){
					//printf("C RESET\n");
					converged = 0;
					sem_post(&start);
					sem_post(&start);
				}else{

				for(int i = 0; i<MAXNUMNODES; i++)
				{
					if(i != virtual_id)
					{
						int n_cost = nData.neighbor_id_cost[i];
						if(n_cost > 0)
						{
							if(n_cost != neighbor_cost[i])
							{
								printf("now linked to node %d with cost %d\n", i, n_cost);

							}
							
							neighbor_cost[i] = n_cost;

							if(strcmp(nData.neighbor_ip_address[i], "N/A") != 0)
							{
								ip_address_nodes[i] = nData.neighbor_ip_address[i];
							}

							
						}
						else if(n_cost < 0)
						{
							//link broken
							map<int, int>::const_iterator rem_cost = neighbor_cost.find(i);
							/*if(rem_cost != neighbor_cost.end())
							{
								neighbor_cost.erase(rem_cost);
							}
							map<int, string>::const_iterator rem_ip = ip_address_nodes.find(i);
							if(rem_ip != ip_address_nodes.end())
							{
								ip_address_nodes.erase(rem_ip);
							}*/

							neighbor_cost.erase(i);
							ip_address_nodes.erase(i);

							printf("no longer linked to node %d\n", i);
						}

					}
					else
					{
						neighbor_cost[i] = 0;
						//ip_address_nodes[i] = s;
					}
				}
				//printf("exit for\n");
				pthread_t sendThread;
				pthread_create(&sendThread, NULL, sendUDP, NULL);
				
				
				converged = 0;
				
				restart_timer();

				}

			}
		}
		else
		{
			//close(sockfd);
		}
	}
	//close(sockfd);
}

int main(int argc, char *argv[])
{	
	if(argc != 2)
	{
        fprintf(stderr, "usage: distvec managerhostname\n");
        exit(1);
    }
	sem_init(&start,0,0);
	sem_init(&sem_send,0,1);
	sem_init(&sem_timer,0,1);
	
	//update manager's ip address
	strcpy(manager_ip_address, argv[1]);

	pthread_t managerThread;
	pthread_create(&managerThread, NULL, communicateWithManager, NULL);

	pthread_t convergeThread;
	pthread_create(&convergeThread, NULL, checkConvergence, NULL);

	pthread_t recvThread;
	pthread_create(&recvThread, NULL, recvMsg, NULL);


	pthread_join(managerThread, NULL);

	return 0;
}

