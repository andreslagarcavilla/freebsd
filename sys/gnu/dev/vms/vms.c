/*
 * vms.c
 */

/*-
 *
 * Copyright (c) 2013-2014 Andres Lagar-Cavilla <andres@lagarcavilla.org>
 * (Gridcentric Inc.)
 * All rights reserved.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License..
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kbio.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/selinfo.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>
#include <vm/vm_page.h>

static struct cdev *vmsdev = NULL;
static char *static_output = NULL;
static unsigned int static_output_length;

MALLOC_DEFINE(M_VMS, "vms", "VMS kernel module memory");

static int
build_static_output(void)
{
    int rv;
#define MAXSZ   512

    static_output = malloc(MAXSZ, M_VMS, M_WAITOK | M_ZERO);
    if (unlikely(static_output == NULL))
        return (1);

    rv = snprintf(static_output, MAXSZ,
             "abi = 1.0\n"
             "word_length = %u\n"
             "vm_page_queues = %lx"
             "pq_inactive = %u"
             "sizeof_vpgqueues = %u\n"
             "off_phys_addr = %u\n",
             sizeof(vm_paddr_t),
             (unsigned long) vm_page_queues,
             PQ_INACTIVE,
             sizeof(struct vpgqueues),
             offsetof(struct vm_page, phys_addr));
    if (unlikely(rv < 0))
    {
        free(static_output, M_VMS);
        static_output = NULL;
        return (1);
    }

    static_output_length = rv;
    return (0);
}

static int
vmsopen(struct cdev *dev __unused, int flags __unused, int fmt __unused,
        struct thread *td)
{
    return (0);
}

static int
vmsrw(struct cdev *dev __unused, struct uio *uio, int flags __unused)
{
    u_long count, off;
    struct iovec *iov;
    int error = 0;

    if (unlikely(static_output == NULL))
        return EINVAL;
    if (unlikely(uio->uio_rw != UIO_READ))
        return EINVAL;

    while (uio->uio_resid > 0 && error == 0) {
        iov = uio->uio_iov;
        if (iov->iov_len == 0) {
            uio->uio_iov++;
            uio->uio_iovcnt--;
            if (unlikely(uio->uio_iovcnt < 0))
                panic("vmsrw");
            continue;
        }

        off = uio->uio_offset;
        if (unlikely(off >= static_output_length))
            return (error);
        count = min(uio->uio_resid, (u_int)(static_output_length - off));
        error = uiomove(static_output + off, (int) count, uio);
        continue;
    }

    return (error);
}

static struct cdevsw vms_cdevsw = {
    .d_version  = D_VERSION,
    .d_open     = vmsopen,
    .d_read     = vmsrw,
    .d_name     = "vms",
};

static int
vms_modevent(module_t mod __unused, int type, void *data __unused)
{
    switch (type) {
    case MOD_LOAD:
        if (build_static_output())
            return ENOMEM;
        vmsdev = make_dev(&vms_cdevsw, 0,
                          UID_ROOT, GID_ROOT, 0440, "vms");
        if (bootverbose && (memdev != NULL))
            printf("VMS helper loaded.\n")
        break;

    case MOD_UNLOAD:
        if (likely(vmsdev != NULL))
        {
            destroy_dev(vmsdev);
            vmsdev = NULL;
        }
        if (likely(static_output != NULL))
        {
            free(static_output, M_VMS);
            static_output = NULL;
        }
        if (bootverbose)
            printf("VMS helper unloaded.\n")
        break;

    default:
        return (EOPNOTSUPP);
    }

    return (0);
}

DEV_MODULE(vms, vms_modevent, NULL);

