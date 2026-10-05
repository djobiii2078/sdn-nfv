#define LAB_RAW_PORT
#include "../include/parser.h"
SEC("xdp")
int port_filter(struct xdp_md *ctx)
{
    /* Loads safely, but compares network-order bytes with host-order port. */
    return filter((void *)(long)ctx->data, (void *)(long)ctx->data_end, 8080);
}
char license[] SEC("license") = "GPL";
