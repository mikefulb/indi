/*******************************************************************************
  Copyright(c) 2017 Jasem Mutlaq. All rights reserved.

  List of INDI Stanadrd Properties

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License version 2 as published by the Free Software Foundation.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.

 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.
*******************************************************************************/

#pragma once

#include "indibase.h"

#undef TIME_UTC

namespace INDI
{

/**
 * @class INDI::StandardProperties
   @brief Common properties standarized across drivers and clients alike.

INDI does not place any special semantics on property names (i.e. properties are just texts, numbers, or switches that represent no physical function). While GUI clients can construct graphical representation of properties in order to permit the user to operate the device, we run into situations where clients and drivers need to agree on the exact meaning of some fundamental properties.
What if some client need to be aware of the existence of some property in order to perform some function useful to the user? How can that client tie itself to such a property if the property can be arbitrary defined by drivers?

The solution is to define Standard Properties in order to establish a level of interoperability among INDI drivers and clients. We propose a set of shared INDI properties that encapsulate the most common characteristics of astronomical instrumentation of interest.
If the semantics of such properties are properly defined, not only it will insure base interoperability, but complete device automation becomes possible as well. Put another way, INDI standard properties are in essence properties that represent a clearly defined characteristic related to the operation of the device drivers.

For example, a very common standard property is EQUATORIAL_EOD_COORD. This property represents the telescope's current RA and DEC. Clients need to be aware of this property in order to, for example, draw the telescope's cross hair on the sky map. If you write a script to control a telescope, you know that any telescope supporting EQUATORIAL_EOD_COORD will behave in an expected manner when the property is invoked.
INDI clients are required to honor standard properties if when and they implement any functions associated with a particular standard property. Furthermore, INDI drivers employing standard properties should strictly adhere to the standard properties structure as defined next.

The properties are defined as string constants. To refer to the property in device drivers, use INDI::StandardProperty::PROPERTY_NAME or the shortcut INDI::SP::PROPERTY_NAME.

The standard properties are divided into the following categories:
<ol>
<li>@ref GeneralProperties</li>
</ol>
@author Jasem Mutlaq
*/
class StandardProperty
{
public:
    /**
     * \defgroup GeneralProperties Standard Properties - General: Common properties shared across devices of multiple genres.
     */

    /*@{*/

    /**
     * @brief Connect to and disconnect from device.
     * Name | Type | Member | Default | Description
     * ---- | ---- | ------ | ------- | -----------
     * CONNECTION | SWITCH | CONNECT | OFF | Establish connection to device
     * CONNECTION | SWITCH | DISCONNECT | ON | Disconnect device
     */
    static constexpr const char *CONNECTION = "CONNECTION";

    /*@}*/


    /*@{*/

    /**
     * @brief Device connection port.
     * Name        | Type | Member | Default | Description
     * ----------- | ---- | ------ | ------- | -----------
     * DEVICE_PORT | TEXT | PORT   |         | Device connection port
     */
    static constexpr const char *DEVICE_PORT = "DEVICE_PORT";

    /*@}*/

    /*@{*/

    /**
     * @brief Local sidereal time HH:MM:SS.
     * Name     | Type   | Member | Default | Description
     * -------- | ------ | ------ | ------- | -----------
     * TIME_LST | NUMBER | LST    |         | Local sidereal time HH:MM:SS
     */
    static constexpr const char *TIME_LST = "TIME_LST";

    /*@}*/

    /*@{*/

//    /**
//     * @brief UTC Time and offset.
//     * Name     | Type   | Member    | Default | Description
//     * -------- | ------ | --------- | ------- | -----------
//     * TIME_UTC | TEXT   | UTC       |         | UTC time in ISO 8601 format
//     * TIME_UTC | TEXT   | OFFSET    |         | UTC offset, in hours +E
//     *
//     * NOTE: Unfortunately TIME_UTC is defined in time.h and conflicts!
//     */
//    static constexpr const char *TIME_UTC = "TIME_UTC";

    /*@}*/

    /*@{*/

    /**
     * @brief Earth geodetic coordinate
     * Name     | Type   | Member | Default | Description
     * -------- | ------ | ------ | ------- | -----------
     * GEOGRAPHIC_COORD | NUMBER | LAT    |         | Site latitude (-90 to +90), degrees +N
     * GEOGRAPHIC_COORD | NUMBER | LONG   |         | Site longitude (0 to 360), degrees +E
     * GEOGRAPHIC_COORD | NUMBER | ELV    |         | Site elevation, meters
     */
    static constexpr const char *GEOGRAPHIC_COORD = "GEOGRAPHIC_COORD";
    static constexpr const char *GEOGRAPHIC_COORD_LAT = "LAT";
    static constexpr const char *GEOGRAPHIC_COORD_LONG = "LONG";
    static constexpr const char *GEOGRAPHIC_COORD_ELEV = "ELEV";


    /*@}*/

    /*@{*/

    /**
     * @brief Weather conditions
     * Name     | Type   | Member | Default | Description
     * -------- | ------ | ------ | ------- | -----------
     * ATMOSPHERE | NUMBER | TEMPERATURE  |         | Kelvin
     * ATMOSPHERE | NUMBER | PRESSURE     |         | Pa
     * ATMOSPHERE | NUMBER | HUMIDITY     |         | Percentage %
     */
    static constexpr const char *ATMOSPHERE = "ATMOSPHERE";
    static constexpr const char *ATMOSPHERE_TEMPERATURE = "TEMPERATURE";
    static constexpr const char *ATMOSPHERE_PRESSURE = "PRESSURE";
    static constexpr const char *ATMOSPHERE_HUMIDITY = "HUMIDITY";


    /*@}*/

};

// SP alias Shortcut for INDI::StandardProperty
using SP = StandardProperty;

} // namespace INDI
