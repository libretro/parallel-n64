/* The combiner keys a session meets, written down as it meets them, so
 * that a crash does not lose what the keys file would only have got at
 * the end. C89.
 *
 * The GL thread hands a key over without waiting; a thread of the journal
 * does the file work. */
#ifndef COMBINER_KEY_JOURNAL_H
#define COMBINER_KEY_JOURNAL_H

#include <stdint.h>
#include <streams/file_stream.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct combiner_journal combiner_journal_t;

/* Appends to the journal at @path, made for @version. NULL when it cannot
 * be opened or the target has no lock-free atomics: keys then only reach
 * the file saved at the end. */
combiner_journal_t *combiner_journal_open(const char *path, uint32_t version);

/* A key record of a file of records: its kind, the four bits, then the
 * key in the host's byte order */
#define COMBINER_JOURNAL_RECORD_KEY  'K'
#define COMBINER_JOURNAL_RECORD_SIZE 10

/* Appends key records to the file of records at @path, which must be
 * whole: opened on the journal's thread when the first key comes, so a
 * session that meets none never opens it. NULL when no thread can be had
 * or the target has no lock-free atomics. */
combiner_journal_t *combiner_journal_append(const char *path);

/* Writes what is queued and stops, handing over the file it appended to,
 * at its end, or NULL where it has none. @whole is set when every key
 * pushed is in the file; when @journal is NULL it is not. */
RFILE *combiner_journal_detach(combiner_journal_t *journal, int *whole);

/* The owner only. Never waits; 0 when the queue is full and @key is not
 * written (it is still saved at the end, on a clean exit). @hwl is up to
 * four bits of whatever else tells the plugin's keys apart. */
int combiner_journal_push(combiner_journal_t *journal, uint64_t key, unsigned hwl);

/* Writes what is queued and stops. */
void combiner_journal_close(combiner_journal_t *journal);

typedef void (*combiner_journal_key_t)(void *ctx, uint64_t key, unsigned hwl);

/* Calls @cb for each whole line of the journal at @path, once it was made
 * for @version. What a crash cut short is skipped. Returns the keys read. */
unsigned combiner_journal_read(const char *path, uint32_t version,
      combiner_journal_key_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
