/******************************************************************************
 * FILE: control.cpp
 * DESCRIPTION: high level control/autopilot interface
 *
 ******************************************************************************/

#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "comms/display.h"
#include "comms/logging.h"
#include "comms/packetizer.hxx"
#include "comms/remote_link.h"
#include "include/globaldefs.h"
#include "main/globals.hxx"
#include "mission/mission_mgr.hxx"
#include "mission/tasks/task_home_mgr.hxx"
#include "mission/tasks/task_route.hxx"

#include "include/util.h"
#include "xmlauto.hxx"

#include "control.h"


//
// global variables
//

// the "FlightGear" autopilot
static FGXMLAutopilot ap;


// autopilot control properties
static SGPropertyNode *ap_master_switch_node = NULL;
static SGPropertyNode *fcs_mode_node = NULL;
//static SGPropertyNode *ap_heading_mode_node = NULL;
//static SGPropertyNode *ap_roll_mode_node = NULL;
//static SGPropertyNode *ap_yaw_mode_node = NULL;
//static SGPropertyNode *ap_altitude_mode_node = NULL;
//static SGPropertyNode *ap_speed_mode_node = NULL;
//static SGPropertyNode *ap_pitch_mode_node = NULL;

static SGPropertyNode *heading_lock_node = NULL;
static SGPropertyNode *roll_lock_node = NULL;
static SGPropertyNode *yaw_lock_node = NULL;
static SGPropertyNode *altitude_lock_node = NULL;
static SGPropertyNode *speed_lock_node = NULL;
static SGPropertyNode *pitch_lock_node = NULL;
static SGPropertyNode *pointing_lock_node = NULL;

static SGPropertyNode *lookat_mode_node = NULL;
static SGPropertyNode *ned_n_node = NULL;
static SGPropertyNode *ned_e_node = NULL;
static SGPropertyNode *ned_d_node = NULL;

static SGPropertyNode *roll_deg_node = NULL;
static SGPropertyNode *pitch_deg_node = NULL;
static SGPropertyNode *cur_speed_node = NULL;
static SGPropertyNode *initial_speed_node = NULL;
static SGPropertyNode *target_roll_deg_node = NULL;
static SGPropertyNode *target_pitch_base_deg_node = NULL;
static SGPropertyNode *target_speed_node = NULL;

// console/logging property nodes
static SGPropertyNode *ap_console_skip = NULL;
static SGPropertyNode *ap_logging_skip = NULL;


static void bind_properties() {
    ap_master_switch_node = fgGetNode("/autopilot/master-switch", true);
    fcs_mode_node = fgGetNode("/config/fcs/mode", true);
    // ap_heading_mode_node = fgGetNode("/autopilot/heading-mode", true);
    // ap_roll_mode_node = fgGetNode("/autopilot/roll-mode", true);
    // ap_yaw_mode_node = fgGetNode("/autopilot/yaw-mode", true);
    // ap_altitude_mode_node = fgGetNode("/autopilot/altitude-mode", true);
    // ap_speed_mode_node = fgGetNode("/autopilot/speed-mode", true);
    // ap_pitch_mode_node = fgGetNode("/autopilot/pitch-mode", true);

    heading_lock_node = fgGetNode("/autopilot/locks/heading", true);
    roll_lock_node = fgGetNode("/autopilot/locks/roll", true);
    yaw_lock_node = fgGetNode("/autopilot/locks/yaw", true);
    altitude_lock_node = fgGetNode("/autopilot/locks/altitude", true);
    speed_lock_node = fgGetNode("/autopilot/locks/speed", true);
    pitch_lock_node = fgGetNode("/autopilot/locks/pitch", true);
    pointing_lock_node = fgGetNode("/autopilot/locks/pointing", true);

    lookat_mode_node = fgGetNode("/pointing/lookat-mode", true);
    ned_n_node = fgGetNode("/pointing/vector/north", true);
    ned_e_node = fgGetNode("/pointing/vector/east", true);
    ned_d_node = fgGetNode("/pointing/vector/down", true);

    roll_deg_node = fgGetNode("/orientation/roll-deg", true);
    pitch_deg_node = fgGetNode("/orientation/pitch-deg", true);
    cur_speed_node = fgGetNode("/velocity/airspeed-kt", true);
    initial_speed_node = fgGetNode("/config/fcs/initial-speed-kt", true);
    target_roll_deg_node
	= fgGetNode("/autopilot/settings/target-roll-deg", true);
    target_pitch_base_deg_node
	= fgGetNode("/autopilot/settings/target-pitch-base-deg", true);
    target_speed_node = fgGetNode("/autopilot/settings/target-speed-kt", true);

    ap_console_skip = fgGetNode("/config/remote-link/autopilot-skip", true);
    ap_logging_skip = fgGetNode("/config/logging/autopilot-skip", true);
}


void control_init() {
    // initialize the autopilot class and build the structures from the
    // configuration file values

    bind_properties();

    // initialize and build the autopilot controller from the property
    // tree config (/config/fcs/autopilot)
    ap.init();

    if ( display_on ) {
	printf("Autopilot initialized\n");
    }
}


void control_reinit() {
    // reread autopilot configuration from the property tree and reset
    // all stages (i.e. real time gain tuning)

    ap.reinit();
}


void control_update(double dt)
{
    static string last_fcs_mode = "";

    string fcs_mode = fcs_mode_node->getStringValue();

    if ( ap_master_switch_node->getBoolValue() ) {
	if ( last_fcs_mode != fcs_mode ) {
	    if ( event_log_on ) {
		event_log( "control mode changed to:", fcs_mode.c_str() );
	    }

	    // set default target speed if not overridden by operator
	    float target_speed_kt = target_speed_node->getFloatValue();
	    float initial_speed_kt = initial_speed_node->getFloatValue();
	    if ( target_speed_kt < 0.1 ) {
		target_speed_node->setFloatValue( initial_speed_kt );
	    }

	    // turn on pointing (universally for now)
	    pointing_lock_node->setStringValue( "on" );
	    lookat_mode_node->setStringValue( "ned-vector" );
	    ned_n_node->setFloatValue( 0.0 );
	    ned_e_node->setFloatValue( 0.0 );
	    ned_d_node->setFloatValue( 1.0 );

	    if ( fcs_mode == "basic" ) {
		// set lock modes for "basic" inner loops only
		heading_lock_node->setStringValue( "" );
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "" );
		altitude_lock_node->setStringValue( "" );
		speed_lock_node->setStringValue( "throttle" );
		pitch_lock_node->setStringValue( "elevator" );

	    } else if ( fcs_mode == "basic+alt" ) {
		// set lock modes for "basic" + alt hold
		heading_lock_node->setStringValue( "" );
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "turn-coord" );
		altitude_lock_node->setStringValue( "pitch" );
		speed_lock_node->setStringValue( "throttle" );
		pitch_lock_node->setStringValue( "elevator" );
	    } else if ( fcs_mode == "basic+alt+nav" ) {
		// set lock modes for "basic" + alt hold + navigation
		heading_lock_node->setStringValue( "route" );
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "turn-coord" );
		altitude_lock_node->setStringValue( "pitch" );
		speed_lock_node->setStringValue( "throttle" );
		pitch_lock_node->setStringValue( "elevator" );
	    } else if ( fcs_mode == "cas" ) {
		// set lock modes for "cas"
		heading_lock_node->setStringValue( "" );
		roll_lock_node->setStringValue( "aileron" );
		yaw_lock_node->setStringValue( "" );
		altitude_lock_node->setStringValue( "" );
		speed_lock_node->setStringValue( "" );
		pitch_lock_node->setStringValue( "elevator" );
		pointing_lock_node->setStringValue( "on" );

		float target_roll_deg = roll_deg_node->getFloatValue();
		if ( target_roll_deg > 45.0 ) { target_roll_deg = 45.0; }
		if ( target_roll_deg < -45.0 ) { target_roll_deg = -45.0; }
		target_roll_deg_node->setFloatValue( target_roll_deg );

		float target_pitch_base_deg = pitch_deg_node->getFloatValue();
		if ( target_pitch_base_deg > 15.0 ) {
		    target_pitch_base_deg = 15.0;
		}
		if ( target_pitch_base_deg < -15.0 ) {
		    target_pitch_base_deg = -15.0;
		}
		target_pitch_base_deg_node->setFloatValue( target_pitch_base_deg );
	    }
	}
	last_fcs_mode = fcs_mode;
    } else {
	if ( fcs_mode != "" ) {
	    // autopilot is just de-activated, clear lock modes
	    heading_lock_node->setStringValue( "" );
	    roll_lock_node->setStringValue( "" );
	    yaw_lock_node->setStringValue( "" );
	    altitude_lock_node->setStringValue( "" );
	    speed_lock_node->setStringValue( "" );
	    pitch_lock_node->setStringValue( "" );
	    pointing_lock_node->setStringValue( "" );
	}
	last_fcs_mode = "";
    }

    if ( fcs_mode != "" ) {
	// update the autopilot stages
	ap.update( dt );
    }

    if ( remote_link_on || log_to_file ) {
	// send one waypoint per message, then home location (with
	// index = 65535)

	static int wp_index = 0;
	int index = 0;
	SGWayPoint wp;
	int route_size = 0;

	UGTaskRoute *route_task
	    = (UGTaskRoute *)mission_mgr.find_seq_task( "route" );
	if ( route_task != NULL ) {
	    FGRouteMgr *route_mgr = route_task->get_route_mgr();
	    if ( route_mgr != NULL ) {
		route_size = route_mgr->size();
		route_task->reposition_if_necessary();
		if ( route_size > 0 && wp_index < route_size ) {
		    wp = route_mgr->get_waypoint( wp_index );
		    index = wp_index;
		}
	    }
	}

        // special case send home as a route waypoint with id = 65535
        if ( wp_index == route_size ) {
	    UGTaskHomeMgr *home_mgr
		= (UGTaskHomeMgr *)mission_mgr.find_global_task( "home-manager" );
	    if ( home_mgr != NULL ) {
		wp = home_mgr->get_home_wpt();
		index = 65535;
	    }
	}

	uint8_t buf[256];
	int pkt_size = packetizer->packetize_ap( buf, route_size, &wp, index );
	
	if ( remote_link_on ) {
	    bool result = remote_link_ap( buf, pkt_size,
					  ap_console_skip->getIntValue() );
	    if ( result ) {
		wp_index++;
		if ( wp_index > route_size ) {
		    wp_index = 0;
		}
	    }
	}

	if ( log_to_file ) {
	    log_ap( buf, pkt_size, ap_logging_skip->getIntValue() );
	}
    }
}


void control_close() {
  // nothing to see here, move along ...
}
