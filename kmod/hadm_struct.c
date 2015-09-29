#include <linux/module.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "hadm_def.h"
#include "hadm_struct.h"
#include "hadm_device.h"
#include "hadm_proc.h"

#include "p_worker.h"
#include "hadm_queue.h"
#include "hadm_thread.h"
#include "hadm_socket.h"

struct hadm_struct *g_hadm;

void hadm_list_add(struct hadm_struct *hadm, struct hadmdev *dev)
{
	write_lock(&hadm->dev_list_lock);
	list_add(&dev->node, &hadm->dev_list);
	write_unlock(&hadm->dev_list_lock);

	atomic_inc(&hadm->dev_list_len);
}

void hadm_list_del(struct hadm_struct *hadm, struct hadmdev *dev)
{
	write_lock(&hadm->dev_list_lock);
	list_del(&dev->node);
	write_unlock(&hadm->dev_list_lock);

	atomic_dec(&hadm->dev_list_len);
}

struct hadmdev *find_hadmdev_by_minor(int minor)
{
	struct hadmdev *dev,*rdev;
	rdev=NULL;
	read_lock(&g_hadm->dev_list_lock);
	if(list_empty(&g_hadm->dev_list)) {
		goto find_out;
	}

	list_for_each_entry(dev, &g_hadm->dev_list, node) {
		if (dev->minor == minor) {
			rdev=dev;
			break;
		}
	}
find_out:
	read_unlock(&g_hadm->dev_list_lock);
	return rdev;
}

void hadm_disconnect(struct hadm_struct *hadm)
{
	struct hadmdev *dev_iter;

	list_for_each_entry(dev_iter, &g_hadm->dev_list, node)
		hadmdev_disconnect_all(dev_iter);
}

int hadm_get_config_count(struct hadm_struct *hadm)
{
	return atomic_read(&hadm->config_count);
}

void hadm_inc_config_count(struct hadm_struct *hadm)
{
	atomic_inc(&hadm->config_count);
}

void hadm_put(struct hadm_struct *hadm)
{
	struct hadmdev *dev, *tmp;
	int i=0;

	if (hadm->cmd_server_sock)
		hadm_socket_close(hadm->cmd_server_sock);
	hadm_net_shutdown(hadm->ctrl_net, SHUT_RDWR);
	hadm_net_shutdown(hadm->data_net, SHUT_RDWR);

	hadm_queue_freeze_all(hadm->cmd_receiver_queue);
	for (i = 0; i < P_TYPE_NUM; i++)
		hadm_queue_freeze_all(hadm->p_sender_queue[i]);

	write_lock(&g_hadm->dev_list_lock);
	list_for_each_entry_safe(dev, tmp, &hadm->dev_list, node) {
		list_del(&dev->node);
		write_unlock(&g_hadm->dev_list_lock);
		hadmdev_put(dev);
		write_lock(&g_hadm->dev_list_lock);
	}
	write_unlock(&g_hadm->dev_list_lock);

	if(!IS_ERR_OR_NULL(hadm->cmd_worker)) {
		hadm_thread_stop(hadm->cmd_worker);
		hadm_thread_free(&hadm->cmd_worker);
	}
	for(i=0;i<P_TYPE_NUM;i++) {
		if ( ! IS_ERR_OR_NULL(hadm->p_sender[i])) {
			hadm_thread_stop(hadm->p_sender[i]);
			hadm_thread_free(&hadm->p_sender[i]);
		}
		if ( ! IS_ERR_OR_NULL(hadm->p_receiver[i])) {
			hadm_thread_stop(hadm->p_receiver[i]);
			hadm_thread_free(&hadm->p_receiver[i]);
		}
	}

	for(i=0;i<P_TYPE_NUM;i++) {
		hadm_pack_queue_clean(hadm->p_sender_queue[i]);
	}
	hadm_pack_queue_clean(hadm->cmd_receiver_queue);
	hadm_socket_release(hadm->cmd_server_sock);
	hadm_net_release(hadm->ctrl_net);
	hadm_net_release(hadm->data_net);

	/* free queue */
	for (i=0; i<P_TYPE_NUM; i++) {
		hadm_queue_free(hadm->p_sender_queue[i]);
	}
	hadm_queue_free(hadm->cmd_receiver_queue);
	if (hadm->major > 0)
		unregister_blkdev(hadm->major, HADMDEV_NAME);

	hadm_remove_proc(hadm);
	kfree(hadm);
	pr_info("hadm module is unloaded\n");
}

struct hadm_struct *hadm_alloc(int gfp_mask)
{
	struct hadm_struct *hadm;
	int i = 0;

	hadm = kzalloc(sizeof(*hadm), gfp_mask);
	if (hadm == NULL)
		return ERR_PTR(-ENOMEM);

	hadm->data_net = hadm_net_create(gfp_mask);
	hadm->ctrl_net = hadm_net_create(gfp_mask);

	hadm->cmd_receiver_queue = hadm_queue_alloc();
	for (i = 0; i < P_TYPE_NUM; i++) {
		hadm->p_sender_queue[i] = hadm_queue_alloc();
		hadm->p_receiver[i] = hadm_thread_alloc();
		hadm->p_sender[i] = hadm_thread_alloc();
	}
	hadm->cmd_worker = hadm_thread_alloc();

	return hadm;
}

static struct hadm_thread_info hadm_sender_threads[] = {
	[P_CTRL_TYPE] = { p_ctrl_sender_run, "ctrl_snd" },
	[P_DATA_TYPE] = { p_data_sender_run, "data_snd" },
	[P_CMD_TYPE] = { cmd_sender_run, "cmd_snd" },
};

static struct hadm_thread_info hadm_receiver_threads[] = {
	[P_CTRL_TYPE] = { p_ctrl_receiver_run, "ctrl_rcv" },
	[P_DATA_TYPE] = { p_data_receiver_run, "data_rcv" },
	[P_CMD_TYPE] = { cmd_receiver_run, "cmd_rcv" },
};

static char *get_ptype_name(int p_type)
{
	switch (p_type) {
	case P_CTRL_TYPE:
		return "ctrl";
	case P_DATA_TYPE:
		return "data";
	case P_CMD_TYPE:
		return "cmd";
	default:
		return "unknown";
	}
}

int hadm_init(struct hadm_struct *hadm, const char *hadm_config_path,
	      const int hadm_cmd_port, int gfp_mask)
{
	int ret = 0;
	int i = 0;
	char name[MAX_QUEUE_NAME];
	hadm->state=1;
	hadm->major = register_blkdev(0, HADMDEV_NAME);
	if (hadm->major < 0)
		return hadm->major;

	INIT_LIST_HEAD(&hadm->dev_list);
	rwlock_init(&hadm->dev_list_lock);
	atomic_set(&hadm->dev_list_len, 0);
	atomic_set(&hadm->config_count, 0);

	hadm->cmd_server_sock = hadm_socket_listen(hadm_cmd_port);
	if (IS_ERR_OR_NULL(hadm->cmd_server_sock)) {
		return PTR_ERR(hadm->cmd_server_sock);
	}

	/* TODO: 可以使用一个数组来表示三个线程，并定义对应的参数列表 */
	hadm_queue_init(hadm->cmd_receiver_queue, "cmd_recv_queue", 1024);
	for (i = 0; i < P_TYPE_NUM; i++) {
		hadm_thread_init(hadm->p_receiver[i], hadm_receiver_threads[i].name,
			hadm_receiver_threads[i].func, NULL, NULL);
		hadm_thread_start(hadm->p_receiver[i]);
		snprintf(name,MAX_QUEUE_NAME-1,"%s_sender_q", get_ptype_name(i));

		hadm_queue_init(hadm->p_sender_queue[i], name , 2048);
		hadm_thread_init(hadm->p_sender[i], hadm_sender_threads[i].name,
			hadm_sender_threads[i].func, NULL, NULL);
		hadm_thread_start(hadm->p_sender[i]);
	}
	hadm_thread_init(hadm->cmd_worker, "cmd_worker",
			cmd_worker_run, NULL, NULL);
	hadm_thread_start(hadm->cmd_worker);

	hadm_create_proc(hadm);

	return ret;
}

struct hadm_queue *hadm_get_queue(struct hadm_struct *hadm, int type)
{
	struct hadm_queue *q;

	if (type > P_CTRL_START &&
			type < P_CTRL_END)
		q = g_hadm->p_sender_queue[P_CTRL_TYPE];
	else if (type > P_DATA_START &&
			type < P_DATA_END)
		q = g_hadm->p_sender_queue[P_DATA_TYPE];
	else {
		pr_err("%s: wrong type:%d.", __FUNCTION__, type);
		q = NULL;
	}

	return q;
}
