#define _POSIX_C_SOURCE 200809L

#include "irm_adapter.h"
#include "irm_database.h"

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct remote_file {
	char name[512];
	time_t mtime;
	uint64_t size;
};

struct options {
	const char *host;
	int port;
	const char *user;
	const char *password_env;
	const char *known_hosts;
	const char *remote_root;
	const char *db_dir;
	const char *state_path;
	const char *local_file;
	unsigned int skip_lines;
	int verbose;
};

static void verbosef(const struct options *options, const char *format, ...)
{
	va_list arguments;
	if (!options->verbose) return;
	fputs("irm-sync: ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

static void usage(const char *program)
{
	fprintf(stderr, "Usage: %s --db-dir PATH [--file PATH | --host HOST --user USER]\n", program);
	fprintf(stderr, "  --password-env NAME --known-hosts PATH --remote-root PATH\n");
	fprintf(stderr, "  --verbose\n");
}

static int parse_uint_option(const char *text, unsigned int maximum,
	unsigned int *value)
{
	char *end;
	unsigned long parsed;
	if (!text || !*text || !value) return 0;
	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (end == text || *end != '\0' || errno == ERANGE || parsed > maximum)
		return 0;
	*value = (unsigned int)parsed;
	return 1;
}

static int read_state(const char *path, struct remote_file *file)
{
	FILE *stream;
	char name[sizeof(file->name)];
	long long mtime;
	unsigned long long size;
	stream = fopen(path, "r");
	if (!stream) return 0;
	if (fscanf(stream, "%511s %lld %llu", name, &mtime, &size) == 3) {
		strncpy(file->name, name, sizeof(file->name) - 1);
		file->name[sizeof(file->name) - 1] = '\0';
		file->mtime = (time_t)mtime;
		file->size = (uint64_t)size;
		fclose(stream);
		return 1;
	}
	fclose(stream);
	return 0;
}

static int write_state(const char *path, const struct remote_file *file)
{
	char temporary[4096];
	FILE *stream;
	if (snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary)) return 0;
	stream = fopen(temporary, "w");
	if (!stream) return 0;
	if (fprintf(stream, "%s %lld %llu\n", file->name, (long long)file->mtime,
		(unsigned long long)file->size) < 0 || fflush(stream) != 0 ||
		fsync(fileno(stream)) != 0) {
		(void)fclose(stream);
		unlink(temporary);
		return 0;
	}
	if (fclose(stream) != 0) {
		unlink(temporary);
		return 0;
	}
	if (rename(temporary, path) != 0) {
		unlink(temporary);
		return 0;
	}
	return 1;
}

static int better_file(const struct remote_file *candidate, const struct remote_file *current)
{
	if (!current->name[0]) return 1;
	if (candidate->mtime != current->mtime) return candidate->mtime > current->mtime;
	return strcmp(candidate->name, current->name) > 0;
}

static int archive_path(char *buffer, size_t length, const char *root)
{
	if (root && root[0])
		return snprintf(buffer, length, "%s/archive", root) < (int)length;
	return snprintf(buffer, length, "archive") < (int)length;
}

static int list_sftp_file(const struct options *options, struct remote_file *selected,
	ssh_session *session_out, sftp_session *sftp_out)
{
	ssh_session session = NULL;
	sftp_session sftp = NULL;
	sftp_dir directory = NULL;
	sftp_attributes attributes;
	char directory_path[1024];
	const char *password;
	int strict = 1;
	memset(selected, 0, sizeof(*selected));
	verbosef(options, "connecting to SFTP %s:%d as %s", options->host,
		options->port, options->user);
	session = ssh_new();
	if (!session) { verbosef(options, "unable to allocate SSH session"); return 0; }
	ssh_options_set(session, SSH_OPTIONS_HOST, options->host);
	ssh_options_set(session, SSH_OPTIONS_PORT, &options->port);
	ssh_options_set(session, SSH_OPTIONS_USER, options->user);
	if (options->known_hosts) ssh_options_set(session, SSH_OPTIONS_KNOWNHOSTS, options->known_hosts);
	if (ssh_connect(session) != SSH_OK) {
		verbosef(options, "SFTP connection failed: %s", ssh_get_error(session));
		goto fail;
	}
	verbosef(options, "SFTP connection established");
	if (options->known_hosts) {
		enum ssh_known_hosts_e known = ssh_session_is_known_server(session);
		if (known != SSH_KNOWN_HOSTS_OK) {
			verbosef(options, "host-key verification failed (status %d)", known);
			goto fail;
		}
		verbosef(options, "host-key verification succeeded using %s", options->known_hosts);
	} else if (ssh_options_set(session, SSH_OPTIONS_STRICTHOSTKEYCHECK, &strict) != SSH_OK) {
		verbosef(options, "unable to enable strict host-key checking");
		goto fail;
	}
	password = options->password_env ? getenv(options->password_env) : NULL;
	if (!password) {
		verbosef(options, "password environment variable %s is not set", options->password_env);
		goto fail;
	}
	if (ssh_userauth_password(session, NULL, password) != SSH_AUTH_SUCCESS) {
		verbosef(options, "SFTP password authentication failed");
		goto fail;
	}
	verbosef(options, "SFTP authentication succeeded");
	sftp = sftp_new(session);
	if (!sftp || sftp_init(sftp) != SSH_OK) {
		verbosef(options, "SFTP subsystem initialization failed");
		goto fail;
	}
	if (!archive_path(directory_path, sizeof(directory_path), options->remote_root)) {
		verbosef(options, "remote archive path is too long");
		goto fail;
	}
	verbosef(options, "scanning remote directory %s", directory_path);
	directory = sftp_opendir(sftp, directory_path);
	if (!directory) {
		verbosef(options, "unable to open remote directory %s", directory_path);
		goto fail;
	}
	while ((attributes = sftp_readdir(sftp, directory)) != NULL) {
		struct remote_file candidate;
		if (!attributes->name || strncmp(attributes->name, "inventory_", 9) != 0 ||
			attributes->type == SSH_FILEXFER_TYPE_DIRECTORY) {
			sftp_attributes_free(attributes);
			continue;
		}
		memset(&candidate, 0, sizeof(candidate));
		strncpy(candidate.name, attributes->name, sizeof(candidate.name) - 1);
		candidate.mtime = attributes->mtime;
		candidate.size = attributes->size;
		candidate.name[sizeof(candidate.name) - 1] = '\0';
		if (better_file(&candidate, selected)) *selected = candidate;
		sftp_attributes_free(attributes);
	}
	if (!sftp_dir_eof(directory)) {
		verbosef(options, "remote directory scan failed");
		goto fail;
	}
	sftp_closedir(directory);
	if (!selected->name[0]) {
		verbosef(options, "no matching inventory_ file found");
		goto fail;
	}
	verbosef(options, "selected %s (%llu bytes, mtime %lld)", selected->name,
		(unsigned long long)selected->size, (long long)selected->mtime);
	*session_out = session;
	*sftp_out = sftp;
	return 1;
fail:
	if (directory) sftp_closedir(directory);
	if (sftp) sftp_free(sftp);
	if (session) {
		ssh_disconnect(session);
		ssh_free(session);
	}
	return 0;
}

static int download_sftp_file(const struct options *options, const struct remote_file *file,
	ssh_session session, sftp_session sftp, const char *local_path)
{
	sftp_file remote;
	char remote_path[1024];
	char temporary[4096];
	char buffer[32768];
	int output;
	ssize_t received;
	uint64_t total = 0;
	if (!archive_path(remote_path, sizeof(remote_path), options->remote_root) ||
		snprintf(remote_path + strlen(remote_path), sizeof(remote_path) - strlen(remote_path), "/%s",
		file->name) >= (int)(sizeof(remote_path) - strlen(remote_path)) ||
		snprintf(temporary, sizeof(temporary), "%s.part", local_path) >= (int)sizeof(temporary)) {
		verbosef(options, "download path is too long");
		return 0;
	}
	verbosef(options, "downloading %s to temporary local file %s", remote_path, temporary);
	remote = sftp_open(sftp, remote_path, O_RDONLY, 0);
	if (!remote) { verbosef(options, "unable to open remote file %s", remote_path); return 0; }
	output = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (output < 0) { verbosef(options, "unable to open local temporary file %s", temporary); sftp_close(remote); return 0; }
	while ((received = sftp_read(remote, buffer, sizeof(buffer))) > 0) {
		total += (uint64_t)received;
		ssize_t written = 0;
		while (written < received) {
			ssize_t result = write(output, buffer + written, (size_t)(received - written));
			if (result <= 0) { verbosef(options, "local write failed while downloading %s", remote_path); close(output); sftp_close(remote); unlink(temporary); return 0; }
			written += result;
		}
	}
	if (received < 0 || fsync(output) != 0) {
		(void)close(output);
		(void)sftp_close(remote);
		unlink(temporary);
		verbosef(options, "download or local publication failed for %s", remote_path);
		return 0;
	}
	if (close(output) != 0) {
		(void)sftp_close(remote);
		unlink(temporary);
		verbosef(options, "download or local publication failed for %s", remote_path);
		return 0;
	}
	output = -1;
	if (sftp_close(remote) != SSH_OK) {
		unlink(temporary);
		verbosef(options, "download or local publication failed for %s", remote_path);
		return 0;
	}
	if (rename(temporary, local_path) != 0) {
		unlink(temporary);
		verbosef(options, "download or local publication failed for %s", remote_path);
		return 0;
	}
	verbosef(options, "downloaded and published %llu bytes", (unsigned long long)total);
	(void)session;
	return 1;
}

static int load_file(const char *path, unsigned int skip_lines, struct irm_record **records,
	unsigned int *count)
{
	FILE *stream;
	char *line = NULL;
	size_t capacity = 0;
	size_t length;
	unsigned int line_number = 0;
	unsigned int used = 0;
	struct irm_record *items = NULL;
	stream = fopen(path, "rb");
	if (!stream) return 0;
	while (getline(&line, &length, stream) >= 0) {
		struct irm_record record;
		char error[128];
		++line_number;
		if (line_number <= skip_lines) continue;
		if (!irm_parse_record(line, strlen(line), &record, error, sizeof(error))) {
			fprintf(stderr, "invalid IRM record at line %u: %s\n", line_number, error);
			free(line); free(items); fclose(stream); return 0;
		}
		if (used == capacity) {
			if (capacity > UINT_MAX / 2U / sizeof(*items)) {
				free(line); free(items); fclose(stream); return 0;
			}
			unsigned int next = capacity ? capacity * 2U : 256U;
			struct irm_record *grown = realloc(items, next * sizeof(*items));
			if (!grown) { free(line); free(items); fclose(stream); return 0; }
			items = grown; capacity = next;
		}
		items[used++] = record;
	}
	if (ferror(stream)) {
		free(line); free(items); fclose(stream); return 0;
	}
	free(line);
	fclose(stream);
	*records = items;
	*count = used;
	return 1;
}

int main(int argc, char **argv)
{
	struct options options = {0};
	struct remote_file previous = {0};
	struct remote_file selected = {0};
	struct irm_record *records = NULL;
	unsigned int count = 0;
	char local_file[4096];
	char lock_path[4096];
	char error[256];
	char media_db_path[4096];
	char library_db_path[4096];
	char media_index_db_path[4096];
	int lock_fd;
	int option;
	static const struct option long_options[] = {
		{"host", required_argument, 0, 'h'}, {"port", required_argument, 0, 'p'},
		{"user", required_argument, 0, 'u'}, {"password-env", required_argument, 0, 'P'},
		{"known-hosts", required_argument, 0, 'k'}, {"remote-root", required_argument, 0, 'r'},
		{"db-dir", required_argument, 0, 'b'}, {"state", required_argument, 0, 's'},
		{"file", required_argument, 0, 'f'}, {"skip-lines", required_argument, 0, 'n'},
		{"verbose", no_argument, 0, 'v'},
		{"help", no_argument, 0, '?'}, {0, 0, 0, 0}
	};
	options.port = 22;
	options.password_env = "IRONMOUNTAIN_SFTP_PASSWORD";
	options.remote_root = "";
	options.skip_lines = 1;
	while ((option = getopt_long(argc, argv, "h:p:u:P:k:r:b:s:f:n:v?", long_options, NULL)) != -1) {
		switch (option) {
		case 'h': options.host = optarg; break;
		case 'p': { unsigned int value; if (!parse_uint_option(optarg, 65535U, &value) || value == 0) return EXIT_FAILURE; options.port = (int)value; break; }
		case 'u': options.user = optarg; break; case 'P': options.password_env = optarg; break;
		case 'k': options.known_hosts = optarg; break; case 'r': options.remote_root = optarg; break;
		case 'b': options.db_dir = optarg; break;
		case 's': options.state_path = optarg; break;
		case 'f': options.local_file = optarg; break;
		case 'n': if (!parse_uint_option(optarg, UINT_MAX, &options.skip_lines)) return EXIT_FAILURE; break;
		case 'v': options.verbose = 1; break;
		default: usage(argv[0]); return option == '?' ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}
	if (!options.db_dir) { usage(argv[0]); return EXIT_FAILURE; }
	if (snprintf(media_db_path, sizeof(media_db_path), "%s/IRMMEDIA.db", options.db_dir) >= (int)sizeof(media_db_path) ||
		snprintf(library_db_path, sizeof(library_db_path), "%s/IRMLIBRARY.db", options.db_dir) >= (int)sizeof(library_db_path) ||
		snprintf(media_index_db_path, sizeof(media_index_db_path), "%s/IRMMEDIAID.db", options.db_dir) >= (int)sizeof(media_index_db_path)) {
		fprintf(stderr, "database directory path is too long\n");
		return EXIT_FAILURE;
	}
	error[0] = '\0';
	verbosef(&options, "starting import; database directory=%s", options.db_dir);
	if (snprintf(lock_path, sizeof(lock_path), "%s.lock", media_db_path) >= (int)sizeof(lock_path)) return EXIT_FAILURE;
	lock_fd = open(lock_path, O_CREAT | O_RDWR, 0644);
	if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) { fprintf(stderr, "database is locked\n"); return EXIT_FAILURE; }
	if (fchmod(lock_fd, 0644) != 0) { fprintf(stderr, "unable to set database lock permissions\n"); close(lock_fd); return EXIT_FAILURE; }
	if (options.local_file) {
		verbosef(&options, "using local file %s; SFTP discovery is disabled", options.local_file);
		strncpy(local_file, options.local_file, sizeof(local_file) - 1);
		local_file[sizeof(local_file) - 1] = '\0';
		strncpy(selected.name, options.local_file, sizeof(selected.name) - 1);
		selected.name[sizeof(selected.name) - 1] = '\0';
	} else {
		ssh_session session = NULL;
		sftp_session sftp = NULL;
		if (!options.host || !options.user || !options.known_hosts || !options.state_path ||
			!list_sftp_file(&options, &selected, &session, &sftp)) { fprintf(stderr, "SFTP discovery failed\n"); close(lock_fd); return EXIT_FAILURE; }
		if (read_state(options.state_path, &previous)) {
			verbosef(&options, "checkpoint found: %s (%llu bytes, mtime %lld)", previous.name,
				(unsigned long long)previous.size, (long long)previous.mtime);
		} else {
			verbosef(&options, "no previous checkpoint found");
		}
		if (previous.name[0] && previous.mtime == selected.mtime &&
			previous.size == selected.size && strcmp(previous.name, selected.name) == 0) {
			verbosef(&options, "selected file matches checkpoint; skipping download and database publication");
			fprintf(stdout, "no new inventory file\n"); sftp_free(sftp); ssh_disconnect(session); ssh_free(session); close(lock_fd); return EXIT_SUCCESS;
		}
		verbosef(&options, "selected file differs from checkpoint; importing it");
		if (snprintf(local_file, sizeof(local_file), "%s.download", media_db_path) >= (int)sizeof(local_file) ||
			!download_sftp_file(&options, &selected, session, sftp, local_file)) { fprintf(stderr, "SFTP download failed\n"); sftp_free(sftp); ssh_disconnect(session); ssh_free(session); close(lock_fd); return EXIT_FAILURE; }
		sftp_free(sftp); ssh_disconnect(session); ssh_free(session);
		verbosef(&options, "SFTP session closed");
	}
	verbosef(&options, "parsing inventory file %s", local_file);
	if (!load_file(local_file, options.skip_lines, &records, &count) ||
		!irm_database_load_snapshot(media_db_path, library_db_path, media_index_db_path,
			records, count,
			error, sizeof(error))) {
		fprintf(stderr, "import failed: %s\n", error[0] ? error : "unable to load file"); free(records); close(lock_fd); return EXIT_FAILURE;
	}
	verbosef(&options, "parsed %u records and published database snapshot", count);
	if (!options.local_file && options.state_path && !write_state(options.state_path, &selected)) {
		fprintf(stderr, "warning: database loaded but state was not updated\n");
	} else if (!options.local_file && options.state_path) {
		verbosef(&options, "checkpoint updated to %s", selected.name);
	}
	fprintf(stdout, "loaded %u records\n", count);
	free(records);
	close(lock_fd);
	return EXIT_SUCCESS;
}
