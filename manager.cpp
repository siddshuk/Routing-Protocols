#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <map>
#include <vector>
#include <sstream>
#include <iostream>
#include <algorithm>

#define MYPORT "8000"    // the port users will be connecting to

#define BACKLOG 20	 // how many pending connections queue will hold

#define MAXDATASIZE 4000 // max number of bytes we can get at once

#define MAXNUMNODES 20 //max number of nodes that can connect at once

using namespace std;

//global variables
map<int, vector< vector<int> > > topology; 
vector<int> nodes_all;
map<int, int> socket_fds;
map<int, string> ip_address_nodes;
map<int, bool> nodes_connected;

typedef struct neighbor_data
{
	//map<int, int> neighbor_id_cost;
	//map<int, string> neighbor_ip_address;
	int neighbor_id_cost[MAXNUMNODES];
	char neighbor_ip_address[MAXNUMNODES][100];
} neighbor_data;

typedef struct message_data
{
	int source;
	int destination;
	char msg[MAXDATASIZE];
} message_data;

message_data mData[100];

void initialize()
{
	
}

/*
*	Testing if topology put in map correctly
*/
void printTopology()
{
	map<int, vector< vector<int> > >::const_iterator it_map;
	for(it_map = topology.begin(); it_map != topology.end(); ++it_map)
	{
		printf("Source: %d => \n", it_map->first);
		vector< vector<int> >::const_iterator it_vec;
		for(it_vec = (it_map->second).begin(); it_vec != (it_map->second).end(); ++it_vec)
		{
			printf("\t* Destination: %d => Cost: %d\n", (*it_vec)[0], (*it_vec)[1]);
		}
		printf("\n");
	}
}

void updateTopology(char * line, char * pch)
{
	pch = strtok(line, " ");
	int pch_key_flag = 0;
	int top_map_key1;
	int top_map_key2;
	vector< vector<int> > top_map_value1;
	vector< vector<int> > top_map_value2;
	vector<int> valuePair1;
	vector<int> valuePair2;
	while(pch != NULL)
	{
		if(pch_key_flag == 0)
		{	
			istringstream (pch) >> top_map_key1;
			valuePair2.push_back(top_map_key1);
		}
		else
		{
			int destOrCost;
			istringstream (pch) >> destOrCost;
			if(pch_key_flag == 1)
			{
				//destination value is read and stored to be a key later
				top_map_key2 = destOrCost;	
			
				valuePair1.push_back(destOrCost);
			}

			else
			{
				valuePair1.push_back(destOrCost);
				valuePair2.push_back(destOrCost);
			}
		}
		pch_key_flag++;
		pch = strtok(NULL, " ");
	}
	top_map_value1.push_back(valuePair1);
	top_map_value2.push_back(valuePair2);
		
	//Store Key:<Source> & Value:<Destination, Cost>
	map<int, vector< vector<int> > >::const_iterator search1 = topology.find(top_map_key1);		 
	if(search1 == topology.end())
	{
		//new key
		topology[top_map_key1] = top_map_value1;
	}
	else
	{
		//key already exists
		vector< vector<int> > updated_top_map_val = search1->second;
		updated_top_map_val.push_back(valuePair1);
		topology[search1->first] = updated_top_map_val;
	}
	
	//Store Key:<Destination> & Value:<Source, Cost>
	map<int, vector< vector<int> > >::const_iterator search2 = topology.find(top_map_key2);		 
	if(search2 == topology.end())
	{
		//new key
		topology[top_map_key2] = top_map_value2;
	}
	else
	{
		//key already exists
		vector< vector<int> > updated_top_map_val = search2->second;
		updated_top_map_val.push_back(valuePair2);
		topology[search2->first] = updated_top_map_val;
	}

	//insert nodes
	if(std::find(nodes_all.begin(), nodes_all.end(), top_map_key1) == nodes_all.end())
	{
		//new node
		nodes_all.push_back(top_map_key1);
	}
	if(std::find(nodes_all.begin(), nodes_all.end(), top_map_key2) == nodes_all.end())
	{
		//new node
		nodes_all.push_back(top_map_key2);
	}
	
	printTopology();
}

void parseMessageFile(char * fileName)
{
	FILE *message_file = fopen(fileName, "r");
	if (message_file == NULL)
		perror("Message file is empty");

	char * line = NULL;
	char * pch;
	size_t len = 0;
	size_t read;
	int pch_flag = 0;
	int i = 0;
	while ((read = getline(&line, &len, message_file)) != -1) 
	{
		int startPoint = 0;
		pch = strtok(line, " ");

		while(pch != NULL)
		{
			if(pch_flag == 0)
			{
				startPoint += strlen(pch);
				istringstream (pch) >> mData[i].source;
				pch_flag = 1;
			}
			else if(pch_flag == 1)
			{
				startPoint += strlen(pch);
				istringstream (pch) >> mData[i].destination;
				pch_flag = 0;
				break;
			}
			pch = strtok(NULL, " ");
		}
		startPoint += 2;
		//strncpy(mData.msg, line+startPoint, strlen(line)-1-startPoint);
		//printf("LINE SIZE = %d\n", len);
		strncpy(mData[i].msg, line+startPoint, len-startPoint);

		printf("MESSAGE source = %d\n", mData[i].source);
	 	printf("MESSAGE destination = %d\n", mData[i].destination);
		printf("MESSAGE data = %s\n", mData[i].msg);

		i++;	
	}
	fclose(message_file);
}

void * informNode(void * param)
{
	printf("ENTERING INFOMR\n");	
	int * arg_id = (int *) param;
	int virtual_id = *arg_id;
	struct neighbor_data nData;

	for(int i = 0; i<MAXNUMNODES; i++)
	{
		nData.neighbor_id_cost[i] = 0;
		strcpy(nData.neighbor_ip_address[i], "N/A");
	}

	map<int, vector< vector<int> > >::const_iterator search = topology.find(virtual_id);		 
	if(search != topology.end())
	{
		//key exists
		vector< vector<int> >::const_iterator it;
		for(it = (search->second).begin(); it != (search->second).end(); ++it)
		{
			int neighbor_id = (*it)[0];
			printf("NEIGHBOR ID: %d => ", neighbor_id);	
			nData.neighbor_id_cost[neighbor_id] = (*it)[1];
			printf("NEIGHBOR COST: %d => \n", (*it)[1]);
			map<int, bool>::const_iterator conn_it = nodes_connected.find(neighbor_id);	
			if(conn_it != nodes_connected.end())
			{
				printf("OH\n");
				if(conn_it->second == true)
				{
					printf("YEA\n");
					map<int, string>::const_iterator search_ip = ip_address_nodes.find(neighbor_id);
					if(search_ip != ip_address_nodes.end())
					{
						//printf("IP = %s\n", search_ip->second);
						//nData.neighbor_ip_address[neighbor_id] = new char[sizeof(search_ip->second)];
						strcpy(nData.neighbor_ip_address[neighbor_id], (search_ip->second).c_str());  
						printf("YO");
					}
				}
			}
		}
	}

	/*map<int, string>::const_iterator check;
	for(check = nData.neighbor_ip_address.begin(); check != nData.neighbor_ip_address.end(); ++check)
	{
		printf("CHECK: %s\n", check->second);
	}*/

	char buffer[MAXDATASIZE];
	//printf("Size of nData = %d\n", sizeof(nData));
	memcpy(buffer, &nData, sizeof(neighbor_data));
	printf("NO ERR\n");
	map<int, int>::const_iterator sock_it = socket_fds.find(virtual_id);	
	if(sock_it != socket_fds.end())
	{
		//printf("buffer = %s\n", buffer[0]);
		if(send(sock_it->second, buffer, MAXDATASIZE, 0) != -1)
			printf("SENDING 4000\n");
			//perror("Error sending neighbor info to node");
	}
	return NULL;
}

void * stdinHandler(void * param)
{
	//maybe sleep for a bit
	sleep(2);	
	while(1)
	{
		printf("\nEnter additional topology info: ");
		char * line = new char[50];
		cin.getline(line, 50);
		printf("LINE ENTERED: %s\n", line);
		char * pch;	
		updateTopology(line, pch);
	
		//send neighbor info to node
        	map<int, int>::const_iterator sock_it;
		for(sock_it = socket_fds.begin(); sock_it != socket_fds.end(); ++sock_it)
		{
			int * arg = new int;
			*arg = sock_it->first;
			pthread_t nodeThread;
			pthread_create(&nodeThread, NULL, informNode, arg);		
		
		}        
	
		sleep(1);
	}

	return NULL;
}

/*
*	Update topology by parsing input file
*/	
void parseTopologyFile(char * fileName)
{
	FILE *topology_file = fopen(fileName, "r");
	if (topology_file == NULL)
		perror("Topology file is empty");

	char * line = NULL;
	char * pch;
	size_t len = 0;
	size_t read;
	while ((read = getline(&line, &len, topology_file)) != -1) 
	{
		updateTopology(line, pch);		
	}
	//arrange all nodes in order
	sort(nodes_all.begin(), nodes_all.end());
	fclose(topology_file);
}

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	initialize();
	int sockfd, new_fd;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	if(argc != 3) {
		fprintf(stderr, "usage: manager top_fileName msg_fileName\n");
		exit(1);
	}
	parseTopologyFile(argv[1]);
	//printTopology();
	parseMessageFile(argv[2]);	

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");
	
	pthread_t stdinThread;
	pthread_create(&stdinThread, NULL, stdinHandler, NULL);
	
	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);
		
		//assign lowest id as virtual id for the node
		int virtual_id = nodes_all.front();
		
		//delete the newly assigned id from list
		nodes_all.erase(nodes_all.begin());
		
		//set the node as connected
		printf("SETTING CONNECTED ID: %d\n", virtual_id);
		nodes_connected[virtual_id] = true;
	
		//save socket being used by node
		socket_fds[virtual_id] = new_fd;		

		//save ip address of the node
		ip_address_nodes[virtual_id] = s;	

		stringstream ss;
		ss << virtual_id;
		char * virtual_id_str = (char *) ss.str().c_str();
 		
		//send virtual id to node
		if(send(new_fd, virtual_id_str, MAXDATASIZE, 0) != -1)
                	printf("SENDING VIRTUAL ID\n");
			//perror("Error sending virtual id");

		sleep(1);
		int msg_flag = 0;
		char msg_buf[MAXDATASIZE];
		for(int i = 0; i<100; i++)
		{
			if(mData[i].source == virtual_id)
			{
				msg_flag = 1;
				memcpy(msg_buf, &(mData[i]), sizeof(message_data));
				//send virtual id to node
				if(send(new_fd, msg_buf, MAXDATASIZE, 0) != -1)
                			printf("SENDING MESSAGE\n");
			}
		}
		if(!msg_flag)
		{
			message_data empty_msg;
			empty_msg.source = -1;
			empty_msg.destination = -1;
			strcpy(empty_msg.msg, "");	
		
			memcpy(msg_buf, &empty_msg, sizeof(message_data));		
			if(send(new_fd, msg_buf, MAXDATASIZE, 0) != -1)
               			printf("SENDING EMPTY MESSAGE\n");
		}

		//send neighbor info to node
		map<int, int>::const_iterator sock_it;
		for(sock_it = socket_fds.begin(); sock_it != socket_fds.end(); ++sock_it)
		{
			int * arg = new int;
			*arg = sock_it->first;
			pthread_t nodeThread;
			pthread_create(&nodeThread, NULL, informNode, arg);		
		
		}
	}

	return 0;
}
