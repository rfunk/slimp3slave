/*
 * util.h:
 *
 */

#ifndef __UTIL_H_ /* include guard */
#define __UTIL_H_

void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *w, size_t n);

#endif /* __UTIL_H_ */
