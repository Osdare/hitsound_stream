 
internal pid_t 
find_osu_process_handle() {
    // TODO(Osdare): implement
}

internal b32
read_process_memory(pid_t pid, uintptr_t address, void* out, size_t size) {
    // TODO(Osdare): implement
}

internal uintptr_t
find_pattern(pid_t pid, const uint8_t* pattern, const uint8_t* mask, size_t len) {
    // TODO(Osdare): implement
}

internal void
set_raw_mode(raw_mode_t *mode) {
    // TODO(Osdare): implement
}

internal void
terminal_input_listen(b32* program_should_stop, float* volume, int* user_offset) {
    // TODO(Osdare): implement
} 

internal dir_iter*
dir_open(const char* path) {
    // TODO(Osdare): implement
}

internal const char*
dir_next(dir_iter* it) {
    // TODO(Osdare): implement
}

internal void
dir_close(dir_iter* it) {
    // TODO(Osdare): implement
}

internal void
check_permissions() {
    return;
}

internal void
discard_root() {
    return;
}

internal void get_songs_folder(char* out) {
    // TODO(Osdare): implement
}
