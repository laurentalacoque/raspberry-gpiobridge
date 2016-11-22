#include <stdio.h>    // Used for printf() statements
#include <wiringPi.h> // Include WiringPi library!
#include <time.h>

// Pin number declarations. We're using the wiringPi chip pin numbers.
const int motorPPin      = 0; // Motor pin (through optocoupler)
const int motorNPin      = 1; // Motor pin (through optocoupler)
const int ringPin        = 2; // Ring sense pin (through optocoupler)
const int busSensePin    = 3; // Motor BUS pin (0 means someone's asking for motor to activate)
const int busActivatePin = 4; // Relay command on Motor BUS

const int activateBus    = 0;
const int deactivateBus  = 1;
const int busActivationTime = 500; // 500ms

const int busActivated   = 0; // GPIO state when bus is activated
const int ringActivated  = 0; // GPIO state when ringer is activated

const int loopWaitTime   = 200; //number of millisecs to wait between two loops

//GLOBALS
#define ILLEGAL (0)
#define OPENING (1)
#define CLOSING (2)
#define IDLE    (3)

//Current state and previous state of the GPIO pins
int gateState,oldGateState;
int ringer,oldRinger;
int bus,oldBus;

// timestamps of the previous events
struct timespec gateStateLast;
struct timespec ringerLast;
struct timespec busLast;
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

//Main function
int main(void)
{

    // Setup stuff:
    wiringPiSetup(); // Initialize wiringPi -- using wiringPI pin numbers

    pinMode(motorPPin, INPUT); // Set Sense pin to input
    pinMode(motorNPin, INPUT); // Set Sense pin to input
    pinMode(ringPin,   INPUT); // Set Sense pin to input
    pinMode(busSensePin, INPUT); // Set Sense pin to input

    pinMode(busActivatePin, OUTPUT);     // Set regular LED as output
    
    oldGateState=0xFF;
    oldRinger=0xFF;
    oldBus=0xFF;
    get_delta_and_reset(&gateStateLast);
    get_delta_and_reset(&ringerLast);
    get_delta_and_reset(&busLast);

    printf("Starting gate interface !\n");

    // Loop (while(1)):
    while(1)
    {
	delay(102);
	gateState = digitalRead(motorPPin) << 1 | digitalRead(motorNPin);
	ringer = digitalRead(ringPin);
	bus = digitalRead(busSensePin);
	if (gateState != oldGateState) {
		sprint_date();
		printf("%s Motor changed to: ",date);
		switch (gateState) {
		case ILLEGAL: printf("illegal"); break;
		case OPENING: printf("opening"); break;
		case CLOSING: printf("closing"); break;
		case IDLE:    printf("idle"); break;
		default:      printf("unknown"); break;
		}
		printf("\t+%d\n",get_delta_and_reset(&gateStateLast));
	}
	if (ringer != oldRinger) {
		sprint_date();
		if (ringer == ringActivated) printf("%s Ringer Activated\t+%d\n",date,get_delta_and_reset(&ringerLast));
		else printf("%s Ringer Deactivated\t+%d\n",date,get_delta_and_reset(&ringerLast));
	}
	if (bus != oldBus) {
		sprint_date();
		if (bus == busActivated) printf("%s Bus Activated\t+%d\n",date,get_delta_and_reset(&busLast));
		else printf("%s Bus Deactivated\t+%d\n",date,date,get_delta_and_reset(&busLast));
	} 

	// now save these variable for later
        oldRinger=ringer;
	oldBus =bus;
	oldGateState=gateState;

        delay(loopWaitTime); // Wait 200ms again

    }

    return 0;
}
