#ifndef _EVENT_H
#define _EVENT_H

#include <inttypes.h>
#include <sys/epoll.h>

struct hyper_event;

struct hyper_event_ops {
	int		(*read)(struct hyper_event *e);
	int		(*write)(struct hyper_event *e, int efd);
	int		(*handle)(struct hyper_event *e, uint32_t len);
	void		(*hup)(struct hyper_event *e, int efd);
	int		rbuf_size;
	int		wbuf_size;
	int		len_offset;
	int		ack;
};

struct hyper_buf {
	uint32_t		get;
	uint32_t		size;
	uint8_t			*data;
};

struct hyper_event {
	int			fd;
	int			flag;
	struct hyper_buf	rbuf;
	struct hyper_buf	wbuf;
	struct hyper_event_ops	*ops;
	void			*ptr;
};

int hyper_add_event(int efd, struct hyper_event *de, int flag);
int hyper_modify_event(int efd, struct hyper_event *de, int flag);
int hyper_init_event(struct hyper_event *de, struct hyper_event_ops *ops,
		     void *arg);
int hyper_handle_event(int efd, struct epoll_event *event);
void hyper_reset_event(struct hyper_event *de);
void hyper_event_hup(struct hyper_event *de, int efd);
int hyper_event_read(struct hyper_event *de);
int hyper_event_write(struct hyper_event *de, int efd);
#endif
