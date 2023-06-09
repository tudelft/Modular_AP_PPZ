/*
 * Copyright (C) 2014 Paparazzi Team
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
 *
 */

/** @file modules/mission/mission_common.h
 *  @brief mission planner library
 *
 *  Provide the generic interface for the mission control
 *  Handle the parsing of datalink messages
 */

#ifndef MISSION_COMMON_H
#define MISSION_COMMON_H

#include "std.h"
#include "math/pprz_geodetic_float.h"
#include "math/pprz_geodetic_int.h"

enum MissionType {
  MissionWP = 1,
  MissionCircle = 2,
  MissionSegment = 3,
  MissionPath = 4,
  MissionCustom = 5
};

enum MissionInsertMode {
  Append,         ///< add at the last position
  Prepend,        ///< add before the current element
  ReplaceCurrent, ///< replace current element
  ReplaceAll,     ///< remove all elements and add the new one
  ReplaceNexts    ///< replace the next element and remove all the others
};

enum MissionRunFlag {
  MissionRun = 0,   ///< normal run
  MissionInit = 1,  ///< first exec
  MissionUpdate = 2 ///< param update
};

struct _mission_wp {
  struct EnuCoor_f wp;
};

struct _mission_circle {
  struct EnuCoor_f center;
  float radius;
};

struct _mission_segment {
  struct EnuCoor_f from;
  struct EnuCoor_f to;
};

#define MISSION_PATH_NB 5
struct _mission_path {
  struct EnuCoor_f path[MISSION_PATH_NB];
  uint8_t path_idx;
  uint8_t nb;
};

#define MISSION_CUSTOM_MAX 12 // maximum number of parameters
#define MISSION_TYPE_SIZE 6

/** custom mission element callback
 * @param[in] nb number of params
 * @param[in] params array of params with a maximum of 12
 * @param[in] init true if the function is called for the first time
 * @return true until the function ends
 */
typedef bool (*mission_custom_cb)(uint8_t nb, float *params, enum MissionRunFlag flag);

struct _mission_registered {
  mission_custom_cb cb;         ///< navigation/action function callback
  char type[MISSION_TYPE_SIZE]; ///< mission element identifier (5 char max + 1 \0)
};

struct _mission_custom {
  struct _mission_registered *reg;  ///< pointer to a registered custom mission element
  float params[MISSION_CUSTOM_MAX]; ///< list of parameters
  uint8_t nb;                       ///< number of parameters
};

struct _mission_element {
  enum MissionType type;
  union {
    struct _mission_wp mission_wp;
    struct _mission_circle mission_circle;
    struct _mission_segment mission_segment;
    struct _mission_path mission_path;
    struct _mission_custom mission_custom;
  } element;

  float duration; ///< time to spend in the element (<= 0 to disable)
  uint8_t index;      ///< index of mission element
};

/** Max number of elements in the tasks' list
 *  can be redefined
 */
#ifndef MISSION_ELEMENT_NB
#define MISSION_ELEMENT_NB 20
#endif

/** Max number of registered nav/action callbacks
 *  can be redefined
 */
#ifndef MISSION_REGISTER_NB
#define MISSION_REGISTER_NB 6
#endif

struct _mission {
  struct _mission_element elements[MISSION_ELEMENT_NB];
  struct _mission_registered registered[MISSION_REGISTER_NB];
  float element_time;   ///< time in second spend in the current element
  uint8_t insert_idx;   ///< inserstion index
  uint8_t current_idx;  ///< current mission element index
};

extern struct _mission mission;

/** Init mission structure
*/
extern void mission_init(void);

/** Insert a mission element according to the insertion mode
 * @param insert insertion mode
 * @param element mission element structure
 * @return return TRUE if insertion is succesful, FALSE otherwise
 */
extern bool mission_insert(enum MissionInsertMode insert, struct _mission_element *element);

/** Register a new navigation or action callback function
 * @param cb callback f(nb, param array)
 * @param type string identifier with 5 characters max (+ 1 '\0' char)
 * @return return TRUE if register is succesful, FALSE otherwise
 */
extern bool mission_register(mission_custom_cb cb, char *type);

/** Get current mission element
 * @return return a pointer to the next mission element or NULL if no more elements
 */
extern struct _mission_element *mission_get(void);

/** Get mission element by index
 * @return return a pointer to the mission element or NULL if no more elements
 */
extern struct _mission_element *mission_get_from_index(uint8_t index);

/** Get the ENU component of LLA mission point
 * This function is firmware specific.
 * @param point pointer to the output ENU point (float)
 * @param lla pointer to the input LLA coordinates (int)
 * @return TRUE if conversion is succesful, FALSE otherwise
 */
extern bool mission_point_of_lla(struct EnuCoor_f *point, struct LlaCoor_i *lla);

/** Run mission
 *
 * This function should be implemented into a dedicated file since
 * navigation functions are different for different firmwares
 *
 * Currently, this function should be called from the flight plan
 *
 * @return return TRUE when the mission is running, FALSE when it is finished
 */
extern int mission_run(void);

/** Report mission status
 *
 * Send mission status over datalink
 */
extern void mission_status_report(void);

/** Parsing functions called when a mission message is received
*/
extern int mission_parse_GOTO_WP(uint8_t *buf);
extern int mission_parse_GOTO_WP_LLA(uint8_t *buf);
extern int mission_parse_CIRCLE(uint8_t *buf);
extern int mission_parse_CIRCLE_LLA(uint8_t *buf);
extern int mission_parse_SEGMENT(uint8_t *buf);
extern int mission_parse_SEGMENT_LLA(uint8_t *buf);
extern int mission_parse_PATH(uint8_t *buf);
extern int mission_parse_PATH_LLA(uint8_t *buf);
extern int mission_parse_CUSTOM(uint8_t *buf);
extern int mission_parse_UPDATE(uint8_t *buf);
extern int mission_parse_GOTO_MISSION(uint8_t *buf);
extern int mission_parse_NEXT_MISSION(uint8_t *buf);
extern int mission_parse_END_MISSION(uint8_t *buf);

#endif // MISSION_COMMON_H

