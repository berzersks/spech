#include <php.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

typedef struct spech_socket_t {
    int sockfd;
    zval callback;
    zend_object std;
} spech_socket_t;

static zend_class_entry *spech_socket_ce;
static zend_object_handlers spech_socket_handlers;

static inline spech_socket_t *php_spech_socket_fetch(zend_object *obj) {
    return (spech_socket_t *)((char *)(obj) - XtOffsetOf(spech_socket_t, std));
}

static zend_object *spech_socket_create(zend_class_entry *ce)
{
    spech_socket_t *intern = ecalloc(1, sizeof(spech_socket_t) + zend_object_properties_size(ce));
    intern->sockfd = -1;

    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    intern->std.handlers = &spech_socket_handlers;
    ZVAL_UNDEF(&intern->callback);

    return &intern->std;
}

static void spech_socket_free(zend_object *object)
{
    spech_socket_t *sock = php_spech_socket_fetch(object);
    if (sock->sockfd != -1) close(sock->sockfd);
    zval_ptr_dtor(&sock->callback);
    zend_object_std_dtor(&sock->std);
}

PHP_METHOD(SpechSocket, __construct)
{
    zend_long port = 0;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(port)
    ZEND_PARSE_PARAMETERS_END();

    spech_socket_t *sock = php_spech_socket_fetch(Z_OBJ_P(ZEND_THIS));
    sock->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock->sockfd < 0) {
        zend_throw_error(NULL, "Erro ao criar socket");
        RETURN_THROWS();
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock->sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        zend_throw_error(NULL, "Erro ao dar bind no socket");
        RETURN_THROWS();
    }
}

PHP_METHOD(SpechSocket, connect)
{
    char *ip;
    size_t ip_len;
    zend_long port;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(ip, ip_len)
        Z_PARAM_LONG(port)
    ZEND_PARSE_PARAMETERS_END();

    spech_socket_t *sock = php_spech_socket_fetch(Z_OBJ_P(ZEND_THIS));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock->sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        zend_throw_error(NULL, "Erro no connect UDP");
        RETURN_THROWS();
    }

    RETURN_TRUE;
}

PHP_METHOD(SpechSocket, listen)
{
    zval *callback;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(callback)
    ZEND_PARSE_PARAMETERS_END();

    spech_socket_t *sock = php_spech_socket_fetch(Z_OBJ_P(ZEND_THIS));
    ZVAL_COPY(&sock->callback, callback);

    while (1) {
        char buffer[1500];
        struct sockaddr_in peer_addr;
        socklen_t addrlen = sizeof(peer_addr);

        ssize_t recv_len = recvfrom(sock->sockfd, buffer, sizeof(buffer), 0,
                                    (struct sockaddr*)&peer_addr, &addrlen);
        if (recv_len <= 0) continue;

        zval retval, args[2];
        array_init(&args[0]);
        add_assoc_string(&args[0], "ip", inet_ntoa(peer_addr.sin_addr));
        add_assoc_long(&args[0], "port", ntohs(peer_addr.sin_port));
        ZVAL_STRINGL(&args[1], buffer, recv_len);

        ZVAL_UNDEF(&retval);
        call_user_function(NULL, NULL, &sock->callback, &retval, 2, args);

        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
        zval_ptr_dtor(&retval);
    }
}

PHP_METHOD(SpechSocket, sendTo)
{
    zval *peer;
    zend_string *data;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_ARRAY(peer)
        Z_PARAM_STR(data)
    ZEND_PARSE_PARAMETERS_END();

    zval *ip_zv = zend_hash_str_find(Z_ARRVAL_P(peer), "ip", sizeof("ip") - 1);
    zval *port_zv = zend_hash_str_find(Z_ARRVAL_P(peer), "port", sizeof("port") - 1);
    if (!ip_zv || !port_zv) {
        zend_throw_error(NULL, "peer deve conter 'ip' e 'port'");
        RETURN_THROWS();
    }

    const char *ip = Z_STRVAL_P(ip_zv);
    int port = Z_LVAL_P(port_zv);

    struct sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    inet_pton(AF_INET, ip, &dest.sin_addr);

    spech_socket_t *sock = php_spech_socket_fetch(Z_OBJ_P(ZEND_THIS));
    sendto(sock->sockfd, ZSTR_VAL(data), ZSTR_LEN(data), 0, (struct sockaddr*)&dest, sizeof(dest));

    RETURN_TRUE;
}

/* ----------- ARGINFOS embutidos ------------ */
ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_connect, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, ip, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_listen, 0, 0, 1)
    ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sendto, 0, 0, 2)
    ZEND_ARG_ARRAY_INFO(0, peer, 0)
    ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* ----------- REGISTRO DE MÉTODOS ------------ */
static const zend_function_entry spech_socket_methods[] = {
    PHP_ME(SpechSocket, __construct, arginfo_construct, ZEND_ACC_PUBLIC)
    PHP_ME(SpechSocket, connect,     arginfo_connect,   ZEND_ACC_PUBLIC)
    PHP_ME(SpechSocket, listen,      arginfo_listen,    ZEND_ACC_PUBLIC)
    PHP_ME(SpechSocket, sendTo,      arginfo_sendto,    ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ----------- MINIT E ENTRYPOINT ------------ */
PHP_MINIT_FUNCTION(spech)
{
    zend_class_entry ce;
    INIT_CLASS_ENTRY(ce, "SpechSocket", spech_socket_methods);
    spech_socket_ce = zend_register_internal_class(&ce);
    spech_socket_ce->create_object = spech_socket_create;

    memcpy(&spech_socket_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    spech_socket_handlers.offset = XtOffsetOf(spech_socket_t, std);
    spech_socket_handlers.free_obj = spech_socket_free;

    return SUCCESS;
}

zend_module_entry spech_module_entry = {
    STANDARD_MODULE_HEADER,
    "spech",
    NULL,
    PHP_MINIT(spech),
    NULL,
    NULL,
    NULL,
    NULL,
    "1.0",
    STANDARD_MODULE_PROPERTIES
};

ZEND_GET_MODULE(spech)
