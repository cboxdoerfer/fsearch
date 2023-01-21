#include "fsearch_database_view2.h"

#include "fsearch_database2.h"

#include <glib.h>

struct _FsearchDatabaseView2 {
    GObject parent_instance;

    FsearchDatabase2 *database;

    guint32 id;
};

enum { PROP_0, PROP_DATABASE, NUM_PROPERTIES };

static GParamSpec *properties[NUM_PROPERTIES];

static guint32 id = 0;

G_DEFINE_TYPE(FsearchDatabaseView2, fsearch_database_view2, G_TYPE_OBJECT)

static void
fsearch_database_view2_constructed(GObject *object) {
    FsearchDatabaseView2 *self = (FsearchDatabaseView2 *)object;

    g_assert(FSEARCH_IS_DATABASE_VIEW2(self));

    G_OBJECT_CLASS(fsearch_database_view2_parent_class)->constructed(object);

    g_print("constructed...\n");
}

static void
fsearch_database_view2_finalize(GObject *object) {
    FsearchDatabaseView2 *self = (FsearchDatabaseView2 *)object;
    g_print("finalize view2...\n");
    g_clear_object(&self->database);

    //// Notify work queue thread to exit itself
    // g_cancellable_cancel(self->work_queue_thread_cancellable);
    // wakeup_work_queue(self);
    // g_thread_join(self->work_queue_thread);

    // database_lock(self);
    // g_clear_object(&self->work_queue_thread_cancellable);
    // g_clear_pointer(&self->work_trigger_queue, g_async_queue_unref);
    // g_clear_pointer(&self->work_queue, g_async_queue_unref);

    // g_clear_object(&self->file);

    // g_clear_pointer(&self->index, fsearch_database_index_free);
    // database_unlock(self);

    // g_mutex_clear(&self->mutex);

    G_OBJECT_CLASS(fsearch_database_view2_parent_class)->finalize(object);
    g_print("finalized view2.\n");
}

static void
fsearch_database_view2_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    FsearchDatabaseView2 *self = FSEARCH_DATABASE_VIEW2(object);

    switch (prop_id) {
    case PROP_DATABASE:
        g_value_set_object(value, self->database);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database_view2_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    FsearchDatabaseView2 *self = FSEARCH_DATABASE_VIEW2(object);

    switch (prop_id) {
    case PROP_DATABASE:
        g_print("set db...\n");
        g_set_object(&self->database, g_value_get_object(value));
        //self->database = g_object_g_value_get_object(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
fsearch_database_view2_class_init(FsearchDatabaseView2Class *klass) {
    g_print("class init....\n");
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = fsearch_database_view2_constructed;
    object_class->finalize = fsearch_database_view2_finalize;
    object_class->set_property = fsearch_database_view2_set_property;
    object_class->get_property = fsearch_database_view2_get_property;

    properties[PROP_DATABASE] = g_param_spec_object("database",
                                                    "Database",
                                                    "The database that will viewed"
                                                    "default",
                                                    FSEARCH_TYPE_DATABASE2,
                                                    (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

    g_object_class_install_properties(object_class, NUM_PROPERTIES, properties);

    // signals[EVENT_LOAD_STARTED] = g_signal_new("load-started",
    //                                            G_TYPE_FROM_CLASS(klass),
    //                                            G_SIGNAL_RUN_LAST,
    //                                            0,
    //                                            NULL,
    //                                            NULL,
    //                                            NULL,
    //                                            G_TYPE_NONE,
    //                                            1,
    //                                            G_TYPE_POINTER);
    // signals[EVENT_LOAD_FINISHED] = g_signal_new("load-finished",
    //                                             G_TYPE_FROM_CLASS(klass),
    //                                             G_SIGNAL_RUN_LAST,
    //                                             0,
    //                                             NULL,
    //                                             NULL,
    //                                             NULL,
    //                                             G_TYPE_NONE,
    //                                             1,
    //                                             G_TYPE_POINTER);
    // signals[EVENT_SAVE_STARTED] = g_signal_new("save-started",
    //                                            G_TYPE_FROM_CLASS(klass),
    //                                            G_SIGNAL_RUN_LAST,
    //                                            0,
    //                                            NULL,
    //                                            NULL,
    //                                            NULL,
    //                                            G_TYPE_NONE,
    //                                            1,
    //                                            G_TYPE_POINTER);
    // signals[EVENT_SAVE_FINISHED] = g_signal_new("save-finished",
    //                                             G_TYPE_FROM_CLASS(klass),
    //                                             G_SIGNAL_RUN_LAST,
    //                                             0,
    //                                             NULL,
    //                                             NULL,
    //                                             NULL,
    //                                             G_TYPE_NONE,
    //                                             1,
    //                                             G_TYPE_POINTER);
}

static void
fsearch_database_view2_init(FsearchDatabaseView2 *self) {
    self->id = id++;
    // g_mutex_init((&self->mutex));
    // self->work_queue = g_async_queue_new();
    // self->work_trigger_queue = g_async_queue_new();
    // self->work_queue_thread = g_thread_new("FsearchDatabaseWorkQueue", work_queue_thread, self);
    // self->work_queue_thread_cancellable = g_cancellable_new();
}

FsearchDatabaseView2 *
fsearch_database_view2_new(GObject *db) {
    g_return_val_if_fail(db, NULL);
    g_return_val_if_fail(FSEARCH_IS_DATABASE2(db), NULL);
    return g_object_new(FSEARCH_TYPE_DATABASE_VIEW2, "database", db, NULL);
}

guint32
fsearch_database_view2_get_id(FsearchDatabaseView2 *self) {
    g_assert(FSEARCH_IS_DATABASE_VIEW2(self));
    return self->id;
}