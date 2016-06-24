#include <ltdl.h>
#include <openvswitch/vlog.h>
#include "physfan.h"
#include "fand_plugins.h"

VLOG_DEFINE_THIS_MODULE(fand_plugins);

#define CHECK_LT_RC(rc, msg)                    \
    do {                                        \
        if (rc) {                               \
            VLOG_ERR(msg ": %s", lt_dlerror()); \
            goto exit;                          \
        }                                       \
    } while (0);

void (*plugin_init)(void);
void (*plugin_deinit)(void);
const struct fand_subsystem_class *(*subsystem_class_get)(void);
const struct fand_fan_class *(*fan_class_get)(void);

static lt_dlinterface_id interface_id;
static lt_dlhandle dlhandle;

static int
plugin_open(const char *filename, void *data)
{
    int rc = 0;

    VLOG_INFO("Loading symbols from %s", filename);

    rc = !(dlhandle = lt_dlopenadvise(filename, *(lt_dladvise *)data));
    CHECK_LT_RC(rc, "lt_dlopenadvise");

    rc = !(plugin_init = lt_dlsym(dlhandle, "fand_plugin_init"));
    CHECK_LT_RC(rc, "Load fand_plugin_init");

    rc = !(plugin_deinit = lt_dlsym(dlhandle, "fand_plugin_deinit"));
    CHECK_LT_RC(rc, "Load fand_plugin_deinit");

    rc = !(subsystem_class_get = lt_dlsym(dlhandle, "fand_subsystem_class_get"));
    CHECK_LT_RC(rc, "Load fand_subsystem_class_get");

    rc = !(fan_class_get = lt_dlsym(dlhandle, "fand_fan_class_get"));
    CHECK_LT_RC(rc, "Load fand_fan__class_get");

exit:
    if (rc && dlhandle) {
        lt_dlclose(dlhandle);
    }
    return rc;
}

/**
 * Loads platform plugin. Initializes callbacks from plugin.
 *
 * @return 0 on success, non-zero value on failure.
 */
int
fand_plugin_load(void)
{
    int rc = 0;
    lt_dladvise advise;

    rc = lt_dlinit() || lt_dlsetsearchpath(PLATFORM_PLUGINS_PATH) ||
        lt_dladvise_init(&advise);
    CHECK_LT_RC(rc, "ltdl initializations");

    rc = !(interface_id = lt_dlinterface_register("fand-plugin", NULL));
    CHECK_LT_RC(rc, "Interface register");

    rc = lt_dladvise_local(&advise) || lt_dladvise_ext (&advise) ||
        lt_dlforeachfile(lt_dlgetsearchpath(), &plugin_open, &advise);
    CHECK_LT_RC(rc, "Setting ltdl advice");

exit:
    /* destroy advice */
    if (interface_id) {
        lt_dlinterface_free(interface_id);
    }

    return rc;
}

/**
 * Unload plugin - deinitialize symbols by destroing handle.
 */
void
fand_plugin_unload(void)
{
    if (dlhandle) {
        lt_dlclose(dlhandle);
    }
}

void
fand_plugin_init(void)
{
    plugin_init();
}

void
fand_plugin_deinit(void)
{
    plugin_deinit();
}

const struct fand_subsystem_class *
fand_subsystem_class_get(void)
{
    return subsystem_class_get();
}

const struct fand_fan_class *
fand_fan_class_get(void)
{
    return fan_class_get();
}
