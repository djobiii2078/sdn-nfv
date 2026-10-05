#include "../include/lab.h"
SEC("xdp")
int port_filter(struct xdp_md *ctx)
{
    /* First repair spelling. Compilation happens BEFORE verifier. */
    return XDP_PAS;
}
char license[] SEC("license") = "GPL";
