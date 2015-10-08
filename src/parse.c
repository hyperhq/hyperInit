#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "list.h"
#include "parse.h"

char *json_token_str(char *js, jsmntok_t *t)
{
	js[t->end] = '\0';
	return js + t->start;
}

int json_token_int(char *js, jsmntok_t *t)
{
	return strtol(json_token_str(js, t), 0, 10);
}

uint64_t json_token_ll(char *js, jsmntok_t *t)
{
	return strtoll(json_token_str(js, t), 0, 10);
}

int json_token_streq(char *js, jsmntok_t *t, char *s)
{
	return (strncmp(js + t->start, s, t->end - t->start) == 0 &&
		strlen(s) == (size_t)(t->end - t->start));
}

static int container_parse_cmd(struct hyper_container *c, char *json, jsmntok_t *toks)
{
	int i = 1, j;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "cmd need array");
		return -1;
	}

	c->exec.argc = toks[i].size;

	c->exec.argv = calloc(c->exec.argc + 1, sizeof(*c->exec.argv));
	c->exec.argv[c->exec.argc] = NULL;

	for (j = 0; j < c->exec.argc; j++) {
		i++;
		c->exec.argv[j] = strdup(json_token_str(json, &toks[i]));
		fprintf(stdout, "container init arg %d %s\n", j, c->exec.argv[j]);
	}

	return i;
}

static int container_parse_volumes(struct hyper_container *c, char *json, jsmntok_t *toks)
{
	int i = 1, j;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "volume need array\n");
		return -1;
	}
	c->vols_num = toks[i].size;
	fprintf(stdout, "volumes num %d\n", c->vols_num);
	c->vols = calloc(c->vols_num, sizeof(*c->vols));

	for (j = 0; j < c->vols_num; j++) {
		int i_volume, next_volume;

		i++;
		if (toks[i].type != JSMN_OBJECT) {
			fprintf(stdout, "volume array need object\n");
			return -1;
		}
		next_volume = toks[i].size;
		for (i_volume = 0; i_volume < next_volume; i_volume++) {
			i++;
			if (json_token_streq(json, &toks[i], "device")) {
				c->vols[j].device =
				strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "volume %d device %s\n", j, c->vols[j].device);
			} else if (json_token_streq(json, &toks[i], "mount")) {
				c->vols[j].mountpoint =
				strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "volume %d mp %s\n", j, c->vols[j].mountpoint);
			} else if (json_token_streq(json, &toks[i], "fstype")) {
				c->vols[j].fstype =
				strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "volume %d fstype %s\n", j, c->vols[j].fstype);
			} else if (json_token_streq(json, &toks[i], "readOnly")) {
				if (!json_token_streq(json, &toks[++i], "false"))
					c->vols[j].readonly = 1;
				fprintf(stdout, "volume %d readonly %d\n", j, c->vols[j].readonly);
			} else {
				fprintf(stdout, "in voulmes incorrect %s\n", json_token_str(json, &toks[i]));
			}
		}
	}

	return i;
}

static int container_parse_fsmap(struct hyper_container *c, char *json, jsmntok_t *toks)
{
	int i = 1, j;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "envs need array\n");
		return -1;
	}

	c->maps_num = toks[i].size;
	fprintf(stdout, "fsmap num %d\n", c->maps_num);
	c->maps = calloc(c->maps_num, sizeof(*c->maps));

	for (j = 0; j < c->maps_num; j++) {
		int i_map, next_map;

		i++;
		if (toks[i].type != JSMN_OBJECT) {
			fprintf(stdout, "fsmap array need object\n");
			return -1;
		}
		next_map = toks[i].size;
		for (i_map = 0; i_map < next_map; i_map++) {
			i++;
			if (json_token_streq(json, &toks[i], "source")) {
				c->maps[j].source =
				strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "maps %d source %s\n", j, c->maps[j].source);
			} else if (json_token_streq(json, &toks[i], "path")) {
				c->maps[j].path =
				strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "maps %d path %s\n", j, c->maps[j].path);
			} else if (json_token_streq(json, &toks[i], "readOnly")) {
				if (!json_token_streq(json, &toks[++i], "false"))
					c->maps[j].readonly = 1;
				fprintf(stdout, "maps %d readonly %d\n", j, c->maps[j].readonly);
			} else {
				fprintf(stdout, "in maps incorrect %s\n",
					json_token_str(json, &toks[i]));
			}
		}
	}

	return i;
}

static int container_parse_envs(struct hyper_container *c, char *json, jsmntok_t *toks)
{
	int i = 1, j;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "encs need array\n");
		return -1;
	}

	c->envs_num = toks[i].size;
	fprintf(stdout, "envs num %d\n", c->envs_num);
	c->envs = calloc(c->envs_num, sizeof(*c->envs));

	for (j = 0; j < c->envs_num; j++) {
		int i_env, next_env;

		i++;
		if (toks[i].type != JSMN_OBJECT) {
			fprintf(stdout, "env array need object\n");
			return -1;
		}
		next_env = toks[i].size;
		for (i_env = 0; i_env < next_env; i_env++) {
			i++;
			if (json_token_streq(json, &toks[i], "env")) {
				c->envs[j].env =
				strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "envs %d env %s\n", j, c->envs[j].env);
			} else if (json_token_streq(json, &toks[i], "value")) {
				c->envs[j].value =
				strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "envs %d value %s\n", j, c->envs[j].value);
			} else {
				fprintf(stdout, "in envs incorrect %s\n", json_token_str(json, &toks[i]));
			}
		}
	}

	return i;
}

static int hyper_parse_container(struct hyper_pod *pod, struct hyper_container *c,
			       char *json, jsmntok_t *toks)
{
	int i = 1, j, next, next_container;
	jsmntok_t *t;

	if (toks[i].type != JSMN_OBJECT) {
		fprintf(stderr, "format incorrect\n");
		return -1;
	}

	c->exec.init = 1;
	c->exec.code = -1;
	c->exec.e.fd = -1;
	c->exec.ptyfd = -1;
	c->exec.seq = 0;
	c->exec.errseq = 0;
	c->ns = -1;

	next_container = toks[i].size;
	fprintf(stdout, "next container %d\n", next_container);

	for (j = 0; j < next_container; j++) {
		i++;
		t = &toks[i];
		fprintf(stdout, "%d name %s\n", i, json_token_str(json, t));
		if (json_token_streq(json, t, "id") && t->size == 1) {
			i++;
			c->id = strdup(json_token_str(json, &toks[i]));
			c->exec.id = strdup(c->id);
			fprintf(stdout, "container id %s\n", c->id);
		} else if (json_token_streq(json, t, "cmd") && t->size == 1) {
			next = container_parse_cmd(c, json, &toks[i]);
			if (next < 0)
				return -1;
			i += next;
		} else if (json_token_streq(json, t, "rootfs") && t->size == 1) {
			i++;
			c->rootfs = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "container rootfs %s\n", c->rootfs);
		} else if (json_token_streq(json, t, "tty") && t->size == 1) {
			i++;
			c->exec.seq = json_token_ll(json, &toks[i]);
			fprintf(stdout, "container seq %" PRIu64 "\n", c->exec.seq);
		} else if (json_token_streq(json, t, "stderr") && t->size == 1) {
			i++;
			c->exec.errseq = json_token_ll(json, &toks[i]);
			fprintf(stdout, "container stderr seq %" PRIu64 "\n", c->exec.errseq);
		} else if (json_token_streq(json, t, "workdir") && t->size == 1) {
			i++;
			c->workdir = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "container workdir %s\n", c->workdir);
		} else if (json_token_streq(json, t, "image") && t->size == 1) {
			i++;
			c->image = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "container image %s\n", c->image);
		} else if (json_token_streq(json, t, "fstype") && t->size == 1) {
			i++;
			c->fstype = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "container fstype %s\n", c->fstype);
		} else if (json_token_streq(json, t, "volumes") && t->size == 1) {
			next = container_parse_volumes(c, json, &toks[i]);
			if (next < 0)
				return -1;
			i += next;
		} else if (json_token_streq(json, t, "fsmap") && t->size == 1) {
			next = container_parse_fsmap(c, json, &toks[i]);
			if (next < 0)
				return -1;
			i += next;
		} else if (json_token_streq(json, t, "envs") && t->size == 1) {
			next = container_parse_envs(c, json, &toks[i]);
			if (next < 0)
				return -1;
			i += next;
		} else if (json_token_streq(json, t, "restartPolicy") && t->size == 1) {
			i++;
/*
			if (json_token_streq(json, &toks[i], "always") && t->size == 1)
				c->exec.flags = POLICY_ALWAYS;
			else if (json_token_streq(json, &toks[i], "onFailure") && t->size == 1)
				c->exec.flags = POLICY_ONFAILURE;
			else
				c->exec.flags = POLICY_NEVER;
*/
			fprintf(stdout, "restartPolicy %s\n", json_token_str(json, &toks[i]));
		}
	}

	return i;
}

static int hyper_parse_containers(struct hyper_pod *pod, char *json, jsmntok_t *toks)
{
	int i = 1, j, next;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "format incorrect\n");
		return -1;
	}

	pod->remains = pod->c_num = toks[i].size;
	fprintf(stdout, "container count %d\n", pod->c_num);
	pod->c = calloc(pod->c_num, sizeof(*pod->c));

	if (pod->c == NULL) {
		fprintf(stdout, "alloc memory for container failed\n");
		return -1;
	}

	for (j = 0; j < pod->c_num; j++) {
		next = hyper_parse_container(pod, &pod->c[j], json, toks + i);
		if (next < 0)
			return -1;

		i += next;
	}

	return i;
}

static int hyper_parse_interfaces(struct hyper_pod *pod, char *json, jsmntok_t *toks)
{
	int i = 1, j, next_if;
	struct hyper_interface *iface;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "interfaces need array\n");
		return -1;
	}

	pod->i_num = toks[i].size;
	fprintf(stdout, "network interfaces num %d\n", pod->i_num);
	pod->iface = calloc(pod->i_num, sizeof(*iface));

	for (j = 0; j < pod->i_num; j++) {
		int i_if;

		iface = &pod->iface[j];
		i++;
		if (toks[i].type != JSMN_OBJECT) {
			fprintf(stdout, "network array need object\n");
			return -1;
		}
		next_if = toks[i].size;

		for (i_if = 0; i_if < next_if; i_if++) {
			i++;
			if (json_token_streq(json, &toks[i], "device")) {
				iface->device = strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "net device is %s\n", iface->device);
			} else if (json_token_streq(json, &toks[i], "ipAddress")) {
				iface->ipaddr = strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "net ipaddress is %s\n", iface->ipaddr);
			} else if (json_token_streq(json, &toks[i], "netMask")) {
				iface->mask = strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "net mask is %s\n", iface->mask);
			}
		}
	}

	return i;
}

static int hyper_parse_routes(struct hyper_pod *pod, char *json, jsmntok_t *toks)
{
	int i = 1, j, next_rt;
	struct hyper_route *rt;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "routes need array\n");
		return -1;
	}

	pod->r_num = toks[i].size;
	fprintf(stdout, "network routes num %d\n", pod->r_num);
	pod->rt = calloc(pod->r_num, sizeof(*rt));

	for (j = 0; j < pod->r_num; j++) {
		int i_rt;

		rt = &pod->rt[j];
		i++;
		if (toks[i].type != JSMN_OBJECT) {
			fprintf(stdout, "routes array need object\n");
			return -1;
		}
		next_rt = toks[i].size;

		for (i_rt = 0; i_rt < next_rt; i_rt++) {
			i++;
			if (json_token_streq(json, &toks[i], "dest")) {
				rt->dst = strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "route %d dest is %s\n", j, rt->dst);
			} else if (json_token_streq(json, &toks[i], "gateway")) {
				rt->gw = strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "route %d gateway is %s\n", j, rt->gw);
			} else if (json_token_streq(json, &toks[i], "device")) {
				rt->device = strdup(json_token_str(json, &toks[++i]));
				fprintf(stdout, "route %d device is %s\n", j, rt->device);
			}
		}
	}

	return i;
}

static int hyper_parse_dns(struct hyper_pod *pod, char *json, jsmntok_t *toks)
{
	int i = 1, j;

	if (toks[i].type != JSMN_ARRAY) {
		fprintf(stdout, "Dns format incorrect\n");
		return -1;
	}

	pod->d_num = toks[i].size;
	fprintf(stdout, "dns count %d\n", pod->d_num);
	pod->dns = calloc(pod->d_num, sizeof(*pod->dns));

	if (pod->dns == NULL) {
		fprintf(stdout, "alloc memory for container failed\n");
		return -1;
	}

	for (j = 0; j < pod->d_num; j++) {
		pod->dns[j] = strdup(json_token_str(json, &toks[++i]));
		fprintf(stdout, "pod dns %d: %s\n", j, pod->dns[j]);
	}

	return i;
}

int hyper_parse_pod(struct hyper_pod *pod, char *json, int length)
{
	int i, n, next = -1;
	jsmn_parser p;
	int toks_num = 100;
	jsmntok_t *toks = NULL;

realloc:
	toks = realloc(toks, toks_num * sizeof(jsmntok_t));

	fprintf(stdout, "call hyper_start_pod, json %s, len %d\n", json, length);
	jsmn_init(&p);
	n = jsmn_parse(&p, json, length, toks, toks_num);
	if (n < 0) {
		fprintf(stdout, "jsmn parse failed, n is %d\n", n);
		if (n == JSMN_ERROR_NOMEM) {
			toks_num *= 2;
			goto realloc;
		}

		goto out;
	}

	pod->policy = POLICY_NEVER;

	fprintf(stdout, "jsmn parse successed, n is %d\n", n);
	next = 0;
	for (i = 0; i < n; i++) {
		jsmntok_t *t = &toks[i];

		fprintf(stdout, "token %d, type is %d, size is %d\n", i, t->type, t->size);

		if (t->type != JSMN_STRING)
			continue;

		if (json_token_streq(json, t, "containers") && t->size == 1) {
			next = hyper_parse_containers(pod, json, t);
			if (next < 0)
				goto out;

			i += next;
		} else if (json_token_streq(json, t, "interfaces") && t->size == 1) {
			next = hyper_parse_interfaces(pod, json, t);
			if (next < 0)
				goto out;

			i += next;
		} else if (json_token_streq(json, t, "routes") && t->size == 1) {
			next = hyper_parse_routes(pod, json, t);
			if (next < 0)
				goto out;

			i += next;
		} else if (json_token_streq(json, t, "dns") && t->size == 1) {
			next = hyper_parse_dns(pod, json, t);
			if (next < 0)
				goto out;

			i += next;
		} else if (json_token_streq(json, t, "shareDir") && t->size == 1) {
			pod->tag = strdup(json_token_str(json, &toks[++i]));
			fprintf(stdout, "9p tag is %s\n", pod->tag);
		} else if (json_token_streq(json, t, "hostname") && t->size == 1) {
			pod->hostname = strdup(json_token_str(json, &toks[++i]));
			fprintf(stdout, "hostname is %s\n", pod->hostname);
		} else if (json_token_streq(json, t, "restartPolicy") && t->size == 1) {
			i++;
			if (json_token_streq(json, &toks[i], "always") && t->size == 1)
				pod->policy = POLICY_ALWAYS;
			else if (json_token_streq(json, &toks[i], "onFailure") && t->size == 1)
				pod->policy = POLICY_ONFAILURE;
			fprintf(stdout, "restartPolicy is %" PRIu8 "\n", pod->policy);
		}
	}

out:
	free(toks);
	return next;
}

int hyper_parse_winsize(struct hyper_win_size *ws, char *json, int length)
{
	int i, n, ret = 0;
	jsmn_parser p;
	int toks_num = 10;
	jsmntok_t *toks = NULL;

realloc:
	toks = realloc(toks, toks_num * sizeof(jsmntok_t));

	jsmn_init(&p);

	n = jsmn_parse(&p, json, length,  toks, toks_num);
	if (n < 0) {
		fprintf(stdout, "jsmn parse failed, n is %d\n", n);
		if (n == JSMN_ERROR_NOMEM) {
			toks_num *= 2;
			goto realloc;
		}

		goto fail;
	}

	for (i = 0; i < n; i++) {
		jsmntok_t *t = &toks[i];

		if (t->type != JSMN_STRING)
			continue;

		if (i++ == n)
			goto fail;
		if (json_token_streq(json, t, "tty")) {
			if (toks[i].type != JSMN_STRING)
				goto fail;
			ws->tty = strdup(json_token_str(json, &toks[i]));
		} else if (json_token_streq(json, t, "seq")) {
			if (toks[i].type != JSMN_PRIMITIVE)
				goto fail;
			ws->seq = json_token_ll(json, &toks[i]);
		} else if (json_token_streq(json, t, "row")) {
			if (toks[i].type != JSMN_PRIMITIVE)
				goto fail;
			ws->row = json_token_int(json, &toks[i]);
		} else if (json_token_streq(json, t, "column")) {
			if (toks[i].type != JSMN_PRIMITIVE)
				goto fail;
			ws->column = json_token_int(json, &toks[i]);
		}
	}
out:
	free(toks);
	return ret;
fail:
	ret = -1;
	goto out;
}

struct hyper_exec *hyper_parse_execcmd(char *json, int length)
{
	int i, j, n, has_seq = 0;
	struct hyper_exec *exec = NULL;
	char **argv = NULL;

	jsmn_parser p;
	int toks_num = 10;
	jsmntok_t *toks = NULL;

realloc:
	toks = realloc(toks, toks_num * sizeof(jsmntok_t));

	jsmn_init(&p);
	n = jsmn_parse(&p, json, length,  toks, toks_num);
	if (n < 0) {
		fprintf(stdout, "jsmn parse failed, n is %d\n", n);
		if (n == JSMN_ERROR_NOMEM) {
			toks_num *= 2;
			goto realloc;
		}
		goto out;
	}

	exec = calloc(1, sizeof(*exec));
	if (exec == NULL)
		goto out;

	exec->ptyfd = -1;
	exec->e.fd = -1;
	exec->errseq = 0;
	INIT_LIST_HEAD(&exec->list);

	for (i = 0, j = 0; i < n; i++) {
		jsmntok_t *t = &toks[i];

		if (t->type != JSMN_STRING)
			continue;

		if (json_token_streq(json, t, "container")) {
			if (i++ == n)
				goto fail;

			exec->id = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "get container %s\n", exec->id);
		} else if (json_token_streq(json, t, "seq")) {
			if (i++ == n)
				goto fail;
			has_seq = 1;
			exec->seq = json_token_ll(json, &toks[i]);
			fprintf(stdout, "get seq %"PRIu64"\n", exec->seq);
		} else if (json_token_streq(json, t, "cmd")) {
			if (i++ == n)
				goto fail;

			if (toks[i].type != JSMN_ARRAY) {
				fprintf(stdout, "execcmd need array\n");
				goto fail;
			}

			exec->argc = toks[i].size;
			argv = calloc(exec->argc + 1, sizeof(*argv));
			argv[exec->argc] = NULL;
		} else if (j < exec->argc) {
			argv[j++] = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "argv %d, %s\n", j - 1, argv[j - 1]);
		}
	}

	if (!has_seq) {
		fprintf(stderr, "execcmd format error, has no seq\n");
		goto fail;
	}
	exec->argv = argv;
out:
	free(toks);
	return exec;
fail:
	free(exec->id);
	for (i = 0; i < exec->argc; i++)
		free(argv[i]);

	free(exec->argv);
	free(exec);

	exec = NULL;
	goto out;
}

int hyper_parse_write_file(struct hyper_writter *writter, char *json, int length)
{
	int i, n, ret = 0;

	jsmn_parser p;
	int toks_num = 10;
	jsmntok_t *toks = NULL;

	toks = calloc(toks_num, sizeof(jsmntok_t));

	jsmn_init(&p);
	n = jsmn_parse(&p, json, length,  toks, toks_num);
	/* Must be json first */
	if (n <= 0) {
		fprintf(stdout, "jsmn parse failed, n is %d\n", n);
		ret = -1;
		goto out;
	}

	writter->len = length - toks[0].end;
	writter->data = malloc(writter->len);

	if (writter->data == NULL)
		goto fail;

	memcpy(writter->data, json + toks[0].end, writter->len);
	fprintf(stdout, "writefile get data len %d %s\n", writter->len, writter->data);

	for (i = 0; i < n; i++) {
		jsmntok_t *t = &toks[i];

		if (t->type != JSMN_STRING)
			continue;

		if (json_token_streq(json, t, "container")) {
			if (i++ == n)
				goto fail;

			writter->id = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "writefile get container %s\n", writter->id);
		} else if (json_token_streq(json, t, "file")) {
			if (i++ == n)
				goto fail;

			writter->file = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "writefile get file %s\n", writter->file);
		} else {
			fprintf(stdout, "in writefile incorrect %s\n", json_token_str(json, &toks[i]));
		}
	}
out:
	free(toks);
	return ret;
fail:
	free(writter->id);
	free(writter->file);
	free(writter->data);

	ret = -1;
	goto out;
}

int hyper_parse_read_file(struct hyper_reader *reader, char *json, int length)
{
	int i, n, ret = 0;

	jsmn_parser p;
	int toks_num = 10;
	jsmntok_t *toks = NULL;

	toks = calloc(toks_num, sizeof(jsmntok_t));
	if (toks == NULL) {
		fprintf(stderr, "fail to allocate tokens for read file cmd\n");
		ret = -1;
		goto out;
	}

	jsmn_init(&p);
	n = jsmn_parse(&p, json, length,  toks, toks_num);
	if (n < 0) {
		fprintf(stdout, "jsmn parse failed, n is %d\n", n);
		ret = -1;
		goto out;
	}

	for (i = 0; i < n; i++) {
		jsmntok_t *t = &toks[i];

		if (t->type != JSMN_STRING)
			continue;

		if (json_token_streq(json, t, "container")) {
			if (i++ == n)
				goto fail;

			reader->id = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "readfile get container %s\n", reader->id);
		} else if (json_token_streq(json, t, "file")) {
			if (i++ == n)
				goto fail;

			reader->file = strdup(json_token_str(json, &toks[i]));
			fprintf(stdout, "readfile get file %s\n", reader->file);
		} else {
			fprintf(stdout, "in readfile incorrect %s\n", json_token_str(json, &toks[i]));
		}
	}

out:
	free(toks);
	return ret;
fail:
	free(reader->id);
	free(reader->file);

	ret = -1;
	goto out;
}
