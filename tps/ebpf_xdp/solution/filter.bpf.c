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
    __u16 port = 8080;
    if (value && *value > 0 && *value <= 65535) port = (__u16)*value;
    void *data = (void *)(long)ctx->data;
    void *end = (void *)(long)ctx->data_end;
    return filter(data, end, port);
}
char license[] SEC("license") = "GPL";
