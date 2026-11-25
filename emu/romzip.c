/* ROM file access: a directory of loose files or a MAME zip (pharrier.zip).
 * MAME split sets keep the Hikaru BOOTROM in a separate parent zip (hikaru.zip); when the game is a zip,
 * hikaru.zip next to it is opened too and searched after the game zip. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"
#include "romzip.h"

static mz_zip_archive zip, parent; static int zip_open, parent_open;

static int is_zip(const char *path) {
    size_t n = strlen(path);
    return n > 4 && (!strcmp(path + n - 4, ".zip") || !strcmp(path + n - 4, ".ZIP"));
}

/* <dir of path>/hikaru.zip (MAME parent set of every Hikaru game) */
static void open_parent(const char *path) {
    char p[2048]; const char *slash = strrchr(path, '/'), *bs = strrchr(path, '\\');
    if (bs && (!slash || bs > slash)) slash = bs;
    int dl = slash ? (int)(slash - path) + 1 : 0;
    if (dl + 11 >= (int)sizeof p) return;
    memcpy(p, path, dl); strcpy(p + dl, "hikaru.zip");
    memset(&parent, 0, sizeof parent);
    parent_open = mz_zip_reader_init_file(&parent, p, 0) ? 1 : 0;
}

int rom_open(const char *path) {
    if (is_zip(path)) {
        memset(&zip, 0, sizeof zip);
        if (!mz_zip_reader_init_file(&zip, path, 0)) { fprintf(stderr, "cannot open %s\n", path); return -1; }
        zip_open = 1;
        open_parent(path);
    }
    return 0;
}

static size_t zip_read(mz_zip_archive *z, int idx, unsigned char *dst, size_t sz) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(z, idx, &st)) return 0;
    size_t n = (size_t)st.m_uncomp_size;
    unsigned char *tmp = malloc(n ? n : 1);
    if (!mz_zip_reader_extract_to_mem(z, idx, tmp, n, 0)) { free(tmp); return 0; }
    if (n > sz) n = sz;
    memcpy(dst, tmp, n); free(tmp);
    return n;
}

/* reads up to sz bytes of `name` into dst; returns bytes read (0 = missing) */
size_t rom_read(const char *dir, const char *name, unsigned char *dst, size_t sz) {
    if (zip_open) {
        int idx = mz_zip_reader_locate_file(&zip, name, 0, MZ_ZIP_FLAG_IGNORE_PATH);
        if (idx >= 0) return zip_read(&zip, idx, dst, sz);
        if (parent_open && (idx = mz_zip_reader_locate_file(&parent, name, 0, MZ_ZIP_FLAG_IGNORE_PATH)) >= 0)
            return zip_read(&parent, idx, dst, sz);
        fprintf(stderr, "missing %s in zip\n", name); return 0;
    }
    char p[2048]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "missing %s\n", p); return 0; }
    size_t r = fread(dst, 1, sz, f); fclose(f); return r;
}
