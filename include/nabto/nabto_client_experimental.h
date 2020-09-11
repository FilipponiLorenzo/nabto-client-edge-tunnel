#ifndef NABTO_CLIENT_EXPERIMENTAL_H
#define NABTO_CLIENT_EXPERIMENTAL_H

#include "nabto_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Experimental header. Functions here are all experimental. They
 * should be used with caution and can be changed in future releases
 * without notice.
 */

/*****************
 * mDNS API
 ******************/
typedef struct NabtoClientMdnsResult_ NabtoClientMdnsResult;


typedef enum NabtoClientMdnsAction_ {
    /**
     * This action is emitted when a mdns cache item is added.
     */
    NABTO_CLIENT_MDNS_ACTION_ADD = 0,
    /**
     * This action is emitted when an mdns cache item is updated.
     */
    NABTO_CLIENT_MDNS_ACTION_UPDATE = 1,
    /**
     * This action is emitted when an mdns cache item is removed,
     * ie. the ttl has expired for the item.
     */
    NABTO_CLIENT_MDNS_ACTION_REMOVE = 2
} NabtoClientMdnsAction;

/**
 * Init listener as mdns resolver
 *
 * Init a mdns result listener. If the subtype is non null or the non
 * empty string the mDNS subtype <subtype>._sub._nabto._udp.local is
 * located instead of the mDNS service _nabto._udp.local.
 */
NABTO_CLIENT_DECL_PREFIX NabtoClientError NABTO_CLIENT_API
nabto_client_mdns_resolver_init_listener(NabtoClient* client, NabtoClientListener* listener, const char* subtype);

/**
 * Wait for a new mdns result.
 */
NABTO_CLIENT_DECL_PREFIX void NABTO_CLIENT_API
nabto_client_listener_new_mdns_result(NabtoClientListener* listener, NabtoClientFuture* future, NabtoClientMdnsResult** mdnsResult);

/**
 * Experimental: free result object
 */
NABTO_CLIENT_DECL_PREFIX void NABTO_CLIENT_API
nabto_client_mdns_result_free(NabtoClientMdnsResult* result);

/**
 * Experimental: get device ID of from result object
 * @return the device ID or the empty string if not set
 */
NABTO_CLIENT_DECL_PREFIX const char* NABTO_CLIENT_API
nabto_client_mdns_result_get_device_id(NabtoClientMdnsResult* result);

/**
 * Experimental: get product ID of from result object
 * @return the product ID or the empty string if not set
 */
NABTO_CLIENT_DECL_PREFIX const char* NABTO_CLIENT_API
nabto_client_mdns_result_get_product_id(NabtoClientMdnsResult* result);

/**
 * Get the service instance name, this can be used to correlate results.
 * This is never NULL and always defined
 */
NABTO_CLIENT_DECL_PREFIX const char* NABTO_CLIENT_API
nabto_client_mdns_result_get_service_instance_name(NabtoClientMdnsResult* result);

/**
 * Get the txt record key value pairs as a json encoded string.
 * The string is owned by the NabtoClientMdnsResult object.
 *
 * The data is encoded as { "key1": "value1", "key2": "value2" }
 */
NABTO_CLIENT_DECL_PREFIX const char* NABTO_CLIENT_API
nabto_client_mdns_result_get_txt_items(NabtoClientMdnsResult* result);

/**
 * Get the action for the result
 */
NABTO_CLIENT_DECL_PREFIX NabtoClientMdnsAction NABTO_CLIENT_API
nabto_client_mdns_result_get_action(NabtoClientMdnsResult* result);


/**
 * TCP Tunel experimental features
 */


/* enum NabtoClientTcpTunnelListenMode { */
/*     LISTEN_MODE_LOCALHOST, */
/*     LISTEN_MODE_ANY */
/* }; */

/**
 * NOT IMPLEMENTED. TBD.
 * Set the listen mode for the tcp listener. Default is to only listen
 * on localhost / loopback such that only applications on the local
 * machine can connect to the tcp listener. Anyone on the local system
 * can connect to the tcp listener. Some form of application layer
 * authentication needs to be present on the tcp connection if the
 * system is multi tenant or not completely trusted or if the
 * application is not run in isolation.
 */
/* NABTO_CLIENT_DECL_PREFIX NabtoClientError NABTO_CLIENT_API */
/* nabto_client_tcp_tunnel_listen_mode(NabtoClientTcpTunnel* tunnel, */
/*                                     enum NabtoClientTcpTunnelListenMode listenMode); */


/**
 * Password Authentication
 *
 * Password authenticate the client and the device. The password
 * authentication is bidirectional and based on PAKE, such that both
 * the client and the device learns that the other end knows the
 * password, without revealing the password to the other end.
 *
 * A specific use case for the password authentication is to prove the
 * identity of a device which identity is not already known, e.g. in a
 * pairing scenario.
 */

/**
 * Password authenticate, do a password authentication exchange with a
 * device.
 *
 * @param connection  The connection
 * @param username    The username
 * @param password    The password
 * @param future      The future with the result
 *
 * The future resolves with the status of the authentication.
 *
 * NABTO_CLIENT_EC_OK iff the authentication went well.
 * NABTO_CLIENT_EC_UNAUTHORIZED iff the username or password is invalid
 * NABTO_CLIENT_EC_NOT_FOUND if the password authentication feature is not available on the device.
 * NABTO_CLIENT_EC_NOT_CONNECTED if the connectio is not connected.
 * NABTO_CLIENT_EC_OPERATION_IN_PROGRESS if a password authentication request is already in progress on the connection.
 * NABTO_CLIENT_EC_TOO_MANY_REQUESTS if too many password attempts has been made.
 */
NABTO_CLIENT_DECL_PREFIX void NABTO_CLIENT_API
nabto_client_connection_password_authenticate(NabtoClientConnection* connection, const char* username, const char* password, NabtoClientFuture* future);

#ifdef __cplusplus
} // extern C
#endif

#endif
