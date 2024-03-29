#include "pico_socket.h"
#include "pico_socket_tcp.h"
#include "pico_ipv4.h"
#include "pico_tcp.h"
#include "pico_eth.h"
#include "pico_protocol.h"
#include "runtime.h"
#include "modules/msu_tcp_handshake.h"
#include "pico_socket_tcp.h"
#include "communication.h"
#include "routing.h"
#include "dedos_thread_queue.h" //for enqueuing outgoing control messages
#include "control_protocol.h"
#include "dedos_hs_timers.h"
#include "dedos_msu_list.h"
#include "dedos_msu_msg_type.h"

#define INIT_SOCKPORT { {&LEAF, NULL}, 0, 0 }
#define PROTO(s) ((s)->proto->proto_number)
#define PICO_MIN_MSS (1280)
#define TCP_STATE(s) (s->state & PICO_SOCKET_STATE_TCP)

volatile pico_time hs_pico_tick;

uint32_t hs_timer_add(heap_hs_timer_ref *timers, pico_time expire, void (*timer)(pico_time, void *, void*), void *arg){
    struct hs_timer *t = PICO_ZALLOC(sizeof(struct hs_timer));
    struct hs_timer_ref tref;
    /* zero is guard for timers */
    if (base_tmr_id == 0u)
        base_tmr_id++;

    if (!t) {
        log_error("Failed to allocate memory for timer %s","");
        return 0;
    }

    tref.expire = PICO_TIME_MS() + expire;
    t->arg = arg;
    t->timer = timer;
    tref.tmr = t;
    tref.id = base_tmr_id++;
    hs_heap_insert(timers, &tref);
    // if (timers->n) {
    //     log_warn("HS Warning: I have %d timers", (int)timers->n);
    // }
    return tref.id;
}

void hs_timer_cancel(heap_hs_timer_ref *timers, uint32_t id){
    uint32_t i;
    struct hs_timer_ref *tref = timers->top;
    if (id == 0u)
        return;
    for (i = 1; i <= timers->n; i++) {
        if (tref[i].id == id) {
            PICO_FREE(timers->top[i].tmr);
            timers->top[i].tmr = NULL;
            break;
        }
    }
}

void hs_check_timers(heap_hs_timer_ref *timers){

    struct hs_timer *t;
    struct hs_timer_ref tref_unused, *tref = hs_heap_first(timers);
    hs_pico_tick = PICO_TIME_MS();
    while((tref) && (tref->expire < hs_pico_tick)) {
        log_debug("%s","Inside check timers while");
        t = tref->tmr;
        log_debug("%s","Inside check timers while, before if");
        if (t && t->timer){
            log_debug("%s","Inside check timers while, inside if");
            t->timer(hs_pico_tick, t->arg, timers);
        }

        if (t)
        {
            // log_debug("%s","----Freed a timer ----");
            PICO_FREE(t);
        }

        t = NULL;
        // log_debug("%s","Inside check timers while, after if, before peek\n");
        hs_heap_peek(timers, &tref_unused);
        // log_debug("%s","Inside check timers while, after if, after peek\n");
        tref = hs_heap_first(timers);
        // log_debug("%s","Inside check timers while, after heap_first\n");
    }
}

extern int socket_cmp(void *ka, void *kb);

static void send_to_next_msu(struct generic_msu *self,
        unsigned int message_type, char *buf, int bufsize, int reply_msu_id)
{
	//the received buff will be discarded by calling function
	// buf is f->buffer, bufsize is f->buffer_len

    /* TODO Lookup flow table for persistent next hop by parsing buf
     * based on next MSU type, e.g. if its next is HS, then buf should be
     * a frame and this function should extract the relevant info to find
     * the next MSU or some way of managing state in the flow table
     * and associate it with buf!
     * or
     * Lookup routing table to pick the msu of the next time
     */
    int next_msu_type = -1;
    if (message_type == MSU_PROTO_TCP_HANDSHAKE_RESPONSE) {
        next_msu_type = DEDOS_TCP_DATA_MSU_ID;
    }
    if (message_type == MSU_PROTO_TCP_CONN_RESTORE) {
        next_msu_type = DEDOS_TCP_DATA_MSU_ID;
    }

    struct msu_endpoint *all_msu_enpoints = get_all_type_msus(self->rt_table,
            next_msu_type);
    if (!all_msu_enpoints) {
        log_error("%s", "No next MSU info...can't continue");
        return;
    }
    //picking up the entry corresponding to the msu that send the request.
    struct msu_endpoint* tmp = NULL;
    tmp = get_msu_from_id(all_msu_enpoints, reply_msu_id);
    if (!tmp) {
        log_error("Could find msu endpoint with id: %d",reply_msu_id);
        return;
    }

    struct dedos_intermsu_message *msg;
	msg = (struct dedos_intermsu_message*) malloc(
			sizeof(struct dedos_intermsu_message));
	if (!msg) {
		log_error("Unable to allocate memory for intermsu message: %s", "");
		return;
	}

	msg->dst_msu_id = tmp->id;
	msg->src_msu_id = self->id;
	msg->proto_msg_type = message_type;
	msg->payload_len = bufsize;

    struct pico_frame* new_frame;
    char *restore_buf;

    if (is_endpoint_local(tmp) == 0){
		//create intermsu dedos msg as above
        log_debug("next data msu is local, msu id: %d",tmp->id);
        if(message_type == MSU_PROTO_TCP_HANDSHAKE_RESPONSE){
            //create a frame struct to enqueue
            new_frame = pico_frame_alloc(bufsize);

            memcpy(new_frame->buffer, buf, bufsize);
            new_frame->buffer_len = bufsize;
            new_frame->start = new_frame->buffer;
            new_frame->len = new_frame->buffer_len;
            new_frame->datalink_hdr = new_frame->buffer;
            new_frame->net_hdr = new_frame->datalink_hdr + PICO_SIZE_ETHHDR;
            new_frame->net_len = 20;
            new_frame->transport_hdr = new_frame->net_hdr + new_frame->net_len;
            new_frame->transport_len = (uint16_t) (new_frame->len - new_frame->net_len
                    - (uint16_t) (new_frame->net_hdr - new_frame->buffer));

            log_debug("Enqueuing following msg in next data msu's queue: %s","");
            log_debug("\tSRC MSU id: %d",msg->src_msu_id);
            log_debug("\tDST MSU id: %d",msg->dst_msu_id);
            log_debug("\tInterMSU msg type: %u",msg->proto_msg_type);
            log_debug("\tMsg payload: %s","");
            log_debug("Sending following frame to data msu: %s","");
            print_frame(new_frame);
            msg->payload = new_frame->buffer;

        } else if(message_type == MSU_PROTO_TCP_CONN_RESTORE) {
            // then payload is just a buffer
            restore_buf = malloc(bufsize);
            if(!restore_buf){
                log_error("Failed to malloc buf for conn restore %s","");
                free(msg);
                return;
            }
            memcpy(restore_buf, buf, bufsize);
            log_debug("Enqueuing following msg in next data msu's queue: %s","");
            log_debug("\tSRC MSU id: %d",msg->src_msu_id);
            log_debug("\tDST MSU id: %d",msg->dst_msu_id);
            log_debug("\tInterMSU msg type: %u",msg->proto_msg_type);
            log_debug("\tMsg payload is conn restore data: %s","");
            msg->payload = restore_buf;
        }

        //create queue item here and then enqueue
        struct generic_msu_queue_item *queue_item;
        queue_item = (struct generic_msu_queue_item*)malloc(sizeof(struct generic_msu_queue_item));

        if(!queue_item){
        	log_error("Failed to malloc queue item %s","");
            if(message_type == MSU_PROTO_TCP_HANDSHAKE_RESPONSE){
                pico_frame_discard(new_frame);
        	}
            else if(message_type == MSU_PROTO_TCP_CONN_RESTORE){
                free(restore_buf);
            }
            free(msg);
            return;
        }

        queue_item->buffer = msg;
        queue_item->buffer_len = msg->payload_len + sizeof(struct dedos_intermsu_message);
        queue_item->next = NULL;

        generic_msu_queue_enqueue(tmp->next_msu_input_queue, queue_item);
        log_debug("Enqueued msg in next data msu's queue: %s","");

        if(message_type == MSU_PROTO_TCP_HANDSHAKE_RESPONSE){
            free(new_frame);//just the frame struct buf not the buffer
        }
        return;

	}
	else {
		msg->payload = (char*) malloc(sizeof(char) * bufsize);
		if (!msg->payload) {
			log_error("Unable to allocate memory for intermsu payload: %s", "");
			free(msg);
			return;
		}
		memcpy(msg->payload, buf, bufsize);
		struct dedos_thread_msg *thread_msg;
		thread_msg = (struct dedos_thread_msg *) malloc(
				sizeof(struct dedos_thread_msg));
		if (!thread_msg) {
			log_error("Unable to allocate memory for dedos_thread_msg: %s", "");
			free(msg->payload);
			free(msg); //prev allocated mem
            return;
		}

		thread_msg->next = NULL;
		thread_msg->action = FORWARD_DATA;
		thread_msg->action_data = tmp->ipv4;

		thread_msg->buffer_len = sizeof(struct dedos_intermsu_message)
				+ msg->payload_len;
		thread_msg->data = (struct dedos_intermsu_message*) msg;

		/* add to allthreads[0] queue,since main thread is always stored at index 0 */
		/* need to create thread_msg struct with action = forward */
		int ret;
		ret = dedos_thread_enqueue(main_thread->thread_q, thread_msg);
		if (ret < 0) {
			log_error("Failed to enqueue data in main threads queue %s", "");
			free(thread_msg);
			free(msg->payload);
			free(msg); //prev allocated mem
			return;
		}
		log_debug("Successfully enqueued msg in main queue, which has size: %d", ret);
	}
}

static int check_socket_sanity(struct pico_socket *s)
{
    /* checking for pending connections */
    if (TCP_STATE(s) == PICO_SOCKET_STATE_TCP_SYN_RECV) {
        if ((PICO_TIME_MS() - s->timestamp) >= PICO_SOCKET_BOUND_TIMEOUT)
            return -1;
    }
    return 0;
}

static int check_socket_established_restore_timeout(struct pico_socket *s){
    if (TCP_STATE(s) == PICO_SOCKET_STATE_TCP_ESTABLISHED) {
        struct pico_socket_tcp *t = (struct pico_socket_tcp *)s;
        // log_debug("MS TIME: %u, ack_ts: %lu, wait_time: %u",PICO_TIME_MS(),t->ack_timestamp,PICO_TCP_WAIT_FOR_RESTORE);
        if ((PICO_TIME_MS() - t->ack_timestamp) >= PICO_TCP_WAIT_FOR_RESTORE)
            return -1;
    }
    return 0;
}

static int pico_sockets_pending_restore_check(struct hs_internal_state *in_state)
{
    struct pico_sockport *start;
    struct pico_socket *s;
    static struct pico_tree_node *index_msu;
    struct pico_sockport *sp_msu = NULL;
    struct pico_tree *msu_table = in_state->hs_table;
    if(pico_tree_empty(msu_table)){
        return 0;
    }
    index_msu = pico_tree_firstNode(msu_table->root);
    sp_msu = index_msu->keyValue;
    start = sp_msu;
    while (sp_msu != NULL) {
        struct pico_tree_node *index = NULL, *safe_index = NULL;
        pico_tree_foreach_safe(index, &sp_msu->socks, safe_index)
        {
            s = index->keyValue;
            if (check_socket_established_restore_timeout(s) < 0) {
                remove_completed_request(in_state, s);
                index_msu = NULL; /* forcing the restart of loop */
                sp_msu = NULL;
                break;
            }
        }

        if (!index_msu || (index && index->keyValue))
            break;

        index_msu = pico_tree_next(index_msu);
        sp_msu = index_msu->keyValue;

        if (sp_msu == NULL) {
            index_msu = pico_tree_firstNode(msu_table->root);
            sp_msu = index_msu->keyValue;
        }

        if (sp_msu == start)
            break;
    }

    return 0;
}

static int pico_sockets_pending_ack_check(struct hs_internal_state *in_state)
{
    struct pico_sockport *start;
    struct pico_socket *s;
    static struct pico_tree_node *index_msu;
    struct pico_sockport *sp_msu = NULL;
    struct pico_tree *msu_table = in_state->hs_table;
    if(pico_tree_empty(msu_table)){
        return 0;
    }
    index_msu = pico_tree_firstNode(msu_table->root);
    sp_msu = index_msu->keyValue;

    start = sp_msu;
    while (sp_msu != NULL) {
        struct pico_tree_node *index = NULL, *safe_index = NULL;
        pico_tree_foreach_safe(index, &sp_msu->socks, safe_index)
        {
            s = index->keyValue;
            if (check_socket_sanity(s) < 0) {
                remove_completed_request(in_state, s);
                index_msu = NULL; /* forcing the restart of loop */
                sp_msu = NULL;
                break;
            }
        }

        if (!index_msu || (index && index->keyValue))
            break;

        index_msu = pico_tree_next(index_msu);
        sp_msu = index_msu->keyValue;

        if (sp_msu == NULL) {
            index_msu = pico_tree_firstNode(msu_table->root);
            sp_msu = index_msu->keyValue;
        }

        if (sp_msu == start)
            break;
    }

    return 0;
}

static struct pico_sockport* find_hs_sockport(struct pico_tree* msu_table,
        uint16_t local_port)
{

    struct pico_sockport *found = NULL;
    struct pico_sockport test = INIT_SOCKPORT;
    test.number = local_port;
    found = pico_tree_findKey(msu_table, &test);
    return found;
}

static int8_t create_hs_sockport(struct pico_tree* msu_table,
        uint16_t localport)
{
    //the request will only reach here if the picoTCP is
    //actually listening on this socket, so attacker cannot
    //cause creation of sockports on random ports
    struct pico_sockport *sp = NULL;
//    PICOTCP_MUTEX_LOCK(Mutex);
    log_debug("Creating sockport..%04x", localport);
    sp = PICO_ZALLOC(sizeof(struct pico_sockport));

    if (!sp) {
        pico_err = PICO_ERR_ENOMEM;
//        PICOTCP_MUTEX_UNLOCK(Mutex);
        return -1;
    }
    log_debug("Allocated sockport memory for %u", short_be(localport));
    sp->proto = MSU_PROTO_TCP_HANDSHAKE;
    sp->number = localport;
    sp->socks.root = &LEAF;
    sp->socks.compare = socket_cmp;

    pico_tree_insert(msu_table, sp);
    //    PICOTCP_MUTEX_UNLOCK(Mutex);
    return 0;
}

int8_t remove_completed_request(struct hs_internal_state *in_state,
        struct pico_socket *s)
{
    struct pico_tree *msu_table = in_state->hs_table;
    struct pico_sockport *sp = NULL;
    sp = find_hs_sockport(msu_table, s->local_port);

    if (!sp) {
        log_error("Cannot find sockport for port: %u", short_be(s->local_port));
        return -1;
    }

    pico_tree_delete(&sp->socks, s);
    pico_socket_tcp_delete(s);
    s->state = PICO_SOCKET_STATE_CLOSED;
    hs_timer_add(in_state->hs_timers, (pico_time) 10, hs_socket_garbage_collect, s);

    struct pico_tree_node *index;
#if DEBUG != 0
    log_info("Current sockets for port %u >>>", short_be(s->local_port));
    pico_tree_foreach(index, &sp->socks)
    {
        s = index->keyValue;
        log_info(">>>> List Socket lc=%hu rm=%hu", short_be(s->local_port),
                short_be(s->remote_port));
    }
    log_debug("%s", "Removed completed request from socket tree");

#endif

    return 0;
}

static inline void tcp_send_add_tcpflags(struct pico_socket_tcp *ts,
        struct pico_frame *f)
{
    struct pico_tcp_hdr *hdr = (struct pico_tcp_hdr *) f->transport_hdr;
    if (ts->rcv_nxt != 0) {
        if ((ts->rcv_ackd == 0)
                || (pico_seq_compare(ts->rcv_ackd, ts->rcv_nxt) != 0)
                || (hdr->flags & PICO_TCP_ACK)) {
            hdr->flags |= PICO_TCP_ACK;
            hdr->ack = long_be(ts->rcv_nxt);
            ts->rcv_ackd = ts->rcv_nxt;
        }
    }

    if (hdr->flags & PICO_TCP_SYN) {
        ts->snd_nxt++;
    }

    if (f->payload_len > 0) {
        hdr->flags |= PICO_TCP_PSH | PICO_TCP_ACK;
        hdr->ack = long_be(ts->rcv_nxt);
        ts->rcv_ackd = ts->rcv_nxt;
    }
}

static int8_t add_pending_request(struct pico_tree *msu_table,
        struct pico_socket *s)
{

    struct pico_sockport *sp = NULL;
    sp = find_hs_sockport(msu_table, s->local_port);

//    PICOTCP_MUTEX_LOCK(Mutex);
    if (!sp) {
        log_error("Cannot find sockport for port: %u", short_be(s->local_port));
        return -1;
    }

    pico_tree_insert(&sp->socks, s);
    s->state |= PICO_SOCKET_STATE_BOUND;
//    PICOTCP_MUTEX_UNLOCK(Mutex);
#if DEBUG != 0
    log_info("Current sockets for port %u >>>", short_be(s->local_port));
    struct pico_tree_node *index;
    pico_tree_foreach(index, &sp->socks)
    {
        s = index->keyValue;
        log_info(">>>> List Socket lc=%hu rm=%hu", short_be(s->local_port),
                short_be(s->remote_port));
    }
#endif
    return 0;
}

static int dedos_tcp_send_synack(struct generic_msu* self,
        struct pico_socket *s, int reply_msu_id)
{
    struct pico_socket_tcp *ts = TCP_SOCK(s);
    struct pico_frame *synack;
    struct pico_tcp_hdr *hdr;
    uint16_t opt_len = tcp_options_size(ts, PICO_TCP_SYN | PICO_TCP_ACK);
    synack = s->net->alloc(s->net, (uint16_t) (PICO_SIZE_TCPHDR + opt_len));
    if (!synack)
        return -1;
    hdr = (struct pico_tcp_hdr *) synack->transport_hdr;

    synack->sock = s;
    hdr->len = (uint8_t) ((PICO_SIZE_TCPHDR + opt_len) << 2 | ts->jumbo);
    hdr->flags = PICO_TCP_SYN | PICO_TCP_ACK;
    hdr->rwnd = short_be(ts->wnd);
    hdr->seq = long_be(ts->snd_nxt);
    ts->rcv_processed = long_be(hdr->seq);
    ts->snd_last = ts->snd_nxt;
    tcp_set_space(ts);
    tcp_add_options(ts, synack, hdr->flags, opt_len);
    synack->payload_len = 0;
    synack->timestamp = TCP_TIME;
// #ifdef MSU_SAME_HOST
//     msu_dbg("DEBUG: %s", "Same host MSU\n");
//     tcp_send(ts, synack);
// #else
    hdr->trans.sport = ts->sock.local_port;
    hdr->trans.dport = ts->sock.remote_port;
    if (!hdr->seq)
        hdr->seq = long_be(ts->snd_nxt);

    tcp_send_add_tcpflags(ts, synack);

    synack->start = synack->transport_hdr + PICO_SIZE_TCPHDR;
    hdr->rwnd = short_be(ts->wnd);
    hdr->crc = 0;
    hdr->crc = short_be(pico_tcp_checksum(synack));
    //copy ip4 stuff to frame from socket
    uint8_t ttl = PICO_IPV4_DEFAULT_TTL;
    uint8_t vhl = 0x45; /* version 4, header length 20 */
    static uint16_t ipv4_progressive_id = 0x91c0;
    uint8_t proto = s->proto->proto_number;
    struct pico_ipv4_hdr *iphdr;
    iphdr = (struct pico_ipv4_hdr *) synack->net_hdr;

    iphdr->vhl = vhl;
    iphdr->len = short_be((uint16_t) (synack->transport_len + synack->net_len));
    iphdr->id = short_be(ipv4_progressive_id);
    iphdr->dst.addr = s->remote_addr.ip4.addr;
    iphdr->src.addr = s->local_addr.ip4.addr;
    iphdr->ttl = ttl;
    iphdr->tos = synack->send_tos;
    iphdr->proto = proto;
    iphdr->frag = short_be(PICO_IPV4_DONTFRAG);
    iphdr->crc = 0;
    iphdr->crc = short_be(pico_checksum(iphdr, synack->net_len));

    log_debug("Sending following SYNACK frame to data for forwarding: %s","");
    print_frame(synack);

    send_to_next_msu(self, MSU_PROTO_TCP_HANDSHAKE_RESPONSE, synack->buffer,
            synack->buffer_len, reply_msu_id);

// #endif
    pico_frame_discard(synack);
    return 0;
}

static int handle_syn(struct generic_msu* self, struct pico_tree *msu_table,
        struct pico_frame *f, int reply_msu_id)
{

    /* normally would clone of listen socket
     * here this is the clone that will be copied to data msu after
     * connection establishment
     */
    log_debug("%s", "Handling SYN packet...");
    struct hs_internal_state *in_state = self->internal_state;
    struct pico_socket *s = hs_pico_socket_tcp_open(PICO_PROTO_IPV4, in_state->hs_timers);
    if (!s) {
        log_error("%s", "Error creating local socket");
        return -1;
    }
    struct pico_socket_tcp *new = NULL;
    struct pico_tcp_hdr *hdr = NULL;
    uint16_t mtu;

    s->q_in.max_size = PICO_DEFAULT_SOCKETQ;
    s->q_out.max_size = PICO_DEFAULT_SOCKETQ;
    s->wakeup = NULL; //will know this later during restore from parent listen socket
    // Initialization of empty socket done till here

    log_debug("%s", "Empty socket initialization done...");
    new = (struct pico_socket_tcp *) s;
    hdr = (struct pico_tcp_hdr *) f->transport_hdr;

    new->sock.remote_port = ((struct pico_trans *) f->transport_hdr)->sport;
    new->sock.local_port = ((struct pico_trans *) f->transport_hdr)->dport;
    log_debug("%s", "local and remote port assigned to socket...");

    new->sock.remote_addr.ip4.addr =
            ((struct pico_ipv4_hdr *) (f->net_hdr))->src.addr;
    new->sock.local_addr.ip4.addr =
            ((struct pico_ipv4_hdr *) (f->net_hdr))->dst.addr;

    log_debug("remote port : %u, local port: %u", short_be(new->sock.remote_port), short_be(new->sock.local_port));

    f->sock = &new->sock;
    tcp_parse_options(f);
    mtu = (uint16_t) pico_socket_get_mss(&new->sock);
    new->mss = (uint16_t) (mtu - PICO_SIZE_TCPHDR );
    new->tcpq_in.max_size = PICO_DEFAULT_SOCKETQ;
    new->tcpq_out.max_size = PICO_DEFAULT_SOCKETQ;
    new->tcpq_hold.max_size = 2u * mtu;
    new->rcv_nxt = long_be(hdr->seq) + 1;
    new->snd_nxt = long_be(pico_paws());
    new->snd_last = new->snd_nxt;
    new->cwnd = PICO_TCP_IW;
    new->ssthresh = (uint16_t) ((uint16_t) (PICO_DEFAULT_SOCKETQ / new->mss)
            - (((uint16_t) (PICO_DEFAULT_SOCKETQ / new->mss)) >> 3u));
    new->recv_wnd = short_be(hdr->rwnd);
    new->jumbo = hdr->len & 0x07;
    new->linger_timeout = PICO_SOCKET_LINGER_TIMEOUT;
    new->sock.parent = NULL;
    new->sock.wakeup = NULL;
    rto_set(new, PICO_TCP_RTO_MIN);
    /* Initialize timestamp values */
    new->sock.state = PICO_SOCKET_STATE_BOUND | PICO_SOCKET_STATE_CONNECTED
            | PICO_SOCKET_STATE_TCP_SYN_RECV;

    add_pending_request(msu_table, &new->sock);
    //print_tcp_socket(&new->sock);

    dedos_tcp_send_synack(self, &new->sock, reply_msu_id);
    log_debug("SYNACK sent, socket added. snd_nxt is 0x%08x", new->snd_nxt);

    return 0;
}

static int handle_ack(struct generic_msu *self, struct pico_frame *f, struct pico_socket_tcp *t, int reply_msu_id)
{

    struct pico_socket *s = (struct pico_socket *) t;
    struct pico_tcp_hdr *hdr = (struct pico_tcp_hdr *) f->transport_hdr;
    log_debug("Handling ACK, expecting %08x got %08x", t->snd_nxt, ACKN(f));
    struct hs_internal_state *in_state = self->internal_state;

    if (t->snd_nxt == ACKN(f)) {
        tcp_set_init_point(s);
        //print_tcp_socket(&t->sock);
        f->sock = s;
        hs_tcp_ack(s, f, in_state->hs_timers);
        //print_tcp_socket(&t->sock);
        s->state &= 0x00FFU;
        s->state |= PICO_SOCKET_STATE_TCP_ESTABLISHED;
#if DEBUG != 0
        log_info("TCP Connection established. State: %04x", s->state);
        log_debug("snd_nxt is now %08x", t->snd_nxt);
        log_debug("rcv_nxt is now %08x", t->rcv_nxt);
#endif
    }

    return 0;
}

static int handle_bad_con(struct pico_frame *f)
{
    /* send reset here*/
    log_error("%s", "Bad Frame..doing nothing");
    return 0;
}

static struct pico_socket *match_tcp_socket(struct pico_socket *s,
        struct pico_frame *f)
{
    struct pico_socket *found = NULL;

    struct pico_ip4 s_local, s_remote, p_src, p_dst;
    struct pico_ipv4_hdr *ip4hdr = (struct pico_ipv4_hdr*) (f->net_hdr);
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
    } else if ((s->remote_port == 0) && /* not connected... listening */
    ((s_local.addr == PICO_IPV4_INADDR_ANY) || (s_local.addr == p_dst.addr))) { /* Either local socket is ANY, or matches dst */
        /* listen socket */
        found = s;
    }

    return found;
}

struct pico_socket *find_tcp_socket(struct pico_sockport *sp,
        struct pico_frame *f)
{

    struct pico_socket *found = NULL;
    struct pico_tree_node *index = NULL;
    struct pico_tree_node *_tmp;
    struct pico_socket *s = NULL;

    pico_tree_foreach_safe(index, &sp->socks, _tmp)
    {
        s = index->keyValue;
        /* 4-tuple identification of socket (port-IP) */
        if (IS_IPV4(f)) {
            found = match_tcp_socket(s, f);
        }

        if (found)
            break;
    } /* FOREACH */

    return found;
}

static int msu_process_hs_request_in(struct generic_msu *self, int reply_msu_id,
        struct pico_frame *f)
{
    //DONT NOT FREE THE INCOMING FRAME HERE
    /* check what flags are set and take appropriate action */
    log_debug("Received frame: %s","");
    print_frame(f);
    struct pico_tcp_hdr *hdr = (struct pico_tcp_hdr *) (f->transport_hdr);
    struct pico_trans *trans_hdr = (struct pico_trans *) f->transport_hdr;
    int ret = -1;
    uint8_t flags = hdr->flags;

    // Check if sockport exists for incoming frame, if not create one
    struct pico_sockport *sp = NULL;
    struct hs_internal_state *in_state = self->internal_state;

    if (!in_state->hs_table) {
        log_error("%s", "No internal state found!");
        goto end;
    }
    sp = find_hs_sockport(in_state->hs_table, trans_hdr->dport);
    if (!sp) {
        ret = create_hs_sockport(in_state->hs_table, trans_hdr->dport);
        if (ret) {
            log_error("Failed to create sockport for %u",
                    short_be(trans_hdr->dport));
            goto end2;
        }
        log_debug("Sockport creation complete %u", short_be(trans_hdr->dport));
        sp = find_hs_sockport(in_state->hs_table, trans_hdr->dport);
    }

    //Check if there is an existing socket associated with this
    struct pico_socket_tcp *tcp_sock_found = NULL;
    struct pico_socket *sock_found = find_tcp_socket(sp, f);

    tcp_sock_found = (struct pico_socket_tcp *) sock_found;

    // take care socket found or not based of flags and current state of socket
    if (flags == PICO_TCP_SYN && !tcp_sock_found) /* First SYN */
    {
        // first syn
        log_debug("Received SYN %d", 1);
        ret = handle_syn(self, in_state->hs_table, f, reply_msu_id);
        if (ret != 0) {
            log_error("%s", "Failed to handle_syn");
        }
    } else if (flags == PICO_TCP_SYN && tcp_sock_found) /* Duplicate SYN */
    {
        log_debug("Received SYN duplicate %s","");
        if (sock_found->state
                == (PICO_SOCKET_STATE_BOUND | PICO_SOCKET_STATE_TCP_SYN_RECV |
                PICO_SOCKET_STATE_CONNECTED)) {
            if (tcp_sock_found->rcv_nxt == long_be(hdr->seq) + 1u) {
                /* take back our own SEQ number to its original value,
                 * so the synack retransmitted is identical to the original.
                 */
                tcp_sock_found->snd_nxt--;
                log_debug("Sending original SYN ACK %s", "");
                dedos_tcp_send_synack(self, &tcp_sock_found->sock, reply_msu_id);
            } else {
                log_warn("Bad seqs..Sending a reset %s","");
                tcp_send_rst(sock_found, f);
                goto end;
            }
        } else {
            log_warn("Bad state associated with received SYN and found sock%s","");
            goto end;
        }
    } else if ((flags == PICO_TCP_ACK || flags == PICO_TCP_PSHACK ) && sock_found) /* ACK for prev seen SYN */
    {
        log_debug("Recieved an ACK, with a known socket %s", "");
        if (sock_found->state
                == (PICO_SOCKET_STATE_BOUND | PICO_SOCKET_STATE_TCP_SYN_RECV |
                PICO_SOCKET_STATE_CONNECTED)) /* First ACK */
        {
            log_debug("%s", "This is the first ack");
            handle_ack(self, f, tcp_sock_found, reply_msu_id);

#ifdef PICO_SUPPORT_SAME_THREAD_TCP_MSUS
            {
                //create a new clone socket to add
                struct pico_socket *s = pico_socket_tcp_open(PICO_PROTO_IPV4);
                struct pico_socket_tcp *new = (struct pico_socket_tcp *)s;

                struct pico_socket *old = &tcp_sock_found->sock;

                if (!s) {
                    log_error("%s", "Error creating local socket");
                    return -1;
                }
                struct pico_tcp_hdr *hdr = NULL;

                hdr = (struct pico_tcp_hdr *) f->transport_hdr;

                memcpy(&s->local_addr, &old->local_addr, sizeof(struct pico_ip4));
                memcpy(&s->remote_addr, &old->remote_addr, sizeof(struct pico_ip4));

                s->local_port = old->local_port;
                s->remote_port = old->remote_port;

                s->ev_pending = old->ev_pending;
                s->max_backlog = old->max_backlog;
                s->number_of_pending_conn = old->number_of_pending_conn;

                s->state = old->state;
                s->opt_flags = old->opt_flags;
                s->timestamp = old->timestamp;

                //copy tcp socket data
                memcpy(&new->snd_nxt, &tcp_sock_found->snd_nxt,
                        sizeof(struct pico_socket_tcp)
                                - (sizeof(struct pico_socket) +
                                    (3 * sizeof(struct pico_tcp_queue))
                                ));


                //call the same thread restore here
                msu_tcp_hs_same_thread_transfer(new, f);
                goto end2;
            }
#endif
            void *transfer_buf = NULL;
            log_debug("Collecting socket info %s","");
            struct pico_socket_tcp_dump *collected_tcp_sock =
                    msu_tcp_hs_collect_socket((void*) sock_found);

            //send to next msu
            send_to_next_msu(self, MSU_PROTO_TCP_CONN_RESTORE,
                    (char*)collected_tcp_sock, sizeof(struct pico_socket_tcp_dump), reply_msu_id);

            //free collected_tcp_sock after sending the buff
            PICO_FREE(collected_tcp_sock);
            log_debug("%s","Freed collected socket");

//#endif
            //Free the resources here(remove socket connection) //move this to later and free after a while
            // in case we get a duplicate First Ack
            //following is now checked by pico_sockets_msu_check()
//            ret = remove_completed_request(in_state,
//                    (struct pico_socket *)tcp_sock_found);
        }
        //Duplicate first ACK
        else if(sock_found->state == (PICO_SOCKET_STATE_BOUND | PICO_SOCKET_STATE_TCP_ESTABLISHED |
                                                                PICO_SOCKET_STATE_CONNECTED))
        {
            log_warn("Duplicate first ACK %s",""); //TODO
            /* take back our own SEQ number to its original value
             * faking to client since we don't want to lose anytime in the meantime
             * when connection is being restored
             * so the synack retransmitted is identical to the original.
             */
            tcp_sock_found->snd_nxt--;
            log_debug("Sending original SYN ACK for keeping TCP happy..%s", "");
            //TODO here now send the original SYN/ACK back so that the client again sends the first ACK,
            //and if the connection has been restored in data MSU, that that will take take of duplicate,
            //first ACK and all things will be happy thereafter.
            dedos_tcp_send_synack(self, &tcp_sock_found->sock, reply_msu_id);
        }
    } else if ((flags == PICO_TCP_ACK || flags == PICO_TCP_PSHACK) && !sock_found) {
    	log_warn("ACK with no associated socket %s","");
    }
    else {
        log_warn("%s", "Unexpected flags set.or a random ack. Ignoring");
        log_warn("Ignoring frame %s", "");
    }

end2:
    pico_sockets_pending_ack_check(in_state); //for connection that only sent SYN
    pico_sockets_pending_restore_check(in_state); //for sockets pending restore
end:
    return ret;
}
/*
 static int msu_process_hs_requeset_out(struct pico_protocol *self,
 struct pico_frame *f)
 {
 printf(
 "msu_process_hs_requeset_out: will output the connected socket data\n");
 Shoud enqueue in tcp_layer queue
 * then need to modify the processing of this kind of data on the other end

 return 0;
 }
 */

void* msu_tcp_hs_collect_socket(void *data)
{

    struct pico_socket *s = (struct pico_socket*) data;
    struct pico_socket_tcp *t = (struct pico_socket_tcp*) s;

    struct pico_socket_tcp_dump *t_dump = PICO_ZALLOC(
            sizeof(struct pico_socket_tcp_dump));
    struct pico_socket_dump *s_dump = (struct pico_socket_dump*) t_dump;

    //copy pico_socket data
    
    s_dump->l4_proto = s->proto->proto_number;
    s_dump->l3_net = s->net->proto_number;

    memcpy(&s_dump->local_addr, &s->local_addr, sizeof(struct pico_ip4));
    memcpy(&s_dump->remote_addr, &s->remote_addr, sizeof(struct pico_ip4));

    s_dump->local_port = s->local_port;
    s_dump->remote_port = s->remote_port;

    s_dump->ev_pending = s->ev_pending;
    s_dump->max_backlog = s->max_backlog;
    s_dump->number_of_pending_conn = s->number_of_pending_conn;

    s_dump->state = s->state;
    s_dump->opt_flags = s->opt_flags;
    s_dump->timestamp = s->timestamp;

    //copy tcp socket data
    memcpy(&t_dump->snd_nxt, &t->snd_nxt,
            sizeof(struct pico_socket_tcp_dump)
                    - sizeof(struct pico_socket_dump));

    //debug queues to see if they have data
    //print_tcp_socket_queues(s);

    //return pointer to serialized buffer
    log_debug("%s", "Collected Socket");
    return (void*) t_dump;
}

static struct pico_socket *match_tcp_sockets(struct pico_socket *s1,
        struct pico_socket *s2)
{
    struct pico_socket *found = NULL;
    if ((s1->remote_port == s2->remote_port) && /* remote port check */
    (s1->remote_addr.ip4.addr == s2->remote_addr.ip4.addr)
            && /* remote addr check */
            ((s1->local_addr.ip4.addr == PICO_IPV4_INADDR_ANY)
                    || (s1->local_addr.ip4.addr == s2->local_addr.ip4.addr))) { /* Either local socket is ANY, or matches dst */
        found = s1;
        return found;
    } else if ((s1->remote_port == 0) && /* not connected... listening */
    (s1->local_port == s2->local_port)) { /* Either local socket is ANY, or matches dst */
        /* listen socket */
        found = s1;
    }

    return found;
}

static struct pico_socket *find_listen_sock(struct pico_sockport *sp, struct pico_socket *sock)
{

    struct pico_socket *found = NULL;
    struct pico_tree_node *index = NULL;
    struct pico_tree_node *_tmp;
    struct pico_socket *s = NULL;

    pico_tree_foreach_safe(index, &sp->socks, _tmp)
    {
        s = index->keyValue;
        /* 4-tuple identification of socket (port-IP) */
        if (sock->net->proto_number == PICO_PROTO_IPV4) {
            found = match_tcp_sockets(s, sock);
        }

        if (found)
            break;
    } /* FOREACH */

    return found;
}

char* pico_frame_to_buf(struct pico_frame *f)
{
    int i = 0;

    return (char*) f->buffer;

    log_debug("f: %p", *f);
    log_debug("f->buffer : %p", f->buffer);
    log_debug("f->buffer_len: %u", f->buffer_len);
    log_debug("f->start: %p", f->start);
    log_debug("f->len: %u", f->len);
    log_debug("f->datalink_hdr: %p", f->datalink_hdr);
    log_debug("f->net_hdr: %p", f->net_hdr);
    log_debug("f->net_len: %u", f->net_len);
    log_debug("f->transport_hdr: %p", f->transport_hdr);
    log_debug("f->transport_len: %u", f->transport_len);

}

void buf_to_pico_frame(struct pico_frame **f, void **buf, uint16_t bufsize)
{
    *f = *buf;
    (*f)->buffer = (*f)->start = *buf;
    (*f)->buffer_len = (*f)->len = bufsize;
    (*f)->datalink_hdr = *buf;
    (*f)->net_hdr = (*f)->buffer + PICO_SIZE_ETHHDR + 2;
    (*f)->net_len = 20;
    (*f)->transport_hdr = (*f)->buffer + 34;
    (*f)->transport_len = (uint16_t) ((*f)->len - (*f)->net_len
            - (uint16_t) ((*f)->net_hdr - (*f)->buffer));
}

void print_frame(struct pico_frame *f)
{
    struct pico_tcp_hdr *hdr = (struct pico_tcp_hdr *) (f->transport_hdr);
    int ret = 0;
    uint8_t flags = hdr->flags;

    f->payload = (f->transport_hdr + ((hdr->len & 0xf0u) >> 2u));
    f->payload_len = (uint16_t) (f->transport_len - ((hdr->len & 0xf0u) >> 2u));
    log_debug("> frame_buffer_len %u", f->buffer_len);
    log_debug("> [tcp input] t_len: %u", f->transport_len);
    log_debug("> flags = 0x%02x", hdr->flags);
    log_debug(
            "> dst port:%u src port: %u seq: 0x%08x ack: 0x%08x flags: 0x%02x t_len: %u, hdr: %u payload: %d",
            short_be(hdr->trans.dport), short_be(hdr->trans.sport), SEQN(f),
            ACKN(f), hdr->flags, f->transport_len, (hdr->len & 0xf0) >> 2,
            f->payload_len);

    struct pico_ipv4_hdr *net_hdr = (struct pico_ipv4_hdr *) f->net_hdr;
    char peer[30], local[30];
    pico_ipv4_to_string(peer, net_hdr->src.addr);
    pico_ipv4_to_string(local, net_hdr->dst.addr);
    log_debug("Src addr %s: dst addr : %s", peer, local);
}

static void handle_incoming_frame(struct generic_msu *self, int reply_dst_msu_id,
        struct pico_frame *f)
{

    struct pico_tcp_hdr *hdr = (struct pico_tcp_hdr *) (f->transport_hdr);
    int ret = 0;
    uint8_t flags = hdr->flags;
    struct pico_ipv4_hdr *iphdr = (struct pico_ipv4_hdr *) f->net_hdr;

    if (flags == (PICO_TCP_SYN | PICO_TCP_ACK)) {
        //forwarding logic only relevant at data MSU
        log_warn("%s", "HS Shouldn't be getting a SYNACK FRAME!");

    } else {
        log_debug("%s", "Non SYNACK frame, enqueuing for processing");
        struct generic_msu_queue_item *item = create_generic_msu_queue_item();
        if(!item){
            log_error("Failed to allocate memory for generic queue item %s","");
        }
        struct hs_queue_item *queue_payload = (struct hs_queue_item*)malloc(sizeof(struct hs_queue_item));
        if(!queue_payload){
            log_error("Failed to allocate memory for hs_queue_item %s","");
            return;
        }
        queue_payload->f = f;
        queue_payload->reply_msu_id = reply_dst_msu_id;
        item->buffer = queue_payload;
        item->buffer_len = f->buffer_len + sizeof(struct hs_queue_item);

        ret = generic_msu_queue_enqueue(&self->q_in, item);
        log_debug("Enqueued MSU_PROTO_TCP_HANDSHAKE request: %d", ret);
    }
}

static int valid_ip_tcp_flags(struct pico_frame *f)
{

    struct pico_tcp_hdr *hdr = (struct pico_tcp_hdr *) (f->transport_hdr);
    uint8_t flags = hdr->flags;
    if (IS_IPV4(f)
            && (flags == PICO_TCP_SYN || flags == (PICO_TCP_SYN | PICO_TCP_ACK)
                    || flags == PICO_TCP_ACK)) {

        return 0; //if valid
    } else {
        return -1;
    }
}

int msu_tcp_hs_restore_socket(local_msu *self, struct dedos_intermsu_message* msg, 
                              void *buf, uint16_t bufsize)
{
    struct pico_frame *f = pico_frame_alloc(bufsize);
    char *bufs = (char *) buf;

    memcpy(f->buffer, bufs, bufsize);
    f->buffer_len = bufsize;
    f->start = f->buffer;
    f->len = f->buffer_len;
    f->datalink_hdr = f->buffer;
    f->net_hdr = f->datalink_hdr + PICO_SIZE_ETHHDR;
    f->net_len = 20;
    f->transport_hdr = f->net_hdr + f->net_len;
    f->transport_len = (uint16_t) (f->len - f->net_len
            - (uint16_t) (f->net_hdr - f->buffer));

    if (msg->proto_msg_type == MSU_PROTO_TCP_HANDSHAKE) {
        log_debug("%s", "Received a valid TCP frame on control socket");
        handle_incoming_frame(self, msg->src_msu_id, f);
    }

    return 0;
}

/* 
 * This was used to transfer on the same machine
 * Couldn't find where it was called so it's commented out for now
 *
int msu_tcp_hs_same_thread_transfer(void *data, void *optional_data)
{
    log_debug("%s", "Same host established conn socket transfer");

    struct pico_socket *sock_found = (struct pico_socket *) data;
    struct pico_socket_tcp *tcp_sock_found =
            (struct pico_socket_tcp *) sock_found;
    struct pico_sockport *data_sp = pico_get_sockport(PICO_PROTO_TCP,
            sock_found->local_port);
    struct pico_frame *f = (struct pico_frame*) optional_data;
    log_debug("Found sockport in TCP data sockport tree%s","");
    if (!data_sp) {
        log_error("No such port %u", short_be(sock_found->local_port));
        return -1;
    }

    struct pico_socket *listen_sock = find_tcp_socket(data_sp, f);
    if (!listen_sock) {
        log_error("No listen sock in orig %s", "");
    }
    log_debug("Found listen sock in data transfer msu%s","");
    //tcp_sock_found would be the restored socket
    tcp_sock_found->sock.parent = listen_sock;
    tcp_sock_found->sock.wakeup = listen_sock->wakeup;

    pico_socket_add(&tcp_sock_found->sock);
    tcp_sock_found->retrans_tmr = 0;

    if (sock_found->parent && sock_found->parent->wakeup) {
        log_debug("FIRST ACK - Parent found -> listening socket %u",
                short_be(sock_found->local_port));
        sock_found->parent->wakeup(PICO_SOCK_EV_CONN, sock_found->parent);
    }

    return 0;
}
*/

int msu_tcp_process_queue_item(local_msu *msu, msu_queue_item *queue_item)
{
    //interface from runtime to picoTCP structures or any other existing code
    struct hs_internal_state *in_state = msu->internal_state;

    //process item
    if(queue_item){
        log_debug("----------------->>%s", "Processing HS MSU item handler");

        struct hs_queue_item *hs_item;
        hs_item = (struct hs_queue_item*)queue_item->buffer;

        msu_process_hs_request_in(msu, hs_item->reply_msu_id, hs_item->f);
        pico_frame_discard(hs_item->f);
        //free the queue item's memory that was processed.
        delete_generic_msu_queue_item(queue_item);
        log_debug("----------------->>%s", "Exiting HS MSU item handler");

    } else {
        //any to do when item is not but you need to do some internal stuff anyway
        pico_sockets_pending_ack_check(in_state); //for connection that only sent SYN
        pico_sockets_pending_restore_check(in_state); //for sockets pending restore
    }

    //check expired timers
    hs_check_timers(in_state->hs_timers);

    return 0;
}

void msu_tcp_handshake_destroy(struct generic_msu *self)
{
    log_debug("Destroying MSU with id: %u", self->id);
    struct hs_internal_state *in_state = self->internal_state;

    free(in_state->hs_table);
    log_debug("%s","Freed hs_table");

    free(in_state->hs_timers);
    log_debug("%s","Freed hs_timers");

    free(self->internal_state);
    log_debug("%s","Freed internal state");
}
struct generic_msu* msu_tcp_handshake_init(local_msu *handshake_msu, 
        struct create_msu_thread_msg_data *create_action)
{
    //handshake_msu->collect_state = msu_tcp_hs_collect_socket;
    //handshake_msu->restore = msu_tcp_hs_restore_socket;
    //handshake_msu->same_machine_move_state = msu_tcp_hs_same_thread_transfer;


    struct hs_internal_state *in_state = (struct hs_internal_state*)malloc(sizeof(struct hs_internal_state));
    if(!in_state){
        return -1;
    }
    struct pico_tree *hs_table = (struct pico_tree*) malloc(
            sizeof(struct pico_tree));
    if (!(hs_table)) {
        log_error("%s", "Failed to allocate internal struct pico_tree");
        free(in_state);
        return -1;
    }

    hs_table->root = &LEAF;
    hs_table->compare = sockport_cmp;

    /* Initialize timer heap */
    in_state->hs_timers = hs_heap_init();
    if (!in_state->hs_timers){
        log_error("%s", "Failed init timer heap");
        free(in_state);
        free(hs_table);
        return -1;
    }
    in_state->hs_table = hs_table;

    handshake_msu->internal_state = in_state;

    log_debug("Created %s MSU with id: %u", handshake_msu->type->name,
            handshake_msu->id);
    return 0;
}

msu_type TCP_HANDSHAKE_MSU_TYPE = {
    .name="tcp_handshake_msu",
    .layer=DEDOS_LAYER_TRANSPORT,
    .type_id=DEDOS_TCP_HANDSHAKE_MSU_ID,
    .proto_number=MSU_PROTO_TCP_HANDSHAKE,
    .init=msu_tcp_handshake_init,
    .destroy=msu_tcp_handshake_destroy,
    .receive=msu_tcp_process_queue_item,
    .receive_ctrl=NULL,
    .route=round_robin,
    .deserialize=msu_tcp_hs_restore_socket,
    .send_local=default_send_local,
    .send_remote=default_send_remote,
};


