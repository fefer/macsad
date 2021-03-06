// Copyright 2018 INTRIG/FEEC/UNICAMP (University of Campinas), Brazil
//
//Licensed under the Apache License, Version 2.0 (the "License");
//you may not use this file except in compliance with the License.
//You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//Unless required by applicable law or agreed to in writing, software
//distributed under the License is distributed on an "AS IS" BASIS,
//WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//See the License for the specific language governing permissions and
//limitations under the License.

#include <stdlib.h>
#include "controller.h"
#include "messages.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#define MAX_IPS 60000

controller c;

void fill_smac_table(uint8_t port, uint8_t mac[6])
{
	char buffer[2048]; /* TODO: ugly */
	struct p4_header* h;
	struct p4_add_table_entry* te;
	struct p4_action* a;
	struct p4_field_match_exact* exact;

	h = create_p4_header(buffer, 0, 2048);
	te = create_p4_add_table_entry(buffer,0,2048);
	strcpy(te->table_name, "smac");

	exact = add_p4_field_match_exact(te, 2048);
	strcpy(exact->header.name, "ethernet.srcAddr");
	memcpy(exact->bitmap, mac, 6);
	exact->length = 6*8+0;

	a = add_p4_action(h, 2048);
	strcpy(a->description.name, "_nop");

	netconv_p4_header(h);
	netconv_p4_add_table_entry(te);
	netconv_p4_field_match_exact(exact);
	netconv_p4_action(a);

	send_p4_msg(c, buffer, 2048);
}

void fill_dmac_table(uint8_t port, uint8_t mac[6])
{
    char buffer[2048];
    struct p4_header* h;
    struct p4_add_table_entry* te;
    struct p4_action* a;
    struct p4_action_parameter* ap;
    struct p4_field_match_exact* exact;

    h = create_p4_header(buffer, 0, 2048);
    te = create_p4_add_table_entry(buffer,0,2048);
    strcpy(te->table_name, "dmac");

    exact = add_p4_field_match_exact(te, 2048);
    strcpy(exact->header.name, "ethernet.dstAddr");
    memcpy(exact->bitmap, mac, 6);
    exact->length = 6*8+0;

    a = add_p4_action(h, 2048);
    strcpy(a->description.name, "forward");

    ap = add_p4_action_parameter(h, a, 2048);
    strcpy(ap->name, "port");
    memcpy(ap->bitmap, &port, 1);
    ap->length = 1*8+0;

    netconv_p4_header(h);
    netconv_p4_add_table_entry(te);
    netconv_p4_field_match_exact(exact);
    netconv_p4_action(a);
    netconv_p4_action_parameter(ap);

    send_p4_msg(c, buffer, 2048);
}

void mac_learn_digest(void* b) {
    uint8_t mac[6];
    uint8_t port[2];
    uint16_t offset=0;
    offset = sizeof(struct p4_digest);
    struct p4_digest_field* df = netconv_p4_digest_field(unpack_p4_digest_field(b, offset));
    memcpy(mac, df->value, 6);
    offset += sizeof(struct p4_digest_field);
    df = netconv_p4_digest_field(unpack_p4_digest_field(b, offset));
    memcpy(port, df->value, 2);

    uint8_t p = port[0];
    printf("Ctrl: mac_learn_digest PORT: %d MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", p, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    fill_dmac_table(p, mac);
    fill_smac_table(p, mac);
}

void dhf(void* b) {
    struct p4_header* h = netconv_p4_header(unpack_p4_header(b, 0));
    if (h->type != P4T_DIGEST) {
        printf("Method is not implemented\n");
        return;
    }

    struct p4_digest* d = unpack_p4_digest(b,0);
    if (strcmp(d->field_list_name, "mac_learn_digest")==0) {
        mac_learn_digest(b);
    } else {
        printf("Unknown digest received: X%sX\n", d->field_list_name);
    }
}

void set_default_action_smac()
{
	char buffer[2048];
	struct p4_header* h;
	struct p4_set_default_action* sda;
	struct p4_action* a;

	printf("Generate set_default_action message for table smac\n");

	h = create_p4_header(buffer, 0, sizeof(buffer));

	sda = create_p4_set_default_action(buffer,0,sizeof(buffer));
	strcpy(sda->table_name, "smac");

	a = &(sda->action);
	strcpy(a->description.name, "mac_learn");

	netconv_p4_header(h);
	netconv_p4_set_default_action(sda);
	netconv_p4_action(a);

	send_p4_msg(c, buffer, sizeof(buffer));
}

void set_default_action_dmac()
{
	char buffer[2048];
	struct p4_header* h;
	struct p4_set_default_action* sda;
	struct p4_action* a;

	printf("Generate set_default_action message for table dmac\n");

	h = create_p4_header(buffer, 0, sizeof(buffer));

	sda = create_p4_set_default_action(buffer,0,sizeof(buffer));
	strcpy(sda->table_name, "dmac");

	a = &(sda->action);
	strcpy(a->description.name, "broadcast");

	netconv_p4_header(h);
	netconv_p4_set_default_action(sda);
	netconv_p4_action(a);

	send_p4_msg(c, buffer, sizeof(buffer));
}

void set_default_action_dmac_classifier()
{
	char buffer[2048];
	struct p4_header* h;
	struct p4_set_default_action* sda;
	struct p4_action* a;
	struct p4_action_parameter* ap;
	uint8_t mode = 0; //L2FWD

	printf("Generate set_default_action message for table dmac_classifier\n");

	h = create_p4_header(buffer, 0, sizeof(buffer));

	sda = create_p4_set_default_action(buffer,0,sizeof(buffer));
	strcpy(sda->table_name, "dmac_classifier");

	a = &(sda->action);
	strcpy(a->description.name, "set_fwd_mode");
	ap = add_p4_action_parameter(h, a, 2048);
	strcpy(ap->name, "mode");
	memcpy(ap->bitmap, &mode, 1);
	ap->length = 1*8+0;


	netconv_p4_header(h);
	netconv_p4_set_default_action(sda);
	netconv_p4_action(a);
	netconv_p4_action_parameter(ap);

	send_p4_msg(c, buffer, sizeof(buffer));
}

void set_default_action_send_frame()
{
	char buffer[2048];
	struct p4_header* h;
	struct p4_set_default_action* sda;
	struct p4_action* a;

	printf("Generate set_default_action message for table send_frame\n");

	h = create_p4_header(buffer, 0, sizeof(buffer));

	sda = create_p4_set_default_action(buffer,0,sizeof(buffer));
	strcpy(sda->table_name, "send_frame");

	a = &(sda->action);
	strcpy(a->description.name, "_nop");

	netconv_p4_header(h);
	netconv_p4_set_default_action(sda);
	netconv_p4_action(a);

	send_p4_msg(c, buffer, sizeof(buffer));
}

void fill_ipv4_lpm_table(uint8_t ip[4], uint32_t nhgrp)
{
    char buffer[2048]; /* TODO: ugly */
    struct p4_header* h;
    struct p4_add_table_entry* te;
    struct p4_action* a;
    struct p4_action_parameter* ap;
    struct p4_field_match_lpm* lpm;

	printf("ipv4_lpm\n");
    h = create_p4_header(buffer, 0, 2048);
    te = create_p4_add_table_entry(buffer,0,2048);
    strcpy(te->table_name, "ipv4_lpm");

    lpm = add_p4_field_match_lpm(te, 2048);
    strcpy(lpm->header.name, "ipv4.dstAddr");
    memcpy(lpm->bitmap, ip, 4);
    lpm->prefix_length = 16;

    a = add_p4_action(h, 2048);
    strcpy(a->description.name, "set_nhop");

    printf("add nhgrp\n");
    ap = add_p4_action_parameter(h, a, 2048);
    strcpy(ap->name, "nhgroup");
    memcpy(ap->bitmap, &nhgrp, 4);
    ap->length = 4*8+0;

    printf("NH-1\n");
    netconv_p4_header(h);
    netconv_p4_add_table_entry(te);
    netconv_p4_field_match_lpm(lpm);
    netconv_p4_action(a);
    netconv_p4_action_parameter(ap);

    send_p4_msg(c, buffer, 2048);
}

void fill_ipv4_forward_table(uint32_t nhgroup, uint8_t dmac[6])
{
    char buffer[2048]; /* TODO: ugly */
    struct p4_header* h;
    struct p4_add_table_entry* te;
    struct p4_action* a;
    struct p4_action_parameter* ap;
    struct p4_field_match_exact* exact;

    printf("Fill table ipv4_forward...\n");

    h = create_p4_header(buffer, 0, 2048);
    te = create_p4_add_table_entry(buffer,0,2048);
    strcpy(te->table_name, "ipv4_forward");

    exact = add_p4_field_match_exact(te, 2048);
    strcpy(exact->header.name, "routing_metadata.nhgroup");
    memcpy(exact->bitmap, &nhgroup, 4);
    exact->length = 4*8+0;

    a = add_p4_action(h, 2048);
    strcpy(a->description.name, "l3forward");

    ap = add_p4_action_parameter(h, a, 2048);
    strcpy(ap->name, "dmac");
    memcpy(ap->bitmap, dmac, 6);
    ap->length = 6*8+0;
/*    ap2 = add_p4_action_parameter(h, a, 2048);
    strcpy(ap2->name, "smac");
    memcpy(ap2->bitmap, smac, 6);
    ap2->length = 6*8+0;

    ap3 = add_p4_action_parameter(h, a, 2048);
    strcpy(ap3->name, "port");
    ap3->bitmap[0] = port;
    ap3->bitmap[1] = 0;
    ap3->length = 2*8+0;
*/
    netconv_p4_header(h);
    netconv_p4_add_table_entry(te);
    netconv_p4_field_match_exact(exact);
    netconv_p4_action(a);
    netconv_p4_action_parameter(ap);
    //netconv_p4_action_parameter(ap2);
    //netconv_p4_action_parameter(ap3);

    send_p4_msg(c, buffer, 2048);
}

void fill_send_frame_table(uint8_t port, uint8_t smac[6])
{
    char buffer[2048]; /* TODO: ugly */
    struct p4_header* h;
    struct p4_add_table_entry* te;
    struct p4_action* a;
    struct p4_action_parameter* ap;
    struct p4_field_match_exact* exact;

    printf("Fill table send_frame...\n");

    h = create_p4_header(buffer, 0, 2048);
    te = create_p4_add_table_entry(buffer,0,2048);
    strcpy(te->table_name, "send_frame");

    exact = add_p4_field_match_exact(te, 2048);
    strcpy(exact->header.name, "standard_metadata.egress_port");
    memcpy(exact->bitmap, &port, 8);
    exact->length = 1*8+0;

    a = add_p4_action(h, 2048);
    strcpy(a->description.name, "rewrite_mac");

    ap = add_p4_action_parameter(h, a, 2048);
    strcpy(ap->name, "smac");
    memcpy(ap->bitmap, smac, 6);
    ap->length = 6*8+0;

    netconv_p4_header(h);
    netconv_p4_add_table_entry(te);
    netconv_p4_field_match_exact(exact);
    netconv_p4_action(a);
    netconv_p4_action_parameter(ap);

    send_p4_msg(c, buffer, 2048);
}

void fill_dmac_classifier_table(uint8_t dmac[6], int8_t mode)
{
    char buffer[2048]; /* TODO: ugly */
    struct p4_header* h;
    struct p4_add_table_entry* te;
    struct p4_action* a;
    struct p4_action_parameter* ap;
    struct p4_field_match_exact* exact;

    printf("Fill table dmac_classifier...\n");

    h = create_p4_header(buffer, 0, 2048);
    te = create_p4_add_table_entry(buffer,0,2048);
    strcpy(te->table_name, "dmac_classifier");

    exact = add_p4_field_match_exact(te, 2048);
    strcpy(exact->header.name, "ethernet.dstAddr");
    memcpy(exact->bitmap, dmac, 6);
    exact->length = 6*8+0;
    a = add_p4_action(h, 2048);
    strcpy(a->description.name, "set_fwd_mode");
    ap = add_p4_action_parameter(h, a, 2048);
    strcpy(ap->name, "mode");
    memcpy(ap->bitmap, &mode, 1);
    ap->length = 1*8+0;

    netconv_p4_header(h);
    netconv_p4_add_table_entry(te);
    netconv_p4_field_match_exact(exact);
    netconv_p4_action(a);
    netconv_p4_action_parameter(ap);

    send_p4_msg(c, buffer, 2048);
}

uint32_t ip_array[MAX_IPS];
uint8_t nhgrp_array[MAX_IPS];
int ip_count = -1;

int read_ip_prefixes_from_file(char *filename) {
	FILE *f;
	char line[100];
	int nhgrp = 0;

	f = fopen(filename, "r");
	if (f == NULL) return -1;

	while (fgets(line, sizeof(line), f)) {
		line[strlen(line)-1] = '\0';
		strtok(line,",");
		nhgrp = atoi(strtok(NULL,","));
		if (inet_pton(AF_INET, line, &(ip_array[++ip_count])) != 1) { /*TODO: check if it is in big-endian or not!!!*/
			printf("%s is not an ip address.\n",line);
			fclose(f);
			return -1;
		}
		nhgrp_array[ip_count] = nhgrp;
	}

	fclose(f);
	return 0;
}

void init_simple() {
	printf("Starting init_simple...\n");
    set_default_action_smac();
    set_default_action_dmac();
    set_default_action_dmac_classifier();
    set_default_action_send_frame();

    uint8_t ip[4] = {10,0,99,99};
    uint8_t mac[6] = {0xd2, 0x69, 0x0f, 0xa8, 0x39, 0x9c};
  //  uint8_t smac[6] = {0xd2, 0x69, 0x0f, 0x00, 0x00, 0x9c};
    uint8_t local_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    //uint8_t port = 1;
    uint32_t nhgrp = 0;

    fill_dmac_classifier_table(local_mac, 1);
    fill_send_frame_table(1, local_mac);
    fill_ipv4_lpm_table(ip, nhgrp);
    fill_ipv4_forward_table(nhgrp, mac);
 //   local_mac[5] = 0x02;
 //   fill_dmac_classifier_table(local_mac, 1);
    //fill_send_frame_table(2, local_mac);

}

void init_complex() {
	printf("Starting init_comple...\n");
    set_default_action_smac();
    set_default_action_dmac();
    set_default_action_dmac_classifier();
    set_default_action_send_frame();

    uint8_t mac[6] = {0xd2, 0x69, 0x0f, 0xa8, 0x39, 0x01};
//    uint8_t smac[6] = {0xd2, 0x69, 0x0f, 0x00, 0x00, 0x9c};
    uint8_t local_mac[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    //uint8_t port = 1;
    //uint32_t nhgrp = 0;
    int i=0;

    for (;i<=ip_count;++i) {
   	/* Delegate routing table entries */
	fill_ipv4_lpm_table((uint8_t*)&(ip_array[i]), nhgrp_array[i]);
    }

    fill_ipv4_forward_table(0, mac);
    mac[5] = 0x02;
    fill_ipv4_forward_table(1, mac);
    fill_dmac_classifier_table(local_mac, 1);
    fill_send_frame_table(1, local_mac);
    local_mac[5] = 0x02;
    fill_dmac_classifier_table(local_mac, 1);
    fill_send_frame_table(2, local_mac);

}

int main(int argc, char* argv[])
{
	printf("Create and configure controller...\n");

	if (argc>1) {
		if (argc!=2) {
			printf("Too many arguments...\nUsage: %s <filename(optional)>\n", argv[0]);
			return -1;
		}
		printf("Command line argument is present...\nLoading configuration data...\n");
		if (read_ip_prefixes_from_file(argv[1])<0) {
			printf("File cannnot be opened...\n");
			return -1;
		}
		c = create_controller_with_init(11111, 3, dhf, init_complex);
	}
	else {
	    printf("inside if Starting init_simple...\n");
		c = create_controller_with_init(11111, 3, dhf, init_simple);
	}

	printf("MACSAD controller started...\n");
	execute_controller(c);

	printf("MACSAD controller terminated\n");
	destroy_controller(c);

	return 0;
}
