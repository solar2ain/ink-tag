/* DA normal source projection v1 */
#ifndef _USER_PERIPHERAL_H_
#define _USER_PERIPHERAL_H_

#include <stdbool.h>
#include <stdint.h>

#include "app.h"
#include "app_callback.h"
#include "app_default_handlers.h"
#include "app_task.h"
#include "app_suotar.h"
#include "arch_wdg.h"
#include "co_error.h"
#include "gapc_task.h"
#include "gapm_task.h"
#include "rwble_config.h"

#define APP_PARAM_UPDATE_REQUEST_TO 1000

void user_app_init(void);
arch_main_loop_callback_ret_t user_app_on_system_powered(void);
sleep_mode_t user_app_validate_sleep(sleep_mode_t sleep_mode);
void user_app_adv_start(void);
const char *inktag_da_device_name(void);
bool inktag_da_get_address(uint8_t address[6]);
void user_app_connection(uint8_t connection_idx, struct gapc_connection_req_ind const *param);
void user_app_adv_undirect_complete(uint8_t status);
void user_app_disconnect(struct gapc_disconnect_ind const *param);
void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id);

#endif
