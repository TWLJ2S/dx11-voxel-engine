#include "ac_mod.h"

static const char initialized[] = "hello mod initialized";
static const char command_name[] = "hello";
static const char response[] = "hello from WebAssembly";

int32_t ac_init(int32_t api_version) {
    if (api_version != AC_API_VERSION) return 1;
    ac_log(AC_LOG_INFO, initialized, (int32_t)(sizeof(initialized) - 1));
    return ac_register_command(command_name, (int32_t)(sizeof(command_name) - 1)) < 0;
}

int32_t ac_command(int32_t command_id, int32_t argument_count) {
    (void)command_id;
    (void)argument_count;
    ac_reply(response, (int32_t)(sizeof(response) - 1));
    ac_ui_notify(response, (int32_t)(sizeof(response) - 1));
    return 0;
}
