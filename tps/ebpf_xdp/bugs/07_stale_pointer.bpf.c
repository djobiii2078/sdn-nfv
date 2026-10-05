#include "../include/lab.h"
SEC("xdp")
int port_filter(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > end) return XDP_PASS;
    if (adjust_head(ctx, 0)) return XDP_PASS;
    /* BUG: helper may invalidate packet pointers and their bounds proofs. */
    return ntoh16(eth->h_proto) == ETH_P_IP ? XDP_DROP : XDP_PASS;
}
char license[] SEC("license") = "GPL";
