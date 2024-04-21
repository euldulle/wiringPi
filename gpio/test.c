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
int wpMode ;

typedef struct {
int nr;
int state;
char name[16];
} gpin;

void gprint(gpin pin){
    fprintf(stderr," %4s #%d = %d\n", pin.name, pin.nr, pin.state);
    }

void gwrite(gpin *pin, int state ){
    digitalWrite(pin->nr, state);
    pin->state=state;
    gprint(*pin);
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


gpin step, dir, enable;
uint32_t usteps_per_um=10;
uint32_t delay_step=10000;

void do_move(int newdir, uint32_t microns){
    static int old_dir;
    uint32_t count;
    gclr(&enable);

    count=usteps_per_um*microns;

    old_dir=newdir;

    if (newdir)
        gclr(&dir);
    else
        gset(&dir);

    while (count>0){
        --count;
        gset(&step);
        usleep(delay_step);
        gclr(&step);
        usleep(delay_step);
        }
    //step_pos=(step_pos+
    gset(&enable);
}


int main(int argc, char *argv[]){
    wiringPiSetupGpio ();
    wpMode = MODE_GPIO;
    step.nr=33;
    strncpy(step.name, "STEP", 4);
	pinMode (step.nr, OUTPUT) ;
    dir.nr=31;
    strncpy(dir.name, "DIR", 3);
	pinMode (dir.nr, OUTPUT) ;
    enable.nr=22;
    strncpy(enable.name, "ENA", 3);
	pinMode (enable.nr, OUTPUT) ;

    do_move(1,10);
    sleep(1);
//    do_move(0,10);
    
    fprintf(stderr,"stat %d %d\n", step.nr,step.state);
}

