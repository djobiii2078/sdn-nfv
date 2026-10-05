#include "../include/lab.h"
SEC("xdp")
int port_filter(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data;
    void *end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > end) return XDP_PASS;
    if (ntoh16(eth->h_proto) != ETH_P_IP) return XDP_PASS;
    struct iphdr *ip = (void *)(eth + 1);
    /* BUG: proving Ethernet bounds proves nothing for the IP header. */
    return ip->protocol == 17 ? XDP_DROP : XDP_PASS;
}
char license[] SEC("license") = "GPL";
