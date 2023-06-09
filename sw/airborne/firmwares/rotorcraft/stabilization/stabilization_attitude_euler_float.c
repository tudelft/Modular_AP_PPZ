/*
 * Copyright (C) 2008-2009 Antoine Drouin <poinix@gmail.com>
 *
 * This file is part of paparazzi.
 *
 * paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with paparazzi; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * @file stabilization_attitude_euler_float.c
 *
 * Rotorcraft attitude stabilization in euler float version.
 */

#include "generated/airframe.h"

#include "firmwares/rotorcraft/stabilization/stabilization_attitude.h"
#include "firmwares/rotorcraft/stabilization/stabilization_attitude_rc_setpoint.h"

#include "std.h"
#include "paparazzi.h"
#include "math/pprz_algebra_float.h"
#include "state.h"

#ifndef USE_ATT_REF
#define USE_ATT_REF 1
#endif

struct FloatAttitudeGains stabilization_gains;
struct FloatEulers stabilization_att_sum_err;

struct FloatEulers stab_att_sp_euler;
struct AttRefEulerFloat att_ref_euler_f;

float stabilization_att_fb_cmd[COMMANDS_NB];
float stabilization_att_ff_cmd[COMMANDS_NB];

#if PERIODIC_TELEMETRY
#include "modules/datalink/telemetry.h"

static void send_att(struct transport_tx *trans, struct link_device *dev)
{
  struct FloatRates *body_rate = stateGetBodyRates_f();
  struct FloatEulers *att = stateGetNedToBodyEulers_f();
  float foo = 0.0;
  pprz_msg_send_STAB_ATTITUDE_FLOAT(trans, dev, AC_ID,
                                    &(body_rate->p), &(body_rate->q), &(body_rate->r),
                                    &(att->phi), &(att->theta), &(att->psi),
                                    &stab_att_sp_euler.phi,
                                    &stab_att_sp_euler.theta,
                                    &stab_att_sp_euler.psi,
                                    &stabilization_att_sum_err.phi,
                                    &stabilization_att_sum_err.theta,
                                    &stabilization_att_sum_err.psi,
                                    &stabilization_att_fb_cmd[COMMAND_ROLL],
                                    &stabilization_att_fb_cmd[COMMAND_PITCH],
                                    &stabilization_att_fb_cmd[COMMAND_YAW],
                                    &stabilization_att_ff_cmd[COMMAND_ROLL],
                                    &stabilization_att_ff_cmd[COMMAND_PITCH],
                                    &stabilization_att_ff_cmd[COMMAND_YAW],
                                    &stabilization_cmd[COMMAND_ROLL],
                                    &stabilization_cmd[COMMAND_PITCH],
                                    &stabilization_cmd[COMMAND_YAW],
                                    &foo, &foo, &foo);
}

static void send_att_ref(struct transport_tx *trans, struct link_device *dev)
{
  pprz_msg_send_STAB_ATTITUDE_REF_FLOAT(trans, dev, AC_ID,
                                        &stab_att_sp_euler.phi,
                                        &stab_att_sp_euler.theta,
                                        &stab_att_sp_euler.psi,
                                        &att_ref_euler_f.euler.phi,
                                        &att_ref_euler_f.euler.theta,
                                        &att_ref_euler_f.euler.psi,
                                        &att_ref_euler_f.rate.p,
                                        &att_ref_euler_f.rate.q,
                                        &att_ref_euler_f.rate.r,
                                        &att_ref_euler_f.accel.p,
                                        &att_ref_euler_f.accel.q,
                                        &att_ref_euler_f.accel.r);
}
#endif

void stabilization_attitude_init(void)
{

  attitude_ref_euler_float_init(&att_ref_euler_f);

  VECT3_ASSIGN(stabilization_gains.p,
               STABILIZATION_ATTITUDE_PHI_PGAIN,
               STABILIZATION_ATTITUDE_THETA_PGAIN,
               STABILIZATION_ATTITUDE_PSI_PGAIN);

  VECT3_ASSIGN(stabilization_gains.d,
               STABILIZATION_ATTITUDE_PHI_DGAIN,
               STABILIZATION_ATTITUDE_THETA_DGAIN,
               STABILIZATION_ATTITUDE_PSI_DGAIN);

  VECT3_ASSIGN(stabilization_gains.i,
               STABILIZATION_ATTITUDE_PHI_IGAIN,
               STABILIZATION_ATTITUDE_THETA_IGAIN,
               STABILIZATION_ATTITUDE_PSI_IGAIN);

  VECT3_ASSIGN(stabilization_gains.dd,
               STABILIZATION_ATTITUDE_PHI_DDGAIN,
               STABILIZATION_ATTITUDE_THETA_DDGAIN,
               STABILIZATION_ATTITUDE_PSI_DDGAIN);

  FLOAT_EULERS_ZERO(stabilization_att_sum_err);

#if PERIODIC_TELEMETRY
  register_periodic_telemetry(DefaultPeriodic, PPRZ_MSG_ID_STAB_ATTITUDE_FLOAT, send_att);
  register_periodic_telemetry(DefaultPeriodic, PPRZ_MSG_ID_STAB_ATTITUDE_REF_FLOAT, send_att_ref);
#endif
}

void stabilization_attitude_read_rc(bool in_flight, bool in_carefree, bool coordinated_turn)
{
  stabilization_attitude_read_rc_setpoint_eulers_f(&stab_att_sp_euler, in_flight, in_carefree, coordinated_turn);
}

void stabilization_attitude_enter(void)
{

  /* reset psi setpoint to current psi angle */
  stab_att_sp_euler.psi = stabilization_attitude_get_heading_f();

  attitude_ref_euler_float_enter(&att_ref_euler_f, stab_att_sp_euler.psi);

  FLOAT_EULERS_ZERO(stabilization_att_sum_err);
}

void stabilization_attitude_set_failsafe_setpoint(void)
{
  stab_att_sp_euler.phi = 0.0;
  stab_att_sp_euler.theta = 0.0;
  stab_att_sp_euler.psi = stateGetNedToBodyEulers_f()->psi;
}

void stabilization_attitude_set_rpy_setpoint_i(struct Int32Eulers *rpy)
{
  EULERS_FLOAT_OF_BFP(stab_att_sp_euler, *rpy);
}

void stabilization_attitude_set_quat_setpoint_i(struct Int32Quat *quat)
{
  struct FloatQuat quat_f;
  QUAT_FLOAT_OF_BFP(quat_f, *quat);
  float_eulers_of_quat(&stab_att_sp_euler, &quat_f);
}

void stabilization_attitude_set_earth_cmd_i(struct Int32Vect2 *cmd, int32_t heading)
{
  struct FloatVect2 cmd_f;
  cmd_f.x = ANGLE_FLOAT_OF_BFP(cmd->x);
  cmd_f.y = ANGLE_FLOAT_OF_BFP(cmd->y);

  /* Rotate horizontal commands to body frame by psi */
  float psi = stateGetNedToBodyEulers_f()->psi;
  float s_psi = sinf(psi);
  float c_psi = cosf(psi);
  stab_att_sp_euler.phi = -s_psi * cmd_f.x + c_psi * cmd_f.y;
  stab_att_sp_euler.theta = -c_psi * cmd_f.x - s_psi * cmd_f.y;
  stab_att_sp_euler.psi = ANGLE_FLOAT_OF_BFP(heading);
}

void stabilization_attitude_set_stab_sp(struct StabilizationSetpoint *sp)
{
  stab_att_sp_euler = stab_sp_to_eulers_f(sp);
}

#define MAX_SUM_ERR 200

void stabilization_attitude_run(bool  in_flight)
{

#if USE_ATT_REF
  static const float dt = (1./PERIODIC_FREQUENCY);
  attitude_ref_euler_float_update(&att_ref_euler_f, &stab_att_sp_euler, dt);
#else
  EULERS_COPY(att_ref_euler_f.euler, stab_att_sp_euler);
  FLOAT_RATES_ZERO(att_ref_euler_f.rate);
  FLOAT_RATES_ZERO(att_ref_euler_f.accel);
#endif

  /* Compute feedforward */
  stabilization_att_ff_cmd[COMMAND_ROLL] =
    stabilization_gains.dd.x * att_ref_euler_f.accel.p;
  stabilization_att_ff_cmd[COMMAND_PITCH] =
    stabilization_gains.dd.y * att_ref_euler_f.accel.q;
  stabilization_att_ff_cmd[COMMAND_YAW] =
    stabilization_gains.dd.z * att_ref_euler_f.accel.r;

  /* Compute feedback                  */
  /* attitude error            */
  struct FloatEulers *att_float = stateGetNedToBodyEulers_f();
  struct FloatEulers att_err;
  EULERS_DIFF(att_err, att_ref_euler_f.euler, *att_float);
  FLOAT_ANGLE_NORMALIZE(att_err.psi);

  if (in_flight) {
    /* update integrator */
    EULERS_ADD(stabilization_att_sum_err, att_err);
    EULERS_BOUND_CUBE(stabilization_att_sum_err, -MAX_SUM_ERR, MAX_SUM_ERR);
  } else {
    FLOAT_EULERS_ZERO(stabilization_att_sum_err);
  }

  /*  rate error                */
  struct FloatRates *rate_float = stateGetBodyRates_f();
  struct FloatRates rate_err;
  RATES_DIFF(rate_err, att_ref_euler_f.rate, *rate_float);

  /*  PID                  */

  stabilization_att_fb_cmd[COMMAND_ROLL] =
    stabilization_gains.p.x  * att_err.phi +
    stabilization_gains.d.x  * rate_err.p +
    stabilization_gains.i.x  * stabilization_att_sum_err.phi;

  stabilization_att_fb_cmd[COMMAND_PITCH] =
    stabilization_gains.p.y  * att_err.theta +
    stabilization_gains.d.y  * rate_err.q +
    stabilization_gains.i.y  * stabilization_att_sum_err.theta;

  stabilization_att_fb_cmd[COMMAND_YAW] =
    stabilization_gains.p.z  * att_err.psi +
    stabilization_gains.d.z  * rate_err.r +
    stabilization_gains.i.z  * stabilization_att_sum_err.psi;


  stabilization_cmd[COMMAND_ROLL] =
    (stabilization_att_fb_cmd[COMMAND_ROLL] + stabilization_att_ff_cmd[COMMAND_ROLL]);
  stabilization_cmd[COMMAND_PITCH] =
    (stabilization_att_fb_cmd[COMMAND_PITCH] + stabilization_att_ff_cmd[COMMAND_PITCH]);
  stabilization_cmd[COMMAND_YAW] =
    (stabilization_att_fb_cmd[COMMAND_YAW] + stabilization_att_ff_cmd[COMMAND_YAW]);

  /* bound the result */
  BoundAbs(stabilization_cmd[COMMAND_ROLL], MAX_PPRZ);
  BoundAbs(stabilization_cmd[COMMAND_PITCH], MAX_PPRZ);
  BoundAbs(stabilization_cmd[COMMAND_YAW], MAX_PPRZ);
}
