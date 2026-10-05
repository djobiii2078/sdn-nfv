#include "../include/parser.h"
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u32);
} config SEC(".maps");
SEC("xdp")
int port_filter(struct xdp_md *ctx)
{
    __u32 key = 0;
    __u32 *value = lookup(&config, &key);
    /* BUG: helper returns a nullable pointer, even for this map usage. */
    __u16 port = *value;
    return filter((void *)(long)ctx->data, (void *)(long)ctx->data_end, port);
}
char license[] SEC("license") = "GPL";
