/*
 * This file is part of oFono - Open Source Telephony
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Rémi Denis-Courmont <remi.denis-courmont@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <glib.h>

#include "phonet.h"
#include "socket.h"
#include "pep.h"

struct _GIsiPEP {
	int gprs_fd;
	guint source;
	uint16_t handle;
};


static gboolean g_isi_pep_callback(GIOChannel *channel, GIOCondition cond,
					gpointer data)

{
	GIsiPEP *pep = data;
	int fd = g_io_channel_unix_get_fd(channel);
	int encap = PNPIPE_ENCAP_IP;
	unsigned ifi;
	socklen_t len = sizeof(ifi);

	if (cond & (G_IO_HUP|G_IO_NVAL))
		return FALSE;

	fd = accept(fd, NULL, NULL);
	if (fd == -1)
		return TRUE;
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	if (setsockopt(fd, SOL_PNPIPE, PNPIPE_ENCAP, &encap, sizeof(encap))
	 || getsockopt(fd, SOL_PNPIPE, PNPIPE_IFINDEX, &ifi, &len)) {
		close(fd);
		return TRUE;
	}
	pep->gprs_fd = fd;
	return FALSE;
}

GIsiPEP *g_isi_pep_create(GIsiModem *modem)
{
	GIsiPEP *pep = g_malloc(sizeof(*pep));
	GIOChannel *channel;
	int fd;
	unsigned ifi = g_isi_modem_index(modem);
	char buf[IF_NAMESIZE];

	fd = socket(PF_PHONET, SOCK_SEQPACKET, 0);
	if (fd == -1)
		return NULL;

	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, O_NONBLOCK|fcntl(fd, F_GETFL));

	if (if_indextoname(ifi, buf) == NULL ||
	    setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, buf, IF_NAMESIZE))
		goto error;

	pep->gprs_fd = -1;
	pep->handle = 0;
	if (listen(fd, 1) || ioctl(fd, SIOCPNGETOBJECT, &pep->handle))
		goto error;

	channel = g_io_channel_unix_new(fd);
	g_io_channel_set_close_on_unref(channel, TRUE);
	g_io_channel_set_encoding(channel, NULL, NULL);
	g_io_channel_set_buffered(channel, FALSE);
	pep->source = g_io_add_watch(channel,
					G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL,
					g_isi_pep_callback, pep);
	g_io_channel_unref(channel);
	return pep;
error:
	close(fd);
	g_free(pep);
	return NULL;
}

uint16_t g_isi_pep_get_object(const GIsiPEP *pep)
{
	return pep->handle;
}

void g_isi_pep_destroy(GIsiPEP *pep)
{
	if (pep->gprs_fd != -1)
		close(pep->gprs_fd);
	else
		g_source_remove(pep->source);
	g_free(pep);
}
