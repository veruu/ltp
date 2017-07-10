/*
 * Copyright (C) 2017 Red Hat, Inc.
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Test description: Verify msync() after writing into mmap()-ed file works.
 *
 * Write to mapped region and sync the memory back with file. Check the page
 * is no longer dirty after msync() call.
 */

#include <errno.h>
#include "tst_test.h"

static int test_fd;
static char *mmaped_area;
static int pagemap_fd;
static int pageflags_fd;
static long pagesize;

uint64_t get_dirty_bit(unsigned long addr)
{
	uint64_t pagemap_entry, pfn, index;

	index = (addr / pagesize) * sizeof(uint64_t);
	pagemap_fd = SAFE_OPEN("/proc/self/pagemap", O_RDONLY);
	SAFE_LSEEK(pagemap_fd, index, SEEK_SET);
	SAFE_READ(1, pagemap_fd, &pagemap_entry, sizeof(uint64_t));
	SAFE_CLOSE(pagemap_fd);
	pfn = pagemap_entry & ((1ULL << 55) - 1);
	if (!pfn)
		return 0;
	pageflags_fd = SAFE_OPEN("/proc/kpageflags", O_RDONLY);
	index = pfn * sizeof(uint64_t);
	SAFE_LSEEK(pageflags_fd, index, SEEK_SET);
	SAFE_READ(1, pageflags_fd, &pagemap_entry, sizeof(uint64_t));
	SAFE_CLOSE(pageflags_fd);
	return pagemap_entry & (1ULL << 4);
}

static void setup(void)
{
	SAFE_MKDIR("msync04", 0777);
	/*
	 * Using a new device to avoid running on tmpfs because of different
	 * behavior
	 */
	SAFE_MOUNT(tst_device->dev, "msync04", tst_device->fs_type, 0, NULL);
	pagesize = (off_t)SAFE_SYSCONF(_SC_PAGESIZE);
}

static void test_msync(void)
{
	uint64_t dirty;

	test_fd = SAFE_OPEN("msync04/testfile", O_CREAT | O_TRUNC | O_RDWR);
	SAFE_WRITE(0, test_fd, "AAAAAAAAAA", 10);
	mmaped_area = SAFE_MMAP(NULL, pagesize, PROT_READ | PROT_WRITE,
			MAP_SHARED, test_fd, 0);
	mmaped_area[8] = 'B';
	dirty = get_dirty_bit((unsigned long)mmaped_area);
	if (!dirty) {
		tst_res(TFAIL, "Expected dirty bit to be set after writing to"
				" mmap()-ed area");
		return;
	}
	if (msync(mmaped_area, pagesize, MS_SYNC) < 0) {
		tst_res(TFAIL | TERRNO, "msync() failed with %s",
				tst_strerrno(errno));
		return;
	}
	dirty = get_dirty_bit((unsigned long)mmaped_area);
	if (dirty)
		tst_res(TFAIL, "msync() failed to write dirty page despite"
				" succeeding");
	else
		tst_res(TPASS, "msync() working correctly");
	SAFE_MUNMAP(mmaped_area, pagesize);
	SAFE_CLOSE(test_fd);
}

static void cleanup(void)
{
	SAFE_MUNMAP(mmaped_area, pagesize);
	if (test_fd > 0)
		SAFE_CLOSE(test_fd);
	SAFE_UMOUNT("msync04");
}

static struct tst_test test = {
	.tid = "msync04",
	.test_all = test_msync,
	.setup = setup,
	.cleanup = cleanup,
	.needs_tmpdir = 1,
	.needs_root = 1,
	.needs_device = 1,
	.format_device = 1,
	.min_kver = "2.6.25",
};
