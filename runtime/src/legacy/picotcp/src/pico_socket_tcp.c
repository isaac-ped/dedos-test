#include "pico_config.h"
#include "pico_socket.h"
#include "pico_ipv4.h"
#include "pico_ipv6.h"
#include "pico_tcp.h"
#include "pico_socket_tcp.h"


static int sockopt_validate_args(struct pico_socket *s,  void *value)
{
    if (!value) {
        pico_err = PICO_ERR_EINVAL;
        return -1;
    }

    if (s->proto->proto_number != PICO_PROTO_TCP) {
        pico_err = PICO_ERR_EPROTONOSUPPORT;
        return -1;
    }

    return 0;
}

int pico_getsockopt_tcp(struct pico_socket *s, int option, void *value)
{
    if (sockopt_validate_args(s, value) < 0)
        return -1;

#ifdef PICO_SUPPORT_TCP
    if (option == PICO_TCP_NODELAY) {
        /* state of the NODELAY option */
        *(int *)value = PICO_SOCKET_GETOPT(s, PICO_SOCKET_OPT_TCPNODELAY);
        return 0;
    }
    else if (option == PICO_SOCKET_OPT_RCVBUF) {
        return pico_tcp_get_bufsize_in(s, (uint32_t *)value);
    }

    else if (option == PICO_SOCKET_OPT_SNDBUF) {
        return pico_tcp_get_bufsize_out(s, (uint32_t *)value);
    }

#endif
    return -1;
}

static void tcp_set_nagle_option(struct pico_socket *s, void *value)
{
    int *val = (int*)value;
    if (*val > 0) {
        dbg("setsockopt: Nagle algorithm disabled.\n");
        PICO_SOCKET_SETOPT_EN(s, PICO_SOCKET_OPT_TCPNODELAY);
    } else {
        dbg("setsockopt: Nagle algorithm enabled.\n");
        PICO_SOCKET_SETOPT_DIS(s, PICO_SOCKET_OPT_TCPNODELAY);
    }
}

int pico_setsockopt_tcp(struct pico_socket *s, int option, void *value)
{
    if (sockopt_validate_args(s, value) < 0)
        return -1;

#ifdef PICO_SUPPORT_TCP
    if (option ==  PICO_TCP_NODELAY) {
        tcp_set_nagle_option(s, value);
        return 0;
    }
    else if (option == PICO_SOCKET_OPT_RCVBUF) {
        uint32_t *val = (uint32_t*)value;
        pico_tcp_set_bufsize_in(s, *val);
        return 0;
    }
    else if (option == PICO_SOCKET_OPT_SNDBUF) {
        uint32_t *val = (uint32_t*)value;
        pico_tcp_set_bufsize_out(s, *val);
        return 0;
    }
    else if (option == PICO_SOCKET_OPT_KEEPCNT) {
        uint32_t *val = (uint32_t*)value;
        pico_tcp_set_keepalive_probes(s, *val);
        return 0;
    }
    else if (option == PICO_SOCKET_OPT_KEEPIDLE) {
        uint32_t *val = (uint32_t*)value;
        pico_tcp_set_keepalive_time(s, *val);
        return 0;
    }
    else if (option == PICO_SOCKET_OPT_KEEPINTVL) {
        uint32_t *val = (uint32_t*)value;
        pico_tcp_set_keepalive_intvl(s, *val);
        return 0;
    }
    else if (option == PICO_SOCKET_OPT_LINGER) {
        uint32_t *val = (uint32_t*)value;
        pico_tcp_set_linger(s, *val);
        return 0;
    }

#endif
    pico_err = PICO_ERR_EINVAL;
    return -1;
}

void pico_socket_tcp_cleanup(struct pico_socket *sock)
{
#ifdef PICO_SUPPORT_TCP
    /* for tcp sockets go further and clean the sockets inside queue */
    if(is_sock_tcp(sock))
        pico_tcp_cleanup_queues(sock);

#endif
}


void pico_socket_tcp_delete(struct pico_socket *s)
{
#ifdef PICO_SUPPORT_TCP
    if(s->parent)
        s->parent->number_of_pending_conn--;

#endif
}

static struct pico_socket *socket_tcp_deliver_ipv4(struct pico_socket *s, struct pico_frame *f)
{
    struct pico_socket *found = NULL;
    #ifdef PICO_SUPPORT_IPV4
    struct pico_ip4 s_local, s_remote, p_src, p_dst;
    struct pico_ipv4_hdr *ip4hdr = (struct pico_ipv4_hdr*)(f->net_hdr);
    struct pico_trans *tr = (struct pico_trans *) f->transport_hdr;
    s_local.addr = s->local_addr.ip4.addr;
    s_remote.addr = s->remote_addr.ip4.addr;
    p_src.addr = ip4hdr->src.addr;
    p_dst.addr = ip4hdr->dst.addr;
    if ((s->remote_port == tr->sport) && /* remote port check */
        (s_remote.addr == p_src.addr) && /* remote addr check */
        ((s_local.addr == PICO_IPV4_INADDR_ANY) || (s_local.addr == p_dst.addr))) { /* Either local socket is ANY, or matches dst */
        found = s;
        return found;
    } else if ((s->remote_port == 0)  && /* not connected... listening */
               ((s_local.addr == PICO_IPV4_INADDR_ANY) || (s_local.addr == p_dst.addr))) { /* Either local socket is ANY, or matches dst */
        /* listen socket */
        found = s;
    }

    #endif
    return found;
}

static struct pico_socket *socket_tcp_deliver_ipv6(struct pico_socket *s, struct pico_frame *f)
{
    struct pico_socket *found = NULL;
    #ifdef PICO_SUPPORT_IPV6
    struct pico_trans *tr = (struct pico_trans *) f->transport_hdr;
    struct pico_ip6 s_local = {{0}}, s_remote = {{0}}, p_src = {{0}}, p_dst = {{0}};
    struct pico_ipv6_hdr *ip6hdr = (struct pico_ipv6_hdr *)(f->net_hdr);
    s_local = s->local_addr.ip6;
    s_remote = s->remote_addr.ip6;
    p_src = ip6hdr->src;
    p_dst = ip6hdr->dst;
    if ((s->remote_port == tr->sport) &&
        (!memcmp(s_remote.addr, p_src.addr, PICO_SIZE_IP6)) &&
        ((!memcmp(s_local.addr, PICO_IP6_ANY, PICO_SIZE_IP6)) || (!memcmp(s_local.addr, p_dst.addr, PICO_SIZE_IP6)))) {
        found = s;
        return found;
    } else if ((s->remote_port == 0)  && /* not connected... listening */
               ((!memcmp(s_local.addr, PICO_IP6_ANY, PICO_SIZE_IP6)) || (!memcmp(s_local.addr, p_dst.addr, PICO_SIZE_IP6)))) {
        /* listen socket */
        found = s;
    }

    #else
    (void) s;
    (void) f;
    #endif
    return found;
}

static int socket_tcp_do_deliver(struct pico_socket *s, struct pico_frame *f)
{
    if (s != NULL) {
        pico_tcp_input(s, f);
        if ((s->ev_pending) && s->wakeup) {
            s->wakeup(s->ev_pending, s);
            if(!s->parent)
                s->ev_pending = 0;
        }

        return 0;
    }

    dbg("TCP SOCKET> Not s.\n");
    return -1;
}

int pico_socket_tcp_deliver(struct pico_sockport *sp, struct pico_frame *f)
{
    struct pico_socket *found = NULL;
    struct pico_tree_node *index = NULL;
    struct pico_tree_node *_tmp;
    struct pico_socket *s = NULL;


    pico_tree_foreach_safe(index, &sp->socks, _tmp){
        s = index->keyValue;
        /* 4-tuple identification of socket (port-IP) */
        if (IS_IPV4(f)) {
            found = socket_tcp_deliver_ipv4(s, f);
        }

        if (IS_IPV6(f)) {
            found = socket_tcp_deliver_ipv6(s, f);
        }

        if (found)
            break;
    } /* FOREACH */

    return socket_tcp_do_deliver(found, f);
}

struct pico_socket *pico_socket_tcp_open(uint16_t family)
{
    struct pico_socket *s = NULL;
    (void) family;
#ifdef PICO_SUPPORT_TCP
    s = pico_tcp_open(family);
    if (!s) {
        pico_err = PICO_ERR_ENOMEM;
        return NULL;
    }

    s->proto = &pico_proto_tcp;
    /*check if Nagle enabled */
    /*
       if (!IS_NAGLE_ENABLED(s))
           dbg("ERROR Nagle should be enabled here\n\n");
     */
#endif
    return s;
}

struct pico_socket *hs_pico_socket_tcp_open(uint16_t family, void* timers_heap)
{
    struct pico_socket *s = NULL;
    (void) family;
    s = hs_pico_tcp_open(family, timers_heap);
    if (!s) {
        pico_err = PICO_ERR_ENOMEM;
        return NULL;
    }

    s->proto = &pico_proto_tcp;

    return s;
}
int pico_socket_tcp_read(struct pico_socket *s, void *buf, uint32_t len)
{
#ifdef PICO_SUPPORT_TCP
    /* check if in shutdown state and if no more data in tcpq_in */
    if ((s->state & PICO_SOCKET_STATE_SHUT_REMOTE) && pico_tcp_queue_in_is_empty(s)) {
        pico_err = PICO_ERR_ESHUTDOWN;
        return -1;
    } else {
        return (int)(pico_tcp_read(s, buf, (uint32_t)len));
    }

#else
    return 0;
#endif
}

void transport_flags_update(struct pico_frame *f, struct pico_socket *s)
{
#ifdef PICO_SUPPORT_TCP
    if(is_sock_tcp(s))
        pico_tcp_flags_update(f, s);

#endif
}

void print_tcp_socket_queues(struct pico_socket *s){
    struct pico_socket_tcp *t = NULL;
    t = (struct pico_socket_tcp *)s;
    dbg("tcpq_in: max_size: %u, size: %u, frames: %u",t->tcpq_in.max_size, t->tcpq_in.size, t->tcpq_in.frames);
    dbg("tcpq_out: max_size: %u, size: %u, frames: %u",t->tcpq_out.max_size, t->tcpq_out.size, t->tcpq_out.frames);
    dbg("tcpq_hold: max_size: %u, size: %u, frames: %u",t->tcpq_hold.max_size, t->tcpq_hold.size, t->tcpq_hold.frames);
    dbg("sock_qin: max_size: %u, size: %u, frames: %u",s->q_in.max_size, s->q_in.size, s->q_in.frames);
    dbg("sock_qout: max_size: %u, size: %u, frames: %u",s->q_out.max_size, s->q_out.size, s->q_out.frames);
    dbg("t->retrans_tmr : %u, retrans_tmr_due: %lu", t->retrans_tmr, t->retrans_tmr_due);
}

void print_tcp_socket(struct pico_socket *s)
{
    struct pico_socket_tcp *t = NULL;
    t = (struct pico_socket_tcp *)s;

    dbg(">>>TCP SOCKET DUMP: \n");
    dbg(">TCP output:  \n");
    dbg("snd_nxt: 0x%08x\nsnd_last:0x%08x\nsnd_old_ack: 0x%08x\nsnd_retry: 0x%08x\nsnd_last_out: 0x%08x\n",
        t->snd_nxt, t->snd_last, t->snd_old_ack, t->snd_retry, t->snd_last_out);
    dbg(">TCP input:  \n");
    dbg("rcv_nxt: 0x%08x\nrcv_ackd: 0x%08x\nrcv_processed: 0x%08x\nwnd: 0x%08x\nwnd_scale: 0x%08x\nremote_closed: 0x%08x\n",
        t->rcv_nxt, t->rcv_ackd, t->rcv_processed, t->wnd, t->wnd_scale, t->remote_closed);
}
