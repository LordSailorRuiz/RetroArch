/* Copyright  (C) 2010-2019 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (core_updater_list.c).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <file/file_path.h>
#include <string/stdstring.h>
#include <lists/string_list.h>
#include <net/net_http.h>
#include <array/rbuf.h>
#include <retro_miscellaneous.h>

#include "file_path_special.h"
#include "core_info.h"

#include "core_updater_list.h"

/* Holds all entries in a core updater list */
struct core_updater_list
{
   core_updater_list_entry_t *entries;
   enum core_updater_list_type type;
};

/* Cached ('global') core updater list */
static core_updater_list_t *core_list_cached = NULL;

/**************************************/
/* Initialisation / De-Initialisation */
/**************************************/

/* Frees contents of specified core updater
 * list entry */
static void core_updater_list_free_entry(core_updater_list_entry_t *entry)
{
   if (!entry)
      return;

   if (entry->remote_filename)
   {
      free(entry->remote_filename);
      entry->remote_filename = NULL;
   }

   if (entry->remote_core_path)
   {
      free(entry->remote_core_path);
      entry->remote_core_path = NULL;
   }

   if (entry->local_core_path)
   {
      free(entry->local_core_path);
      entry->local_core_path = NULL;
   }

   if (entry->local_info_path)
   {
      free(entry->local_info_path);
      entry->local_info_path = NULL;
   }

   if (entry->display_name)
   {
      free(entry->display_name);
      entry->display_name = NULL;
   }

   if (entry->description)
   {
      free(entry->description);
      entry->description = NULL;
   }

   if (entry->licenses_list)
   {
      string_list_free(entry->licenses_list);
      entry->licenses_list = NULL;
   }
}

/* Creates a new, empty core updater list.
 * Returns a handle to a new core_updater_list_t object
 * on success, otherwise returns NULL. */
core_updater_list_t *core_updater_list_init(void)
{
   /* Create core updater list */
   core_updater_list_t *core_list = (core_updater_list_t*)
         malloc(sizeof(*core_list));

   if (!core_list)
      return NULL;

   /* Initialise members */
   core_list->entries = NULL;
   core_list->type    = CORE_UPDATER_LIST_TYPE_UNKNOWN;

   return core_list;
}

/* Resets (removes all entries of) specified core
 * updater list */
void core_updater_list_reset(core_updater_list_t *core_list)
{
   if (!core_list)
      return;

   if (core_list->entries)
   {
      size_t i;

      for (i = 0; i < RBUF_LEN(core_list->entries); i++)
         core_updater_list_free_entry(&core_list->entries[i]);

      RBUF_FREE(core_list->entries);
   }

   core_list->type = CORE_UPDATER_LIST_TYPE_UNKNOWN;
}

/* Frees specified core updater list */
void core_updater_list_free(core_updater_list_t *core_list)
{
   if (!core_list)
      return;

   core_updater_list_reset(core_list);
   free(core_list);
}

/***************/
/* Cached List */
/***************/

/* Creates a new, empty cached core updater list
 * (i.e. 'global' list).
 * Returns false in the event of an error. */
bool core_updater_list_init_cached(void)
{
   /* Free any existing cached core updater list */
   if (core_list_cached)
   {
      core_updater_list_free(core_list_cached);
      core_list_cached = NULL;
   }

   core_list_cached = core_updater_list_init();

   if (!core_list_cached)
      return false;

   return true;
}

/* Fetches cached core updater list */
core_updater_list_t *core_updater_list_get_cached(void)
{
   if (core_list_cached)
      return core_list_cached;

   return NULL;
}

/* Frees cached core updater list */
void core_updater_list_free_cached(void)
{
   core_updater_list_free(core_list_cached);
   core_list_cached = NULL;
}

/***********/
/* Getters */
/***********/

/* Returns number of entries in core updater list */
size_t core_updater_list_size(core_updater_list_t *core_list)
{
   if (!core_list)
      return 0;

   return RBUF_LEN(core_list->entries);
}

/* Returns 'type' (core delivery method) of
 * specified core updater list */
enum core_updater_list_type core_updater_list_get_type(
      core_updater_list_t *core_list)
{
   if (!core_list)
      return CORE_UPDATER_LIST_TYPE_UNKNOWN;

   return core_list->type;
}

/* Fetches core updater list entry corresponding
 * to the specified entry index.
 * Returns false if index is invalid. */
bool core_updater_list_get_index(
      core_updater_list_t *core_list,
      size_t idx,
      const core_updater_list_entry_t **entry)
{
   if (!core_list || !entry)
      return false;

   if (idx >= RBUF_LEN(core_list->entries))
      return false;

   *entry = &core_list->entries[idx];

   return true;
}

/* Fetches core updater list entry corresponding
 * to the specified remote core filename.
 * Returns false if core is not found. */
bool core_updater_list_get_filename(
      core_updater_list_t *core_list,
      const char *remote_filename,
      const core_updater_list_entry_t **entry)
{
   size_t num_entries;
   size_t i;

   if (!core_list || !entry || string_is_empty(remote_filename))
      return false;

   num_entries = RBUF_LEN(core_list->entries);

   if (num_entries < 1)
      return false;

   /* Search for specified filename */
   for (i = 0; i < num_entries; i++)
   {
      core_updater_list_entry_t *current_entry = &core_list->entries[i];

      if (string_is_empty(current_entry->remote_filename))
         continue;

      if (string_is_equal(remote_filename, current_entry->remote_filename))
      {
         *entry = current_entry;
         return true;
      }
   }

   return false;
}

/* Fetches core updater list entry corresponding
 * to the specified core.
 * Returns false if core is not found. */
bool core_updater_list_get_core(
      core_updater_list_t *core_list,
      const char *local_core_path,
      const core_updater_list_entry_t **entry)
{
   bool resolve_symlinks;
   size_t num_entries;
   size_t i;
   char real_core_path[PATH_MAX_LENGTH];

   if (!core_list || !entry || string_is_empty(local_core_path))
      return false;
   if ((num_entries = RBUF_LEN(core_list->entries)) < 1)
      return false;

   /* Resolve absolute pathname of local_core_path */
   strlcpy(real_core_path, local_core_path, sizeof(real_core_path));
   /* Can't resolve symlinks when dealing with cores
    * installed via play feature delivery, because the
    * source files have non-standard file names (which
    * will not be recognised by regular core handling
    * routines) */
   resolve_symlinks = (core_list->type != CORE_UPDATER_LIST_TYPE_PFD);
   path_resolve_realpath(real_core_path, sizeof(real_core_path),
         resolve_symlinks);

   if (string_is_empty(real_core_path))
      return false;

   /* Search for specified core */
   for (i = 0; i < num_entries; i++)
   {
      core_updater_list_entry_t *current_entry = &core_list->entries[i];

      if (string_is_empty(current_entry->local_core_path))
         continue;

#ifdef _WIN32
      /* Handle case-insensitive operating systems*/
      if (string_is_equal_noncase(real_core_path, current_entry->local_core_path))
      {
#else
      if (string_is_equal(real_core_path, current_entry->local_core_path))
      {
#endif
         *entry = current_entry;
         return true;
      }
   }

   return false;
}

/***********/
/* Setters */
/***********/

/* Parses date string and adds contents to
 * specified core updater list entry */
static bool core_updater_list_set_date(
      core_updater_list_entry_t *entry, const char *date_str)
{
   char *tok, *save   = NULL;
   char *elem0        = NULL;
   char *elem1        = NULL;
   char *elem2        = NULL;
   unsigned list_size = 0;
   char *date_str_cpy = NULL;

   if (!entry || string_is_empty(date_str))
      return false;

   date_str_cpy = strdup(date_str);

   /* Split date string into component values */
   if ((tok = strtok_r(date_str_cpy, "-", &save)))
   {
      elem0 = strdup(tok);
      list_size++;
   }
   if ((tok = strtok_r(NULL, "-", &save)))
   {
      elem1 = strdup(tok);
      list_size++;
   }
   if ((tok = strtok_r(NULL, "-", &save)))
   {
      elem2 = strdup(tok);
      list_size++;
   }
   free(date_str_cpy);

   /* Date string must have 3 values:
    * [year] [month] [day] */
   if (list_size < 3)
   {
      if (elem0)
         free(elem0);
      if (elem1)
         free(elem1);
      if (elem2)
         free(elem2);

      return false;
   }

   /* Convert date string values */
   entry->date.year  = string_to_unsigned(elem0);
   entry->date.month = string_to_unsigned(elem1);
   entry->date.day   = string_to_unsigned(elem2);

   /* Clean up */
   free(elem0);
   free(elem1);
   free(elem2);

   return true;
}

/* Parses crc string and adds value to
 * specified core updater list entry */
static bool core_updater_list_set_crc(
      core_updater_list_entry_t *entry, const char *crc_str)
{
   uint32_t crc;

   if (!entry || string_is_empty(crc_str))
      return false;

   if ((crc = (uint32_t)string_hex_to_unsigned(crc_str)) == 0)
      return false;

   entry->crc = crc;

   return true;
}

/* Parses core filename string and adds all
 * associated paths to the specified core
 * updater list entry */
static bool core_updater_list_set_paths(
      core_updater_list_entry_t *entry,
      const char *path_dir_libretro,
      const char *path_libretro_info,
      const char *network_buildbot_url,
      const char *filename_str,
      enum core_updater_list_type list_type)
{
   char *last_underscore                  = NULL;
   char *tmp_url                          = NULL;
   bool is_archive                        = true;
   /* Can't resolve symlinks when dealing with cores
    * installed via play feature delivery, because the
    * source files have non-standard file names (which
    * will not be recognised by regular core handling
    * routines) */
   char remote_core_path[PATH_MAX_LENGTH];
   char local_core_path[PATH_MAX_LENGTH];
   char local_info_path[PATH_MAX_LENGTH];
   bool resolve_symlinks = (list_type != CORE_UPDATER_LIST_TYPE_PFD);

   if (  !entry
       || string_is_empty(filename_str)
       || string_is_empty(path_dir_libretro)
       || string_is_empty(path_libretro_info))
      return false;

   /* Only buildbot cores require the buildbot URL */
   if ((list_type == CORE_UPDATER_LIST_TYPE_BUILDBOT) &&
       string_is_empty(network_buildbot_url))
      return false;

   /* Check whether remote file is an archive */
   is_archive = path_is_compressed_file(filename_str);

   /* remote_filename */
   if (entry->remote_filename)
   {
      free(entry->remote_filename);
      entry->remote_filename = NULL;
   }

   entry->remote_filename = strdup(filename_str);

   /* remote_core_path
    * > Leave blank if this is not a buildbot core */
   if (list_type == CORE_UPDATER_LIST_TYPE_BUILDBOT)
   {
      fill_pathname_join_special(
            remote_core_path,
            network_buildbot_url,
            filename_str,
            sizeof(remote_core_path));

      /* > Apply proper URL encoding (messy...) */
      tmp_url             = strdup(remote_core_path);
      remote_core_path[0] = '\0';
      net_http_urlencode_full(
            remote_core_path, tmp_url, sizeof(remote_core_path));
      if (tmp_url)
         free(tmp_url);
   }

   if (entry->remote_core_path)
   {
      free(entry->remote_core_path);
      entry->remote_core_path = NULL;
   }

   entry->remote_core_path = strdup(remote_core_path);

   fill_pathname_join_special(
         local_core_path,
         path_dir_libretro,
         filename_str,
         sizeof(local_core_path));

   if (is_archive)
      path_remove_extension(local_core_path);

   path_resolve_realpath(local_core_path, sizeof(local_core_path),
         resolve_symlinks);

   if (entry->local_core_path)
   {
      free(entry->local_core_path);
      entry->local_core_path = NULL;
   }

   entry->local_core_path = strdup(local_core_path);

   fill_pathname_join_special(
         local_info_path,
         path_libretro_info,
         filename_str,
         sizeof(local_info_path));
   path_remove_extension(local_info_path);

   if (is_archive)
      path_remove_extension(local_info_path);

   /* > Remove any non-standard core filename
    *   additions (i.e. info files end with
    *   '_libretro' but core files may have
    *   a platform specific addendum,
    *   e.g. '_android')*/
   last_underscore = (char*)strrchr(local_info_path, '_');

   if (!string_is_empty(last_underscore))
      if (!string_is_equal(last_underscore, "_libretro"))
         *last_underscore = '\0';

   /* > Add proper file extension */
   strlcat(
         local_info_path,
         FILE_PATH_CORE_INFO_EXTENSION,
         sizeof(local_info_path));

   if (entry->local_info_path)
   {
      free(entry->local_info_path);
      entry->local_info_path = NULL;
   }

   entry->local_info_path = strdup(local_info_path);

   return true;
}

/* Reads info file associated with core and
 * adds relevant information to updater list
 * entry */
static bool core_updater_list_set_core_info(
      core_updater_list_entry_t *entry,
      const char *local_info_path,
      const char *filename_str)
{
   core_updater_info_t *core_info = NULL;

   if (  !entry
       || string_is_empty(local_info_path)
       || string_is_empty(filename_str))
      return false;

   /* Clear any existing core info */
   if (entry->display_name)
   {
      free(entry->display_name);
      entry->display_name = NULL;
   }

   if (entry->description)
   {
      free(entry->description);
      entry->description = NULL;
   }

   if (entry->licenses_list)
   {
      /* Note: We can safely leave this as NULL if
       * the core info file is invalid */
      string_list_free(entry->licenses_list);
      entry->licenses_list = NULL;
   }

   entry->is_experimental = false;

   /* Read core info file
    * > Note: It's a bit rubbish that we have to
    *   read the actual core info files here...
    *   Would be better to cache this globally
    *   (at present, we only cache info for
    *    *installed* cores...) */
   if ((core_info = core_info_get_core_updater_info(local_info_path)))
   {
      /* display_name + is_experimental */
      if (!string_is_empty(core_info->display_name))
      {
         entry->display_name    = strdup(core_info->display_name);
         entry->is_experimental = core_info->is_experimental;
      }
      else
      {
         /* If display name is blank, use core filename and
          * assume core is experimental (i.e. all 'fit for consumption'
          * cores must have a valid/complete core info file) */
         entry->display_name    = strdup(filename_str);
         entry->is_experimental = true;
      }

      /* description */
      if (!string_is_empty(core_info->description))
         entry->description     = strdup(core_info->description);
      else
         entry->description     = strldup("", sizeof(""));

      /* licenses_list */
      if (!string_is_empty(core_info->licenses))
         entry->licenses_list   = string_split(core_info->licenses, "|");

      /* Clean up */
      core_info_free_core_updater_info(core_info);
   }
   else
   {
      /* If info file is missing, use core filename and
       * assume core is experimental (i.e. all 'fit for consumption'
       * cores must have a valid/complete core info file) */
      entry->display_name       = strdup(filename_str);
      entry->is_experimental    = true;
      entry->description        = strldup("", sizeof(""));
   }

   return true;
}

/* Adds entry to the end of the specified core
 * updater list
 * NOTE: Entry string values are passed by
 * reference - *do not free the entry passed
 * to this function* */
static bool core_updater_list_push_entry(
      core_updater_list_t *core_list, core_updater_list_entry_t *entry)
{
   core_updater_list_entry_t *list_entry = NULL;
   size_t num_entries;

   if (!core_list || !entry)
      return false;

   /* Get current number of list entries */
   num_entries = RBUF_LEN(core_list->entries);

   /* Attempt to allocate memory for new entry */
   if (!RBUF_TRYFIT(core_list->entries, num_entries + 1))
      return false;

   /* Allocation successful - increment array size */
   RBUF_RESIZE(core_list->entries, num_entries + 1);

   /* Get handle of new entry at end of list, and
    * zero-initialise members */
   list_entry = &core_list->entries[num_entries];
   memset(list_entry, 0, sizeof(*list_entry));

   /* Assign paths */
   list_entry->remote_filename  = entry->remote_filename;
   list_entry->remote_core_path = entry->remote_core_path;
   list_entry->local_core_path  = entry->local_core_path;
   list_entry->local_info_path  = entry->local_info_path;

   /* Assign core info */
   list_entry->display_name     = entry->display_name;
   list_entry->description      = entry->description;
   list_entry->licenses_list    = entry->licenses_list;
   list_entry->is_experimental  = entry->is_experimental;

   /* Copy crc */
   list_entry->crc              = entry->crc;

   /* Copy date */
   memcpy(&list_entry->date, &entry->date, sizeof(core_updater_list_date_t));

   return true;
}

/* Parses the contents of a single buildbot
 * core listing and adds it to the specified
 * core updater list */
static void core_updater_list_add_entry(
      core_updater_list_t *core_list,
      const char *path_dir_libretro,
      const char *path_libretro_info,
      const char *network_buildbot_url,
      const char *date_str,
      const char *crc_str,
      const char *filename_str)
{
   const core_updater_list_entry_t *search_entry = NULL;
   core_updater_list_entry_t entry               = {0};

   /* Check whether core file is already included
    * in the list (this is *not* an error condition,
    * it just means we can skip the current listing) */
   if (core_updater_list_get_filename(core_list,
         filename_str, &search_entry))
      goto error;

   /* Parse individual listing strings */
   if (!core_updater_list_set_date(&entry, date_str))
      goto error;

   if (!core_updater_list_set_crc(&entry, crc_str))
      goto error;

   if (!core_updater_list_set_paths(
            &entry,
            path_dir_libretro,
            path_libretro_info,
            network_buildbot_url,
            filename_str,
            CORE_UPDATER_LIST_TYPE_BUILDBOT))
      goto error;

   if (!core_updater_list_set_core_info(
         &entry,
         entry.local_info_path,
         filename_str))
      goto error;

   /* Add entry to list */
   if (!core_updater_list_push_entry(core_list, &entry))
      goto error;

   return;

error:
   /* This is not a *fatal* error - it just
    * means one of the following:
    * - The current line of entry text received
    *   from the buildbot is broken somehow
    *   (could be the case that the network buffer
    *    wasn't large enough to cache the entire
    *    string, so the last line was truncated)
    * - We had insufficient memory to allocate a new
    *   entry in the core updater list
    * In either case, the current entry is discarded
    * and we move on to the next one
    * (network transfers are fishy business, so we
    * choose to ignore this sort of error - don't
    * want the whole fetch to fail because of a
    * trivial glitch...) */
   core_updater_list_free_entry(&entry);
}

/* Enhanced metadata for WizModl dual-level grouping */
typedef struct {
   const char *core_name;           /* Core name pattern to match */
   const char *manufacturer;        /* Nintendo, Sony, Sega, etc. */
   const char *console_model;       /* Specific console model name */
   const char *console_type;        /* home, portable, arcade, computer */
   int release_year;               /* Console release year */
   int manufacturer_priority;       /* Manufacturer ordering (lower = first) */
   int console_priority;           /* Console ordering within manufacturer */
} core_metadata_wizmodl_t;

/* Comprehensive metadata database for dual-level grouping */
static const core_metadata_wizmodl_t core_metadata_wizmodl_db[] = {
   /* Nintendo - Home Consoles */
   {"Family Computer", "Nintendo", "Nintendo Entertainment System", "home", 1983, 1, 10},
   {"Famicom", "Nintendo", "Nintendo Entertainment System", "home", 1983, 1, 10},
   {"FCEUmm", "Nintendo", "Nintendo Entertainment System", "home", 1983, 1, 10},
   {"Nestopia", "Nintendo", "Nintendo Entertainment System", "home", 1983, 1, 10},
   {"QuickNES", "Nintendo", "Nintendo Entertainment System", "home", 1983, 1, 10},
   
   {"Super Nintendo", "Nintendo", "Super Nintendo Entertainment System", "home", 1990, 1, 20},
   {"Snes9x", "Nintendo", "Super Nintendo Entertainment System", "home", 1990, 1, 20},
   {"bsnes", "Nintendo", "Super Nintendo Entertainment System", "home", 1990, 1, 20},
   {"higan", "Nintendo", "Super Nintendo Entertainment System", "home", 1990, 1, 20},
   
   {"Nintendo 64", "Nintendo", "Nintendo 64", "home", 1996, 1, 30},
   {"Mupen64Plus", "Nintendo", "Nintendo 64", "home", 1996, 1, 30},
   {"ParaLLEl", "Nintendo", "Nintendo 64", "home", 1996, 1, 30},
   
   {"GameCube", "Nintendo", "Nintendo GameCube", "home", 2001, 1, 40},
   {"Dolphin", "Nintendo", "Nintendo GameCube", "home", 2001, 1, 40},
   
   {"Wii", "Nintendo", "Nintendo Wii", "home", 2006, 1, 50},
   
   /* Nintendo - Portable Consoles */
   {"Game Boy", "Nintendo", "Game Boy", "portable", 1989, 1, 100},
   {"SameBoy", "Nintendo", "Game Boy", "portable", 1989, 1, 100},
   {"Gambatte", "Nintendo", "Game Boy", "portable", 1989, 1, 100},
   {"TGB Dual", "Nintendo", "Game Boy", "portable", 1989, 1, 100},
   
   {"Game Boy Color", "Nintendo", "Game Boy Color", "portable", 1998, 1, 110},
   
   {"Game Boy Advance", "Nintendo", "Game Boy Advance", "portable", 2001, 1, 120},
   {"mGBA", "Nintendo", "Game Boy Advance", "portable", 2001, 1, 120},
   {"VBA", "Nintendo", "Game Boy Advance", "portable", 2001, 1, 120},
   {"VBA-M", "Nintendo", "Game Boy Advance", "portable", 2001, 1, 120},
   
   {"Nintendo DS", "Nintendo", "Nintendo DS", "portable", 2004, 1, 130},
   {"DeSmuME", "Nintendo", "Nintendo DS", "portable", 2004, 1, 130},
   {"melonDS", "Nintendo", "Nintendo DS", "portable", 2004, 1, 130},
   
   {"Nintendo 3DS", "Nintendo", "Nintendo 3DS", "portable", 2011, 1, 140},
   {"Citra", "Nintendo", "Nintendo 3DS", "portable", 2011, 1, 140},
   
   /* Sony - Home Consoles */
   {"PlayStation", "Sony", "PlayStation", "home", 1994, 2, 10},
   {"PCSX", "Sony", "PlayStation", "home", 1994, 2, 10},
   {"Beetle PSX", "Sony", "PlayStation", "home", 1994, 2, 10},
   {"SwanStation", "Sony", "PlayStation", "home", 1994, 2, 10},
   
   {"PlayStation 2", "Sony", "PlayStation 2", "home", 2000, 2, 20},
   {"PCSX2", "Sony", "PlayStation 2", "home", 2000, 2, 20},
   
   {"PlayStation 3", "Sony", "PlayStation 3", "home", 2006, 2, 30},
   {"RPCS3", "Sony", "PlayStation 3", "home", 2006, 2, 30},
   
   /* Sony - Portable Consoles */
   {"PlayStation Portable", "Sony", "PlayStation Portable", "portable", 2004, 2, 100},
   {"PPSSPP", "Sony", "PlayStation Portable", "portable", 2004, 2, 100},
   
   {"PlayStation Vita", "Sony", "PlayStation Vita", "portable", 2011, 2, 110},
   {"Vita3K", "Sony", "PlayStation Vita", "portable", 2011, 2, 110},
   
   /* Sega - Home Consoles */
   {"Master System", "Sega", "Sega Master System", "home", 1986, 3, 10},
   {"SMS Plus", "Sega", "Sega Master System", "home", 1986, 3, 10},
   
   {"Genesis", "Sega", "Sega Genesis/Mega Drive", "home", 1988, 3, 20},
   {"Mega Drive", "Sega", "Sega Genesis/Mega Drive", "home", 1988, 3, 20},
   {"Genesis Plus GX", "Sega", "Sega Genesis/Mega Drive", "home", 1988, 3, 20},
   {"PicoDrive", "Sega", "Sega Genesis/Mega Drive", "home", 1988, 3, 20},
   
   {"Sega CD", "Sega", "Sega CD", "home", 1991, 3, 25},
   
   {"32X", "Sega", "Sega 32X", "home", 1994, 3, 28},
   
   {"Saturn", "Sega", "Sega Saturn", "home", 1994, 3, 30},
   {"Beetle Saturn", "Sega", "Sega Saturn", "home", 1994, 3, 30},
   {"Yabause", "Sega", "Sega Saturn", "home", 1994, 3, 30},
   {"Kronos", "Sega", "Sega Saturn", "home", 1994, 3, 30},
   
   {"Dreamcast", "Sega", "Sega Dreamcast", "home", 1998, 3, 40},
   {"Flycast", "Sega", "Sega Dreamcast", "home", 1998, 3, 40},
   {"Redream", "Sega", "Sega Dreamcast", "home", 1998, 3, 40},
   
   /* Sega - Portable Consoles */
   {"Game Gear", "Sega", "Sega Game Gear", "portable", 1990, 3, 100},
   
   /* Atari - Home Consoles */
   {"Atari 2600", "Atari", "Atari 2600", "home", 1977, 4, 10},
   {"Stella", "Atari", "Atari 2600", "home", 1977, 4, 10},
   
   {"Atari 5200", "Atari", "Atari 5200", "home", 1982, 4, 20},
   
   {"Atari 7800", "Atari", "Atari 7800", "home", 1986, 4, 30},
   {"ProSystem", "Atari", "Atari 7800", "home", 1986, 4, 30},
   
   {"Atari Jaguar", "Atari", "Atari Jaguar", "home", 1993, 4, 40},
   {"Virtual Jaguar", "Atari", "Atari Jaguar", "home", 1993, 4, 40},
   
   /* Atari - Portable Consoles */
   {"Atari Lynx", "Atari", "Atari Lynx", "portable", 1989, 4, 100},
   {"Handy", "Atari", "Atari Lynx", "portable", 1989, 4, 100},
   
   /* SNK */
   {"Neo Geo", "SNK", "Neo Geo", "home", 1990, 5, 10},
   {"FinalBurn Neo", "SNK", "Neo Geo", "home", 1990, 5, 10},
   {"Neo Geo Pocket", "SNK", "Neo Geo Pocket", "portable", 1998, 5, 100},
   {"RACE", "SNK", "Neo Geo Pocket", "portable", 1998, 5, 100},
   
   /* NEC */
   {"PC Engine", "NEC", "PC Engine/TurboGrafx-16", "home", 1987, 6, 10},
   {"Beetle PCE", "NEC", "PC Engine/TurboGrafx-16", "home", 1987, 6, 10},
   {"TurboGrafx", "NEC", "PC Engine/TurboGrafx-16", "home", 1987, 6, 10},
   {"PC-FX", "NEC", "PC-FX", "home", 1994, 6, 20},
   
   /* Bandai */
   {"WonderSwan", "Bandai", "WonderSwan", "portable", 1999, 7, 100},
   {"Beetle Cygne", "Bandai", "WonderSwan", "portable", 1999, 7, 100},
   
   /* Arcade */
   {"MAME", "Arcade", "Multiple Arcade Systems", "arcade", 1972, 8, 10},
   {"Final Burn", "Arcade", "Multiple Arcade Systems", "arcade", 1972, 8, 10},
   {"FBNeo", "Arcade", "Multiple Arcade Systems", "arcade", 1972, 8, 10},
   
   /* Computer Systems */
   {"Commodore 64", "Commodore", "Commodore 64", "computer", 1982, 9, 10},
   {"VICE", "Commodore", "Commodore 64", "computer", 1982, 9, 10},
   {"Amiga", "Commodore", "Amiga", "computer", 1985, 9, 20},
   {"PUAE", "Commodore", "Amiga", "computer", 1985, 9, 20},
   
   {"MSX", "Microsoft", "MSX", "computer", 1983, 10, 10},
   {"blueMSX", "Microsoft", "MSX", "computer", 1983, 10, 10},
   
   {"DOS", "IBM", "IBM PC Compatible", "computer", 1981, 11, 10},
   {"DOSBox", "IBM", "IBM PC Compatible", "computer", 1981, 11, 10},
   
   /* Unknown/Fallback */
   {NULL, "Unknown", "Unknown System", "unknown", 9999, 999, 999}
};

/* Get enhanced metadata for a core with console model details */
static const core_metadata_wizmodl_t* get_core_metadata_wizmodl(const char *display_name) {
   int i;
   const core_metadata_wizmodl_t *fallback = &core_metadata_wizmodl_db[
      sizeof(core_metadata_wizmodl_db)/sizeof(core_metadata_wizmodl_db[0]) - 1];
   
   if (string_is_empty(display_name))
      return fallback;
   
   /* Find the best match based on core name pattern */
   for (i = 0; i < (int)(sizeof(core_metadata_wizmodl_db)/sizeof(core_metadata_wizmodl_db[0]) - 1); i++) {
      if (core_metadata_wizmodl_db[i].core_name && 
          strstr(display_name, core_metadata_wizmodl_db[i].core_name)) {
         return &core_metadata_wizmodl_db[i];
      }
   }
   
   return fallback;
}

/* Create a manufacturer-level header entry */
static core_updater_list_entry_t* create_manufacturer_header(const char *manufacturer) {
   core_updater_list_entry_t *header;
   char header_text[256];
   
   if (string_is_empty(manufacturer))
      return NULL;
   
   header = (core_updater_list_entry_t*)malloc(sizeof(*header));
   if (!header)
      return NULL;
   
   memset(header, 0, sizeof(*header));
   
   /* Create manufacturer header display text */
   snprintf(header_text, sizeof(header_text), "=== %s ===", manufacturer);
   
   /* Set manufacturer header properties */
   header->remote_filename        = strdup(header_text);
   header->display_name           = strdup(header_text);
   header->description            = strdup("");
   header->is_experimental        = false;
   header->is_manufacturer_header = true;
   header->is_console_header      = false;
   header->crc                    = 0;
   
   return header;
}

/* Create a console model-level header entry */
static core_updater_list_entry_t* create_console_header(const char *console_model, int release_year) {
   core_updater_list_entry_t *header;
   char header_text[256];
   
   if (string_is_empty(console_model))
      return NULL;
   
   header = (core_updater_list_entry_t*)malloc(sizeof(*header));
   if (!header)
      return NULL;
   
   memset(header, 0, sizeof(*header));
   
   /* Create console header display text with year */
   if (release_year > 0 && release_year < 9999)
      snprintf(header_text, sizeof(header_text), "--- %s (%d) ---", console_model, release_year);
   else
      snprintf(header_text, sizeof(header_text), "--- %s ---", console_model);
   
   /* Set console header properties */
   header->remote_filename        = strdup(header_text);
   header->display_name           = strdup(header_text);
   header->description            = strdup("");
   header->is_experimental        = false;
   header->is_manufacturer_header = false;
   header->is_console_header      = true;
   header->crc                    = 0;
   
   return header;
}

/* WizModl sorting function with dual-level grouping */
static int core_updater_list_qsort_func_wizmodl(
      const core_updater_list_entry_t *a, const core_updater_list_entry_t *b)
{
   const core_metadata_wizmodl_t *meta_a, *meta_b;
   int manufacturer_cmp, console_cmp, type_cmp, year_cmp;
   
   if (!a || !b)
      return 0;

   /* Headers always come before regular entries */
   if ((a->is_manufacturer_header || a->is_console_header) && 
       !(b->is_manufacturer_header || b->is_console_header))
      return -1;
   if (!(a->is_manufacturer_header || a->is_console_header) && 
       (b->is_manufacturer_header || b->is_console_header))
      return 1;
   
   /* Manufacturer headers before console headers */
   if (a->is_manufacturer_header && b->is_console_header)
      return -1;
   if (a->is_console_header && b->is_manufacturer_header)
      return 1;
   
   /* Sort headers alphabetically among themselves */
   if ((a->is_manufacturer_header && b->is_manufacturer_header) ||
       (a->is_console_header && b->is_console_header))
      return strcasecmp(a->display_name, b->display_name);

   if (string_is_empty(a->display_name) || string_is_empty(b->display_name))
      return 0;

   /* Get metadata for both cores */
   meta_a = get_core_metadata_wizmodl(a->display_name);
   meta_b = get_core_metadata_wizmodl(b->display_name);
   
   /* Primary sort: by manufacturer priority */
   if (meta_a->manufacturer_priority != meta_b->manufacturer_priority)
      return (meta_a->manufacturer_priority < meta_b->manufacturer_priority) ? -1 : 1;
   
   /* Secondary sort: by manufacturer name (for same priority) */
   manufacturer_cmp = strcasecmp(meta_a->manufacturer, meta_b->manufacturer);
   if (manufacturer_cmp != 0)
      return manufacturer_cmp;
   
   /* Tertiary sort: by console priority within manufacturer */
   if (meta_a->console_priority != meta_b->console_priority)
      return (meta_a->console_priority < meta_b->console_priority) ? -1 : 1;
   
   /* Quaternary sort: by console model name */
   console_cmp = strcasecmp(meta_a->console_model, meta_b->console_model);
   if (console_cmp != 0)
      return console_cmp;
   
   /* Quinary sort: by console type (home before portable) */
   type_cmp = strcasecmp(meta_a->console_type, meta_b->console_type);
   if (type_cmp != 0)
      return type_cmp;
   
   /* Senary sort: by release year */
   year_cmp = meta_a->release_year - meta_b->release_year;
   if (year_cmp != 0)
      return year_cmp;
   
   /* Final sort: alphabetically by display name for same console */
   return strcasecmp(a->display_name, b->display_name);
}

/* Core updater list qsort helper function (original) */
static int core_updater_list_qsort_func(
      const core_updater_list_entry_t *a, const core_updater_list_entry_t *b)
{
   if (!a || !b)
      return 0;

   if (string_is_empty(a->display_name) || string_is_empty(b->display_name))
      return 0;

   return strcasecmp(a->display_name, b->display_name);
}

/* Inject both manufacturer and console model headers into sorted core list */
static void core_updater_list_inject_dual_headers(core_updater_list_t *core_list)
{
   size_t num_entries;
   core_updater_list_entry_t *new_entries = NULL;
   size_t new_count = 0;
   const char *last_manufacturer = NULL;
   const char *last_console_model = NULL;
   size_t i;
   
   if (!core_list)
      return;
   
   num_entries = RBUF_LEN(core_list->entries);
   if (num_entries == 0)
      return;
   
   /* Estimate maximum new size (original + manufacturer headers + console headers) */
   if (!RBUF_TRYFIT(new_entries, num_entries * 3))
      return;
   
   RBUF_RESIZE(new_entries, 0);
   
   for (i = 0; i < num_entries; i++)
   {
      const core_updater_list_entry_t *entry = &core_list->entries[i];
      const core_metadata_wizmodl_t *meta;
      core_updater_list_entry_t *new_entry;
      
      if (!entry || string_is_empty(entry->display_name))
         continue;
      
      meta = get_core_metadata_wizmodl(entry->display_name);
      
      /* Check if we need to insert a new manufacturer header */
      if (!last_manufacturer || strcasecmp(last_manufacturer, meta->manufacturer) != 0)
      {
         core_updater_list_entry_t *mfg_header = create_manufacturer_header(meta->manufacturer);
         if (mfg_header)
         {
            if (RBUF_TRYFIT(new_entries, new_count + 1))
            {
               RBUF_RESIZE(new_entries, new_count + 1);
               memcpy(&new_entries[new_count], mfg_header, sizeof(*mfg_header));
               new_count++;
            }
            free(mfg_header);
         }
         last_manufacturer = meta->manufacturer;
         last_console_model = NULL; /* Reset console tracking for new manufacturer */
      }
      
      /* Check if we need to insert a new console model header */
      if (!last_console_model || strcasecmp(last_console_model, meta->console_model) != 0)
      {
         core_updater_list_entry_t *console_header = create_console_header(
            meta->console_model, meta->release_year);
         if (console_header)
         {
            if (RBUF_TRYFIT(new_entries, new_count + 1))
            {
               RBUF_RESIZE(new_entries, new_count + 1);
               memcpy(&new_entries[new_count], console_header, sizeof(*console_header));
               new_count++;
            }
            free(console_header);
         }
         last_console_model = meta->console_model;
      }
      
      /* Add the original core entry */
      if (RBUF_TRYFIT(new_entries, new_count + 1))
      {
         RBUF_RESIZE(new_entries, new_count + 1);
         new_entry = &new_entries[new_count];
         memset(new_entry, 0, sizeof(*new_entry));
         
         /* Copy all fields from original entry */
         new_entry->remote_filename        = entry->remote_filename ? strdup(entry->remote_filename) : NULL;
         new_entry->remote_core_path       = entry->remote_core_path ? strdup(entry->remote_core_path) : NULL;
         new_entry->local_core_path        = entry->local_core_path ? strdup(entry->local_core_path) : NULL;
         new_entry->local_info_path        = entry->local_info_path ? strdup(entry->local_info_path) : NULL;
         new_entry->display_name           = entry->display_name ? strdup(entry->display_name) : NULL;
         new_entry->description            = entry->description ? strdup(entry->description) : NULL;
         new_entry->is_experimental        = entry->is_experimental;
         new_entry->is_manufacturer_header = false;
         new_entry->is_console_header      = false;
         new_entry->crc                    = entry->crc;
         memcpy(&new_entry->date, &entry->date, sizeof(core_updater_list_date_t));
         
         /* Copy licenses list if present */
         if (entry->licenses_list)
            new_entry->licenses_list = entry->licenses_list;
         
         new_count++;
      }
   }
   
   /* Replace the original entries with the new list including dual headers */
   if (new_count > 0)
   {
      /* Free original entries */
      for (i = 0; i < num_entries; i++)
         core_updater_list_free_entry(&core_list->entries[i]);
      RBUF_FREE(core_list->entries);
      
      /* Assign new entries */
      core_list->entries = new_entries;
   }
   else
   {
      /* Cleanup on failure */
      RBUF_FREE(new_entries);
   }
}

/* Enhanced sorting with dual header injection */
static void core_updater_list_qsort_wizmodl(core_updater_list_t *core_list)
{
   size_t num_entries;

   if (!core_list)
      return;
   if ((num_entries = RBUF_LEN(core_list->entries)) < 2)
      return;
   
   /* First, sort the existing entries using WizModl algorithm */
   if (core_list->entries)
      qsort(
         core_list->entries, num_entries,
         sizeof(core_updater_list_entry_t),
         (int (*)(const void *, const void *))
               core_updater_list_qsort_func_wizmodl);
   
   /* Then inject dual-level headers (manufacturer + console model) */
   core_updater_list_inject_dual_headers(core_list);
}

/* Sorts core updater list into alphabetical order */
static void core_updater_list_qsort(core_updater_list_t *core_list)
{
   size_t num_entries;

   if (!core_list)
      return;
   if ((num_entries = RBUF_LEN(core_list->entries)) < 2)
      return;
   if (core_list->entries)
      qsort(
         core_list->entries, num_entries,
         sizeof(core_updater_list_entry_t),
         (int (*)(const void *, const void *))
               core_updater_list_qsort_func);
}

/* Reads the contents of a buildbot core list
 * network request into the specified
 * core_updater_list_t object.
 * Returns false in the event of an error. */
bool core_updater_list_parse_network_data(
      core_updater_list_t *core_list,
      const char *path_dir_libretro,
      const char *path_libretro_info,
      const char *network_buildbot_url,
      const char *data, size_t len)
{
   char *tok, *save   = NULL;
   unsigned list_size = 0;
   char *data_buf     = NULL;

   /* Sanity check */
   if (!core_list || string_is_empty(data) || (len < 1))
      return false;

   /* We're populating a list 'from scratch' - remove
    * any existing entries */
   core_updater_list_reset(core_list);

   /* Input data string is not terminated - have
    * to copy it to a temporary buffer... */
   if (!(data_buf = (char*)malloc((len + 1) * sizeof(char))))
      return false;

   memcpy(data_buf, data, len * sizeof(char));
   data_buf[len] = '\0';

   list_size     = string_count_occurrences_single_character(data_buf, '\n');

   if (list_size < 1)
   {
      free(data_buf);
      return false;
   }

   /* Split network listing request into lines */
   /* Loop over lines */
   for (tok = strtok_r(data_buf, "\n", &save); tok;
        tok = strtok_r(NULL, "\n", &save))
   {
      char *tok2, *save2 = NULL;
      char *elem0        = NULL;
      char *elem1        = NULL;
      char *elem2        = NULL;
      char *line_cpy     = NULL;
      const char *line   = tok;

      if (string_is_empty(line))
         continue;

      line_cpy = strdup(line);

      /* Split line into listings info components */
      if ((tok2 = strtok_r(line_cpy, " ", &save2)))
         elem0 = strdup(tok2); /* date */
      if ((tok2 = strtok_r(NULL, " ", &save2)))
         elem1 = strdup(tok2); /* crc  */
      if ((tok2 = strtok_r(NULL, " ", &save2)))
         elem2 = strdup(tok2); /* filename */

      free(line_cpy);

      /* Parse listings info and add to core updater
       * list */
      /* > Listings must have 3 entries:
       *   [date] [crc] [filename] */
      if (     !string_is_empty(elem0)
            && !string_is_empty(elem1)
            && !string_is_empty(elem2))
         core_updater_list_add_entry(
               core_list,
               path_dir_libretro,
               path_libretro_info,
               network_buildbot_url,
               elem0, elem1, elem2);

      /* Clean up */
      free(elem0);
      free(elem1);
      free(elem2);
   }

   /* Temporary data buffer is no longer required */
   free(data_buf);
   data_buf = NULL;

   /* Sanity check */
   if (RBUF_LEN(core_list->entries) < 1)
      return false;

   /* Sort completed list using WizModl algorithm */
   core_updater_list_qsort_wizmodl(core_list);

   /* Set list type */
   core_list->type = CORE_UPDATER_LIST_TYPE_BUILDBOT;

   return true;
}

/* Parses a single play feature delivery core
 * listing and adds it to the specified core
 * updater list */
static void core_updater_list_add_pfd_entry(
      core_updater_list_t *core_list,
      const char *path_dir_libretro,
      const char *path_libretro_info,
      const char *filename_str)
{
   const core_updater_list_entry_t *search_entry = NULL;
   core_updater_list_entry_t entry               = {0};

   if (!core_list || string_is_empty(filename_str))
      goto error;

   /* Check whether core file is already included
    * in the list (this is *not* an error condition,
    * it just means we can skip the current listing) */
   if (core_updater_list_get_filename(core_list,
         filename_str, &search_entry))
      goto error;

   /* Note: Play feature delivery cores have no
    * timestamp or CRC info - leave these fields
    * zero initialised */

   /* Populate entry fields */
   if (!core_updater_list_set_paths(
            &entry,
            path_dir_libretro,
            path_libretro_info,
            NULL,
            filename_str,
            CORE_UPDATER_LIST_TYPE_PFD))
      goto error;

   if (!core_updater_list_set_core_info(
         &entry,
         entry.local_info_path,
         filename_str))
      goto error;

   /* Add entry to list */
   if (!core_updater_list_push_entry(core_list, &entry))
      goto error;

   return;

error:
   /* This is not a *fatal* error - it just
    * means one of the following:
    * - The core listing entry obtained from the
    *   play feature delivery interface is broken
    *   somehow
    * - We had insufficient memory to allocate a new
    *   entry in the core updater list
    * In either case, the current entry is discarded
    * and we move on to the next one */
   core_updater_list_free_entry(&entry);
}

/* Reads the list of cores currently available
 * via play feature delivery (PFD) into the
 * specified core_updater_list_t object.
 * Returns false in the event of an error. */
bool core_updater_list_parse_pfd_data(
      core_updater_list_t *core_list,
      const char *path_dir_libretro,
      const char *path_libretro_info,
      const struct string_list *pfd_cores)
{
   size_t i;

   /* Sanity check */
   if (!core_list || !pfd_cores || (pfd_cores->size < 1))
      return false;

   /* We're populating a list 'from scratch' - remove
    * any existing entries */
   core_updater_list_reset(core_list);

   /* Loop over play feature delivery core list */
   for (i = 0; i < pfd_cores->size; i++)
   {
      const char *filename_str = pfd_cores->elems[i].data;

      if (string_is_empty(filename_str))
         continue;

      /* Parse core file name and add to core
       * updater list */
      core_updater_list_add_pfd_entry(
            core_list,
            path_dir_libretro,
            path_libretro_info,
            filename_str);
   }

   /* Sanity check */
   if (RBUF_LEN(core_list->entries) < 1)
      return false;

   /* Sort completed list using WizModl algorithm */
   core_updater_list_qsort_wizmodl(core_list);

   /* Set list type */
   core_list->type = CORE_UPDATER_LIST_TYPE_PFD;

   return true;
}
