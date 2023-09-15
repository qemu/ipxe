/*
 * Copyright (C) 2021 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * You can also choose to distribute this program under the terms of
 * the Unmodified Binary Distribution Licence (as given in the file
 * COPYING.UBDL), provided that you have satisfied its requirements.
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <assert.h>
#include <errno.h>
#include <byteswap.h>
#include <ipxe/iobuf.h>
#include <ipxe/if_ether.h>
#include <ipxe/netdevice.h>
#include <ipxe/eap.h>
#include <ipxe/eapol.h>

/** @file
 *
 * Extensible Authentication Protocol over LAN (EAPoL)
 *
 */

struct net_driver eapol_driver __net_driver;

/** EAPoL destination MAC address */
static const uint8_t eapol_mac[ETH_ALEN] = {
	0x01, 0x80, 0xc2, 0x00, 0x00, 0x03
};

/**
 * Process EAPoL packet
 *
 * @v iobuf		I/O buffer
 * @v netdev		Network device
 * @v ll_dest		Link-layer destination address
 * @v ll_source		Link-layer source address
 * @v flags		Packet flags
 * @ret rc		Return status code
 */
static int eapol_rx ( struct io_buffer *iobuf, struct net_device *netdev,
		      const void *ll_dest __unused, const void *ll_source,
		      unsigned int flags __unused ) {
	struct eapol_supplicant *supplicant;
	struct eapol_header *eapol;
	struct eapol_handler *handler;
	size_t remaining;
	size_t len;
	int rc;

	/* Find matching supplicant */
	supplicant = netdev_priv ( netdev, &eapol_driver );

	/* Sanity checks */
	if ( iob_len ( iobuf ) < sizeof ( *eapol ) ) {
		DBGC ( netdev, "EAPOL %s underlength header:\n",
		       netdev->name );
		DBGC_HDA ( netdev, 0, iobuf->data, iob_len ( iobuf ) );
		rc = -EINVAL;
		goto drop;
	}
	eapol = iobuf->data;
	remaining = ( iob_len ( iobuf ) - sizeof ( *eapol ) );
	len = ntohs ( eapol->len );
	if ( len > remaining ) {
		DBGC ( netdev, "EAPOL %s v%d type %d len %zd underlength "
		       "payload:\n", netdev->name, eapol->version,
		       eapol->type, len );
		DBGC_HDA ( netdev, 0, iobuf->data, iob_len ( iobuf ) );
		rc = -EINVAL;
		goto drop;
	}

	/* Strip any trailing padding */
	iob_unput ( iobuf, ( len - remaining ) );

	/* Handle according to type */
	for_each_table_entry ( handler, EAPOL_HANDLERS ) {
		if ( handler->type == eapol->type ) {
			return handler->rx ( supplicant, iob_disown ( iobuf ),
					     ll_source );
		}
	}
	rc = -ENOTSUP;
	DBGC ( netdev, "EAPOL %s v%d type %d unsupported\n",
	       netdev->name, eapol->version, eapol->type );
	DBGC_HDA ( netdev, 0, iobuf->data, iob_len ( iobuf ) );

 drop:
	free_iob ( iobuf );
	return rc;
}

/** EAPoL protocol */
struct net_protocol eapol_protocol __net_protocol = {
	.name = "EAPOL",
	.net_proto = htons ( ETH_P_EAPOL ),
	.rx = eapol_rx,
};

/**
 * Process EAPoL-encapsulated EAP packet
 *
 * @v supplicant	EAPoL supplicant
 * @v ll_source		Link-layer source address
 * @ret rc		Return status code
 */
static int eapol_eap_rx ( struct eapol_supplicant *supplicant,
			  struct io_buffer *iobuf,
			  const void *ll_source __unused ) {
	struct net_device *netdev = supplicant->eap.netdev;
	struct eapol_header *eapol;
	int rc;

	/* Sanity check */
	assert ( iob_len ( iobuf ) >= sizeof ( *eapol ) );

	/* Strip EAPoL header */
	eapol = iob_pull ( iobuf, sizeof ( *eapol ) );

	/* Process EAP packet */
	if ( ( rc = eap_rx ( &supplicant->eap, iobuf->data,
			     iob_len ( iobuf ) ) ) != 0 ) {
		DBGC ( netdev, "EAPOL %s v%d EAP failed: %s\n",
		       netdev->name, eapol->version, strerror ( rc ) );
		goto drop;
	}

 drop:
	free_iob ( iobuf );
	return rc;
}

/** EAPoL handler for EAP packets */
struct eapol_handler eapol_eap __eapol_handler = {
	.type = EAPOL_TYPE_EAP,
	.rx = eapol_eap_rx,
};

/**
 * Transmit EAPoL packet
 *
 * @v supplicant	EAPoL supplicant
 * @v type		Packet type
 * @v data		Packet body
 * @v len		Length of packet body
 * @ret rc		Return status code
 */
static int eapol_tx ( struct eapol_supplicant *supplicant, unsigned int type,
		      const void *data, size_t len ) {
	struct net_device *netdev = supplicant->eap.netdev;
	struct io_buffer *iobuf;
	struct eapol_header *eapol;
	int rc;

	/* Allocate I/O buffer */
	iobuf = alloc_iob ( MAX_LL_HEADER_LEN + sizeof ( *eapol ) + len );
	if ( ! iobuf )
		return -ENOMEM;
	iob_reserve ( iobuf, MAX_LL_HEADER_LEN );

	/* Construct EAPoL header */
	eapol = iob_put ( iobuf, sizeof ( *eapol ) );
	eapol->version = EAPOL_VERSION_2001;
	eapol->type = type;
	eapol->len = htons ( len );

	/* Append packet body */
	memcpy ( iob_put ( iobuf, len ), data, len );

	/* Transmit packet */
	if ( ( rc = net_tx ( iob_disown ( iobuf ), netdev, &eapol_protocol,
			     &eapol_mac, netdev->ll_addr ) ) != 0 ) {
		DBGC ( netdev, "EAPOL %s could not transmit type %d: %s\n",
		       netdev->name, type, strerror ( rc ) );
		DBGC_HDA ( netdev, 0, data, len );
		return rc;
	}

	return 0;
}

/**
 * Transmit EAPoL-encapsulated EAP packet
 *
 * @v supplicant	EAPoL supplicant
 * @v ll_source		Link-layer source address
 * @ret rc		Return status code
 */
static int eapol_eap_tx ( struct eap_supplicant *eap, const void *data,
			  size_t len ) {
	struct eapol_supplicant *supplicant =
		container_of ( eap, struct eapol_supplicant, eap );

	/* Transmit encapsulated packet */
	return eapol_tx ( supplicant, EAPOL_TYPE_EAP, data, len );
}

/**
 * Create EAPoL supplicant
 *
 * @v netdev		Network device
 * @v priv		Private data
 * @ret rc		Return status code
 */
static int eapol_probe ( struct net_device *netdev, void *priv ) {
	struct eapol_supplicant *supplicant = priv;

	/* Initialise structure */
	supplicant->eap.netdev = netdev;
	supplicant->eap.tx = eapol_eap_tx;

	return 0;
}

/** EAPoL driver */
struct net_driver eapol_driver __net_driver = {
	.name = "EAPoL",
	.priv_len = sizeof ( struct eapol_supplicant ),
	.probe = eapol_probe,
};
