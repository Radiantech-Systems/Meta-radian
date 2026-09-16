#define _POSIX_C_SOURCE 200809L

#include <gst/gst.h>
#include <glib.h>

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    gchar *rtsp_url;
    gchar *protocol;
    guint latency_ms;

    gboolean recording_enabled;
    gchar *recording_directory;
    guint segment_seconds;
    gdouble maximum_storage_gb;

    gboolean streaming_enabled;
    gchar *srt_uri;

    guint reconnect_seconds;
    guint storage_check_seconds;
} RelayConfig;

typedef struct {
    gchar *path;
    guint64 size;
    time_t modified;
} Recording;

static volatile sig_atomic_t stop_requested = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static void config_clear(RelayConfig *config)
{
    g_free(config->rtsp_url);
    g_free(config->protocol);
    g_free(config->recording_directory);
    g_free(config->srt_uri);
    memset(config, 0, sizeof(*config));
}

static gboolean read_positive_uint(
    GKeyFile *file,
    const gchar *group,
    const gchar *key,
    guint *destination,
    GError **error)
{
    gint64 value = g_key_file_get_int64(file, group, key, error);
    if (*error != NULL) {
        return FALSE;
    }
    if (value <= 0 || value > G_MAXUINT) {
        g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                    "%s.%s must be a positive integer", group, key);
        return FALSE;
    }
    *destination = (guint)value;
    return TRUE;
}

static gboolean load_config(const gchar *path, RelayConfig *config, GError **error)
{
    GKeyFile *file = g_key_file_new();
    gboolean valid = FALSE;

    if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, error)) {
        goto done;
    }

    config->rtsp_url = g_key_file_get_string(file, "input", "rtsp-url", error);
    if (*error != NULL) goto done;
    config->protocol = g_key_file_get_string(file, "input", "protocol", error);
    if (*error != NULL) goto done;
    if (!read_positive_uint(file, "input", "latency-ms", &config->latency_ms, error)) goto done;

    config->recording_enabled = g_key_file_get_boolean(file, "recording", "enabled", error);
    if (*error != NULL) goto done;
    config->recording_directory = g_key_file_get_string(file, "recording", "directory", error);
    if (*error != NULL) goto done;
    if (!read_positive_uint(file, "recording", "segment-seconds", &config->segment_seconds, error)) goto done;
    config->maximum_storage_gb = g_key_file_get_double(file, "recording", "maximum-storage-gb", error);
    if (*error != NULL) goto done;

    config->streaming_enabled = g_key_file_get_boolean(file, "streaming", "enabled", error);
    if (*error != NULL) goto done;
    config->srt_uri = g_key_file_get_string(file, "streaming", "srt-uri", error);
    if (*error != NULL) goto done;

    if (!read_positive_uint(file, "recovery", "reconnect-seconds", &config->reconnect_seconds, error)) goto done;
    if (!read_positive_uint(file, "recovery", "storage-check-seconds", &config->storage_check_seconds, error)) goto done;

    if (!g_str_has_prefix(config->rtsp_url, "rtsp://") &&
        !g_str_has_prefix(config->rtsp_url, "rtsps://")) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "input.rtsp-url must use rtsp:// or rtsps://");
        goto done;
    }
    if (g_strcmp0(config->protocol, "tcp") != 0 &&
        g_strcmp0(config->protocol, "udp") != 0) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "input.protocol must be tcp or udp");
        goto done;
    }
    if (!config->recording_enabled && !config->streaming_enabled) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "at least one output must be enabled");
        goto done;
    }
    if (config->maximum_storage_gb <= 0.0) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "recording.maximum-storage-gb must be positive");
        goto done;
    }
    if (config->streaming_enabled && !g_str_has_prefix(config->srt_uri, "srt://")) {
        g_set_error_literal(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                            "streaming.srt-uri must use srt://");
        goto done;
    }

    valid = TRUE;

done:
    g_key_file_unref(file);
    if (!valid) {
        config_clear(config);
    }
    return valid;
}

static gchar *timestamp_string(const gchar *format)
{
    time_t now = time(NULL);
    struct tm local_time;
    gchar buffer[32];

    localtime_r(&now, &local_time);
    if (strftime(buffer, sizeof(buffer), format, &local_time) == 0) {
        return g_strdup("unknown-time");
    }
    return g_strdup(buffer);
}

static gchar *quote_pipeline_value(const gchar *value)
{
    gchar *escaped = g_strescape(value, NULL);
    gchar *quoted = g_strdup_printf("\"%s\"", escaped);
    g_free(escaped);
    return quoted;
}

static gchar *build_pipeline(const RelayConfig *config, GError **error)
{
    GString *pipeline = g_string_new(NULL);
    gchar *quoted_rtsp = quote_pipeline_value(config->rtsp_url);

    g_string_append_printf(
        pipeline,
        "rtspsrc location=%s protocols=%s latency=%u "
        "! rtph264depay ! h264parse config-interval=-1 ! tee name=relay ",
        quoted_rtsp, config->protocol, config->latency_ms);
    g_free(quoted_rtsp);

    if (config->recording_enabled) {
        gchar *date = timestamp_string("%Y-%m-%d");
        gchar *clock = timestamp_string("%H-%M-%S");
        gchar *date_directory = g_build_filename(config->recording_directory, date, NULL);
        gchar *filename = g_strdup_printf("%s_%%05d.mp4", clock);
        gchar *pattern = g_build_filename(date_directory, filename, NULL);
        gchar *quoted_pattern = quote_pipeline_value(pattern);
        guint64 duration_ns = (guint64)config->segment_seconds * GST_SECOND;

        if (g_mkdir_with_parents(date_directory, 0750) != 0) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "cannot create recording directory %s: %s",
                        date_directory, g_strerror(errno));
            g_free(date);
            g_free(clock);
            g_free(date_directory);
            g_free(filename);
            g_free(pattern);
            g_free(quoted_pattern);
            g_string_free(pipeline, TRUE);
            return NULL;
        }

        g_string_append_printf(
            pipeline,
            "relay. ! queue ! h264parse ! splitmuxsink location=%s "
            "max-size-time=%" G_GUINT64_FORMAT " async-finalize=true ",
            quoted_pattern, duration_ns);

        g_free(date);
        g_free(clock);
        g_free(date_directory);
        g_free(filename);
        g_free(pattern);
        g_free(quoted_pattern);
    }

    if (config->streaming_enabled) {
        gchar *quoted_srt = quote_pipeline_value(config->srt_uri);
        g_string_append_printf(
            pipeline,
            "relay. ! queue leaky=downstream max-size-time=2000000000 "
            "! h264parse ! mpegtsmux alignment=7 "
            "! srtsink uri=%s wait-for-connection=false sync=false ",
            quoted_srt);
        g_free(quoted_srt);
    }

    return g_string_free(pipeline, FALSE);
}

static gboolean has_mp4_suffix(const gchar *name)
{
    return g_str_has_suffix(name, ".mp4");
}

static void recording_free(gpointer data)
{
    Recording *recording = data;
    g_free(recording->path);
    g_free(recording);
}

static void scan_recordings(const gchar *directory, GPtrArray *recordings, guint64 *total)
{
    GDir *dir = g_dir_open(directory, 0, NULL);
    const gchar *name;

    if (dir == NULL) {
        return;
    }

    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *path = g_build_filename(directory, name, NULL);
        struct stat status;

        if (lstat(path, &status) != 0) {
            g_free(path);
            continue;
        }
        if (S_ISDIR(status.st_mode)) {
            scan_recordings(path, recordings, total);
            g_free(path);
            continue;
        }
        if (S_ISREG(status.st_mode) && has_mp4_suffix(name)) {
            Recording *recording = g_new0(Recording, 1);
            recording->path = path;
            recording->size = (guint64)status.st_size;
            recording->modified = status.st_mtime;
            *total += recording->size;
            g_ptr_array_add(recordings, recording);
            continue;
        }
        g_free(path);
    }
    g_dir_close(dir);
}

static gint compare_recordings(gconstpointer left, gconstpointer right)
{
    const Recording *a = *(Recording * const *)left;
    const Recording *b = *(Recording * const *)right;
    if (a->modified < b->modified) return -1;
    if (a->modified > b->modified) return 1;
    return 0;
}

static void prune_recordings(const RelayConfig *config)
{
    GPtrArray *recordings;
    guint64 total = 0;
    guint64 maximum;
    guint index;

    if (!config->recording_enabled) {
        return;
    }

    recordings = g_ptr_array_new_with_free_func(recording_free);
    scan_recordings(config->recording_directory, recordings, &total);
    maximum = (guint64)(config->maximum_storage_gb * 1024.0 * 1024.0 * 1024.0);

    if (total > maximum) {
        g_ptr_array_sort(recordings, compare_recordings);
        for (index = 0; index < recordings->len && total > maximum; ++index) {
            Recording *recording = g_ptr_array_index(recordings, index);
            if (unlink(recording->path) == 0) {
                g_print("Removed old recording %s\n", recording->path);
                total = total > recording->size ? total - recording->size : 0;
            } else {
                g_printerr("Cannot remove %s: %s\n", recording->path, g_strerror(errno));
            }
        }
    }
    g_ptr_array_unref(recordings);
}

static gpointer storage_worker(gpointer data)
{
    const RelayConfig *config = data;
    while (!stop_requested) {
        guint elapsed;
        prune_recordings(config);
        for (elapsed = 0; elapsed < config->storage_check_seconds && !stop_requested; ++elapsed) {
            g_usleep(G_USEC_PER_SEC);
        }
    }
    return NULL;
}

static gboolean run_pipeline(const RelayConfig *config)
{
    GError *error = NULL;
    gchar *description = build_pipeline(config, &error);
    GstElement *pipeline;
    GstBus *bus;
    gboolean clean_stop = FALSE;

    if (description == NULL) {
        g_printerr("Cannot build pipeline: %s\n", error->message);
        g_clear_error(&error);
        return FALSE;
    }

    pipeline = gst_parse_launch(description, &error);
    g_free(description);
    if (pipeline == NULL || error != NULL) {
        g_printerr("Cannot create GStreamer pipeline: %s\n",
                   error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        if (pipeline != NULL) gst_object_unref(pipeline);
        return FALSE;
    }

    bus = gst_element_get_bus(pipeline);
    if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        g_printerr("GStreamer pipeline refused PLAYING state\n");
        gst_object_unref(bus);
        gst_object_unref(pipeline);
        return FALSE;
    }

    g_print("Video relay pipeline started\n");
    while (!stop_requested) {
        GstMessage *message = gst_bus_timed_pop_filtered(
            bus, 500 * GST_MSECOND,
            (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

        if (message == NULL) continue;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *pipeline_error = NULL;
            gchar *debug = NULL;
            gst_message_parse_error(message, &pipeline_error, &debug);
            g_printerr("Pipeline error from %s: %s\n",
                       GST_OBJECT_NAME(message->src), pipeline_error->message);
            if (debug != NULL) g_printerr("%s\n", debug);
            g_clear_error(&pipeline_error);
            g_free(debug);
        } else {
            g_print("RTSP stream ended\n");
        }
        gst_message_unref(message);
        break;
    }

    if (stop_requested) {
        GstMessage *message;
        clean_stop = TRUE;
        gst_element_send_event(pipeline, gst_event_new_eos());
        message = gst_bus_timed_pop_filtered(
            bus, 5 * GST_SECOND,
            (GstMessageType)(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
        if (message != NULL) gst_message_unref(message);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);
    return clean_stop;
}

static void wait_interruptibly(guint seconds)
{
    guint elapsed;
    for (elapsed = 0; elapsed < seconds && !stop_requested; ++elapsed) {
        sleep(1);
    }
}

int main(int argc, char **argv)
{
    const gchar *config_path;
    RelayConfig config = {0};
    GError *error = NULL;
    GThread *storage_thread;

    gst_init(&argc, &argv);
    config_path = argc > 1 ? argv[1] : "/etc/radian-video-relay/config.conf";

    if (!load_config(config_path, &config, &error)) {
        g_printerr("Cannot load configuration: %s\n", error->message);
        g_clear_error(&error);
        return EXIT_FAILURE;
    }

    if (config.recording_enabled &&
        g_mkdir_with_parents(config.recording_directory, 0750) != 0) {
        g_printerr("Cannot create %s: %s\n",
                   config.recording_directory, g_strerror(errno));
        config_clear(&config);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    storage_thread = g_thread_new("recording-retention", storage_worker, &config);

    while (!stop_requested) {
        if (run_pipeline(&config)) break;
        if (!stop_requested) {
            g_printerr("Reconnecting in %u seconds\n", config.reconnect_seconds);
            wait_interruptibly(config.reconnect_seconds);
        }
    }

    stop_requested = 1;
    g_thread_join(storage_thread);
    config_clear(&config);
    return EXIT_SUCCESS;
}
