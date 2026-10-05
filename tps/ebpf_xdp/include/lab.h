#ifndef LAB_H
#define LAB_H
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#define SEC(name) __attribute__((section(name), used))
#define __uint(name, val) int (*name)[val]
#define __type(name, val) val *name
#define INLINE static __inline __attribute__((always_inline))
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define ntoh16(x) __builtin_bswap16((__u16)(x))
#else
#define ntoh16(x) ((__u16)(x))
#endif
#ifndef LAB_NATIVE
static void *(*lookup)(void *, const void *) = (void *)BPF_FUNC_map_lookup_elem;
static long (*adjust_head)(struct xdp_md *, int) = (void *)BPF_FUNC_xdp_adjust_head;
#endif
#endif
