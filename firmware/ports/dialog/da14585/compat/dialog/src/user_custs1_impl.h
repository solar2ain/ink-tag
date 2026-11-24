#ifndef _USER_CUSTS1_IMPL_H_
#define _USER_CUSTS1_IMPL_H_

#include <stdbool.h>
#include <stdint.h>

#include "custs1_task.h"
#include "ke_msg.h"

void inktag_da_init(void);
void inktag_da_startup_ready(void);
bool inktag_da_connection_reached(void);
bool inktag_da_process(void);
bool inktag_da_allows_extended_sleep(void);
void inktag_da_connected(uint8_t connection_idx);
void inktag_da_disconnected(void);
void inktag_da_write_value(uint8_t connection_idx, uint16_t handle,
                           const uint8_t *value, uint16_t length);
void inktag_da_write_handler(ke_msg_id_t const msgid,
                             struct custs1_val_write_ind const *param,
                             ke_task_id_t const dest_id,
                             ke_task_id_t const src_id);
void inktag_da_notify_cfm_handler(struct custs1_val_ntf_cfm const *param);
void inktag_da_att_info_req_handler(ke_msg_id_t const msgid,
                                    struct custs1_att_info_req const *param,
                                    ke_task_id_t const dest_id,
                                    ke_task_id_t const src_id);

#endif
