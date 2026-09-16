#define _POSIX_C_SOURCE 200809L

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    guint port;
    gchar *cors_origin;
    gchar *video_directory;
    gchar *canonical_video_directory;
    GMainLoop *loop;
} ApiConfig;

typedef struct {
    gchar *name;
    gchar *relative_path;
} VideoEntry;

static gboolean handle_signal(gpointer user_data)
{
    GMainLoop *loop = user_data;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

static void api_config_clear(ApiConfig *config)
{
    g_free(config->cors_origin);
    g_free(config->video_directory);
    g_free(config->canonical_video_directory);
    memset(config, 0, sizeof(*config));
}

static gboolean load_config(const gchar *path, ApiConfig *config, GError **error)
{
    GKeyFile *file = g_key_file_new();
    gint64 port;

    if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, error)) {
        g_key_file_unref(file);
        return FALSE;
    }

    port = g_key_file_get_int64(file, "server", "port", error);
    if (*error != NULL) goto fail;
    if (port <= 0 || port > 65535) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "server.port must be between 1 and 65535");
        goto fail;
    }
    config->port = (guint)port;

    config->cors_origin = g_key_file_get_string(file, "server", "cors-origin", error);
    if (*error != NULL) goto fail;
    config->video_directory = g_key_file_get_string(file, "videos", "directory", error);
    if (*error != NULL) goto fail;
    if (!g_path_is_absolute(config->video_directory)) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "videos.directory must be an absolute path");
        goto fail;
    }

    config->canonical_video_directory =
        g_canonicalize_filename(config->video_directory, NULL);
    g_key_file_unref(file);
    return TRUE;

fail:
    g_key_file_unref(file);
    api_config_clear(config);
    return FALSE;
}

static void video_entry_free(gpointer data)
{
    VideoEntry *entry = data;
    g_free(entry->name);
    g_free(entry->relative_path);
    g_free(entry);
}

static void scan_videos(
    const gchar *root,
    const gchar *directory,
    GPtrArray *entries)
{
    GDir *dir = g_dir_open(directory, 0, NULL);
    const gchar *name;

    if (dir == NULL) return;

    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *path = g_build_filename(directory, name, NULL);
        struct stat status;

        if (lstat(path, &status) != 0) {
            g_free(path);
            continue;
        }
        if (S_ISDIR(status.st_mode)) {
            scan_videos(root, path, entries);
        } else if (S_ISREG(status.st_mode) && g_str_has_suffix(name, ".mp4")) {
            VideoEntry *entry = g_new0(VideoEntry, 1);
            const gchar *relative = path + strlen(root);
            while (*relative == G_DIR_SEPARATOR) ++relative;
            entry->name = g_strdup(name);
            entry->relative_path = g_strdup(relative);
            g_ptr_array_add(entries, entry);
        }
        g_free(path);
    }
    g_dir_close(dir);
}

static gint compare_video_entries(gconstpointer left, gconstpointer right)
{
    const VideoEntry *a = *(VideoEntry * const *)left;
    const VideoEntry *b = *(VideoEntry * const *)right;
    return -g_strcmp0(a->relative_path, b->relative_path);
}

static void add_common_headers(SoupServerMessage *message, const ApiConfig *config)
{
    SoupMessageHeaders *headers = soup_server_message_get_response_headers(message);
    soup_message_headers_replace(headers, "Access-Control-Allow-Origin", config->cors_origin);
    soup_message_headers_replace(headers, "Access-Control-Allow-Methods", "GET, HEAD, OPTIONS");
    soup_message_headers_replace(headers, "Access-Control-Allow-Headers", "Range, Content-Type");
    soup_message_headers_replace(headers, "Access-Control-Expose-Headers",
                                 "Accept-Ranges, Content-Length, Content-Range");
}

static void text_response(
    SoupServerMessage *message,
    guint status,
    const gchar *content_type,
    const gchar *body)
{
    soup_server_message_set_status(message, status, NULL);
    soup_server_message_set_response(message, content_type, SOUP_MEMORY_COPY,
                                     body, strlen(body));
}

static void list_videos(SoupServerMessage *message, const ApiConfig *config)
{
    GPtrArray *entries = g_ptr_array_new_with_free_func(video_entry_free);
    JsonBuilder *builder = json_builder_new();
    JsonGenerator *generator = json_generator_new();
    JsonNode *root;
    gchar *json;
    guint index;

    scan_videos(config->canonical_video_directory,
                config->canonical_video_directory, entries);
    g_ptr_array_sort(entries, compare_video_entries);

    json_builder_begin_array(builder);
    for (index = 0; index < entries->len; ++index) {
        VideoEntry *entry = g_ptr_array_index(entries, index);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "name");
        json_builder_add_string_value(builder, entry->name);
        json_builder_set_member_name(builder, "path");
        json_builder_add_string_value(builder, entry->relative_path);
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);

    root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json = json_generator_to_data(generator, NULL);
    soup_server_message_set_status(message, SOUP_STATUS_OK, NULL);
    soup_server_message_set_response(message, "application/json", SOUP_MEMORY_TAKE,
                                     json, strlen(json));

    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    g_ptr_array_unref(entries);
}

static gboolean parse_range(
    const gchar *header,
    goffset file_size,
    goffset *start,
    goffset *end)
{
    gchar **parts;
    gchar *separator;
    gchar *end_pointer = NULL;
    gint64 parsed_start;
    gint64 parsed_end;

    if (header == NULL || !g_str_has_prefix(header, "bytes=")) return FALSE;
    parts = g_strsplit(header + strlen("bytes="), ",", 2);
    if (parts[0] == NULL || parts[1] != NULL) {
        g_strfreev(parts);
        return FALSE;
    }
    separator = strchr(parts[0], '-');
    if (separator == NULL || separator == parts[0]) {
        g_strfreev(parts);
        return FALSE;
    }
    *separator = '\0';
    errno = 0;
    parsed_start = g_ascii_strtoll(parts[0], &end_pointer, 10);
    if (errno != 0 || end_pointer == parts[0] || *end_pointer != '\0' ||
        parsed_start < 0 || parsed_start >= file_size) {
        g_strfreev(parts);
        return FALSE;
    }

    if (*(separator + 1) == '\0') {
        parsed_end = file_size - 1;
    } else {
        errno = 0;
        parsed_end = g_ascii_strtoll(separator + 1, &end_pointer, 10);
        if (errno != 0 || end_pointer == separator + 1 || *end_pointer != '\0' ||
            parsed_end < parsed_start) {
            g_strfreev(parts);
            return FALSE;
        }
        if (parsed_end >= file_size) parsed_end = file_size - 1;
    }

    *start = parsed_start;
    *end = parsed_end;
    g_strfreev(parts);
    return TRUE;
}

static gboolean resolve_video_path(
    const ApiConfig *config,
    const gchar *encoded_relative_path,
    gchar **resolved)
{
    gchar *decoded = g_uri_unescape_string(encoded_relative_path, NULL);
    gchar *candidate;
    gchar *resolved_candidate;
    gchar *required_prefix;
    gboolean allowed;

    if (decoded == NULL || g_path_is_absolute(decoded)) {
        g_free(decoded);
        return FALSE;
    }
    candidate = g_canonicalize_filename(decoded, config->canonical_video_directory);
    resolved_candidate = realpath(candidate, NULL);
    required_prefix = g_strconcat(config->canonical_video_directory, G_DIR_SEPARATOR_S, NULL);
    allowed = resolved_candidate != NULL &&
              g_str_has_prefix(resolved_candidate, required_prefix) &&
              g_str_has_suffix(resolved_candidate, ".mp4");
    g_free(required_prefix);
    g_free(decoded);
    g_free(candidate);

    if (!allowed) {
        free(resolved_candidate);
        return FALSE;
    }
    *resolved = g_strdup(resolved_candidate);
    free(resolved_candidate);
    return TRUE;
}

static void serve_video(
    SoupServerMessage *message,
    const ApiConfig *config,
    const gchar *encoded_relative_path,
    gboolean head_only)
{
    gchar *path = NULL;
    GMappedFile *mapping;
    GError *error = NULL;
    GBytes *all_bytes;
    GBytes *response_bytes;
    SoupMessageHeaders *request_headers;
    SoupMessageHeaders *response_headers;
    const gchar *range_header;
    goffset file_size;
    goffset start = 0;
    goffset end;
    gboolean partial = FALSE;
    gchar *content_range = NULL;

    if (!resolve_video_path(config, encoded_relative_path, &path)) {
        text_response(message, SOUP_STATUS_BAD_REQUEST, "text/plain", "Invalid video path\n");
        return;
    }

    mapping = g_mapped_file_new(path, FALSE, &error);
    g_free(path);
    if (mapping == NULL) {
        guint status = g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)
            ? SOUP_STATUS_NOT_FOUND : SOUP_STATUS_INTERNAL_SERVER_ERROR;
        text_response(message, status, "text/plain",
                      status == SOUP_STATUS_NOT_FOUND ? "Video not found\n" : "Cannot read video\n");
        g_clear_error(&error);
        return;
    }

    file_size = (goffset)g_mapped_file_get_length(mapping);
    end = file_size > 0 ? file_size - 1 : 0;
    request_headers = soup_server_message_get_request_headers(message);
    response_headers = soup_server_message_get_response_headers(message);
    range_header = soup_message_headers_get_one(request_headers, "Range");

    if (range_header != NULL) {
        if (file_size == 0 || !parse_range(range_header, file_size, &start, &end)) {
            gchar *unsatisfied = g_strdup_printf("bytes */%" G_GOFFSET_FORMAT, file_size);
            soup_message_headers_replace(response_headers, "Content-Range", unsatisfied);
            g_free(unsatisfied);
            soup_server_message_set_status(message, SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE, NULL);
            g_mapped_file_unref(mapping);
            return;
        }
        partial = TRUE;
        content_range = g_strdup_printf("bytes %" G_GOFFSET_FORMAT "-%" G_GOFFSET_FORMAT
                                        "/%" G_GOFFSET_FORMAT,
                                        start, end, file_size);
        soup_message_headers_replace(response_headers, "Content-Range", content_range);
        g_free(content_range);
    }

    soup_message_headers_replace(response_headers, "Content-Type", "video/mp4");
    soup_message_headers_replace(response_headers, "Accept-Ranges", "bytes");
    soup_message_headers_set_content_length(response_headers,
                                            file_size == 0 ? 0 : end - start + 1);
    soup_server_message_set_status(message,
                                   partial ? SOUP_STATUS_PARTIAL_CONTENT : SOUP_STATUS_OK,
                                   NULL);

    if (!head_only && file_size > 0) {
        all_bytes = g_mapped_file_get_bytes(mapping);
        response_bytes = g_bytes_new_from_bytes(all_bytes, (gsize)start, (gsize)(end - start + 1));
        soup_message_body_append_bytes(soup_server_message_get_response_body(message), response_bytes);
        soup_message_body_complete(soup_server_message_get_response_body(message));
        g_bytes_unref(response_bytes);
        g_bytes_unref(all_bytes);
    }
    g_mapped_file_unref(mapping);
}

static void request_handler(
    SoupServer *server,
    SoupServerMessage *message,
    const gchar *path,
    GHashTable *query,
    gpointer user_data)
{
    ApiConfig *config = user_data;
    const gchar *method = soup_server_message_get_method(message);
    gboolean is_get = g_strcmp0(method, SOUP_METHOD_GET) == 0;
    gboolean is_head = g_strcmp0(method, SOUP_METHOD_HEAD) == 0;
    (void)server;
    (void)query;

    add_common_headers(message, config);
    if (g_strcmp0(method, SOUP_METHOD_OPTIONS) == 0) {
        soup_server_message_set_status(message, SOUP_STATUS_NO_CONTENT, NULL);
        return;
    }
    if (!is_get && !is_head) {
        soup_server_message_set_status(message, SOUP_STATUS_METHOD_NOT_ALLOWED, NULL);
        return;
    }

    if (g_strcmp0(path, "/") == 0) {
        text_response(message, SOUP_STATUS_OK, "text/plain",
                      is_head ? "" : "Jetson Video Server is running");
    } else if (g_strcmp0(path, "/videos") == 0) {
        if (is_head) {
            text_response(message, SOUP_STATUS_OK, "application/json", "");
        } else {
            list_videos(message, config);
        }
    } else if (g_str_has_prefix(path, "/video/") && strlen(path) > strlen("/video/")) {
        serve_video(message, config, path + strlen("/video/"), is_head);
    } else {
        text_response(message, SOUP_STATUS_NOT_FOUND, "text/plain", "Not found\n");
    }
}

int main(int argc, char **argv)
{
    const gchar *config_path = argc > 1 ? argv[1] : "/etc/radian-video-server/config.conf";
    ApiConfig config = {0};
    GError *error = NULL;
    SoupServer *server;

    if (!load_config(config_path, &config, &error)) {
        g_printerr("Cannot load configuration: %s\n", error->message);
        g_clear_error(&error);
        return EXIT_FAILURE;
    }
    if (g_mkdir_with_parents(config.video_directory, 0750) != 0) {
        g_printerr("Cannot create %s: %s\n", config.video_directory, g_strerror(errno));
        api_config_clear(&config);
        return EXIT_FAILURE;
    }

    server = soup_server_new(NULL, NULL);
    soup_server_add_handler(server, NULL, request_handler, &config, NULL);
    if (!soup_server_listen_all(server, config.port, 0, &error)) {
        g_printerr("Cannot listen on port %u: %s\n", config.port, error->message);
        g_clear_error(&error);
        g_object_unref(server);
        api_config_clear(&config);
        return EXIT_FAILURE;
    }

    config.loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGINT, handle_signal, config.loop);
    g_unix_signal_add(SIGTERM, handle_signal, config.loop);
    g_print("Radian Video API listening on port %u; serving %s\n",
            config.port, config.video_directory);
    g_main_loop_run(config.loop);

    soup_server_disconnect(server);
    g_main_loop_unref(config.loop);
    config.loop = NULL;
    g_object_unref(server);
    api_config_clear(&config);
    return EXIT_SUCCESS;
}
