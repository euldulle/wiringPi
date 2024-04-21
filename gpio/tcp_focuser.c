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
#include <iniparser.h>

#include <wiringPi.h>

#include "../version.h"

#define MAX_COMMAND_SIZE 1024

#define INFOCUS 0
#define OUTFOCUS 1

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

uint32_t usteps_per_mm=470;
uint32_t usteps_per_step=32;
uint32_t delay_step;
uint32_t backlash[2]={
    200, // [0] = backlash when going from OUTFOCUS to INFOCUS
    240  // [1] = backlash when going from INFOCUS to OUTFOCUS
};
                                   
#ifndef TRUE
#  define	TRUE	(1==1)
#  define	FALSE	(1==2)
#endif

int wpMode ;

// Function to read parameters from .ini file
void read_ini_file(const char *filename, int *params) {
    // Load .ini file
    dictionary *ini = iniparser_load(filename);
    if (ini == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // Read integer parameters
    steps_per_mm = iniparser_getint(ini, "focuser:steps_per_mm", -1);
    backlash[0] = iniparser_getint(ini, "focuser:backlash0", -1);
    backlash[1] = iniparser_getint(ini, "focuser:backlash1", -1);
    delay_step = iniparser_getint(ini, "focuser:delay", -1);
    dir.nr = iniparser_getint(ini, "focuser:dir-pin", -1);
    step.nr = iniparser_getint(ini, "focuser:step-pin", -1);
    enable.nr = iniparser_getint(ini, "focuser:enable-pin", -1);
    port= iniparser_getint(ini, "focuser:port", -1);

    // Free memory allocated by iniparser
    iniparser_freedict(ini);
}

// Function to write parameters to .ini file
void write_ini_file(const char *filename, const int *params) {
    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        fprintf(stderr, "Error: cannot write to %s\n", filename);
        exit(EXIT_FAILURE);
    }
    fprintf(f, "[focuser]\n");
    fprintf(f, "steps_per_mm = %d\n", steps_per_mm);
    fprintf(f, "backlash0 = %d\n", backlash[0]);
    fprintf(f, "backlash1 = %d\n", backlash[1]);
    fprintf(f, "delay = %d\n", delay_step);
    fprintf(f, "dir-pin = %d\n", dir.nr);
    fprintf(f, "step-pin = %d\n", step.nr);
    fprintf(f, "enable-pin = %d\n", enable.nr);
    fprintf(f, "port = %d\n", port);
    fclose(f);
}

//   int main() {
//       const char *filename = "config.ini";
//       int params[6];
//
//       // Read parameters from .ini file
//       read_ini_file(filename, params);
//
//       // Print read parameters
//       printf("Parameters read from %s:\n", filename);
//       for (int i = 0; i < 6; i++) {
//           printf("Parameter %d: %d\n", i + 1, params[i]);
//       }
//
//       // Manipulate parameters as needed
//
//       // Write parameters back to .ini file (optional)
//       // Note: This will overwrite the original file
//       write_ini_file(filename, params);
//
//       printf("Parameters written back to %s.\n", filename);
//
//       return EXIT_SUCCESS;
//   }

void gprint(gpin pin){
    fprintf(stderr," %4s #%d = %d\n", pin.name, pin.nr, pin.state);
    }

void gwrite(gpin *pin, int state ){
    digitalWrite(pin->nr, state);
    pin->state=state;
    //gprint(*pin);
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

void do_move(int newdir, uint32_t microns){
    static int olddir, pos;
    int step_inc;
    int32_t count;

    gclr(&enable);

    count=(usteps_per_mm*microns)/1000;
    if (old_dir!=newdir){
        count=count+backlash[newdir];
    }
    old_dir=newdir;
    fprintf(stderr,"initial %d %d\n", count, microns);
    
    if (newdir==INFOCUS){  // infocus 
        step_inc=-1;
        gclr(&dir);
    }
    else{                   // outfocus
        step_inc=1;
        gset(&dir);
    }

    while (count>0){
        --count;
        gset(&step);
        usleep(delay_step);
        gclr(&step);
        usleep(delay_step);
        pos=pos+step_inc;
        }
    //step_pos=(step_pos+
    fprintf(stderr, "ustep pos # %d\n", pos);
    gset(&enable);
}

void handle_command(int client_socket, const char *command) {
    char response[128], action[16];
    uint32_t param;
    int pin, state, nbfield; 

    nbfield=sscanf(command, "%s %d", action, &param );

    if (strcasecmp(action, "IF") == 0) {
        snprintf(response,128,"processing %s %d...\n", action, param);
        do_move(INFOCUS,param);
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "OF") == 0) {
        snprintf(response,128,"processing %s %d...\n", action, param);
        do_move(OUTFOCUS,param);
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "Um") == 0) {
        snprintf(response,128,"usteps / mm  = %d\n", param);
        usteps_per_mm=param;
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "OI") == 0) {
        snprintf(response,128,"OI backlash = %d\n", param);
        backlash[0]=param;
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "IO") == 0) {
        snprintf(response,128,"IO backlash = %d\n", param);
        backlash[1]=param;
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "TOG") == 0) {
        snprintf(response,128,"processing %s %d...\n", action, param);
        switch(param) {
            case 0:
                gtoggle(&dir);
                break;
            case 1:
                gtoggle(&step);
                break;
            case 2:
                gtoggle(&enable);
                break;
        }
                
        send(client_socket, response, strlen(response), 0);
    } else if (strcasecmp(action, "EXIT") == 0) {
        snprintf(response,128,"Closing connection");
        send(client_socket, response, strlen(response), 0);
        close(client_socket);
    } else {
        snprintf(response, 128, "Unknown command: @%s@", command);
        send(client_socket, response, strlen(response), 0);
    }
    //fprintf(stderr,"server finished processing @%s@\n", response);

    gread(&step);
    gread(&dir);
    gread(&enable);

    gprint(step);
    gprint(dir);
    gprint(enable);
    fprintf(stderr, " backlash oi  : %d usteps\n", backlash[INFOCUS]);
    fprintf(stderr, " backlash io  : %d usteps\n", backlash[OUTFOCUS]);
    fprintf(stderr, " steps per mm : %d steps \n", usteps_per_mm);
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
    pinMode(step.nr,OUTPUT);
    pinMode(dir.nr,OUTPUT);
    pinMode(enable.nr,OUTPUT);

    delay_step=5000/usteps_per_step;
    
    //delay_step=10000;
    //backlash_param=5*(double)usteps_per_step;

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
