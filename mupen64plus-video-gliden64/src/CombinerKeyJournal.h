/* The combiner keys a session meets, written down as it meets them, so
 * that a crash does not lose what the keys file would only have got at
 * the end. C89.
 *
 * The GL thread hands a key over without waiting; a thread of the journal
 * does the file work. */
#ifndef COMBINER_KEY_JOURNAL_H
#define COMBINER_KEY_JOURNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct combiner_journal combiner_journal_t;

/* Appends to the journal at @path, made for @version. NULL when it cannot
 * be opened or the target has no lock-free atomics: keys then only reach
 * the file saved at the end. */
combiner_journal_t *combiner_journal_open(const char *path, uint32_t version);

/* The owner only. Never waits; 0 when the queue is full and @key is not
 * written (it is still saved at the end, on a clean exit). */
int combiner_journal_push(combiner_journal_t *journal, uint64_t key, unsigned hwl);

/* Writes what is queued and stops. */
void combiner_journal_close(combiner_journal_t *journal);

typedef void (*combiner_journal_key_t)(void *ctx, uint64_t key, unsigned hwl);

/* Calls @cb for each whole line of the journal at @path, once it was made
 * for @version. What a crash cut short is skipped. Returns the keys read. */
unsigned combiner_journal_read(const char *path, uint32_t version,
      combiner_journal_key_t cb, void *ctx);

void combiner_journal_remove(const char *path);

#ifdef __cplusplus
}
#endif

#endif
