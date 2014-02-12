#ifndef FILEWATCH_H_
#define FILEWATCH_H_

struct resman;
struct resource;

int resman_init_file_monitor(struct resman *rman);
void resman_destroy_file_monitor(struct resman *rman);

int resman_start_watch(struct resman *rman, struct resource *res);
void resman_stop_watch(struct resman *rman, struct resource *res);

void resman_check_watch(struct resman *rman);

#endif	/* FILEWATCH_H_ */
