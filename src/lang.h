/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The language of notifications and of the messages the panel shows: the console's, or
 * the one chosen in the settings. */
#pragma once

#include <stddef.h>

/* Columns of TR, in this order; a language missing from a TR falls back to English. */
enum { LANG_EN, LANG_ES, LANG_TR, LANG_COUNT };

extern int g_lang;

/* TR("English", "Español", "Türkçe"): the text in g_lang. Every text takes the same
 * arguments in the same order (test/policy.sh checks the conversions); a TR with fewer
 * texts shows the English one for the languages it lacks. */
#define TR(...) lang_pick((const char *const[]){__VA_ARGS__}, sizeof((const char *const[]){__VA_ARGS__}) / sizeof(char *))
const char *lang_pick(const char *const *texts, size_t n);

/* "en", "es", "tr": the language of the panel's texts. */
const char *lang_tag(int lang);
/* LANG_* for a tag ("es", "en", "tr"), -1 for one that is not known. */
int lang_from_tag(const char *tag);
/* LANG_* for a console language code (sceSystemServiceParamGetInt, parameter 1):
 * Spanish and Spanish (Latin America) are Spanish, Turkish (19) Turkish, any other English. */
int lang_from_console(int code);
