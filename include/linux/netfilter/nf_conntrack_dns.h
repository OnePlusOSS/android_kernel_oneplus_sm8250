/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _SOP_NF_CONNTRACK_DNS_H
#define _SOP_NF_CONNTRACK_DNS_H

#define DNS_PORT 53
#define	DNS_RECORD_TYPE				2
#define	DNS_RECORD_CLASS			2
#define	DNS_RECORD_TYPE_AND_CLASS		(DNS_RECORD_TYPE + DNS_RECORD_CLASS)
#define	DNS_RECORD_MIN				(sizeof("A") + DNS_RECORD_TYPE_AND_CLASS)

struct nf_ct_dns {
	u8 usage;
	char query[0];
};

struct dnshdr {
	__be16 query_id;
	__be16 flags;
	__be16 question_count;
	__be16 answer_count;
	__be16 authority_count;
	__be16 additional_record_count;
	char query[0];
};

#endif /* _SOP_NF_CONNTRACK_DNS_H */
