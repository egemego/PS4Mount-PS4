/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Copies of a game's saved data on a drive, and putting one back.
 *
 * The console keeps each user's saves of a game in /user/home/<user>/savedata/<TITLE_ID>/:
 * for every save an encrypted image (sdimg_<name>) and its sealed key (<name>.bin). A copy
 * is those files as they are (still encrypted, so for this console and this user), taken
 * while the game is closed, in <drive>/PS4Mount/saves/<user>/<TITLE_ID>/<stamp>/.
 *
 * The saves the game uses stay on the console: an image the game writes to must not be on
 * a drive that rest mode powers off or that can be pulled out mid-write.
 *
 * Putting a copy back only overwrites saves the console already has, with images of the
 * same size: the system's save database, which only the system writes, already lists
 * them. The saves on the console are copied first (a "before-restore" copy), so putting
 * one back can be undone. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define SAVES_DIR "PS4Mount/saves" /* below a drive's root */
#define SAVES_TAG_RESTORE "before-restore"
#define SAVES_KEEP_RESTORE 3 /* before-restore copies kept per user and game */
#define SAVES_MAX_COPIES 64

typedef struct {
    char user[16];      /* the user's folder in /user/home */
    char stamp[48];     /* the copy's folder: YYYYMMDD-HHMMSS, or YYYYMMDD-HHMMSS-before-restore */
    uint64_t bytes;
    int files;
    int before_restore; /* made by saves_restore before it put another copy back */
} save_copy_t;

/* The folder the users' saves are read from and put back to ("/user/home"; tests point it
 * elsewhere). */
extern const char *saves_home;
/* Called every few MB while files are copied (the daemon serves its panel and keeps the
 * console awake there). NULL: never called. */
extern void (*saves_pump)(void);
/* The clock that names copies (time; tests set their own). */
extern time_t (*saves_clock)(time_t *);
/* 1 while tid is open: checked again by saves_restore right before it replaces the files
 * (the pump may have served a launch meanwhile). NULL: not checked. */
extern int (*saves_open)(const char *tid);

/* 1 for a title ID (CUSA12345, or a homebrew port's SMWR00001), a user folder (8 hex
 * digits) and a copy's folder name: what may become part of a path. */
int saves_valid_tid(const char *tid);
int saves_valid_user(const char *user);
int saves_valid_stamp(const char *stamp);

/* A fingerprint of tid's saves on the console, of every user (names, sizes and times of
 * the files): it changes when the game saves. 0 when the console has none. */
uint64_t saves_fp(const char *tid);
/* How many users have saves of tid on the console, and their size. */
int saves_on_console(const char *tid, uint64_t *bytes);

/* Copies the saves of tid (of user, or of every user with NULL) to
 * root/SAVES_DIR/<user>/<tid>/<stamp>/ and keeps the newest `keep` copies (tag NULL) or
 * SAVES_KEEP_RESTORE (tag SAVES_TAG_RESTORE). A copy is written to a .part folder and
 * named only once every file is in it with its size; a save that changes meanwhile fails
 * the copy. Returns the users copied (0: the console has no saves of tid), -1 on an error
 * that msg explains. */
int saves_backup(const char *tid, const char *root, const char *user, int keep, const char *tag, char *msg, size_t cap);

/* The copies of tid on root, newest first. Returns how many (up to max). */
int saves_list(const char *tid, const char *root, save_copy_t *out, int max);

/* Puts the copy root/SAVES_DIR/<user>/<tid>/<stamp> back on the console: each save of the
 * copy the console has too, with an image of the same size (both its files, or neither).
 * The console's saves of that user are copied first (before-restore); without that copy
 * nothing is changed. Returns the saves put back (>0), -1 when none was: msg says why. */
int saves_restore(const char *tid, const char *root, const char *user, const char *stamp, int keep, char *msg, size_t cap);
