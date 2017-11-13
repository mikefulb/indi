/*
    INDI Explore Scientific PMC8 driver

    Copyright (C) 2017 Michael Fulbright

    Based on IEQPro driver.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "pmc8driver.h"

#include "indicom.h"
#include "indilogger.h"
#include "inditelescope.h"

#include <libnova/julian_day.h>
#include <libnova/sidereal_time.h>

#include <math.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define PMC8_TIMEOUT 5 /* FD timeout in seconds */

#define PMC8_SIMUL_VERSION_RESP "ESGvES06B9T9"

// FIXME - (MSF) these should be read from the controller? Depends on mount type.
#define PMC8_AXIS0_SCALE 4608000.0
#define PMC8_AXIS1_SCALE 4608000.0
#define ARCSEC_IN_CIRCLE 1296000.0

// FIXME - (MSF) just placeholders need better way to represent
//         This value is from PMC8 SDK document
#define PMC8_MAX_PRECISE_MOTOR_RATE 2641

// set max settable slew rate as move rate as 256x sidereal
#define PMC8_MAX_MOVE_MOTOR_RATE (256*15)

// if tracking speed above this then mount is slewing
// NOTE - (MSF) 55 is fine since sidereal rate is 53 in these units
//              BUT if custom tracking rates are allowed in future
//              must change this limit to accomodate possibility
//              custom rate is higher than sidereal
#define PMC8_MINSLEWRATE 55

bool pmc8_debug                 = false;
bool pmc8_simulation            = false;
char pmc8_device[MAXINDIDEVICE] = "PMC8";
double pmc8_latitude            = 0;  // must be kept updated by pmc8.cpp when it is changed!
double pmc8_longitude           = 0;  // must be kept updated by pmc8.cpp when it is changed!
PMC8Info simPMC8Info;

struct
{
    double ra;
    double dec;
    PMC8_DIRECTION raDirection;
    PMC8_DIRECTION decDirection;
    double trackRate;
    double moveRate;
    double guide_rate;
} simPMC8Data;

void set_pmc8_debug(bool enable)
{
    pmc8_debug = enable;
}

void set_pmc8_simulation(bool enable)
{
    pmc8_simulation = enable;
    if (enable)
        simPMC8Data.guide_rate = 0.5;
}

void set_pmc8_device(const char *name)
{
    strncpy(pmc8_device, name, MAXINDIDEVICE);
}

void set_pmc8_location(double latitude, double longitude)
{
    pmc8_latitude = latitude;
    pmc8_longitude = longitude;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "Set PMC8 'lowlevel' lat:%f long:%f",pmc8_latitude, pmc8_longitude);

}

void set_pmc8_sim_system_status(PMC8_SYSTEM_STATUS value)
{
    simPMC8Info.systemStatus = value;

    if (value == ST_PARKED)
    {
        double lst;
        double ra;

        lst = get_local_sidereal_time(pmc8_longitude);

        ra = lst + 6;
        if (ra > 24)
            ra -= 24;

        set_pmc8_sim_ra(ra);
        set_pmc8_sim_dec(90.0);

    }
}

void set_pmc8_sim_track_rate(PMC8_TRACK_RATE value)
{
    simPMC8Data.trackRate = value;
}

void set_pmc8_sim_move_rate(PMC8_MOVE_RATE value)
{
    simPMC8Data.moveRate = value;
}

#if 0
void set_sim_hemisphere(IEQ_HEMISPHERE value)
{
    simPMC8Info.hemisphere = value;
}
#endif // prob not needed pmc8

void set_pmc8_sim_ra(double ra)
{
    simPMC8Data.ra = ra;
}

void set_pmc8_sim_dec(double dec)
{
    simPMC8Data.dec = dec;
}

//void set_pmc8_sim_guide_rate(double rate)
//{
//    simPMC8Data.guide_rate = rate;
//}

bool check_pmc8_connection(int fd)
{
    char initCMD[] = "ESGv!";
    int errcode    = 0;
    char errmsg[MAXRBUF];
    char response[16];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "Initializing PMC8 using ESGv! CMD...");

    for (int i = 0; i < 2; i++)
    {
        if (pmc8_simulation)
        {
            strcpy(response, PMC8_SIMUL_VERSION_RESP);
            nbytes_read = strlen(response);
        }
        else
        {
            tcflush(fd, TCIFLUSH);

            if ((errcode = tty_write(fd, initCMD, strlen(initCMD), &nbytes_written)) != TTY_OK)
            {
                tty_error_msg(errcode, errmsg, MAXRBUF);
                DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
                usleep(50000);
                continue;
            }

            if ((errcode = tty_read_section(fd, response, '!', PMC8_TIMEOUT, &nbytes_read)))
            {
                tty_error_msg(errcode, errmsg, MAXRBUF);
                DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
                usleep(50000);
                continue;
            }
        }

        if (nbytes_read > 0)
        {
            response[nbytes_read] = '\0';
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

            // FIXME - (MSF) need to put in better check for a valid firmware version response
            if (!strncmp(response, "ESGvES", 6))
                return true;
        }

        usleep(50000);
    }

    return false;
}

// really dont think we need this since PMC8 doesnt give status
#if 0
bool get_pmc8_status(int fd, PMC8Info *info)
{
    char cmd[]  = ":GAS#";
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

#if 1
    DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_EXTRA_1, "get_pmc8_status() not implemented!");
    return false;

#else
    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_EXTRA_1, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        // FIXME - (MSF) Need to implement simcode for get status
//        snprintf(response, 8, "%d%d%d%d%d%d#", simPMC8Info.gpsStatus, simPMC8Info.systemStatus, simPMC8Info.trackRate,
//                 simPMC8Info.slewRate + 1, simPMC8Info.timeSource + 1, simPMC8Info.hemisphere);
//        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read_section(fd, response, '#', PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_EXTRA_1, "RES (%s)", response);

#if 0
        // FIXME - (MSF) need to implement pmc8 get status command
        if (nbytes_read == 7)
        {
            info->gpsStatus    = (IEQ_GPS_STATUS)(response[0] - '0');
            info->systemStatus = (IEQ_SYSTEM_STATUS)(response[1] - '0');
            info->trackRate    = (IEQ_TRACK_RATE)(response[2] - '0');
            info->slewRate     = (IEQ_SLEW_RATE)(response[3] - '0' - 1);
            info->timeSource   = (IEQ_TIME_SOURCE)(response[4] - '0' - 1);
            info->hemisphere   = (IEQ_HEMISPHERE)(response[5] - '0');

            tcflush(fd, TCIFLUSH);

            return true;
        }
#endif
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 7.", nbytes_read);
    return false;
#endif
}
#endif

bool get_pmc8_model(int fd, FirmwareInfo *info)
{
    INDI_UNUSED(fd);

    // FIXME - only one model for now
    info->Model.assign("PMC-Eight");
    return true;
}

bool get_pmc8_main_firmware(int fd, FirmwareInfo *info)
{
    char cmd[]  = "ESGv!";
    char board[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[24];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, PMC8_SIMUL_VERSION_RESP);
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read_section(fd, response, '#', PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        if (nbytes_read == 13)
        {
            response[12] = '\0';
            strncpy(board, response+6, 6);

            info->MainBoardFirmware.assign(board, 6);

            tcflush(fd, TCIFLUSH);

            return true;
        }
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 13.", nbytes_read);
    return false;
}


bool get_pmc8_firmware(int fd, FirmwareInfo *info)
{
    bool rc = false;

    rc = get_pmc8_model(fd, info);

    if (rc == false)
        return rc;

    rc = get_pmc8_main_firmware(fd, info);

    return rc;

}

bool get_pmc8_tracking_rate_axis(int fd, PMC8_AXIS axis, int &rate)
{

    char cmd[32];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[16];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    snprintf(cmd, sizeof(cmd), "ESGr%d!", axis);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        if (axis == PMC8_RA_AXIS)
            rate = simPMC8Data.trackRate;
        else if (axis == PMC8_DEC_AXIS)
            rate = 0; // DEC tracking not supported yet
        else
            return false;

        return true;
    }


    tcflush(fd, TCIFLUSH);

    if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    if ((errcode = tty_read(fd, response, 10, PMC8_TIMEOUT, &nbytes_read)))
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    response[nbytes_read] = '\0';

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    if (nbytes_read != 10)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Axis get track rate cmd response incorrect");
        return false;
    }

    char num_str[16]= {0};

    strcpy(num_str, "0X");
    strncat(num_str, response+5, 6);

    //point = atoi(num_str);
    rate = (int)strtol(num_str, NULL, 0);

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "get rates num_str = %s atoi() returns %d", num_str, rate);

    return true;
}

bool get_pmc8_direction_axis(int fd, PMC8_AXIS axis, int &dir)
{
    char cmd[32];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[16];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    snprintf(cmd, sizeof(cmd), "ESGd%d!", axis);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        if (axis == PMC8_RA_AXIS)
            dir = simPMC8Data.raDirection;
        else if (axis == PMC8_DEC_AXIS)
            dir = simPMC8Data.decDirection;
        else
            return false;

        return true;
    }

    tcflush(fd, TCIFLUSH);

    if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    if ((errcode = tty_read(fd, response, 7, PMC8_TIMEOUT, &nbytes_read)))
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    response[nbytes_read] = '\0';

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    if (nbytes_read != 7)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Axis get dir cmd response incorrect");
        return false;
    }

    char num_str[16]= {0};

    strncat(num_str, response+5, 2);

    dir = (int)strtol(num_str, NULL, 0);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "get dir num_str = %s atoi() returns %d", num_str, dir);

    return true;
}

bool set_pmc8_direction_axis(int fd, PMC8_AXIS axis, int dir)
{

    char cmd[32];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[16];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    snprintf(cmd, sizeof(cmd), "ESSd%d%d!", axis, dir);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        if (axis == PMC8_RA_AXIS)
            simPMC8Data.raDirection = (PMC8_DIRECTION) dir;
        else if (axis == PMC8_DEC_AXIS)
            simPMC8Data.decDirection = (PMC8_DIRECTION) dir;
        else
            return false;

        return true;
    }

    tcflush(fd, TCIFLUSH);

    if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    if ((errcode = tty_read(fd, response, 7, PMC8_TIMEOUT, &nbytes_read)))
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    response[nbytes_read] = '\0';

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    if (nbytes_read != 7)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Axis get dir cmd response incorrect");
        return false;
    }

    return true;
}

bool get_pmc8_is_scope_slewing(int fd, bool &isslew)
{
    int rarate;
    int decrate;
    bool rc;

    rc=get_pmc8_tracking_rate_axis(fd, PMC8_RA_AXIS, rarate);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "get_pmc8_is_scope_slewing(): Error reading RA tracking rate");
        return false;
    }

    rc=get_pmc8_tracking_rate_axis(fd, PMC8_DEC_AXIS, decrate);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "get_pmc8_is_scope_slewing(): Error reading DEC tracking rate");
        return false;
    }

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "get_pmc8_is_scope_slewing(): rarate=%d decreate=%d", rarate, decrate);


    if (pmc8_simulation)
    {
        isslew = (simPMC8Info.systemStatus == ST_SLEWING);
        return true;
    }
    else
    {
        isslew = ((rarate > PMC8_MINSLEWRATE) || (decrate > PMC8_MINSLEWRATE));
    }

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "get_pmc8_is_scope_slewing(): isslew=%d", isslew);

    return true;
}

int convert_movespeedindex_to_rate(int mode)
{
    int r=0;

    switch (mode)
    {
        case 0:
            r = 4*15;
            break;
        case 1:
            r = 16*15;
            break;
        case 2:
            r = 64*15;
            break;
        case 3:
            r = 256*15;
            break;
        default:
            r = 0;
            break;
    }

    return r;
}

bool start_pmc8_motion(int fd, PMC8_DIRECTION dir, int mode)
{
    bool isslew;

    // check speed
    if (get_pmc8_is_scope_slewing(fd, isslew) == false)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "start_pmc8_motion(): Error reading slew state");
        return false;
    }

    if (isslew)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "start_pmc8_motion(): cannot start motion during slew!");
        return false;
    }

    int rarate = 0;
    int decrate = 0;
    int reqrate = 0;

    reqrate = convert_movespeedindex_to_rate(mode);

    if (reqrate > PMC8_MAX_MOVE_MOTOR_RATE)
        reqrate = PMC8_MAX_MOVE_MOTOR_RATE;
    else if (reqrate < -PMC8_MAX_MOVE_MOTOR_RATE)
        reqrate = -PMC8_MAX_MOVE_MOTOR_RATE;

    switch (dir)
    {
        case PMC8_N:
            decrate = reqrate;
            break;
        case PMC8_S:
            decrate = -reqrate;
            break;
        case PMC8_W:
            rarate = reqrate;  // doesn't accord for sidereal motion
            break;
        case PMC8_E:
            rarate = -reqrate;  // doesn't accord for sidereal motion
            break;
    }

    if (rarate != 0)
        set_pmc8_custom_ra_move_rate(fd, rarate);
    if (decrate != 0)
        set_pmc8_custom_dec_move_rate(fd, decrate);

    return true;


#if 0
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    int nbytes_written = 0;

    switch (dir)
    {
        case PMC8_N:
            strcpy(cmd, ":mn#");
            break;
        case PMC8_S:
            strcpy(cmd, ":ms#");
            break;
        case PMC8_W:
            strcpy(cmd, ":mw#");
            break;
        case PMC8_E:
            strcpy(cmd, ":me#");
            break;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
        return true;

    tcflush(fd, TCIFLUSH);

    if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    tcflush(fd, TCIFLUSH);
    return true;
#else
    // FIXME - (MSF) implement start_pmc8_motion
    return false;
#endif
}

bool stop_pmc8_tracking_motion(int fd)
{
    bool rc;

    // stop tracking
    rc = set_pmc8_custom_ra_track_rate(fd, 0);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error stopping RA axis!");
        return false;
    }

    return true;
}

bool stop_pmc8_motion(int fd, PMC8_DIRECTION dir)
{
    bool rc;

    // FIXME - (MSF) this should restart tracking in right direction based on state before start_pmc8_motion() was called!!
    switch (dir)
    {
        case PMC8_N:
        case PMC8_S:
            rc = set_pmc8_custom_dec_move_rate(fd, 0);
            break;

        case PMC8_W:
        case PMC8_E:
            rc = set_pmc8_custom_ra_move_rate(fd, 0);
            break;
    }

    return rc;
}

#if 0
// PMC8 slew rate is 25x the tracking rate - no need to set
bool set_pmc8_slew_rate(int fd, IEQ_SLEW_RATE rate)
{
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    snprintf(cmd, 16, ":SR%d#", ((int)rate) + 1);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        simPMC8Info.slewRate = rate;
        strcpy(response, "1");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, 1, PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}
#endif


// convert mount count to 6 character two complement hex string
void convert_motor_counts_to_hex(int val, char *hex)
{
    unsigned tmp;
    char h[16];

    if (val < 0)
    {
        tmp=abs(val);
        tmp=~tmp;
        tmp++;
    }
    else
    {
        tmp=val;
    }

    sprintf(h, "%08X", tmp);

    strcpy(hex, h+2);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "convert_motor_counts_to_hex val=%d, h=%s, hex=%s", val, h, hex);
}

// convert rate in arcsec/sidereal_second to internal PMC8 motor rate for RA axis ONLY
bool convert_precise_rate_to_motor(float rate, int *mrate)
{

    *mrate = (int)(25*rate*(PMC8_AXIS0_SCALE/ARCSEC_IN_CIRCLE));

    if (*mrate > PMC8_MAX_PRECISE_MOTOR_RATE)
        *mrate = PMC8_MAX_PRECISE_MOTOR_RATE;
    else if (*mrate < -PMC8_MAX_PRECISE_MOTOR_RATE)
        *mrate = -PMC8_MAX_PRECISE_MOTOR_RATE;

    return true;
}

// convert rate in arcsec/sidereal_second to internal PMC8 motor rate for move action (not slewing)
bool convert_move_rate_to_motor(float rate, int *mrate)
{

    *mrate = (int)(rate*(PMC8_AXIS0_SCALE/ARCSEC_IN_CIRCLE));

    if (*mrate > PMC8_MAX_MOVE_MOTOR_RATE)
        *mrate = PMC8_MAX_MOVE_MOTOR_RATE;
    else if (*mrate < -PMC8_MAX_MOVE_MOTOR_RATE)
        *mrate = -PMC8_MAX_MOVE_MOTOR_RATE;

    return true;
}

// set speed for move action (MoveNS/MoveWE) NOT slews!
bool set_pmc8_axis_move_rate(int fd, PMC8_AXIS axis, float rate)
{
    char cmd[24];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[24];
    int nbytes_read    = 0;
    int nbytes_written = 0;
    int rateval;
    bool rc;

    // set direction
    if (rate < 0)
        rc=set_pmc8_direction_axis(fd, axis, 0);
    else
        rc=set_pmc8_direction_axis(fd, axis, 1);

    if (!rc)
        return rc;

    if (!convert_move_rate_to_motor(fabs(rate), &rateval))
    {
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error converting rate %f", rate);
        return false;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "PMC8 internal rate %d for requested rate %f", rateval, rate);

    snprintf(cmd, sizeof(cmd), "ESSr%d%04X!", axis, rateval);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        simPMC8Data.moveRate = rate;
        return true;
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, strlen(cmd), PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read == 10)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 10.", nbytes_read);
    return false;
}


#if 0
bool set_pmc8_track_enabled(int fd, bool enabled)
{
    char cmd[32];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    snprintf(cmd, 32, ":ST%d#", enabled ? 1 : 0);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        // FIXME - (MSF) - need to implement pmc8 track enabled sim
//        simPMC8Info.systemStatus = enabled ? ST_TRACKING_PEC_ON : ST_STOPPED;
//        strcpy(response, "1");
//        nbytes_read = strlen(response);

        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Need to implement pmc8 track enabled sim");
        return false;
    }
    else
    {
        // determine current tracking mode

//        return SetTrackMode(enabled ? IUFindOnSwitchIndex(&TrackModeSP) : AP_TRACKING_OFF);
    }
}
#endif

bool set_pmc8_track_mode(int fd, uint rate)
{
    float ratereal;

    switch (rate)
    {
        case PMC8_TRACK_SIDEREAL:
            ratereal = 15.0;
            break;
        case PMC8_TRACK_LUNAR:
            ratereal = 14.453;
            break;
        case PMC8_TRACK_SOLAR:
            ratereal = 15.041;
            break;
    }

    return set_pmc8_custom_ra_track_rate(fd, ratereal);
}

bool set_pmc8_custom_ra_track_rate(int fd, double rate)
{
    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "set_pmc8_custom_ra_track_rate() called rate=%f ", rate);

    char cmd[24];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[24];
    int nbytes_read    = 0;
    int nbytes_written = 0;
    int rateval;

    if (!convert_precise_rate_to_motor(rate, &rateval))
    {
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error converting rate %f", rate);
        return false;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "PMC8 internal precise rate %d for requested rate %f", rateval, rate);

    snprintf(cmd, sizeof(cmd), "ESTr%04X!", rateval);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        simPMC8Data.trackRate = rate;
        return true;
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, strlen(cmd), PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read != 9)
    {
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 9.", nbytes_read);
        return false;
    }

    response[nbytes_read] = '\0';
    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    tcflush(fd, TCIFLUSH);

    // set direction to 1
    return set_pmc8_direction_axis(fd, PMC8_RA_AXIS, 1);
}


#if 0
bool set_pmc8_custom_dec_track_rate(int fd, double rate)
{
    bool rc;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "set_pmc8_custom_dec_track_rate() called rate=%f ", rate);


    if (pmc8_simulation)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "set_pmc8_custom_dec_track_rate simulation not implemented");

        rc=false;
    }
    else
    {
        rc=set_pmc8_axis_rate(fd, PMC8_DEC_AXIS, rate);
    }

    return rc;
}
#else
bool set_pmc8_custom_dec_track_rate(int fd, double rate)
{
    INDI_UNUSED(fd);
    INDI_UNUSED(rate);

    DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "set_pmc8_custom_dec_track_rate not implemented!");
    return false;
}
#endif

bool set_pmc8_custom_ra_move_rate(int fd, double rate)
{
    bool rc;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "set_pmc8_custom_ra move_rate() called rate=%f ", rate);

    // (MSF) safe guard for now - only all use to STOP slewing or MOVE commands with this
    if (fabs(rate) > PMC8_MAX_MOVE_MOTOR_RATE)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "set_pmc8_custom_ra_move rate only supports low rates currently");

        return false;
    }

    rc=set_pmc8_axis_move_rate(fd, PMC8_RA_AXIS, rate);

    return rc;
}


bool set_pmc8_custom_dec_move_rate(int fd, double rate)
{
    bool rc;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "set_pmc8_custom_dec_move_rate() called rate=%f ", rate);

    // (MSF) safe guard for now - only all use to STOP slewing with this
    if (fabs(rate) > PMC8_MAX_MOVE_MOTOR_RATE)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "set_pmc8_custom_dec_move_rate only supports low rates currently");
        return false;
    }

    rc=set_pmc8_axis_move_rate(fd, PMC8_DEC_AXIS, rate);

    return rc;
}

bool set_pmc8_guide_rate(int fd, double rate)
{
    INDI_UNUSED(fd);
    INDI_UNUSED(rate);

    DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "set_pmc8_guide_rate not implemented!");
    return false;
}

// not yet implemented for PMC8
bool get_pmc8_guide_rate(int fd, double *rate)
{
    INDI_UNUSED(fd);
    INDI_UNUSED(rate);

    DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "get_pmc8_guide_rate not implemented!");
    return false;
}

#if 0
// not yet implemented for PMC8
bool start_ieqpro_guide(int fd, IEQ_DIRECTION dir, int ms)
{
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    int nbytes_written = 0;

    char dir_c = 0;

    switch (dir)
    {
        case IEQ_N:
            dir_c = 'n';
            break;

        case IEQ_S:
            dir_c = 's';
            break;

        case IEQ_W:
            dir_c = 'w';
            break;

        case IEQ_E:
            dir_c = 'e';
            break;
    }

    snprintf(cmd, 16, ":M%c%05d#", dir_c, ms);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
        return true;
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    tcflush(fd, TCIFLUSH);
    return true;
}
#endif


// convert from axis position returned by controller to motor counts used in conversion to RA/DEC
int convert_axispos_to_motor(int axispos)
{
    int r;

    if (axispos > 8388608)
        r = 0 - (16777216 - axispos);
    else
        r = axispos;

    return r;
}

bool convert_ra_to_motor(double ra, INDI::Telescope::TelescopePierSide sop, int *mcounts)
{
    double motor_angle;
    double hour_angle;
    double lst;

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "convert_ra_to_motor - ra=%f sop=%d", ra, sop);

    lst = get_local_sidereal_time(pmc8_longitude);

    hour_angle = lst- ra;

    // limit values to +/- 12 hours
    if (hour_angle > 12)
        hour_angle = hour_angle - 24;
    else if (hour_angle <= -12)
        hour_angle = hour_angle + 24;

    if (sop == INDI::Telescope::PIER_EAST)
        motor_angle = hour_angle - 6;
    else if (sop == INDI::Telescope::PIER_WEST)
        motor_angle = hour_angle + 6;
    else
        return false;

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "convert_ra_to_motor - lst = %f hour_angle=%f", lst, hour_angle);

    *mcounts = motor_angle * PMC8_AXIS0_SCALE / 24;

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "convert_ra_to_motor - motor_angle=%f *mcounts=%d", motor_angle, *mcounts);


    return true;
}

bool convert_motor_to_radec(int racounts, int deccounts, double &ra_value, double &dec_value)
{
    double motor_angle;
    double hour_angle;
    //double sid_time;

    double lst;

    lst = get_local_sidereal_time(pmc8_longitude);

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "lst = %f", lst);

    motor_angle = (24.0 * racounts) / PMC8_AXIS0_SCALE;

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "racounts = %d  motor_angle = %f", racounts, motor_angle);

    if (deccounts < 0)
        hour_angle = motor_angle + 6;
    else
        hour_angle = motor_angle - 6;

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "hour_angle = %f", hour_angle);

    ra_value = lst - hour_angle;

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "ra_value  = %f", ra_value);

    if (ra_value >= 24.0)
        ra_value = ra_value - 24.0;
    else if (ra_value < 0.0)
         ra_value = ra_value + 24.0;

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "ra_value (final) = %f", ra_value);

    motor_angle = (360.0 * deccounts) / PMC8_AXIS1_SCALE;

    if (motor_angle >= 0)
        dec_value = 90 - motor_angle;
    else
        dec_value = 90 + motor_angle;

    return true;
}

bool convert_dec_to_motor(double dec, INDI::Telescope::TelescopePierSide sop, int *mcounts)
{
    double motor_angle;

    if (sop == INDI::Telescope::PIER_EAST)
        motor_angle = (dec - 90.0);
    else if (sop == INDI::Telescope::PIER_WEST)
        motor_angle = -(dec - 90.0);
    else
        return false;

     *mcounts = (motor_angle / 360.0) * PMC8_AXIS1_SCALE;

//     DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "convert_dec_to_motor dec = %f, sop = %d", dec, sop);
//     DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "convert_dec_to_motor motor_angle = %f, motor_counts= %d", motor_angle, *mcounts);

     return true;
}

bool set_pmc8_target_position_axis(int fd, PMC8_AXIS axis, int point)
{

    char cmd[32];
    char expresp[32];
    char hexpt[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[16];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    convert_motor_counts_to_hex(point, hexpt);
    snprintf(cmd, sizeof(cmd), "ESPt%d%s!", axis, hexpt);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
        return true;

    tcflush(fd, TCIFLUSH);

    if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    if ((errcode = tty_read(fd, response, strlen(cmd), PMC8_TIMEOUT, &nbytes_read)))
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    response[nbytes_read] = '\0';

    if (nbytes_read > 0)
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    // compare to expected response
    snprintf(expresp, sizeof(expresp), "ESGt%d%s!", axis, hexpt);

    if (strncmp(response, expresp, strlen(response)))
    {
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Axis Set Point cmd response incorrect: %s - expected %s", response, expresp);
        return false;
    }

    return true;
}


bool set_pmc8_target_position(int fd, int rapoint, int decpoint)
{
    bool rc;

    rc = set_pmc8_target_position_axis(fd, PMC8_RA_AXIS, rapoint);

    if (!rc)
        return rc;

    rc = set_pmc8_target_position_axis(fd, PMC8_DEC_AXIS, decpoint);

    return rc;
}


bool set_pmc8_position_axis(int fd, PMC8_AXIS axis, int point)
{

    char cmd[32];
    char expresp[32];
    char hexpt[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[16];
    int nbytes_read    = 0;
    int nbytes_written = 0;


    if (pmc8_simulation)
    {
        // FIXME - (MSF) - need to implement simulation code for setting point position
        return true;
    }

    convert_motor_counts_to_hex(point, hexpt);
    snprintf(cmd, sizeof(cmd), "ESSp%d%s!", axis, hexpt);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    tcflush(fd, TCIFLUSH);

    if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    if ((errcode = tty_read(fd, response, strlen(cmd), PMC8_TIMEOUT, &nbytes_read)))
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    response[nbytes_read] = '\0';

    if (nbytes_read > 0)
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    // compare to expected response
    snprintf(expresp, sizeof(expresp), "ESGp%d%s!", axis, hexpt);

    if (strncmp(response, expresp, strlen(response)))
    {
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Axis Set Point cmd response incorrect: %s - expected %s", response, expresp);
        return false;
    }

    return true;
}


bool set_pmc8_position(int fd, int rapoint, int decpoint)
{
    bool rc;

    rc = set_pmc8_position_axis(fd, PMC8_RA_AXIS, rapoint);

    if (!rc)
        return rc;

    rc = set_pmc8_position_axis(fd, PMC8_DEC_AXIS, decpoint);

    return rc;
}


bool get_pmc8_position_axis(int fd, PMC8_AXIS axis, int &point)
{

    char cmd[32];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[16];
    int nbytes_read    = 0;
    int nbytes_written = 0;


    if (pmc8_simulation)
    {
        // FIXME - (MSF) - need to implement simulation code for setting point position
        return true;
    }


    snprintf(cmd, sizeof(cmd), "ESGp%d!", axis);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    tcflush(fd, TCIFLUSH);

    if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    if ((errcode = tty_read(fd, response, 12, PMC8_TIMEOUT, &nbytes_read)))
    {
        tty_error_msg(errcode, errmsg, MAXRBUF);
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
        return false;
    }

    response[nbytes_read] = '\0';

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

    if (nbytes_read != 12)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Axis Set Point cmd response incorrect");
        return false;
    }

    char num_str[16]= {0};

    strcpy(num_str, "0X");
    strncat(num_str, response+5, 6);

    //point = atoi(num_str);
    point = (int)strtol(num_str, NULL, 0);

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "get pos num_str = %s atoi() returns %d", num_str, point);

    return true;
}


bool get_pmc8_position(int fd, int &rapoint, int &decpoint)
{
    bool rc;
    int axis_ra_pos, axis_dec_pos;

    rc = get_pmc8_position_axis(fd, PMC8_RA_AXIS, axis_ra_pos);

    if (!rc)
        return rc;

    rc = get_pmc8_position_axis(fd, PMC8_DEC_AXIS, axis_dec_pos);

    if (!rc)
        return rc;

    // convert from axis position to motor counts
    rapoint = convert_axispos_to_motor(axis_ra_pos);
    decpoint = convert_axispos_to_motor(axis_dec_pos);

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "ra  axis pos = 0x%x  motor_counts=%d",  axis_ra_pos,  rapoint);
//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "dec axis pos = 0x%x  motor_counts=%d", axis_dec_pos, decpoint);

    return rc;
}


bool park_pmc8(int fd)
{

    bool rc;

    rc = set_pmc8_target_position(fd, 0, 0);

    // FIXME - (MSF) Need to add code to handle simulation and also setting any scope state values

    return rc;
}


bool unpark_pmc8(int fd)
{
    INDI_UNUSED(fd);

    // nothing really to do for PMC8 there is no unpark command

    if (pmc8_simulation)
    {
        set_pmc8_sim_system_status(ST_STOPPED);
        return true;
    }


    // FIXME - (MSF) probably need to set a state variable to show we're unparked
    DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "PMC8 unparked");

    return true;
}

bool abort_pmc8(int fd)
{
    bool rc;


    if (pmc8_simulation)
    {
        // FIXME - (MSF) need to do something to represent mount has stopped slewing
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "PMC8 slew stopped in simulation - need to add more code?");
        return true;
    }

    // stop move/slew rates
    rc = set_pmc8_custom_ra_move_rate(fd, 0);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error stopping RA axis!");
        return false;
    }

    rc = set_pmc8_custom_dec_move_rate(fd, 0);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error stopping DEC axis!");
        return false;
    }

    return true;
}

// "slew" on PMC8 is instantaneous once you set the target ra/dec
// no concept of setting target and then starting a slew operation as two steps
bool slew_pmc8(int fd, double ra, double dec)
{
    bool rc;
    int racounts, deccounts;
    INDI::Telescope::TelescopePierSide sop;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "slew_pmc8: ra=%f  dec=%f", ra, dec);

    sop = destSideOfPier(ra, dec);

    rc = convert_ra_to_motor(ra, sop, &racounts);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "slew_pmc8: error convering RA to motor counts");
        return false;
    }

    rc = convert_dec_to_motor(dec, sop, &deccounts);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "slew_pmc8: error convering DEC to motor counts");
        return false;
    }

    rc = set_pmc8_target_position(fd, racounts, deccounts);

    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error slewing PMC8");
        return false;
    }

    if (pmc8_simulation)
    {
        set_pmc8_sim_system_status(ST_SLEWING);
    }

    return true;
}

INDI::Telescope::TelescopePierSide destSideOfPier(double ra, double dec)
{
    double hour_angle;
    double lst;

    INDI_UNUSED(dec);

    lst = get_local_sidereal_time(pmc8_longitude);

    hour_angle = lst - ra;

    // limit values to +/- 12 hours
    if (hour_angle > 12)
        hour_angle = hour_angle - 24;
    else if (hour_angle <= -12)
        hour_angle = hour_angle + 24;

    if (hour_angle < 0.0)
        return INDI::Telescope::PIER_WEST;
    else
        return INDI::Telescope::PIER_EAST;
}

bool sync_pmc8(int fd, double ra, double dec)
{
    bool rc;
    int racounts, deccounts;
    INDI::Telescope::TelescopePierSide sop;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "sync_pmc8: ra=%f  dec=%f", ra, dec);

    sop = destSideOfPier(ra, dec);

    rc = convert_ra_to_motor(ra, sop, &racounts);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "sync_pmc8: error convering RA to motor counts");
        return false;
    }

    rc = convert_dec_to_motor(dec, sop, &deccounts);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "sync_pmc8: error convering DEC to motor counts");
        return false;
    }

    if (pmc8_simulation)
    {
        // FIXME - (MSF) need to implement pmc8 sync sim
//        strcpy(response, "1");
//        nbytes_read = strlen(response);
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Need to implement PMC8 sync simulation");
        return false;
    }
    else
    {
        rc = set_pmc8_position(fd, racounts, deccounts);
    }

    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error setting pmc8 position");
        return false;
    }

    return true;
}

bool set_pmc8_radec(int fd, double ra, double dec)
{
    bool rc;
    int racounts, deccounts;
    INDI::Telescope::TelescopePierSide sop;


    sop = destSideOfPier(ra, dec);

    rc = convert_ra_to_motor(ra, sop, &racounts);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "set_pmc8_radec: error convering RA to motor counts");
        return false;
    }

    rc = convert_dec_to_motor(ra, sop, &deccounts);
    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "set_pmc8_radec: error convering DEC to motor counts");
        return false;
    }

    if (pmc8_simulation)
    {
        // FIXME - (MSF) need to implement pmc8 sync sim
//        strcpy(response, "1");
//        nbytes_read = strlen(response);
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Need to implement PMC8 sync simulation");
        return false;
    }
    else
    {

        rc = set_pmc8_target_position(fd, racounts, deccounts);
    }

    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error setting target positoin");
        return false;
    }

    return true;
}

#if 0
// convert as required
bool set_ieqpro_longitude(int fd, double longitude)
{
    char cmd[16];
    char sign;
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    if (longitude >= 0)
        sign = '+';
    else
        sign = '-';

    int longitude_arcsecs = fabs(longitude) * 60 * 60;
    snprintf(cmd, 16, ":Sg%c%06d#", sign, longitude_arcsecs);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "1");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, 1, PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}

bool set_ieqpro_latitude(int fd, double latitude)
{
    char cmd[16];
    char sign;
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    if (latitude >= 0)
        sign = '+';
    else
        sign = '-';

    int latitude_arcsecs = fabs(latitude) * 60 * 60;
    snprintf(cmd, 16, ":St%c%06d#", sign, latitude_arcsecs);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "1");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, 1, PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}

bool get_ieqpro_longitude(int fd, double *longitude)
{
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    strcpy(cmd, ":Gg#");

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "+172800");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
        if ((errcode = tty_read_section(fd, response, '#', PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);

        int longitude_arcsecs = 0;

        if (sscanf(response, "%d#", &longitude_arcsecs) > 0)
        {
            *longitude = longitude_arcsecs / 3600.0;
            return true;
        }

        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error: Malformed result (%s).", response);
        return false;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 8.", nbytes_read);
    return false;
}

bool get_ieqpro_latitude(int fd, double *latitude)
{
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    strcpy(cmd, ":Gt#");

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "+106200");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
        if ((errcode = tty_read_section(fd, response, '#', PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);

        int latitude_arcsecs = 0;

        if (sscanf(response, "%d#", &latitude_arcsecs) > 0)
        {
            *latitude = latitude_arcsecs / 3600.0;
            return true;
        }

        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Error: Malformed result (%s).", response);
        return false;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 8.", nbytes_read);
    return false;
}

bool set_ieqpro_local_date(int fd, int yy, int mm, int dd)
{
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    snprintf(cmd, 16, ":SC%02d%02d%02d#", yy, mm, dd);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "1");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, 1, PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}

bool set_ieqpro_local_time(int fd, int hh, int mm, int ss)
{
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    snprintf(cmd, 16, ":SL%02d%02d%02d#", hh, mm, ss);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "1");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, 1, PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}

bool set_ieqpro_daylight_saving(int fd, bool enabled)
{
    char cmd[16];
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    if (enabled)
        strcpy(cmd, ":SDS1#");
    else
        strcpy(cmd, ":SDS0#");

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "1");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, 1, PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}

bool set_ieqpro_utc_offset(int fd, double offset)
{
    char cmd[16];
    char sign;
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[8];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    if (offset >= 0)
        sign = '+';
    else
        sign = '-';

    int offset_minutes = fabs(offset) * 60.0;

    snprintf(cmd, 16, ":SG%c%03d#", sign, offset_minutes);

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    if (pmc8_simulation)
    {
        strcpy(response, "1");
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read(fd, response, 1, PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        tcflush(fd, TCIFLUSH);
        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}
#endif

bool get_pmc8_coords(int fd, double &ra, double &dec)
{
    int racounts, deccounts;
    bool rc;

    if (pmc8_simulation)
    {
        // sortof silly but convert simulated RA/DEC to counts so we can then convert
        // back to RA/DEC to test that conversion code
        INDI::Telescope::TelescopePierSide sop;

        sop = destSideOfPier(simPMC8Data.ra, simPMC8Data.dec);

        rc = convert_ra_to_motor(simPMC8Data.ra, sop, &racounts);

        if (!rc)
            return rc;

        rc = convert_dec_to_motor(simPMC8Data.dec, sop, &deccounts);

        if (!rc)
            return rc;
    }
    else
    {
        rc = get_pmc8_position(fd, racounts, deccounts);
    }

    if (!rc)
    {
        DEBUGDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "Error getting PMC8 motor position");
        return false;
    }

    // convert motor counts to ra/dec
    convert_motor_to_radec(racounts, deccounts, ra, dec);

//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "ra  motor_counts=%d  RA  = %f", racounts, ra);
//    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "dec motor_counts=%d  DEC = %f", deccounts, dec);

    return rc;
}

#if 0
bool get_ieqpro_utc_date_time(int fd, double *utc_hours, int *yy, int *mm, int *dd, int *hh, int *minute, int *ss)
{
    char cmd[]  = ":GLT#";
    int errcode = 0;
    char errmsg[MAXRBUF];
    char response[32];
    int nbytes_read    = 0;
    int nbytes_written = 0;

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "CMD (%s)", cmd);

    // Format according to Manual is sMMMYYMMDDHHMMSS#
    // However as pointed out by user Shepherd on INDI forums, actual format is
    // sMMMxYYMMDDHHMMSS#
    // Where x is either 0 or 1 denoting daying savings
    if (pmc8_simulation)
    {
        strncpy(response, "+1800150331173000#", 32);
        nbytes_read = strlen(response);
    }
    else
    {
        tcflush(fd, TCIFLUSH);

        if ((errcode = tty_write(fd, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }

        if ((errcode = tty_read_section(fd, response, '#', PMC8_TIMEOUT, &nbytes_read)))
        {
            tty_error_msg(errcode, errmsg, MAXRBUF);
            DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "%s", errmsg);
            return false;
        }
    }

    if (nbytes_read > 0)
    {
        tcflush(fd, TCIFLUSH);
        response[nbytes_read] = '\0';
        DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_DEBUG, "RES (%s)", response);

        char utc_str[8]={0}, yy_str[8]={0}, mm_str[8]={0}, dd_str[8]={0}, hh_str[8]={0}, minute_str[8]={0}, ss_str[8]={0}, dst_str[8]={0};

        // UTC Offset
        strncpy(utc_str, response, 4);
        // Daylight savings
        strncpy(dst_str, response + 4, 1);
        // Year
        strncpy(yy_str, response + 5, 2);
        // Month
        strncpy(mm_str, response + 7, 2);
        // Day
        strncpy(dd_str, response + 9, 2);
        // Hour
        strncpy(hh_str, response + 11, 2);
        // Minute
        strncpy(minute_str, response + 13, 2);
        // Second
        strncpy(ss_str, response + 15, 2);

        *utc_hours = atoi(utc_str) / 60.0;
        *yy        = atoi(yy_str) + 2000;
        *mm        = atoi(mm_str) + 1;
        *dd        = atoi(dd_str);
        *hh        = atoi(hh_str);
        *minute    = atoi(minute_str);
        *ss        = atoi(ss_str);

        ln_zonedate localTime;
        ln_date utcTime;

        localTime.years   = *yy;
        localTime.months  = *mm;
        localTime.days    = *dd;
        localTime.hours   = *hh;
        localTime.minutes = *minute;
        localTime.seconds = *ss;
        localTime.gmtoff  = *utc_hours * 3600;

        ln_zonedate_to_date(&localTime, &utcTime);

        *yy     = utcTime.years;
        *mm     = utcTime.months;
        *dd     = utcTime.days;
        *hh     = utcTime.hours;
        *minute = utcTime.minutes;
        *ss     = utcTime.seconds;

        return true;
    }

    DEBUGFDEVICE(pmc8_device, INDI::Logger::DBG_ERROR, "Only received #%d bytes, expected 1.", nbytes_read);
    return false;
}

#endif
