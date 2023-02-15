/*
   Unix SMB/CIFS implementation.

   Database Glue between Samba and the KDC

   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2005-2009
   Copyright (C) Simo Sorce <idra@samba.org> 2010

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.


   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

struct sdb_keys;
struct sdb_entry_ex;


int samba_kdc_set_fixed_keys(krb5_context context,
			     struct samba_kdc_db_context *kdc_db_ctx,
			     const struct ldb_val *secretbuffer,
			     bool is_protected,
			     struct sdb_keys *keys);

krb5_error_code samba_kdc_fetch(krb5_context context,
				struct samba_kdc_db_context *kdc_db_ctx,
				krb5_const_principal principal,
				unsigned flags,
				krb5_kvno kvno,
				struct sdb_entry_ex *entry_ex);

krb5_error_code samba_kdc_firstkey(krb5_context context,
				   struct samba_kdc_db_context *kdc_db_ctx,
				   struct sdb_entry_ex *entry);

krb5_error_code samba_kdc_nextkey(krb5_context context,
				  struct samba_kdc_db_context *kdc_db_ctx,
				  struct sdb_entry_ex *entry);

krb5_error_code
samba_kdc_check_client_matches_target_service(krb5_context context,
			 struct samba_kdc_entry *skdc_entry_client,
			 struct samba_kdc_entry *skdc_entry_server_target);

krb5_error_code
samba_kdc_check_pkinit_ms_upn_match(krb5_context context,
				    struct samba_kdc_db_context *kdc_db_ctx,
				    struct samba_kdc_entry *skdc_entry,
				    krb5_const_principal certificate_principal);

krb5_error_code
samba_kdc_check_s4u2proxy(krb5_context context,
			  struct samba_kdc_db_context *kdc_db_ctx,
			  struct samba_kdc_entry *skdc_entry,
			  krb5_const_principal target_principal);

krb5_error_code samba_kdc_check_s4u2proxy_rbcd(
		krb5_context context,
		struct samba_kdc_db_context *kdc_db_ctx,
		krb5_const_principal client_principal,
		krb5_const_principal server_principal,
		krb5_pac header_pac,
		struct samba_kdc_entry *proxy_skdc_entry);

NTSTATUS samba_kdc_setup_db_ctx(TALLOC_CTX *mem_ctx, struct samba_kdc_base_context *base_ctx,
				struct samba_kdc_db_context **kdc_db_ctx_out);
