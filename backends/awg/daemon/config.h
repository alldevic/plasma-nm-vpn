/* SPDX-License-Identifier: MIT */
#ifndef PNV_AWG_CONFIG_H
#define PNV_AWG_CONFIG_H

#include "pnv-profile.h"

/* The config for `awg setconf`: [Interface] with the private key, the allowlisted parameters and the header
 * protection key; one [Peer]. No Address, DNS, MTU or wg-quick keys: NM sets the interface up. */
char *pnv_awg_config(const PnvProfile *profile, const char *server_ip);

#endif
