#include <arpa/inet.h>
#include <boost/optional.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <cassert>
#include <cstring>
#include <dmtr/annot.h>
#include <dmtr/libos.h>
#include <dmtr/wait.h>
#include <iostream>
#include <libos/common/mem.h>
#include <netinet/in.h>
#include <yaml-cpp/yaml.h>

#include "common.hh"

#define USE_CONNECT 1
#define FILL_CHAR 'a'

namespace po = boost::program_options;

int main(int argc, char *argv[])
{
    parse_args(argc, argv, false);

    DMTR_OK(dmtr_init(dmtr_argc, dmtr_argv));

    dmtr_timer_t *timer = NULL;
    DMTR_OK(dmtr_new_timer(&timer, "end-to-end"));

    int qd = 0;
    DMTR_OK(dmtr_socket(&qd, AF_INET, SOCK_DGRAM, 0));
    printf("client qd:\t%d\n", qd);

    struct sockaddr_in saddr = {};
    saddr.sin_family = AF_INET;
    const char *server_ip = boost::get(server_ip_addr).c_str();
    if (inet_pton(AF_INET, server_ip, &saddr.sin_addr) != 1) {
        std::cerr << "Unable to parse IP address." << std::endl;
        return -1;
    }
    saddr.sin_port = htons(port);

    dmtr_sgarray_t sga = {};
    sga.sga_numsegs = 1;
    sga.sga_segs[0].sgaseg_len = packet_size;
    sga.sga_segs[0].sgaseg_buf = generate_packet();

#if USE_CONNECT
    std::cerr << "Attempting to connect to `" << server_ip_addr << ":" << port << "`..." << std::endl;
    DMTR_OK(dmtr_connect(qd, reinterpret_cast<struct sockaddr *>(&saddr), sizeof(saddr)));
#else
    sga.sga_addr = saddr;
    sga.sga_addrlen = sizeof(saddr);
#endif

    for (size_t i = 0; i < iterations; i++) {
        dmtr_qtoken_t qt;
        DMTR_OK(dmtr_start_timer(timer));
        DMTR_OK(dmtr_push(&qt, qd, &sga));
        DMTR_OK(dmtr_wait(NULL, qt));
        //fprintf(stderr, "send complete.\n");

        dmtr_qresult_t qr = {};
        DMTR_OK(dmtr_pop(&qt, qd));
        DMTR_OK(dmtr_wait(&qr, qt));
        DMTR_OK(dmtr_stop_timer(timer));
        assert(DMTR_OPC_POP == qr.qr_opcode);
        assert(qr.qr_value.sga.sga_numsegs == 1);
        assert(reinterpret_cast<uint8_t *>(qr.qr_value.sga.sga_segs[0].sgaseg_buf)[0] == FILL_CHAR);

        //fprintf(stderr, "[%lu] client: rcvd\t%s\tbuf size:\t%d\n", i, reinterpret_cast<char *>(qr.qr_value.sga.sga_segs[0].sgaseg_buf), qr.qr_value.sga.sga_segs[0].sgaseg_len);
        free(qr.qr_value.sga.sga_buf);
    }

    DMTR_OK(dmtr_dump_timer(stderr, timer));
    DMTR_OK(dmtr_close(qd));

    return 0;
}