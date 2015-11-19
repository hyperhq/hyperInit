#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>
#include "syscall.h"

#include "hyper.h"
#include "util.h"
#include "parse.h"

static void pts_hup(struct hyper_event *de, int efd, int out)
{
	struct hyper_exec *exec;
	struct hyper_pod *pod = de->ptr;
	struct hyper_buf *buf = &pod->tty.wbuf;
	uint64_t seq;

	if (out) {
		exec = container_of(de, struct hyper_exec, e);
		seq = exec->seq;
	} else {
		exec = container_of(de, struct hyper_exec, errev);
		seq = exec->errseq;
	}

	fprintf(stdout, "%s, seq %" PRIu64"\n", __func__, seq);

	hyper_event_hup(de, efd);

	if (buf->get + 12 > buf->size) {
		fprintf(stdout, "%s: tty buf full\n", __func__);
		return;
	}

	/* no in event, no more data, send eof */
	hyper_set_be64(buf->data + buf->get, seq);
	hyper_set_be32(buf->data + buf->get + 8, 12);
	buf->get += 12;

	hyper_modify_event(pod->efd, &pod->tty, EPOLLIN | EPOLLOUT);

	hyper_release_exec(exec, pod);
}

static void stdout_hup(struct hyper_event *de, int efd)
{
	fprintf(stdout, "%s\n", __func__);
	return pts_hup(de, efd, 1);
}

static void stderr_hup(struct hyper_event *de, int efd)
{
	fprintf(stdout, "%s\n", __func__);
	return pts_hup(de, efd, 0);
}

static int pts_loop(struct hyper_event *de, uint64_t seq)
{
	int size = -1;
	struct hyper_pod *pod = de->ptr;
	struct hyper_buf *buf = &pod->tty.wbuf;

	while ((buf->get + 12 < buf->size) && size) {
		size = read(de->fd, buf->data + buf->get + 12, buf->size - buf->get - 12);
		fprintf(stdout, "%s: read %d data\n", __func__, size);
		if (size <= 0) {
			if (errno == EINTR)
				continue;

			if (errno != EAGAIN && errno != EIO) {
				perror("fail to read tty fd");
				return -1;
			}

			break;
		}

		hyper_set_be64(buf->data + buf->get, seq);
		hyper_set_be32(buf->data + buf->get + 8, size + 12);
		buf->get += size + 12;
	}

	if (hyper_modify_event(pod->efd, &pod->tty, EPOLLIN | EPOLLOUT) < 0) {
		fprintf(stderr, "modify ctl tty event to in & out failed\n");
		return -1;
	}

	return 0;
}

static int stdout_loop(struct hyper_event *de)
{
	struct hyper_exec *exec = container_of(de, struct hyper_exec, e);
	fprintf(stdout, "%s, seq %" PRIu64"\n", __func__, exec->seq);

	return pts_loop(de, exec->seq);
}

struct hyper_event_ops pts_ops = {
	.read		= stdout_loop,
	.hup		= stdout_hup,
	.write		= hyper_event_write,
	.wbuf_size	= 512,
	/* don't need read buff, the pts data will store in tty buffer */
};

static int stderr_loop(struct hyper_event *de)
{
	struct hyper_exec *exec = container_of(de, struct hyper_exec, errev);
	fprintf(stdout, "%s, seq %" PRIu64"\n", __func__, exec->errseq);

	return pts_loop(de, exec->errseq);
}

struct hyper_event_ops err_ops = {
	.read		= stderr_loop,
	.hup		= stderr_hup,
	/* don't need read buff, the stderr data will store in tty buffer */
	/* don't need write buff, the stderr data is one way */
};

int hyper_setup_exec_tty(struct hyper_exec *e)
{
	int unlock = 0;
	char ptmx[512], path[512];

	if (e->seq == 0)
		return 0;

	if (e->errseq > 0) {
		int errpipe[2];
		if (pipe2(errpipe, O_NONBLOCK|O_CLOEXEC) < 0) {
			fprintf(stderr, "creating stderr pipe failed\n");
			return -1;
		}
		e->errev.fd = errpipe[0];
		e->errfd = errpipe[1];
	}

	if (e->id) {
		if (sprintf(path, "/tmp/hyper/%s/devpts/", e->id) < 0) {
			fprintf(stderr, "get ptmx path failed\n");
			return -1;
		}
	} else {
		if (sprintf(path, "/dev/pts/") < 0) {
			fprintf(stderr, "get ptmx path failed\n");
			return -1;
		}
	}

	if (sprintf(ptmx, "%s/ptmx", path) < 0) {
		fprintf(stderr, "get ptmx path failed\n");
		return -1;
	}

	e->e.fd = open(ptmx, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
	if (e->e.fd < 0) {
		perror("open ptmx device for execcmd failed");
		return -1;
	}

	if (ioctl(e->e.fd, TIOCSPTLCK, &unlock) < 0) {
		perror("ioctl unlock ptmx device failed");
		return -1;
	}

	if (ioctl(e->e.fd, TIOCGPTN, &e->ptyno) < 0) {
		perror("ioctl get execcmd pty device failed");
		return -1;
	}

	if (sprintf(ptmx, "%s/%d", path, e->ptyno) < 0) {
		fprintf(stderr, "get ptmx path failed\n");
		return -1;
	}

	e->ptyfd = open(ptmx, O_RDWR | O_NOCTTY | O_CLOEXEC);
	fprintf(stdout, "get pty device for exec %s\n", ptmx);

	fprintf(stdout, "%s pts event %p, fd %d %d\n",
		__func__, &e->e, e->e.fd, e->ptyfd);
	return 0;
}

int hyper_dup_exec_tty(int to, struct hyper_exec *e)
{
	int fd = -1, ret = -1;
	char pty[128];

	fprintf(stdout, "%s\n", __func__);
	setsid();

	if (e->seq) {
		fd = e->ptyfd;
	} else {
		if (sprintf(pty, "/dev/null") < 0) {
			perror("get pts device name failed");
			goto out;
		}
		fd = open(pty, O_RDWR | O_NOCTTY);
	}

	if (fd < 0) {
		perror("open pty device for execcmd failed");
		goto out;
	}

	if (e->seq && (ioctl(fd, TIOCSCTTY, NULL) < 0)) {
		perror("ioctl pty device for execcmd failed");
		goto out;
	}

	fflush(stdout);
	hyper_send_type(to, READY);

	if (dup2(fd, STDIN_FILENO) < 0) {
		perror("dup tty device to stdin failed");
		goto out;
	}

	if (dup2(fd, STDOUT_FILENO) < 0) {
		perror("dup tty device to stdout failed");
		goto out;
	}

	if (e->errseq > 0) {
		if (dup2(e->errfd, STDERR_FILENO) < 0) {
			perror("dup err pipe to stderr failed");
			goto out;
		}
	} else {
		if (dup2(fd, STDERR_FILENO) < 0) {
			perror("dup tty device to stderr failed");
			goto out;
		}
	}

	ret = 0;
out:
	close(fd);

	return ret;
}

int hyper_watch_exec_pty(struct hyper_exec *exec, struct hyper_pod *pod)
{
	fprintf(stdout, "hyper_init_event container pts event %p, ops %p, fd %d\n",
		&exec->e, &pts_ops, exec->e.fd);

	if (exec->seq == 0)
		return 0;

	if (hyper_init_event(&exec->e, &pts_ops, pod) < 0 ||
	    hyper_add_event(pod->efd, &exec->e, EPOLLIN) < 0) {
		fprintf(stderr, "add container pts master event failed\n");
		return -1;
	}
	exec->ref++;

	if (exec->errseq == 0)
		return 0;

	if (hyper_init_event(&exec->errev, &err_ops, pod) < 0 ||
	    hyper_add_event(pod->efd, &exec->errev, EPOLLIN) < 0) {
		fprintf(stderr, "add container stderr event failed\n");
		return -1;
	}
	exec->ref++;
	return 0;
}

int hyper_enter_container(struct hyper_pod *pod,
			  struct hyper_exec *exec)
{
	struct hyper_container *c;
	char path[512];

	c = hyper_find_container(pod, exec->id);
	if (c == NULL) {
		fprintf(stderr, "can not find container %s\n", exec->id);
		return -1;
	}

	if (c->ns < 0) {
		perror("fail to open mntns of pod init");
		return -1;
	}

	if (setns(c->ns, CLONE_NEWNS) < 0) {
		perror("fail to enter container ns");
		return -1;
	}

	sprintf(path, "/tmp/hyper/%s/root/%s/", c->id, c->rootfs);
	fprintf(stdout, "root directory for container is %s, exec %s\n",
		path, exec->argv[0]);

	/* TODO: wait for container finishing setup root */
	if (chroot(path) < 0) {
		perror("chroot for exec command failed");
		return -1;
	}

	chdir("/");

	return hyper_setup_env(c->envs, c->envs_num);
}

static void hyper_free_exec(struct hyper_exec *exec)
{
	int i;

	free(exec->id);

	for (i = 0; i < exec->argc; i++) {
		//fprintf(stdout, "argv %d %s\n", i, exec->argv[i]);
		free(exec->argv[i]);
	}

	free(exec->argv);
	free(exec);
}

static int hyper_do_exec_cmd(struct hyper_pod *pod, struct hyper_exec *exec, int pipe)
{
	int ret = -1;

	if (hyper_enter_container(pod, exec) < 0) {
		fprintf(stderr, "enter container ns failed\n");
		goto exit;
	}

	if (hyper_dup_exec_tty(pipe, exec) < 0) {
		fprintf(stderr, "dup pts to exec stdio failed\n");
		goto exit;
	}

	if (execvp(exec->argv[0], exec->argv) < 0) {
		perror("exec failed");
		goto exit;
	}

	ret = 0;
exit:
	hyper_send_type(pipe, ERROR);
	_exit(ret);
}

int hyper_exec_cmd(struct hyper_pod *pod, char *json, int length)
{
	struct hyper_exec *exec;
	int pipe[2] = {-1 , -1};
	int pid, ret = -1;
	uint32_t type;

	fprintf(stdout, "call hyper_exec_cmd, json %s, len %d\n", json, length);

	exec = hyper_parse_execcmd(json, length);
	if (exec == NULL) {
		fprintf(stderr, "parse exec cmd failed\n");
		goto out;
	}

	if (exec->argv == NULL || exec->id == NULL) {
		fprintf(stderr, "cmd is %p, seq %" PRIu64 ", container %s\n",
			exec->argv, exec->seq, exec->id);
		goto free_exec;
	}

	if (hyper_setup_exec_tty(exec) < 0) {
		fprintf(stderr, "setup exec tty failed\n");
		goto free_exec;
	}

	if (pipe2(pipe, O_CLOEXEC) < 0) {
		perror("create pipe between pod init execcmd failed");
		goto close_tty;
	}

	if (hyper_watch_exec_pty(exec, pod) < 0) {
		fprintf(stderr, "add pts master event failed\n");
		goto out;
	}

	pid = fork();
	if (pid < 0) {
		perror("fail to create exec process");
		goto close_tty;
	} else if (pid == 0) {
		return hyper_do_exec_cmd(pod, exec, pipe[1]);
	}

	if (hyper_get_type(pipe[0], &type) < 0 || type != READY) {
		fprintf(stderr, "hyper init doesn't get execcmd ready message\n");
		goto close_tty;
	}

	exec->pid = pid;
	//TODO combin ref++ and add to list.
	list_add_tail(&exec->list, &pod->exec_head);
	exec->ref++;
	fprintf(stdout, "create exec cmd %s pid %d, ref %d\n", exec->argv[0], pid, exec->ref);

	ret = 0;
out:
	close(pipe[0]);
	close(pipe[1]);
	return ret;
close_tty:
	close(exec->ptyfd);
	close(exec->errfd);
	close(exec->e.fd);
free_exec:
	hyper_free_exec(exec);
	goto out;
}

int hyper_release_exec(struct hyper_exec *exec,
		       struct hyper_pod *pod)
{
	if (--exec->ref != 0) {
		fprintf(stdout, "still have %d user of exec\n", exec->ref);
		return 0;
	}

	/* exec has no pty or the pty user already exited */
	fprintf(stdout, "last user of exec exit, release\n");

	hyper_reset_event(&exec->e);
	hyper_reset_event(&exec->errev);

	list_del_init(&exec->list);

	fprintf(stdout, "%s exit code %" PRIu8"\n", __func__, exec->code);
	if (exec->init) {
		fprintf(stdout, "%s container init exited, type %d, remains %d, policy %d\n",
			__func__, pod->type, pod->remains, pod->policy);

		// TODO send finish of this container and full cleanup
		if (--pod->remains > 0)
			return 0;

		if (pod->type == STOPPOD) {
			/* stop pod manually */
			hyper_send_type(pod->chan.fd, ACK);

		} else {
			/* send out pod finish message, hyper will decide if restart pod or not */
			hyper_send_pod_finished(pod);
		}

		hyper_cleanup_pod(pod);
		return 0;
	}

	hyper_free_exec(exec);

	return 0;
}

struct hyper_exec *hyper_find_exec_by_pid(struct list_head *head, int pid)
{
	struct hyper_exec *exec;

	list_for_each_entry(exec, head, list) {
		fprintf(stdout, "exec pid %d, pid %d\n", exec->pid, pid);
		if (exec->pid != pid)
			continue;

		return exec;
	}

	return NULL;
}

struct hyper_exec *hyper_find_exec_by_seq(struct hyper_pod *pod, uint64_t seq)
{
	struct hyper_exec *exec;

	list_for_each_entry(exec, &pod->exec_head, list) {
		fprintf(stdout, "exec seq %" PRIu64 ", seq %" PRIu64 "\n",
			exec->seq, seq);
		if (exec->seq != seq)
			continue;

		return exec;
	}

	return NULL;
}

int hyper_handle_exec_exit(struct hyper_pod *pod, int pid, uint8_t code)
{
	struct hyper_exec *exec;

	exec = hyper_find_exec_by_pid(&pod->exec_head, pid);
	if (exec == NULL) {
		fprintf(stdout, "can not find exec whose pid is %d\n",
			pid);
		return 0;
	}

	fprintf(stdout, "%s exec exit pid %d, seq %" PRIu64 ", container %s\n",
		__func__, exec->pid, exec->seq, exec->id ? exec->id : "pod");

	exec->code = code;
	exec->exit = 1;

	close(exec->ptyfd);
	exec->ptyfd = -1;
	close(exec->errfd);
	exec->errfd = -1;

	hyper_release_exec(exec, pod);

	return 0;
}

void hyper_cleanup_exec(struct hyper_pod *pod)
{
	struct hyper_exec *exec, *next;
	uint8_t buf[12];

	if (hyper_setfd_block(pod->tty.fd) < 0)
		return;

	hyper_set_be32(buf + 8, 12);
	list_for_each_entry_safe(exec, next, &pod->exec_head, list) {
		fprintf(stdout, "send eof for exec seq %" PRIu64 "\n", exec->seq);
		hyper_set_be64(buf, exec->seq);
		if (hyper_send_data(pod->tty.fd, buf, 12) < 0)
			fprintf(stderr, "send eof failed\n");
	}
}
