/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Copies of saved data on a drive (src/saves.c), against a made-up /user/home and drive.
 * Build and run with: bash test/run.sh */
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lang.h"
#include "saves.h"

static int g_failed, g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

#define TID "CUSA00411"
#define OTHER "CUSA07408"
#define USER "1eb71bbd"
#define USER2 "10000000"

static char g_home[PATH_MAX], g_drive[PATH_MAX];

/* snprintf for paths (the temporary folder keeps them short) */
static void fmt(char *out, size_t n, const char *f, ...) __attribute__((format(printf, 3, 4)));
static void fmt(char *out, size_t n, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    vsnprintf(out, n, f, ap);
    va_end(ap);
}

static void mkdirs(const char *path) {
    char tmp[PATH_MAX];
    fmt(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    mkdir(tmp, 0777);
}

/* A file of n bytes, all c. */
static void put(const char *dir, const char *name, size_t n, int c) {
    char p[PATH_MAX];
    mkdirs(dir);
    fmt(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    for (size_t i = 0; f && i < n; i++)
        fputc(c, f);
    if (f)
        fclose(f);
}

/* The first byte of a file and its size; -1 when it is not there. */
static int first_byte(const char *dir, const char *name, long *size) {
    char p[PATH_MAX];
    fmt(p, sizeof(p), "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f)
        return -1;
    int c = fgetc(f);
    fseek(f, 0, SEEK_END);
    if (size)
        *size = ftell(f);
    fclose(f);
    return c;
}

static void console_dir(char *out, size_t n, const char *user, const char *tid) {
    fmt(out, n, "%s/%s/savedata/%s", g_home, user, tid);
}

static void copy_dir(char *out, size_t n, const char *user, const char *tid, const char *stamp) {
    fmt(out, n, "%s/" SAVES_DIR "/%s/%s/%s", g_drive, user, tid, stamp);
}

/* A clock that moves a second each time a copy is named. */
static time_t g_now = 1798754400; /* 2027-01-01 */
static time_t fake_clock(time_t *t) {
    time_t v = g_now++;
    if (t)
        *t = v;
    return v;
}

static int count_entries(const char *dir, const char *suffix) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    while (d && (e = readdir(d))) {
        size_t l = strlen(e->d_name), s = strlen(suffix);
        if (e->d_name[0] != '.' && l >= s && !strcmp(e->d_name + l - s, suffix))
            n++;
    }
    if (d)
        closedir(d);
    return n;
}

static void test_names(void) {
    printf("names that may become paths\n");
    CHECK(saves_valid_tid("CUSA00411") && saves_valid_tid("SMWR00001"));
    CHECK(!saves_valid_tid("cusa00411") && !saves_valid_tid("CUSA0041") && !saves_valid_tid("../../etc") && !saves_valid_tid(NULL));
    CHECK(saves_valid_user("1eb71bbd") && saves_valid_user("10000000"));
    CHECK(!saves_valid_user("1eb71bb") && !saves_valid_user("1eb71bbz") && !saves_valid_user(".."));
    CHECK(saves_valid_stamp("20261101-231500") && saves_valid_stamp("20261101-231500-" SAVES_TAG_RESTORE));
    CHECK(!saves_valid_stamp("20261101-231500.part") && !saves_valid_stamp("../20261101-2315") &&
          !saves_valid_stamp("2026110-1231500") && !saves_valid_stamp("20261101-231500-x"));
}

static void test_fingerprint(void) {
    printf("fingerprint of the saves on the console\n");
    char dir[PATH_MAX];
    CHECK(saves_fp(TID) == 0); /* none yet */
    console_dir(dir, sizeof(dir), USER, TID);
    put(dir, "sdimg_SGTA50000", 4096, 'A');
    put(dir, "SGTA50000.bin", 96, 'k');
    uint64_t a = saves_fp(TID), bytes = 0;
    CHECK(a != 0 && a == saves_fp(TID));
    CHECK(saves_on_console(TID, &bytes) == 1 && bytes == 4096 + 96);
    CHECK(saves_fp(OTHER) == 0 && saves_on_console(OTHER, NULL) == 0);
    sleep(1); /* a new time for the file */
    put(dir, "sdimg_SGTA50000", 4096, 'B');
    CHECK(saves_fp(TID) != a); /* the game saved */
}

static void test_backup(void) {
    printf("a copy on the drive\n");
    char msg[512], dir[PATH_MAX];
    save_copy_t list[16];
    CHECK(saves_backup(OTHER, g_drive, NULL, 5, NULL, msg, sizeof(msg)) == 0); /* no saves of that game */
    CHECK(saves_list(OTHER, g_drive, list, 16) == 0);
    int n = saves_backup(TID, g_drive, NULL, 5, NULL, msg, sizeof(msg));
    CHECK(n == 1);
    n = saves_list(TID, g_drive, list, 16);
    CHECK(n == 1 && !strcmp(list[0].user, USER) && list[0].files == 2 && list[0].bytes == 4096 + 96 && !list[0].before_restore);
    copy_dir(dir, sizeof(dir), USER, TID, list[0].stamp);
    long size = 0;
    CHECK(first_byte(dir, "sdimg_SGTA50000", &size) == 'B' && size == 4096);
    CHECK(first_byte(dir, "SGTA50000.bin", &size) == 'k' && size == 96);
    char base[PATH_MAX];
    fmt(base, sizeof(base), "%s/" SAVES_DIR "/" USER "/" TID, g_drive);
    CHECK(count_entries(base, ".part") == 0);
    CHECK(saves_backup("../x/../y", g_drive, NULL, 5, NULL, msg, sizeof(msg)) == -1);
}

static void test_keep(void) {
    printf("only the newest copies are kept\n");
    char msg[512], base[PATH_MAX];
    save_copy_t list[16];
    for (int i = 0; i < 6; i++)
        CHECK(saves_backup(TID, g_drive, NULL, 3, NULL, msg, sizeof(msg)) == 1);
    int n = saves_list(TID, g_drive, list, 16);
    CHECK(n == 3);
    CHECK(n == 3 && strcmp(list[0].stamp, list[1].stamp) > 0 && strcmp(list[1].stamp, list[2].stamp) > 0); /* newest first */
    /* a .part folder an interrupted copy left goes at the next copy */
    fmt(base, sizeof(base), "%s/" SAVES_DIR "/" USER "/" TID "/20260101-000000.part", g_drive);
    put(base, "sdimg_SGTA50000", 10, 'x');
    CHECK(saves_backup(TID, g_drive, NULL, 3, NULL, msg, sizeof(msg)) == 1);
    fmt(base, sizeof(base), "%s/" SAVES_DIR "/" USER "/" TID, g_drive);
    CHECK(count_entries(base, ".part") == 0);
    CHECK(saves_list(TID, g_drive, list, 16) == 3);
}

static void test_restore(void) {
    printf("putting a copy back\n");
    char msg[512], dir[PATH_MAX];
    save_copy_t list[16];
    int n = saves_list(TID, g_drive, list, 16);
    CHECK(n == 3);
    char older[48];
    fmt(older, sizeof(older), "%s", list[n - 1].stamp); /* has 'B' */
    console_dir(dir, sizeof(dir), USER, TID);
    sleep(1);
    put(dir, "sdimg_SGTA50000", 4096, 'C'); /* played on: the console has 'C' now */
    int put_back = saves_restore(TID, g_drive, USER, older, 3, msg, sizeof(msg));
    CHECK(put_back == 1);
    CHECK(first_byte(dir, "sdimg_SGTA50000", NULL) == 'B');
    CHECK(count_entries(dir, ".ps4mount-new") == 0);
    /* what the console had went to a before-restore copy first */
    n = saves_list(TID, g_drive, list, 16);
    int found = 0;
    for (int i = 0; i < n; i++)
        if (list[i].before_restore) {
            char c[PATH_MAX];
            copy_dir(c, sizeof(c), USER, TID, list[i].stamp);
            found += first_byte(c, "sdimg_SGTA50000", NULL) == 'C';
        }
    CHECK(found == 1 && n == 4);
    /* and that one can be put back too: undoing the restore */
    for (int i = 0; i < n; i++)
        if (list[i].before_restore)
            CHECK(saves_restore(TID, g_drive, USER, list[i].stamp, 3, msg, sizeof(msg)) == 1);
    CHECK(first_byte(dir, "sdimg_SGTA50000", NULL) == 'C');
}

static void test_restore_refused(void) {
    printf("what is not put back\n");
    char msg[512], dir[PATH_MAX], cdir[PATH_MAX];
    save_copy_t list[16];
    int n = saves_list(TID, g_drive, list, 16);
    CHECK(n >= 1);
    /* a save the console no longer has with that size (deleted and made again) */
    console_dir(dir, sizeof(dir), USER, TID);
    put(dir, "sdimg_SGTA50000", 8192, 'D');
    CHECK(saves_restore(TID, g_drive, USER, list[0].stamp, 3, msg, sizeof(msg)) == -1);
    CHECK(first_byte(dir, "sdimg_SGTA50000", NULL) == 'D'); /* untouched */
    CHECK(strstr(msg, "same size") != NULL);
    /* a save the copy has and the console lacks is left out, the others go back */
    put(dir, "sdimg_SGTA50000", 4096, 'D');
    copy_dir(cdir, sizeof(cdir), USER, TID, list[0].stamp);
    put(cdir, "sdimg_SGTA50001", 512, 'z');
    put(cdir, "SGTA50001.bin", 96, 'z');
    CHECK(saves_restore(TID, g_drive, USER, list[0].stamp, 3, msg, sizeof(msg)) == 1);
    CHECK(first_byte(dir, "sdimg_SGTA50001", NULL) == -1 && strstr(msg, "left out") != NULL);
    /* a save without its key is not put back */
    char lone[48];
    fmt(lone, sizeof(lone), "%s", list[0].stamp);
    copy_dir(cdir, sizeof(cdir), USER, TID, lone);
    char key[PATH_MAX];
    fmt(key, sizeof(key), "%s/SGTA50000.bin", cdir);
    unlink(key);
    CHECK(saves_restore(TID, g_drive, USER, lone, 3, msg, sizeof(msg)) == -1);
    /* no saves of the game for that user on the console: start the game first */
    CHECK(saves_restore(OTHER, g_drive, USER, lone, 3, msg, sizeof(msg)) == -1);
    /* names that are no copy */
    CHECK(saves_restore(TID, g_drive, USER, "../../../../etc", 3, msg, sizeof(msg)) == -1);
    CHECK(saves_restore(TID, g_drive, "..", lone, 3, msg, sizeof(msg)) == -1);
    CHECK(saves_restore(TID, g_drive, USER, "20990101-000000", 3, msg, sizeof(msg)) == -1); /* not on the drive */
}

/* A game started while the files were copied: nothing is replaced. */
static int g_open_now;
static int fake_open(const char *tid) {
    (void)tid;
    return g_open_now;
}

static void test_started_meanwhile(void) {
    printf("a game started while a copy is put back\n");
    char msg[512], dir[PATH_MAX];
    save_copy_t list[16];
    int n = saves_list(TID, g_drive, list, 16), plain = -1;
    for (int i = 0; i < n && plain < 0; i++)
        if (!list[i].before_restore)
            plain = i;
    CHECK(plain >= 0);
    if (plain < 0)
        return;
    console_dir(dir, sizeof(dir), USER, TID);
    put(dir, "sdimg_SGTA50000", 4096, 'S');
    put(dir, "SGTA50000.bin", 96, 'k');
    saves_open = fake_open;
    g_open_now = 1;
    CHECK(saves_restore(TID, g_drive, USER, list[plain].stamp, 3, msg, sizeof(msg)) == -1);
    CHECK(strstr(msg, "started meanwhile") != NULL);
    CHECK(first_byte(dir, "sdimg_SGTA50000", NULL) == 'S'); /* untouched */
    CHECK(count_entries(dir, ".ps4mount-new") == 0);
    g_open_now = 0;
    saves_open = NULL;
}

static void test_two_users(void) {
    printf("two users\n");
    char msg[512], d1[PATH_MAX], d2[PATH_MAX];
    save_copy_t list[32];
    console_dir(d1, sizeof(d1), USER, TID);
    console_dir(d2, sizeof(d2), USER2, TID);
    put(d1, "sdimg_SGTA50000", 4096, 'E');
    put(d1, "SGTA50000.bin", 96, 'k');
    put(d2, "sdimg_SGTA50000", 4096, 'F');
    put(d2, "SGTA50000.bin", 96, 'm');
    CHECK(saves_on_console(TID, NULL) == 2);
    CHECK(saves_backup(TID, g_drive, NULL, 3, NULL, msg, sizeof(msg)) == 2);
    int n = saves_list(TID, g_drive, list, 32), u2 = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i].user, USER2) && u2 < 0)
            u2 = i;
    CHECK(u2 >= 0);
    put(d1, "sdimg_SGTA50000", 4096, 'G');
    put(d2, "sdimg_SGTA50000", 4096, 'H');
    if (u2 >= 0)
        CHECK(saves_restore(TID, g_drive, USER2, list[u2].stamp, 3, msg, sizeof(msg)) == 1);
    CHECK(first_byte(d2, "sdimg_SGTA50000", NULL) == 'F'); /* that user's copy */
    CHECK(first_byte(d1, "sdimg_SGTA50000", NULL) == 'G'); /* the other user untouched */
    CHECK(saves_backup(TID, g_drive, USER2, 3, NULL, msg, sizeof(msg)) == 1); /* one user only */
}

/* A save that changes while it is copied fails the copy and leaves nothing. */
static char g_changing[PATH_MAX];
static void change_it(void) {
    FILE *f = fopen(g_changing, "ab");
    if (f) {
        fputc('!', f);
        fclose(f);
    }
}

static void test_changed_meanwhile(void) {
    printf("a save that changes while it is copied\n");
    char msg[512], dir[PATH_MAX], base[PATH_MAX];
    save_copy_t list[32];
    console_dir(dir, sizeof(dir), USER, OTHER);
    put(dir, "sdimg_BIG", 20u << 20, 'Q'); /* big enough for the pump to run */
    put(dir, "BIG.bin", 96, 'q');
    fmt(g_changing, sizeof(g_changing), "%s/sdimg_BIG", dir);
    saves_pump = change_it;
    CHECK(saves_backup(OTHER, g_drive, USER, 3, NULL, msg, sizeof(msg)) == -1);
    saves_pump = NULL;
    CHECK(strstr(msg, "changed while it was copied") != NULL);
    CHECK(saves_list(OTHER, g_drive, list, 32) == 0);
    fmt(base, sizeof(base), "%s/" SAVES_DIR "/" USER "/" OTHER, g_drive);
    CHECK(count_entries(base, ".part") == 0);
    CHECK(saves_backup(OTHER, g_drive, USER, 3, NULL, msg, sizeof(msg)) == 1); /* settled: it copies */
}

int main(void) {
    char tmp[] = "/tmp/saves_test.XXXXXX";
    if (!mkdtemp(tmp))
        return 1;
    fmt(g_home, sizeof(g_home), "%s/user/home", tmp);
    fmt(g_drive, sizeof(g_drive), "%s/mnt/usb0", tmp);
    mkdirs(g_home);
    mkdirs(g_drive);
    saves_home = g_home;
    saves_clock = fake_clock;
    g_lang = LANG_EN;
    test_names();
    test_fingerprint();
    test_backup();
    test_keep();
    test_restore();
    test_restore_refused();
    test_started_meanwhile();
    test_two_users();
    test_changed_meanwhile();
    char cmd[PATH_MAX + 16];
    fmt(cmd, sizeof(cmd), "rm -rf '%s'", tmp);
    if (system(cmd))
        printf("  (could not remove %s)\n", tmp);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
