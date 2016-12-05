#include <stdio.h>    // Used for printf() statements
#include <wiringPi.h> // Include WiringPi library!
#include <time.h>
//configuration
#include <libconfig.h>
//microhttpd
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <microhttpd.h>
#include <string.h>
//http connection
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h> /* getprotobyname */
#include <netinet/in.h>
#include <unistd.h>
int http_send_request(const char* hostname, unsigned short port, const char* URL);
// OPENING TIME 25200
// CLOSING TIME 32100
// listening port
#define PORT 8888
// Pin number declarations. We're using the wiringPi chip pin numbers.
// These will be overwritten by a config if there is one...

int motorPPin      = 0; // Motor pin (through optocoupler)
int motorNPin      = 1; // Motor pin (through optocoupler)
int maxIdleTimeBeforeEvent = 2000; // Wait 2s before event generation
int loopWaitTime   = 100; //number of millisecs to wait between two loops

int testhtmlactivate =0;

//GLOBALS
#define ILLEGAL (0)
#define OPENING (1)
#define CLOSING (2)
#define IDLE    (3)
#define DEFAULT_CONFIG_FILE ("gatekeeper.cfg")

//Current state and previous state of the GPIO pins
int gateState,oldGateState;

// timestamps of the previous events
struct timespec gateStateLast;
struct timespec idleLast;
struct timespec eventTime;
char date[100];

//helper function to print the date to stdout
void sprint_date(){
	time_t now=time(NULL);
	struct tm* local_time;
	local_time=localtime(&now);
	strftime(date,sizeof(date),"%d/%m/%y %H:%M:%S",local_time);	
};

//helper function to return the delta in millis since last timestamp. Also reset the timestamp to now
int get_delta_and_reset(struct timespec *event){
	struct timespec eventTime;
	clock_gettime(CLOCK_MONOTONIC_RAW,&eventTime);
	int eventDuration =  ((eventTime.tv_sec - event->tv_sec)*1000000 + ((eventTime.tv_nsec - event->tv_nsec)/1000)) / 1000;
	*event=eventTime;
	return (eventDuration);
}

int parse_config_file(const char* config_file_name){
    config_t cfg;
    config_setting_t *root, *setting, *movie;

    config_init(&cfg);

    /* Read the file. If there is an error, report it and exit. */
    if(! config_read_file(&cfg, config_file_name))
    {
        fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
                config_error_line(&cfg), config_error_text(&cfg));
        config_destroy(&cfg);
        return(1);
    }
    // Config file parsed, let's use it !
    if(config_lookup_int(&cfg,"motorPPin",&motorPPin)) 
        printf("motorPPin changed to %d\n",motorPPin);     
    if(config_lookup_int(&cfg,"motorNPin",&motorNPin)) 
        printf("motorNPin changed to %d\n",motorNPin);         
    if(config_lookup_int(&cfg,"loopWaitTime",&loopWaitTime))         
        printf("loopWaitTime changed to %d\n",loopWaitTime);
    return (0);
}     
int answer_to_connection (void *cls, struct MHD_Connection *connection,
        const char *url,
        const char *method, const char *version,
        const char *upload_data,
        size_t *upload_data_size, void **con_cls)
{
    char page[200];
    struct MHD_Response *response;
    int ret;
    //debug
    printf("url : %s\nmethod : %s\nversion:%s\ndata_size:%d\n",url,method,version,upload_data_size);
    
    //we only support GET method
    if (!strcmp("GET",method)){
            if(!strcmp("/state",url)) {
            snprintf(page,200,"GateState:%d\r\ntest:%d\r\n",gateState,testhtmlactivate);
            response = MHD_create_response_from_buffer (strlen (page),
                    (void*) page, MHD_RESPMEM_MUST_COPY);
            ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
            MHD_destroy_response (response);
          }
          else {
            //catch all
            snprintf(page,200,"<html><body><h1>404</h1><p>This was not found here</p></body></html>");
            //send the response
            response = MHD_create_response_from_buffer (strlen (page),
                    (void*) page, MHD_RESPMEM_MUST_COPY);
            ret = MHD_queue_response (connection, 404, response);
            MHD_destroy_response (response);
          }
    }
    else {
        //only GET supported
        snprintf(page,200,"<html><body><h1>403</h1><p>This is not allowed</p></body></html>");
        //send the response
        response = MHD_create_response_from_buffer (strlen (page),
                (void*) page, MHD_RESPMEM_MUST_COPY);
        ret = MHD_queue_response (connection, 403, response);
        MHD_destroy_response (response);
    }

    return ret;
}

//Main function
int main(void)
{

    // Setup stuff:
    parse_config_file(DEFAULT_CONFIG_FILE); //TODO parse argv
    
    //GPIO setup
    wiringPiSetup(); // Initialize wiringPi -- using wiringPI pin numbers

    pinMode(motorPPin, INPUT); // Set Sense pin to input
    pinMode(motorNPin, INPUT); // Set Sense pin to input
    
    oldGateState=0xFF;
    get_delta_and_reset(&gateStateLast);

    printf("Starting gate interface !\n");

    //Webserver
    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
            &answer_to_connection, NULL, MHD_OPTION_END);
    if (NULL == daemon) {
        fprintf(stderr,"Could not launch webserver\n");
        return 1;
    }
    int openDuration=0;
    int idleDuration=0;
    // Loop (while(1)):
    while(1)
    {
        gateState = digitalRead(motorPPin) << 1 | digitalRead(motorNPin);
        //reset idletime if needed
        if (gateState != IDLE) { idleDuration =0; get_delta_and_reset(&idleLast);}
        //check for state change
        if (gateState != oldGateState) {
            int eventDelay = get_delta_and_reset(&gateStateLast);
            //detect start of opening and closing events
            if (openDuration == 0) {
                if (gateState == OPENING) { printf(">>> OPENING START\n"); }
                else if (gateState == CLOSING) { printf(">>> CLOSING START\n");}
            }
            //cumulate closing and opening time
            if (oldGateState == OPENING) openDuration += eventDelay;
            else if (oldGateState == CLOSING) openDuration -= eventDelay;
            /*
            sprint_date();
            printf("%s Motor changed to: ",date);
            switch (gateState) {
                case ILLEGAL: printf("illegal"); break;
                case OPENING: printf("opening"); break;
                case CLOSING: printf("closing"); break;
                case IDLE:    printf("idle"); break;
                default:      printf("unknown"); break;
            }
            printf("\t+%d\n",eventDelay);
            */
            sprint_date();
            printf("%s currentstate %d openingDuration %d\n",date,gateState,openDuration);
        } else if (gateState == IDLE && oldGateState == IDLE) {
            // cumulate idle duration
            idleDuration += get_delta_and_reset(&idleLast);
            // if idle for more than a given time, generate end event
            if ((openDuration !=0) && (idleDuration > maxIdleTimeBeforeEvent)) {
                sprint_date();
                printf(">>> CLOSING OR OPENING ENDS %s openingDuration %d\n",date,openDuration);
                openDuration =0;
            }
        }

        // now save these variable for later
        oldGateState=gateState;

        delay(loopWaitTime); // Wait 200ms again

    }

    MHD_stop_daemon (daemon);
    return 0;
}

//This code mainly comes from this answer on stackoverflow: http://stackoverflow.com/a/35680609/940745
//It was adapted to fit in this scheme
#define URLBUFSIZE 512
#define REQBUFSIZE 512
#define DATBUFSIZE 512
int http_send_request(const char* hostname, unsigned short port, const char* URL){
    	char buffer[DATBUFSIZE];
	char request[REQBUFSIZE];
	struct protoent *protoent;
	in_addr_t in_addr;
	int request_len;
	int socket_file_descriptor;
	ssize_t nbytes_total, nbytes_last;
	struct hostent *hostent;
	struct sockaddr_in sockaddr_in;

	/* build request */
	request_len=snprintf(request,REQBUFSIZE,"GET %s HTTP/1.1\r\nHost: %s\r\n\r\n",URL,hostname);

	/* Build the socket. */
	protoent = getprotobyname("tcp");
	if (protoent == NULL) { perror("getprotobyname"); return (1); }

	socket_file_descriptor = socket(AF_INET, SOCK_STREAM, protoent->p_proto);
	if (socket_file_descriptor == -1) { perror("socket"); return (1); }

	/* Build the address. */
	hostent = gethostbyname(hostname);
	if (hostent == NULL) { fprintf(stderr, "error: gethostbyname(\"%s\")\n", hostname); return (1); }

	in_addr = inet_addr(inet_ntoa(*(struct in_addr*)*(hostent->h_addr_list)));
	if (in_addr == (in_addr_t)-1) { fprintf(stderr, "error: inet_addr(\"%s\")\n", *(hostent->h_addr_list)); return (1); }

	sockaddr_in.sin_addr.s_addr = in_addr;
	sockaddr_in.sin_family = AF_INET;
	sockaddr_in.sin_port = htons(port);

	/* Actually connect. */
	if (connect(socket_file_descriptor, (struct sockaddr*)&sockaddr_in, sizeof(sockaddr_in)) == -1) { perror("connect"); return (1); }

	/* Send HTTP request. */
	nbytes_total = 0;
	while (nbytes_total < request_len) {
		nbytes_last = write(socket_file_descriptor, request + nbytes_total, request_len - nbytes_total);
		if (nbytes_last == -1) { perror("write"); return (1); }
		nbytes_total += nbytes_last;
	}

	/*
	   Read the response. 
	*/
	nbytes_total = 0;
	while ((nbytes_total = read(socket_file_descriptor, buffer, DATBUFSIZE))  > 0 ) {
		//write(STDOUT_FILENO, buffer, nbytes_total);
        //TODO save content to a char* ?
		if(nbytes_total < DATBUFSIZE) break;
	}
	if (nbytes_total == -1) {
		perror("read");
		return (1);
	}

	close(socket_file_descriptor);
	return(0);
}
/*
int main(int argc, char** argv) {
	if(http_send_request("example.com", 80, "/")){
		fprintf(stderr,"Error\n");
	}	
	if(http_send_request("example.com", 80, "/")){
		fprintf(stderr,"Error\n");
	}	
	return 0;
}
*/
