#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "net.h"
#include "util.h"
#include "hyper.h"
#include "event.h"

void hyper_reset_event(struct hyper_event *he)
{
	free(he->rbuf.data);
	free(he->wbuf.data);
	close(he->fd);
	memset(he, 0, sizeof(*he));
	he->fd = -1;
}

int hyper_init_event(struct hyper_event *he, struct hyper_event_ops *ops, void *arg)
{
	struct hyper_buf *rbuf = &he->rbuf;
	struct hyper_buf *wbuf = &he->wbuf;

	memset(rbuf, 0, sizeof(*rbuf));
	memset(wbuf, 0, sizeof(*wbuf));

	he->ops		= ops;
	he->ptr		= arg;
	rbuf->size	= ops->rbuf_size;
	wbuf->size	= ops->wbuf_size;

	if (rbuf->size) {
		rbuf->data = malloc(rbuf->size);
		if (rbuf->data == NULL) {
			fprintf(stderr, "allocate read buffer for event failed\n");
			return -1;
		}
	}

	if (wbuf->size) {
		wbuf->data = malloc(wbuf->size);
		if (wbuf->data == NULL) {
			fprintf(stderr, "allocate write buffer for event failed\n");
			return -1;
		}
	}

	return 0;
}

int hyper_add_event(int efd, struct hyper_event *he, int flag)
{
	struct epoll_event event = {
		.events		= flag,
		.data.ptr	= he,
	};

	he->flag = flag;
	if (hyper_setfd_nonblock(he->fd) < 0) {
		perror("set fd nonblock failed");
		return -1;
	}

	fprintf(stdout, "%s add event fd %d, %p\n", __func__, he->fd, he->ops);

	if (epoll_ctl(efd, EPOLL_CTL_ADD, he->fd, &event) < 0) {
		perror("epoll_ctl fd failed");
		return -1;
	}

	return 0;
}

int hyper_modify_event(int efd, struct hyper_event *he, int flag)
{
	struct epoll_event event = {
		.events		= flag,
		.data.ptr	= he,
	};

	if (he->flag == flag)
		return 0;

	he->flag = flag;
	fprintf(stdout, "%s modify event fd %d, %p, event %d\n",
			__func__, he->fd, he, flag);

	if (epoll_ctl(efd, EPOLL_CTL_MOD, he->fd, &event) < 0) {
		perror("epoll_ctl fd failed");
		return -1;
	}

	return 0;
}

static int hyper_getmsg_len(struct hyper_event *he, uint32_t *len)
{
	struct hyper_buf *buf = &he->rbuf;

	if (buf->get < he->ops->len_offset + 4)
		return -1;

	*len = hyper_get_be32(buf->data + he->ops->len_offset);
	return 0;
}

int hyper_event_read(struct hyper_event *he, int efd)
{
	struct hyper_buf *buf = &he->rbuf;
	uint32_t len = 4;
	uint8_t data[4];
	int offset = he->ops->len_offset;
	int end = offset + 4;
	int size;

	fprintf(stdout, "%s\n", __func__);

	while (hyper_getmsg_len(he, &len) < 0) {
		size = read(he->fd, buf->data + buf->get, end - buf->get);
		if (size > 0) {
			buf->get += size;
			fprintf(stdout, "already read %" PRIu32 " bytes data\n",
				buf->get);

			if (he->ops->ack) {
				/* control channel, need ack */
				hyper_set_be32(data, size);
				hyper_send_msg(he->fd, NEXT, 4, data);
			}
			continue;
		}

		if (errno == EINTR)
			continue;

		if (errno != EAGAIN && size != 0) {
			perror("fail to read");
			return -1;
		}

		return 0;
	}

	fprintf(stdout, "get length %" PRIu32"\n", len);
	if (len > buf->size) {
		fprintf(stderr, "get length %" PRIu32", too long\n", len);
		return -1;
	}

	while (buf->get < len) {
		size = read(he->fd, buf->data + buf->get, len - buf->get);
		if (size > 0) {
			buf->get += size;
			fprintf(stdout, "read %d bytes data, total data %" PRIu32 "\n",
				size, buf->get);
			if (he->ops->ack) {
				/* control channel, need ack */
				hyper_set_be32(data, size);
				hyper_send_msg(he->fd, NEXT, 4, data);
			}

			continue;
		}

		if (errno == EINTR)
			continue;

		if (errno != EAGAIN && size != 0) {
			perror("fail to read");
			return -1;
		}

		/* size == 0 : No one connect to qemu socket */
		return 0;
	}

	/* get the whole data */
	if (he->ops->handle(he, len) != 0)
		return -1;

	/* len: length of the already get new data */
	buf->get -= len;
	memmove(buf->data, buf->data + len, buf->get);

	return 0;
}

int hyper_event_write(struct hyper_event *he, int efd)
{
	struct hyper_buf *buf = &he->wbuf;
	uint32_t len = 0;
	int size = 0;

	while (len < buf->get) {
		size = write(he->fd, buf->data + len, buf->get - len);
		if (size <= 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || size == 0)
				break;
			return -1;
		}
		len += size;
	}

	buf->get -= len;
	memmove(buf->data, buf->data + len, buf->get);
	return 0;
}

void hyper_event_hup(struct hyper_event *he, int efd)
{
	if (epoll_ctl(efd, EPOLL_CTL_DEL, he->fd, NULL) < 0)
		perror("epoll_ctl del epoll event failed");
	hyper_reset_event(he);
}

int hyper_handle_event(int efd, struct epoll_event *event)
{
	struct hyper_event *he = event->data.ptr;
	fprintf(stdout, "%s get event %d, he %p, fd %d. ops %p\n",
			__func__, event->events, he, he->fd, he->ops);

	/* do not handle hup event if have in event */
	if ((event->events & EPOLLIN) && he->ops->read) {
		fprintf(stdout, "%s event EPOLLIN, he %p, fd %d, %p\n",
			__func__, he, he->fd, he->ops);
		if (he->ops->read && he->ops->read(he, efd) < 0)
			return -1;
	} else if (event->events & EPOLLHUP) {
		fprintf(stdout, "%s event EPOLLHUP, he %p, fd %d, %p\n",
			__func__, he, he->fd, he->ops);
		if (he->ops->hup)
			he->ops->hup(he, efd);
		return 0;
	}

	if (event->events & EPOLLOUT) {
		fprintf(stdout, "%s event EPOLLOUT, he %p, fd %d, %p\n",
			__func__, he, he->fd, he->ops);
		if (he->ops->write && he->ops->write(he, efd) < 0)
			return -1;
	}

	if (event->events & EPOLLERR) {
		fprintf(stderr, "get epoll err of not epool in event\n");
		if (he->ops->hup)
			he->ops->hup(he, efd);
		return 0;
	}

	return 0;
}
