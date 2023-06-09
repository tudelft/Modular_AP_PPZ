/*
 * Copyright (C) 2015 Freek van Tienen <freek.v.tienen@gmail.com>
 * Copyright (C) 2022 Gautier Hattenberger <gautier.hattenberger@enac.fr>
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
 * along with paparazzi; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 */

/** @file modules/intermcu/intermcu_fbw.c
 *  @brief Inter-MCU on FlyByWire side
 */

#include "modules/intermcu/intermcu_fbw.h"
#include "main_fbw.h"
#include "pprzlink/intermcu_msg.h"
#include "generated/modules.h"
#include "modules/radio_control/radio_control.h"
#include "modules/energy/electrical.h"
#if TELEMETRY_INTERMCU
#include "modules/datalink/intermcu_dl.h"
#endif
#include "modules/datalink/datalink.h"
#include "modules/core/abi.h"


#include "modules/spektrum_soft_bind/spektrum_soft_bind_fbw.h"

#include BOARD_CONFIG
#ifdef BOARD_PX4IO
#include "libopencm3/cm3/scb.h"
#include "led.h"
#include "mcu_periph/sys_time.h"
static uint8_t px4RebootSequence[] = {0x41, 0xd7, 0x32, 0x0a, 0x46, 0x39};
static uint8_t px4RebootSequenceCount = 0;
tid_t px4bl_tid; ///< id for time out of the px4 bootloader reset
#endif

#if RADIO_CONTROL_NB_CHANNEL > 8
//#undef RADIO_CONTROL_NB_CHANNEL
//#define RADIO_CONTROL_NB_CHANNEL 8
INFO("InterMCU UART will only send 8 radio channels!")
#endif

/* Main InterMCU structure */
struct intermcu_t intermcu = {
  .device = (&((INTERMCU_LINK).device)),
  .enabled = true,
  .msg_available = false
};
uint8_t imcu_msg_buf[256] __attribute__((aligned));  ///< The InterMCU message buffer

pprz_t intermcu_commands[COMMANDS_NB];
bool intermcu_ap_motors_on = false;


#ifdef BOARD_PX4IO
static void checkPx4RebootCommand(unsigned char b);
#endif

// ABI callback for radio control
#ifndef IMCU_RADIO_CONTROL_ID
#define IMCU_RADIO_CONTROL_ID ABI_BROADCAST
#endif
static abi_event rc_ev;
static void rc_cb(uint8_t sender_id, struct RadioControl *rc);

// ABI callback for GPS
#ifdef USE_GPS
#include "modules/gps/gps.h"
#ifndef IMCU_GPS_ID
#define IMCU_GPS_ID GPS_MULTI_ID
#endif
static abi_event gps_ev;
static void gps_cb(uint8_t sender_id, uint32_t stamp, struct GpsState *gps_s);
#endif

void intermcu_init(void)
{
  pprz_transport_init(&intermcu.transport);

  AbiBindMsgRADIO_CONTROL(IMCU_RADIO_CONTROL_ID, &rc_ev, rc_cb);
#if USE_GPS
  AbiBindMsgGPS(IMCU_GPS_ID, &gps_ev, gps_cb);
#endif

#ifdef BOARD_PX4IO
  px4bl_tid = sys_time_register_timer(20.0, NULL); //bootloader time out. After this intermcu will be set to slow baud.
#endif
}

void intermcu_periodic(void)
{
  /* Check for interMCU loss */
  if (intermcu.time_since_last_frame >= INTERMCU_LOST_CNT) {
    intermcu.status = INTERMCU_LOST;
  } else {
    intermcu.time_since_last_frame++;
  }
}

void intermcu_event(void)
{
  uint8_t i, c;

  // Check if there are messages in the device
  if (intermcu.device->char_available(intermcu.device->periph)) {
    while (intermcu.device->char_available(intermcu.device->periph) && !intermcu.transport.trans_rx.msg_received) {
      c = intermcu.device->get_byte(intermcu.device->periph);
      parse_pprz(&intermcu.transport, c);

#ifdef BOARD_PX4IO
      // TODO: create a hook
      checkPx4RebootCommand(c);
#endif
    }

    // If we have a message copy it
    if (intermcu.transport.trans_rx.msg_received) {
      for (i = 0; i < intermcu.transport.trans_rx.payload_len; i++) {
        imcu_msg_buf[i] = intermcu.transport.trans_rx.payload[i];
      }

      intermcu.msg_available = true;
      intermcu.transport.trans_rx.msg_received = false;
    }
  }

  if (intermcu.msg_available) {
    uint8_t class_id = pprzlink_get_msg_class_id(imcu_msg_buf);
    if (class_id == DL_intermcu_CLASS_ID) {
      // parse intermcu messages and call callbacks
      dl_parse_msg(intermcu.device, &intermcu.transport.trans_tx, imcu_msg_buf);
#if TELEMETRY_INTERMCU
    } else {
      // forward all other messages if needed
      intermcu_dl_on_msg(imcu_msg_buf, intermcu.transport.trans_rx.payload_len);
#endif
    }
    intermcu.msg_available = false;
  }
}

void intermcu_send_status(void)
{
#ifdef RADIO_CONTROL
  uint8_t rc_status = radio_control.status;
  uint8_t rc_rate = radio_control.frame_rate;
#else
  uint8_t rc_status = 0;
  uint8_t rc_rate = 0;
#endif
  // Send Status
  pprz_msg_send_IMCU_FBW_STATUS(&(intermcu.transport.trans_tx), intermcu.device, AC_ID,
                                &fbw_mode, &rc_status, &rc_rate,
                                &electrical.vsupply, &electrical.current);
}

void intermcu_parse_IMCU_COMMANDS(uint8_t *buf)
{
  uint8_t size = DL_IMCU_COMMANDS_values_length(buf);
  int16_t *new_commands = DL_IMCU_COMMANDS_values(buf);
  intermcu.cmd_status |= DL_IMCU_COMMANDS_status(buf);

  // Read the autopilot status and then clear it
  intermcu_ap_motors_on = INTERMCU_GET_CMD_STATUS(INTERMCU_CMD_MOTORS_ON);
  INTERMCU_CLR_CMD_STATUS(INTERMCU_CMD_MOTORS_ON);

  for (int i = 0; i < size; i++) {
    intermcu_commands[i] = new_commands[i];
  }

  intermcu.status = INTERMCU_OK;
  intermcu.time_since_last_frame = 0;
}

void intermcu_parse_IMCU_SPEKTRUM_SOFT_BIND(uint8_t *buf __attribute__((unused)))
{
#if defined(SPEKTRUM_HAS_SOFT_BIND_PIN)
  received_spektrum_soft_bind();
#endif
}

void intermcu_forward_uplink(uint8_t *buf __attribute__((unused)))
{
  // forward all incoming messages to intermcu for AP side
#if TELEMETRY_INTERMCU
  uint8_t size = (DOWNLINK_TRANSPORT).trans_rx.payload_len; // FIXME is it always a valid length ?
  intermcu_dl_repack(&(intermcu.transport.trans_tx), intermcu.device, buf, size);
#endif
}

static void rc_cb(uint8_t sender_id __attribute__((unused)), struct RadioControl *rc __attribute__((unused)))
{
#if RADIO_CONTROL
  pprz_t  values[9];

  values[INTERMCU_RADIO_THROTTLE] = radio_control.values[RADIO_THROTTLE];
  values[INTERMCU_RADIO_ROLL] = radio_control.values[RADIO_ROLL];
  values[INTERMCU_RADIO_PITCH] = radio_control.values[RADIO_PITCH];
  values[INTERMCU_RADIO_YAW] = radio_control.values[RADIO_YAW];
#ifdef RADIO_MODE
  values[INTERMCU_RADIO_MODE] = radio_control.values[RADIO_MODE];
#endif
#ifdef RADIO_KILL_SWITCH
  values[INTERMCU_RADIO_KILL_SWITCH] = radio_control.values[RADIO_KILL_SWITCH];
#endif

#ifdef RADIO_AUX1
#ifdef RADIO_KILL_SWITCH
#warning "RC AUX1 and KILL_SWITCH are on the same channel. AUX1 is discarded."
#else
  values[INTERMCU_RADIO_AUX1] = radio_control.values[RADIO_AUX1];
#endif
#endif
#ifdef RADIO_AUX2
  values[INTERMCU_RADIO_AUX2] = radio_control.values[RADIO_AUX2];
#endif
#ifdef RADIO_AUX3
  values[INTERMCU_RADIO_AUX3] = radio_control.values[RADIO_AUX3];
#endif

  pprz_msg_send_IMCU_RADIO_COMMANDS(&(intermcu.transport.trans_tx), intermcu.device,
                                    INTERMCU_FBW, &fbw_mode, INTERMCU_RADIO_CONTROL_NB_CHANNEL, values);
#endif
}

#if USE_GPS
static void gps_cb(uint8_t sender_id __attribute__((unused)),
                   uint32_t stamp __attribute__((unused)),
                   struct GpsState *gps_s) {
  pprz_msg_send_IMCU_REMOTE_GPS(&(intermcu.transport.trans_tx), intermcu.device, INTERMCU_FBW,
    &gps_s->ecef_pos.x,
    &gps_s->ecef_pos.y,
    &gps_s->ecef_pos.z,
    &gps_s->lla_pos.alt,
    &gps_s->hmsl,
    &gps_s->ecef_vel.x,
    &gps_s->ecef_vel.y,
    &gps_s->ecef_vel.z,
    &gps_s->course,
    &gps_s->gspeed,
    &gps_s->pacc,
    &gps_s->sacc,
    &gps_s->num_sv,
    &gps_s->fix);
}
#endif

/* SOME STUFF FOR PX4IO BOOTLOADER (TODO: move this code) */
#ifdef BOARD_PX4IO
static void checkPx4RebootCommand(uint8_t b)
{
  if (intermcu.stable_px4_baud == CHANGING_BAUD && sys_time_check_and_ack_timer(px4bl_tid)) {
    //to prevent a short intermcu comm loss, give some time to changing the baud
    sys_time_cancel_timer(px4bl_tid);
    intermcu.stable_px4_baud = PPRZ_BAUD;
  } else if (intermcu.stable_px4_baud == PX4_BAUD) {

    if (sys_time_check_and_ack_timer(px4bl_tid)) {
      //time out the possibility to reboot to the px4 bootloader, to prevent unwanted restarts during flight
      sys_time_cancel_timer(px4bl_tid);
      //for unknown reasons, 1500000 baud does not work reliably after prolonged times.
      //I suspect a temperature related issue, combined with the fbw f1 crystal which is out of specs
      //After an initial period on 1500000, revert to 230400
      //We still start at 1500000 to remain compatible with original PX4 firmware. (which always runs at 1500000)
      uart_periph_set_baudrate(intermcu.device->periph, B230400);
      intermcu.stable_px4_baud = CHANGING_BAUD;
      px4bl_tid = sys_time_register_timer(1.0, NULL);
      return;
    }

#ifdef SYS_TIME_LED
    LED_ON(SYS_TIME_LED);
#endif

    if (b == px4RebootSequence[px4RebootSequenceCount]) {
      px4RebootSequenceCount++;
    } else {
      px4RebootSequenceCount = 0;
    }

    if (px4RebootSequenceCount >= 6) { // 6 = length of rebootSequence + 1
      px4RebootSequenceCount = 0; // should not be necessary...

      //send some magic back
      //this is the same as the Pixhawk IO code would send
      intermcu.device->put_byte(intermcu.device->periph, 0, 0x00);
      intermcu.device->put_byte(intermcu.device->periph, 0, 0xe5);
      intermcu.device->put_byte(intermcu.device->periph, 0, 0x32);
      intermcu.device->put_byte(intermcu.device->periph, 0, 0x0a);
      intermcu.device->put_byte(intermcu.device->periph, 0,
                                0x66); // dummy byte, seems to be necessary otherwise one byte is missing at the fmu side...

      while (((struct uart_periph *)(intermcu.device->periph))->tx_running) {
        // tx_running is volatile now, so LED_TOGGLE not necessary anymore
#ifdef SYS_TIME_LED
        LED_TOGGLE(SYS_TIME_LED);
#endif
      }

#ifdef SYS_TIME_LED
      LED_OFF(SYS_TIME_LED);
#endif
      scb_reset_system();
    }
  }
}
#endif
