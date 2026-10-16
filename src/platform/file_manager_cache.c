#include "file_manager_cache.h"

#ifdef USE_FILE_CACHE

#include "core/string.h"
#include "platform/file_manager.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    STAT_DOESNT_WORK = -1,
    STAT_UNTESTED = 0,
    STAT_WORKS = 1
};

static dir_info *base_dir_info;
static int stat_status;

const dir_info *platform_file_manager_cache_get_dir_info(const char *dir)
{
    dir_info *info = base_dir_info;
    if (info) {
        while (1) {
            if (strcmp(info->name, dir) == 0) {
                return info;
            }
            if (!info->next) {
                break;
            }
            info = info->next;
        }
    }
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    if (!info) {
        info = malloc(sizeof(dir_info));
        base_dir_info = info;
    } else {
        info->next = malloc(sizeof(dir_info));
        info = info->next;
    }
    strncpy(info->name, dir, FILE_NAME_MAX - 1);
    info->name[FILE_NAME_MAX - 1] = 0;
    info->first_file = 0;
    info->next = 0;
    struct dirent *entry;
    file_info *file_item = 0;
    while ((entry = readdir(d))) {
        const char *name = entry->d_name;
        if (name[0] == '.') {
            // Skip hidden files
            continue;
        }
        if (!file_item) {
            file_item = malloc(sizeof(file_info));
            info->first_file = file_item;
        } else {
            file_item->next = malloc(sizeof(file_info));
            file_item = file_item->next;
        }
        file_item->next = 0;

        // Copy name
        strncpy(file_item->name, name, FILE_NAME_MAX - 1);
        file_item->name[FILE_NAME_MAX - 1] = 0;

        // Copy extension
        char c;
        const char *extension = file_item->name;
        do {
            c = *extension;
            extension++;
        } while (c != '.' && c);
        file_item->extension = extension;

        // Check type: in Caesar III, directories never have extensions (555, wavs, smk, mp3).
        // Skipping stat() for files with extensions avoids 800+ slow FAT32 IPC stat() calls in wavs/ on 3DS!
        int type = TYPE_FILE;
        if (!*file_item->extension) {
            struct stat st_info;
            char full_name[2 * FILE_NAME_MAX];
            snprintf(full_name, sizeof(full_name), "%s/%s", dir, name);

            if (stat_status == STAT_UNTESTED) {
                stat_status = stat(full_name, &st_info) == STAT_DOESNT_WORK ? STAT_DOESNT_WORK : STAT_WORKS;
            }
            if (stat_status == STAT_WORKS) {
                if (stat(full_name, &st_info) == 0 && !S_ISREG(st_info.st_mode)) {
                    type = TYPE_DIR;
                }
            } else {
                DIR *file_d = opendir(full_name);
                if (file_d) {
                    type = TYPE_DIR;
                    closedir(file_d);
                }
            }
        }
        file_item->type = type;
    }
    closedir(d);
    return info;
}

void platform_file_manager_cache_add_file_info(const char *filename)
{
    // Julius only creates files in the base dir
    if (!base_dir_info) {
        return;
    }
    file_info *f = malloc(sizeof(file_info));
    strncpy(f->name, filename, FILE_NAME_MAX - 1);
    f->name[FILE_NAME_MAX - 1] = 0;
    f->type = TYPE_FILE;
    char c;
    const char *name = f->name;
    do {
        c = *name;
        name++;
    } while (c != '.' && c);
    f->extension = name;
    f->next = base_dir_info->first_file;
    base_dir_info->first_file = f;
}

void platform_file_manager_cache_delete_file_info(const char *filename)
{
    // Julius only deletes files from the base dir
    if (!base_dir_info) {
        return;
    }
    file_info *prev = 0;
    for (file_info *f = base_dir_info->first_file; f; f = f->next) {
        if (strcmp(filename, f->name) == 0) {
            if (prev) {
                prev->next = f->next;
            } else {
                base_dir_info->first_file = f->next;
            }
            free(f);
            return;
        }
        prev = f;
    }
}

int platform_file_manager_cache_file_has_extension(const file_info *f, const char *extension)
{
    if (!(f->type & TYPE_FILE) || !extension || !*extension) {
        return 1;
    }
    return platform_file_manager_compare_filename(f->extension, extension) == 0;
}

void platform_file_manager_cache_invalidate(void)
{
    dir_info *info = base_dir_info;
    while (info) {
        file_info *file_item = info->first_file;
        while (file_item) {
            file_info *old_file_item = file_item;
            file_item = file_item->next;
            free(old_file_item);
        }
        dir_info *old_info = info;
        info = info->next;
        free(old_info);
    }
    base_dir_info = 0;
}

#endif // USE_FILE_CACHE
