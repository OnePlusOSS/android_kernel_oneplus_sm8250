/* op_freezer.c
 *
 * add for oneplus freeze manager
 *
 * Copyright (C) 2007-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <oneplus/op_freezer/op_freezer.h>

#define NETLINK_PORT_OP_FREEZER        (0x15356)

static struct sock *sock_handle = NULL;
static atomic_t op_freezer_deamon_port;

/*
 * netlink report function to tell freezer native deamon unfreeze process info
 * if the parameters is empty, fill it with (pid/uid with -1)
 */
int op_freezer_report(enum message_type type, int caller_pid, int target_uid, const char *rpc_name, int code)
{
	int len = 0;
	int ret = 0;
	struct op_freezer_message *data = NULL;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;

	if (atomic_read(&op_freezer_deamon_port) == -1) {
		pr_err("%s: op_freezer_deamon_port invalid!\n", __func__);
                return OP_FREEZER_ERROR;
	}

	if (sock_handle == NULL) {
		pr_err("%s: sock_handle invalid!\n", __func__);
                return OP_FREEZER_ERROR;
	}

	if (type >= TYPE_MAX) {
		pr_err("%s: type = %d invalid!\n", __func__, type);
		return OP_FREEZER_ERROR;
	}

	len = sizeof(struct op_freezer_message);
	skb = nlmsg_new(len, GFP_ATOMIC);
	if (skb == NULL) {
		pr_err("%s: type =%d, nlmsg_new failed!\n", __func__, type);
		return OP_FREEZER_ERROR;
	}

	nlh = nlmsg_put(skb, 0, 0, 0, len, 0);
	if (nlh == NULL) {
		pr_err("%s: type =%d, nlmsg_put failed!\n", __func__, type);
		kfree_skb(skb);
		return OP_FREEZER_ERROR;
	}

	data = nlmsg_data(nlh);
	if(data == NULL) {
		pr_err("%s: type =%d, nlmsg_data failed!\n", __func__, type);
		return OP_FREEZER_ERROR;
	}
	data->type = type;
	data->port = NETLINK_PORT_OP_FREEZER;
	data->caller_pid = caller_pid;
	data->target_uid = target_uid;
	data->pkg_cmd = -1; //invalid package cmd
	data->code = code;
	strlcpy(data->rpc_name, rpc_name, INTERFACETOKEN_BUFF_SIZE);
	nlmsg_end(skb, nlh);

	if ((ret = nlmsg_unicast(sock_handle, skb, (u32)atomic_read(&op_freezer_deamon_port))) < 0) {
		pr_err("%s: nlmsg_unicast failed! err = %d\n", __func__ , ret);
		return OP_FREEZER_ERROR;
	}

	return OP_FREEZER_NOERROR;
}

// op_freezer kernel module handle the message from freezer native deamon
static void op_freezer_handler(struct sk_buff *skb)
{
	struct op_freezer_message *data = NULL;
	struct nlmsghdr *nlh = NULL;
	unsigned int len  = 0;

	if (!skb) {
		pr_err("%s: recv skb NULL!\n", __func__);
		return;
	}

	if (skb->len >= NLMSG_SPACE(0)) {
		nlh = nlmsg_hdr(skb);
		len = NLMSG_PAYLOAD(nlh, 0);
		data = (struct op_freezer_message *)NLMSG_DATA(nlh);

		if (len < sizeof (struct op_freezer_message)) {
			pr_err("%s: op_freezer_message len check faied! len = %d  min_expected_len = %lu!\n", __func__, len, sizeof(struct op_freezer_message));
			return;
		}

		if (data->port < 0) {
			pr_err("%s: portid = %d invalid!\n", __func__, data->port);
			return;
		}
		if (data->type >= TYPE_MAX) {
			pr_err("%s: type = %d invalid!\n", __func__, data->type);
			return;
		}
		if (atomic_read(&op_freezer_deamon_port) == -1 && data->type != LOOP_BACK) {
			pr_err("%s: handshake not setup, type = %d!\n", __func__, data->type);
                        return;
		}

		switch (data->type) {
			case LOOP_BACK:  /*Loop back message, only for native deamon and kernel handshake*/
				atomic_set(&op_freezer_deamon_port, data->port);
				op_freezer_report(LOOP_BACK, -1, -1, "loop back", -1);
				printk(KERN_ERR "%s: --> LOOP_BACK, port = %d\n", __func__, data->port);
				break;
			case PKG:
				printk(KERN_ERR "%s: --> PKG, uid = %d, pkg_cmd = %d\n", __func__, data->target_uid, data->pkg_cmd);
				op_freezer_network_cmd_parse(data->target_uid, data->pkg_cmd);
				break;
			case FROZEN_TRANS:
				printk(KERN_ERR "%s: --> FROZEN_TRANS, uid = %d\n", __func__, data->target_uid);
				op_freezer_check_frozen_transcation(data->target_uid);
				break;

			default:
				pr_err("%s: op_freezer_messag type invalid %d\n", __func__, data->type);
				break;
		}
	}
}

static int __init op_freezer_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = op_freezer_handler,
	};

	atomic_set(&op_freezer_deamon_port, -1);

	sock_handle = netlink_kernel_create(&init_net, NETLINK_OP_FREEZER, &cfg);
	if (sock_handle == NULL) {
		pr_err("%s: create netlink socket failed!\n", __func__);
		return OP_FREEZER_ERROR;
	}

	if (op_freezer_netfilter_init() == OP_FREEZER_ERROR) {
		pr_err("%s: netfilter init failed!\n", __func__);
		netlink_kernel_release(sock_handle);  //release socket
		return OP_FREEZER_ERROR;
	}

	printk(KERN_INFO "%s: -\n", __func__);
	return OP_FREEZER_NOERROR;
}

static void __exit op_freezer_exit(void)
{
	if (sock_handle)
		netlink_kernel_release(sock_handle);

	op_freezer_netfilter_deinit();
	printk(KERN_INFO "%s: -\n", __func__);
}

module_init(op_freezer_init);
module_exit(op_freezer_exit);

MODULE_LICENSE("GPL");

