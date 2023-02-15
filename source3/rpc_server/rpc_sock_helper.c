/*
 *  Unix SMB/CIFS implementation.
 *
 *  RPC Socket Helper
 *
 *  Copyright (c) 2011      Andreas Schneider <asn@samba.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "ntdomain.h"

#include "../lib/tsocket/tsocket.h"
#include "librpc/rpc/dcerpc_ep.h"
#include "librpc/rpc/dcesrv_core.h"
#include "rpc_server/rpc_sock_helper.h"
#include "lib/server_prefork.h"
#include "librpc/ndr/ndr_table.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_RPC_SRV

static NTSTATUS dcesrv_open_ncacn_ip_tcp_sockets(
	struct dcesrv_endpoint *e,
	TALLOC_CTX *mem_ctx,
	size_t *pnum_fds,
	int **pfds)
{
	uint16_t port = 0;
	char port_str[6];
	const char *endpoint = NULL;
	size_t i = 0, num_fds;
	int *fds = NULL;
	struct samba_sockaddr *addrs = NULL;
	NTSTATUS status = NT_STATUS_INVALID_PARAMETER;
	bool ok;

	endpoint = dcerpc_binding_get_string_option(
		e->ep_description, "endpoint");
	if (endpoint != NULL) {
		port = atoi(endpoint);
	}

	if (lp_interfaces() && lp_bind_interfaces_only()) {
		num_fds = iface_count();
	} else {
		num_fds = 1;
#ifdef HAVE_IPV6
		num_fds += 1;
#endif
	}

	addrs = talloc_array(mem_ctx, struct samba_sockaddr, num_fds);
	if (addrs == NULL) {
		status = NT_STATUS_NO_MEMORY;
		goto fail;
	}
	fds = talloc_array(mem_ctx, int, num_fds);
	if (fds == NULL) {
		status = NT_STATUS_NO_MEMORY;
		goto fail;
	}

	/*
	 * Fill "addrs"
	 */

	if (lp_interfaces() && lp_bind_interfaces_only()) {
		for (i=0; i<num_fds; i++) {
			const struct sockaddr_storage *ifss =
				iface_n_sockaddr_storage(i);

			ok = sockaddr_storage_to_samba_sockaddr(
				&addrs[i], ifss);
			if (!ok) {
				i = 0; /* nothing to close */
				goto fail;
			}
		}
	} else {
		struct sockaddr_storage ss = { .ss_family = 0 };

#ifdef HAVE_IPV6
		ok = interpret_string_addr(
			&ss, "::", AI_NUMERICHOST|AI_PASSIVE);
		if (!ok) {
			goto fail;
		}
		ok = sockaddr_storage_to_samba_sockaddr(&addrs[0], &ss);
		if (!ok) {
			goto fail;
		}
#endif
		ok = interpret_string_addr(
			&ss, "0.0.0.0", AI_NUMERICHOST|AI_PASSIVE);
		if (!ok) {
			goto fail;
		}

		/* num_fds set above depending on HAVE_IPV6 */
		ok = sockaddr_storage_to_samba_sockaddr(
			&addrs[num_fds-1], &ss);
		if (!ok) {
			goto fail;
		}
	}

	for (i=0; i<num_fds; i++) {
		status = dcesrv_create_ncacn_ip_tcp_socket(
			&addrs[i].u.ss, &port, &fds[i]);
		if (!NT_STATUS_IS_OK(status)) {
			goto fail;
		}
		samba_sockaddr_set_port(&addrs[i], port);
	}

	/* Set the port in the endpoint */
	snprintf(port_str, sizeof(port_str), "%u", port);

	status = dcerpc_binding_set_string_option(
		e->ep_description, "endpoint", port_str);
	if (!NT_STATUS_IS_OK(status)) {
		DBG_ERR("Failed to set binding endpoint '%s': %s\n",
			port_str, nt_errstr(status));
		goto fail;
	}

	TALLOC_FREE(addrs);

	*pfds = fds;
	*pnum_fds = num_fds;

	return NT_STATUS_OK;

fail:
	while (i > 0) {
		close(fds[i-1]);
		i -= 1;
	}
	TALLOC_FREE(fds);
	TALLOC_FREE(addrs);
	return status;
}

NTSTATUS dcesrv_create_ncacn_ip_tcp_sockets(struct dcesrv_endpoint *e,
					    struct pf_listen_fd *listen_fd,
					    int *listen_fd_size)
{
	TALLOC_CTX *tmp_ctx;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;
	int *fds = NULL;
	size_t i, num_fds = 0;

	tmp_ctx = talloc_stackframe();
	if (tmp_ctx == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	status = dcesrv_open_ncacn_ip_tcp_sockets(e, tmp_ctx, &num_fds, &fds);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	for (i=0; i<num_fds; i++) {
		listen_fd[*listen_fd_size].fd = fds[i];
		listen_fd[*listen_fd_size].fd_data = e;
		*listen_fd_size += 1;
	}

	status = NT_STATUS_OK;
done:
	talloc_free(tmp_ctx);
	return status;
}

NTSTATUS dcesrv_setup_ncacn_ip_tcp_sockets(struct tevent_context *ev_ctx,
					   struct messaging_context *msg_ctx,
					   struct dcesrv_context *dce_ctx,
					   struct dcesrv_endpoint *e,
					   dcerpc_ncacn_termination_fn t_fn,
					   void *t_data)
{
	TALLOC_CTX *tmp_ctx;
	NTSTATUS status;

	tmp_ctx = talloc_stackframe();
	if (tmp_ctx == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	if (lp_interfaces() && lp_bind_interfaces_only()) {
		uint32_t num_ifs = iface_count();
		uint32_t i;

		/*
		 * We have been given an interfaces line, and been told to only
		 * bind to those interfaces. Create a socket per interface and
		 * bind to only these.
		 */

		/* Now open a listen socket for each of the interfaces. */
		for (i = 0; i < num_ifs; i++) {
			const struct sockaddr_storage *ifss =
					iface_n_sockaddr_storage(i);

			status = dcesrv_setup_ncacn_ip_tcp_socket(ev_ctx,
								  msg_ctx,
								  dce_ctx,
								  e,
								  ifss,
								  t_fn,
								  t_data);
			if (!NT_STATUS_IS_OK(status)) {
				goto done;
			}
		}
	} else {
		const char *sock_addr;
		const char *sock_ptr;
		char *sock_tok;

#ifdef HAVE_IPV6
		sock_addr = "::,0.0.0.0";
#else
		sock_addr = "0.0.0.0";
#endif

		for (sock_ptr = sock_addr;
		     next_token_talloc(talloc_tos(), &sock_ptr, &sock_tok, " \t,");
		    ) {
			struct sockaddr_storage ss;

			/* open an incoming socket */
			if (!interpret_string_addr(&ss,
						   sock_tok,
						   AI_NUMERICHOST|AI_PASSIVE)) {
				continue;
			}

			status = dcesrv_setup_ncacn_ip_tcp_socket(ev_ctx,
								  msg_ctx,
								  dce_ctx,
								  e,
								  &ss,
								  t_fn,
								  t_data);
			if (!NT_STATUS_IS_OK(status)) {
				goto done;
			}
		}
	}

	status = NT_STATUS_OK;
done:
	talloc_free(tmp_ctx);
	return status;
}

/* vim: set ts=8 sw=8 noet cindent syntax=c.doxygen: */
