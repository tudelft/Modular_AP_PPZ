/*
 * Copyright (C) Michaël Ozo
 *
 * This file is part of paparazzi
 *
 */
/**
 * @file "modules/stereocam/stereo_gate_position/stereo_gate_position.c"
 * @author Michaël Ozo
 * Stereo board gate detection is fused with optic flow velocity.
 */

#include <math.h>
#include <sys/time.h>
#include "subsystems/datalink/telemetry.h"
#include "modules/stereocam/stereo_gate_position/stereo_gate_position.h"
#include "state.h"

#define PI 3.1415926

#define GOOD_FIT 8


void stereocam_to_state(void);

char x_center = 0;
char y_center = 0;
char radius   = 0;
char fitness  = 0;
char fps      = 0;

float measured_x_gate = 0;
float measured_y_gate = 0;
float measured_z_gate = 0;

float predicted_x_gate = 0;
float predicted_y_gate = 0;

float current_x_gate = 0;
float current_y_gate = 0;

float previous_x_gate = 0;
float previous_y_gate = 0;
// Settings:
float FOV_width = 57.4f;
float FOV_height = 44.5f;
float gate_size_meters = 1.0f;

int uncertainty_gate = 0;


struct timeval stop, start;




float deg2rad(float deg)
{
	return ((deg * PI) / 180.0f);
}

static void stereo_gate_send(struct transport_tx *trans, struct link_device *dev)
    {
    pprz_msg_send_STEREO_GATE_INFO(trans, dev, AC_ID,&x_center, &y_center,&radius,&fitness,&fps,&current_x_gate,&current_y_gate,&measured_z_gate);
    }  

 void stereo_gate_position_init(void)
 {
   register_periodic_telemetry(DefaultPeriodic, PPRZ_MSG_ID_STEREO_GATE_INFO, stereo_gate_send);
   gettimeofday(&start, NULL);
 }
 
 void get_stereo_data_periodic(void) 
 {
   if (stereocam_data.fresh) {
	  stereocam_to_state();
	  y_center+=1;
    stereocam_data.fresh = 0;
  }
 }
 


void stereocam_to_state(void)
{
  //message of length 5 with the x_center (image coord), y_center, radius, fitness (<4 is good, > 10 bad),
  //and frame rate of the calculations.
  
	x_center = stereocam_data.data[0];
	y_center = stereocam_data.data[1];
	radius   = stereocam_data.data[2];
	fitness  = stereocam_data.data[3];
	fps      = stereocam_data.data[4];
  
  // Determine the measurement:
	float alpha = (radius / 128.0f) * FOV_width;
	float measured_distance_gate = (0.5f * gate_size_meters) / tan(deg2rad(alpha));
	float measured_angle_gate = ((x_center - 64.0f) / (128.0f)) * FOV_width;
	float measured_angle_vert = ((y_center - 48.0f) / (96.0f)) * FOV_height;
	
	measured_x_gate = measured_distance_gate * sin(deg2rad(measured_angle_gate));
	measured_y_gate = measured_distance_gate * cos(deg2rad(measured_angle_gate));
	measured_z_gate = measured_distance_gate * sin(deg2rad(measured_angle_vert));
	
  
  //State filter 
	

	//convert earth velocity to body x y velocity
	float v_x_earth =stateGetSpeedNed_f()->x;
	float v_y_earth = stateGetSpeedNed_f()->y;
	float psi = stateGetNedToBodyEulers_f()->psi;
	float body_v_x = cosf(psi)*v_x_earth + sinf(psi)*v_y_earth;
	float body_v_y = -sinf(psi)*v_x_earth+cosf(psi)*v_y_earth;
	
	
	
	gettimeofday(&stop, NULL);
	printf("took %lu\n", stop.tv_usec - start.tv_usec);
	double dt = (stop.tv_usec - start.tv_usec)/1000000;
	gettimeofday(&start, NULL);
	
    // predict the new location:
	float dx_gate = dt * body_v_x;//(cos(current_angle_gate) * gate_turn_rate * current_distance);
	float dy_gate = dt * body_v_y; //(velocity_gate - sin(current_angle_gate) * gate_turn_rate * current_distance);
	predicted_x_gate = previous_x_gate + dx_gate;
	predicted_y_gate = previous_y_gate + dy_gate;
	
  if (fitness < GOOD_FIT)
	{
	
		// Mix the measurement with the prediction:
		float weight_measurement;
		if (uncertainty_gate > 30)
			weight_measurement = 1.0f;
		else
			weight_measurement = (8-fitness)/8.0;

		current_x_gate = weight_measurement * measured_x_gate + (1.0f - weight_measurement) * predicted_x_gate;
		current_y_gate = weight_measurement * measured_y_gate + (1.0f - weight_measurement) * predicted_y_gate;
		

		// reset uncertainty:
		uncertainty_gate = 0;
	}
	else
	{
		// just the prediction
		current_x_gate = predicted_x_gate;
		current_y_gate = predicted_y_gate;

		// increase uncertainty
		uncertainty_gate++;
	}
	// set the previous state for the next time:
	previous_x_gate = current_x_gate;
	previous_y_gate = current_y_gate;
  
}

