#include "file_backup.h"

#include "debug.h"
#include "diag.h"
#include "error.h"
#include "fopen_mode.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define BACKUP_PATH_MAX_LEN 512
#define BACKUP_COPY_BUFFER_SIZE 4096

bool backup_file(Diag *diag, const char *path)
{
    char backup_path[BACKUP_PATH_MAX_LEN];
    (void)snprintf(backup_path, BACKUP_PATH_MAX_LEN, "%s.bak", path);

    FILE *source = fopen(path, FOPEN_READ);
    if (!source) {
        error_set(diag->error, "backup fopen(%s): %s", path, strerror(errno));
        return false;
    }

    FILE *dest = fopen(backup_path, FOPEN_WRITE);
    if (!dest) {
        error_set(diag->error, "backup fopen(%s): %s", backup_path, strerror(errno));
        (void)fclose(source);
        return false;
    }

    char buffer[BACKUP_COPY_BUFFER_SIZE];
    for (;;) {
        size_t bytes = fread(buffer, 1, sizeof(buffer), source);
        if (bytes > 0 && fwrite(buffer, 1, bytes, dest) != bytes) {
            error_set(diag->error, "backup fwrite(%s): %s", backup_path, strerror(errno));
            (void)fclose(source);
            (void)fclose(dest);
            return false;
        }
        if (bytes < sizeof(buffer)) {
            break;
        }
    }

    bool read_ok = (ferror(source) == 0);
    (void)fclose(source);
    (void)fclose(dest);

    if (!read_ok) {
        error_set(diag->error, "backup fread(%s): %s", path, strerror(errno));
        return false;
    }

    debug_log(diag->debug, "backup: %s -> %s", path, backup_path);
    return true;
}
