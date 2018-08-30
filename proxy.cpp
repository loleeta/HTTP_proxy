/* November 5, 2017
 *
 * This program implements a web proxy using HTTP 1.0, which takes in GET 
 * requests, parses the absolute URI, and forwards the modified request to the 
 * origin server, and returns the response to the client. This program is 
 * multi-threaded, using pthreads, and can be tested using a browser configured
 * for HTTP/1.0.
 * 
 * To compile: g++ -lpthread -std=c++11 -Wall -pedantic proxy.cpp -o proxy
 * To run: ./proxy <portNumber> 
 *	ex: 10088
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>
#include <pthread.h>
#include <stdint.h>

using namespace std;

#define BACKLOG 10

#define MAXDATASIZE 4096 //max amount of bytes for buffer

/* Writes n bytes from fd to buf, restarting after partial write or
 * interruptions by a signal handler. Returns number of bytes written. */ 
ssize_t writen(int fd, char* buf, size_t n);

/* Clean exit for program */
void cleanExit();

/* Ignore interrupts from pipe */
void ignore_sigpipe();

/* Main function for thread; gets HTTP request from browser and parses it, 
 * creates a socket to browser and connects to it, sends the header and 
   receives a response back */
void* sendRecv (void* socket);

/* Parses a formatted HTTP request and returns a header with relative URL */
string parseInitialInput(string userInput);

/* Struct holding:
 * int for socket
 * string for IP address
 * number of pthreads created so far */
struct sockArg{
	int sock;
	string ipAddress;
	int threadCount = 0;
};

/* Returns pointer to an IPV4 or IPV6 addressed-sized value */
void *get_in_addr(struct sockaddr *sa){
	if(sa->sa_family == AF_INET){
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* Main program function */
int main(int argc, char *argv[]){
	int sockfd; //Listening socket
	int new_fd; //New connection socket
	int rv;
	int yes = 1;

	string str;

	char s[INET6_ADDRSTRLEN];

	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; //client's address
	
	ignore_sigpipe();

	socklen_t sin_size;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; //use my IP

	const char* port = argv[1];

	// checks required number of arguments
	if (argc != 2) {
		printf("Argument required: port number.\nExitng program.\n\n");
		return 1;
	}

	// checks for valid address
	if((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0){
		fprintf(stderr, "Error getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	//loops through linked list and binds to first socket available
	for(p = servinfo; p != NULL; p = p->ai_next){
		if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
			printf("Error: Creating socket on server");
			continue;
		}

		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
			printf("Error: setsockopt");
			exit(1);
		}

		if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1){
			close(sockfd);
			printf("Error: Binding server");
			continue;
		} 

		break;
	}

	freeaddrinfo(servinfo);

    if (p == NULL)  {
        printf("Server: Failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        printf("Error: Setting socket to listen");
        exit(1);
    }

    printf("\nServer: Waiting for connections...\n\n");

    //Server is available, waiting for connections, ad receives request from
    //client (browser), and creates a pthread, which executes the pthread
    //function. 
	while(true) {
		pthread_t pid;

		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);

		if(new_fd == -1){
			printf("Error: Accepting connection");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		printf("Server: Got connection from %s\n\n", s);

		sockArg *sock1 = new sockArg;
		sock1->sock = new_fd;	
		sock1->ipAddress = s;

		int status = pthread_create(&pid, NULL, sendRecv, (void*)sock1);

		if(status){
			cout << "ERROR CREATING THREAD" << endl;
		} else {
			cout << "NEW THREAD" << endl;
		}
	}    

	return 0;
}

/* Creates a socket from args and parses the request and sends the request to
 * the remote (origin) server */
void* sendRecv (void* sock) {
	sockArg *clientSock;
	clientSock = (sockArg*)sock; //create socket from parameters
	string ipClient = clientSock->ipAddress;

	int browserSock; //socket for origin server
	int numbytes;
	bool isLocal; //boolean for if request is from telnet localhost or browser

	string getRequest; //Request from user.
	string error = "500 'Internal Error'\n";
	string request;	// Complete request
	string hostUrl;

	int rv;

	struct addrinfo hints;
	struct addrinfo *q;
	struct addrinfo *httpinfo;

	char buf[MAXDATASIZE];

	pthread_detach(pthread_self());

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if(!ipClient.compare("127.0.0.1")){
		isLocal = true;
	} else {
		isLocal = false;
	}

	if((numbytes = recv(clientSock->sock, buf, MAXDATASIZE-1, 0)) == -1){
		perror("Error recieving from client");
		exit(1);
	}

	//receives the message from client socket 
	getRequest = string(buf);

	string getRequest2 = getRequest;

	//parses initial HTTP request 
	request = parseInitialInput(getRequest);

	//parses message depending on where it's from 
	if(isLocal == true){
		//Used for telnet
	   	getRequest2.erase(0, getRequest2.find("w"));
	    hostUrl = getRequest2.substr(0, getRequest2.find("/"));

	} else {
		//Used for web browser
		getRequest2.erase(0, getRequest2.find("Host: "));
		getRequest2.erase(0, 6);
	    hostUrl = getRequest2.substr(0, getRequest2.find("User-Agent"));
	    hostUrl = hostUrl.substr(0, hostUrl.size()-2);
	}

	//if request is not formatted correctly, send 500 Internal Error back 
	if(!request.compare(error)) {

		memset(buf, 0, sizeof buf);
		strcpy(buf,error.c_str());
		if(send(clientSock->sock, buf, sizeof buf, 0) == -1) {
			perror("Error sending error msg");
			pthread_exit(NULL);
		}
		pthread_exit(NULL);
	}

	string defaultPort = "80";

	//checks for valid address
	if((rv = getaddrinfo(hostUrl.c_str(), defaultPort.c_str(), &hints, &httpinfo)) != 0){
		fprintf(stderr, "Error getaddrinfo: %s\n", gai_strerror(rv));
		pthread_exit(NULL);
	}

	//loops through linked list and binds to first socket available
	for (q = httpinfo; q != NULL; q = q->ai_next) {
		if ((browserSock = socket(q->ai_family, q->ai_socktype, q->ai_protocol)) == -1) {
			printf("Error: 2nd socket creation\n");
			continue;
		}

		if(connect(browserSock, q->ai_addr, q->ai_addrlen) == -1){
			close(browserSock);
			perror("Error: client connect");
			continue;
		}

		break;
	}

	if(send(browserSock, request.c_str(), request.length(), 0) == -1) {
		cout << "Error sending" << endl;
	}

	memset(buf, 0, sizeof buf); //reset buffer 

	int bytesRecv;
	int bufLen = sizeof buf;

	//while buffer receives from browser, send to client
	while((bytesRecv = recv(browserSock, buf, bufLen, 0)) > 0){
		if (writen(clientSock->sock, buf, bytesRecv) == -1) {
			break;
		}
	}

	close(clientSock->sock);
	close(browserSock);
	freeaddrinfo(httpinfo);

    pthread_exit(NULL);   
}

/* Writes n bytes from fd to buf, restarting after partial write or
 * interruptions by a signal handler. Returns number of bytes written. 
 * Source: http://man7.org/tlpi/code/online/dist/sockets/rdwrn.c.html */ 
ssize_t writen(int fd, char* buf, size_t n)
{
    ssize_t numWritten;                /* # of bytes written by last write() */
    size_t totWritten;                 /* Total # of bytes written so far */

    for (totWritten = 0; totWritten < n; ) {
        numWritten = write(fd, buf, n - totWritten);

        /* The "write() returns 0" case should never happen, but the
           following ensures that we don't loop forever if it does */

        if (numWritten <= 0) {
            if (numWritten == -1 && errno == EINTR)
                continue;              /* Interrupted --> restart write() */
            else
                return -1;             /* Some other error */
        }
        totWritten += numWritten;
        buf += numWritten;
    }

    return totWritten;                 /* Must be 'n' bytes if we get here */
}

/* Free up port if exiting from program */
void cleanExit() {
	exit(0);
}

/* Handles signals from pipe by ignoring 
 * https://stackoverflow.com/questions/7774569/using-signals-and-sigpipe
 * https://stackoverflow.com/questions/17942034/simple-linux-signal-handling */
void ignore_sigpipe() {
	struct sigaction act;
	int r;
	act.sa_handler = SIG_IGN; //ignore the interrupt
	act.sa_flags = 0;
	r = sigaction(SIGPIPE, &act, NULL); //register signal 
	if (r == -1) {
		cleanExit();
	}
}

/* Parses HTTP request for absolute URI format and returns relative URI */
string parseInitialInput(string userInput){
	string method;	// HTTP type
	string scheme;	// Only GET requests can be used
	string link; 	// Inital web link with file path
	string path;	// File path
	string auth;	// Host address
	string portno;	// Port Number
	string ver;		// HTTP version
	string request;	// Complete request

	int start, ptr, end, ptr2, authlen, pathlen, portlen; //pointers 

	string getRequest = userInput;
	int numbytes = getRequest.length();

	//parse absolute URI
	//URI = scheme :// auth / path
	//GET http://www.seattleu.edu:1000000/index.html HTTP/1.0
	
	start = 0; //pointer to start of string
	ptr = 0; //pointer for string
	end = numbytes; //pointer for end of string;
	string error = "500 'Internal Error'\n";

	//parse for scheme of URI: POST or GET, only accept GET
	while ((getRequest[ptr] != ' ') && (ptr <= end)) {

		ptr++;
	}
	scheme = getRequest.substr(start, ptr);

	if (scheme.compare("GET")) {
		return error;
	} //if it's CONNECT or POST 
	
	//parse for method of URI: HTTP or HTTPS, only accept HTTP
	ptr++;
	start = ptr;
	while ((getRequest[ptr] != ':') && (ptr <= end)) {
		ptr++;
	}

	method = getRequest.substr(start, ptr-start);
	if (!method.compare("https")) {
		return error;
	} 
	
	//parse for colon
	start = ptr;
	ptr++;
	if (getRequest[start] != ':') {
		return error;
	}

	//parse for two slashes
	ptr++; 
	if ((getRequest[ptr] != '/') && (getRequest[ptr+1] != '/') && (ptr <= end)) {
		return error;
	}

	//parse for link: www.seattleu.edu/index.html 
	ptr++;
	start = ptr;
	while ((getRequest[ptr] != ' ') && (ptr <= end)) {
		ptr++;
	}

	link = getRequest.substr(start, ptr-start);

	//parse for path in link
	ptr2 = 0;
	int linklen = link.length();
	while ((link[ptr2] != '/') && (ptr2 < linklen)) {
		ptr2++;
	}

	auth = link.substr(0, ptr2); //authority
	path = link.substr(ptr2, linklen); //path

	//parse for port number in link, e.g. laml2@seattleu.edu:80
	authlen = auth.length();
	pathlen = path.length();
	int ptr3 = 0;

	while ((link[ptr3] != ':') && (ptr3 < authlen)) {
		ptr3++;
	}

	if (link[ptr3] == ':') { //found a port number
		portno = auth.substr(ptr3+1, authlen-pathlen);
		portlen = portno.length();
		auth = auth.substr(0, (authlen-portlen-1));
	}
	else {
		portno = "80"; //Default port number if no number is provided
	}

	//parse for HTTP version; 1.0 or 1.1; 1.0 only 
	start = ptr;
	while ((getRequest[ptr] != '/') && (ptr <= end)) {
		ptr++;
	}
	ver = getRequest.substr(ptr + 1, 3); //assuming the length of it is 3
	if (ver != "1.0") {
		return error;
	}	

	request = scheme + " " + path + " HTTP/" + ver + "\r\n" + "Host: " + auth + 
		"\r\n" + "Connection: close" + "\r\n\r\n";

	return request;
}
