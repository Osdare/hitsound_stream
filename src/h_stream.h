#ifndef H_STREAM_H
#define H_STREAM_H

#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE process_handle;
    #define PATH_SEP "\\"
#else
    #define _GNU_SOURCE
    #include <sys/types.h>
    #include <sys/uio.h>
    #include <termios.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <dirent.h>
    #include <errno.h>
    #include <pwd.h>
    typedef pid_t process_handle;
    #define PATH_SEP "/"
#endif

#define internal static
#define global static
#define local_persist static

#include "ds_dynamic_array.h"
DA_DECLARE(int);

#include "miniaudio.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
typedef int32_t b32;

typedef struct {
#ifdef _WIN32
    DWORD original_mode;
    HANDLE hStdin;
#else
    struct termios origin;
#endif
} raw_mode_t;

typedef struct {
#ifdef _WIN32
    HANDLE handle;
    WIN32_FIND_DATAA data;
    b32 first;
#else
    DIR* dir_pointer;
#endif
} dir_iter;

typedef struct {
    int time;
    double beat_length;
    int uninherited;
} timing_point;
DA_DECLARE(timing_point);

typedef enum {
    cr_general,
    cr_hit_objects,
    cr_timing_points,
    cr_difficulty,
    cr_none,
} CurrentRegion;

typedef struct {
    int sample_rate;
    int duration_beep_ms;
    int frequency;
    int duration_sec;
    int total_samples;
} sound_buffer_cfg;

typedef struct {
    int16_t *buffer;
    ma_uint64 total_samples;
    ma_uint64 cursor;
} playback_ctx;

typedef struct {
    uintptr_t start;
    uintptr_t end;
    char perms[8];
} mem_region;

typedef enum {
   osu_status_main_menu = 0, 
   osu_status_edit = 1, 
   osu_status_playing = 2, 
   osu_status_game_over = 3, 
   osu_status_song_select = 5, 
   osu_status_results = 7, 
   osu_status_online_select = 8, 
   osu_status_lobby = 11, 
   osu_status_match = 12, 
   osu_status_match_setup = 14, 
} osu_status;

const char* osu_status_name(uint32_t status) {
    switch (status) {
        case 0: return "Main Menu";
        case 1: return "Edit";
        case 2: return "Playing";
        case 3: return "Gamer Over";
        case 5: return "Song Select";
        case 7: return "Results Screen";
        case 8: return "Online Selection";
        case 11: return "Lobby";
        case 12: return "Match";
        case 14: return "Match Setup";
        default: return "Unknown";
    }
}


internal void 
data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count);

// returns slider velocity in pixels per ms
internal double 
get_slider_velocity(timing_point_da *points, int time, double slider_multiplier);

internal int_da
get_time_stamps_from_file(const char *filename, int *offset, float time_multiplier);

internal void
fill_sound_buffer(int16_t *buffer, int_da *time_stamps, const sound_buffer_cfg *cfg, float volume);

internal b32 
is_digit(const char c);

internal b32
osu_file_matches_difficulty(const char *filepath, const char *difficulty);

internal b32
millisecond_has_passed();

internal b32
second_has_passed();

internal void 
load_user_data(int* user_offset, float* volume);

internal void 
save_user_data(int user_offset, float volume);

internal process_handle 
find_osu_process_handle();

internal b32
read_process_memory(process_handle handle, uintptr_t address, void *out, size_t size);

internal uintptr_t
scan_region(process_handle handle, uintptr_t start, uintptr_t end, 
        const uint8_t *pattern, const uint8_t *mask, size_t len);

internal uintptr_t
find_pattern(process_handle handle, const uint8_t *pattern, const uint8_t *mask, size_t len);

internal uintptr_t 
read_ptr(process_handle handle, uintptr_t address);

internal uintptr_t
find_ruleset_sig(process_handle handle);

internal void
set_raw_mode(raw_mode_t *mode);

internal void
terminal_input_listen(b32* rogram_should_stop, float* volume, int* user_offset);

internal uintptr_t
find_ruleset_sig(process_handle handle);

internal dir_iter*
dir_open(const char* path);

internal const char*
dir_next(dir_iter* it);

internal void
dir_close(dir_iter* it);

internal void
check_permissions();

internal void
discard_root();

internal b32
is_valid_songs_folder(const char* folder);

internal b32
get_songs_folder(char* out, size_t out_size);

#endif // H_STREAM_H
