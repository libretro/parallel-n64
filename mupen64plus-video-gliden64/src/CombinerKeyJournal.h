/* The combiner keys a session meets, written down as it meets them, so
 * that a crash does not lose what the keys file would only have got at
 * the end. C89.
 *
 * A journal of lines can be one every game shares (the N64's combiners
 * are fixed function: a key the same whatever the game): a session that
 * meets a key starts its lines with a line of its game (@ and the 16 hex
 * digits of its CRCs), and a game reads only the keys of its lines.
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

/* Appends to the journal at @path, made for @version where it is not
 * there: opened on the journal's thread when the first key comes, so a
 * session that meets none never touches it. NULL when no thread can be
 * had or the target has no lock-free atomics: keys then only reach the
 * file saved at the end. */
combiner_journal_t *combiner_journal_open(const char *path, uint32_t version);

/* The hex digits that name a game's lines */
#define COMBINER_JOURNAL_GAME 16

/* As combiner_journal_open, the lines this session writes after a line
 * of @game (COMBINER_JOURNAL_GAME hex digits) */
combiner_journal_t *combiner_journal_open_game(const char *path, uint32_t version,
      const char *game);

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

/* As combiner_journal_read, only the keys of @game's lines */
unsigned combiner_journal_read_game(const char *path, uint32_t version, const char *game,
      combiner_journal_key_t cb, void *ctx);

/* Where @path is not there: the files of before in @folder, a game's each
 * (@prefix, its CRCs in COMBINER_JOURNAL_GAME hex digits, .keys), made for
 * @old_version, read into a journal of lines made for @version at @path,
 * each in lines of its game; once it is written they are all removed,
 * those of another version too. Whether there were any. */
int combiner_journal_gather(const char *path, uint32_t version, const char *folder,
      const char *prefix, uint32_t old_version);

#ifdef __cplusplus
}
#endif

#endif
