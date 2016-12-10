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
//interrupt SIGTERM and SIGBREAK to safely close everything
#include <signal.h>
//log events
#include <sqlite3.h>


//signal handler
void sig_handler(int sig);
//database create
int create_db(sqlite3 *db);
int sqlite_log(sqlite3* db, const char* timestamp, const char* event_type,int lastDuration);

//prototype for push function
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
int loopWaitTime   = 100; //number of millisecs to wait between two loops0
//server options
int launchServer   = 1;    //should we launch a webserver ?
int serverPort     = 8888; //where should we listen

//push url
const char* pushHost     = NULL;
int   pushPort           = 80; //push port
int   pushDetailedEvents = 0; //should we push detailed events? (opening, open, closing, closed)
const char* PDEURL       = NULL;
int   pushCoarseEvents   = 0; //should we push coarse events? (open, closed)
const char* PCEURL       = NULL;

enum openState_t {unknown, opening, open, closing, closed};
enum openState_t openState;
int isOpen = 0;
int exitLoop = 0;

int testhtmlactivate =0;

const char* sqliteDBName = NULL; //default db null (no db)

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
void sprint_date();
//helper function to return the delta in millis since last timestamp. Also reset the timestamp to now
int get_delta_and_reset(struct timespec *event);
int parse_config_file(const char* config_file_name);
int answer_to_connection (void *cls, struct MHD_Connection *connection,
        const char *url,
        const char *method, const char *version,
        const char *upload_data,
        size_t *upload_data_size, void **con_cls);

//Push notifications
int report_event(enum openState_t openState, int isOpen);

//Main function
int main(int argc, char** argv)
{
    sqlite3* db=NULL;
    int rc;

    char* config_file=DEFAULT_CONFIG_FILE;
    // check arguments
    if (argc != 1 && argc != 3) {
        fprintf(stderr,"Usage: gatekeeper [-c <configfile>]\n");
        return 1;
    }
    // should be -c <config_file>
    if (argc == 3) {
        if(!strcmp(argv[1],"-c")) config_file=argv[2];
        else {
            //wasn't -c as first argument
            fprintf(stderr,"Usage: gatekeeper [-c <configfile>]\n");
            return 1;
        }
    }

    // Setup stuff:
    parse_config_file(config_file); 
    
    //GPIO setup
    wiringPiSetup(); // Initialize wiringPi -- using wiringPI pin numbers
    pinMode(motorPPin, INPUT); // Set Sense pin to input
    pinMode(motorNPin, INPUT); // Set Sense pin to input
    //Internal state init : unknown
    oldGateState=0xFF;
    openState = unknown;
    isOpen=0; //TODO read this from sql ?

    get_delta_and_reset(&gateStateLast);
    printf("Starting gate interface !\n");
    //install signal handler to quit when killed
    signal(SIGINT,&sig_handler);
    signal(SIGTERM,&sig_handler);

    //sqlite3 init
    if (sqliteDBName != NULL){
        //try to open the file
        printf("--- Opening database %s\n",sqliteDBName);
        rc = sqlite3_open(sqliteDBName,&db);
        if (rc != SQLITE_OK){
            fprintf(stderr,"Can't open database %s : %s\n",sqliteDBName,sqlite3_errmsg(db));
            db=NULL;
        }
        create_db(db);
        sprint_date();
        sqlite_log(db,date,"B",0);
    }

    //Webserver init
    struct MHD_Daemon *daemon;
    if (launchServer){
        printf("--- Starting webserver\n");
        daemon = MHD_start_daemon (MHD_USE_SELECT_INTERNALLY, serverPort, NULL, NULL,
            &answer_to_connection, NULL, MHD_OPTION_END);
        if (NULL == daemon) {
            fprintf(stderr,"Could not launch webserver\n");
            return 1;
        }
        else {
            printf("Listening on port %d\n",serverPort);
        }
    }
    /****************************************************
     * Event Detection Loop
     ***************************************************/
    int openDuration=0;
    int idleDuration=0;
    // Loop (while(1)):
    while(!exitLoop)
    {
        gateState = digitalRead(motorPPin) << 1 | digitalRead(motorNPin);
        //reset idletime if needed
        if (gateState != IDLE) { idleDuration =0; get_delta_and_reset(&idleLast);}
        //check for state change
        if (gateState != oldGateState) {
            int eventDelay = get_delta_and_reset(&gateStateLast);
            //detect start of opening and closing events
            if (openDuration == 0) {
                sprint_date();
                if (gateState == OPENING) { 
                    printf(">>> OPENING START %s\n",date); 
                    openState=opening; 
                    isOpen=1;
                    sqlite_log(db,date,"o",eventDelay);
                } else if (gateState == CLOSING) { 
                    printf(">>> CLOSING START %s\n",date); 
                    openState=closing; 
                    isOpen=0;
                    sqlite_log(db,date,"c",eventDelay);
                }
                report_event(openState,isOpen);
            }
            //cumulate closing and opening time
            if (oldGateState == OPENING) openDuration += eventDelay;
            else if (oldGateState == CLOSING) openDuration -= eventDelay;
        } else if (gateState == IDLE && oldGateState == IDLE) {
            // cumulate idle duration
            idleDuration += get_delta_and_reset(&idleLast);
            // if idle for more than a given time, generate end event
            if ((openDuration !=0) && (idleDuration > maxIdleTimeBeforeEvent)) {
                sprint_date();
                //printf(">>> CLOSING OR OPENING ENDS %s openingDuration %d\n",date,openDuration);
                switch (openState){
                    case opening:
                    case open:
                        openState=open;
                        isOpen=1;
                        sqlite_log(db,date,"O",openDuration);
                        break;
                    case closing:
                    case closed:
                        openState=closed;
                        isOpen=0;
                        sqlite_log(db,date,"C",openDuration);
                        break;
                    default:
                        openState=unknown;
                        isOpen=-1;
                }
                report_event(openState,isOpen);
                openDuration =0;
            }
        }

        // now save these variable for later
        oldGateState=gateState;

        delay(loopWaitTime); // Wait 200ms again

    }

    /****************************************************
     * End of Event Detection Loop
     ***************************************************/
    if (launchServer){
        printf("--- Shutting down Webserver\n");
        MHD_stop_daemon (daemon);
    }
    if (db !=NULL){
        sprint_date();
        sqlite_log(db,date,"S",0);
        printf("--- Closing database\n");
        sqlite3_close(db);
    }
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

//helper function to print the date to stdout
void sprint_date(){
	time_t now=time(NULL);
	struct tm* local_time;
	local_time=localtime(&now);
	strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",local_time);	
};

//helper function to return the delta in millis since last timestamp. Also reset the timestamp to now
int get_delta_and_reset(struct timespec *event){
	struct timespec eventTime;
	clock_gettime(CLOCK_MONOTONIC_RAW,&eventTime);
	int eventDuration =  ((eventTime.tv_sec - event->tv_sec)*1000000 + ((eventTime.tv_nsec - event->tv_nsec)/1000)) / 1000;
	*event=eventTime;
	return (eventDuration);
}

// helper function that reads the configuration file
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
    printf("--- Parsing config file %s\n",config_lookup_int);
    if(config_lookup_int(&cfg,"motorPPin",&motorPPin)) 
        printf("motorPPin changed to %d\n",motorPPin);     
    if(config_lookup_int(&cfg,"motorNPin",&motorNPin)) 
        printf("motorNPin changed to %d\n",motorNPin);         
    if(config_lookup_int(&cfg,"loopWaitTime",&loopWaitTime))         
        printf("loopWaitTime changed to %d\n",loopWaitTime);
    if(config_lookup_string(&cfg,"pushHost",&pushHost)) printf("pushHost:	%s\n",pushHost);
    if(config_lookup_string(&cfg,"PDEURL",&PDEURL)) printf("PDEURL:	%s\n",PDEURL);
    if(config_lookup_string(&cfg,"PCEURL",&PCEURL)) printf("PCEURL:	%s\n",PCEURL);
    if(config_lookup_string(&cfg,"sqliteDBFile",&sqliteDBName)) printf("sqliteDBName:	%s\n",sqliteDBName);
    if(config_lookup_int(&cfg,"launchServer",&launchServer)) printf("launchServer:	%d\n",launchServer);
    if(config_lookup_int(&cfg,"serverPort",&serverPort)) printf("serverPort:	%d\n",serverPort);
    if(config_lookup_int(&cfg,"pushPort",&pushPort)) printf("pushPort:	%d\n",pushPort);
    if(config_lookup_int(&cfg,"pushDetailedEvents",&pushDetailedEvents)) printf("pushDetailedEvents:	%d\n",pushDetailedEvents);
    if(config_lookup_int(&cfg,"pushCoarseEvents",&pushCoarseEvents)) printf("pushCoarseEvents:	%d\n",pushCoarseEvents);

    // Now make PDEURL and PCEURL magic by changing @@ with %d
    char* p;
    if (PDEURL != NULL){
        p=strstr(PDEURL,"@@");
        if (p!=NULL) {
            p[0]='%';
            p[1]='d';
        }
    }
    if (PCEURL != NULL){
        p=strstr(PCEURL,"@@");
        if (p!=NULL) {
            p[0]='%';
            p[1]='d';
        }
    }
    return (0);
}     

//Webserver callback function
int answer_to_connection (void *cls, struct MHD_Connection *connection,
        const char *url,
        const char *method, const char *version,
        const char *upload_data,
        size_t *upload_data_size, void **con_cls)
{
    char page[300];
    struct MHD_Response *response;
    int ret;
    //debug
    printf("url : %s\nmethod : %s\nversion:%s\ndata_size:%d\n",url,method,version,upload_data_size);
    
    //we only support GET method
    if (!strcmp("GET",method)){
            if(!strcmp("/gateState",url)) {
            snprintf(page,300,"%d\r\n",isOpen);
            response = MHD_create_response_from_buffer (strlen (page),
                    (void*) page, MHD_RESPMEM_MUST_COPY);
            ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
            MHD_destroy_response (response);
            }
            else if (!strcmp("/gateStateFull",url)){
                snprintf(page,300,"%d\r\n",openState);
                response = MHD_create_response_from_buffer (strlen (page),
                        (void*) page, MHD_RESPMEM_MUST_COPY);
                ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
                MHD_destroy_response (response);
            }
            else if (!strcmp("/gateStateHuman",url)){
                char* statefull_message;
                switch(openState){
                    case open: statefull_message="open"; break;
                    case closed: statefull_message="closed"; break;
                    case opening: statefull_message="opening"; break;
                    case closing: statefull_message="closing"; break;
                    case unknown: statefull_message="unknown"; break;
                }
                snprintf(page,300,"<html><head><title>GateState</title><body><p>Gate is now in state: %s (%s)</p><p>(%d/%d)</p><p>Last Event time: %s</p></body></html>\r\n",statefull_message,isOpen?"open":"closed",openState,isOpen,date);
                response = MHD_create_response_from_buffer (strlen (page),
                        (void*) page, MHD_RESPMEM_MUST_COPY);
                ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
                MHD_destroy_response (response);
            }
            else {
            //catch all
            snprintf(page,300,"<html><body><h1>404</h1><p>This was not found here</p></body></html>");
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

//Push notifications generation
int report_event(enum openState_t openState, int isOpen){
    char finalURL[250];

    // pushing events to remote webserver if needed
    if(pushHost && PDEURL && pushDetailedEvents){
        snprintf(finalURL,250,PDEURL,openState);
        http_send_request(pushHost,pushPort,finalURL);
    }
    if(pushHost && PCEURL && pushCoarseEvents){
        snprintf(finalURL,250,PCEURL,isOpen);
        http_send_request(pushHost,pushPort,finalURL);
    }
}

// sqlite logging
int sqlite_log(sqlite3* db, const char* timestamp, const char* event_type,int lastDuration){
    if (NULL==db) return 1;
    char req[500];
    char* error;
    int rc;
    //update event duration
    if (lastDuration >0){
        snprintf(req,500,"update eventlog set length = %d where id = (select max(id) from eventlog limit 1);",lastDuration);
        rc=sqlite3_exec(db,req,0,0,&error);
        if (rc != SQLITE_OK){
            fprintf(stderr, "sqlite update duration error %d: %s\nrequest: %s\n",rc,error,req);
            sqlite3_free(error);
            return(rc);
        }
    }
    snprintf(req,500,"INSERT INTO eventlog(ts,type,length) VALUES('%s','%s',0);",timestamp,event_type);
    rc=sqlite3_exec(db,req,0,0,&error);
    if (rc != SQLITE_OK){
        fprintf(stderr, "sqlite insert error %d: %s\nrequest:%s\n",rc,error,req);
        sqlite3_free(error);
        return(rc);
    }
}

//signal handler (break + kill)
void sig_handler(int sig){
    exitLoop = 1;
}

// Database creation
int create_db(sqlite3 *db){
    int rc;
    char *error;
    char *test = "SELECT id,ts,type,length from eventlog limit 1;";
    char *sql= "CREATE TABLE eventlog(" \
                "id   INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL," \
                "ts   DATETIME NOT NULL," \
                "type CHAR(1) NOT NULL DEFAULT('?') REFERENCES eventtypes(chartype),"\
                "length INTEGER NOT NULL DEFAULT(0)"\
                ");" \
                "CREATE TABLE eventtype(" \
                "chartype CHAR(1) PRIMARY KEY NOT NULL," \
                "name VARCHAR(15) NOT NULL" \
                ");" \
                "INSERT INTO eventtype(chartype,name) VALUES('?','unknown');" \
                "INSERT INTO eventtype(chartype,name) VALUES('o','opening');" \
                "INSERT INTO eventtype(chartype,name) VALUES('c','closing');" \
                "INSERT INTO eventtype(chartype,name) VALUES('O','open');" \
                "INSERT INTO eventtype(chartype,name) VALUES('C','closed');" \
                "INSERT INTO eventtype(chartype,name) VALUES('B','boot');" \
                "INSERT INTO eventtype(chartype,name) VALUES('S','shutdown');"; 

    if(db == NULL) return 1;
    //test if we can read from db
    rc=sqlite3_exec(db,test,0,0,&error);
    if (rc != SQLITE_OK) {
        sqlite3_free(error);
        //not ok, create db
        rc=sqlite3_exec(db,sql,0,0,&error);
        if (rc != SQLITE_OK){
            fprintf(stderr,"Error %d:%s\n",rc,error);
            return rc;
        }
    } else {
        return 0;
    }
}

