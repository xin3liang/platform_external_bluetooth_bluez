/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
 *
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/bnep.h>
#include <bluetooth/l2cap.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#include <glib.h>

#include "logging.h"
#include "dbus.h"
#include "error.h"

#define NETWORK_SERVER_INTERFACE "org.bluez.network.Server"

#include "common.h"
#include "server.h"

struct network_server {
	char		*iface;		/* Routing interface */
	char		*name;		/* Server service name */
	char		*path; 		/* D-Bus path */
	dbus_bool_t	secure;
	uint32_t	record_id;	/* Service record id */
	uint16_t	id;		/* Service class identifier */
	GIOChannel	*io;		/* GIOChannel when listening */
};

void add_lang_attr(sdp_record_t *r)
{
	sdp_lang_attr_t base_lang;
	sdp_list_t *langs = 0;

	/* UTF-8 MIBenum (http://www.iana.org/assignments/character-sets) */
	base_lang.code_ISO639 = (0x65 << 8) | 0x6e;
	base_lang.encoding = 106;
	base_lang.base_offset = SDP_PRIMARY_LANG_BASE;
	langs = sdp_list_append(0, &base_lang);
	sdp_set_lang_attr(r, langs);
	sdp_list_free(langs, 0);
}

static int create_server_record(sdp_buf_t *buf, uint16_t id)
{
	sdp_list_t *svclass, *pfseq, *apseq, *root, *aproto;
	uuid_t root_uuid, pan, l2cap, bnep;
	sdp_profile_desc_t profile[1];
	sdp_list_t *proto[2];
	sdp_data_t *v, *p;
	uint16_t psm = BNEP_PSM, version = 0x0100;
	uint16_t security_desc = 0;
	uint16_t net_access_type = 0xfffe;
	uint32_t max_net_access_rate = 0;
	const char *name = "BlueZ PAN";
	const char *desc = "BlueZ PAN Service";
	sdp_record_t record;
	int ret;

	/* FIXME: service name must be configurable */

	memset(&record, 0, sizeof(sdp_record_t));

	sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
	root = sdp_list_append(NULL, &root_uuid);
	sdp_set_browse_groups(&record, root);

	sdp_uuid16_create(&l2cap, L2CAP_UUID);
	proto[0] = sdp_list_append(NULL, &l2cap);
	p = sdp_data_alloc(SDP_UINT16, &psm);
	proto[0] = sdp_list_append(proto[0], p);
	apseq    = sdp_list_append(NULL, proto[0]);

	sdp_uuid16_create(&bnep, BNEP_UUID);
	proto[1] = sdp_list_append(NULL, &bnep);
	v = sdp_data_alloc(SDP_UINT16, &version);
	proto[1] = sdp_list_append(proto[1], v);

	/* Supported protocols */
	{
		uint16_t ptype[] = { 
			0x0800,  /* IPv4 */
			0x0806,  /* ARP */
		};
		sdp_data_t *head, *pseq;
		int p;

		for (p = 0, head = NULL; p < 2; p++) {
			sdp_data_t *data = sdp_data_alloc(SDP_UINT16, &ptype[p]);
			if (head)
				sdp_seq_append(head, data);
			else
				head = data;
		}
		pseq = sdp_data_alloc(SDP_SEQ16, head);
		proto[1] = sdp_list_append(proto[1], pseq);
	}

	apseq = sdp_list_append(apseq, proto[1]);

	aproto = sdp_list_append(NULL, apseq);
	sdp_set_access_protos(&record, aproto);

	add_lang_attr(&record);

	/* FIXME: Missing check the security attribute */
	sdp_attr_add_new(&record, SDP_ATTR_SECURITY_DESC,
				SDP_UINT16, &security_desc);

	if (id == BNEP_SVC_NAP) {
		sdp_uuid16_create(&pan, NAP_SVCLASS_ID);
		svclass = sdp_list_append(NULL, &pan);
		sdp_set_service_classes(&record, svclass);

		sdp_uuid16_create(&profile[0].uuid, NAP_PROFILE_ID);
		profile[0].version = 0x0100;
		pfseq = sdp_list_append(NULL, &profile[0]);
		sdp_set_profile_descs(&record, pfseq);

		sdp_set_info_attr(&record, "Network Access Point", name, desc);

		sdp_attr_add_new(&record, SDP_ATTR_NET_ACCESS_TYPE,
					SDP_UINT16, &net_access_type);
		sdp_attr_add_new(&record, SDP_ATTR_MAX_NET_ACCESSRATE,
					SDP_UINT32, &max_net_access_rate);
	} else {
		/* BNEP_SVC_GN */
		sdp_uuid16_create(&pan, GN_SVCLASS_ID);
		svclass = sdp_list_append(NULL, &pan);
		sdp_set_service_classes(&record, svclass);

		sdp_uuid16_create(&profile[0].uuid, GN_PROFILE_ID);
		profile[0].version = 0x0100;
		pfseq = sdp_list_append(NULL, &profile[0]);
		sdp_set_profile_descs(&record, pfseq);
		
		sdp_set_info_attr(&record, "Group Network Service", name, desc);
	}

	if (sdp_gen_record_pdu(&record, buf) < 0)
		ret = -1;
	else
		ret = 0;

	sdp_data_free(p);
	sdp_data_free(v);
	sdp_list_free(apseq, NULL);
	sdp_list_free(root, NULL);
	sdp_list_free(aproto, NULL);
	sdp_list_free(proto[0], NULL);
	sdp_list_free(proto[1], NULL);
	sdp_list_free(svclass, NULL);
	sdp_list_free(pfseq, NULL);
	sdp_list_free(record.attrlist, (sdp_free_func_t) sdp_data_free);
	sdp_list_free(record.pattern, free);

	return ret;
}

static gboolean connect_event(GIOChannel *chan,
				GIOCondition cond, gpointer data)
{
	info("FIXME: Connect event");
	return FALSE;
}

static int l2cap_listen(struct network_server *ns)
{
	struct l2cap_options l2o;
	struct sockaddr_l2 l2a;
	socklen_t olen;
	int sk, lm, err;

	/* Create L2CAP socket and bind it to PSM BNEP */
	sk = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
	if (sk < 0) {
		err = errno;
		error("Cannot create L2CAP socket. %s(%d)",
					strerror(err), err);
		return -err;
	}

	memset(&l2a, 0, sizeof(l2a));
	l2a.l2_family = AF_BLUETOOTH;
	bacpy(&l2a.l2_bdaddr, BDADDR_ANY);
	l2a.l2_psm = htobs(BNEP_PSM);

	if (bind(sk, (struct sockaddr *) &l2a, sizeof(l2a))) {
		err = errno;
		error("Bind failed. %s(%d)", strerror(err), err);
		goto fail;
	}

	/* Setup L2CAP options according to BNEP spec */
	memset(&l2o, 0, sizeof(l2o));
	olen = sizeof(l2o);
	if (getsockopt(sk, SOL_L2CAP, L2CAP_OPTIONS, &l2o, &olen) < 0) {
		err = errno;
		error("Failed to get L2CAP options. %s(%d)",
					strerror(err), err);
		goto fail;
	}

	l2o.imtu = l2o.omtu = BNEP_MTU;
	if (setsockopt(sk, SOL_L2CAP, L2CAP_OPTIONS, &l2o, sizeof(l2o)) < 0) {
		err = errno;
		error("Failed to set L2CAP options. %s(%d)",
					strerror(err), err);
		goto fail;
	}

	/* Set link mode */
	lm = (ns->secure ? L2CAP_LM_SECURE : 0);
	if (lm && setsockopt(sk, SOL_L2CAP, L2CAP_LM, &lm, sizeof(lm)) < 0) {
		err = errno;
		error("Failed to set link mode. %s(%d)",
					strerror(err), err);
		goto fail;
	}

	if (listen(sk, 10) < 0) {
		err = errno;
		error("Listen failed. %s(%d)", strerror(err), err);
		goto fail;
	}

	ns->io = g_io_channel_unix_new(sk);
	g_io_channel_set_close_on_unref(ns->io, TRUE);

	g_io_add_watch(ns->io, G_IO_IN, connect_event, NULL);

	return 0;
fail:

	close(sk);
	errno = err;
	return -err;
}

static uint32_t add_server_record(DBusConnection *conn, uint16_t id)
{
	DBusMessage *msg, *reply;
	DBusError derr;
	dbus_uint32_t rec_id;
	sdp_buf_t buf;

	msg = dbus_message_new_method_call("org.bluez", "/org/bluez",
				"org.bluez.Database", "AddServiceRecord");
	if (!msg) {
		error("Can't allocate new method call");
		return 0;
	}

	if (create_server_record(&buf, id) < 0) {
		error("Unable to allocate new service record");
		dbus_message_unref(msg);
		return 0;
	}

	dbus_message_append_args(msg, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
				&buf.data, buf.data_size, DBUS_TYPE_INVALID);

	dbus_error_init(&derr);
	reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &derr);

	free(buf.data);
	dbus_message_unref(msg);

	if (dbus_error_is_set(&derr) || dbus_set_error_from_message(&derr, reply)) {
		error("Adding service record failed: %s", derr.message);
		dbus_error_free(&derr);
		return 0;
	}

	dbus_message_get_args(reply, &derr, DBUS_TYPE_UINT32, &rec_id,
				DBUS_TYPE_INVALID);

	if (dbus_error_is_set(&derr)) {
		error("Invalid arguments to AddServiceRecord reply: %s", derr.message);
		dbus_message_unref(reply);
		dbus_error_free(&derr);
		return 0;
	}

	dbus_message_unref(reply);

	debug("add_server_record: got record id 0x%x", rec_id);

	return rec_id;
}

static DBusHandlerResult get_uuid(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct network_server *ns = data;
	DBusMessage *reply;
	const char *uuid;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	uuid = bnep_uuid(ns->id);
	dbus_message_append_args(reply,
			DBUS_TYPE_STRING, &uuid,
			DBUS_TYPE_INVALID);

	return send_message_and_unref(conn, reply);
}

static DBusHandlerResult enable(DBusConnection *conn,
				DBusMessage *msg, void *data)
{
	struct network_server *ns = data;
	DBusMessage *reply;
	int err;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	/* Add the service record */
	ns->record_id = add_server_record(conn, ns->id);
	if (!ns->record_id) {
		error("Unable to register the server(0x%x) service record", ns->id);
		return err_failed(conn, msg, "Unable to register the service record");
	}

	/* FIXME: Check security */

	err = l2cap_listen(ns);
	if (err < 0) 
		return err_failed(conn, msg, strerror(-err));

	return send_message_and_unref(conn, reply);
}

static DBusHandlerResult disable(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult set_name(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct network_server *ns = data;
	DBusMessage *reply;
	DBusError derr;
	const char *name;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_error_init(&derr);
	if (!dbus_message_get_args(msg, &derr,
				DBUS_TYPE_STRING, &name,
				DBUS_TYPE_INVALID)) {
		err_invalid_args(conn, msg, derr.message);
		dbus_error_free(&derr);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (!name || (strlen(name) == 0))
		return err_invalid_args(conn, msg, "Invalid name");

	if (ns->name)
		g_free(ns->name);
	ns->name = g_strdup(name);

	/* FIXME: Update the service record attribute */

	return send_message_and_unref(conn, reply);
}

static DBusHandlerResult get_name(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct network_server *ns = data;
	char name[] = "";
	const char *pname = (ns->name ? ns->name : name);
	DBusMessage *reply;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_append_args(reply,
			DBUS_TYPE_STRING, &pname,
			DBUS_TYPE_INVALID);

	return send_message_and_unref(conn, reply);
}

static DBusHandlerResult set_address_range(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult set_routing(DBusConnection *conn, DBusMessage *msg,
					void *data)
{
	struct network_server *ns = data;
	DBusMessage *reply;
	DBusError derr;
	const char *iface;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_error_init(&derr);
	if (!dbus_message_get_args(msg, &derr,
				DBUS_TYPE_STRING, &iface,
				DBUS_TYPE_INVALID)) {
		err_invalid_args(conn, msg, derr.message);
		dbus_error_free(&derr);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	/* FIXME: Check if the interface is valid/UP */
	if (!iface || (strlen(iface) == 0))
		return err_invalid_args(conn, msg, "Invalid interface");

	if (ns->iface)
		g_free(ns->iface);
	ns->iface = g_strdup(iface);

	return send_message_and_unref(conn, reply);
}

static DBusHandlerResult set_security(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct network_server *ns = data;
	DBusMessage *reply;
	DBusError derr;
	dbus_bool_t secure;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_error_init(&derr);
	if (!dbus_message_get_args(msg, &derr,
				DBUS_TYPE_BOOLEAN, &secure,
				DBUS_TYPE_INVALID)) {
		err_invalid_args(conn, msg, derr.message);
		dbus_error_free(&derr);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	ns->secure = secure;

	return send_message_and_unref(conn, reply);
}

static DBusHandlerResult get_security(DBusConnection *conn,
					DBusMessage *msg, void *data)
{
	struct network_server *ns = data;
	DBusMessage *reply;

	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return DBUS_HANDLER_RESULT_NEED_MEMORY;

	dbus_message_append_args(reply,
			DBUS_TYPE_BOOLEAN, &ns->secure,
			DBUS_TYPE_INVALID);

	return send_message_and_unref(conn, reply);
}

static DBusHandlerResult server_message(DBusConnection *conn,
						DBusMessage *msg, void *data)
{
	const char *iface, *member;

	iface = dbus_message_get_interface(msg);
	member = dbus_message_get_member(msg);

	if (strcmp(NETWORK_SERVER_INTERFACE, iface))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (strcmp(member, "GetUUID") == 0)
		return get_uuid(conn, msg, data);

	if (strcmp(member, "Enable") == 0)
		return enable(conn, msg, data);

	if (strcmp(member, "Disable") == 0)
		return disable(conn, msg, data);

	if (strcmp(member, "SetName") == 0)
		return set_name(conn, msg, data);

	if (strcmp(member, "GetName") == 0)
		return get_name(conn, msg, data);

	if (strcmp(member, "SetAddressRange") == 0)
		return set_address_range(conn, msg, data);

	if (strcmp(member, "SetRouting") == 0)
		return set_routing(conn, msg, data);

	if (strcmp(member, "SetSecurity") == 0)
		return set_security(conn, msg, data);

	if (strcmp(member, "GetSecurity") == 0)
		return get_security(conn, msg, data);

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void server_free(struct network_server *ns)
{
	if (!ns)
		return;

	if (ns->iface)
		g_free(ns->iface);

	if (ns->name)
		g_free(ns->name);

	if (ns->path)
		g_free(ns->path);

	g_free(ns);
}

static void server_unregister(DBusConnection *conn, void *data)
{
	struct network_server *ns = data;

	info("Unregistered server path:%s", ns->path);

	server_free(ns);
}

/* Virtual table to handle server object path hierarchy */
static const DBusObjectPathVTable server_table = {
	.message_function = server_message,
	.unregister_function = server_unregister,
};

int server_register(DBusConnection *conn, const char *path, uint16_t id)
{
	struct network_server *ns;

	if (!conn)
		return -EINVAL;

	if (!path)
		return -EINVAL;

	ns = g_new0(struct network_server, 1);

	/* Register path */
	if (!dbus_connection_register_object_path(conn, path,
						&server_table, ns)) {
		error("D-Bus failed to register %s path", path);
		goto fail;
	}

	ns->path = g_strdup(path);
	ns->id = id;
	info("Registered server path:%s", ns->path);

	return 0;
fail:
	server_free(ns);
	return -1;
}
