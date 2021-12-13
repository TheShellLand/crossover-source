/* hmac-gosthash94.c

   HMAC-GOSTHASH94 message authentication code.

   Copyright (C) 2016 Dmitry Eremin-Solenikov

   This file is part of GNU Nettle.

   GNU Nettle is free software: you can redistribute it and/or
   modify it under the terms of either:

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at your
       option) any later version.

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at your
       option) any later version.

   or both in parallel, as here.

   GNU Nettle is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see https://www.gnu.org/licenses/.
*/

#if HAVE_CONFIG_H
# include <config.h>
#endif

#ifndef HAVE_NETTLE_GOSTHASH94CP_UPDATE

#include <gnutls_int.h>

#include <nettle/hmac.h>
#include "hmac-gost.h"
#include "gosthash94.h"

void
hmac_gosthash94cp_set_key(struct hmac_gosthash94cp_ctx *ctx,
		    size_t key_length, const uint8_t *key)
{
  HMAC_SET_KEY(ctx, &nettle_gosthash94cp, key_length, key);
}

void
hmac_gosthash94cp_update(struct hmac_gosthash94cp_ctx *ctx,
		   size_t length, const uint8_t *data)
{
  gosthash94cp_update(&ctx->state, length, data);
}
void
hmac_gosthash94cp_digest(struct hmac_gosthash94cp_ctx *ctx,
		   size_t length, uint8_t *digest)
{
  HMAC_DIGEST(ctx, &nettle_gosthash94cp, length, digest);
}
#endif
