// route_mgr.cxx - manage a route (i.e. a collection of waypoints)
//
// Written by Curtis Olson, started January 2004.
//            Norman Vine
//            Melchior FRANZ
//
// Copyright (C) 2004  Curtis L. Olson  - http://www.flightgear.org/~curt
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// $Id: route_mgr.cxx,v 1.14 2009/04/13 15:29:48 curt Exp $


#include <math.h>
#include <stdlib.h>

#include "comms/display.h"
#include "comms/logging.h"
#include "comms/remote_link.h"
#include "include/globaldefs.h"
#include "main/globals.hxx"
#include "props/props_io.hxx"
#include "sensors/gps_mgr.hxx"
#include "util/exception.hxx"
#include "util/sg_path.hxx"
#include "util/wind.hxx"

#include "waypoint.hxx"
#include "route_mgr.hxx"


FGRouteMgr::FGRouteMgr() :
    active( new SGRoute ),
    standby( new SGRoute ),
    config_props( NULL ),
    lon_node( NULL ),
    lat_node( NULL ),
    alt_node( NULL ),
    true_hdg_deg( NULL ),
    target_agl_ft( NULL ),
    override_agl_ft( NULL ),
    target_msl_ft( NULL ),
    override_msl_ft( NULL ),
    target_waypoint( NULL ),
    wp_dist_m( NULL ),
    wp_eta_sec( NULL ),
    wind_speed_kt( NULL ),
    wind_dir_deg( NULL ),
    true_airspeed_kt( NULL ),
    est_wind_target_heading_deg( NULL ),
    ap_console_skip( NULL ),
    ap_logging_skip( NULL )
{
}


FGRouteMgr::~FGRouteMgr() {
    delete standby;
    delete active;
}


// bind property nodes
void FGRouteMgr::bind() {
    lon_node = fgGetNode( "/position/longitude-deg", true );
    lat_node = fgGetNode( "/position/latitude-deg", true );
    alt_node = fgGetNode( "/position/altitude-ft", true );

    true_hdg_deg = fgGetNode( "/autopilot/settings/target-groundtrack-deg", true );
    target_msl_ft
        = fgGetNode( "/autopilot/settings/target-msl-ft", true );
    override_msl_ft
        = fgGetNode( "/autopilot/settings/override-msl-ft", true );
    target_agl_ft
        = fgGetNode( "/autopilot/settings/target-agl-ft", true );
    override_agl_ft
        = fgGetNode( "/autopilot/settings/override-agl-ft", true );
    target_waypoint
	= fgGetNode( "/autopilot/route-mgr/target-waypoint-idx", true );
    wp_dist_m = fgGetNode( "/autopilot/route-mgr/wp-dist-m", true );
    wp_eta_sec = fgGetNode( "/autopilot/route-mgr/wp-eta-sec", true );

    wind_speed_kt = fgGetNode("/filters/wind-est/wind-speed-kt", true);
    wind_dir_deg = fgGetNode("/filters/wind-est/wind-dir-deg", true);
    true_airspeed_kt = fgGetNode("/filters/wind-est/true-airspeed-kt", true);
    est_wind_target_heading_deg
	= fgGetNode("/filters/wind-est/target-heading-deg", true);

    ap_console_skip = fgGetNode("/config/remote-link/autopilot-skip", true);
    ap_logging_skip = fgGetNode("/config/logging/autopilot-skip", true);
}


void FGRouteMgr::init( SGPropertyNode *branch ) {
    config_props = branch;

    bind();

    active->clear();
    standby->clear();

    if ( ! build() ) {
	printf("Detected an internal inconsistency in the route\n");
	printf(" configuration.  See earlier errors for\n" );
	printf(" details.");
	exit(-1);
    }

    // build() constructs the new route in the "standby" slot, swap it
    // to "active"
    swap();
}


void FGRouteMgr::update() {
    double wp_course, wp_distance;

    double override_agl = override_agl_ft->getDoubleValue();
    double override_msl = override_msl_ft->getDoubleValue();
    double target_agl_m = 0;
    double target_msl_m = 0;

    if ( active->size() > 0 ) {
	if ( GPS_age() < 10.0 ) {
	    // track current waypoint of route (only if we have fresh gps data)
	    SGWayPoint wp = active->get_current();
	    wp.CourseAndDistance( lon_node->getDoubleValue(),
				  lat_node->getDoubleValue(),
				  alt_node->getDoubleValue(),
				  &wp_course, &wp_distance );

	    true_hdg_deg->setDoubleValue( wp_course );
	    target_agl_m = wp.get_target_agl_m();
	    target_msl_m = wp.get_target_alt_m();

	    if ( wp_distance < 50.0 ) {
		active->increment_current();
	    }

	    // publish current target waypoint
	    target_waypoint->setIntValue( active->get_waypoint_index() );
	}
    } else {
        // FIXME: we've been commanded to follow a route, but no route
        // has been defined.

        // We are in ill-defined territory, should we do some sort of
        // circle of our home position?
    }

    wp_dist_m->setFloatValue( wp_distance );

    // update target altitude based on waypoint targets and possible
    // overrides ... preference is given to agl if both agl & msl are
    // set.
    if ( override_agl > 1 ) {
	target_agl_ft->setDoubleValue( override_agl );
    } else if ( override_msl > 1 ) {
	target_msl_ft->setDoubleValue( override_msl );
    } else if ( target_agl_m > 1 ) {
	target_agl_ft->setDoubleValue( target_agl_m * SG_METER_TO_FEET );
    } else if ( target_msl_m > 1 ) {
	target_msl_ft->setDoubleValue( target_msl_m * SG_METER_TO_FEET );
    }

    double hd_deg = 0.0;
    double gs_kt = 0.0;
    wind_course( wind_speed_kt->getDoubleValue(),
		 true_airspeed_kt->getDoubleValue(),
		 wind_dir_deg->getDoubleValue(),
		 wp_course,
		 &hd_deg, &gs_kt );

    est_wind_target_heading_deg->setDoubleValue( hd_deg );

    if ( gs_kt > 0.1 ) {
	wp_eta_sec->setFloatValue( wp_distance / (gs_kt * SG_KT_TO_MPS) );
    } else {
	wp_eta_sec->setFloatValue( 0.0 );
    }

#if 0
    if ( display_on ) {
	SGPropertyNode *ground_deg = fgGetNode("/orientation/groundtrack-deg", true);
	double gtd = ground_deg->getDoubleValue();
	if ( gtd < 0 ) { gtd += 360.0; }
	double diff = wp_course - gtd;
	if ( diff < -180.0 ) { diff += 360.0; }
	if ( diff > 180.0 ) { diff -= 360.0; }
	SGPropertyNode *psi = fgGetNode("/orientation/heading-deg", true);
	printf("true filt=%.1f true-wind-est=%.1f target-hd=%.1f\n",
	       psi->getDoubleValue(), true_deg, hd * SGD_RADIANS_TO_DEGREES);
	printf("gt cur=%.1f target=%.1f diff=%.1f\n", gtd, wp_course, diff);
	diff = hd*SGD_RADIANS_TO_DEGREES - true_deg;
	if ( diff < -180.0 ) { diff += 360.0; }
	if ( diff > 180.0 ) { diff -= 360.0; }
	printf("wnd: cur=%.1f target=%.1f diff=%.1f\n",
	       true_deg, hd * SGD_RADIANS_TO_DEGREES, diff);
    }
#endif
}


bool FGRouteMgr::swap() {
    if ( !standby->size() ) {
	// standby route is empty
	return false;
    }

    SGRoute *tmp;
    tmp = active;
    active = standby;
    standby = tmp;

    return true;
}


bool FGRouteMgr::build() {
    standby->clear();

    SGPropertyNode *node;
    int i;

    int count = config_props->nChildren();
    for ( i = 0; i < count; ++i ) {
        node = config_props->getChild(i);
        string name = node->getName();
        // cout << name << endl;
        if ( name == "wpt" ) {
            SGWayPoint wpt( node );
            standby->add_waypoint( wpt );
	} else if ( name == "enable" ) {
	    // happily ignore this
        } else {
            printf("Unknown top level section: %s\n", name.c_str() );
            return false;
        }
    }

    printf("loaded %d waypoints\n", standby->size());

    return true;
}


int FGRouteMgr::new_waypoint( const string& wpt_string )
{
    SGWayPoint wp = make_waypoint( wpt_string );
    standby->add_waypoint( wp );
    return 1;
}


int FGRouteMgr::new_waypoint( const double field1, const double field2,
			      const int mode )
{
    if ( mode == 0 ) {
        // relative waypoint
	SGWayPoint wp( 0.0, 0.0, -9999.0, -9999.0, 0.0, 0.0, 0.0,
		       SGWayPoint::SPHERICAL, "" );
	standby->add_waypoint( wp );
    } else if ( mode == 1 ) {
	// absolute waypoint
	SGWayPoint wp( field1, field1, -9999.0, -9999.0, 0.0, field2, field1,
		       SGWayPoint::SPHERICAL, "" );
	standby->add_waypoint( wp );
    }

    return 1;
}


SGWayPoint FGRouteMgr::make_waypoint( const string& wpt_string ) {
    string target = wpt_string;
    double lon = 0.0;
    double lat = 0.0;
    double alt_m = -9999.0;
    double agl_m = -9999.0;
    double speed_kt = 0.0;

    // WARNING: this routine doesn't have any way to handle AGL
    // altitudes.  Nor can it handle any offset heading/dist requests

    // extract altitude
    size_t pos = target.find( '@' );
    if ( pos != string::npos ) {
        alt_m = atof( target.c_str() + pos + 1 ) * SG_FEET_TO_METER;
        target = target.substr( 0, pos );
    }

    // check for lon,lat
    pos = target.find( ',' );
    if ( pos != string::npos ) {
        lon = atof( target.substr(0, pos).c_str());
        lat = atof( target.c_str() + pos + 1);
    }

    printf("Adding waypoint lon = %.6f lat = %.6f alt_m = %.0f\n",
           lon, lat, alt_m);
    SGWayPoint wp( lon, lat, alt_m, agl_m, speed_kt, 0.0, 0.0,
                   SGWayPoint::SPHERICAL, "" );

    return wp;
}


bool FGRouteMgr::reposition_pattern( const SGWayPoint &wp, const double hdg )
{
    // sanity check
    if ( fabs(wp.get_target_lon() > 0.0001)
	 || fabs(wp.get_target_lat() > 0.0001) )
    {
	// good location
	active->refresh_offset_positions( wp, hdg );
	if ( display_on ) {
	    printf( "ROUTE pattern updated: %.6f %.6f (course = %.1f)\n",
		    wp.get_target_lon(), wp.get_target_lat(),
		    hdg );
	}
	return true;
    } else {
	// bogus location, ignore ...
	return false;
    }
}
