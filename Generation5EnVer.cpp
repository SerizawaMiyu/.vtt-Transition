#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>

// Remove audio extension from filename (e.g., .wav., .mp3.)
void remove_audio_extension(const char *vtt_name, char *clean_name) {
    const char *audio_extensions[] = {".wav", ".mp3", ".flac", ".ogg", ".m4a", ".aac", ".wma"};
    int num_extensions = sizeof(audio_extensions) / sizeof(audio_extensions[0]);
    char *vtt_ext_pos = strstr(vtt_name, ".vtt");
    if (!vtt_ext_pos) vtt_ext_pos = strstr(vtt_name, ".VTT");
    if (!vtt_ext_pos) {
        strcpy(clean_name, vtt_name);
        return;
    }

    char *prev_dot = NULL;
    for (char *current = (char *)vtt_name; current < vtt_ext_pos; current++) {
        if (*current == '.') prev_dot = current;
    }

    if (prev_dot != NULL) {
        int ext_len = vtt_ext_pos - prev_dot;
        char potential_ext[32];
        strncpy(potential_ext, prev_dot, ext_len);
        potential_ext[ext_len] = '\0';
        for (char *p = potential_ext; *p; p++) *p = tolower(*p);

        for (int i = 0; i < num_extensions; i++) {
            if (strcmp(potential_ext, audio_extensions[i]) == 0) {
                int prefix_len = prev_dot - vtt_name;
                strncpy(clean_name, vtt_name, prefix_len);
                clean_name[prefix_len] = '\0';
                return;
            }
        }
    }

    int base_len = vtt_ext_pos - vtt_name;
    strncpy(clean_name, vtt_name, base_len);
    clean_name[base_len] = '\0';
}

void convert_vtt_to_lrc(const char *vtt_filename, const char *lrc_filename) {
    FILE *vtt_file = fopen(vtt_filename, "rb");
    if (!vtt_file) {
        perror("Cannot open VTT file");
        return;
    }

    FILE *lrc_file = fopen(lrc_filename, "wb");
    if (!lrc_file) {
        perror("Cannot create LRC file");
        fclose(vtt_file);
        return;
    }

    char line[1024];
    int is_first_webvtt_line = 1;
    int found_first_timestamp = 0;

    while (fgets(line, sizeof(line), vtt_file)) {
        line[strcspn(line, "\r\n")] = 0;

        // Skip WEBVTT header and empty lines
        if (is_first_webvtt_line && (strstr(line, "WEBVTT") || strlen(line) == 0 || line[0] == '\r')) {
            if (strstr(line, "WEBVTT")) is_first_webvtt_line = 0;
            continue;
        }
        if (strstr(line, "NOTE") || strstr(line, "STYLE") || strstr(line, "REGION") || strlen(line) == 0) {
            continue;
        }

        // Skip pure number lines (cue numbers)
        char *trimmed = line;
        while (isspace((unsigned char)*trimmed)) trimmed++;
        if (*trimmed != '\0') {
            int all_digits = 1;
            for (char *p = trimmed; *p; p++) {
                if (!isdigit((unsigned char)*p)) {
                    all_digits = 0;
                    break;
                }
            }
            if (all_digits) {
                // Is a pure number line, skip (it's likely the next cue number)
                continue;
            }
        }

        char *arrow_pos = strstr(line, "-->");
        if (arrow_pos != NULL) {
            // Process timestamp line (may have leading numbers, but separate number lines are already skipped)
            char *time_start = line;
            // If the line starts with digits+space (e.g., "1 00:00:04.128 -->"), skip them
            if (isdigit((unsigned char)time_start[0])) {
                char *p = time_start;
                while (isdigit((unsigned char)*p)) p++;
                if (*p == ' ' || *p == '\t') {
                    time_start = p + 1;
                    while (isspace((unsigned char)*time_start)) time_start++;
                }
            }

            if (time_start >= arrow_pos) continue;

            int len = arrow_pos - time_start;
            while (len > 0 && isspace((unsigned char)time_start[len-1])) len--;
            if (len <= 0) continue;

            char start_time[64];
            strncpy(start_time, time_start, len);
            start_time[len] = '\0';

            int hour = 0, minute = 0, second = 0, millisecond = 0;
            int parsed = 0;

            if (sscanf(start_time, "%d:%d:%d.%d", &hour, &minute, &second, &millisecond) == 4) parsed = 1;
            else if (sscanf(start_time, "%d:%d.%d", &minute, &second, &millisecond) == 3) { parsed = 1; hour = 0; }
            else if (sscanf(start_time, "%d:%d:%d,%d", &hour, &minute, &second, &millisecond) == 4) parsed = 1;
            else if (sscanf(start_time, "%d:%d,%d", &minute, &second, &millisecond) == 3) { parsed = 1; hour = 0; }

            if (!parsed) continue;

            found_first_timestamp = 1;
            int total_seconds = hour * 3600 + minute * 60 + second;
            int centiseconds = (millisecond + 5) / 10;
            fprintf(lrc_file, "[%02d:%02d.%02d]", total_seconds / 60, total_seconds % 60, centiseconds);
        }
        else if (found_first_timestamp && strlen(line) > 0) {
            // Process lyric line: remove trailing number
            char lyric[1024];
            strcpy(lyric, line);
            char *last_space = strrchr(lyric, ' ');
            if (last_space != NULL) {
                char *p = last_space + 1;
                int all_digits = 1;
                while (*p) {
                    if (!isdigit((unsigned char)*p)) {
                        all_digits = 0;
                        break;
                    }
                    p++;
                }
                if (all_digits && p > last_space + 1) {
                    *last_space = '\0';
                }
            }
            fprintf(lrc_file, "%s\n", lyric);
        }
    }

    fclose(vtt_file);
    fclose(lrc_file);
}

int main() {
    printf("=== VTT to LRC Batch Converter ===\n");
    printf("Description: Automatically remove audio extension, skip number lines, delete trailing numbers.\n");

    DIR *dir = opendir(".");
    if (dir == NULL) {
        perror("Cannot open current directory");
        return 1;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char *dot = strrchr(entry->d_name, '.');
        if (dot != NULL && (strcasecmp(dot, ".vtt") == 0)) {
            char base_name[512];
            remove_audio_extension(entry->d_name, base_name);

            char lrc_filename[512];
            sprintf(lrc_filename, "%s.lrc", base_name);

            printf("Converting: %s -> %s\n", entry->d_name, lrc_filename);
            convert_vtt_to_lrc(entry->d_name, lrc_filename);
            count++;
        }
    }

    closedir(dir);
    printf("Conversion complete! %d file(s) processed.\n", count);

#ifdef _WIN32
    system("pause");
#endif
    return 0;
}