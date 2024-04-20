/*
 * gpio.c:
 *	Swiss-Army-Knife, Set-UID command-line interface to the Raspberry
 *	Pi's GPIO.
 *	Copyright (c) 2012-2017 Gordon Henderson
 ***********************************************************************
 * This file is part of wiringPi:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with wiringPi.  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <wiringPi.h>

#include "../version.h"

#define MAX_COMMAND_SIZE 1024
int port;
typedef struct {
int nr;
int state;
char name[16];
} gpin;

gpin step, dir, enable;

extern int wiringPiDebug ;

// External functions I can't be bothered creating a separate .h file for:
extern void doReadall    (int argc, char *argv []);
extern void doAllReadall (void) ;
extern void doUnexport   (int argc, char *agrv []);


int step_scale[13]={1,2,5,10,20,50,100,200,500,1000,2000,5000,10000};
int max_range=12;
int step_range=0;
int step_inc=1;
int old_dir=1;
int step_pos=0;
int ustep_count=0;
double steps_per_um=0.0104;
int usteps_per_step=32;
double delay_step;
double usteps_per_um;
double backlash_param=(double)250;

#ifndef TRUE
#  define	TRUE	(1==1)
#  define	FALSE	(1==2)
#endif

int wpMode ;

void gprint(gpin pin){
    fprintf(stderr," %+4s #%d = %d\n", pin.name, pin.nr, pin.state);
    }

void gwrite(gpin *pin, int state ){
    digitalWrite(pin->nr, state);
    pin->nr=state;
    }

void gclr(gpin *pin){
    gwrite(pin, 0);
    }    

void gset(gpin *pin){
    gwrite(pin, 1);
    }    

int gread(gpin *pin){
    pin->state=digitalRead(pin->nr);
    return pin->state;
}

void gtoggle(gpin *pin){
    gwrite(pin, !gread(pin));
    }

/*
 * doVersion:
 *	Handle the ever more complicated version command and print out
 *	some usefull information.
 *********************************************************************************
 */
static void doVersion (char *argv [])
{
	int model, rev, mem, maker, warranty ;
	struct stat statBuf ;
	char name [80] ;
	FILE *fd ;

	int vMaj;
	char *vMin[32];

	wiringPiVersion (&vMaj, vMin) ;
	printf ("gpio version: %d.%s\n", vMaj, *vMin) ;
	printf ("Copyright (c) 2012-2017 Gordon Henderson, 2017-2020 Hardkernel Co., Ltd.\n") ;
	printf ("This is free software with ABSOLUTELY NO WARRANTY.\n") ;
	printf ("For details type: %s -warranty\n", argv [0]) ;
	printf ("\n") ;
	piBoardId (&model, &rev, &mem, &maker, &warranty) ;

	printf ("ODROID Board Details:\n") ;
	printf ("  Type: %s, Revision: %s, Memory: %dMB\n" \
	        "  Maker: %s, Chip-Vendor: %s\n",
		piModelNames [model],
		piRevisionNames [rev],
		piMemorySize [mem],
		"Hardkernel",
		piMakerNames [maker]);

	// Show current kernel version
	printf("  * Current devices' kernel version: %s\n", kernelVersion->release);

	// Check for device tree
	if (stat ("/proc/device-tree", &statBuf) == 0)	// We're on a devtree system ...
		printf ("  * Device tree is enabled.\n") ;

	// Output Kernel idea of board type
	if (stat ("/proc/device-tree/model", &statBuf) == 0) {
		if ((fd = fopen ("/proc/device-tree/model", "r")) != NULL) {
			if (fgets (name, 80, fd) == NULL)
				fprintf(stderr, "Unable to read from the file descriptor: %s \n", strerror(errno));
			fclose (fd) ;
			printf ("  *--> %s\n", name) ;
		}
	}

	// User level GPIO is GO
	if (stat ("/dev/gpiomem", &statBuf) == 0)
		printf ("  * Supports user-level GPIO access.\n") ;
	else
		printf ("  * Root or sudo required for GPIO access.\n") ;
}

void handle_command(int client_socket, const char *command) {
    char response[128], action[16];
    int pin, state, nbfield; 

    nbfield=sscanf(command, "%s %s %d", action );

    if (strcasecmp(action, "IN") == 0) {
        snprintf(response,128,"Received GET command");
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "OUT") == 0) {
        // Perform action for SET command
        snprintf(response,128,"Received SET command");
        digitalWrite(pin, state);
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "REV") == 0) {
        // Perform action for SET command
        snprintf(response,128,"Received SET command");
        digitalWrite(pin, state);
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "EXIT") == 0) {
        snprintf(response,128,"Closing connection");
        send(client_socket, response, strlen(response), 0);
        close(client_socket);
    } else {
        snprintf(response, 128, "Unknown command: @%s@", command);
        send(client_socket, response, strlen(response), 0);
    }
    fprintf(stderr,"server finished processing @%s@\n", response);

    gread(&step);
    gread(&dir);
    gread(&enable);

    gprint(step);
    gprint(dir);
    gprint(enable);
}

/*
 * main:
 *	Start here
 *********************************************************************************
 */

int main (int argc, char *argv []){
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char command[MAX_COMMAND_SIZE];
    ssize_t bytes_received;

	if (argc != 5) {
        fprintf(stderr, "usage: tcp_focuser port dir-pin step-pin enable_pin\n");
        exit(-1);
    }

    port=atoi(argv[1]);
    dir.nr=atoi(argv[2]);
    snprintf(dir.name, 16, "DIR");
    step.nr=atoi(argv[3]);
    snprintf(step.name, 16, "STEP");
    enable.nr=atoi(argv[4]);
    snprintf(enable.name, 16, "ENBL");
    fprintf(stderr, "%s running on:\n port %d\n dir pin %d\n step pin %d\n enable pin %d\n", argv[0], port, dir.nr, step.nr, enable.nr);

    wiringPiSetupGpio () ;
    wpMode = MODE_GPIO ;
    delay_step=0.005/usteps_per_step;
    usteps_per_um=steps_per_um*usteps_per_step;
    backlash_param=5*(double)usteps_per_step;

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Prepare the sockaddr_in structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding");
        exit(EXIT_FAILURE);
    }

    // Listen
    listen(server_socket, 3);

    printf("Server listening on port %d %s...\n", port, command);
    // Accept incoming connections
    
    while ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len))) {
    printf("Connection accepted\n");

    // Receive and handle commands until client closes the connection
    while (1) {
        // Receive command
        bytes_received = recv(client_socket, command, MAX_COMMAND_SIZE, 0);
        if (bytes_received < 0) {
            perror("Error receiving data");
            close(client_socket);
            break;
        } else if (bytes_received == 0) {
            printf("Client closed connection\n");
            close(client_socket);
            break;
        }

        // Null-terminate the received data
        command[bytes_received] = '\0';

        // Handle command
        handle_command(client_socket, command);

        // Clear the command buffer
        memset(command, 0, MAX_COMMAND_SIZE);
    }
}

    if (client_socket < 0) {
        perror("Error accepting connection");
        exit(EXIT_FAILURE);
    }

    return 0;
}
