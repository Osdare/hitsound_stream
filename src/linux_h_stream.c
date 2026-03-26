
internal pid_t 
find_osu_process_handle() {
   const char target[] = "osu!.exe";

    DIR *dir = opendir("/proc");
    struct dirent *entry;

    for (;(entry = readdir(dir)) != NULL;) {
        if(!is_digit(entry->d_name[0])) continue;

        char path[256];
        snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);

        FILE *f = fopen(path, "r");
        if (!f) { 
            fprintf(stderr, "fopen(%s) failed: %s\n", path, strerror(errno));
            continue;
        }

        char name[256];
        fgets(name, sizeof(name), f);
        fclose(f);

        name[strcspn(name, "\n")] = 0;

        if (strcmp(name, target) == 0) {
            closedir(dir);
            return atoi(entry->d_name);
        }
    }

    closedir(dir);
    return -1;
}

// NOTE(Osdare): process_vm_readv is linux only
internal b32
read_process_memory(pid_t pid, uintptr_t address, void* out, size_t size) {
    struct iovec local  = { .iov_base = out,            .iov_len = size }; 
    struct iovec remote = { .iov_base = (void*)address, .iov_len = size};
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    return n == (ssize_t)size;
}

//NOTE(Osdare): linux only function
internal uintptr_t
find_pattern(pid_t pid, const uint8_t* pattern, const uint8_t* mask, size_t len) {
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);

    FILE* maps = fopen(maps_path, "r");
    if (!maps) {
        perror("fopen /proc/maps");
        return 0;
    }

    char line[256];
    for (;fgets(line, sizeof(line), maps);) {
        uintptr_t start, end;
        char perms[8];

        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3)
            continue;

        if (perms[0] != 'r') continue;
        if (perms[2] != 'x') continue;

        if (strstr(line, "[vsyscall]") || strstr(line, "[vvar]"))
            continue;

        uintptr_t result = scan_region(pid, start, end, pattern, mask, len);
        if (result) {
            fclose(maps);
            return result;
        }
    }

    fclose(maps);
    return 0;
}

internal void
set_raw_mode(raw_mode_t *mode) {
    tcgetattr(STDIN_FILENO, &mode->origin);
    struct termios raw = mode->origin;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

internal void
terminal_input_listen(b32* program_should_stop, float* volume, int* user_offset) {
    char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'q') {
            *program_should_stop = true;
        } else if (c == '+' && *volume < 1.0f) {
            *volume += 0.1f;
            printf("volume is: %.1f\n", *volume);
        } else if (c == '-' && *volume > 0.1f) {
            *volume -= 0.1f;
            printf("volume is: %.1f\n", *volume);
        } else if (c == 27) {
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && (seq[0] == '[' || seq[0] == 0) && 
                    read(STDIN_FILENO, &seq[1], 1) == 1) {
                switch (seq[1]) {
                    case 'A': *user_offset += 5; break;
                    case 'B': *user_offset -= 5; break;
                    case 'C': *user_offset += 1; break;
                    case 'D': *user_offset -= 1; break;
                    default: printf("seq[1]: %c seq[0]: %c\n", seq[1], seq[0]); break;
                }
                printf("offset is: %dms\n", *user_offset);
            }
        }
    }
}

internal uintptr_t
find_ruleset_sig(pid_t handle) {
    const uint8_t pattern[] = { 0x7D, 0x15, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x85, 0xC0 };
    const uint8_t mask[]    = { 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF };

    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", handle);
    FILE *maps = fopen(maps_path, "r");
    if (!maps) return 0;

    int match_count = 0;
    char line[256];
    for (;fgets(line, sizeof(line), maps);) {
        uintptr_t start, end;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &start, &end, perms) != 3) continue;
        if (perms[0] != 'r') continue;
        if (strstr(line, "[vsyscall]") || strstr(line, "[vvar]")) continue;

        uintptr_t result = scan_region(handle, start, end, pattern, mask, sizeof(pattern));
        if (result) {
            match_count++;

            // verify this is not the beatmap sig by checking the embedded address
            uintptr_t embedded = 0;
            read_process_memory(handle, result + 0x3, &embedded, sizeof(uint32_t));
            if (embedded == 0x023e44c8) {  // known beatmap base address
                fprintf(stderr, "DEBUG: skipping beatmap sig\n");
                continue;
            }

            fclose(maps);
            return result;
        }
    }

    fclose(maps);
    return 0;
}

internal dir_iter*
dir_open(const char* path) {
    dir_iter* it = malloc(sizeof(it));
    if (!it) return NULL;
    it->dir_pointer = opendir(path);
    if (!it->dir_pointer) { free(it); return NULL; }
    return it;
}

internal const char*
dir_next(dir_iter* it) {
    struct dirent* e = readdir(it->dir_pointer);;
    return e ? e->d_name : NULL;
}

internal void
dir_close(dir_iter* it) {
    if (it) { closedir(it->dir_pointer); free(it); }
}

internal void
check_permissions() {
     if (geteuid() != 0) 
        fprintf(stderr, "run as root, process_vm_readv requires it\n");
}

internal void
discard_root() {
    uid_t real_uid = atoi(getenv("SUDO_UID"));
    gid_t real_gid = atoi(getenv("SUDO_GID"));
    setgid(real_gid);
    setuid(real_uid);

    char runtime_dir[64];
    snprintf(runtime_dir, sizeof(runtime_dir), "/run/user/%d", real_uid);
    setenv("XDG_RUNTIME_DIR", runtime_dir, 1);

    const char* home = getpwuid(real_uid)->pw_dir;
    setenv("HOME", home, 1);
}

internal b32
is_valid_songs_folder(const char* folder) {
    dir_iter* it = dir_open(folder);
    if (!it) return false; 

    const char* name;

    for(;(name = dir_next(it)) != NULL;) {
        size_t temp_size = strlen(folder) + 1 + strlen(name) + 1;
        char* temp = malloc(temp_size);
        if(!temp) {
            dir_close(it);
            return false;
        }

        snprintf(temp, temp_size, "%s/%s", folder, name);

        dir_iter* sub = dir_open(temp);
        if (sub) {
            const char* subname;
            while ((subname = dir_next(sub)) != NULL) {
                size_t len = strlen(subname);
                if (len >= 4 && strcmp(subname + len - 4, ".osu") == 0) {
                    dir_close(sub);
                    dir_close(it);
                    free(temp);
                    return true;
                } 
            }
            dir_close(sub);
        }
        free(temp);
    }
    dir_close(it);
    return false;
}


// call before exiting root
internal b32
get_songs_folder(char* out, size_t out_size) {
    uid_t uid = atoi(getenv("SUDO_UID"));
    struct passwd *pw = getpwuid(uid);
    if (!pw) return false;

    const char *candidates[] = {
        "/home/%s/.local/share/osu-wine/osu!/Songs",
        "/home/%s/.wine/drive_c/users/%s/AppData/Local/osu!/Songs",
        "/home/%s/Games/osu!/Songs",
    };

    const int num_candidates = sizeof(candidates) / sizeof(candidates[0]);
    
    for (int i = 0; i < num_candidates; i++) {
        
        int written = snprintf(out, out_size, candidates[i], pw->pw_name, pw->pw_name);
        if (written < 0 || written >= out_size) {
            continue;
        }

        if (is_valid_songs_folder(out)) {
            printf("INFO: songs folder found: %s\n", out);
            return true;
        }
    }
    return false;
}

