/**
 * regex_msu.c
 *
 * MSU for processing regular expressions
 */
#include "runtime.h"
#include "modules/regex_msu.h"
#include "communication.h"
#include "routing.h"
#include "dedos_msu_msg_type.h"
#include "dedos_msu_list.h"
#include "dedos_thread_queue.h" //for enqueuing outgoing control messages
#include "control_protocol.h"
#include "logging.h"
#include "modules/ssl_msu.h"

#include <pcre.h>
#define REGEX_KEY "regex="
#define EVIL_REGEX "^(a+)+$"
#define HTML "\
<!DOCTYPE html>\n\
<html>\n\
    <body>\n\
        <h1>Does %s match %s?</h1> <br/>\n\
        <p>%s.</p>\n\
    </body>\n\
</html>\
"

/** Maximum length of the HTML to be returned */
int html_len() {
    return strlen(HTML) + 200;
}

/**
 * Recieves data for the Regex MSU
 * @param self Regex MSU to receive the data
 * @param input_data contains a regex_data_payload* in input_data->buffer
 * @return ID of next MSU-type to receive data, or -1 on error
 */
int regex_receive(local_msu *self, msu_queue_item *input_data) {
    if (self && input_data) {
        struct regex_data_payload *regex_data = (struct regex_data_payload *) (input_data->buffer);
        int ret;

        if (regex_data->dst_type == DEDOS_SSL_WRITE_MSU_ID) {

            char http_ok[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length:";
            char *html = (char *) malloc(sizeof(char) * html_len());
            html[0] = '\0';

            const char *pcreErrorStr;
            int errOffset;
            pcre *reCompiled = pcre_compile(EVIL_REGEX, 0, &pcreErrorStr, &errOffset, NULL);

            pcre_extra pcreExtra;
            pcreExtra.match_limit = -1;
            pcreExtra.match_limit_recursion = -1;
            pcreExtra.flags = PCRE_EXTRA_MATCH_LIMIT | PCRE_EXTRA_MATCH_LIMIT_RECURSION;

            int len = strlen(regex_data->string);
            int x[1];

            ret = pcre_exec(reCompiled, &pcreExtra, regex_data->string, len, 0, 0, x, 1);

            char resp[5];
            if (ret < 0) {
                sprintf(resp, "%s", "NO");
            }
            else {
                sprintf(resp, "%s", "YES");
            }

            sprintf(html, HTML, regex_data->string, EVIL_REGEX, resp);

            int http_resp_len = strlen(http_ok) + strlen(html) + 20;

            //FIXME: useless double copy: go straight for ssl_data->msg
            char *http_response = (char *) malloc(http_resp_len);
            snprintf(http_response, http_resp_len, "%s %d\r\n\r\n%s",
                     http_ok,
                     (int) strlen(html),
                     html);

            struct ssl_data_payload *ssl_data = (struct ssl_data_payload *) regex_data->dst_packet;
            ssl_data->type = WRITE;
            snprintf(ssl_data->msg, strlen(http_response)+1, http_response);
            free(http_response);
            free(html);

            input_data->buffer = ssl_data;
            log_debug("Returning input data->buffer = %s", ssl_data->msg);
            input_data->buffer_len = sizeof(struct ssl_data_payload);

            free(regex_data);

            return DEDOS_SSL_WRITE_MSU_ID;
        }
    }
    return -1;
}

/**
 * All regex MSUs contain a reference to this type
 */
const msu_type REGEX_MSU_TYPE = {
    .name="regex_msu",
    .layer=DEDOS_LAYER_APPLICATION,
    .type_id=DEDOS_REGEX_MSU_ID,
    .proto_number=MSU_PROTO_REGEX,
    .init=NULL,
    .destroy=NULL,
    .receive=regex_receive,
    .receive_ctrl=NULL,
    .route=round_robin,
    .deserialize=default_deserialize,
    .send_local=default_send_local,
    .send_remote=default_send_remote,
};
