/*
 *  idmap_rid: static map between Active Directory/NT RIDs and RFC 2307 accounts
 *  Copyright (C) Guenther Deschner, 2004
 *  Copyright (C) Sumit Bose, 2004
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "includes.h"
#include "winbindd.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_IDMAP

struct idmap_rid_context {
	const char *domain_name;
	uint32_t low_id;
	uint32_t high_id;
	uint32_t base_rid;
};

/******************************************************************************
  compat params can't be used because of the completely different way
  we support multiple domains in the new idmap
 *****************************************************************************/

static NTSTATUS idmap_rid_initialize(struct idmap_domain *dom, const char *compat_params)
{
	NTSTATUS ret;
	struct idmap_rid_context *ctx;
	char *config_option = NULL;
	const char *range;

	if ( (ctx = talloc_zero(dom, struct idmap_rid_context)) == NULL ) {
		DEBUG(0, ("Out of memory!\n"));
		return NT_STATUS_NO_MEMORY;
	}

	config_option = talloc_asprintf(ctx, "idmap config %s", dom->name);
	if ( ! config_option) {
		DEBUG(0, ("Out of memory!\n"));
		ret = NT_STATUS_NO_MEMORY;
		goto failed;
	}

	range = lp_parm_const_string(-1, config_option, "range", NULL);
	if ( !range ||
	    (sscanf(range, "%u - %u", &ctx->low_id, &ctx->high_id) != 2) ||
	    (ctx->low_id > ctx->high_id)) 
	{
		ctx->low_id = 0;
		ctx->high_id = 0;
	}

	if ( !ctx->low_id || !ctx->high_id ) {
		DEBUG(1, ("ERROR: Invalid configuration, ID range missing\n"));
		ret = NT_STATUS_UNSUCCESSFUL;
		goto failed;
	}

	ctx->base_rid = lp_parm_int(-1, config_option, "base_rid", 0);
	ctx->domain_name = talloc_strdup( ctx, dom->name );
	
	dom->private_data = ctx;

	talloc_free(config_option);
	return NT_STATUS_OK;

failed:
	talloc_free(ctx);
	return ret;
}

static NTSTATUS idmap_rid_id_to_sid(TALLOC_CTX *memctx, struct idmap_rid_context *ctx, struct id_map *map)
{
	const char *domname, *name;
	enum lsa_SidType sid_type;
	BOOL ret;
	struct winbindd_domain *domain;	

	/* apply filters before checking */
	if ((map->xid.id < ctx->low_id) || (map->xid.id > ctx->high_id)) {
		DEBUG(5, ("Requested id (%u) out of range (%u - %u). Filtered!\n",
				map->xid.id, ctx->low_id, ctx->high_id));
		return NT_STATUS_NONE_MAPPED;
	}

	if ( (domain = find_domain_from_name_noinit(ctx->domain_name)) == NULL ) {
		return NT_STATUS_NO_SUCH_DOMAIN;
	}
	
	sid_compose(map->sid, &domain->sid, map->xid.id - ctx->low_id + ctx->base_rid);

	/* by default calls to winbindd are disabled
	   the following call will not recurse so this is safe */
	winbind_on();
	ret = winbind_lookup_sid(memctx, map->sid, &domname, &name, &sid_type);
	winbind_off();

	if (ret) {
		switch (sid_type) {
		case SID_NAME_USER:
			if (map->xid.type != ID_TYPE_UID) {
				/* wrong type */
				map->status = ID_UNMAPPED;
				DEBUG(5, ("Resulting SID is of wrong ID type\n"));
				return NT_STATUS_NONE_MAPPED;
			}
			break;
		case SID_NAME_DOM_GRP:
		case SID_NAME_ALIAS:
		case SID_NAME_WKN_GRP:
			if (map->xid.type != ID_TYPE_GID) {
				/* wrong type */
				map->status = ID_UNMAPPED;
				DEBUG(5, ("Resulting SID is of wrong ID type\n"));
				return NT_STATUS_NONE_MAPPED;
			}
			break;
		default:
			/* invalid sid?? */
			map->status = ID_UNKNOWN;
			DEBUG(10, ("SID %s is UNKNOWN, skip mapping\n", sid_string_static(map->sid)));
			return NT_STATUS_NONE_MAPPED;
		}
	} else {
		/* TODO: how do we known if the lookup was negative
		 * or something just failed? */
		map->status = ID_UNMAPPED;
		DEBUG(2, ("Failed: to resolve SID\n"));
		return NT_STATUS_UNSUCCESSFUL;
	}

	map->status = ID_MAPPED;

	return NT_STATUS_OK;
}

/**********************************
 Single sid to id lookup function. 
**********************************/

static NTSTATUS idmap_rid_sid_to_id(TALLOC_CTX *memctx, struct idmap_rid_context *ctx, struct id_map *map)
{
	const char *domname, *name;
	enum lsa_SidType sid_type;
	uint32_t rid;
	BOOL ret;

	sid_peek_rid(map->sid, &rid);
	map->xid.id = rid - ctx->base_rid + ctx->low_id;

	/* by default calls to winbindd are disabled
	   the following call will not recurse so this is safe */
	winbind_on();
	/* check if this is a valid SID and set the type */
	ret = winbind_lookup_sid(memctx, map->sid, &domname, &name, &sid_type);
	winbind_off();

	if (ret) {
		switch (sid_type) {
		case SID_NAME_USER:
			map->xid.type = ID_TYPE_UID;
			break;
		case SID_NAME_DOM_GRP:
		case SID_NAME_ALIAS:
		case SID_NAME_WKN_GRP:
			map->xid.type = ID_TYPE_GID;
			break;
		default:
			/* invalid sid, let's just leave it unmapped */
			DEBUG(10, ("SID %s is UNKNOWN, skip mapping\n", sid_string_static(map->sid)));
			map->status = ID_UNKNOWN;
			return NT_STATUS_NONE_MAPPED;
		}
	} else {
		/* TODO: how do we known if the lookup was negative
		 * or something just failed? */
		map->status = ID_UNMAPPED;
		DEBUG(2, ("Failed: to resolve SID\n"));
		return NT_STATUS_UNSUCCESSFUL;
	}

	/* apply filters before returning result */
	if ((map->xid.id < ctx->low_id) || (map->xid.id > ctx->high_id)) {
		DEBUG(5, ("Requested id (%u) out of range (%u - %u). Filtered!\n",
				map->xid.id, ctx->low_id, ctx->high_id));
		map->status = ID_UNMAPPED;
		return NT_STATUS_NONE_MAPPED;
	}

	map->status = ID_MAPPED;

	return NT_STATUS_OK;
}

/**********************************
 lookup a set of unix ids. 
**********************************/

static NTSTATUS idmap_rid_unixids_to_sids(struct idmap_domain *dom, struct id_map **ids)
{
	struct idmap_rid_context *ridctx;
	TALLOC_CTX *ctx;
	NTSTATUS ret;
	int i;

	ridctx = talloc_get_type(dom->private_data, struct idmap_rid_context);

	ctx = talloc_new(dom);
	if ( ! ctx) {
		DEBUG(0, ("Out of memory!\n"));
		return NT_STATUS_NO_MEMORY;
	}

	for (i = 0; ids[i]; i++) {

		ret = idmap_rid_id_to_sid(ctx, ridctx, ids[i]);

		if (( ! NT_STATUS_IS_OK(ret)) &&
		    ( ! NT_STATUS_EQUAL(ret, NT_STATUS_NONE_MAPPED))) {
			/* some fatal error occurred, log it */
			DEBUG(3, ("Unexpected error resolving an ID (%d)\n", ids[i]->xid.id));
		}
	}

	talloc_free(ctx);
	return NT_STATUS_OK;
}

/**********************************
 lookup a set of sids. 
**********************************/

static NTSTATUS idmap_rid_sids_to_unixids(struct idmap_domain *dom, struct id_map **ids)
{
	struct idmap_rid_context *ridctx;
	TALLOC_CTX *ctx;
	NTSTATUS ret;
	int i;

	ridctx = talloc_get_type(dom->private_data, struct idmap_rid_context);

	ctx = talloc_new(dom);
	if ( ! ctx) {
		DEBUG(0, ("Out of memory!\n"));
		return NT_STATUS_NO_MEMORY;
	}

	for (i = 0; ids[i]; i++) {

		ret = idmap_rid_sid_to_id(ctx, ridctx, ids[i]);

		if (( ! NT_STATUS_IS_OK(ret)) &&
		    ( ! NT_STATUS_EQUAL(ret, NT_STATUS_NONE_MAPPED))) {
			/* some fatal error occurred, log it */
			DEBUG(3, ("Unexpected error resolving a SID (%s)\n",
					sid_string_static(ids[i]->sid)));
		}
	}

	talloc_free(ctx);
	return NT_STATUS_OK;
}

static NTSTATUS idmap_rid_close(struct idmap_domain *dom)
{
	if (dom->private_data) {
		TALLOC_FREE(dom->private_data);
	}
	return NT_STATUS_OK;
}

static struct idmap_methods rid_methods = {
	.init = idmap_rid_initialize,
	.unixids_to_sids = idmap_rid_unixids_to_sids,
	.sids_to_unixids = idmap_rid_sids_to_unixids,
	.close_fn = idmap_rid_close
};

NTSTATUS idmap_rid_init(void)
{
	return smb_register_idmap(SMB_IDMAP_INTERFACE_VERSION, "rid", &rid_methods);
}

