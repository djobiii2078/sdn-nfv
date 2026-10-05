#ifndef LAB_PARSER_H
#define LAB_PARSER_H
#include "lab.h"
struct vlan_tag { __be16 tci, proto; };
/* Policy: pass unsupported L3. Drop malformed/truncated supported packets.
 * Drop ALL IPv4 TCP/UDP fragments: a deliberate conservative policy.
 * Parse at most two VLAN tags; pass deeper encapsulations as unsupported.
 */
INLINE int filter(void *data, void *end, __u16 port)
{
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > end) return XDP_DROP;
    __u16 proto = ntoh16(eth->h_proto);
    __u64 offset = sizeof(*eth);
#pragma unroll
    for (int i = 0; i < 2; i++) {
        if (proto != ETH_P_8021Q && proto != ETH_P_8021AD) break;
        struct vlan_tag *v = data + offset;
        if ((void *)(v + 1) > end) return XDP_DROP;
        proto = ntoh16(v->proto);
        offset += sizeof(*v);
    }
    if (proto != ETH_P_IP) return XDP_PASS;
    struct iphdr *ip = data + offset;
    if ((void *)(ip + 1) > end) return XDP_DROP;
    if (ip->version != 4 || ip->ihl < 5) return XDP_DROP;
    __u32 ihl = (__u32)ip->ihl * 4;
    if ((void *)ip + ihl > end) return XDP_DROP;
    __u32 total = ntoh16(ip->tot_len);
    if (total < ihl || (void *)ip + total > end) return XDP_DROP;
    if (ip->protocol != 6 && ip->protocol != 17) return XDP_PASS;
    if (ntoh16(ip->frag_off) & 0x3fff) return XDP_DROP;
#ifdef LAB_FIXED_IHL
    /* Bug accepted by verifier: mistakes options for transport header. */
    __u32 transport_offset = 20;
#else
    __u32 transport_offset = ihl;
#endif
    void *l4 = (void *)ip + transport_offset;
    __u16 dest;
    if (ip->protocol == 17) {
        struct udphdr *udp = l4;
        if ((void *)(udp + 1) > end) return XDP_DROP;
        if (total < transport_offset + sizeof(*udp)) return XDP_DROP;
        __u32 len = ntoh16(udp->len);
        if (len < sizeof(*udp) || len != total - transport_offset) return XDP_DROP;
        dest = udp->dest;
    } else {
        struct tcphdr *tcp = l4;
        if ((void *)(tcp + 1) > end) return XDP_DROP;
        if (total < transport_offset + sizeof(*tcp)) return XDP_DROP;
        __u32 hlen = (__u32)tcp->doff * 4;
        if (hlen < sizeof(*tcp) || hlen > total - transport_offset) return XDP_DROP;
        if ((void *)tcp + hlen > end) return XDP_DROP;
        dest = tcp->dest;
    }
#ifdef LAB_RAW_PORT
    return dest == port ? XDP_DROP : XDP_PASS;
#else
    return ntoh16(dest) == port ? XDP_DROP : XDP_PASS;
#endif
}
#endif
