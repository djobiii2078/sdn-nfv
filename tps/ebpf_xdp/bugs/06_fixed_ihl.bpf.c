#define LAB_FIXED_IHL
#include "../include/parser.h"
SEC("xdp")
int port_filter(struct xdp_md *ctx)
{
    /* Loads safely, but parses transport fields inside IPv4 options. */
    return filter((void *)(long)ctx->data, (void *)(long)ctx->data_end, 8080);
}
char license[] SEC("license") = "GPL";
