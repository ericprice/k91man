/*
 * MIT License
 *
 * Copyright (c) 2025
 */

#include <stdlib.h>
#include <stdio.h>
#include "k91man_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_private_display.h"

static void _update_alarm_indicator(bool settings_alarm_enabled, k91man_state_t *state) {
    state->alarm_enabled = settings_alarm_enabled;
    if (state->alarm_enabled) watch_set_indicator(WATCH_INDICATOR_SIGNAL);
    else watch_clear_indicator(WATCH_INDICATOR_SIGNAL);
}

static uint32_t _get_tz_offset_seconds(movement_settings_t *settings) {
    return (uint32_t)(movement_timezone_offsets[settings->bit.time_zone]) * 60;
}

void k91man_face_setup(movement_settings_t *settings, uint8_t watch_face_index, void ** context_ptr) {
    (void) settings;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(k91man_state_t));
        k91man_state_t *state = (k91man_state_t *)*context_ptr;
        state->signal_enabled = false;
        state->watch_face_index = watch_face_index;
    }
}

void k91man_face_activate(movement_settings_t *settings, void *context) {
    k91man_state_t *state = (k91man_state_t *)context;

    if (watch_tick_animation_is_running()) watch_stop_tick_animation();

#ifdef CLOCK_FACE_24H_ONLY
    watch_set_indicator(WATCH_INDICATOR_24H);
#else
    if (settings->bit.clock_mode_24h) watch_set_indicator(WATCH_INDICATOR_24H);
#endif

    if (state->signal_enabled) watch_set_indicator(WATCH_INDICATOR_BELL);
    else watch_clear_indicator(WATCH_INDICATOR_BELL);

    _update_alarm_indicator(settings->bit.alarm_enabled, state);

    watch_set_colon();
    state->previous_minute = 0xFF;
    state->previous_second = 0xFF;
    state->previous_day_date = 0xFF;
}

static void _format_standard_time(watch_date_time dt, movement_settings_t *settings, char *buf, uint8_t *pos, bool *set_leading_zero, bool low_energy) {
#ifndef CLOCK_FACE_24H_ONLY
    if (!settings->bit.clock_mode_24h) {
        if (dt.unit.hour < 12) {
            watch_clear_indicator(WATCH_INDICATOR_PM);
        } else {
            watch_set_indicator(WATCH_INDICATOR_PM);
        }
        dt.unit.hour %= 12;
        if (dt.unit.hour == 0) dt.unit.hour = 12;
    }
#endif
    if (settings->bit.clock_mode_24h && settings->bit.clock_24h_leading_zero && dt.unit.hour < 10) {
        *set_leading_zero = true;
    }
    *pos = 0;
    if (low_energy) {
        if (!watch_tick_animation_is_running()) watch_start_tick_animation(500);
        snprintf(buf, 11, "%s%2d%2d%02d  ", watch_utility_get_weekday(dt), dt.unit.day, dt.unit.hour, dt.unit.minute);
    } else {
        snprintf(buf, 11, "%s%2d%2d%02d%02d", watch_utility_get_weekday(dt), dt.unit.day, dt.unit.hour, dt.unit.minute, dt.unit.second);
    }
}

static void _format_countdown_to_5pm(watch_date_time now_dt, movement_settings_t *settings, char *buf, uint8_t *pos, bool low_energy) {
    uint32_t tz = _get_tz_offset_seconds(settings);

    uint32_t now_ts = watch_utility_date_time_to_unix_time(now_dt, tz);
    // target is today at 17:00:00 local time
    uint32_t target_ts = watch_utility_convert_to_unix_time(now_dt.unit.year + WATCH_RTC_REFERENCE_YEAR, now_dt.unit.month, now_dt.unit.day, 17, 0, 0, tz);

    uint32_t diff = (now_ts < target_ts) ? (target_ts - now_ts) : 0;
    // Adjust by 1s so that at 17:59:XX we show 00:00:XX instead of 00:01:XX
    uint32_t diff_adj = (diff > 0) ? (diff - 1) : 0;
    watch_duration_t dur = watch_utility_seconds_to_duration(diff_adj);
    uint32_t hours_total = dur.hours + (uint32_t)dur.days * 24U;
    int hours2 = (hours_total > 99U) ? 99 : (int)hours_total; // bound to 2 digits for display

    // Format as HH:MM:SS on the main six digits; leave weekday+day as spaces (4 spaces)
    *pos = 0;
    if (low_energy) {
        if (!watch_tick_animation_is_running()) watch_start_tick_animation(500);
        // 4 spaces + HH + MM + 2 spaces = 10 chars
        snprintf(buf, 11, "    %02d%02d  ", hours2, (int)dur.minutes);
    } else {
        // 4 spaces + HH + MM + SS = 10 chars
        snprintf(buf, 11, "    %02d%02d%02d", hours2, (int)dur.minutes, (int)dur.seconds);
    }
}

bool k91man_face_loop(movement_event_t event, movement_settings_t *settings, void *context) {
    k91man_state_t *state = (k91man_state_t *)context;
    char buf[11];
    uint8_t pos;

    watch_date_time date_time;
    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
        case EVENT_LOW_ENERGY_UPDATE:
            date_time = watch_rtc_get_date_time();
            uint8_t prev_min = state->previous_minute;
            uint8_t prev_sec = state->previous_second;
            uint8_t prev_day_date = state->previous_day_date;
            state->previous_minute = date_time.unit.minute;
            state->previous_second = date_time.unit.second;
            state->previous_day_date = (date_time.unit.hour << 5) | date_time.unit.day;
            
            // Beep once at exactly 17:00:00 (5:00:00 PM)
            if (date_time.unit.hour == 17 && date_time.unit.minute == 0 && date_time.unit.second == 0 && prev_sec != 0) {
                watch_buzzer_play_note(BUZZER_NOTE_C8, 150); // Play a short celebratory beep
            }

            // check the battery voltage once a week (on day change when day % 7 == 0)
            if (date_time.unit.day != state->last_battery_check && (date_time.unit.day % 7) == 0) {
                state->last_battery_check = date_time.unit.day;
                watch_enable_adc();
                uint16_t voltage = watch_get_vcc_voltage();
                watch_disable_adc();
                state->battery_low = (voltage < 2200);
            }
            if (state->battery_low) watch_set_indicator(WATCH_INDICATOR_LAP);

            bool low_energy = (event.event_type == EVENT_LOW_ENERGY_UPDATE);
            bool set_leading_zero = false;

            // Determine display mode: 09:00:00 - 16:59:59 inclusive -> countdown to 17:00
            bool between_9_and_5 = (date_time.unit.hour >= 9 && date_time.unit.hour < 17);

            bool seconds_only = (date_time.unit.minute == prev_min && prev_sec != date_time.unit.second && 
                                prev_day_date == ((date_time.unit.hour << 5) | date_time.unit.day) && !low_energy);
            
            if (seconds_only) {
                // seconds only changed
                if (!between_9_and_5) {
                    watch_display_character_lp_seconds('0' + date_time.unit.second / 10, 8);
                    watch_display_character_lp_seconds('0' + date_time.unit.second % 10, 9);
                    break;
                }
                // countdown mode: update seconds field
                uint32_t tz = _get_tz_offset_seconds(settings);
                uint32_t now_ts = watch_utility_date_time_to_unix_time(date_time, tz);
                uint32_t target_ts = watch_utility_convert_to_unix_time(date_time.unit.year + WATCH_RTC_REFERENCE_YEAR, date_time.unit.month, date_time.unit.day, 17, 0, 0, tz);
                uint32_t diff = (now_ts < target_ts) ? (target_ts - now_ts) : 0;
                uint32_t diff_adj = (diff > 0) ? (diff - 1) : 0;
                watch_duration_t dur = watch_utility_seconds_to_duration(diff_adj);
                watch_display_character_lp_seconds('0' + (dur.seconds / 10), 8);
                watch_display_character_lp_seconds('0' + (dur.seconds % 10), 9);
                break;
            } else if (date_time.unit.minute != prev_min && prev_day_date == ((date_time.unit.hour << 5) | date_time.unit.day) && !low_energy) {
                // minutes changed
                pos = 6;
                if (!between_9_and_5) snprintf(buf, 11, "%02d%02d", date_time.unit.minute, date_time.unit.second);
                else {
                    uint32_t tz = _get_tz_offset_seconds(settings);
                    uint32_t now_ts = watch_utility_date_time_to_unix_time(date_time, tz);
                    uint32_t target_ts = watch_utility_convert_to_unix_time(date_time.unit.year + WATCH_RTC_REFERENCE_YEAR, date_time.unit.month, date_time.unit.day, 17, 0, 0, tz);
                    uint32_t diff = (now_ts < target_ts) ? (target_ts - now_ts) : 0;
                    uint32_t diff_adj = (diff > 0) ? (diff - 1) : 0;
                    watch_duration_t dur = watch_utility_seconds_to_duration(diff_adj);
                    snprintf(buf, 11, "%02d%02d", dur.minutes, dur.seconds);
                }
            } else {
                if (!between_9_and_5) {
                    _format_standard_time(date_time, settings, buf, &pos, &set_leading_zero, low_energy);
                } else {
                    _format_countdown_to_5pm(date_time, settings, buf, &pos, low_energy);
                }
            }

            watch_display_string(buf, pos);
            if (set_leading_zero) watch_display_string("0", 4);

            if (state->alarm_enabled != settings->bit.alarm_enabled) _update_alarm_indicator(settings->bit.alarm_enabled, state);
            break;
        case EVENT_ALARM_LONG_PRESS:
            state->signal_enabled = !state->signal_enabled;
            if (state->signal_enabled) watch_set_indicator(WATCH_INDICATOR_BELL);
            else watch_clear_indicator(WATCH_INDICATOR_BELL);
            break;
        case EVENT_BACKGROUND_TASK:
            movement_play_signal();
            break;
        default:
            return movement_default_loop_handler(event, settings);
    }

    return true;
}

void k91man_face_resign(movement_settings_t *settings, void *context) {
    (void) settings;
    (void) context;
}

bool k91man_face_wants_background_task(movement_settings_t *settings, void *context) {
    (void) settings;
    k91man_state_t *state = (k91man_state_t *)context;
    if (!state->signal_enabled) return false;
    watch_date_time date_time = watch_rtc_get_date_time();
    return date_time.unit.minute == 0;
}
