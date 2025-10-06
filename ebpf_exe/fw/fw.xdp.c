// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define IPPROTO_TCP 6

struct {
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __uint(max_entries, 64);
    __type(key, __u32);
    __type(value, __u32);
} xsks_map SEC(".maps");

SEC("xdp")
int xdp_sack_redirect(struct xdp_md *ctx)
{
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct ethhdr *eth = data;

    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    if (eth->h_proto != __bpf_htons(ETH_P_IP))
        return XDP_PASS;

    struct iphdr *iph = (void *)(eth + 1);
    if ((void *)(iph + 1) > data_end)
        return XDP_PASS;

    if (iph->protocol != IPPROTO_TCP)
        return XDP_PASS;

    struct tcphdr *tcph = (void *)iph + iph->ihl * 4;
    if ((void *)(tcph + 1) > data_end)
        return XDP_PASS;

    // Example: intercept TCP dest port 5000 only
    if (tcph->dest == __bpf_htons(5000)) {
        __u32 key = ctx->rx_queue_index;
        if (bpf_map_lookup_elem(&xsks_map, &key))
            return bpf_redirect_map(&xsks_map, key, 0);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";

