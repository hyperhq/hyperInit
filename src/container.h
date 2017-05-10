#ifndef _CONTAINER_H_
#define _CONTAINER_H_

#include <stdbool.h>

#include "exec.h"
#include "api.h"

struct volume {
	char	*device;
	char	*scsiaddr;
	char	*mountpoint;
	char	*fstype;
	int	readonly;
	int	docker;
};

struct fsmap {
	char	*source;
	char	*path;
	int	readonly;
	int	docker;
};

struct sysctl {
	char	*path;
	char	*value;
};

struct port {
	int  host_port;
	int  container_port;
	char *protocol;
};

struct hyper_container {
	struct list_head	list;
	struct hyper_exec	exec;
	int			ns;

	// configs
	char			*id;
	char			*rootfs;
	char			*image;
	char			*scsiaddr;
	char			*fstype;
	struct volume		*vols;
	struct fsmap		*maps;
	struct sysctl		*sys;
	struct port		*ports;
	int			vols_num;
	int			maps_num;
	int			sys_num;
	int			ports_num;
	int			initialize;
};

struct hyper_pod;

int hyper_setup_container(struct hyper_container *container, struct hyper_pod *pod);
struct hyper_container *hyper_find_container(struct hyper_pod *pod, const char *id);
void hyper_cleanup_container(struct hyper_container *container, struct hyper_pod *pod, bool sync_only);
void hyper_cleanup_mounts(struct hyper_pod *pod);
void hyper_free_container(struct hyper_container *c);

static inline int hyper_has_container(struct hyper_pod *pod, const char *id) {
	return strcmp(id, HYPERSTART_EXEC_CONTAINER) == 0 || hyper_find_container(pod, id) != NULL;
}
#endif
