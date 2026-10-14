#include "internal.h"
#include "hx_os_persistent_queue.h"
#include "hx_os_util.h"
#include "hx_os_file.h"

struct hx_os_persistent_message {
	hx_os_bytes_view data;
	char *id;
	unsigned char receipt[16], queue_id[16];
};

int hx_os_persistent_queue_available(void)
{
	return HX_OS_HAVE_PERSISTENT_QUEUE;
}

const char *hx_os_persistent_message_id(const hx_os_persistent_message *message)
{
	return message ? message->id : NULL;
}

hx_os_bytes_view hx_os_persistent_message_data(
	const hx_os_persistent_message *message)
{
	hx_os_bytes_view empty = {NULL, 0};

	return message ? message->data : empty;
}

void hx_os_persistent_message_destroy(hx_os_persistent_message **message)
{
	if (message) {
		hx_os_persistent_message *value = *message;

		*message = NULL;
		hx_os_memory_free(value);
	}
}

#if HX_OS_HAVE_PERSISTENT_QUEUE
#include <sqlite3.h>

struct hx_os_persistent_queue {
	sqlite3 *database;
	hx_os_mutex *mutex;
	hx_os_context *context;
	hx_os_persistent_options options;
	unsigned char identity[16];
	int busy;
};

static hx_os_status hx_os_store_error(
	hx_os_persistent_queue *queue, int code, hx_os_error *error)
{
	hx_os_status status = HX_OS_IO_ERROR;
	int primary = code & 255;

	if (code == SQLITE_OK || code == SQLITE_DONE || code == SQLITE_ROW)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (primary == SQLITE_BUSY || primary == SQLITE_LOCKED)
		status = HX_OS_BUSY;
	else if (primary == SQLITE_NOMEM)
		status = HX_OS_NO_MEMORY;
	else if (primary == SQLITE_READONLY || primary == SQLITE_PERM ||
		primary == SQLITE_AUTH)
		status = HX_OS_ACCESS_DENIED;
	else if (code == SQLITE_CONSTRAINT_TRIGGER)
		status = HX_OS_LIMIT;
	else if (primary == SQLITE_CONSTRAINT)
		status = HX_OS_EXISTS;

	(void)queue;
	return hx_os_i_error(error, status, HX_OS_ERROR_SQLITE, code);
}

static int hx_os_store_exec(hx_os_persistent_queue *queue, const char *sql)
{
	return sqlite3_exec(queue->database, sql, NULL, NULL, NULL);
}

static int hx_os_store_prepare(hx_os_persistent_queue *queue, const char *sql,
	sqlite3_stmt **statement)
{
	return sqlite3_prepare_v2(queue->database, sql, -1, statement, NULL);
}

static hx_os_status hx_os_store_enter(
	hx_os_persistent_queue *queue, hx_os_error *error)
{
	hx_os_status status;

	if (!queue)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(queue->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (queue->busy) {
		(void)hx_os_mutex_unlock(queue->mutex, NULL);
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	}
	queue->busy = 1;
	(void)sqlite3_busy_timeout(queue->database, 5000);

	return HX_OS_OK;
}

static hx_os_status hx_os_store_leave(
	hx_os_persistent_queue *queue, hx_os_status status, hx_os_error *error)
{
	queue->busy = 0;
	(void)hx_os_mutex_unlock(queue->mutex, NULL);
	if (status == HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	return status;
}

static hx_os_status hx_os_store_finish(
	hx_os_persistent_queue *queue, hx_os_status status, hx_os_error *error)
{
	if (status == HX_OS_OK)
		status = hx_os_store_error(
			queue, hx_os_store_exec(queue, "COMMIT"), error);
	if (status != HX_OS_OK)
		(void)hx_os_store_exec(queue, "ROLLBACK");

	return hx_os_store_leave(queue, status, error);
}

static int hx_os_store_id_valid(const char *id)
{
	size_t size = 0, invalid;

	if (!id)
		return 0;
	while (size <= 128 && id[size])
		++size;

	return size && size <= 128 &&
		hx_os_text_validate_utf8(id, size, &invalid) == HX_OS_OK;
}

static int hx_os_store_options_valid(const hx_os_persistent_options *options)
{
	return options->size >= sizeof(*options) && options->version == 1 &&
		options->max_messages && options->max_messages <= INT64_MAX &&
		options->max_content_bytes &&
		options->max_content_bytes <= INT64_MAX &&
		options->max_message_bytes &&
		options->max_message_bytes <= 64 * 1024 * 1024 &&
		options->max_message_bytes <= options->max_content_bytes &&
		options->lease_ms && options->lease_ms <= 86400000;
}

static int hx_os_store_schema(hx_os_persistent_queue *queue)
{
	static const char schema[] =
		"CREATE TABLE hxq_meta(k INTEGER PRIMARY KEY CHECK(k=1),"
		"version INTEGER NOT NULL,maximum INTEGER NOT NULL,"
		"max_bytes INTEGER NOT NULL,max_item INTEGER NOT NULL,"
		"entries INTEGER NOT NULL,total INTEGER NOT NULL,"
		"identity BLOB NOT NULL);"
		"CREATE TABLE hxq_record(seq INTEGER PRIMARY KEY AUTOINCREMENT,"
		"id TEXT NOT NULL UNIQUE,body BLOB NOT NULL,"
		"receipt BLOB,expires INTEGER NOT NULL DEFAULT 0);"
		"CREATE INDEX hxq_expiry ON hxq_record(expires,seq);"
		"CREATE TRIGGER hxq_limit BEFORE INSERT ON hxq_record WHEN "
		"(SELECT entries>=maximum OR total>max_bytes-length(NEW.body)"
		" OR length(NEW.body)>max_item FROM hxq_meta WHERE k=1)"
		" BEGIN SELECT RAISE(ABORT,'hxq capacity');END;"
		"CREATE TRIGGER hxq_insert AFTER INSERT ON hxq_record BEGIN "
		"UPDATE hxq_meta SET entries=entries+1,"
		"total=total+length(NEW.body) WHERE k=1;END;"
		"CREATE TRIGGER hxq_delete AFTER DELETE ON hxq_record BEGIN "
		"UPDATE hxq_meta SET entries=entries-1,"
		"total=total-length(OLD.body) WHERE k=1;END;"
		"PRAGMA application_id=0x48585051;";
	sqlite3_stmt *statement = NULL;
	int code = hx_os_store_exec(queue, schema);

	if (code == SQLITE_OK)
		code = hx_os_store_prepare(queue,
			"INSERT INTO hxq_meta VALUES(1,1,?,?,?,0,0,?)",
			&statement);
	if (code == SQLITE_OK) {
		(void)sqlite3_bind_int64(statement, 1,
			(sqlite3_int64)queue->options.max_messages);
		(void)sqlite3_bind_int64(statement, 2,
			(sqlite3_int64)queue->options.max_content_bytes);
		(void)sqlite3_bind_int64(
			statement, 3, queue->options.max_message_bytes);
		(void)sqlite3_bind_blob(
			statement, 4, queue->identity, 16, SQLITE_STATIC);
		code = sqlite3_step(statement);
	}
	(void)sqlite3_finalize(statement);

	return code;
}

static int hx_os_store_scalar(
	hx_os_persistent_queue *queue, const char *sql, sqlite3_int64 *value)
{
	sqlite3_stmt *statement = NULL;
	int code = hx_os_store_prepare(queue, sql, &statement);

	if (code == SQLITE_OK) {
		code = sqlite3_step(statement);
		if (code == SQLITE_ROW)
			*value = sqlite3_column_int64(statement, 0);
		else if (code == SQLITE_DONE)
			code = SQLITE_CORRUPT;
	}
	(void)sqlite3_finalize(statement);

	return code;
}

static hx_os_status hx_os_store_load_options(
	hx_os_persistent_queue *queue, int explicit_options, hx_os_error *error)
{
	sqlite3_stmt *statement = NULL;
	hx_os_persistent_options stored = HX_OS_PERSISTENT_OPTIONS_INIT;
	int code = hx_os_store_prepare(queue,
		"SELECT version,maximum,max_bytes,max_item,identity "
		"FROM hxq_meta WHERE k=1",
		&statement);
	hx_os_status status;

	if (code == SQLITE_OK)
		code = sqlite3_step(statement);
	status = hx_os_store_error(queue, code, error);
	if (code != SQLITE_ROW && status == HX_OS_OK)
		status = hx_os_i_error(error, HX_OS_IO_ERROR,
			HX_OS_ERROR_SQLITE, SQLITE_CORRUPT);
	if (code == SQLITE_ROW) {
		stored.version = (uint32_t)sqlite3_column_int(statement, 0);
		stored.max_messages =
			(uint64_t)sqlite3_column_int64(statement, 1);
		stored.max_content_bytes =
			(uint64_t)sqlite3_column_int64(statement, 2);
		stored.max_message_bytes =
			(uint32_t)sqlite3_column_int64(statement, 3);
		if (!hx_os_store_options_valid(&stored) ||
			sqlite3_column_bytes(statement, 4) != 16)
			status = hx_os_i_error(error, HX_OS_IO_ERROR,
				HX_OS_ERROR_SQLITE, SQLITE_CORRUPT);
		else if (explicit_options &&
			(stored.max_messages != queue->options.max_messages ||
				stored.max_content_bytes !=
					queue->options.max_content_bytes ||
				stored.max_message_bytes !=
					queue->options.max_message_bytes))
			status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		else {
			memcpy(queue->identity,
				sqlite3_column_blob(statement, 4), 16);
			queue->options.max_messages = stored.max_messages;
			queue->options.max_content_bytes =
				stored.max_content_bytes;
			queue->options.max_message_bytes =
				stored.max_message_bytes;
		}
	}
	(void)sqlite3_finalize(statement);

	return status;
}

hx_os_status hx_os_persistent_queue_open(hx_os_context *context,
	const char *path, const hx_os_persistent_options *options,
	hx_os_persistent_queue **out, hx_os_error *error)
{
	hx_os_persistent_queue *queue;
	hx_os_persistent_options defaults = HX_OS_PERSISTENT_OPTIONS_INIT;
	sqlite3_int64 application = 0, tables = 0;
	size_t invalid;
	hx_os_status status;
	int code;
	hx_os_file *created = NULL;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!options)
		options = &defaults;
	if (!path || !*path || !hx_os_store_options_valid(options))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_text_validate_utf8(path, strlen(path), &invalid) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);

	queue = hx_os_i_zero(context, sizeof(*queue));
	if (!queue)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	queue->context = context;
	queue->options = *options;
	status = hx_os_mutex_create(context, 1, &queue->mutex, error);
	if (status != HX_OS_OK)
		goto fail;
	status = hx_os_file_open(context, path,
		HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			HX_OS_FILE_EXCLUSIVE,
		&created, error);
	if (status == HX_OS_OK)
		status = hx_os_file_close(&created, error);
	else if (status == HX_OS_EXISTS)
		status = HX_OS_OK;
	if (status != HX_OS_OK)
		goto fail;
	code = sqlite3_open_v2(path, &queue->database,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
			SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_NOFOLLOW,
		NULL);
	if (code != SQLITE_OK) {
		status = hx_os_store_error(queue, code, error);
		goto fail;
	}
	(void)sqlite3_extended_result_codes(queue->database, 1);
	(void)sqlite3_busy_timeout(queue->database, 5000);
	/* A rollback journal bounds journal growth by the current transaction.
	 * EXTRA synchronizes directory removal at commit as well as data. */
	code = hx_os_store_exec(queue,
		"PRAGMA synchronous=EXTRA;"
		"PRAGMA cache_size=-1024;PRAGMA temp_store=MEMORY;"
		"PRAGMA mmap_size=0;BEGIN IMMEDIATE;");
	if (code != SQLITE_OK) {
		status = hx_os_store_error(queue, code, error);
		goto fail;
	}
	code = hx_os_store_scalar(queue, "PRAGMA application_id", &application);
	if (code == SQLITE_ROW && application == 0) {
		code = hx_os_store_scalar(queue,
			"SELECT count(*) FROM sqlite_schema WHERE type='table' "
			"AND name NOT LIKE 'sqlite_%'",
			&tables);
		if (code == SQLITE_ROW && tables == 0) {
			status = hx_os_random_bytes(queue->identity,
				sizeof(queue->identity), error);
			if (status != HX_OS_OK)
				goto rollback;
			code = hx_os_store_schema(queue);
		}
		else if (code == SQLITE_ROW)
			code = SQLITE_NOTADB;
	}
	else if (code == SQLITE_ROW && application != 0x48585051)
		code = SQLITE_NOTADB;
	status = hx_os_store_error(queue, code, error);
	if (status == HX_OS_OK)
		status = hx_os_store_load_options(
			queue, options != &defaults, error);
	if (status == HX_OS_OK)
		status = hx_os_store_error(
			queue, hx_os_store_exec(queue, "COMMIT"), error);
	if (status != HX_OS_OK)
		goto rollback;

	*out = queue;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);

rollback:
	(void)hx_os_store_exec(queue, "ROLLBACK");
fail:
	if (queue->database)
		(void)sqlite3_close(queue->database);
	hx_os_mutex_destroy(queue->mutex);
	hx_os_memory_free(queue);
	return status;
}

static hx_os_status hx_os_store_insert(hx_os_persistent_queue *queue,
	const hx_os_persistent_item *item, hx_os_error *error)
{
	sqlite3_stmt *statement = NULL;
	hx_os_status status;
	int code = hx_os_store_prepare(
		queue, "SELECT body FROM hxq_record WHERE id=?", &statement);

	if (code == SQLITE_OK) {
		(void)sqlite3_bind_text(
			statement, 1, item->id, -1, SQLITE_STATIC);
		code = sqlite3_step(statement);
	}
	if (code == SQLITE_ROW) {
		size_t size = (size_t)sqlite3_column_bytes(statement, 0);
		int same = size == item->data.size &&
			(!size ||
				!memcmp(sqlite3_column_blob(statement, 0),
					item->data.data, size));

		(void)sqlite3_finalize(statement);
		return hx_os_i_error(
			error, same ? HX_OS_OK : HX_OS_EXISTS, 0, 0);
	}
	status = hx_os_store_error(queue, code, error);
	(void)sqlite3_finalize(statement);
	if (status != HX_OS_OK)
		return status;

	code = hx_os_store_prepare(queue,
		"INSERT INTO hxq_record(id,body) VALUES(?,?)", &statement);
	if (code == SQLITE_OK) {
		(void)sqlite3_bind_text(
			statement, 1, item->id, -1, SQLITE_STATIC);
		/* A zero-byte message is an empty blob, not SQL NULL. */
		(void)sqlite3_bind_blob64(statement, 2,
			item->data.size ? item->data.data : "",
			(sqlite3_uint64)item->data.size, SQLITE_STATIC);
		code = sqlite3_step(statement);
	}
	status = hx_os_store_error(queue, code, error);
	(void)sqlite3_finalize(statement);

	return status;
}

hx_os_status hx_os_persistent_queue_push_many(hx_os_persistent_queue *queue,
	const hx_os_persistent_item *items, size_t count, hx_os_error *error)
{
	hx_os_status status;
	size_t i;

	if ((!items && count) || count > 1024)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_store_enter(queue, error);
	if (status != HX_OS_OK)
		return status;
	for (i = 0; i < count; ++i) {
		if (!hx_os_store_id_valid(items[i].id) ||
			(!items[i].data.data && items[i].data.size)) {
			status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
			break;
		}
		if (items[i].data.size > queue->options.max_message_bytes) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}
	}
	if (status != HX_OS_OK || !count)
		return hx_os_store_leave(queue, status, error);

	status = hx_os_store_error(
		queue, hx_os_store_exec(queue, "BEGIN IMMEDIATE"), error);
	for (i = 0; i < count && status == HX_OS_OK; ++i)
		status = hx_os_store_insert(queue, &items[i], error);

	return hx_os_store_finish(queue, status, error);
}

hx_os_status hx_os_persistent_queue_push(hx_os_persistent_queue *queue,
	const char *id, hx_os_bytes_view data, hx_os_error *error)
{
	hx_os_persistent_item item = {id, data};

	return hx_os_persistent_queue_push_many(queue, &item, 1, error);
}

static hx_os_status hx_os_store_clock(
	uint32_t lease_ms, int64_t *now, int64_t *until, hx_os_error *error)
{
	hx_os_status status = hx_os_wall_time_read(now, error);

	if (status != HX_OS_OK)
		return status;
	if (*now < 0 || *now > INT64_MAX - lease_ms)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	*until = *now + lease_ms;

	return HX_OS_OK;
}

hx_os_status hx_os_persistent_queue_take(hx_os_persistent_queue *queue,
	hx_os_persistent_message **out, hx_os_error *error)
{
	sqlite3_stmt *statement = NULL;
	hx_os_persistent_message *message = NULL;
	sqlite3_int64 sequence = 0;
	hx_os_status status;
	int64_t now = 0, until = 0;
	int code;
	size_t id_bytes, bytes, invalid;
	const void *id;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_store_enter(queue, error);
	if (status != HX_OS_OK)
		return status;
	(void)sqlite3_busy_timeout(queue->database, 0);
	status = hx_os_store_error(
		queue, hx_os_store_exec(queue, "BEGIN IMMEDIATE"), error);
	if (status == HX_OS_OK)
		status = hx_os_store_clock(
			queue->options.lease_ms, &now, &until, error);
	if (status != HX_OS_OK)
		return hx_os_store_finish(queue, status, error);
	code = hx_os_store_prepare(queue,
		"SELECT seq,id,body FROM hxq_record "
		"WHERE receipt IS NULL OR expires<=? ORDER BY seq "
		"LIMIT 1",
		&statement);
	if (code == SQLITE_OK) {
		(void)sqlite3_bind_int64(statement, 1, now);
		code = sqlite3_step(statement);
	}
	status = hx_os_store_error(queue, code, error);
	if (code == SQLITE_DONE)
		status = hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
	if (code == SQLITE_ROW) {
		sequence = sqlite3_column_int64(statement, 0);
		id = sqlite3_column_text(statement, 1);
		id_bytes = (size_t)sqlite3_column_bytes(statement, 1);
		bytes = (size_t)sqlite3_column_bytes(statement, 2);
		if (!id || !id_bytes || id_bytes > 128 ||
			bytes > queue->options.max_message_bytes ||
			memchr(id, 0, id_bytes) ||
			hx_os_text_validate_utf8(id, id_bytes, &invalid) !=
				HX_OS_OK)
			status = hx_os_i_error(error, HX_OS_IO_ERROR,
				HX_OS_ERROR_SQLITE, SQLITE_CORRUPT);
		else {
			message = hx_os_i_zero(queue->context,
				sizeof(*message) + id_bytes + 1 + bytes);
			if (!message)
				status = hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);
			else {
				message->id = (char *)(message + 1);
				memcpy(message->id, id, id_bytes);
				message->data.data = message->id + id_bytes + 1;
				message->data.size = bytes;
				if (bytes)
					memcpy((void *)message->data.data,
						sqlite3_column_blob(
							statement, 2),
						bytes);
				memcpy(message->queue_id, queue->identity, 16);
				status = hx_os_random_bytes(message->receipt,
					sizeof(message->receipt), error);
			}
		}
	}
	(void)sqlite3_finalize(statement);
	statement = NULL;
	if (status == HX_OS_OK) {
		code = hx_os_store_prepare(queue,
			"UPDATE hxq_record SET receipt=?,expires=? WHERE seq=?",
			&statement);
		if (code == SQLITE_OK) {
			(void)sqlite3_bind_blob(statement, 1, message->receipt,
				16, SQLITE_STATIC);
			(void)sqlite3_bind_int64(statement, 2, until);
			(void)sqlite3_bind_int64(statement, 3, sequence);
			code = sqlite3_step(statement);
		}
		status = hx_os_store_error(queue, code, error);
	}
	(void)sqlite3_finalize(statement);
	status = hx_os_store_finish(queue, status, error);
	if (status == HX_OS_OK)
		*out = message;
	else
		hx_os_persistent_message_destroy(&message);

	return status;
}

static hx_os_status hx_os_store_receipt(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *message, int operation, int64_t now,
	int64_t until, hx_os_error *error)
{
	static const char *const sql[] = {
		"DELETE FROM hxq_record WHERE id=? AND receipt=? AND expires>?",
		"UPDATE hxq_record SET receipt=NULL,expires=0 "
		"WHERE id=? AND receipt=?",
		"UPDATE hxq_record SET expires=?4 "
		"WHERE id=?1 AND receipt=?2 AND expires>?3"};
	sqlite3_stmt *statement = NULL;
	int code = hx_os_store_prepare(queue, sql[operation], &statement);
	hx_os_status status;

	if (code == SQLITE_OK) {
		(void)sqlite3_bind_text(
			statement, 1, message->id, -1, SQLITE_STATIC);
		(void)sqlite3_bind_blob(
			statement, 2, message->receipt, 16, SQLITE_STATIC);
		if (operation != 1)
			(void)sqlite3_bind_int64(statement, 3, now);
		if (operation == 2)
			(void)sqlite3_bind_int64(statement, 4, until);
		code = sqlite3_step(statement);
	}
	status = hx_os_store_error(queue, code, error);
	if (status == HX_OS_OK && sqlite3_changes(queue->database) != 1)
		status = hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	(void)sqlite3_finalize(statement);

	return status;
}

static hx_os_status hx_os_store_receipts(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *const *messages, size_t count,
	int operation, uint32_t lease_ms, hx_os_error *error)
{
	hx_os_status status;
	int64_t now = 0, until = 0;
	size_t i;

	if ((!messages && count) || count > 1024 ||
		(operation == 2 && (!lease_ms || lease_ms > 86400000)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_store_enter(queue, error);
	if (status != HX_OS_OK)
		return status;
	for (i = 0; i < count; ++i)
		if (!messages[i] ||
			memcmp(messages[i]->queue_id, queue->identity, 16)) {
			status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
			break;
		}
	if (status != HX_OS_OK || !count)
		return hx_os_store_leave(queue, status, error);
	status = hx_os_store_error(
		queue, hx_os_store_exec(queue, "BEGIN IMMEDIATE"), error);
	if (status == HX_OS_OK)
		status = hx_os_store_clock(lease_ms, &now, &until, error);
	for (i = 0; i < count && status == HX_OS_OK; ++i)
		status = hx_os_store_receipt(
			queue, messages[i], operation, now, until, error);

	return hx_os_store_finish(queue, status, error);
}

hx_os_status hx_os_persistent_queue_ack_many(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *const *messages, size_t count,
	hx_os_error *error)
{
	return hx_os_store_receipts(queue, messages, count, 0, 0, error);
}

hx_os_status hx_os_persistent_queue_ack(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *message, hx_os_error *error)
{
	return hx_os_store_receipts(queue, &message, 1, 0, 0, error);
}

hx_os_status hx_os_persistent_queue_release(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *message, hx_os_error *error)
{
	return hx_os_store_receipts(queue, &message, 1, 1, 0, error);
}

hx_os_status hx_os_persistent_queue_renew(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *message, uint32_t lease_ms,
	hx_os_error *error)
{
	return hx_os_store_receipts(queue, &message, 1, 2, lease_ms, error);
}

hx_os_status hx_os_persistent_queue_stats(hx_os_persistent_queue *queue,
	hx_os_persistent_stats *out, hx_os_error *error)
{
	hx_os_persistent_stats result;
	sqlite3_stmt *statement = NULL;
	hx_os_status status;
	int64_t now = 0, unused = 0;
	int code;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_store_enter(queue, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_store_clock(0, &now, &unused, error);
	if (status != HX_OS_OK)
		return hx_os_store_leave(queue, status, error);
	code = hx_os_store_prepare(queue,
		"SELECT entries,total,maximum,max_bytes,"
		"(SELECT count(*) FROM hxq_record "
		"WHERE receipt IS NOT NULL AND expires>?) "
		"FROM hxq_meta WHERE k=1",
		&statement);
	if (code == SQLITE_OK) {
		(void)sqlite3_bind_int64(statement, 1, now);
		code = sqlite3_step(statement);
	}
	status = hx_os_store_error(queue, code, error);
	if (code == SQLITE_ROW) {
		result.messages = (uint64_t)sqlite3_column_int64(statement, 0);
		result.content_bytes =
			(uint64_t)sqlite3_column_int64(statement, 1);
		result.max_messages =
			(uint64_t)sqlite3_column_int64(statement, 2);
		result.max_content_bytes =
			(uint64_t)sqlite3_column_int64(statement, 3);
		result.in_flight = (uint64_t)sqlite3_column_int64(statement, 4);
		*out = result;
	}
	else if (status == HX_OS_OK)
		status = hx_os_i_error(error, HX_OS_IO_ERROR,
			HX_OS_ERROR_SQLITE, SQLITE_CORRUPT);
	(void)sqlite3_finalize(statement);

	return hx_os_store_leave(queue, status, error);
}

hx_os_status hx_os_persistent_queue_close(
	hx_os_persistent_queue **queue, hx_os_error *error)
{
	hx_os_persistent_queue *value;
	hx_os_status status;

	if (!queue)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *queue;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_store_enter(value, error);
	if (status != HX_OS_OK)
		return status;
	status =
		hx_os_store_error(value, sqlite3_close(value->database), error);
	status = hx_os_store_leave(value, status, error);
	if (status == HX_OS_OK) {
		hx_os_mutex_destroy(value->mutex);
		*queue = NULL;
		hx_os_memory_free(value);
	}

	return status;
}
#else

hx_os_status hx_os_persistent_queue_open(hx_os_context *context,
	const char *path, const hx_os_persistent_options *options,
	hx_os_persistent_queue **out, hx_os_error *error)
{
	(void)context;
	(void)path;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_push_many(hx_os_persistent_queue *queue,
	const hx_os_persistent_item *items, size_t count, hx_os_error *error)
{
	(void)queue;
	(void)items;
	(void)count;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_push(hx_os_persistent_queue *queue,
	const char *id, hx_os_bytes_view data, hx_os_error *error)
{
	(void)queue;
	(void)id;
	(void)data;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_take(hx_os_persistent_queue *queue,
	hx_os_persistent_message **out, hx_os_error *error)
{
	(void)queue;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_ack_many(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *const *messages, size_t count,
	hx_os_error *error)
{
	(void)queue;
	(void)messages;
	(void)count;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_ack(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *message, hx_os_error *error)
{
	(void)queue;
	(void)message;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_release(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *message, hx_os_error *error)
{
	(void)queue;
	(void)message;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_renew(hx_os_persistent_queue *queue,
	const hx_os_persistent_message *message, uint32_t lease_ms,
	hx_os_error *error)
{
	(void)queue;
	(void)message;
	(void)lease_ms;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_stats(hx_os_persistent_queue *queue,
	hx_os_persistent_stats *out, hx_os_error *error)
{
	(void)queue;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_persistent_queue_close(
	hx_os_persistent_queue **queue, hx_os_error *error)
{
	if (queue && !*queue)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

#endif

hx_os_status hx_os_persistent_queue_take_until(hx_os_persistent_queue *queue,
	hx_os_deadline deadline, hx_os_cancel_token *cancel,
	hx_os_persistent_message **out, hx_os_error *error)
{
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	for (;;) {
		hx_os_deadline sleep;

		if (hx_os_cancel_requested(cancel))
			return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
		status = hx_os_persistent_queue_take(queue, out, error);
		if (status != HX_OS_WOULD_BLOCK && status != HX_OS_BUSY)
			return status;
		if (deadline == HX_OS_DEADLINE_NOW)
			return status;
		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);

		sleep = hx_os_deadline_after_ms(10);
		if (sleep > deadline)
			sleep = deadline;
		status = hx_os_sleep_cancellable_until(sleep, cancel, error);
		if (status != HX_OS_OK)
			return status;
	}
}
