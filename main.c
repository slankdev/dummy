
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

static volatile bool force_quit;
static uint16_t nb_rxd = 128;
static uint16_t nb_txd = 512;
#define RTE_LOGTYPE_L2FWD RTE_LOGTYPE_USER1
#define NB_MBUF   8192
#define MAX_PKT_BURST 32
#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */
#define MEMPOOL_CACHE_SIZE 256

static const struct rte_eth_conf port_conf = {
	.rxmode = {
		.max_rx_pkt_len = 0x2600,
		.jumbo_frame    = 1,
		.hw_strip_crc   = 1,
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

struct rte_mempool * l2fwd_pktmbuf_pool = NULL;

/* main processing loop */
static int
l2fwd_main_loop(__attribute__((unused)) void *dummy)
{
  printf("starting %s() on lcore%u \n", __func__, rte_lcore_id());

	while (!force_quit) {

		/*
		 * Read packet from RX queues
		 */
    const uint32_t n_port = rte_eth_dev_count();
		for (uint32_t portid = 0; portid < n_port; portid++) {

      struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
			uint32_t nb_rx = rte_eth_rx_burst(portid, 0,
						 pkts_burst, MAX_PKT_BURST);
			for (uint32_t j = 0; j < nb_rx; j++) {
        struct rte_mbuf *m = pkts_burst[j];
				printf("fwd %u -> %u \n", portid, portid^1);
        int ret = rte_eth_tx_burst(portid^1, 0, &m, 1);
        if (ret != 1) rte_pktmbuf_free(m);
			}

		}
	}
  return 0;
}


static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		force_quit = true;
	}
}

int
main(int argc, char **argv)
{
	int ret;
	uint16_t portid;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	l2fwd_pktmbuf_pool = rte_pktmbuf_pool_create(
      "mbuf_pool", NB_MBUF, MEMPOOL_CACHE_SIZE, 0,
      RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (l2fwd_pktmbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");

	uint16_t nb_ports;
	nb_ports = rte_eth_dev_count();
	if (nb_ports != 2)
		rte_exit(EXIT_FAILURE, "Number of Ethernet ports isn't 2. - bye\n");

	/* Initialise each port */
	for (portid = 0; portid < nb_ports; portid++) {

		/* init port */
		printf("Initializing port %u... ", portid);
		fflush(stdout);
		ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
          "Cannot configure device: err=%d, port=%u\n",
				  ret, portid);

		ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd, &nb_txd);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, port=%u\n",
				 ret, portid);

		/* init one RX queue */
		fflush(stdout);
		ret = rte_eth_rx_queue_setup(portid, 0, nb_rxd,
             rte_eth_dev_socket_id(portid), NULL,
             l2fwd_pktmbuf_pool);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
				  ret, portid);

		/* init one TX queue on each port */
		fflush(stdout);
		ret = rte_eth_tx_queue_setup(portid, 0, nb_txd,
				rte_eth_dev_socket_id(portid), NULL);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
				ret, portid);

		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
				  ret, portid);

		printf("done: \n");
		rte_eth_promiscuous_enable(portid);
	}

	ret = 0;
	/* launch per-lcore init on every lcore */
	rte_eal_remote_launch(l2fwd_main_loop, NULL, 1);
  rte_eal_mp_wait_lcore();

	for (portid = 0; portid < nb_ports; portid++) {
		printf("Closing port %d...", portid);
		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
		printf(" Done\n");
	}
	printf("Bye...\n");
	return ret;
}
