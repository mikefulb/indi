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

#pragma once

#include "lx200generic.h"

class LX200AstroPhysics : public LX200Generic
{
  public:
    LX200AstroPhysics();
    ~LX200AstroPhysics() {}

    typedef enum { MCV_E, MCV_F, MCV_G, MCV_H, MCV_I, MCV_J, MCV_K_UNUSED,
                   MCV_L, MCV_M, MCV_N, MCV_O, MCV_P, MCV_Q, MCV_R, MCV_S,
                   MCV_T, MCV_U, MCV_V, MCV_UNKNOWN} ControllerVersion;
    typedef enum { GTOCP1=1, GTOCP2, GTOCP3, GTOCP4, GTOCP_UNKNOWN} ServoVersion;

    virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
    virtual void ISGetProperties(const char *dev) override;

  protected:
    virtual const char *getDefaultName() override;
    virtual bool initProperties() override;
    virtual bool updateProperties() override;    

    virtual bool ReadScopeStatus() override;
    virtual bool Handshake() override;
    virtual bool Disconnect() override;

    // Parking
    virtual bool SetCurrentPark() override;
    virtual bool SetDefaultPark() override;
    virtual bool Park() override;
    virtual bool UnPark() override;

    virtual bool Sync(double ra, double dec) override;
    virtual bool Goto(double, double) override;
    virtual bool updateTime(ln_date *utc, double utc_offset) override;
    virtual bool updateLocation(double latitude, double longitude, double elevation) override;
    virtual bool SetSlewRate(int index) override;

    virtual int  SendPulseCmd(int direction, int duration_msec) override;

    virtual bool getUTFOffset(double *offset) override;

    // Tracking
    virtual bool SetTrackMode(uint8_t mode) override;
    virtual bool SetTrackEnabled(bool enabled) override;
    virtual bool SetTrackRate(double raRate, double deRate) override;

    // NSWE Motion Commands
    virtual bool MoveNS(INDI_DIR_NS dir, TelescopeMotionCommand command) override;
    virtual bool MoveWE(INDI_DIR_WE dir, TelescopeMotionCommand command) override;

    virtual bool saveConfigItems(FILE *fp) override;

    virtual void debugTriggered(bool enable) override;

    void handleGTOCP2MotionBug();

    INumber HourangleCoordsN[2];
    INumberVectorProperty HourangleCoordsNP;

    INumber HorizontalCoordsN[2];
    INumberVectorProperty HorizontalCoordsNP;

    ISwitch APSlewSpeedS[3];
    ISwitchVectorProperty APSlewSpeedSP;

    ISwitch SwapS[2];
    ISwitchVectorProperty SwapSP;

    ISwitch SyncCMRS[2];
    ISwitchVectorProperty SyncCMRSP;
    enum { USE_REGULAR_SYNC, USE_CMR_SYNC };

    ISwitch APGuideSpeedS[3];
    ISwitchVectorProperty APGuideSpeedSP;

    IText VersionT[1];
    ITextVectorProperty VersionInfo;

  private:
    bool initMount();

    // Side of pier
    void syncSideOfPier();
    bool IsMountInitialized(bool *initialized);
    bool IsMountParked(bool *isParked);

    bool timeUpdated=false, locationUpdated=false;
    ControllerVersion firmwareVersion = MCV_UNKNOWN;
    ServoVersion servoType = GTOCP_UNKNOWN;

    double currentAlt=0, currentAz=0;
    double lastRA=0, lastDE=0;
    double lastAZ=0, lastAL=0;

    bool motionCommanded=false;
    bool mountInitialized=false;
    bool mountParked=false;
};
