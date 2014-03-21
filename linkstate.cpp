#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <time.h>
#include <limits>
#include <stack>
#include <queue>

#define MYPORT "4950"
#define MAXDATASIZE 900 // max number of bytes we can get at once 
#define MANAGERPORT "8000"
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
map<int, vector<int> > routing_tbl;
int update_routing_flag = 1;

typedef struct neighbor_data
{
        int neighbor_id_cost[MAXNUMNODES];
	char neighbor_ip_address[MAXNUMNODES][100];
} neighbor_data;

typedef struct routing_data
{
	int node_id;
	int topology[MAXNUMNODES][MAXNUMNODES];
} routing_data;

routing_data rData_init;

typedef struct message_data
{
        int source;
        int destination;
        char msg[MAXDATASIZE];
} message_data;

queue<message_data> mData;
queue<message_data> mData_inc;

void restart_timer()
{
	time_stamp = (long)time(0); 
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


void * sendMsg(void * param)
{
	sleep(2);
	if(mData_inc.empty()) 
		return NULL;
	map<int, vector<int> >::const_iterator nextNode = routing_tbl.find(mData_inc.front().destination);
	if((nextNode != routing_tbl.end()) && ((nextNode->second)[1] != 0))
	{
		map<int, string>::const_iterator ip_it = ip_address_nodes.find((nextNode->second)[2]);
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
	
	    		if ((rv = getaddrinfo((ip_it->second).c_str(), port_str, &hints, &servinfo)) != 0) {
        			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
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
			memcpy(buf, &mData_inc.front(), sizeof(message_data));
    			printf("SEND to %d\n", mData_inc.front().destination);
			mData_inc.pop();
    			if ((numbytes = sendto(sockfd, buf, MAXDATASIZE, 0,
             			p->ai_addr, p->ai_addrlen)) == -1) {
        			perror("talker: sendto");
        			exit(1);
    			}
	
    			freeaddrinfo(servinfo);

    			printf("talker: sent %d bytes\n", numbytes);
			close(sockfd);
			sleep(1);
    		}
	}

	return NULL;	
}

void * recvMsg(void * param)
{	
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

    	printf("listener: waiting to recvfrom...\n");

    	addr_len = sizeof their_addr;
	while(1)
	{
		if ((numbytes = recvfrom(sockfd, buf, MAXDATASIZE , 0,
        		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
        		perror("recvfrom");
        		exit(1);
    		}

    		printf("listener: got packet from %s\n",
        		inet_ntop(their_addr.ss_family,
           		get_in_addr((struct sockaddr *)&their_addr),
            		s, sizeof s));
    		printf("listener: packet is %d bytes long\n", numbytes);
    		//buf[numbytes] = '\0';
    		//printf("listener: packet contains \"%s\"\n", buf);
		message_data msg_recv;
		memcpy(&msg_recv, buf, sizeof(message_data));
		printf("MESSAGE RECEIVED: %s\n", msg_recv.msg);
		mData_inc.push(msg_recv);
		pthread_t sendThread;
		pthread_create(&sendThread, NULL, sendMsg, NULL);
			
	}
    	close(sockfd);
}


int dj_prev[MAXNUMNODES];
int dj_cost[MAXNUMNODES];

void updateRoutingTbl()
{
	printf("UPDATING ROUTING TBL\n");
	for(int i = 0; i<MAXNUMNODES; i++)
	{
		if(dj_prev[i] != -1)
		{
			vector<int> routeinfo;
			routeinfo.push_back(dj_cost[i]);
			stack<int> st;
			int j = i;
			st.push(j);
			
			while(dj_prev[j] != virtual_id)
			{
				st.push(dj_prev[j]);
				j = dj_prev[j];
				printf("WHILE\n");
			}
			printf("out of while\n");
			st.push(virtual_id);
			
			while(!st.empty())
			{
				int k = st.top();
				routeinfo.push_back(k);
				st.pop();
			}
			routing_tbl[i] = routeinfo;
		}
		else if(i == virtual_id)
		{
			//do nothing
		}
	}
	printf("DONE UPDATING ROUTING TBL\n");
}

int minDist(int distance[], bool s[])
{
	int min = 32767;
	int n;
	for(int i = 0; i<MAXNUMNODES; i++)
	{
		if((distance[i] <= min) && (s[i] == false))
		{
			min = distance[i];
			n = i;
		}
	}
	return n;
}

void djikstra()
{
	int source = virtual_id;
	int distance[MAXNUMNODES];
	bool s[MAXNUMNODES];
	for(int i = 0; i<MAXNUMNODES; i++)
	{
		distance[i] = 32767;
		s[i] = false;	
		dj_prev[i] = -1;
	}
		
	distance[source] = 0;
	for(int j = 0; j<MAXNUMNODES-1; j++)
	{
		int u = minDist(distance, s);
		s[u] = true;
	
		for(int k = 0; k<MAXNUMNODES; k++)
		{
			int weight = rData_init.topology[u][k];
			if(weight>0)
			{
				printf("%d, %d => %d\n", u, k, weight);
				if((!s[k])&&(distance[u] != 32767)&&((distance[u] + weight) < distance[k]))
				{
					distance[k] = distance[u] + weight;
					dj_prev[k] = u;
					dj_cost[k] = distance[k];
					printf("NODE k = %d\n", k);
				}
			}
		}
	}
printf("DJIKSTRA's COMPLETE!\n");	
	//dj_prev[source] = 0;
	for(int i = 0; i<MAXNUMNODES; i++)
	{
		printf("DJ_PRE: %d\n", dj_prev[i]);
	}
	updateRoutingTbl();	
}

void * checkConvergence(void * param)
{
	while(1)
	{
		sleep(5);
		long curr_time = (long)time(0);
		if(((curr_time - time_stamp) > 5) && (time_stamp != 0))
		{
			//converged
			converged = 1;
			if(update_routing_flag)
			{
				djikstra();

                		for(int x = 0; x<MAXNUMNODES; x++)
                		{
                    			for(int y = 0; y<MAXNUMNODES; y++)
                    			{
                        			if(rData_init.topology[x][y] != -1)
                            			printf("RData_INIT: %d, at %d, %d\n",rData_init.topology[x][y], x, y);
                    			}
                		}
                
				printf("Routing Table:\n");
				map<int, vector<int> >::const_iterator it_map;
				for(it_map = routing_tbl.begin(); it_map != routing_tbl.end(); ++it_map)
				{
					printf("%d %d:", it_map->first, (it_map->second)[0]);
					for(int i = 1; i<(it_map->second).size(); i++)
					{
						printf(" %d", (it_map->second)[i]);
					}
					printf("\n");
				}

				//update_routing_flag = 0;
				pthread_t recvThread;
			    	pthread_create(&recvThread, NULL, recvMsg, NULL);
				sleep(2);
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
	
				    	pthread_t sendThread;
				    	pthread_create(&sendThread, NULL, sendMsg, NULL);
				    	//pthread_join(sendThread, NULL);
				    	//sendMsg(NULL);
					sleep(2);
			    	}

			    	while(converged)
			    	{
	
				}
			}
		}
	}
	exit(0);
	return NULL;
}

void * sendUDP(void * param)
{
	sleep(2);
	
	char buf[MAXDATASIZE];
	memcpy(buf, &rData_init, sizeof(routing_data));

	map<int, string>::const_iterator ip_it;
	for(ip_it = ip_address_nodes.begin(); ip_it != ip_address_nodes.end(); ++ip_it)
	{
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
        		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
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

    		printf("talker: sent %d bytes\n", numbytes);
		close(sockfd);
		sleep(1);
    	}

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
            		perror("lrecvUDO listener: bind");
            		printf("bind failed with port: %d",port);
            		continue;
        	}

        	break;
    	}

    	if (p == NULL) {
        	fprintf(stderr, "listener: failed to bind socket\n");
        	exit(1);
    	}

    	freeaddrinfo(servinfo);

    	printf("listener: waiting to recvfrom...\n");

    	addr_len = sizeof their_addr;
	printf("HERE\n");
	while(1)
	{
		printf("YOOOOOO\n");
		if ((numbytes = recvfrom(sockfd, buf, MAXDATASIZE , 0,
        		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
        		perror("recvfrom");
        		exit(1);
    		}

    		printf("listener: got packet from %s\n",
        		inet_ntop(their_addr.ss_family,
           		get_in_addr((struct sockaddr *)&their_addr),
            		s, sizeof s));
    		printf("listener: packet is %d bytes long\n", numbytes);
    		//buf[numbytes] = '\0';
    		//printf("listener: packet contains \"%s\"\n", buf)MAXDATASIZEta_inc;
		routing_data rData_inc;
		memcpy(&rData_inc, buf, sizeof(routing_data));

		printf("NODE INFO FOR ID: %d\n", rData_inc.node_id);

		int update_flag = 0;
		//update forwarding table
		for(int i = 0; i<MAXNUMNODES; i++)
		{
			for(int j = 0; j<MAXNUMNODES; j++)
			{
				if(rData_init.topology[i][j] == -1)
				{
					if(rData_inc.topology[i][j] != -1)
					{
						update_flag = 1;
						rData_init.topology[i][j] = rData_inc.topology[i][j];					
						vector<int> hop_cost;
						hop_cost.push_back(rData_inc.topology[i][j]);
						hop_cost.push_back(j);
						routing_tbl[j] = hop_cost;
					}
				}
			}
		}
		if(update_flag)
		{
			restart_timer();
			pthread_t sendThread;
			pthread_create(&sendThread, NULL, sendUDP, NULL);
			//pthread_detach(sendThread);
			update_flag = 0;
			restart_timer();
		}	
	}
    	close(sockfd);
}

void * communicateWithNodes(void * param)
{
	printf("COMMUNICATING\n");
	rData_init.node_id = virtual_id;
	for(int i = 0; i<MAXNUMNODES; i++)
	{
		for(int j = 0; j<MAXNUMNODES; j++)
		{
			if(i == virtual_id)
			{
				if(j != virtual_id)
				{
					map<int, int>::const_iterator weight = neighbor_cost.find(j);
					if(weight != neighbor_cost.end())
					{
						rData_init.topology[i][j] = weight->second; 
					}
					else
					{
						//set to -1
						rData_init.topology[i][j] = -1;
					}
				}
				else
				{
					rData_init.topology[i][j] = 0;
					vector<int> hop_cost;
					hop_cost.push_back(0);
					hop_cost.push_back(j);
					routing_tbl[j] = hop_cost;
				}
			}
			else
			{
				//set to -1
				rData_init.topology[i][j] = -1;
			}		
		}
	}
	
	sleep(2);
	pthread_t recvThread;
	pthread_create(&recvThread, NULL, recvUDP, NULL);
	//pthread_detach(recvThread);
	pthread_t sendThread;
	pthread_create(&sendThread, NULL, sendUDP, NULL);
	//pthread_detach(sendThread);
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
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
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
			printf("Number of bytes recv = %d\n", numbytes);
			if(virtual_id == 0)
			{
				//assign node new id
				istringstream (buf) >> virtual_id;
				printf("VIRTUAL ID = %d\n", virtual_id);
				
				pthread_t nodeThread;
				pthread_create(&nodeThread, NULL, communicateWithNodes, NULL);
			}
			else if(msg_flag == 0)
            {
				message_data msg_recv;
				memcpy(&msg_recv, buf, sizeof(message_data));
				printf("MESSAGE SOURCE: %d\n", msg_recv.source);
				printf("MESSAGE DESTINATION: %d\n", msg_recv.destination);
				printf("MESSAGE PAYLOAD: %s\n ", msg_recv.msg);
				if(msg_recv.source == -1)
					msg_flag = 1;
				else
					mData.push(msg_recv);
			}
			else
			{	
				//get neighbor info
				neighbor_data nData;
				memcpy(&nData, buf, sizeof(neighbor_data));
				//printf("TEST Val = %d\n", nData.neighbor_id_cost[2]);
				printf("enter for\n");
				for(int i = 0; i<MAXNUMNODES; i++)
				{
					if(i != virtual_id)
					{
						int n_cost = nData.neighbor_id_cost[i];
						if(n_cost > 0)
						{
							neighbor_cost[i] = n_cost;
							
							rData_init.topology[virtual_id][i] = n_cost;
							rData_init.topology[i][virtual_id] = n_cost;							

							if(strcmp(nData.neighbor_ip_address[i], "N/A") != 0)
							{
								ip_address_nodes[i] = nData.neighbor_ip_address[i];
							}

							printf("now linked to node %d with cost %d\n", i, n_cost);
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
							rData_init.topology[virtual_id][i] = -1;
							rData_init.topology[i][virtual_id] = -1;
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
				printf("exit for\n");
				pthread_t sendThread;
				pthread_create(&sendThread, NULL, sendUDP, NULL);
				
				//pthread_t nodeThread;
				//pthread_create(&nodeThread, NULL, communicateWithNodes, NULL);
				
				converged = 0;
				restart_timer();
				//update_routing_flag = 1;
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
	if(argc != 2) {
                fprintf(stderr, "usage: linkstate managerhostname\n");
                exit(1);
        }
	
	//update manager's ip address
	strcpy(manager_ip_address, argv[1]);

	pthread_t managerThread;
	pthread_create(&managerThread, NULL, communicateWithManager, NULL);

	pthread_t convergeThread;
	pthread_create(&convergeThread, NULL, checkConvergence, NULL);
	
	pthread_join(convergeThread, NULL);

	return 0;
}

