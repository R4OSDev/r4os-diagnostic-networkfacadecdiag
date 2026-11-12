#include <r4os/r4os.h>
#include <r4os/app2.h>
#include <r4os/cli_helpers.h>
#include <r4os/network_family_platform2.h>

R4OS_TEXT(msg_usage, "CNETD /DNS name | /TCP a.b.c.d port | /UDP a.b.c.d port text\r\n");
R4OS_TEXT(msg_ok, "CNETD result: OK\r\n");
R4OS_TEXT(msg_failed, "CNETD result: FAILED ");
/* An explicitly owned diagnostic endpoint; R4OS rejects port zero. */
static const uint16_t udp_local_port = 65023u;

static int token_eq(const uint8_t *raw, R4SdkArgToken token, const char *text) {
    uintptr_t i = token.value_start;
    while (i < token.value_end && *text) {
        uint8_t a = raw[i++], b = (uint8_t)*text++;
        if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
        if (a != b) return 0;
    }
    return i == token.value_end && !*text;
}
static int parse_port(const uint8_t *at, uintptr_t length, uint16_t *out) {
    uint32_t value = 0;
    if (!length) return 0;
    for (uintptr_t i = 0; i < length; ++i) {
        if (at[i] < '0' || at[i] > '9') return 0;
        value = value * 10u + at[i] - '0';
        if (value > 65535u) return 0;
    }
    *out = (uint16_t)value;
    return value != 0;
}
static int parse_ipv4(const uint8_t *at, uintptr_t length, R4NetworkAddress *out) {
    uint32_t part = 0, value = 0, digits = 0;
    *out = (R4NetworkAddress){.family = 4};
    for (uintptr_t i = 0; i < length; ++i) {
        if (at[i] >= '0' && at[i] <= '9') {
            value = value * 10u + at[i] - '0';
            if (value > 255u) return 0;
            ++digits;
        } else if (at[i] == '.' && digits && part < 3u) {
            out->bytes[part++] = (uint8_t)value;
            value = digits = 0;
        } else return 0;
    }
    if (!digits || part != 3u) return 0;
    out->bytes[3] = (uint8_t)value;
    return 1;
}
static R4Timeout timeout_ns(uint64_t ns) {
    return (R4Timeout){.kind = R4OS_TIMEOUT_KIND_FINITE, .nanoseconds = ns};
}
static int prepare(R4SdkNetwork *client, R4SdkRpcBudget *budget, R4NetworkDeadline *wire) {
    uint64_t remaining;
    if (R4SdkRpcBudgetRemaining(budget, &remaining) < 0) return 0;
    client->timeout_ticks = remaining;
    return R4SdkNetworkDeadlineFromBudget(&budget->deadline, wire) == 0;
}

int32_t r4_app_main(R4App2 *app) {
    R4Sys sys;
    if (r4_app2_system(app, &sys) < 0) return 1;
    uint64_t count = 0;
    const uint8_t *raw = r4_app2_args(app, &count);
    R4SdkArgCursor args;
    R4SdkArgFailure detail;
    R4SdkArgPolicy policy = {R4_ARG_NO_QUOTES, R4_ARG_SLASH, 4096u};
    R4SdkArgToken command, first, second, extra;
    if (!raw || count > 4096u || R4SdkArgInit(raw, (uintptr_t)count, policy, &args, &detail) < 0 ||
        R4SdkArgNext(&args, &command, &detail) != 0 ||
        R4SdkArgNext(&args, &first, &detail) != 0) goto usage;
    int dns = token_eq(raw, command, "/DNS");
    int udp = token_eq(raw, command, "/UDP");
    if (!dns && !udp && !token_eq(raw, command, "/TCP")) goto usage;
    R4NetworkEndpoint remote = {0};
    uintptr_t payload_at = 0, payload_size = 0;
    if (dns) {
        if (first.value_end - first.value_start > 255u ||
            R4SdkArgNext(&args, &extra, &detail) != 1) goto usage;
    } else {
        if (R4SdkArgNext(&args, &second, &detail) != 0 ||
            !parse_ipv4(raw + first.value_start, first.value_end - first.value_start, &remote.address) ||
            !parse_port(raw + second.value_start, second.value_end - second.value_start, &remote.port)) goto usage;
        if (udp) {
            if (R4SdkArgRemaining(&args, 1u, &payload_at, &payload_size) < 0 || payload_size > 3968u) goto usage;
        } else if (R4SdkArgNext(&args, &extra, &detail) != 1) goto usage;
    }
    uint64_t address = 0;
    if (r4_app2_function(app, 1, offsetof(R4XStartR4Sys2, monotonic_clock), &address) != 0) return 1;
    R4SysMonotonicClockFn clock = (R4SysMonotonicClockFn)(uintptr_t)address;
    R4SdkRpcBudget budget;
    R4Timeout timeout = timeout_ns(UINT64_C(10000000000));
    if (R4SdkRpcBudgetInit(clock, &timeout, &budget) < 0) return 1;
    R4SdkNetwork client = {0};
    const char *failure = "provider";
    int ok = 0;
    if (R4SdkNetworkConnect(&app->context, dns ? R4_NETWORK_DNS : udp ? R4_NETWORK_UDP : R4_NETWORK_TCP, &client) != 0) goto report;
    R4NetworkProviderInfo provider;
    R4NetworkDeadline wire;
    if (!prepare(&client, &budget, &wire) || R4SdkNetworkInfo(&client, &provider) != 0) goto close_channel;
    if (dns) {
        R4NetworkResolveRequest request = {.version = 1, .size = sizeof(request), .provider = provider.provider,
            .deadline = wire, .family = 4, .capacity = 1, .name_len = (uint32_t)(first.value_end - first.value_start)};
        __builtin_memcpy(request.name, raw + first.value_start, request.name_len);
        R4NetworkResolveAnswer answer;
        R4NetworkResolvePage page;
        failure = "resolve";
        if (prepare(&client, &budget, &request.deadline) && R4SdkNetworkResolve(&client, request, &answer, 1, &page) == 0) {
            R4SdkRpcBudget cleanup;
            R4Timeout cleanup_timeout = timeout_ns(UINT64_C(2000000000));
            R4NetworkDeadline unused;
            int released = R4SdkRpcBudgetInit(clock, &cleanup_timeout, &cleanup) == 0 &&
                prepare(&client, &cleanup, &unused) && R4SdkNetworkRelease(&client, provider.provider, page.query) == 0;
            ok = answer.address.family == 4 && released;
        }
    } else {
        R4NetworkSocketRequest request = {.version = 1, .size = sizeof(request), .provider = provider.provider,
            .endpoint = udp ? (R4NetworkEndpoint){.port = udp_local_port, .address = {.family = 4}} : remote};
        R4NetworkSocketResult socket;
        failure = udp ? "bind" : "connect";
        if (prepare(&client, &budget, &request.deadline) &&
            R4SdkNetworkSocket(&client, udp ? R4OS_NETWORK_OP_BIND : R4OS_NETWORK_OP_CONNECT, request, NULL, 0, NULL, 0, &socket) == 0) {
            request.socket = socket.socket;
            request.generation = socket.generation;
            ok = socket.state == R4OS_NETWORK_STATE_READY;
            if (udp && ok) {
                request.endpoint = remote;
                request.bytes = (uint32_t)payload_size;
                R4NetworkSocketResult sent;
                failure = "send/partial-result";
                ok = prepare(&client, &budget, &request.deadline) &&
                    R4SdkNetworkSocket(&client, R4OS_NETWORK_OP_SEND, request, raw + payload_at,
                        request.bytes, NULL, 0, &sent) == 0 && sent.state == R4OS_NETWORK_STATE_READY && sent.bytes == request.bytes;
            }
            /* Release has its own bounded lifetime budget; it never renews
             * or replays the original operation. Retain the exact generation. */
            R4SdkRpcBudget cleanup;
            R4Timeout cleanup_timeout = timeout_ns(UINT64_C(2000000000));
            request.bytes = 0;
            request.endpoint = (R4NetworkEndpoint){0};
            R4NetworkSocketResult closed;
            int released = R4SdkRpcBudgetInit(clock, &cleanup_timeout, &cleanup) == 0 &&
                prepare(&client, &cleanup, &request.deadline) &&
                R4SdkNetworkSocket(&client, R4OS_NETWORK_OP_CLOSE, request, NULL, 0, NULL, 0, &closed) == 0 &&
                closed.state == R4OS_NETWORK_STATE_LOCAL_CLOSED;
            if (!released) { ok = 0; failure = "socket release"; }
        }
    }
close_channel:
    if (R4SdkNetworkClose(&client) != 0) { ok = 0; failure = "channel release"; }
report:
    r4sys_write_cstr(&sys, ok ? msg_ok : msg_failed);
    if (!ok) r4sys_write_line(&sys, failure);
    return ok ? 0 : 1;
usage:
    r4sys_write_cstr(&sys, msg_usage);
    return 1;
}
