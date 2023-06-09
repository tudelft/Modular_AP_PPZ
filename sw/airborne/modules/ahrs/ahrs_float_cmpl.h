/*
 * Copyright (C) 2010 The Paparazzi Team
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
 * @file modules/ahrs/ahrs_float_cmpl.h
 *
 * Complementary filter in float to estimate the attitude, heading and gyro bias.
 *
 * Propagation can be done in rotation matrix or quaternion representation.
 */

#ifndef AHRS_FLOAT_CMPL_H
#define AHRS_FLOAT_CMPL_H

#include "std.h"
#include "math/pprz_algebra_float.h"
#include "math/pprz_orientation_conversion.h"
#include "modules/gps/gps.h"

enum AhrsFCStatus {
  AHRS_FC_UNINIT,
  AHRS_FC_RUNNING
};

struct AhrsFloatCmpl {
  struct FloatRates gyro_bias;
  struct FloatRates rate_correction;
  struct FloatRates body_rate;
  struct FloatQuat ltp_to_body_quat;
  struct FloatRMat ltp_to_body_rmat;

  bool correct_gravity; ///< enable gravity correction during coordinated turns
  float ltp_vel_norm; ///< velocity norm for gravity correction during coordinated turns
  bool ltp_vel_norm_valid;

  float accel_omega;  ///< filter cut-off frequency for correcting the attitude from accels (pseudo-gravity measurement)
  float accel_zeta;   ///< filter damping for correcting the gyro-bias from accels (pseudo-gravity measurement)
  float mag_omega;    ///< filter cut-off frequency for correcting the attitude (heading) from magnetometer
  float mag_zeta;     ///< filter damping for correcting the gyro bias from magnetometer

  /** sets how strongly the gravity heuristic reduces accel correction.
   * Set to zero in order to disable gravity heuristic.
   */
  uint8_t gravity_heuristic_factor;
  float weight;

  bool heading_aligned;
  struct FloatVect3 mag_h;

  /* internal counters for the gains */
  uint16_t accel_cnt; ///< number of propagations since last accel update
  uint16_t mag_cnt;   ///< number of propagations since last mag update

  enum AhrsFCStatus status;
  bool is_aligned;
};

extern struct AhrsFloatCmpl ahrs_fc;

extern void ahrs_fc_init(void);
extern bool ahrs_fc_align(struct FloatRates *lp_gyro, struct FloatVect3 *lp_accel,
                            struct FloatVect3 *lp_mag);
extern void ahrs_fc_propagate(struct FloatRates *gyro, float dt);
extern void ahrs_fc_update_accel(struct FloatVect3 *accel, float dt);
extern void ahrs_fc_update_mag(struct FloatVect3 *mag, float dt);
extern void ahrs_fc_update_gps(struct GpsState *gps_s);

/** Update yaw based on a heading measurement.
 * e.g. from GPS course
 * @param heading Heading in body frame, radians (CW/north)
 */
void ahrs_fc_update_heading(float heading);

/** Hard reset yaw to a heading.
 * Doesn't affect the bias.
 * Sets ahrs_fc.heading_aligned to TRUE.
 * @param heading Heading in body frame, radians (CW/north)
 */
void ahrs_fc_realign_heading(float heading);


#endif /* AHRS_FLOAT_CMPL_H */
