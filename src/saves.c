/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Copies of a game's saved data on a drive: see saves.h. */
#include "saves.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lang.h"
#include "mem.h"

const char *saves_home = "/user/home";
void (*saves_pump)(void);
time_t (*saves_clock)(time_t *) = time;
int (*saves_open)(const char *tid);

#define COPY_BLOCK (1u << 20)
#define PUMP_EVERY (8ull << 20)
#define MAX_DEPTH 3    /* folders inside a game's save folder (the system keeps none) */
#define MAX_PLAN 64    /* saves of one game put back at once */
#define NEW_SUFFIX ".ps4mount-new"
#define PART_SUFFIX ".part"

/* A path made like snprintf; -1 when it does not fit (a cut path names another file). */
static int pathf(char *out, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int pathf(char *out, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, n, fmt, ap);
    va_end(ap);
    return r < 0 || (size_t)r >= n ? -1 : 0;
}

int saves_valid_tid(const char *tid) {
    if (!tid || strlen(tid) != 9)
        return 0;
    for (int i = 0; i < 9; i++)
        if (!((tid[i] >= 'A' && tid[i] <= 'Z') || (tid[i] >= '0' && tid[i] <= '9')))
            return 0;
    return 1;
}

int saves_valid_user(const char *user) {
    if (!user || strlen(user) != 8)
        return 0;
    for (int i = 0; i < 8; i++) {
        char c = user[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

int saves_valid_stamp(const char *stamp) {
    if (!stamp)
        return 0;
    size_t n = strlen(stamp), tag = sizeof(SAVES_TAG_RESTORE) - 1;
    if (n != 15 && !(n == 16 + tag && stamp[15] == '-' && !strcmp(stamp + 16, SAVES_TAG_RESTORE)))
        return 0;
    for (int i = 0; i < 15; i++)
        if (i == 8 ? stamp[i] != '-' : (stamp[i] < '0' || stamp[i] > '9'))
            return 0;
    return 1;
}

static int is_dir(const char *p) {
    struct stat st;
    return !lstat(p, &st) && S_ISDIR(st.st_mode);
}

static int is_file(const char *p, struct stat *st) {
    struct stat s;
    if (!st)
        st = &s;
    return !lstat(p, st) && S_ISREG(st->st_mode);
}

static void mkdirs(const char *path) {
    char tmp[PATH_MAX];
    if (pathf(tmp, sizeof(tmp), "%s", path))
        return;
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    mkdir(tmp, 0777);
}

/* Removes a copy's folder: its files and folders, no deeper than MAX_DEPTH. Only called on
 * folders saves.c made below SAVES_DIR. */
static void rm_copy(const char *dir, int depth) {
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char p[PATH_MAX];
        if (pathf(p, sizeof(p), "%s/%s", dir, e->d_name))
            continue;
        if (is_dir(p)) {
            if (depth < MAX_DEPTH)
                rm_copy(p, depth + 1);
        } else
            unlink(p);
    }
    if (d)
        closedir(d);
    rmdir(dir);
}

#define FNV_OFFSET 14695981039346656037ull

static uint64_t fnv(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* The users' folders in saves_home that have a save folder for tid; calls fn on each. */
typedef int (*user_fn)(const char *user, const char *dir, void *ctx);

static int each_user(const char *tid, user_fn fn, void *ctx) {
    DIR *d = opendir(saves_home);
    struct dirent *e;
    int rc = 0;
    while (d && !rc && (e = readdir(d))) {
        if (!saves_valid_user(e->d_name))
            continue;
        char dir[PATH_MAX];
        if (!pathf(dir, sizeof(dir), "%s/%s/savedata/%s", saves_home, e->d_name, tid) && is_dir(dir))
            rc = fn(e->d_name, dir, ctx);
    }
    if (d)
        closedir(d);
    return rc;
}

/* Size of the files of a folder (and its folders) and, with h, their fingerprint: the sum of
 * one hash per file (its path, size and time), so the order readdir gives does not count. */
static void tree_stats(const char *dir, int depth, uint64_t *h, uint64_t *bytes, int *files) {
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char p[PATH_MAX];
        struct stat st;
        if (pathf(p, sizeof(p), "%s/%s", dir, e->d_name) || lstat(p, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_DEPTH)
                tree_stats(p, depth + 1, h, bytes, files);
            continue;
        }
        if (!S_ISREG(st.st_mode))
            continue;
        if (h) {
            int64_t v[3] = {(int64_t)st.st_size, (int64_t)st.st_mtim.tv_sec, (int64_t)st.st_mtim.tv_nsec};
            *h += fnv(fnv(FNV_OFFSET, p, strlen(p)), v, sizeof(v));
        }
        *bytes += (uint64_t)st.st_size;
        (*files)++;
    }
    if (d)
        closedir(d);
}

typedef struct {
    uint64_t h, bytes;
    int files, users;
} fp_ctx_t;

static int fp_user(const char *user, const char *dir, void *ctx) {
    fp_ctx_t *c = ctx;
    int before = c->files;
    (void)user; /* in the paths hashed */
    tree_stats(dir, 0, &c->h, &c->bytes, &c->files);
    c->users += c->files > before;
    return 0;
}

uint64_t saves_fp(const char *tid) {
    fp_ctx_t c = {0, 0, 0, 0};
    if (!saves_valid_tid(tid))
        return 0;
    each_user(tid, fp_user, &c);
    return c.files ? (c.h ? c.h : 1) : 0;
}

int saves_on_console(const char *tid, uint64_t *bytes) {
    fp_ctx_t c = {0, 0, 0, 0};
    if (saves_valid_tid(tid))
        each_user(tid, fp_user, &c);
    if (bytes)
        *bytes = c.bytes;
    return c.users;
}

/* Copies src to dst (made or emptied) with the mode of like (or of src). 0 when every byte
 * arrived and src did not change meanwhile; why says what failed. */
static int copy_file(const char *src, const char *dst, const struct stat *like, uint8_t *buf, char *why, size_t wn) {
    struct stat a, b;
    int in = open(src, O_RDONLY);
    if (in < 0 || fstat(in, &a)) {
        snprintf(why, wn, "%s: %s", src, strerror(errno));
        if (in >= 0)
            close(in);
        return -1;
    }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        snprintf(why, wn, "%s: %s", dst, strerror(errno));
        close(in);
        return -1;
    }
    uint64_t done = 0, since = 0;
    int rc = 0;
    for (;;) {
        ssize_t n = read(in, buf, COPY_BLOCK);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0) {
            snprintf(why, wn, "%s: %s", src, strerror(errno));
            rc = -1;
            break;
        }
        if (n == 0)
            break;
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(out, buf + off, (size_t)(n - off));
            if (w < 0 && errno == EINTR)
                continue;
            if (w <= 0) {
                snprintf(why, wn, "%s: %s", dst, w < 0 ? strerror(errno) : "no space");
                rc = -1;
                break;
            }
            off += w;
        }
        if (rc)
            break;
        done += (uint64_t)n;
        since += (uint64_t)n;
        if (since >= PUMP_EVERY && saves_pump) {
            since = 0;
            saves_pump();
        }
    }
    const struct stat *m = like ? like : &a;
    if (!rc) {
        if (fchmod(out, m->st_mode & 07777)) {
            /* a drive without modes (exFAT): nothing to keep */
        }
        if (like && fchown(out, like->st_uid, like->st_gid)) {
            /* not ours to give: the mode alone is kept */
        }
        fsync(out);
    }
    if (close(out) && !rc) {
        snprintf(why, wn, "%s: %s", dst, strerror(errno));
        rc = -1;
    }
    close(in);
    if (rc)
        return -1;
    if (done != (uint64_t)a.st_size || stat(src, &b) || b.st_size != a.st_size || b.st_mtim.tv_sec != a.st_mtim.tv_sec ||
        b.st_mtim.tv_nsec != a.st_mtim.tv_nsec) {
        snprintf(why, wn, TR("%s changed while it was copied", "%s cambió mientras se copiaba", "%s kopyalanırken değişti"), src);
        return -1;
    }
    if (stat(dst, &b) || b.st_size != a.st_size) {
        snprintf(why, wn, TR("%s did not arrive whole", "%s no llegó entero", "%s eksik kopyalandı"), dst);
        return -1;
    }
    return 0;
}

/* Copies the files and folders of src into dst (which exists). */
static int copy_tree(const char *src, const char *dst, int depth, uint8_t *buf, char *why, size_t wn) {
    DIR *d = opendir(src);
    struct dirent *e;
    int rc = 0;
    if (!d) {
        snprintf(why, wn, "%s: %s", src, strerror(errno));
        return -1;
    }
    while (!rc && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char s[PATH_MAX], t[PATH_MAX];
        struct stat st;
        if (pathf(s, sizeof(s), "%s/%s", src, e->d_name) || pathf(t, sizeof(t), "%s/%s", dst, e->d_name)) {
            snprintf(why, wn, TR("a path is too long: %s", "una ruta es demasiado larga: %s", "bir yol çok uzun: %s"), e->d_name);
            rc = -1;
            continue;
        }
        if (lstat(s, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth >= MAX_DEPTH)
                continue;
            if (mkdir(t, 0777) && errno != EEXIST) {
                snprintf(why, wn, "%s: %s", t, strerror(errno));
                rc = -1;
            } else
                rc = copy_tree(s, t, depth + 1, buf, why, wn);
        } else if (S_ISREG(st.st_mode))
            rc = copy_file(s, t, NULL, buf, why, wn);
    }
    closedir(d);
    return rc;
}

/* The copies of one user and game: their folder names, sorted (oldest first). */
static int copy_names(const char *base, char (*names)[48], int max) {
    DIR *d = opendir(base);
    struct dirent *e;
    int n = 0;
    while (d && n < max && (e = readdir(d)))
        if (saves_valid_stamp(e->d_name) && !pathf(names[n], 48, "%s", e->d_name))
            n++;
    if (d)
        closedir(d);
    for (int i = 1; i < n; i++) /* few: insertion sort */
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char tmp[48];
            memcpy(tmp, names[j], sizeof(tmp));
            memcpy(names[j], names[j - 1], sizeof(tmp));
            memcpy(names[j - 1], tmp, sizeof(tmp));
        }
    return n;
}

/* Keeps the newest keep copies of each kind, and drops .part folders an interrupted copy
 * left. */
static void prune(const char *base, int keep, int keep_restore) {
    static char names[SAVES_MAX_COPIES * 2][48];
    int n = copy_names(base, names, SAVES_MAX_COPIES * 2), plain = 0, restore = 0;
    for (int i = 0; i < n; i++) {
        if (strstr(names[i], SAVES_TAG_RESTORE))
            restore++;
        else
            plain++;
    }
    /* oldest first: each one goes while more than keep of its kind are left */
    for (int i = 0; i < n; i++) {
        int r = strstr(names[i], SAVES_TAG_RESTORE) != NULL, *left = r ? &restore : &plain;
        if (*left <= (r ? keep_restore : keep))
            continue;
        (*left)--;
        char p[PATH_MAX];
        if (pathf(p, sizeof(p), "%s/%s", base, names[i]))
            continue;
        printf("[saves] old copy %s removed\n", p);
        rm_copy(p, 0);
    }
    DIR *d = opendir(base);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        size_t l = strlen(e->d_name), sl = sizeof(PART_SUFFIX) - 1;
        if (l > sl && !strcmp(e->d_name + l - sl, PART_SUFFIX)) {
            char stamp[64], p[PATH_MAX];
            snprintf(stamp, sizeof(stamp), "%.*s", (int)(l - sl), e->d_name);
            if (!saves_valid_stamp(stamp))
                continue;
            if (!pathf(p, sizeof(p), "%s/%s", base, e->d_name))
                rm_copy(p, 0);
        }
    }
    if (d)
        closedir(d);
}

typedef struct {
    const char *tid, *root, *only, *stamp;
    int keep, keep_restore, copied;
    uint8_t *buf;
    char *msg;
    size_t cap;
} backup_ctx_t;

static int backup_user(const char *user, const char *dir, void *ctx) {
    backup_ctx_t *c = ctx;
    if (c->only && strcmp(user, c->only))
        return 0;
    uint64_t bytes = 0;
    int files = 0;
    tree_stats(dir, 0, NULL, &bytes, &files);
    if (!files)
        return 0;
    char base[PATH_MAX], part[PATH_MAX], final[PATH_MAX], why[PATH_MAX + 64] = "";
    if (pathf(base, sizeof(base), "%s/" SAVES_DIR "/%s/%s", c->root, user, c->tid) ||
        pathf(part, sizeof(part), "%s/%s" PART_SUFFIX, base, c->stamp) || pathf(final, sizeof(final), "%s/%s", base, c->stamp)) {
        snprintf(c->msg, c->cap, TR("Could not copy the saves: %s", "No se pudieron copiar las partidas: %s",
                                    "Kayıtlar kopyalanamadı: %s"), c->root);
        return -1;
    }
    mkdirs(base);
    if (is_dir(final)) {
        snprintf(c->msg, c->cap, "%s",
                 TR("A copy with this time is there already: try again in a moment",
                    "Ya hay una copia con esta hora: inténtalo de nuevo en un momento",
                    "Bu saatle bir kopya zaten var: biraz sonra tekrar dene"));
        return -1;
    }
    rm_copy(part, 0);
    if (mkdir(part, 0777) && errno != EEXIST) {
        snprintf(c->msg, c->cap, TR("Could not copy the saves: %s", "No se pudieron copiar las partidas: %s",
                                    "Kayıtlar kopyalanamadı: %s"), strerror(errno));
        return -1;
    }
    if (copy_tree(dir, part, 0, c->buf, why, sizeof(why)) || rename(part, final)) {
        if (!why[0])
            snprintf(why, sizeof(why), "%s: %s", final, strerror(errno));
        rm_copy(part, 0);
        snprintf(c->msg, c->cap, TR("Could not copy the saves: %s", "No se pudieron copiar las partidas: %s",
                                    "Kayıtlar kopyalanamadı: %s"), why);
        return -1;
    }
    printf("[saves] %s of user %s copied to %s (%d file(s), %llu KB)\n", c->tid, user, final, files,
           (unsigned long long)(bytes >> 10));
    prune(base, c->keep, c->keep_restore);
    c->copied++;
    return 0;
}

static void make_stamp(char *out, size_t n, const char *tag) {
    time_t now = saves_clock(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char s[16];
    strftime(s, sizeof(s), "%Y%m%d-%H%M%S", &tm);
    if (tag)
        snprintf(out, n, "%s-%s", s, tag);
    else
        snprintf(out, n, "%s", s);
}

int saves_backup(const char *tid, const char *root, const char *user, int keep, const char *tag, char *msg, size_t cap) {
    if (!saves_valid_tid(tid) || (user && !saves_valid_user(user)) || !root || !root[0]) {
        snprintf(msg, cap, "%s", TR("Not a game or a user of this console", "No es un juego ni un usuario de esta consola",
                                     "Bu konsolun oyunu ya da kullanıcısı değil"));
        return -1;
    }
    if (keep < 1)
        keep = 1;
    char stamp[48];
    make_stamp(stamp, sizeof(stamp), tag);
    uint8_t *buf = mem_alloc(COPY_BLOCK);
    if (!buf) {
        snprintf(msg, cap, "%s", TR("Not enough memory to copy the saves", "No hay memoria para copiar las partidas",
                                     "Kayıtları kopyalamak için bellek yetmiyor"));
        return -1;
    }
    msg[0] = 0;
    /* a before-restore copy leaves the plain ones as they are */
    backup_ctx_t c = {tid, root, user, stamp, tag ? SAVES_MAX_COPIES : keep, SAVES_KEEP_RESTORE, 0, buf, msg, cap};
    int rc = each_user(tid, backup_user, &c);
    mem_free(buf);
    return rc ? -1 : c.copied;
}

int saves_list(const char *tid, const char *root, save_copy_t *out, int max) {
    if (!saves_valid_tid(tid) || !root)
        return 0;
    char users_dir[PATH_MAX];
    if (pathf(users_dir, sizeof(users_dir), "%s/" SAVES_DIR, root))
        return 0;
    DIR *d = opendir(users_dir);
    struct dirent *e;
    int n = 0;
    static char names[SAVES_MAX_COPIES * 2][48];
    while (d && n < max && (e = readdir(d))) {
        if (!saves_valid_user(e->d_name))
            continue;
        char base[PATH_MAX];
        if (pathf(base, sizeof(base), "%s/%s/%s", users_dir, e->d_name, tid))
            continue;
        int k = copy_names(base, names, SAVES_MAX_COPIES * 2);
        for (int i = 0; i < k && n < max; i++) {
            save_copy_t *s = &out[n];
            char p[PATH_MAX];
            if (pathf(p, sizeof(p), "%s/%s", base, names[i]))
                continue;
            memset(s, 0, sizeof(*s));
            pathf(s->user, sizeof(s->user), "%s", e->d_name); /* 8 hex digits: it fits */
            pathf(s->stamp, sizeof(s->stamp), "%s", names[i]); /* both 48 bytes */
            s->before_restore = strstr(names[i], SAVES_TAG_RESTORE) != NULL;
            tree_stats(p, 0, NULL, &s->bytes, &s->files);
            n++;
        }
    }
    if (d)
        closedir(d);
    for (int i = 1; i < n; i++) /* newest first, by the time in the name */
        for (int j = i; j > 0 && strncmp(out[j - 1].stamp, out[j].stamp, 15) < 0; j--) {
            save_copy_t tmp = out[j];
            out[j] = out[j - 1];
            out[j - 1] = tmp;
        }
    return n;
}

typedef struct {
    char img[256], key[256];
} plan_t;

int saves_restore(const char *tid, const char *root, const char *user, const char *stamp, int keep, char *msg, size_t cap) {
    if (!saves_valid_tid(tid) || !saves_valid_user(user) || !saves_valid_stamp(stamp) || !root || !root[0]) {
        snprintf(msg, cap, "%s", TR("Not a copy of this console", "No es una copia de esta consola", "Bu konsolun bir kopyası değil"));
        return -1;
    }
    char src[PATH_MAX], dst[PATH_MAX];
    if (pathf(src, sizeof(src), "%s/" SAVES_DIR "/%s/%s/%s", root, user, tid, stamp) ||
        pathf(dst, sizeof(dst), "%s/%s/savedata/%s", saves_home, user, tid) || !is_dir(src)) {
        snprintf(msg, cap, "%s", TR("That copy is not on the drive any more", "Esa copia ya no está en la unidad",
                                     "Bu kopya artık sürücüde yok"));
        return -1;
    }
    if (!is_dir(dst)) {
        snprintf(msg, cap, "%s",
                 TR("This user has no saves of the game on the console yet: start the game, save once, close it and try again",
                    "Este usuario aún no tiene partidas de este juego en la consola: abre el juego, guarda una vez, ciérralo y vuelve a intentarlo",
                    "Bu kullanıcının konsolda bu oyuna ait kaydı yok: oyunu aç, bir kez kaydet, kapat ve tekrar dene"));
        return -1;
    }
    /* what can go back: each save of the copy the console has too, with an image of the
     * same size; both files of a save or neither */
    static plan_t plan[MAX_PLAN];
    int np = 0, skipped = 0;
    DIR *d = opendir(src);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (strncmp(e->d_name, "sdimg_", 6) || !e->d_name[6] || strlen(e->d_name) >= sizeof(plan[0].img))
            continue;
        char key[256], p[PATH_MAX];
        struct stat si, sk, ci, ck;
        int ok = !pathf(key, sizeof(key), "%s.bin", e->d_name + 6);
        ok = ok && !pathf(p, sizeof(p), "%s/%s", src, e->d_name) && is_file(p, &si);
        ok = ok && !pathf(p, sizeof(p), "%s/%s", src, key) && is_file(p, &sk);
        ok = ok && !pathf(p, sizeof(p), "%s/%s", dst, e->d_name) && is_file(p, &ci);
        ok = ok && !pathf(p, sizeof(p), "%s/%s", dst, key) && is_file(p, &ck);
        ok = ok && si.st_size == ci.st_size && sk.st_size == ck.st_size;
        if (!ok || np == MAX_PLAN) {
            skipped++;
            printf("[saves] %s: %s not put back (not on the console with the same size)\n", tid, e->d_name);
            continue;
        }
        snprintf(plan[np].img, sizeof(plan[np].img), "%s", e->d_name);
        snprintf(plan[np].key, sizeof(plan[np].key), "%s", key);
        np++;
    }
    if (d)
        closedir(d);
    if (!np) {
        snprintf(msg, cap, "%s",
                 TR("Nothing to put back: no save of this copy is on the console with the same size",
                    "Nada que devolver: ninguna partida de esta copia está en la consola con el mismo tamaño",
                    "Geri yüklenecek bir şey yok: bu kopyadaki kayıtların hiçbiri konsolda aynı boyutta değil"));
        return -1;
    }
    /* the console's saves first: without that copy nothing changes */
    char why[PATH_MAX + 64];
    if (saves_backup(tid, root, user, keep, SAVES_TAG_RESTORE, why, sizeof(why)) <= 0) {
        snprintf(msg, cap,
                 TR("Nothing changed: the saves on the console could not be copied first (%s)",
                    "No se cambió nada: no se pudieron copiar antes las partidas de la consola (%s)",
                    "Hiçbir şey değişmedi: konsoldaki kayıtlar önce yedeklenemedi (%s)"),
                 why);
        return -1;
    }
    uint8_t *buf = mem_alloc(COPY_BLOCK);
    if (!buf) {
        snprintf(msg, cap, "%s", TR("Not enough memory to copy the saves", "No hay memoria para copiar las partidas",
                                     "Kayıtları kopyalamak için bellek yetmiyor"));
        return -1;
    }
    /* every file next to its save as <name>.ps4mount-new, then each one renamed over it */
    int rc = 0;
    why[0] = 0;
    for (int i = 0; i < np && !rc; i++)
        for (int k = 0; k < 2 && !rc; k++) {
            const char *name = k ? plan[i].key : plan[i].img;
            char s[PATH_MAX], t[PATH_MAX], c[PATH_MAX];
            struct stat like;
            if (pathf(s, sizeof(s), "%s/%s", src, name) || pathf(c, sizeof(c), "%s/%s", dst, name) ||
                pathf(t, sizeof(t), "%s/%s" NEW_SUFFIX, dst, name)) {
                snprintf(why, sizeof(why), TR("a path is too long: %s", "una ruta es demasiado larga: %s", "bir yol çok uzun: %s"), name);
                rc = -1;
            } else if (stat(c, &like)) {
                snprintf(why, sizeof(why), "%s: %s", c, strerror(errno));
                rc = -1;
            } else
                rc = copy_file(s, t, &like, buf, why, sizeof(why));
        }
    mem_free(buf);
    if (!rc && saves_open && saves_open(tid)) { /* started while the files were copied */
        snprintf(why, sizeof(why), "%s", TR("the game was started meanwhile", "el juego se abrió mientras tanto",
                                            "oyun bu sırada açıldı"));
        rc = -1;
    }
    if (rc) {
        for (int i = 0; i < np; i++)
            for (int k = 0; k < 2; k++) {
                char t[PATH_MAX];
                if (!pathf(t, sizeof(t), "%s/%s" NEW_SUFFIX, dst, k ? plan[i].key : plan[i].img))
                    unlink(t);
            }
        snprintf(msg, cap, TR("Nothing changed: %s", "No se cambió nada: %s", "Hiçbir şey değişmedi: %s"), why);
        return -1;
    }
    int put = 0;
    for (int i = 0; i < np; i++) {
        int ok = 1;
        for (int k = 0; k < 2; k++) {
            const char *name = k ? plan[i].key : plan[i].img;
            char t[PATH_MAX], c[PATH_MAX];
            if (pathf(t, sizeof(t), "%s/%s" NEW_SUFFIX, dst, name) || pathf(c, sizeof(c), "%s/%s", dst, name)) {
                ok = 0;
                continue;
            }
            if (rename(t, c)) {
                printf("[saves] %s: %s: %s\n", tid, c, strerror(errno));
                unlink(t);
                ok = 0;
            }
        }
        put += ok;
    }
    printf("[saves] %s of user %s: %d save(s) put back from %s, %d left out\n", tid, user, put, src, skipped);
    if (put < np) {
        snprintf(msg, cap,
                 TR("Only %d of %d save(s) were put back: the copy made just before is on the drive",
                    "Solo se devolvieron %d de %d partida(s): la copia hecha justo antes está en la unidad",
                    "%d / %d kayıt geri yüklenebildi: hemen önce alınan kopya sürücüde duruyor"),
                 put, np);
        return put ? put : -1;
    }
    if (skipped)
        snprintf(msg, cap,
                 TR("%d save(s) put back; %d of the copy are not on the console with the same size and were left out",
                    "%d partida(s) devuelta(s); %d de la copia no están en la consola con el mismo tamaño y se dejaron",
                    "%d kayıt geri yüklendi; kopyadaki %d kayıt konsolda aynı boyutta olmadığı için atlandı"),
                 put, skipped);
    else
        snprintf(msg, cap, TR("%d save(s) put back", "%d partida(s) devuelta(s)", "%d kayıt geri yüklendi"), put);
    return put;
}
