#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/pkt_cls.h>    
#include <bpf/bpf_helpers.h>


#define DROP_PORT 8080

#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

#ifndef bpf_htons
#define bpf_htons(x) __builtin_bswap16(x)
#endif
#ifndef bpf_ntohs
#define bpf_ntohs(x) __builtin_bswap16(x)
#endif
#ifndef bpf_htonl
#define bpf_htonl(x) __builtin_bswap32(x)
#endif
#ifndef bpf_ntohl
#define bpf_ntohl(x) __builtin_bswap32(x)
#endif

SEC("tc")
int tc_drop_port(struct __sk_buff *skb) {
    void *data     = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    // parse ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    // only IPv4
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    // ip header
    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    // compute transport header offset
    __u32 ihl = ip->ihl * 4;
    void *trans = (void *)ip + ihl;
    if (trans > data_end)
        return XDP_PASS;

    __u16 dst_port = 0;
    __u8 proto = ip->protocol;

    if (proto == IPPROTO_TCP) {
        struct tcphdr *tcp = trans;
        if ((void *)(tcp + 1) > data_end)
            return XDP_PASS;
        dst_port = bpf_ntohs(tcp->dest);
    } else if (proto == IPPROTO_UDP) {
        struct udphdr *udp = trans;
        if ((void *)(udp + 1) > data_end)
            return XDP_PASS;
        dst_port = bpf_ntohs(udp->dest);
    } else {
        return XDP_PASS;
    }

     if (dst_port == DROP_PORT) {
        // drop the packet
        return XDP_DROP;
    }


    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
