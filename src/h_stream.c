
internal void data_callback(ma_device *device, void *output, const void *input, ma_uint32 frame_count) {
    playback_ctx *ctx = (playback_ctx *)device->pUserData;
    ma_uint64 remaining = ctx->total_samples - ctx->cursor;
    ma_uint64 to_copy = frame_count < remaining ? frame_count : remaining;

    memcpy(output, ctx->buffer + ctx->cursor, to_copy * sizeof(int16_t));
    ctx->cursor += to_copy;
    
    // silence remaining frames
    if (to_copy < frame_count)
        memset((int16_t *)output + to_copy, 0, (frame_count - to_copy) * sizeof(int16_t));
}

internal double 
get_slider_velocity(timing_point_da *points, int time, double slider_multiplier) {
    double base_beat_length = 500.0; // fallback 120 bpm
    double velocity_multiplier = 1.0;

    for (int i = 0; i < points->base.count; i++) {
        timing_point tp = timing_point_da_get(points, i);

        if (tp.time > time) break;

        if (tp.uninherited) {
            base_beat_length = tp.beat_length;
            velocity_multiplier = 1.0; // reset on new uninherited point
        } else {
            // inherited: beat_length is negative inverse percentage
            velocity_multiplier = -100.0 / tp.beat_length;
        }
    }

    // pixels per ms
    return (slider_multiplier * 100.0 * velocity_multiplier) / base_beat_length;
}

internal int_da
get_time_stamps_from_file(const char *filename, int *offset, float time_multiplier) {
    char string[256];

    FILE *osu_file = fopen(filename, "r");
    if (osu_file == 0) {
        printf("Error file missing\n");
        exit(-1);
    }

    CurrentRegion current_region = cr_none;

    int_da time_stamps;
    int_da_init(&time_stamps);

    timing_point_da points;
    timing_point_da_init(&points);

    double slider_multiplier = 1.4; // fallback default

    int audio_lean_in = 0;

    int start_offset = 0;
    b32 first_hitobject_found = false;

    for (;fscanf(osu_file, "%255[^\n]\n", string) > 0;) {

        // region detection
        if (strstr(string, "[General]") != 0) {
            current_region = cr_general;
        } else if (strstr(string, "[HitObjects]") != 0) {
            current_region = cr_hit_objects;
            continue;
        } else if (strstr(string, "[TimingPoints]") != 0) {
            current_region = cr_timing_points;
            continue;
        } else if (strstr(string, "[Difficulty]") != 0) {
            current_region = cr_difficulty;
            continue;
        } else if (string[0] == '[') {
            current_region = cr_none;
            continue;
        }

        if (current_region == cr_general && audio_lean_in == 0) {
            if (sscanf(string, "AudioLeadIn: %d", &audio_lean_in) == 1) {
                printf("INFO: AudioLeadIn: %d\n", audio_lean_in);
                // :D
            } 
        } else if (current_region == cr_difficulty) {
            double val;
            if (sscanf(string, "SliderMultiplier:%lf", &val) == 1) {
                slider_multiplier = val;
            }
        } else if (current_region == cr_timing_points) {

            timing_point tp = {0};
            char *token = strtok(string, ","); // time
            if (!token) continue;
            tp.time = atoi(token);

            token = strtok(NULL, ","); // beatLength
            if (!token) continue;
            tp.beat_length = atof(token);

            // skip meter, sampleSet, sampleIndex, volume
            for (int i = 0; i < 4; i++) {
                token = strtok(NULL, ",");
                if (!token) goto next_line;
            }

            token = strtok(NULL, ","); // uninherited
            if (!token) goto next_line;
            tp.uninherited = atoi(token);

            timing_point_da_append(&points, tp);

            next_line:;

        } else if (current_region == cr_hit_objects) {
            if (string[0] == '\0' || string[0] == '[') break;

            int pos = 0, consumed = 0;
            int x, y, start_time, type, hit_sound;

            if (sscanf(string + pos, "%d,%d,%d,%d,%d%n",
                        &x, &y, &start_time, &type, &hit_sound, &consumed) != 5)
                continue;
           pos += consumed; 

            if (!first_hitobject_found) {
                *offset = start_time;
                start_offset = start_time; 
                first_hitobject_found = true;
            }

            int ts = (start_time + audio_lean_in - start_offset) * time_multiplier;
            assert(ts >= 0 && "timestamp is less than zero");
            int_da_append(&time_stamps, ts);

            if (type & (1 << 1)) {

                char curve[256];
                if(sscanf(string + pos, ",%255[^,]%n", curve, &consumed) != 1)
                    continue;
                pos += consumed;

                int slides = 0;
                double length = 0.0;
                if (sscanf(string + pos, ",%d,%lf%n", &slides, &length, &consumed) != 2)
                    continue;
                pos += consumed;

                double velocity = get_slider_velocity(&points, start_time, slider_multiplier);
                int duration = (int)round(length / velocity);

                // repeat timestamps
                for (int i = 1; i <= slides; i++) {
                    int ts = ((start_time + duration * i) + audio_lean_in - start_offset) * time_multiplier;
                    assert(ts >= 0 && "timestamp should not be negative");
                    int_da_append(&time_stamps, ts);
                }
            }
        } 
    }

    fclose(osu_file);
    timing_point_da_free(&points);

    return time_stamps;
}

internal void
fill_sound_buffer(int16_t *buffer, int_da *time_stamps, const sound_buffer_cfg *cfg, float volume) {

    float *fbuffer = calloc(cfg->total_samples, sizeof(float));
    assert(fbuffer);

    // Precompute beeps
    int beep_samples = (cfg->sample_rate * cfg->duration_beep_ms) / 1000;
    float *beep_template = malloc(beep_samples * sizeof(float));
    for (int i = 0; i < beep_samples; i++) {
        double envelope = 1.0 - (double)i / beep_samples;
        double t = (double)i /cfg->sample_rate;
        beep_template[i] = (float)(sin(2 * M_PI * cfg->frequency * t) * envelope);
    }

    for (int i = 0; i < time_stamps->base.count; i++) {
        int start_sample = (int)((int64_t)int_da_get(time_stamps, i) * cfg->sample_rate / 1000);
        assert(int_da_get(time_stamps, i) >= 0 && "negative timestamp passed to fill_sound_buffer");
        for (int j = 0; j < beep_samples; j++) {
            int idx = start_sample + j;
            if (idx >= cfg->total_samples) break;
            fbuffer[idx] += beep_template[j];
        }
    }

    float peak = 0.0f;
    for (int i = 0; i < cfg->total_samples; i++) {
        float abs_val = fabsf(fbuffer[i]);
        if (abs_val > peak) peak = abs_val;
    }

    float scale = (peak > 1.0f) ? (32767.0f / peak) : 32767.0f;
    for (int i = 0; i < cfg->total_samples; i++) {
        buffer[i] = (int16_t)((fbuffer[i] * scale) * volume);
    }

    free(fbuffer);
    free(beep_template);
}

internal b32 
is_digit(const char c) {
    return c >= '0' && c <= '9';
}

internal b32
osu_file_matches_difficulty(const char *filepath, const char *difficulty) {
    FILE *f = fopen(filepath, "r");
    if (!f) return false;

    char line[256];
    for (;fgets(line, sizeof(line), f);) {
        line[strcspn(line, "\r\n")] = '\0';
        char *val = NULL;
        if (strncmp(line, "Version:", 8) == 0)
            val = line + 8;
        if (val && strcmp(val, difficulty) == 0) {
            fclose(f);
            return true;
        }
    }
    fclose(f);
    return false;
}

internal b32
second_has_passed() {
    local_persist time_t last_time = 0;
    time_t current_time = time(NULL);
    if (difftime(current_time, last_time) >= 1.0) {
        last_time = current_time;
        return true;
    }
    return false;
}

internal b32
millisecond_has_passed() {
    local_persist long last_ms = -1;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 10000000; // <- not ms

    if (last_ms == -1) last_ms = now_ms;

    if (now_ms != last_ms) {
        last_ms = now_ms;
        return true;
    }
    return false;
}

internal void 
load_user_data(int* user_offset, float *volume) {
    FILE *f = fopen("user_data.bin", "rb");
    if (!f) return;
    int offset;
    fread(user_offset, sizeof(int), 1, f);
    fread(volume, sizeof(float), 1, f);
    fclose(f);
}

internal void 
save_user_data(int user_offset, float volume) {
    FILE *f = fopen("user_data.bin", "wb");
    fwrite(&user_offset, sizeof(int), 1, f);
    fwrite(&volume, sizeof(float), 1, f);
    printf("INFO: saving user data to file\n");
    fclose(f);
}

