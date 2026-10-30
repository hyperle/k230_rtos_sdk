/* SPDX-License-Identifier: MIT */
#include "py/runtime.h"
#include "k_sensor_comm.h"

/* Sensor enum values depend on the enabled MPP drivers. Use the same
 * headers as libsensor instead of hard-coding a Python mode number. */
static const mp_rom_map_elem_t dart_board_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__dart_board) },
#ifdef CONFIG_MPP_ENABLE_SENSOR_SC038HGS
    { MP_ROM_QSTR(MP_QSTR_SC038HGS_MODE),
      MP_ROM_INT(SC038HGS_MIPI_CSI2_640X480_10BIT_LINEAR) },
#endif
};
static MP_DEFINE_CONST_DICT(dart_board_globals, dart_board_globals_table);

const mp_obj_module_t dart_board_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&dart_board_globals,
};
MP_REGISTER_MODULE(MP_QSTR__dart_board, dart_board_module);
