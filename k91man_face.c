/*
 * MIT License
 *
 * Copyright (c) 2025
 */

#include "k91man_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_common_display.h"

#define K91MAN_LOW_BATTERY_MV 2400

static k91man_state_t k91man_state;

static watch_date_time_t _read_local_time(k91man_state_t *state, bool force_conversion) {
    uint32_t timestamp = movement_get_utc_timestamp();
    int32_t timezone_offset = movement_get_current_timezone_offset();
    bool timestamp_valid = state->last_timestamp != UINT32_MAX;
    bool timestamp_unchanged = timestamp_valid && timestamp == state->last_timestamp;
    bool timestamp_sequential = timestamp_valid && timestamp == state->last_timestamp + 1;
    bool minute_rollover = timestamp_sequential && state->previous.unit.second == 59;
    watch_date_time_t local;

    if (force_conversion || state->previous.reg == UINT32_MAX ||
        timezone_offset != state->last_timezone_offset || minute_rollover ||
        (!timestamp_unchanged && !timestamp_sequential)) {
        local = watch_utility_date_time_from_unix_time(timestamp, timezone_offset);
    } else {
        local = state->previous;
        if (timestamp_sequential) local.unit.second++;
    }

    state->last_timestamp = timestamp;
    state->last_timezone_offset = timezone_offset;
    return local;
}

static void _indicate(watch_indicator_t indicator, bool enabled) {
    if (enabled) watch_set_indicator(indicator);
    else watch_clear_indicator(indicator);
}

static void _update_alarm_indicator(k91man_state_t *state) {
    bool enabled = movement_alarm_enabled();
    if (state->alarm_enabled == enabled) return;

    state->alarm_enabled = enabled;
    _indicate(WATCH_INDICATOR_SIGNAL, enabled);
}

static void _update_low_battery_indicator(const k91man_state_t *state) {
    watch_indicator_t indicator = watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM
        ? WATCH_INDICATOR_ARROWS
        : WATCH_INDICATOR_LAP;
    _indicate(indicator, state->battery_low);
}

static void _check_battery(k91man_state_t *state, watch_date_time_t date_time) {
    if (date_time.unit.day == state->last_battery_check) return;

    state->last_battery_check = date_time.unit.day;
    state->battery_low = watch_get_vcc_voltage() < K91MAN_LOW_BATTERY_MV;
    _update_low_battery_indicator(state);
}

static uint8_t _decimal_tens(uint8_t *value) {
    if (*value >= 50) { *value -= 50; return 5; }
    if (*value >= 40) { *value -= 40; return 4; }
    if (*value >= 30) { *value -= 30; return 3; }
    if (*value >= 20) { *value -= 20; return 2; }
    if (*value >= 10) { *value -= 10; return 1; }
    return 0;
}

static void _display_pair(uint8_t position, uint8_t value, bool leading_zero) {
    uint8_t tens = _decimal_tens(&value);
    watch_display_character((tens || leading_zero) ? '0' + tens : ' ', position);
    watch_display_character('0' + value, position + 1);
}

static void _display_seconds(uint8_t seconds) {
    uint8_t tens = _decimal_tens(&seconds);
    watch_display_character_lp_seconds('0' + tens, 8);
    watch_display_character_lp_seconds('0' + seconds, 9);
}

static void _display_minute_and_second(uint8_t minute, uint8_t second) {
    _display_pair(6, minute, true);
    _display_seconds(second);
}

/* Subtract the local time from 17:00:00 using ordinary clock borrow. */
static void _countdown_to_17(watch_date_time_t date_time, uint8_t *hour, uint8_t *minute, uint8_t *second) {
    if (date_time.unit.hour >= 17) {
        *hour = 0;
        *minute = 0;
        *second = 0;
        return;
    }

    *hour = 17 - date_time.unit.hour;
    *minute = 0;
    *second = 0;

    if (date_time.unit.second) {
        *second = 60 - date_time.unit.second;
        *minute = 59;
        (*hour)--;
    }

    if (date_time.unit.minute) {
        if (*minute < date_time.unit.minute) {
            *minute += 60;
            (*hour)--;
        }
        *minute -= date_time.unit.minute;
    }
}

static void _clear_top(void) {
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "   ", "  ");
    watch_display_text(WATCH_POSITION_TOP_RIGHT, "  ");
}

static uint8_t _display_hour(watch_date_time_t date_time, movement_clock_mode_t mode) {
    uint8_t hour = date_time.unit.hour;

    if (mode == MOVEMENT_CLOCK_MODE_12H) {
        _indicate(WATCH_INDICATOR_PM, hour >= 12);
        if (hour >= 12) hour -= 12;
        if (!hour) hour = 12;
    } else {
        _indicate(WATCH_INDICATOR_PM, false);
    }

    _display_pair(4, hour, mode == MOVEMENT_CLOCK_MODE_024H);
    return hour;
}

static void _display_standard_full(watch_date_time_t date_time, bool low_energy) {
    movement_clock_mode_t mode = movement_clock_mode_24h();

    watch_display_text_with_fallback(
        WATCH_POSITION_TOP_LEFT,
        watch_utility_get_long_weekday(date_time),
        watch_utility_get_weekday(date_time)
    );
    _display_pair(2, date_time.unit.day, false);
    _display_hour(date_time, mode);
    _display_pair(6, date_time.unit.minute, true);

    if (low_energy) {
        watch_display_character(' ', 8);
        watch_display_character(' ', 9);
    } else {
        _display_seconds(date_time.unit.second);
    }
}

static void _display_countdown_full(watch_date_time_t date_time, bool low_energy) {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    _countdown_to_17(date_time, &hour, &minute, &second);

    _clear_top();
    _indicate(WATCH_INDICATOR_PM, false);
    _display_pair(4, hour, true);
    _display_pair(6, minute, true);

    if (low_energy) {
        watch_display_character(' ', 8);
        watch_display_character(' ', 9);
    } else {
        _display_seconds(second);
    }
}

static void _display_active(k91man_state_t *state, watch_date_time_t current) {
    bool countdown = current.unit.hour >= 9 && current.unit.hour < 17;
    uint8_t hour = current.unit.hour;
    uint8_t minute = current.unit.minute;
    uint8_t second = current.unit.second;
    if (countdown) _countdown_to_17(current, &hour, &minute, &second);

    if ((current.reg >> 12) != (state->previous.reg >> 12)) {
        if (countdown) _display_countdown_full(current, false);
        else _display_standard_full(current, false);
    } else if (hour != state->rendered_hour) {
        _display_pair(4, hour, countdown || movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_024H);
        _display_minute_and_second(minute, second);
    } else if (minute != state->rendered_minute) {
        _display_minute_and_second(minute, second);
    } else if (second != state->rendered_second) {
        _display_seconds(second);
    }

    state->rendered_hour = hour;
    state->rendered_minute = minute;
    state->rendered_second = second;
}

void k91man_face_setup(uint8_t watch_face_index, void **context_ptr) {
    (void)watch_face_index;

    if (*context_ptr == NULL) {
        k91man_state = (k91man_state_t){0};
        k91man_state.previous.reg = UINT32_MAX;
        k91man_state.last_timestamp = UINT32_MAX;
        k91man_state.last_timezone_offset = INT32_MIN;
        k91man_state.last_battery_check = UINT8_MAX;
        *context_ptr = &k91man_state;
    }
}

void k91man_face_activate(void *context) {
    k91man_state_t *state = (k91man_state_t *)context;

    if (watch_sleep_animation_is_running()) {
        watch_stop_sleep_animation();
        watch_stop_blink();
    }
    movement_request_tick_frequency(1);

    state->previous.reg = UINT32_MAX;
    state->last_timestamp = UINT32_MAX;
    state->last_timezone_offset = INT32_MIN;
    state->in_low_energy = false;
    state->alarm_enabled = !movement_alarm_enabled();

    _indicate(WATCH_INDICATOR_24H, movement_clock_mode_24h() != MOVEMENT_CLOCK_MODE_12H);
    _update_alarm_indicator(state);
    _update_low_battery_indicator(state);
    watch_set_colon();
}

bool k91man_face_loop(movement_event_t event, void *context) {
    k91man_state_t *state = (k91man_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK: {
            watch_date_time_t current = _read_local_time(
                state,
                event.event_type == EVENT_ACTIVATE
            );
            state->in_low_energy = false;

            _check_battery(state, current);
            _update_alarm_indicator(state);
            _display_active(state, current);
            state->previous = current;
            break;
        }
        case EVENT_LOW_ENERGY_UPDATE: {
            watch_date_time_t current = _read_local_time(state, true);
            bool entering_low_energy = !state->in_low_energy;
            state->in_low_energy = true;

            bool countdown = current.unit.hour >= 9 && current.unit.hour < 17;
            uint8_t hour = current.unit.hour;
            uint8_t minute = current.unit.minute;
            uint8_t second = current.unit.second;
            if (countdown) _countdown_to_17(current, &hour, &minute, &second);

            if (entering_low_energy ||
                (current.reg >> 12) != (state->previous.reg >> 12)) {
                if (countdown) _display_countdown_full(current, true);
                else _display_standard_full(current, true);
            } else {
                if (hour != state->rendered_hour) {
                    _display_pair(4, hour, countdown || movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_024H);
                }
                if (minute != state->rendered_minute) _display_pair(6, minute, true);
            }
            state->rendered_hour = hour;
            state->rendered_minute = minute;
            state->rendered_second = second;
            state->previous = current;
            break;
        }
        case EVENT_TIMEOUT:
            movement_move_to_face(0);
            break;
        default:
            return movement_default_loop_handler(event);
    }

    return true;
}

void k91man_face_resign(void *context) {
    k91man_state_t *state = (k91man_state_t *)context;
    state->in_low_energy = false;
    movement_request_tick_frequency(1);
}
