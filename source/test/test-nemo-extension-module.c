/* A stand-in third-party extension for test-nemo-extension-load. It is built
 * without linking the extension library, the way one built against the static
 * exe has to be, so every API call it makes resolves against whatever loaded it. */

#include <gmodule.h>
#include <libnemo-extension/nemo-menu-provider.h>

typedef struct { GObject parent; } TestExt;
typedef struct { GObjectClass parent_class; } TestExtClass;

static void test_ext_menu_iface_init (NemoMenuProviderIface *iface);

G_DEFINE_TYPE_WITH_CODE (TestExt, test_ext, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (NEMO_TYPE_MENU_PROVIDER,
						test_ext_menu_iface_init))

/* Two calls nemo itself never makes, so a static build that kept only what the
   exe references would fail here. */
static GList *
test_ext_get_file_items (G_GNUC_UNUSED NemoMenuProvider *provider, G_GNUC_UNUSED GtkWidget *window, G_GNUC_UNUSED GList *files)
{
	GList *items = NULL;

	items = g_list_append (items, nemo_menu_item_new ("TestExt::item", "Test item", "A tip", NULL));
	items = g_list_append (items, nemo_menu_item_new_separator ("TestExt::sep"));
	return items;
}

static void
test_ext_menu_iface_init (NemoMenuProviderIface *iface)
{
	iface->get_file_items = test_ext_get_file_items;
}

static void test_ext_init (G_GNUC_UNUSED TestExt *self) {}
static void test_ext_class_init (G_GNUC_UNUSED TestExtClass *klass) {}

static GType types[1];

G_MODULE_EXPORT void
nemo_module_initialize (G_GNUC_UNUSED GTypeModule *module)
{
	types[0] = test_ext_get_type ();
}

G_MODULE_EXPORT void
nemo_module_shutdown (void)
{
}

G_MODULE_EXPORT void
nemo_module_list_types (const GType **out_types, int *num_types)
{
	*out_types = types;
	*num_types = G_N_ELEMENTS (types);
}
