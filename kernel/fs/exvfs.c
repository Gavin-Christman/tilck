
#include <common/basic_defs.h>
#include <common/string_util.h>

#include <exos/fs/exvfs.h>
#include <exos/kmalloc.h>
#include <exos/errno.h>

#include "fs_int.h"

static u32 next_device_id;

/*
 * Returns:
 *  - 0 in case of non match.
 *  - strlen(mp) in case of a match
 */
STATIC int check_mountpoint_match(const char *mp, const char *path)
{
   int m = 0;
   const size_t lm = strlen(mp);
   const size_t lp = strlen(path);
   const size_t min_len = MIN(lm, lp);

   /*
    * Mount points MUST end with '/'.
    */
   ASSERT(mp[lm-1] == '/');

   for (size_t i = 0; i < min_len; i++) {

      if (mp[i] != path[i])
         break;

      m++;
   }

   /*
    * We assume that both the paths are absolute. Therefore, at least the
    * initial '/' must match.
    */
   ASSERT(m > 0);

   if (mp[m] != 0) {

      /*
       * The match stopped before the end of mount point's path.
       * Therefore, there is no match.
       */

      return 0;
   }

   if (path[m-1] != '/' && path[m-1] != 0) {

      /*
       * The match stopped before the end of a path component in 'path'.
       * In positive cases, the next character after a match (= position 'm')
       * is either a '/' or \0.
       */

      return 0;
   }

   return m;
}

int exvfs_open(const char *path, fs_handle *out)
{
   mountpoint *mp, *best_match = NULL;
   int best_match_len = 0;
   mp_cursor cur;
   int rc;

   ASSERT(path != NULL);

   if (*path != '/')
      panic("exvfs_open() works only with absolute paths");

   mountpoint_iter_begin(&cur);

   while ((mp = mountpoint_get_next(&cur))) {

      int len = check_mountpoint_match(mp->path, path);

      if (len > best_match_len) {
         best_match = mp;
         best_match_len = len;
      }
   }

   if (!best_match) {
      rc = -ENOENT;
      goto out;
   }

   filesystem *fs = best_match->fs;

   exvfs_fs_shlock(fs);
   {
      rc = fs->open(fs, path + best_match_len - 1, out);
   }
   exvfs_fs_shunlock(fs);

out:
   mountpoint_iter_end(&cur);
   return rc;
}

void exvfs_close(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   hb->fs->close(h);
}

int exvfs_dup(fs_handle h, fs_handle *dup_h)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb)
      return -EBADF;

   int rc = hb->fs->dup(h, dup_h);

   if (rc)
      return rc;

   ASSERT(*dup_h != NULL);
   return 0;
}

ssize_t exvfs_read(fs_handle h, void *buf, size_t buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ssize_t ret;

   if (!hb->fops.read)
      return -EINVAL;

   exvfs_shlock(h);
   {
      ret = hb->fops.read(h, buf, buf_size);
   }
   exvfs_shunlock(h);
   return ret;
}

ssize_t exvfs_write(fs_handle h, void *buf, size_t buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ssize_t ret;

   if (!hb->fops.write)
      return -EINVAL;

   exvfs_exlock(h);
   {
      ret = hb->fops.write(h, buf, buf_size);
   }
   exvfs_exunlock(h);
   return ret;
}

off_t exvfs_seek(fs_handle h, off_t off, int whence)
{
   fs_handle_base *hb = (fs_handle_base *) h;

   if (!hb->fops.seek)
      return -ESPIPE;

   return hb->fops.seek(h, off, whence);
}

int exvfs_ioctl(fs_handle h, uptr request, void *argp)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   if (!hb->fops.ioctl)
      return -ENOTTY; // Yes, ENOTTY IS the right error. See the man page.

   exvfs_exlock(h);
   {
      ret = hb->fops.ioctl(h, request, argp);
   }
   exvfs_exunlock(h);
   return ret;
}

int exvfs_stat(fs_handle h, struct stat *statbuf)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   int ret;

   ASSERT(hb->fops.stat != NULL); /* stat is NOT optional */

   exvfs_shlock(h);
   {
      ret = hb->fops.stat(h, statbuf);
   }
   exvfs_shunlock(h);
   return ret;
}

void exvfs_exlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.exlock) {
      hb->fops.exlock(h);
   } else {
      ASSERT(!hb->fops.exunlock);
      exvfs_fs_exlock(get_fs(h));
   }
}

void exvfs_exunlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.exunlock) {
      hb->fops.exunlock(h);
   } else {
      ASSERT(!hb->fops.exlock);
      exvfs_fs_exunlock(get_fs(h));
   }
}

void exvfs_shlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.shlock) {
      hb->fops.shlock(h);
   } else {
      ASSERT(!hb->fops.shunlock);
      exvfs_fs_shlock(get_fs(h));
   }
}

void exvfs_shunlock(fs_handle h)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);

   if (hb->fops.shunlock) {
      hb->fops.shunlock(h);
   } else {
      ASSERT(!hb->fops.shlock);
      exvfs_fs_shunlock(get_fs(h));
   }
}

void exvfs_fs_exlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exlock);

   fs->fs_exlock(fs);
}

void exvfs_fs_exunlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_exunlock);

   fs->fs_exunlock(fs);
}

void exvfs_fs_shlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shlock);

   fs->fs_shlock(fs);
}

void exvfs_fs_shunlock(filesystem *fs)
{
   ASSERT(fs != NULL);
   ASSERT(fs->fs_shunlock);

   fs->fs_shunlock(fs);
}

int exvfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 buf_size)
{
   fs_handle_base *hb = (fs_handle_base *) h;
   ASSERT(hb != NULL);
   ASSERT(hb->fs->getdents64);

   exvfs_fs_shlock(hb->fs);
   {
      // NOTE: the fs implementation MUST handle an invalid user 'dirp' pointer.
      hb->fs->getdents64(h, dirp, buf_size);
   }
   exvfs_fs_shunlock(hb->fs);
   return -ENOSYS;
}

u32 exvfs_get_new_device_id(void)
{
   return next_device_id++;
}
