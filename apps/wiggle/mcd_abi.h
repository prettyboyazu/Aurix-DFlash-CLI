/*
 *  Copyright (c) 2026 Infineon Technologies AG.
 *
 *  This file is part of TAS Client, an API for device access for Infineon's
 *  automotive MCUs.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *  **************************************************************************************************************** */

//********************************************************************************************************************
//  MCD (Multi-Core Debug) API ABI 定义——逐字摘录自 Infineon DAS SDK
//  (C:\Program Files\DAS64\include\mcd_api.h / mcd_types.h, SPRINT MCD API 1.1)。
//  仅摘录动态加载 mcdxdas.dll 所需的最小集合，保证与官方 DLL 二进制布局一致。
//  执行控制（stop/run/step）走 MCD；TAS RW API 无此能力（仅连接模式）。
//********************************************************************************************************************

#ifndef WIGGLE_MCD_ABI_H
#define WIGGLE_MCD_ABI_H

#include <stdint.h>

typedef char mcd_char_t;
typedef uint32_t mcd_bool_t;
typedef uint32_t mcd_return_et;
typedef uint32_t mcd_core_state_et;
typedef uint32_t mcd_core_step_type_et;
typedef uint32_t mcd_core_event_et;
typedef uint32_t mcd_error_code_et;
typedef uint32_t mcd_error_event_et;

#define MCD_INFO_STR_LEN        256
#define MCD_KEY_LEN             64
#define MCD_API_IMP_VENDOR_LEN  32
#define MCD_HOSTNAME_LEN        64
#define MCD_UNIQUE_NAME_LEN     64

enum {
    MCD_RET_ACT_NONE         = 0x00000000,
    MCD_RET_ACT_AGAIN        = 0x00000001,
    MCD_RET_ACT_HANDLE_EVENT = 0x00000002,
    MCD_RET_ACT_HANDLE_ERROR = 0x00000003
};

enum {
    MCD_CORE_STATE_UNKNOWN = 0x00000000,
    MCD_CORE_STATE_RUNNING = 0x00000001,
    MCD_CORE_STATE_HALTED  = 0x00000002,
    MCD_CORE_STATE_DEBUG   = 0x00000003
};

enum {
    MCD_CORE_STEP_TYPE_CYCLES = 0x00000001,
    MCD_CORE_STEP_TYPE_INSTR  = 0x00000002
};

typedef struct {
    uint16_t   v_api_major;
    uint16_t   v_api_minor;
    mcd_char_t author[MCD_API_IMP_VENDOR_LEN];
} mcd_api_version_st;

typedef struct {
    mcd_api_version_st  v_api;
    uint16_t            v_imp_major;
    uint16_t            v_imp_minor;
    uint16_t            v_imp_build;
    mcd_char_t          vendor[MCD_API_IMP_VENDOR_LEN];
    mcd_char_t          date[16];
} mcd_impl_version_info_st;

typedef struct {
    mcd_return_et       return_status;
    mcd_error_code_et   error_code;
    mcd_error_event_et  error_events;
    mcd_char_t          error_str[MCD_INFO_STR_LEN];
} mcd_error_info_st;

typedef struct {
    void             *instance;
    const mcd_char_t *host;
    const mcd_char_t *config_string;
} mcd_server_st;

typedef struct {
    mcd_char_t server[MCD_UNIQUE_NAME_LEN];
    mcd_char_t system_instance[MCD_UNIQUE_NAME_LEN];
    mcd_char_t acc_hw[MCD_UNIQUE_NAME_LEN];
} mcd_server_info_st;

typedef struct {
    mcd_char_t  host[MCD_HOSTNAME_LEN];
    uint32_t    server_port;
    mcd_char_t  server_key[MCD_KEY_LEN];
    mcd_char_t  system_key[MCD_KEY_LEN];
    mcd_char_t  device_key[MCD_KEY_LEN];
    mcd_char_t  system[MCD_UNIQUE_NAME_LEN];
    mcd_char_t  system_instance[MCD_UNIQUE_NAME_LEN];
    mcd_char_t  acc_hw[MCD_UNIQUE_NAME_LEN];
    uint32_t    device_type;
    mcd_char_t  device[MCD_UNIQUE_NAME_LEN];
    uint32_t    device_id;
    mcd_char_t  core[MCD_UNIQUE_NAME_LEN];
    uint32_t    core_type;
    uint32_t    core_id;
} mcd_core_con_info_st;

typedef struct {
    void                       *instance;
    const mcd_core_con_info_st *core_con_info;
} mcd_core_st;

typedef struct {
    mcd_core_state_et  state;
    mcd_core_event_et  event;
    uint32_t           hw_thread_id;
    uint32_t           trig_id;
    mcd_char_t         stop_str[MCD_INFO_STR_LEN];
    mcd_char_t         info_str[MCD_INFO_STR_LEN];
} mcd_core_state_st;

// 地址/事务/触发 ABI（逐字摘录自 mcd_api.h）
typedef uint32_t mcd_addr_space_type_et;
typedef uint32_t mcd_tx_access_type_et;
typedef uint32_t mcd_tx_access_opt_et;
typedef uint32_t mcd_trig_type_et;
typedef uint32_t mcd_trig_opt_et;
typedef uint32_t mcd_trig_action_et;

enum {
    MCD_TX_AT_R = 0x00000001,
    MCD_TX_AT_W = 0x00000002
};

enum {
    MCD_TRIG_TYPE_IP    = 0x00000001,
    MCD_TRIG_TYPE_READ  = 0x00000002,
    MCD_TRIG_TYPE_WRITE = 0x00000004,
    MCD_TRIG_TYPE_RW    = 0x00000008
};

enum {
    MCD_TRIG_ACTION_DBG_DEBUG = 0x00000001
};

typedef struct {
    uint64_t               address;
    uint32_t               mem_space_id;
    uint32_t               addr_space_id;
    mcd_addr_space_type_et addr_space_type;
} mcd_addr_st;

typedef struct {
    mcd_addr_st           addr;
    mcd_tx_access_type_et access_type;
    mcd_tx_access_opt_et  options;
    uint8_t               access_width;
    uint8_t               core_mode;
    uint8_t*              data;
    uint32_t              num_bytes;
    uint32_t              num_bytes_ok;
} mcd_tx_st;

typedef struct {
    mcd_tx_st* tx;
    uint32_t   num_tx;
    uint32_t   num_tx_ok;
} mcd_txlist_st;

typedef struct {
    uint32_t           struct_size;
    mcd_trig_type_et   type;
    mcd_trig_opt_et    option;
    mcd_trig_action_et action;
    uint32_t           action_param;
    mcd_bool_t         modified;
    uint32_t           state_mask;
    mcd_addr_st        addr_start;
    uint64_t           addr_range;
} mcd_trig_simple_core_st;

typedef struct {
    mcd_bool_t active;
    mcd_bool_t captured;
    mcd_bool_t captured_valid;
    uint64_t   count_value;
    mcd_bool_t count_valid;
} mcd_trig_state_st;

#define MCD_REG_NAME_LEN        32
#define MCD_MEM_SPACE_NAME_LEN  32

typedef uint32_t mcd_mem_type_et;
typedef uint32_t mcd_endian_et;
typedef uint32_t mcd_reg_type_et;

typedef struct {
    uint32_t   reg_group_id;
    mcd_char_t reg_group_name[MCD_REG_NAME_LEN];
    uint32_t   n_registers;
} mcd_register_group_st;

typedef struct {
    mcd_addr_st      addr;
    uint32_t         reg_group_id;
    mcd_char_t       regname[MCD_REG_NAME_LEN];
    uint32_t         regsize;
    uint32_t         core_mode_mask_read;
    uint32_t         core_mode_mask_write;
    mcd_bool_t       has_side_effects_read;
    mcd_bool_t       has_side_effects_write;
    mcd_reg_type_et  reg_type;
    uint32_t         hw_thread_id;
} mcd_register_info_st;

typedef struct {
    uint32_t           mem_space_id;
    mcd_char_t         mem_space_name[MCD_MEM_SPACE_NAME_LEN];
    mcd_mem_type_et    mem_type;
    uint32_t           bits_per_mau;
    uint8_t            invariance;
    mcd_endian_et      endian;
    uint64_t           min_addr;
    uint64_t           max_addr;
    uint32_t           num_mem_blocks;
    mcd_tx_access_opt_et supported_access_options;
    uint32_t           core_mode_mask_read;
    uint32_t           core_mode_mask_write;
} mcd_memspace_st;

typedef struct {
    uint32_t   class_vector;
    mcd_char_t info_str[MCD_INFO_STR_LEN];
} mcd_rst_info_st;

// 动态加载函数指针类型（mcdxdas.dll 导出，_f 后缀）
typedef mcd_return_et (*mcd_initialize_fn)(const mcd_api_version_st*, mcd_impl_version_info_st*);
typedef void        (*mcd_exit_fn)(void);
typedef mcd_return_et (*mcd_open_server_fn)(const mcd_char_t*, const mcd_char_t*, mcd_server_st**);
typedef mcd_return_et (*mcd_close_server_fn)(const mcd_server_st*);
typedef mcd_return_et (*mcd_qry_servers_fn)(const mcd_char_t*, mcd_bool_t, uint32_t, uint32_t*, mcd_server_info_st*);
typedef mcd_return_et (*mcd_qry_cores_fn)(const mcd_core_con_info_st*, uint32_t, uint32_t*, mcd_core_con_info_st*);
typedef mcd_return_et (*mcd_open_core_fn)(const mcd_core_con_info_st*, mcd_core_st**);
typedef mcd_return_et (*mcd_close_core_fn)(const mcd_core_st*);
typedef mcd_return_et (*mcd_run_fn)(const mcd_core_st*, mcd_bool_t);
typedef mcd_return_et (*mcd_stop_fn)(const mcd_core_st*, mcd_bool_t);
typedef mcd_return_et (*mcd_step_fn)(const mcd_core_st*, mcd_bool_t, mcd_core_step_type_et, uint32_t);
typedef mcd_return_et (*mcd_qry_state_fn)(const mcd_core_st*, mcd_core_state_st*);
typedef mcd_return_et (*mcd_qry_error_info_fn)(const mcd_core_st*, mcd_error_info_st*);
typedef mcd_return_et (*mcd_execute_txlist_fn)(const mcd_core_st*, mcd_txlist_st*);
typedef mcd_return_et (*mcd_create_trig_fn)(const mcd_core_st*, void*, uint32_t*);
typedef mcd_return_et (*mcd_remove_trig_fn)(const mcd_core_st*, uint32_t);
typedef mcd_return_et (*mcd_activate_trig_set_fn)(const mcd_core_st*);
typedef mcd_return_et (*mcd_qry_trig_state_fn)(const mcd_core_st*, uint32_t, mcd_trig_state_st*);
typedef mcd_return_et (*mcd_qry_reg_groups_fn)(const mcd_core_st*, uint32_t, uint32_t*, mcd_register_group_st*);
typedef mcd_return_et (*mcd_qry_reg_map_fn)(const mcd_core_st*, uint32_t, uint32_t, uint32_t*, mcd_register_info_st*);
typedef mcd_return_et (*mcd_qry_mem_spaces_fn)(const mcd_core_st*, uint32_t, uint32_t*, mcd_memspace_st*);
typedef mcd_return_et (*mcd_qry_rst_classes_fn)(const mcd_core_st*, uint32_t*);
typedef mcd_return_et (*mcd_qry_rst_class_info_fn)(const mcd_core_st*, uint8_t, mcd_rst_info_st*);
typedef mcd_return_et (*mcd_rst_fn)(const mcd_core_st*, uint32_t, mcd_bool_t);

#endif  // WIGGLE_MCD_ABI_H