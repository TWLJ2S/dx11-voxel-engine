#ifndef AC_MOD_H
#define AC_MOD_H

#include <stdint.h>

#if defined(__wasm__)
#define AC_IMPORT(name) __attribute__((import_module("ac"), import_name(name)))
#define AC_EXPORT(name) __attribute__((export_name(name)))
#else
#define AC_IMPORT(name)
#define AC_EXPORT(name)
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { AC_API_VERSION = 2 };

enum ac_log_level {
    AC_LOG_DEBUG = 0,
    AC_LOG_INFO = 1,
    AC_LOG_WARNING = 2,
    AC_LOG_ERROR = 3
};

enum ac_world_write_flag {
    AC_NOTIFY_FLUIDS = 1 << 0,
    AC_NOTIFY_REDSTONE = 1 << 1,
    AC_NOTIFY_FALLING_BLOCKS = 1 << 2,
    AC_IMMEDIATE_REMESH = 1 << 3,
    AC_NOTIFY_ALL = 15
};

AC_IMPORT("log") void ac_log(int32_t level, const char* message, int32_t length);
AC_IMPORT("reply") void ac_reply(const char* message, int32_t length);
AC_IMPORT("block_find") int32_t ac_block_find(const char* id, int32_t length);
AC_IMPORT("block_read") int64_t ac_block_read(int32_t x, int32_t y, int32_t z);
AC_IMPORT("block_write") int32_t ac_block_write(
    int32_t x, int32_t y, int32_t z, int32_t state, int32_t flags);
AC_IMPORT("register_command") int32_t ac_register_command(const char* name, int32_t length);
AC_IMPORT("argument_length") int32_t ac_argument_length(int32_t index);
AC_IMPORT("argument_read") int32_t ac_argument_read(
    int32_t index, char* destination, int32_t capacity);
AC_IMPORT("registry_find") int32_t ac_registry_find(
    const char* registry, int32_t registry_length, const char* entry, int32_t entry_length);
AC_IMPORT("registry_size") int32_t ac_registry_size(const char* registry, int32_t registry_length);
AC_IMPORT("registry_add") int32_t ac_registry_add(
    const char* registry, int32_t registry_length, const char* entry, int32_t entry_length,
    const char* value, int32_t value_length);
AC_IMPORT("registry_value_length") int32_t ac_registry_value_length(
    const char* registry, int32_t registry_length, int32_t handle);
AC_IMPORT("registry_value_read") int32_t ac_registry_value_read(
    const char* registry, int32_t registry_length, int32_t handle,
    char* destination, int32_t capacity);
AC_IMPORT("entity_spawn") int64_t ac_entity_spawn(
    const char* type, int32_t type_length, float x, float y, float z, float yaw);
AC_IMPORT("entity_exists") int32_t ac_entity_exists(int64_t entity);
AC_IMPORT("entity_position") float ac_entity_position(int64_t entity, int32_t axis);
AC_IMPORT("entity_health") float ac_entity_health(int64_t entity);
AC_IMPORT("entity_teleport") int32_t ac_entity_teleport(
    int64_t entity, float x, float y, float z);
AC_IMPORT("entity_damage") int32_t ac_entity_damage(
    int64_t entity, float amount, float direction_x, float direction_y,
    float direction_z, float knockback);
AC_IMPORT("entity_remove") int32_t ac_entity_remove(int64_t entity);
AC_IMPORT("particle_burst") int32_t ac_particle_burst(
    float x, float y, float z, uint32_t rgba, int32_t count,
    float speed, float lifetime, float size);
AC_IMPORT("ui_notify") void ac_ui_notify(const char* message, int32_t length);
AC_IMPORT("ui_screen_length") int32_t ac_ui_screen_length(void);
AC_IMPORT("ui_screen_read") int32_t ac_ui_screen_read(char* destination, int32_t capacity);
AC_IMPORT("storage_length") int32_t ac_storage_length(const char* key, int32_t key_length);
AC_IMPORT("storage_read") int32_t ac_storage_read(
    const char* key, int32_t key_length, void* destination, int32_t capacity);
AC_IMPORT("storage_write") int32_t ac_storage_write(
    const char* key, int32_t key_length, const void* value, int32_t value_length);
AC_IMPORT("storage_remove") int32_t ac_storage_remove(const char* key, int32_t key_length);
AC_IMPORT("network_available") int32_t ac_network_available(void);
AC_IMPORT("network_send") int32_t ac_network_send(
    const char* channel, int32_t channel_length, const void* payload, int32_t payload_length);

/* Required. Return zero on success. */
AC_EXPORT("ac_init") int32_t ac_init(int32_t api_version);

/* Optional callbacks. Export only the callbacks the mod uses. */
AC_EXPORT("ac_shutdown") void ac_shutdown(void);
AC_EXPORT("ac_server_tick") void ac_server_tick(
    int64_t tick, int32_t subtick, float delta_time);
AC_EXPORT("ac_block_changing") int64_t ac_block_changing(
    int32_t x, int32_t y, int32_t z, int32_t previous_state, int32_t next_state);
AC_EXPORT("ac_block_changed") void ac_block_changed(
    int32_t x, int32_t y, int32_t z, int32_t previous_state, int32_t current_state);
AC_EXPORT("ac_command") int32_t ac_command(int32_t command_id, int32_t argument_count);
AC_EXPORT("ac_world_opened") void ac_world_opened(int64_t seed);
AC_EXPORT("ac_world_saving") void ac_world_saving(void);
AC_EXPORT("ac_world_saved") void ac_world_saved(void);
AC_EXPORT("ac_world_closed") void ac_world_closed(void);
AC_EXPORT("ac_resources_reloaded") void ac_resources_reloaded(void);
AC_EXPORT("ac_registries_frozen") void ac_registries_frozen(void);
AC_EXPORT("ac_engine_started") void ac_engine_started(void);

static inline int ac_block_is_present(int64_t packed) {
    return (uint64_t)packed >> 32u != 0u;
}

static inline uint32_t ac_block_state(int64_t packed) {
    return (uint32_t)packed;
}

static inline int64_t ac_block_change_result(uint32_t state, int cancel) {
    return (int64_t)((uint64_t)(cancel != 0) << 32u | state);
}

static inline uint32_t ac_rgba(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    return (uint32_t)red | ((uint32_t)green << 8u) |
        ((uint32_t)blue << 16u) | ((uint32_t)alpha << 24u);
}

#ifdef __cplusplus
}
#endif

#endif
