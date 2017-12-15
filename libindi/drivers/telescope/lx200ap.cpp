/*
    Astro-Physics INDI driver

    Copyright (C) 2014 Jasem Mutlaq

    Based on INDI Astrophysics Driver by Markus Wildi

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

#include "lx200ap.h"

#include "indicom.h"
#include "lx200driver.h"
#include "lx200apdriver.h"

#include <libnova/transform.h>

#include <cmath>
#include <cstring>
#include <unistd.h>
#include <termios.h>

/* Constructor */
LX200AstroPhysics::LX200AstroPhysics() : LX200Generic()
{
    setLX200Capability(LX200_HAS_PULSE_GUIDING);
    SetTelescopeCapability(GetTelescopeCapability() | TELESCOPE_HAS_PIER_SIDE | TELESCOPE_HAS_PEC | TELESCOPE_CAN_CONTROL_TRACK | TELESCOPE_HAS_TRACK_RATE, 4);    

    sendLocationOnStartup = false;
    sendTimeOnStartup = false;
}

const char *LX200AstroPhysics::getDefaultName()
{
    return (const char *)"AstroPhysics";
}

bool LX200AstroPhysics::initProperties()
{
    LX200Generic::initProperties();

    timeFormat = LX200_24;

    IUFillNumber(&HourangleCoordsN[0], "HA", "HA H:M:S", "%10.6m", 0., 24., 0., 0.);
    IUFillNumber(&HourangleCoordsN[1], "DEC", "Dec D:M:S", "%10.6m", -90.0, 90.0, 0., 0.);
    IUFillNumberVector(&HourangleCoordsNP, HourangleCoordsN, 2, getDeviceName(), "HOURANGLE_COORD", "Hourangle Coords",
                       MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    IUFillNumber(&HorizontalCoordsN[0], "AZ", "Az D:M:S", "%10.6m", 0., 360., 0., 0.);
    IUFillNumber(&HorizontalCoordsN[1], "ALT", "Alt D:M:S", "%10.6m", -90., 90., 0., 0.);
    IUFillNumberVector(&HorizontalCoordsNP, HorizontalCoordsN, 2, getDeviceName(), "HORIZONTAL_COORD",
                       "Horizontal Coords", MAIN_CONTROL_TAB, IP_RW, 120, IPS_IDLE);


    // Max rate is 999.99999X for the GTOCP4.
    // Using :RR998.9999#  just to be safe. 15.041067*998.99999 = 15026.02578
    TrackRateN[AXIS_RA].min = -15026.0258;
    TrackRateN[AXIS_RA].max = 15026.0258;
    TrackRateN[AXIS_DE].min = -998.9999;
    TrackRateN[AXIS_DE].max = 998.9999;

    // Motion speed of axis when pressing NSWE buttons
    IUFillSwitch(&SlewRateS[0], "12", "12x", ISS_OFF);
    IUFillSwitch(&SlewRateS[1], "64", "64x", ISS_ON);
    IUFillSwitch(&SlewRateS[2], "600", "600x", ISS_OFF);
    IUFillSwitch(&SlewRateS[3], "1200", "1200x", ISS_OFF);
    IUFillSwitchVector(&SlewRateSP, SlewRateS, 4, getDeviceName(), "TELESCOPE_SLEW_RATE", "Slew Rate", MOTION_TAB, IP_RW, ISR_1OFMANY, 0, IPS_IDLE);

    // Slew speed when performing regular GOTO
    IUFillSwitch(&APSlewSpeedS[0], "600", "600x", ISS_ON);
    IUFillSwitch(&APSlewSpeedS[1], "900", "900x", ISS_OFF);
    IUFillSwitch(&APSlewSpeedS[2], "1200", "1200x", ISS_OFF);
    IUFillSwitchVector(&APSlewSpeedSP, APSlewSpeedS, 3, getDeviceName(), "GOTO Rate", "", MOTION_TAB, IP_RW, ISR_1OFMANY,
                       0, IPS_IDLE);

    IUFillSwitch(&SwapS[0], "NS", "North/South", ISS_OFF);
    IUFillSwitch(&SwapS[1], "EW", "East/West", ISS_OFF);
    IUFillSwitchVector(&SwapSP, SwapS, 2, getDeviceName(), "SWAP", "Swap buttons", MOTION_TAB, IP_RW, ISR_1OFMANY, 0,
                       IPS_IDLE);

    IUFillSwitch(&SyncCMRS[USE_REGULAR_SYNC], ":CM#", ":CM#", ISS_ON);
    IUFillSwitch(&SyncCMRS[USE_CMR_SYNC], ":CMR#", ":CMR#", ISS_OFF);
    IUFillSwitchVector(&SyncCMRSP, SyncCMRS, 2, getDeviceName(), "SYNCCMR", "Sync", MOTION_TAB, IP_RW, ISR_1OFMANY, 0,
                       IPS_IDLE);

    // guide speed
    IUFillSwitch(&APGuideSpeedS[0], "0.25", "0.25x", ISS_OFF);
    IUFillSwitch(&APGuideSpeedS[1], "0.5", "0.50x", ISS_ON);
    IUFillSwitch(&APGuideSpeedS[2], "1.0", "1.0x", ISS_OFF);
    IUFillSwitchVector(&APGuideSpeedSP, APGuideSpeedS, 3, getDeviceName(), "Guide Rate", "", GUIDE_TAB, IP_RW, ISR_1OFMANY,
                       0, IPS_IDLE);

    IUFillText(&VersionT[0], "Version", "Version", "");
    IUFillTextVector(&VersionInfo, VersionT, 1, getDeviceName(), "Firmware", "Firmware", MAIN_CONTROL_TAB, IP_RO, 0, IPS_IDLE);

    SetParkDataType(PARK_AZ_ALT);

    return true;
}

void LX200AstroPhysics::ISGetProperties(const char *dev)
{
    LX200Generic::ISGetProperties(dev);

    if (isConnected())
    {        
        defineText(&VersionInfo);

        /* Motion group */
        defineSwitch(&APSlewSpeedSP);
        defineSwitch(&SwapSP);
        defineSwitch(&SyncCMRSP);
        defineSwitch(&APGuideSpeedSP);
    }
}

bool LX200AstroPhysics::updateProperties()
{
    LX200Generic::updateProperties();

    if (isConnected())
    {
        defineText(&VersionInfo);

        /* Motion group */
        defineSwitch(&APSlewSpeedSP);
        defineSwitch(&SwapSP);
        defineSwitch(&SyncCMRSP);
        defineSwitch(&APGuideSpeedSP);

        if (InitPark())
        {
            // If loading parking data is successful, we just set the default parking values.
            SetAxis1ParkDefault(LocationN[LOCATION_LATITUDE].value >= 0 ? 0 : 180);
            SetAxis2ParkDefault(LocationN[LOCATION_LATITUDE].value);
        }
        else
        {
            // Otherwise, we set all parking data to default in case no parking data is found.
            SetAxis1Park(LocationN[LOCATION_LATITUDE].value >= 0 ? 0 : 180);
            SetAxis1ParkDefault(LocationN[LOCATION_LATITUDE].value);

            SetAxis1ParkDefault(LocationN[LOCATION_LATITUDE].value >= 0 ? 0 : 180);
            SetAxis2ParkDefault(LocationN[LOCATION_LATITUDE].value);
        }

        double longitude=-1000, latitude=-1000;
        // Get value from config file if it exists.
        IUGetConfigNumber(getDeviceName(), "GEOGRAPHIC_COORD", "LONG", &longitude);
        IUGetConfigNumber(getDeviceName(), "GEOGRAPHIC_COORD", "LAT", &latitude);
        if (longitude != -1000 && latitude != -1000)
            updateLocation(latitude, longitude, 0);
    }
    else
    {        
        deleteProperty(VersionInfo.name);
        deleteProperty(APSlewSpeedSP.name);
        deleteProperty(SwapSP.name);
        deleteProperty(SyncCMRSP.name);
        deleteProperty(APGuideSpeedSP.name);
    }

    return true;
}

bool LX200AstroPhysics::getFirmwareVersion()
{
    bool success;
    char rev[8];
    char versionString[128];

    success = false;

    if (isSimulation())
        strncpy(versionString, "VCP4-P01-01", 128);
    else
        getAPVersionNumber(PortFD, versionString);

    VersionInfo.s = IPS_OK;
    IUSaveText(&VersionT[0], versionString);
    IDSetText(&VersionInfo, nullptr);

    // Check controller version
    // example "VCP4-P01-01" for CP4 or newer
    //         single or double letter like "T" or "V1" for CP3 and older

    // CP4
    if (strstr(versionString, "VCP4"))
    {
        firmwareVersion = MCV_V;
        servoType = GTOCP4;
        strcpy(rev, "V");
        success = true;
    }
    else if (strlen(versionString) == 1 || strlen(versionString) == 2)
    {
        // Check earlier versions
        // FIXME could probably use better range checking in case we get a letter like 'Z' that doesn't map to anything!
        int typeIndex = VersionT[0].text[0] - 'E';
        if (typeIndex >= 0)
        {
            firmwareVersion = static_cast<ControllerVersion>(typeIndex);
            DEBUGF(INDI::Logger::DBG_DEBUG, "Firmware version index: %d", typeIndex);
            if (firmwareVersion < MCV_G)
                servoType = GTOCP2;
            else
                servoType = GTOCP3;

            strcpy(rev, versionString);

            success = true;
        }
    }

    if (success)
    {
        DEBUGF(INDI::Logger::DBG_SESSION, "Servo Box Controller: GTOCP%d.", servoType);
        DEBUGF(INDI::Logger::DBG_SESSION, "Firmware Version: '%s' - %s", rev, versionString+5);
    }

    return success;
}

bool LX200AstroPhysics::initMount()
{
    // Make sure that the mount is setup according to the properties
    int err=0;

    if (!IsMountInitialized(&mountInitialized))
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Error determining if mount is initialized!");
        return false;
    }

    if (!IsMountParked(&mountParked))
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Error determining if mount is parked!");
        return false;
    }

    if (!mountInitialized)
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "Mount is not yet initialized. Initializing it...");

        if (isSimulation() == false)
        {
            // This is how to init the mount in case RA/DE are missing.
            // :PO#
            if (setAPUnPark(PortFD) < 0)
            {
                DEBUG(INDI::Logger::DBG_ERROR, "UnParking Failed.");
                return false;
            }

            // Stop :Q#
            abortSlew(PortFD);
        }
    }

    mountInitialized = true;

    DEBUG(INDI::Logger::DBG_DEBUG, "Mount is initialized.");

    // Astrophysics mount is always unparked on startup
    // In this driver, unpark only sets the tracking ON.
    // setAPUnPark() is NOT called as this function, despite its name, is only used for initialization purposes.
    UnPark();

    // On most mounts SlewRateS defines the MoveTo AND Slew (GOTO) speeds
    // lx200ap is different - some of the MoveTo speeds are not VALID
    // Slew speeds so we have to keep two lists.
    //
    // SlewRateS is used as the MoveTo speed
    if (isSimulation() == false && (err = selectAPMoveToRate(PortFD, IUFindOnSwitchIndex(&SlewRateSP))) < 0)
    {
        DEBUGF(INDI::Logger::DBG_ERROR, "Error setting move rate (%d).", err);
        return false;
    }

    SlewRateSP.s = IPS_OK;
    IDSetSwitch(&SlewRateSP, nullptr);

    // APSlewSpeedsS defines the Slew (GOTO) speeds valid on the AP mounts
    if (isSimulation() == false && (err = selectAPSlewRate(PortFD, IUFindOnSwitchIndex(&APSlewSpeedSP))) < 0)
    {
        DEBUGF(INDI::Logger::DBG_ERROR, "Error setting slew to rate (%d).", err);
        return false;
    }

    APSlewSpeedSP.s = IPS_OK;
    IDSetSwitch(&APSlewSpeedSP, nullptr);

    return true;
}

bool LX200AstroPhysics::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    int err = 0;

    // ignore if not ours //
    if (strcmp(getDeviceName(), dev))
        return false;    

    // =======================================
    // Swap Buttons
    // =======================================
    if (!strcmp(name, SwapSP.name))
    {
        int currentSwap;

        IUResetSwitch(&SwapSP);
        IUUpdateSwitch(&SwapSP, states, names, n);
        currentSwap = IUFindOnSwitchIndex(&SwapSP);

        if ((!isSimulation() && (err = swapAPButtons(PortFD, currentSwap)) < 0))
        {
            DEBUGF(INDI::Logger::DBG_ERROR, "Error swapping buttons (%d).", err);
            return false;
        }

        SwapS[0].s = ISS_OFF;
        SwapS[1].s = ISS_OFF;
        SwapSP.s   = IPS_OK;
        IDSetSwitch(&SwapSP, nullptr);
        return true;
    }

    // ===========================================================
    // GOTO ("slew") Speed.
    // ===========================================================
    if (!strcmp(name, APSlewSpeedSP.name))
    {
        IUUpdateSwitch(&APSlewSpeedSP, states, names, n);
        int slewRate = IUFindOnSwitchIndex(&APSlewSpeedSP);

        if (!isSimulation() && (err = selectAPSlewRate(PortFD, slewRate) < 0))
        {
            DEBUGF(INDI::Logger::DBG_ERROR, "Error setting move to rate (%d).", err);
            return false;
        }

        APSlewSpeedSP.s = IPS_OK;
        IDSetSwitch(&APSlewSpeedSP, nullptr);
        return true;
    }

    // ===========================================================
    // Guide Speed.
    // ===========================================================
    if (!strcmp(name, APGuideSpeedSP.name))
    {
        IUUpdateSwitch(&APGuideSpeedSP, states, names, n);
        int guideRate = IUFindOnSwitchIndex(&APGuideSpeedSP);

        if (!isSimulation() && (err = selectAPGuideRate(PortFD, guideRate) < 0))
        {
            DEBUGF(INDI::Logger::DBG_ERROR, "Error setting guiding to rate (%d).", err);
            return false;
        }

        APGuideSpeedSP.s = IPS_OK;
        IDSetSwitch(&APGuideSpeedSP, nullptr);
        return true;
    }

    // =======================================
    // Choose the appropriate sync command
    // =======================================
    if (!strcmp(name, SyncCMRSP.name))
    {
        IUResetSwitch(&SyncCMRSP);
        IUUpdateSwitch(&SyncCMRSP, states, names, n);
        IUFindOnSwitchIndex(&SyncCMRSP);
        SyncCMRSP.s = IPS_OK;
        IDSetSwitch(&SyncCMRSP, nullptr);
        return true;
    }

    // =======================================
    // Choose the PEC playback mode
    // =======================================
    if (!strcmp(name, PECStateSP.name))
    {
        IUResetSwitch(&PECStateSP);
        IUUpdateSwitch(&PECStateSP, states, names, n);
        IUFindOnSwitchIndex(&PECStateSP);

        int pecstate = IUFindOnSwitchIndex(&PECStateSP);

        if (!isSimulation() && (err = selectAPPECState(PortFD, pecstate) < 0))
        {
            DEBUGF(INDI::Logger::DBG_ERROR, "Error setting PEC state (%d).", err);
            return false;
        }

        PECStateSP.s = IPS_OK;
        IDSetSwitch(&PECStateSP, nullptr);

        return true;
    }

    return LX200Generic::ISNewSwitch(dev, name, states, names, n);
}

bool LX200AstroPhysics::ReadScopeStatus()
{
    if (isSimulation())
    {
        mountSim();
        return true;
    }

    if (getLX200RA(PortFD, &currentRA) < 0 || getLX200DEC(PortFD, &currentDEC) < 0)
    {
        EqNP.s = IPS_ALERT;
        IDSetNumber(&EqNP, "Error reading RA/DEC.");
        return false;
    }

    if (TrackState == SCOPE_SLEWING)
    {
        double dx = lastRA - currentRA;
        double dy = lastDE - currentDEC;

        DEBUGF(INDI::Logger::DBG_DEBUG, "Slewing... currentRA: %g dx: %g currentDE: %g dy: %g", currentRA, dx, currentDEC, dy);

        // Wait until acknowledged
        if (dx == 0 && dy == 0)
        {
            TrackState = SCOPE_TRACKING;
            DEBUG(INDI::Logger::DBG_SESSION, "Slew is complete. Tracking...");
        }

        // Keep try of last values to determine if the mount settled.
        lastRA = currentRA;
        lastDE = currentDEC;
    }
    else if (TrackState == SCOPE_PARKING)
    {
        if (getLX200Az(PortFD, &currentAz) < 0 || getLX200Alt(PortFD, &currentAlt) < 0)
        {
            EqNP.s = IPS_ALERT;
            IDSetNumber(&EqNP, "Error reading Az/Alt.");
            return false;
        }

        double dx = lastAZ - currentAz;
        double dy = lastAL - currentAlt;

        DEBUGF(INDI::Logger::DBG_DEBUG, "Parking... currentAz: %g dx: %g currentAlt: %g dy: %g", currentAz, dx, currentAlt, dy);

        if (dx == 0 && dy == 0)
        {
            DEBUG(INDI::Logger::DBG_DEBUG, "Parking slew is complete. Asking astrophysics mount to park...");

            if (!isSimulation() && setAPPark(PortFD) < 0)
            {
                DEBUG(INDI::Logger::DBG_ERROR, "Parking Failed.");
                return false;
            }

            // Turn off tracking.
            SetTrackEnabled(false);

            SetParked(true);

            DEBUG(INDI::Logger::DBG_SESSION, "Please disconnect and power off the mount.");
        }

        lastAZ = currentAz;
        lastAL = currentAlt;
    }

    NewRaDec(currentRA, currentDEC);

    syncSideOfPier();

    return true;
}

// experimental function needs testing!!!
bool LX200AstroPhysics::IsMountInitialized(bool *initialized)
{
    double ra, dec;
    bool raZE, deZE, de90;

    double epscheck = 1e-5; // two doubles this close are considered equal

    DEBUG(INDI::Logger::DBG_DEBUG, "EXPERIMENTAL: LX200AstroPhysics::IsMountInitialized()");

    if (getLX200RA(PortFD, &ra) || getLX200DEC(PortFD, &dec))
        return false;

    DEBUGF(INDI::Logger::DBG_SESSION, "IsMountInitialized: RA: %f - DEC: %f", ra, dec);

    raZE = (fabs(ra) < epscheck);
    deZE = (fabs(dec) < epscheck);
    de90 = (fabs(dec-90) < epscheck);

    DEBUGF(INDI::Logger::DBG_SESSION, "IsMountInitialized: raZE: %d - deZE: %d - de90: %d", raZE, deZE, de90);

    // RA is zero and DEC is zero or 90
    // then mount is not initialized and we need to initialized it.
    if ( (raZE && deZE) || (raZE && de90))
    {
        DEBUG(INDI::Logger::DBG_DEBUG, "Mount is not yet initialized.");
        *initialized = false;
        return true;
    }

    // mount is initialized
    DEBUG(INDI::Logger::DBG_DEBUG, "Mount is initialized.");
    *initialized = true;

    return true;
}

// experimental function needs testing!!!
bool LX200AstroPhysics::IsMountParked(bool *isParked)
{
    double ra1, ra2;

    DEBUG(INDI::Logger::DBG_DEBUG, "EXPERIMENTAL: LX200AstroPhysics::IsMountParked()");

    // try one method
    if (getMountStatus(isParked))
    {
        return true;
    }

    // fallback for older controllers
    if (getLX200RA(PortFD, &ra1))
        return false;

    // wait 250ms
    usleep(250000);

    if (getLX200RA(PortFD, &ra2))
        return false;

    // if within an arcsec then assume RA is constant
    if (abs(ra1-ra2) < (1.0/(15.0*3600.0)))
    {
        *isParked=false;
        return true;
    }

    // can't determine
    return false;

}

bool LX200AstroPhysics::getMountStatus(bool *isParked)
{
    // check for newer
    if ((firmwareVersion != MCV_UNKNOWN) && (firmwareVersion >= MCV_T))
    {
        char parkStatus;

        if (check_lx200ap_status(PortFD, &parkStatus) == 0)
        {
            DEBUGF(INDI::Logger::DBG_SESSION, "parkStatus: %c", parkStatus);

            *isParked = (parkStatus == 'P');
            return true;
        }
    }

    return false;
}

bool LX200AstroPhysics::Goto(double r, double d)
{
    targetRA  = r;
    targetDEC = d;

    char RAStr[64], DecStr[64];
    fs_sexa(RAStr, targetRA, 2, 3600);
    fs_sexa(DecStr, targetDEC, 2, 3600);

    // If moving, let's stop it first.
    if (EqNP.s == IPS_BUSY)
    {
        if (!isSimulation() && abortSlew(PortFD) < 0)
        {
            AbortSP.s = IPS_ALERT;
            IDSetSwitch(&AbortSP, "Abort slew failed.");
            return false;
        }

        AbortSP.s = IPS_OK;
        EqNP.s    = IPS_IDLE;
        IDSetSwitch(&AbortSP, "Slew aborted.");
        IDSetNumber(&EqNP, nullptr);

        if (MovementNSSP.s == IPS_BUSY || MovementWESP.s == IPS_BUSY)
        {
            MovementNSSP.s = MovementWESP.s = IPS_IDLE;
            EqNP.s                          = IPS_IDLE;
            IUResetSwitch(&MovementNSSP);
            IUResetSwitch(&MovementWESP);
            IDSetSwitch(&MovementNSSP, nullptr);
            IDSetSwitch(&MovementWESP, nullptr);
        }

        // sleep for 100 mseconds
        usleep(100000);
    }

    if (!isSimulation())
    {
        if (setAPObjectRA(PortFD, targetRA) < 0 || (setAPObjectDEC(PortFD, targetDEC)) < 0)
        {
            EqNP.s = IPS_ALERT;
            IDSetNumber(&EqNP, "Error setting RA/DEC.");
            return false;
        }

        int err = 0;

        /* Slew reads the '0', that is not the end of the slew */
        if ((err = Slew(PortFD)))
        {
            EqNP.s = IPS_ALERT;
            IDSetNumber(&EqNP, "Error Slewing to JNow RA %s - DEC %s\n", RAStr, DecStr);
            slewError(err);
            return false;
        }

        motionCommanded = true;
        lastRA = targetRA;
        lastDE = targetDEC;
    }

    TrackState = SCOPE_SLEWING;
    EqNP.s     = IPS_BUSY;

    DEBUGF(INDI::Logger::DBG_SESSION, "Slewing to RA: %s - DEC: %s", RAStr, DecStr);
    return true;
}


int LX200AstroPhysics::SendPulseCmd(int direction, int duration_msec)
{
    if (firmwareVersion == MCV_E)
        handleGTOCP2MotionBug();

    return APSendPulseCmd(PortFD, direction, duration_msec);
}

bool LX200AstroPhysics::Handshake()
{
    if (isSimulation())
    {
        DEBUG(INDI::Logger::DBG_SESSION, "Simulated Astrophysics is online. Retrieving basic data...");
        return true;
    }

    int err=0;

    if ((err = setAPClearBuffer(PortFD)) < 0)
    {
        DEBUGF(INDI::Logger::DBG_ERROR, "Error clearing the buffer (%d): %s", err, strerror(err));
        return false;
    }

    if ((err = setAPBackLashCompensation(PortFD, 0, 0, 0)) < 0)
    {
        // It seems we need to send it twice before it works!
        if ((err = setAPBackLashCompensation(PortFD, 0, 0, 0)) < 0)
        {
            DEBUGF(INDI::Logger::DBG_ERROR, "Error setting back lash compensation (%d): %s.", err, strerror(err));
            return false;
        }
    }

    // get firmware version
    getFirmwareVersion();

    // Detect and set fomat. It should be LONG.
    return (checkLX200Format(PortFD) == 0);
}

bool LX200AstroPhysics::Disconnect()
{
    timeUpdated     = false;
    //locationUpdated = false;
    mountInitialized = false;

    return LX200Generic::Disconnect();
}

bool LX200AstroPhysics::Sync(double ra, double dec)
{
    char syncString[256];

    int syncType = IUFindOnSwitchIndex(&SyncCMRSP);

    if (!isSimulation())
    {
        if (setAPObjectRA(PortFD, ra) < 0 || setAPObjectDEC(PortFD, dec) < 0)
        {
            EqNP.s = IPS_ALERT;
            IDSetNumber(&EqNP, "Error setting RA/DEC. Unable to Sync.");
            return false;
        }

        bool syncOK = true;

        switch (syncType)
        {
        case USE_REGULAR_SYNC:
            if (::Sync(PortFD, syncString) < 0)
                syncOK = false;
            break;

        case USE_CMR_SYNC:
            if (APSyncCMR(PortFD, syncString) < 0)
                syncOK = false;
            break;

        default:
            break;
        }

        if (syncOK == false)
        {
            EqNP.s = IPS_ALERT;
            IDSetNumber(&EqNP, "Synchronization failed.");
            return false;
        }

    }

    currentRA  = ra;
    currentDEC = dec;

    DEBUGF(INDI::Logger::DBG_DEBUG, "%s Synchronization successful %s", (syncType == USE_REGULAR_SYNC ? "CM" : "CMR"), syncString);
    DEBUG(INDI::Logger::DBG_SESSION, "Synchronization successful.");

    EqNP.s     = IPS_OK;

    NewRaDec(currentRA, currentDEC);

    return true;
}

bool LX200AstroPhysics::updateTime(ln_date *utc, double utc_offset)
{
    struct ln_zonedate ltm;

    ln_date_to_zonedate(utc, &ltm, utc_offset * 3600.0);

    JD = ln_get_julian_day(utc);

    DEBUGF(INDI::Logger::DBG_DEBUG, "New JD is %.2f", JD);

    // Set Local Time
    if (isSimulation() == false && setLocalTime(PortFD, ltm.hours, ltm.minutes, (int)ltm.seconds) < 0)
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Error setting local time.");
        return false;
    }

    DEBUGF(INDI::Logger::DBG_DEBUG, "Set Local Time %02d:%02d:%02d is successful.", ltm.hours, ltm.minutes,
           (int)ltm.seconds);

    if (isSimulation() == false && setCalenderDate(PortFD, ltm.days, ltm.months, ltm.years) < 0)
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Error setting local date.");
        return false;
    }

    DEBUGF(INDI::Logger::DBG_DEBUG, "Set Local Date %02d/%02d/%02d is successful.", ltm.days, ltm.months, ltm.years);

    if (isSimulation() == false && setAPUTCOffset(PortFD, fabs(utc_offset)) < 0)
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Error setting UTC Offset.");
        return false;
    }

    DEBUGF(INDI::Logger::DBG_DEBUG, "Set UTC Offset %g (always positive for AP) is successful.", fabs(utc_offset));

    DEBUG(INDI::Logger::DBG_SESSION, "Time updated.");

    timeUpdated = true;

    if (locationUpdated && timeUpdated && mountInitialized == false)
        initMount();

    return true;
}

bool LX200AstroPhysics::updateLocation(double latitude, double longitude, double elevation)
{
    INDI_UNUSED(elevation);

    if (!isSimulation() && setAPSiteLongitude(PortFD, 360.0 - longitude) < 0)
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Error setting site longitude coordinates");
        return false;
    }

    if (!isSimulation() && setAPSiteLatitude(PortFD, latitude) < 0)
    {
        DEBUG(INDI::Logger::DBG_ERROR, "Error setting site latitude coordinates");
        return false;
    }

    char l[32], L[32];
    fs_sexa(l, latitude, 3, 3600);
    fs_sexa(L, longitude, 4, 3600);

    DEBUGF(INDI::Logger::DBG_SESSION, "Site location updated to Lat %.32s - Long %.32s", l, L);

    locationUpdated = true;

    if (locationUpdated && timeUpdated && mountInitialized == false)
        initMount();

    return true;
}

void LX200AstroPhysics::debugTriggered(bool enable)
{
    LX200Generic::debugTriggered(enable);
    set_lx200ap_name(getDeviceName(), DBG_SCOPE);
}

// For most mounts the SetSlewRate() method sets both the MoveTo and Slew (GOTO) speeds.
// For AP mounts these two speeds are handled separately - so SetSlewRate() actually sets the MoveTo speed for AP mounts - confusing!
// ApSetSlew
bool LX200AstroPhysics::SetSlewRate(int index)
{
    if (!isSimulation() && selectAPMoveToRate(PortFD, index) < 0)
    {
        SlewRateSP.s = IPS_ALERT;
        IDSetSwitch(&SlewRateSP, "Error setting slew mode.");
        return false;
    }

    SlewRateSP.s = IPS_OK;
    IDSetSwitch(&SlewRateSP, nullptr);
    return true;
}

bool LX200AstroPhysics::Park()
{
    double parkAz  = GetAxis1Park();
    double parkAlt = GetAxis2Park();

    char AzStr[16], AltStr[16];
    fs_sexa(AzStr, parkAz, 2, 3600);
    fs_sexa(AltStr, parkAlt, 2, 3600);
    DEBUGF(INDI::Logger::DBG_DEBUG, "Parking to Az (%s) Alt (%s)...", AzStr, AltStr);

    if (isSimulation())
    {
        ln_lnlat_posn observer;
        observer.lat = LocationN[LOCATION_LATITUDE].value;
        observer.lng = LocationN[LOCATION_LONGITUDE].value;
        if (observer.lng > 180)
            observer.lng -= 360;

        ln_hrz_posn horizontalPos;
        // Libnova south = 0, west = 90, north = 180, east = 270

        horizontalPos.az = parkAz + 180;
        if (horizontalPos.az > 360)
            horizontalPos.az -= 360;
        horizontalPos.alt = parkAlt;

        ln_equ_posn equatorialPos;

        ln_get_equ_from_hrz(&horizontalPos, &observer, ln_get_julian_from_sys(), &equatorialPos);

        Goto(equatorialPos.ra / 15.0, equatorialPos.dec);
    }
    else
    {
        if (setAPObjectAZ(PortFD, parkAz) < 0 || setAPObjectAlt(PortFD, parkAlt) < 0)
        {
            DEBUG(INDI::Logger::DBG_ERROR, "Error setting Az/Alt.");
            return false;
        }

        int err = 0;

        /* Slew reads the '0', that is not the end of the slew */
        if ((err = Slew(PortFD)))
        {
            DEBUGF(INDI::Logger::DBG_ERROR, "Error Slewing to Az %s - Alt %s", AzStr, AltStr);
            slewError(err);
            return false;
        }

        motionCommanded = true;
        lastAZ = parkAz;
        lastAL = parkAlt;
    }

    EqNP.s     = IPS_BUSY;
    TrackState = SCOPE_PARKING;
    DEBUG(INDI::Logger::DBG_SESSION, "Parking is in progress...");

    return true;
}

bool LX200AstroPhysics::UnPark()
{
    // The AP :PO# should only be used during initilization and not here as indicated by email from Preston on 2017-12-12

    // Enable tracking
    SetTrackEnabled(true);

    SetParked(false);

    return true;
}

bool LX200AstroPhysics::SetCurrentPark()
{
    ln_hrz_posn horizontalPos;
    // Libnova south = 0, west = 90, north = 180, east = 270

    ln_lnlat_posn observer;
    observer.lat = LocationN[LOCATION_LATITUDE].value;
    observer.lng = LocationN[LOCATION_LONGITUDE].value;
    if (observer.lng > 180)
        observer.lng -= 360;

    ln_equ_posn equatorialPos;
    equatorialPos.ra  = currentRA * 15;
    equatorialPos.dec = currentDEC;
    ln_get_hrz_from_equ(&equatorialPos, &observer, ln_get_julian_from_sys(), &horizontalPos);

    double parkAZ = horizontalPos.az - 180;
    if (parkAZ < 0)
        parkAZ += 360;
    double parkAlt = horizontalPos.alt;

    char AzStr[16], AltStr[16];
    fs_sexa(AzStr, parkAZ, 2, 3600);
    fs_sexa(AltStr, parkAlt, 2, 3600);

    DEBUGF(INDI::Logger::DBG_DEBUG, "Setting current parking position to coordinates Az (%s) Alt (%s)", AzStr, AltStr);

    SetAxis1Park(parkAZ);
    SetAxis2Park(parkAlt);

    return true;
}

bool LX200AstroPhysics::SetDefaultPark()
{
    // Az = 0 for North hemisphere
    SetAxis1Park(LocationN[LOCATION_LATITUDE].value > 0 ? 0 : 180);

    // Alt = Latitude
    SetAxis2Park(LocationN[LOCATION_LATITUDE].value);

    return true;
}

void LX200AstroPhysics::syncSideOfPier()
{
    const char *cmd = ":pS#";
    // Response
    char response[16] = { 0 };
    int rc = 0, nbytes_read = 0, nbytes_written = 0;

    DEBUGF(INDI::Logger::DBG_DEBUG, "CMD: <%s>", cmd);

    tcflush(PortFD, TCIOFLUSH);

    if ((rc = tty_write(PortFD, cmd, strlen(cmd), &nbytes_written)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error writing to device %s (%d)", errmsg, rc);
        return;
    }

    // Read Side
    if ((rc = tty_read_section(PortFD, response, '#', 3, &nbytes_read)) != TTY_OK)
    {
        char errmsg[256];
        tty_error_msg(rc, errmsg, 256);
        DEBUGF(INDI::Logger::DBG_ERROR, "Error reading from device %s (%d)", errmsg, rc);
        return;
    }

    response[nbytes_read - 1] = '\0';

    tcflush(PortFD, TCIOFLUSH);

    DEBUGF(INDI::Logger::DBG_DEBUG, "RES: <%s>", response);

    if (!strcmp(response, "East"))
        setPierSide(INDI::Telescope::PIER_EAST);
    else if (!strcmp(response, "West"))
        setPierSide(INDI::Telescope::PIER_WEST);
    else
        DEBUGF(INDI::Logger::DBG_ERROR, "Invalid pier side response from device-> %s", response);

}

bool LX200AstroPhysics::saveConfigItems(FILE *fp)
{
    LX200Generic::saveConfigItems(fp);

    IUSaveConfigSwitch(fp, &SyncCMRSP);
    IUSaveConfigSwitch(fp, &APSlewSpeedSP);
    IUSaveConfigSwitch(fp, &APGuideSpeedSP);

    return true;
}

bool LX200AstroPhysics::SetTrackMode(uint8_t mode)
{
    int err=0;

    if (mode == TRACK_CUSTOM)
    {
        if (!isSimulation() && (err = selectAPTrackingMode(PortFD, AP_TRACKING_SIDEREAL)) < 0)
        {
            DEBUGF(INDI::Logger::DBG_ERROR, "Error setting tracking mode (%d).", err);
            return false;
        }

        return SetTrackRate(TrackRateN[AXIS_RA].value, TrackRateN[AXIS_DE].value);
    }

    if (!isSimulation() && (err = selectAPTrackingMode(PortFD, mode)) < 0)
    {
        DEBUGF(INDI::Logger::DBG_ERROR, "Error setting tracking mode (%d).", err);
        return false;
    }

    return true;
}

bool LX200AstroPhysics::SetTrackEnabled(bool enabled)
{
   return SetTrackMode(enabled ? IUFindOnSwitchIndex(&TrackModeSP) : AP_TRACKING_OFF);
}

bool LX200AstroPhysics::SetTrackRate(double raRate, double deRate)
{
    // Convert to arcsecs/s to AP sidereal multiplier
    /*
    :RR0.0000#      =       normal sidereal tracking in RA - similar to  :RT2#
    :RR+1.0000#     =       1 + normal sidereal     =       2X sidereal
    :RR+9.0000#     =       9 + normal sidereal     =       10X sidereal
    :RR-1.0000#     =       normal sidereal - 1     =       0 or Stop - similar to  :RT9#
    :RR-11.0000#    =       normal sidereal - 11    =       -10X sidereal (East at 10X)

    :RD0.0000#      =       normal zero rate for Dec.
    :RD5.0000#      =       5 + normal zero rate    =       5X sidereal clockwise from above - equivalent to South
    :RD-5.0000#     =       normal zero rate - 5    =       5X sidereal counter-clockwise from above - equivalent to North
    */

    double APRARate = (raRate - TRACKRATE_SIDEREAL) / TRACKRATE_SIDEREAL;
    double APDERate = deRate / TRACKRATE_SIDEREAL;

    if (!isSimulation())
    {
        if (setAPRATrackRate(PortFD, APRARate) < 0 || setAPDETrackRate(PortFD, APDERate) < 0)
            return false;
    }

    return true;
}

bool LX200AstroPhysics::getUTFOffset(double *offset)
{
    return (getAPUTCOffset(PortFD, offset) == 0);
}

bool LX200AstroPhysics::MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command)
{
    bool rc = LX200Generic::MoveNS(dir, command);

    if (command == MOTION_START)
           motionCommanded = true;

    return rc;
}

bool LX200AstroPhysics::MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command)
{
    bool rc = LX200Generic::MoveWE(dir, command);

    if (command == MOTION_START)
        motionCommanded = true;

    return rc;
}

void LX200AstroPhysics::handleGTOCP2MotionBug()
{
    // GTOCP2 (Version 'E' and earilar) has a bug that would reset the guide rate to whatever last motion took place
    // So it must be reset to the user setting in order for guiding to work properly.
    if (motionCommanded)
    {
        selectAPGuideRate(PortFD, IUFindOnSwitchIndex(&APGuideSpeedSP));
        motionCommanded = false;
    }
}
