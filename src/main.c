#include <r4os/r4os.h>

R4OS_TEXT(msg_usage, "CNETD /DNS name | /TCP a.b.c.d port | /UDP a.b.c.d port text\r\n");
R4OS_TEXT(msg_unavailable, "CNETD: R4NET or service unavailable\r\n");
R4OS_TEXT(msg_ok, "CNETD result: OK\r\n");
R4OS_TEXT(msg_failed, "CNETD result: FAILED\r\n");

static int is_space(uint8_t ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }
static const uint8_t *skip_space(const uint8_t *at, const uint8_t *end) { while (at < end && is_space(*at)) ++at; return at; }
static int token_eq(const uint8_t *at, const uint8_t *end, const char *text) { while (at < end && !is_space(*at) && *text != 0) { uint8_t a = *at++, b = (uint8_t)*text++; if (a >= 'a' && a <= 'z') a = (uint8_t)(a - 32); if (b >= 'a' && b <= 'z') b = (uint8_t)(b - 32); if (a != b) return 0; } return (at == end || is_space(*at)) && *text == 0; }
static const uint8_t *next_token(const uint8_t *at, const uint8_t *end) { while (at < end && !is_space(*at)) ++at; return skip_space(at, end); }
static uint32_t token_len(const uint8_t *at, const uint8_t *end) { const uint8_t *start = at; while (at < end && !is_space(*at)) ++at; return (uint32_t)(at - start); }
static int parse_u16(const uint8_t *at, const uint8_t *end, uint16_t *out) { uint32_t value = 0u, digits = 0u; while (at < end && !is_space(*at)) { if (*at < '0' || *at > '9') return 0; value = value * 10u + (uint32_t)(*at++ - '0'); if (value > 65535u) return 0; ++digits; } if (digits == 0u || value == 0u) return 0; *out = (uint16_t)value; return 1; }
static int parse_ipv4(const uint8_t *at, const uint8_t *end, R4Ipv4Address *out) { uint32_t part = 0u, value = 0u, digits = 0u; *out = r4_ipv4(0, 0, 0, 0); while (at < end && !is_space(*at)) { if (*at >= '0' && *at <= '9') { value = value * 10u + (uint32_t)(*at - '0'); if (value > 255u) return 0; ++digits; } else if (*at == '.') { if (digits == 0u || part >= 3u) return 0; out->octets[part++] = (uint8_t)value; value = 0u; digits = 0u; } else return 0; ++at; } if (digits == 0u || part != 3u) return 0; out->octets[3] = (uint8_t)value; return 1; }
static R4Timeout network_timeout(void) { R4Timeout timeout = {0}; timeout.kind = R4OS_TIMEOUT_KIND_FINITE; timeout.nanoseconds = 10000000000ull; return timeout; }

int32_t r4_app_main(R4App *app) {
    R4Network network = r4_app_network(app);
    if (!r4_network_available(&network)) { r4sys_write_cstr(&app->system, msg_unavailable); return 1; }
    const uint8_t *args = r4_app_args(app, 0); const uint8_t *end = args != 0 ? args + app->context->args_len : args;
    if (args == 0) { r4sys_write_cstr(&app->system, msg_usage); return 1; }
    args = skip_space(args, end);
    if (token_eq(args, end, "/DNS")) {
        const uint8_t *name = next_token(args, end); R4Ipv4Address answer; R4Resolver resolver = r4_network_resolver(network);
        R4NetResult result = r4_resolver_resolve_a(&resolver, name, token_len(name, end), 0, network_timeout(), &answer);
        r4sys_write_cstr(&app->system, result.kind == R4_NET_RESULT_OK ? msg_ok : msg_failed); return result.kind == R4_NET_RESULT_OK ? 0 : 1;
    }
    if (token_eq(args, end, "/TCP") || token_eq(args, end, "/UDP")) {
        int udp = token_eq(args, end, "/UDP"); const uint8_t *ip_text = next_token(args, end); const uint8_t *port_text = next_token(ip_text, end); R4Ipv4Address ip; uint16_t port;
        if (!parse_ipv4(ip_text, end, &ip) || !parse_u16(port_text, end, &port)) { r4sys_write_cstr(&app->system, msg_usage); return 1; }
        R4SocketAddress remote = {ip, port, 0u}; R4NetResult result;
        if (udp) { R4UdpSocket socket; result = r4_network_bind_udp(&network, 0u, network_timeout(), &socket); if (result.kind == R4_NET_RESULT_OK) { const uint8_t *text = next_token(port_text, end); result = r4_udp_socket_send_to(&socket, remote, text, (uint32_t)(end - text), network_timeout()); (void)r4_udp_socket_close(&socket, network_timeout()); } }
        else { R4TcpSocket socket; result = r4_network_connect_tcp(&network, remote, network_timeout(), &socket); if (result.kind == R4_NET_RESULT_OK) (void)r4_tcp_socket_close(&socket, network_timeout()); }
        r4sys_write_cstr(&app->system, result.kind == R4_NET_RESULT_OK || result.kind == R4_NET_RESULT_CLOSED ? msg_ok : msg_failed); return result.kind == R4_NET_RESULT_OK || result.kind == R4_NET_RESULT_CLOSED ? 0 : 1;
    }
    r4sys_write_cstr(&app->system, msg_usage); return 1;
}
