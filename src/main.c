
#include "h_stream.h"
#include "h_stream.c"

#ifdef _WIN32
    #include "win32_h_stream.c"
#else
    #include "linux_h_stream.c"
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define DYNAMIC_ARRAY_IMPLEMENTATION
#include "ds_dynamic_array.h"

internal uintptr_t
scan_region(process_handle handle, uintptr_t start, uintptr_t end, 
        const uint8_t* pattern, const uint8_t* mask, size_t len) {
    const size_t BUF = 4096;
    uint8_t buf[BUF];

    for (uintptr_t addr = start; addr < end; addr += BUF) {
        size_t to_read = BUF;
        if (addr + to_read > end)
            to_read = end - addr;

        if(!read_process_memory(handle, addr, buf, to_read))
            continue;

        for (size_t i = 0; i + len <= to_read; i++) {
            b32 match = true;
            for (size_t j = 0; j < len; j++) {
                if (mask[j] && buf[i+j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return addr + i;
            }
        }
    }
    return 0;
}

internal uintptr_t 
read_ptr(process_handle handle, uintptr_t address) {
    uint32_t val = 0;
    read_process_memory(handle, address, &val, sizeof(val));
    return (uintptr_t)val;
}

internal char*
read_dotnet_string(process_handle handle, uintptr_t str_ptr) {
    if (!str_ptr) return NULL;

    int32_t len = 0;
    if (!read_process_memory(handle, str_ptr + 0x4, &len, sizeof(len)))
        return NULL;
    if (len <= 0 || len > 1024) return NULL;

    uint16_t *utf16 = malloc(len * sizeof(uint16_t));
    if (!read_process_memory(handle, str_ptr + 0x8, utf16, len * sizeof(uint16_t))) {
        free(utf16);
        return NULL;
    }

    char* result = malloc(len + 1);
    for (int i = 0; i < len; i++)
        result[i] = (char)(utf16[i] & 0xFF);
    result[len] = '\0';

    free(utf16);
    return result;
}

internal uintptr_t
get_base_sig(process_handle handle) {
    const uint8_t base_pattern[]   = { 0xF8, 0x01, 0x74, 0x04, 0x83, 0x65 };
    const uint8_t mask[]           = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    return find_pattern(handle, base_pattern, mask, sizeof(base_pattern));
}

// cheatsheet https://github.com/l3lackShark/gosumemory/blob/8b1915f594e218c3fbaa23d0dcddfadd4523c762/memory/read.go#L73
// get .osu file of currently playing map.
internal b32 
get_current_map(process_handle osu_handle, const char* map_folder, char* out, size_t out_size) {

    uintptr_t base_sig = get_base_sig(osu_handle);
    if (!base_sig) {
        fprintf(stderr, "Base signature not found\n");
        return false;
    }

    uintptr_t base_ptr = read_ptr(osu_handle, base_sig - 0xC);

    uintptr_t beatmap_ptr = read_ptr(osu_handle, base_ptr);

    int32_t set_id = 0;
    if (!read_process_memory(osu_handle, beatmap_ptr + 0xCC, &set_id, sizeof(set_id))) {
        fprintf(stderr, "Could not read mapId pointer\n");
        return false;
    }
    char set_id_str[32];
    snprintf(set_id_str, sizeof(set_id_str), "%d", set_id);

    uintptr_t str_ptr = read_ptr(osu_handle, beatmap_ptr + 0xAC);
    char* difficulty_name = read_dotnet_string(osu_handle, str_ptr);

    size_t set_id_str_len = strlen(set_id_str);
    char set_path[512];

    dir_iter* it = dir_open(map_folder);
    if (!it) { perror("dir_open"); return false; }

    const char* name;
    for (;(name = dir_next(it)) != NULL;) {
        if (strncmp(name, set_id_str, set_id_str_len) == 0) {
            snprintf(set_path, sizeof(set_path), 
                    "%s" PATH_SEP "%s", map_folder, name);
            break;
        }
    }
    dir_close(it);
    
    if (!set_path[0]) return false;

    it = dir_open(set_path);
    if (!it) { perror("opendir"); return false; }

    for (;(name = dir_next(it)) != NULL;) {
        size_t len = strlen(name);
        if(len < 4 || strcmp(name + len - 4, ".osu") != 0) continue;

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s" PATH_SEP "%s", set_path, name);

        if (osu_file_matches_difficulty(filepath, difficulty_name)) {
            snprintf(out, out_size, "%s", filepath);
            dir_close(it);
            return true;
        }
    }
    dir_close(it);
    return false;
}

internal b32
read_mods(process_handle handle, uintptr_t ruleset_sig, int32_t* out) {
    // [[Rulesets - 0xB] + 0x4]
    uintptr_t rulesets_ptr = 0;
    read_process_memory(handle, ruleset_sig - 0xB, &rulesets_ptr, sizeof(uint32_t));

    uintptr_t ruleset = 0;
    read_process_memory(handle, rulesets_ptr + 0x4, &ruleset, sizeof(uint32_t));

    // [[[Ruleset + 0x68] + 0x38] + 0x1C]
    uintptr_t p1 = 0;
    read_process_memory(handle, ruleset + 0x68, &p1, sizeof(uint32_t));

    uintptr_t p2 = 0;
    read_process_memory(handle, p1 + 0x38, &p2, sizeof(uint32_t));

    uintptr_t p3 = 0;
    read_process_memory(handle, p2 + 0x1C, &p3, sizeof(uint32_t));

    int32_t xor1 = 0, xor2 = 0;
    read_process_memory(handle, p3 + 0xC, &xor1, sizeof(int32_t));
    read_process_memory(handle, p3 + 0x8, &xor2, sizeof(int32_t));

    *out = xor1 ^ xor2;
    return true;
}

internal uintptr_t
find_playtime_sig(process_handle handle) {
    const uint8_t pattern[] = { 0x5E, 0x5F, 0x5D, 0xC3, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x89, 0x00, 0x04 };
    const uint8_t mask[]    = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF };
    return find_pattern(handle, pattern, mask, sizeof(pattern));
}

internal b32
read_playtime(process_handle handle, uintptr_t sig, int32_t* out) {
    uint8_t raw[12] = {0};
    read_process_memory(handle, sig, raw, sizeof(raw));

    uintptr_t ptr = 0;
    b32 ok1 = read_process_memory(handle, sig + 0x5, &ptr, sizeof(uint32_t));
    if (!ok1) return false;

    int32_t val = 0;
    b32 ok2 = read_process_memory(handle, ptr, &val, sizeof(int32_t));
    *out = val;
    return ok2;
}

int main(int argc, char *argv[]) {

    //terminal settings
    raw_mode_t mode;
    set_raw_mode(&mode);

    // only relevant for linux is a stub on windows.
    check_permissions();

    process_handle osu_handle = find_osu_process_handle();
    if (osu_handle == -1) {
        sleep(3);
        for (;osu_handle == -1;) {
            osu_handle = find_osu_process_handle();
            if (osu_handle == -1) {
                fprintf(stderr, "failed to get osu! handle\n");
                sleep(3);
            } 
        }
        sleep(1);
    }

    // get status memory pointer
    printf("osu! handle is %d\n", osu_handle);

    // base sig
    uintptr_t base_sig = get_base_sig(osu_handle);
    if (!base_sig) {
        fprintf(stderr, "base signature not found\n");
        return 0;
    }

    const uint8_t status_pattern[] = { 0x48, 0x83, 0xf8, 0x04, 0x73, 0x1e };
    const uint8_t mask[]           = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

    uintptr_t status_sig = find_pattern(osu_handle, status_pattern, mask, sizeof(status_pattern));
    if (!status_sig) {
        fprintf(stderr, "status signature not found\n");
        return 1;
    }

    uintptr_t status_ptr = read_ptr(osu_handle, status_sig - 0x4);
    uint32_t last_status = 0xffffffff;  
    uintptr_t playtime_sig = find_playtime_sig(osu_handle);
    uintptr_t ruleset_sig = find_ruleset_sig(osu_handle);

    char songs_folder[512] = {0};
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-path") == 0) {
            if (i + 1 < argc) {
                strncpy(songs_folder, argv[i + 1], sizeof(songs_folder) - 1);
                i++;
            }
        }
    }

    if (!is_valid_songs_folder(songs_folder) && !get_songs_folder(songs_folder, sizeof(songs_folder))) {
        fprintf(stderr, "ERROR: could not find songs folder");
        return 1;
    } 

    int start_offset = 0;
    int_da time_stamps = {0};

    // sound initialization
    sound_buffer_cfg buffer_config = {0};
    buffer_config.sample_rate      = 44100;
    buffer_config.duration_beep_ms = 25;
    buffer_config.frequency        = 1000;

    int16_t *buffer = NULL;
    playback_ctx ctx = {0};

    ma_device_config device_config  = ma_device_config_init(ma_device_type_playback);
    device_config.playback.format   = ma_format_s16;
    device_config.playback.channels = 1;
    device_config.sampleRate        = buffer_config.sample_rate;
    device_config.dataCallback      = data_callback;
    device_config.pUserData         = &ctx;

    // exit root so miniaudio can run 
    // stub on windows
    discard_root();

    ma_device device = {0};

    float volume = 1.0f;
    int user_offset = 0;
    load_user_data(&user_offset, &volume);
    printf("INFO: offset=%d\n", user_offset);
    printf("INFO: volume=%.1f\n", volume);

    b32 game_is_paused = false;
    b32 first_hitobject_fired = false;
    int last_playtime = -1;
    b32 program_should_stop = false;
    for(;!program_should_stop;) {

        terminal_input_listen(&program_should_stop, &volume, &user_offset);

        uint32_t status = 0;
        if (read_process_memory(osu_handle, status_ptr, &status, sizeof(status))) {
            if (status != last_status) {
                printf("status: %u (%s)\n", status, osu_status_name(status));

                ma_device_state ds = ma_device_get_state(&device);
                if (ds == ma_device_state_started) {
                    ma_device_stop(&device);
                }
                if (ds != ma_device_state_uninitialized) {
                    ma_device_uninit(&device);
                }
                ctx.cursor = 0;

                if (status == osu_status_playing) {

                    ruleset_sig = find_ruleset_sig(osu_handle);
                    int32_t mods = 0;
                    b32 mods_ok = read_mods(osu_handle, ruleset_sig, &mods);
                    if (!mods_ok) {
                        fprintf(stderr, "failed to read mods");
                    }

                    // (mods >> 6) & 1 = dt
                    // (mods >> 8) & 1 = ht
                    float time_multiplier = ((mods >> 6) & 1) != 0 ? 1.0 / 1.5f : ((mods >> 8) & 1) != 0 ? 1.0f / 0.75f : 1.0f;

                    first_hitobject_fired = false;

                    playtime_sig = find_playtime_sig(osu_handle);

                    char current_map[512];
                    if (!get_current_map(osu_handle, songs_folder, current_map, sizeof(current_map))) {
                        fprintf(stderr, "failed to get current map\n");
                        free(buffer);
                        int_da_free(&time_stamps);
                        return 1;
                    }

                    int_da_free(&time_stamps);
                    time_stamps = get_time_stamps_from_file(current_map, &start_offset, time_multiplier);

                    buffer_config.duration_sec = (int_da_get(&time_stamps, time_stamps.base.count-1) + 1000) / 1000;
                    buffer_config.total_samples = buffer_config.sample_rate * buffer_config.duration_sec;

                    free(buffer);
                    buffer = calloc(buffer_config.total_samples, sizeof(int16_t));
                    assert(buffer);
                    fill_sound_buffer(buffer, &time_stamps, &buffer_config, volume);

                    ctx.buffer = buffer;
                    ctx.total_samples = buffer_config.total_samples;
                    ctx.cursor = 0;

                    device_config.sampleRate = buffer_config.sample_rate;
                    device_config.pUserData  = &ctx;

                    // prepare audio device
                    if (ma_device_init(NULL, &device_config, &device) != MA_SUCCESS) {
                        fprintf(stderr, "failed to init audio device\n");
                        free(buffer);
                        int_da_free(&time_stamps);
                        return 1;
                    } 
                }
            }
        }

        int32_t playtime = 0;
        if (!read_playtime(osu_handle, playtime_sig, &playtime)) {
            fprintf(stderr, "could not read playtime\n");
            return 1;
        }

        // map restart
        if (status == 2 && 
                first_hitobject_fired && 
                playtime == 0 && 
                ma_device_get_state(&device) == ma_device_state_started) {

            ma_device_stop(&device);
            ctx.cursor = 0;
            first_hitobject_fired = false;
        }

        // trigger block
        if (status == 2 && !first_hitobject_fired && start_offset > 0) {
            if (start_offset + user_offset < 0) {
                user_offset = -start_offset;
                printf("INFO: user_offset is too low, new offset is: %d\n", user_offset);
            }
            if (playtime >= start_offset + user_offset) {
                printf("start playing sounds\n");
                if (ma_device_get_state(&device) != ma_device_state_started) {
                    if (ma_device_start(&device) != MA_SUCCESS) {
                        fprintf(stderr, "failed to start audio device\n");
                        free(buffer);
                        int_da_free(&time_stamps);
                        return 1;
                    }
                }
                first_hitobject_fired = true;
            }
        }

        // in game pausing
        if (game_is_paused && ma_device_get_state(&device) == ma_device_state_started && status == 2) {
            ma_device_stop(&device);
        } else if (!game_is_paused && 
                first_hitobject_fired && 
                ma_device_get_state(&device) != ma_device_state_started && 
                status == 2) {
            if (ma_device_start(&device) != MA_SUCCESS) {
                fprintf(stderr, "failed to start audio device\n");
                free(buffer);
                int_da_free(&time_stamps);
                return 1;
            }
        }

        if (millisecond_has_passed() && status == 2) {
            game_is_paused = (playtime == last_playtime);
            last_playtime = playtime;
        }

        last_status = status;
        usleep(10);
    }

    // TODO(Osdare): save user data on forced exit (ctrl-c)
    save_user_data(user_offset, volume);

    free(buffer);
    int_da_free(&time_stamps);

    return 0;
}
