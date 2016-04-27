#include <odp_api.h>
#include "odp_lib.h"
#include <odp/api/packet.h>
#include <odp/helper/linux.h>
#include <odp/helper/eth.h>
#include <odp/helper/ip.h>

//extern void p4_handle_packet(packet* p, unsigned portid);

uint32_t enabled_port_mask = 0;
struct ether_addr ports_eth_addr[MAX_ETHPORTS];

/* A tsc-based timer responsible for triggering statistics printout */
#define TIMER_MILLISECOND 2000000ULL /* around 1ms at 2 Ghz */
#define MAX_TIMER_PERIOD  86400 /* 1 day max */
int64_t timer_period = 10 * TIMER_MILLISECOND * 1000; /* default period is 10 seconds */

#define MAX_PORTS 16

#define MCAST_CLONE_PORTS       2
#define MCAST_CLONE_SEGS        2

#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512
uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

#define PKT_MBUF_DATA_SIZE      RTE_MBUF_DEFAULT_BUF_SIZE
#define NB_PKT_MBUF				8192

#define HDR_MBUF_DATA_SIZE      (2 * RTE_PKTMBUF_HEADROOM)
#define NB_HDR_MBUF				(NB_PKT_MBUF * MAX_PORTS)

#define NB_CLONE_MBUF		    (NB_PKT_MBUF * MCAST_CLONE_PORTS * MCAST_CLONE_SEGS * 2)

#define BURST_TX_DRAIN_US		100 /* TX drain every ~100us */

// note: this much space MUST be able to hold all deparsed content
#define DEPARSE_BUFFER_SIZE		1024

//=============================================================================

/* Send burst of packets on an output interface */
static inline int
send_burst(struct lcore_conf *qconf, uint16_t n, uint8_t port)
{
/*
	struct rte_mbuf **m_table;
	int ret;
	uint16_t queueid;

	queueid = qconf->tx_queue_id[port];
	m_table = (struct rte_mbuf **)qconf->tx_mbufs[port].m_table;

	ret = rte_eth_tx_burst(port, queueid, m_table, n);
	if (unlikely(ret < n)) {
		do {
			rte_pktmbuf_free(m_table[ret]);
		} while (++ret < n);
	}
*/
	return 0;
}

/* Send burst of outgoing packet, if timeout expires. */
static inline void
send_timeout_burst(struct lcore_conf *qconf)
{
/*
       uint64_t cur_tsc;
        uint8_t portid;
        const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;

        cur_tsc = rte_rdtsc();
        if (likely (cur_tsc < qconf->tx_tsc + drain_tsc))
                return;

        for (portid = 0; portid < MAX_PORTS; portid++) {
                if (qconf->tx_mbufs[portid].len != 0) {
                        send_burst(qconf, qconf->tx_mbufs[portid].len, portid);
			qconf->tx_mbufs[portid].len = 0;
		}
        }
        qconf->tx_tsc = cur_tsc;
*/
}

static int
get_socketid(unsigned lcore_id)
{
/*
    if (numa_on)
        return rte_lcore_to_socket_id(lcore_id);
    else
        return 0;
*/
return 0;
}

static inline void
dbg_print_headers(packet_descriptor_t* pd)
{
    for (int i = 0; i < HEADER_INSTANCE_COUNT; ++i) {
        debug("    :: header %d (type=%d, len=%d) = ", i, pd->headers[i].type, pd->headers[i].length);
        for (int j = 0; j < pd->headers[i].length; ++j) {
            debug("%02x ", ((uint8_t*)(pd->headers[i].pointer))[j]);
        }
        debug("\n");
    }
}

static inline unsigned
deparse_headers(packet_descriptor_t* pd, int socketid)
{
/*
	uint8_t* deparse_buffer = (uint8_t*)rte_pktmbuf_append(deparse_mbuf, DEPARSE_BUFFER_SIZE);
	int len = 0;
    for (int i = 0; i < HEADER_INSTANCE_COUNT; ++i) {
    	uint8_t* hdr_ptr = (uint8_t*)(pd->headers[i].pointer);
    	unsigned hdr_len = pd->headers[i].length;
        for (int j = 0; j < hdr_len; ++j) {
            *deparse_buffer = *hdr_ptr;
            ++deparse_buffer;
            ++hdr_ptr;
        }
        len += hdr_len;
    }
    return len;
*/
    return -1;
}

/* Get number of bits set. */
static inline uint32_t bitcnt(uint32_t v)
{
	uint32_t n;

	for (n = 0; v != 0; v &= v - 1, n++)
		;

	return (n);
}

/* send one pkt each time */
static void odp_send_packet(odp_packet_t *p, uint8_t port)
{
	int sent;

	sent = odp_pktout_send(gconf->pktios[port].pktout[0], p, 1);
	if (sent < 0)
	{
		printf("pkt sent failed \n");
		sent = 0;
	}
}

#define EXTRACT_EGRESSPORT(p) (*(uint32_t *)(((uint8_t*)(p)->headers[/*header instance id - hopefully it's the very first one*/0].pointer)+/*byteoffset*/6) & /*mask*/0x7fc) >> /*bitoffset*/2
#define EXTRACT_INGRESSPORT(p) (*(uint32_t *)(((uint8_t*)(p)->headers[/*header instance id - hopefully it's the very first one*/0].pointer)+/*byteoffset*/0) & /*mask*/0x1ff) >> /*bitoffset*/0

static void maco_bcast_packet(packet_descriptor_t* pd, uint8_t ingress_port)
{
	appl_args_t *appl = &gconf->appl;
	uint32_t total_ports= appl->if_count;
	int port;

	printf("Broadcast: ingress port %d, total no of ports %d\n",
			ingress_port, total_ports);

	for (port = 0;port < total_ports;port++){
		if (port != ingress_port){
			printf("Broadcasting on o/p port id %d\n", port);
			odp_send_packet((odp_packet_t *)pd->packet, port);
		}
	}

	return;
}

/* Enqueue a single packet, and send burst if queue is filled */
static inline int send_packet(packet_descriptor_t* pd, int thr_idx)
{
	macs_conf_t *mconf;
	int port = EXTRACT_EGRESSPORT(pd);
	int inport = EXTRACT_INGRESSPORT(pd);

	mconf = &gconf->mconf[thr_idx];

	printf("  :::: EGRESSING\n");
	dbg_print_headers(pd);
	printf("    :: deparsing headers\n");
	printf("    :: sendPacket called\n");
	printf("ingress port is %d and egress port is %d \n", inport, port);

	if (port==100) {
		maco_bcast_packet(pd, inport);
	} else {
		/* o/p queue, pkt, no. of pkt to send */
		odp_send_packet((odp_packet_t *)pd->packet, port);
	}

	/* TODO write code to properly free the packet */
//	odp_packet_free ((struct odp_packet_t *)pd->packet);

	return 0;
}

static void init_metadata(packet_descriptor_t* packet_desc, uint32_t inport)
{
	packet_desc->headers[header_instance_standard_metadata] =
		(header_descriptor_t) {
			.type = header_instance_standard_metadata,
			.length = header_instance_byte_width[header_instance_standard_metadata],
			.pointer = calloc(header_instance_byte_width[header_instance_standard_metadata], sizeof(uint8_t))
		};

	int res32; // needs for the macro
	MODIFY_INT32_INT32(packet_desc, field_instance_standard_metadata_ingress_port, inport); // fix? LAKI
}

void packet_received(packet_descriptor_t *pd, odp_packet_t *p, unsigned portid, int thr_idx)
{
	printf(":::: EXECUTING packet recieved\n");
	pd->pointer = (uint8_t *)odp_packet_data(*p);
	pd->packet = (packet *)p;

//	set_metadata_inport(pd, portid);
	init_metadata(pd, portid);
	struct lcore_state *state_tmp = &gconf->state;
	handle_packet(pd, state_tmp->tables);
//	handle_packet(pd, &state->tables);
	send_packet(pd, thr_idx);
}

void odp_main_worker (void *arg)
{
	int pkts, i;
	int portid;
	//	struct macs_conf *macs = &mconf_list[0];;
	int thr;
	//thread_args_t *thr_args;
	macs_conf_t *thr_args;
	odp_pktio_t pktio;
	odp_pktin_queue_t pktin;
	odp_pktout_queue_t pktout;
	int pkts_ok;
	odp_packet_t pkt_tbl[MAX_PKT_BURST];
	unsigned long pkt_cnt = 0;
	unsigned long err_cnt = 0;
	unsigned long tmp = 0;
	int if_idx;
	packet_descriptor_t pd;

//	init_dataplane(&pd, gconf->state.tables);

	printf(":: INSIDE odp_main_worker\n");
	thr = odp_thread_id();
	printf("	the thread id is %d\n",thr);
	thr_args = arg;

	pktio = odp_pktio_lookup(thr_args->pktio_dev);
	if_idx = odp_dev_name_to_id (thr_args->pktio_dev);

	if (gconf->pktios[if_idx].pktio == ODP_PKTIO_INVALID) {
		printf("  [%02i] Error: lookup of pktio %s failed\n",
				thr, thr_args->pktio_dev);
		return;
	}
	if (pktio == ODP_PKTIO_INVALID) {
		printf("  [%02i] Error: lookup of pktio for if %s failed\n",
				thr, thr_args->pktio_dev);
		return;
	}
	printf("  [%02i] looked up pktio:%02" PRIu64 ", burst mode\n",
			thr, odp_pktio_to_u64(pktio));

	if (odp_pktin_queue(pktio, &gconf->pktios[if_idx].pktin[0], 1) != 1) {
		printf("  [%02i] Error: no pktin queue\n", thr);
		return;
	}
	if (odp_pktout_queue(pktio, &gconf->pktios[if_idx].pktout[0], 1) != 1) {
		printf("  [%02i] Error: no pktout queue\n", thr);
		return;
	}

	/* Currently Only one interface supported per thread.
	 * Hence only one pktio[0] */
	thr_args->pktio[if_idx].pktout = pktout;

	/* Loop packets */
	for (;;) {
		pkts = odp_pktin_recv(gconf->pktios[if_idx].pktin[0], pkt_tbl, MAX_PKT_BURST);
		if (pkts > 0) {
			for (i = 0; i < pkts; i++) {
				odp_packet_t pkt = pkt_tbl[i];
				packet_received(&pd, &pkt, if_idx, thr_args->thr_idx);
			}

			/* Print packet counts every once in a while */
			tmp += pkts_ok;
			if (odp_unlikely((tmp >= 100000) || /* OR first print:*/
						((pkt_cnt == 0) && ((tmp-1) < MAX_PKT_BURST)))) {
				pkt_cnt += tmp;
				printf("  [%02i] pkt_cnt:%lu\n", thr, pkt_cnt);
				fflush(NULL);
				tmp = 0;
			}
		}
	}

	return;
}
