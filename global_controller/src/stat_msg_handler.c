#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "scheduling.h"
#include "controller_tools.h"
#include "stat_msg_handler.h"
#include "communication.h"
#include "control_protocol.h"
#include "dfg.h"

#define NEXT_MSU_LOCAL 1
#define NEXT_MSU_REMOTE 2

static void send_route_update(char *input, int action) {
    char *cmd = &(*input);
    int from_msu_id, to_msu_id, runtime_sock, from_msu_type, to_msu_type, to_msu_locality;
    char *ip_str;
    long total_msg_size = 0;
    int to_ip = 0;
    int ret;

    debug("DEBUG: Route update *input: %s", input);
    debug("DEBUG: Route update cmd : %s", cmd);
    debug("DEBUG: Route update action: %u", action);

    runtime_sock = atoi(strtok(cmd, " "));
    from_msu_id  = atoi(strtok(NULL, " "));
    from_msu_type = atoi(strtok(NULL, " "));
    to_msu_id = atoi(strtok(NULL, " "));
    to_msu_type = atoi(strtok(NULL, " "));
    to_msu_locality = atoi(strtok(NULL, " "));

    if (to_msu_locality == NEXT_MSU_REMOTE) {
        ip_str = strtok(NULL, "\r\n");
        debug("ip_str :%s", ip_str);
        string_to_ipv4(ip_str, &to_ip);
    }

    ret = update_route(action, runtime_sock, from_msu_id, from_msu_type,
                       to_msu_id, to_msu_type, to_msu_locality, to_ip);

    if (ret < 0 ) {
        debug("ERROR: %s", "Could not process update route request");
    }
}

void process_stats_msg(struct msu_stats_data *stats_data, int runtime_sock) {
    //TODO: add specific stat report message types and code
    struct msu_stats_data *stats = stats_data;

    /*
    debug("DEBUG: %s", "processing stat messages");
    debug("DEBUG: %s: %d", "payload.msu_id", stats->msu_id);
    debug("DEBUG: %s: %u", "payload.item_processed", stats->queue_item_processed);
    debug("DEBUG: %s: %u", "payload.memory_allocated", stats->memory_allocated);
    debug("DEBUG: %s: %u", "payload.data_queue_size", stats->data_queue_size);
    */

    /**
     * TODO:
     * check memory consumption (check with requirements for the MSU, stored in JSON)
     * trigger remote cloning
     */

    struct dfg_config *dfg_config_g = NULL;
    dfg_config_g = get_dfg();

    if (dfg_config_g == NULL) {
        debug("ERROR: %s", "could not retrieve dfg");
        return;
    }

    if (stats->data_queue_size > 5) {
        /**
         * First create the new msu
         */
        char data[32];
        memset(data, '\0', sizeof(data));
        struct dfg_vertex *new_msu = (struct dfg_vertex *) malloc (sizeof(struct dfg_vertex));
        //TODO: check whether this MSU is legit
        pthread_mutex_lock(dfg_config_g->dfg_mutex);
        struct dfg_vertex *msu = dfg_config_g->vertices[stats->msu_id - 1];

        int target_runtime_sock, target_thread_id, ret;
        ret = find_placement(&target_runtime_sock, &target_thread_id);

        if (ret == -1) {
            debug("ERROR: %s", "could not find runtime or thread to place new clone");
            return;
        }

        /* Check if we need to spawn a new worker thread */
        //right now we assume the new thread doesn't exist.
        create_worker_thread(target_runtime_sock);
        sleep(2);


        /* Deep copy of msu to be cloned and update specific fields */
        memcpy(new_msu, msu, sizeof(*msu));

        new_msu->msu_id = dfg_config_g->vertex_cnt + 1;
        new_msu->thread_id = target_thread_id;
        /* clone routes counts will be incremented when actually created */
        new_msu->num_dst = 0;
        new_msu->num_src = 0;
        /* clean up target and destination pointers */
        int r;
        for (r = 0; r < msu->num_dst; r++) {
            new_msu->msu_dst_list[r] = NULL;
        }
        for (r = 0; r < msu->num_src; r++) {
            new_msu->msu_src_list[r] = NULL;
        }

        pthread_mutex_unlock(dfg_config_g->dfg_mutex);

        snprintf(data, strlen(new_msu->msu_mode) + 1 + how_many_digits(new_msu->thread_id) + 1,
                 "%s %d", new_msu->msu_mode, new_msu->thread_id);

        add_msu(&data, new_msu, target_runtime_sock);

        /**
         * Then update downstream and upstream routes
         */
        int action;
        action = MSU_ROUTE_ADD;

        //Add route to downstream
        //TODO: see todo in dfg.c (handle the holes in the list due to MSU_ROUTE_DEL)
        debug("DEBUG: %s", "updating routes for newly cloned msu");
        struct dedos_control_msg update_route_downstream;
        char cmd[128];
        int len, base_len;
        //TODO: determine locality on a per destination basis
        int locality = 1;

        /**
         * 1 char for '\0', 5 for spaces
         */
        base_len = 1 + 5;
        base_len = base_len + how_many_digits(new_msu->msu_id);
        base_len = base_len + how_many_digits(new_msu->msu_type);

        int dst;
        for (dst = 0; dst < msu->num_dst; dst++) {
            memset(cmd, '\0', sizeof(cmd));
            len = base_len;
            len = len + how_many_digits(new_msu->msu_runtime->sock);
            len = len + how_many_digits(msu->msu_dst_list[dst]->msu_id);
            len = len + how_many_digits(msu->msu_dst_list[dst]->msu_type);
            len = len + how_many_digits(locality);

            snprintf(cmd, len, "%d %d %d %d %d %d",
                     new_msu->msu_runtime->sock,
                     new_msu->msu_id,
                     new_msu->msu_type,
                     msu->msu_dst_list[dst]->msu_id,
                     msu->msu_dst_list[dst]->msu_type,
                     locality);

            sleep(2);
            send_route_update(cmd, action);
        }

        int src;
        for (src = 0; src < msu->num_src; src++) {
            memset(cmd, '\0', sizeof(cmd));
            len = base_len;
            len = len + how_many_digits(msu->msu_src_list[src]->msu_runtime->sock);
            len = len + how_many_digits(msu->msu_src_list[src]->msu_id);
            len = len + how_many_digits(msu->msu_src_list[src]->msu_type);
            len = len + how_many_digits(locality);

            snprintf(cmd, len, "%d %d %d %d %d %d",
                     msu->msu_src_list[src]->msu_runtime->sock,
                     msu->msu_src_list[src]->msu_id,
                     msu->msu_src_list[src]->msu_type,
                     new_msu->msu_id,
                     new_msu->msu_type,
                     locality);

            sleep(2);
            send_route_update(cmd, action);
        }
    }
}
