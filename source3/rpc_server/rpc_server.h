/*
 *  RPC Server helper headers
 *  Almost completely rewritten by (C) Jeremy Allison 2005 - 2010
 *  Copyright (C) Simo Sorce <idra@samba.org> - 2010
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

#ifndef _RPC_SERVER_H_
#define _RPC_SERVER_H_

#include "librpc/rpc/rpc_common.h" /* For enum dcerpc_transport_t */

#include "librpc/rpc/dcesrv_core.h"

struct pipes_struct;
struct auth_session_info;
struct cli_credentials;

typedef void (*dcerpc_ncacn_termination_fn)(struct dcesrv_connection *,
					    void *);

struct dcerpc_ncacn_conn {
	int sock;

	struct pipes_struct *p;
	dcerpc_ncacn_termination_fn termination_fn;
	void *termination_data;

	struct tevent_context *ev_ctx;
	struct messaging_context *msg_ctx;
	struct dcesrv_context *dce_ctx;
	struct dcesrv_endpoint *endpoint;

	struct tstream_context *tstream;

	struct tsocket_address *remote_client_addr;
	char *remote_client_name;
	struct tsocket_address *local_server_addr;
	char *local_server_name;
	struct auth_session_info *session_info;
};

NTSTATUS dcerpc_ncacn_conn_init(TALLOC_CTX *mem_ctx,
				struct tevent_context *ev_ctx,
				struct messaging_context *msg_ctx,
				struct dcesrv_context *dce_ctx,
				struct dcesrv_endpoint *endpoint,
				dcerpc_ncacn_termination_fn term_fn,
				void *termination_data,
				struct dcerpc_ncacn_conn **out);

void set_incoming_fault(struct pipes_struct *p);
void process_complete_pdu(struct pipes_struct *p, struct ncacn_packet *pkt);

struct dcerpc_ncacn_listen_state;
int dcesrv_setup_ncacn_listener(
	TALLOC_CTX *mem_ctx,
	struct dcesrv_context *dce_ctx,
	struct tevent_context *ev_ctx,
	struct messaging_context *msg_ctx,
	struct dcesrv_endpoint *e,
	int *fd,
	dcerpc_ncacn_termination_fn term_fn,
	void *termination_data,
	struct dcerpc_ncacn_listen_state **listen_state);

void dcerpc_ncacn_accept(struct tevent_context *ev_ctx,
			 struct messaging_context *msg_ctx,
			 struct dcesrv_context *dce_ctx,
			 struct dcesrv_endpoint *e,
			 struct tsocket_address **cli_addr,
			 struct tsocket_address **srv_addr,
			 int s,
			 dcerpc_ncacn_termination_fn termination_fn,
			 void *termination_data);

NTSTATUS dcesrv_auth_gensec_prepare(
	TALLOC_CTX *mem_ctx,
	struct dcesrv_call_state *call,
	struct gensec_security **out,
	void *private_data);
void dcesrv_log_successful_authz(
	struct dcesrv_call_state *call,
	void *private_data);
NTSTATUS dcesrv_assoc_group_find(
	struct dcesrv_call_state *call,
	void *private_data);

NTSTATUS dcesrv_endpoint_by_ncacn_np_name(struct dcesrv_context *dce_ctx,
					  const char *endpoint,
					  struct dcesrv_endpoint **out);

struct pipes_struct *dcesrv_get_pipes_struct(struct dcesrv_connection *conn);

void dcesrv_transport_terminate_connection(struct dcesrv_connection *dce_conn,
					   const char *reason);

#endif /* _PRC_SERVER_H_ */
