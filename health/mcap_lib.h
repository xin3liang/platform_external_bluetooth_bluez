/*
 *
 *  MCAP for BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2010 GSyC/LibreSoft, Universidad Rey Juan Carlos.
 *
 *  Authors:
 *  Santiago Carot-Nemesio <sancane at gmail.com>
 *  Jose Antonio Santos-Cadenas <santoscadenas at gmail.com>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __MCAP_LIB_H
#define __MCAP_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
/* MCAP Error Response Codes */
	MCAP_ERROR_INVALID_OP_CODE = 1,
	MCAP_ERROR_INVALID_PARAM_VALUE,
	MCAP_ERROR_INVALID_MDEP,
	MCAP_ERROR_MDEP_BUSY,
	MCAP_ERROR_INVALID_MDL,
	MCAP_ERROR_MDL_BUSY,
	MCAP_ERROR_INVALID_OPERATION,
	MCAP_ERROR_RESOURCE_UNAVAILABLE,
	MCAP_ERROR_UNSPECIFIED_ERROR,
	MCAP_ERROR_REQUEST_NOT_SUPPORTED,
	MCAP_ERROR_CONFIGURATION_REJECTED,
/* MCAP Internal Errors */
	MCAP_ERROR_INVALID_ARGS,
	MCAP_ERROR_ALREADY_EXISTS,
	MCAP_ERROR_REQ_IGNORED,
	MCAP_ERROR_MCL_CLOSED,
	MCAP_ERROR_FAILED
} McapError;

struct mcap_instance;
struct mcap_mcl;

/************ Callbacks ************/

/* mcl callbacks */

typedef void (* mcap_mcl_event_cb) (struct mcap_mcl *mcl, gpointer data);

/************ Operations ************/

/* MCAP main operations */

struct mcap_instance *mcap_create_instance(bdaddr_t *src,
					BtIOSecLevel sec, uint16_t ccpsm,
					uint16_t dcpsm,
					mcap_mcl_event_cb mcl_connected,
					mcap_mcl_event_cb mcl_reconnected,
					mcap_mcl_event_cb mcl_disconnected,
					mcap_mcl_event_cb mcl_uncached,
					gpointer user_data,
					GError **gerr);

void mcap_release_instance(struct mcap_instance *mi);

uint16_t mcap_get_ctrl_psm(struct mcap_instance *mi, GError **err);
uint16_t mcap_get_data_psm(struct mcap_instance *mi, GError **err);

#ifdef __cplusplus
}
#endif

#endif /* __MCAP_LIB_H */