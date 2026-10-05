/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* nemo-statusbar.c
 * 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#include "nemo-statusbar.h"

#include "nemo-list-view.h"
#include "nemo-icon-view.h"

#include <libnemo-private/nemo-thumbnails.h>

#include <config.h>
#include <glib/gi18n.h>

enum {
        LAST_SIGNAL
};

enum {
    PROP_WINDOW = 1,
    NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (NemoStatusBar, nemo_status_bar, GTK_TYPE_BOX);

static void
nemo_status_bar_init (NemoStatusBar *bar)
{
    bar->window = NULL;
}

static void
nemo_status_bar_set_property (GObject        *object,
                              guint           arg_id,
                              const GValue   *value,
                              GParamSpec     *pspec)
{
    NemoStatusBar *self = NEMO_STATUS_BAR (object);

    switch (arg_id) {
        case PROP_WINDOW:
            self->window = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
            break;
    }
}

static void
nemo_status_bar_get_property (GObject      *object,
                              guint         arg_id,
                              GValue       *value,
                              GParamSpec   *pspec)
{
    NemoStatusBar *self = NEMO_STATUS_BAR (object);

    switch (arg_id) {
        case PROP_WINDOW:
            g_value_set_object (value, self->window);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, arg_id, pspec);
            break;
    }
}

#define THUMB_TICK_MS 150
/* A run shorter than this, one edited file say, never puts the bars up. */
#define THUMB_SHOW_AFTER_TICKS 2
/* And they stay up a moment once it ends, full, rather than just vanish. */
#define THUMB_LINGER_TICKS 4

static void
set_bar (GtkWidget *bar, const char *format, guint done, guint total)
{
    g_autofree char *tip = g_strdup_printf (format, done, total);

    gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (bar),
                                   total > 0 ? (gdouble) done / total : 1.0);
    gtk_widget_set_tooltip_text (bar, tip);
}

/* The room stays taken while they are hidden, or the status text, which is
 * centered in what is left, would jump sideways each time they come and go. */
static void
show_thumb_bars (NemoStatusBar *bar, gboolean show)
{
    gtk_widget_set_child_visible (bar->build_bar, show);
    gtk_widget_set_child_visible (bar->render_bar, show);
}

static gboolean
thumb_tick_cb (gpointer user_data)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (user_data);
    NemoWindowSlot *slot;
    guint done, waiting;
    guint shown = 0, wanted = 0;

    nemo_thumbnail_jobs (&done, &waiting);

    if (waiting == 0) {
        bar->thumb_busy_ticks = 0;

        if (!gtk_widget_get_child_visible (bar->build_bar) ||
            ++bar->thumb_idle_ticks > THUMB_LINGER_TICKS) {
            show_thumb_bars (bar, FALSE);
            bar->thumb_tick_id = 0;
            return G_SOURCE_REMOVE;
        }
    } else {
        bar->thumb_idle_ticks = 0;

        if (++bar->thumb_busy_ticks < THUMB_SHOW_AFTER_TICKS) {
            return G_SOURCE_CONTINUE;
        }
    }

    slot = bar->window != NULL ? nemo_window_get_active_slot (bar->window) : NULL;

    if (slot != NULL && NEMO_IS_ICON_VIEW (slot->content_view) &&
        nemo_file_should_show_thumbnail (nemo_view_get_directory_as_file (slot->content_view))) {
        nemo_icon_container_count_thumbnails (
            nemo_icon_view_get_icon_container (NEMO_ICON_VIEW (slot->content_view)),
            &shown, &wanted);
    }

    nemo_thumbnail_rendered_for_bar (done, waiting, &shown, &wanted);

    set_bar (bar->build_bar, _("Building thumbnails: %u of %u"), done, done + waiting);
    set_bar (bar->render_bar, _("Rendering thumbnails: %u of %u"), shown, wanted);
    show_thumb_bars (bar, TRUE);

    return G_SOURCE_CONTINUE;
}

static void
thumb_jobs_started_cb (gpointer user_data)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (user_data);

    bar->thumb_busy_ticks = 0;
    bar->thumb_idle_ticks = 0;

    if (bar->thumb_tick_id == 0) {
        bar->thumb_tick_id = g_timeout_add (THUMB_TICK_MS, thumb_tick_cb, bar);
    }
}

static void
nemo_status_bar_dispose (GObject *object)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (object);

    bar->window = NULL;

    nemo_thumbnail_unwatch_jobs (thumb_jobs_started_cb, bar);
    g_clear_handle_id (&bar->thumb_tick_id, g_source_remove);

    G_OBJECT_CLASS (nemo_status_bar_parent_class)->dispose (object);
}

static void
places_button_toggled_cb (GtkToggleButton *button, NemoStatusBar *bar)
{
    nemo_window_set_show_places (bar->window, gtk_toggle_button_get_active (button));
}

static void
tree_button_toggled_cb (GtkToggleButton *button, NemoStatusBar *bar)
{
    nemo_window_set_show_tree (bar->window, gtk_toggle_button_get_active (button));
}

static void
hide_sidebar_clicked_cb (G_GNUC_UNUSED GtkButton *button, NemoStatusBar *bar)
{
    nemo_window_hide_sidebar (bar->window);
}

static void
show_sidebar_clicked_cb (G_GNUC_UNUSED GtkButton *button, NemoStatusBar *bar)
{
    nemo_window_show_sidebar (bar->window);
}

static void
sidebar_state_changed_cb (G_GNUC_UNUSED GObject *object, G_GNUC_UNUSED GParamSpec *pspec, gpointer user_data)
{
    nemo_status_bar_sync_button_states (NEMO_STATUS_BAR (user_data));
}

static void
on_slider_changed_cb (GtkWidget *zoom_slider, gpointer user_data)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (user_data);
    gdouble val = gtk_range_get_value (GTK_RANGE (zoom_slider));

    NemoWindowSlot *slot = nemo_window_get_active_slot (bar->window);

    if (!NEMO_IS_WINDOW_SLOT (slot))
        return;

    NemoView *view = slot->content_view;

    if (!NEMO_IS_VIEW (view))
        return;

    nemo_view_set_icon_size (view, nemo_icon_size_at_position (val));
}

#define SLIDER_WIDTH 100
#define THUMB_BARS_WIDTH 80
#define SLIDER_END_MARGIN 6

static void
nemo_status_bar_constructed (GObject *object)
{
    NemoStatusBar *bar = NEMO_STATUS_BAR (object);
    G_OBJECT_CLASS (nemo_status_bar_parent_class)->constructed (object);

    GtkWidget *statusbar = gtk_statusbar_new ();
    GtkStyleContext *context;

    bar->real_statusbar = statusbar;

    gtk_widget_set_name (GTK_WIDGET (bar), "nemo-statusbar");

    context = gtk_widget_get_style_context (GTK_WIDGET (bar));
    gtk_style_context_add_class (context, GTK_STYLE_CLASS_TOOLBAR);
    gtk_container_set_border_width (GTK_CONTAINER (bar), 2);

    GtkIconSize size = gtk_icon_size_from_name (NEMO_STATUSBAR_ICON_SIZE_NAME);
    GtkWidget *button, *icon, *sep;

    button = gtk_toggle_button_new ();
    icon = gtk_image_new_from_icon_name ("nemo-sidebar-places-symbolic", size);
    gtk_button_set_image (GTK_BUTTON (button), icon);
    gtk_widget_set_tooltip_text (button, _("Show places"));
    bar->places_button = button;
    gtk_box_pack_start (GTK_BOX (bar), button, FALSE, FALSE, 2);
    g_signal_connect (button, "toggled",
                      G_CALLBACK (places_button_toggled_cb), bar);

    button = gtk_toggle_button_new ();
    icon = gtk_image_new_from_icon_name ("nemo-sidebar-tree-symbolic", size);
    gtk_button_set_image (GTK_BUTTON (button), icon);
    gtk_widget_set_tooltip_text (button, _("Show tree view"));
    bar->tree_button = button;
    gtk_box_pack_start (GTK_BOX (bar), button, FALSE, FALSE, 2);
    g_signal_connect (button, "toggled",
                      G_CALLBACK (tree_button_toggled_cb), bar);

    sep = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start (GTK_BOX (bar), sep, FALSE, FALSE, 6);
    bar->separator = sep;

    /* Only ever one of the next two is up, so keep show_all off both and let
       sync_button_states say which. */
    button = gtk_button_new ();
    icon = gtk_image_new_from_icon_name ("nemo-sidebar-hide-symbolic", size);
    gtk_button_set_image (GTK_BUTTON (button), icon);
    gtk_widget_set_tooltip_text (button, _("Show contents only (F9)"));
    gtk_widget_set_no_show_all (button, TRUE);
    bar->hide_button = button;
    gtk_box_pack_start (GTK_BOX (bar), button, FALSE, FALSE, 2);
    g_signal_connect (button, "clicked",
                      G_CALLBACK (hide_sidebar_clicked_cb), bar);

    button = gtk_button_new ();
    icon = gtk_image_new_from_icon_name ("nemo-sidebar-show-symbolic", size);
    gtk_button_set_image (GTK_BUTTON (button), icon);
    gtk_widget_set_tooltip_text (button, _("Full view (F9)"));
    gtk_widget_set_no_show_all (button, TRUE);
    bar->show_button = button;
    gtk_box_pack_start (GTK_BOX (bar), button, FALSE, FALSE, 2);
    g_signal_connect (button, "clicked",
                      G_CALLBACK (show_sidebar_clicked_cb), bar);

    /* Stacked, so the pair takes no more height than one button. */
    bar->thumb_bars = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_valign (bar->thumb_bars, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request (bar->thumb_bars, THUMB_BARS_WIDTH, -1);
    bar->build_bar = gtk_progress_bar_new ();
    bar->render_bar = gtk_progress_bar_new ();
    gtk_box_pack_start (GTK_BOX (bar->thumb_bars), bar->build_bar, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (bar->thumb_bars), bar->render_bar, FALSE, FALSE, 0);
    show_thumb_bars (bar, FALSE);
    gtk_box_pack_start (GTK_BOX (bar), bar->thumb_bars, FALSE, FALSE, 8);
    nemo_thumbnail_watch_jobs (thumb_jobs_started_cb, bar);

    gtk_box_pack_start (GTK_BOX (bar), statusbar, TRUE, TRUE, 10);
    gtk_widget_set_margin_top (GTK_WIDGET (statusbar), 0);
    gtk_widget_set_margin_bottom (GTK_WIDGET (statusbar), 0);

    guint n_steps;
    guint step;

    nemo_icon_size_steps (&n_steps);

    /* The slider runs along the steps rather than over pixels, so the marks
       come out evenly spaced although the sizes they stand for do not. */
    GtkWidget *zoom_slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                                       0.0,
                                                       (gdouble) (n_steps - 1),
                                                       0.05);
    gtk_widget_set_tooltip_text (GTK_WIDGET (zoom_slider), _("Adjust zoom level"));
    bar->zoom_slider = zoom_slider;

    gtk_box_pack_start (GTK_BOX (bar), zoom_slider, FALSE, FALSE, 2);

    /* sync_zoom_widgets owns whether this is up, so keep show_all off it. */
    gtk_widget_set_no_show_all (zoom_slider, TRUE);
    gtk_widget_show (zoom_slider);

    gtk_widget_set_size_request (GTK_WIDGET (zoom_slider), SLIDER_WIDTH, 0);

    /* Last thing in the row, so without this the trough runs into the window
       edge while the buttons at the other end sit clear of it. */
    gtk_widget_set_margin_end (GTK_WIDGET (zoom_slider), SLIDER_END_MARGIN);
    gtk_scale_set_draw_value (GTK_SCALE (zoom_slider), FALSE);
    gtk_range_set_increments (GTK_RANGE (zoom_slider), 0.05, 1.0);
    gtk_range_set_round_digits (GTK_RANGE (zoom_slider), 2);

    for (step = 0; step < n_steps; step++) {
        gtk_scale_add_mark (GTK_SCALE (zoom_slider), (gdouble) step, GTK_POS_BOTTOM, NULL);
    }

    gtk_widget_show_all (GTK_WIDGET (bar));

    g_signal_connect_object (NEMO_WINDOW (bar->window), "notify::show-sidebar",
                             G_CALLBACK (sidebar_state_changed_cb), bar, G_CONNECT_AFTER);

    g_signal_connect_object (NEMO_WINDOW (bar->window), "notify::show-places",
                             G_CALLBACK (sidebar_state_changed_cb), bar, G_CONNECT_AFTER);

    g_signal_connect_object (NEMO_WINDOW (bar->window), "notify::show-tree",
                             G_CALLBACK (sidebar_state_changed_cb), bar, G_CONNECT_AFTER);

    g_signal_connect (GTK_RANGE (zoom_slider), "value-changed",
                      G_CALLBACK (on_slider_changed_cb), bar);

    GtkWidget *cont = gtk_statusbar_get_message_area (GTK_STATUSBAR (statusbar));

    GList *children = gtk_container_get_children (GTK_CONTAINER (cont));

    gtk_box_set_child_packing (GTK_BOX (cont),
                               GTK_WIDGET (children->data),
                               TRUE, FALSE, 10, GTK_PACK_START);

    g_list_free (children);

    nemo_status_bar_sync_button_states (bar);
}


static void
nemo_status_bar_class_init (NemoStatusBarClass *status_bar_class)
{
    GObjectClass *oclass;

    oclass = G_OBJECT_CLASS (status_bar_class);

    oclass->set_property = nemo_status_bar_set_property;
    oclass->get_property = nemo_status_bar_get_property;

    oclass->dispose = nemo_status_bar_dispose;
    oclass->constructed = nemo_status_bar_constructed;

    properties[PROP_WINDOW] = g_param_spec_object ("window",
                                                   "The NemoWindow",
                                                   "The parent NemoWindow",
                                                   NEMO_TYPE_WINDOW,
                                                   G_PARAM_READWRITE |
                                                   G_PARAM_CONSTRUCT_ONLY |
                                                   G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (oclass, NUM_PROPERTIES, properties);
}

/* Returns: (transfer floating) */
GtkWidget *
nemo_status_bar_new (NemoWindow *window)
{
    return g_object_new (NEMO_TYPE_STATUS_BAR,
                         "orientation", GTK_ORIENTATION_HORIZONTAL,
                         "spacing", 0,
                         "window", window,
                         NULL);
}

/* Returns: (transfer none): owned by @bar */
GtkWidget *
nemo_status_bar_get_real_statusbar (NemoStatusBar *bar)
{
    return bar->real_statusbar;
}

void
nemo_status_bar_sync_button_states (NemoStatusBar *bar)
{
    gboolean places, tree, any;

    if (!NEMO_IS_WINDOW (bar->window))
        return;

    places = nemo_window_get_show_places (bar->window);
    tree = nemo_window_get_show_tree (bar->window);
    any = places || tree;

    g_signal_handlers_block_by_func (bar->places_button, places_button_toggled_cb, bar);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bar->places_button), places);
    g_signal_handlers_unblock_by_func (bar->places_button, places_button_toggled_cb, bar);

    g_signal_handlers_block_by_func (bar->tree_button, tree_button_toggled_cb, bar);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bar->tree_button), tree);
    g_signal_handlers_unblock_by_func (bar->tree_button, tree_button_toggled_cb, bar);

    gtk_widget_set_visible (bar->hide_button, any);
    gtk_widget_set_visible (bar->show_button, !any);
}

void
nemo_status_bar_sync_zoom_widgets (NemoStatusBar *bar)
{

    NemoWindowSlot *slot = nemo_window_get_active_slot (bar->window);

    if (!NEMO_IS_WINDOW_SLOT (slot))
        return;

    NemoView *view = slot->content_view;

    if (!NEMO_IS_VIEW (view))
        return;

    /* Row height is not what the slider is for - list view sizes itself off the
       columns, so it only clutters the bar there. */
    gtk_widget_set_visible (bar->zoom_slider, !NEMO_IS_LIST_VIEW (view));

    gint icon_size = nemo_view_get_icon_size (NEMO_VIEW (view));

    g_signal_handlers_block_by_func (GTK_RANGE (bar->zoom_slider), on_slider_changed_cb, bar);

    gtk_range_set_value (GTK_RANGE (bar->zoom_slider), nemo_icon_size_position (icon_size));

    g_signal_handlers_unblock_by_func (GTK_RANGE (bar->zoom_slider), on_slider_changed_cb, bar);
}
