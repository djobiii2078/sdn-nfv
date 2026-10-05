#include "../include/lab.h"
SEC("xdp")
int port_filter(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    struct ethhdr *eth = data;
    /* BUG: no proof that these bytes exist. */
    if (ntoh16(eth->h_proto) == ETH_P_IP) return XDP_DROP;
    return XDP_PASS;
}
char license[] SEC("license") = "GPL";
