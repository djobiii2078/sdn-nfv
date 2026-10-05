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
    if ((void *)(ip + 1) > end) return XDP_PASS;
    if (ip->version != 4 || ip->ihl < 5 || ip->protocol != 17) return XDP_PASS;
    __u32 ihl = (__u32)ip->ihl * 4;
    if ((void *)ip + ihl > end) return XDP_PASS;
    struct udphdr *udp = (void *)ip + ihl;
    /* BUG: validated IPv4 header, but no bounds check for UDP. */
    return ntoh16(udp->dest) == 8080 ? XDP_DROP : XDP_PASS;
}
char license[] SEC("license") = "GPL";
