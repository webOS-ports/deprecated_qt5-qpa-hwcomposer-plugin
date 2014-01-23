/****************************************************************************
**
** Copyright (C) 2013 Jolla Ltd.
** Contact: Thomas Perl <thomas.perl@jolla.com>
** Copyright (c) 2014 Simon Busch <morphis@gravedo.de>
**
** This file is part of the hwcomposer plugin.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "hwcomposer_backend_v11.h"

#ifdef HWC_PLUGIN_HAVE_HWCOMPOSER1_API

static const char *
comp_type_str(int32_t type)
{
    switch (type) {
        case HWC_BACKGROUND: return "BACKGROUND";
        case HWC_FRAMEBUFFER_TARGET: return "FB TARGET";
        case HWC_FRAMEBUFFER: return "FB";
        case HWC_OVERLAY: return "OVERLAY";
    }

    return "unknown";
}

static const char *
blending_type_str(int32_t type)
{
    switch (type) {
        case HWC_BLENDING_NONE: return "NONE";
        case HWC_BLENDING_PREMULT: return "PREMULT";
        case HWC_BLENDING_COVERAGE: return "COVERAGE";
    }

    return "unknown";
}

static void
dump_display_contents(hwc_display_contents_1_t *contents)
{
    static const char *dump_env = getenv("HWC_DUMP_DISPLAY_CONTENTS");
    static bool do_dump = (dump_env != NULL && strcmp(dump_env, "1") == 0);

    if (!do_dump) {
        return;
    }

    fprintf(stderr, "============ QPA-HWC: dump_display_contents(%p) ============\n",  contents);
    fprintf(stderr, "retireFenceFd = %d\n", contents->retireFenceFd);
    fprintf(stderr, "dpy = %p\n", contents->dpy);
    fprintf(stderr, "sur = %p\n", contents->sur);
    fprintf(stderr, "flags = %x\n", contents->flags);
    fprintf(stderr, "numHwLayers = %d\n", contents->numHwLayers);
    for (int i=0; i<contents->numHwLayers; i++) {
        hwc_layer_1_t *layer = &(contents->hwLayers[i]);
        fprintf(stderr, "Layer %d (%p):\n"
                        "    type=%s, hints=%x, flags=%x, handle=%x, transform=%d, blending=%s\n"
                        "    sourceCrop={%d, %d, %d, %d}, displayFrame={%d, %d, %d, %d}\n"
                        "    visibleRegionScreen=<%d rect(s)>, acquireFenceFd=%d, releaseFenceFd=%d\n",
                i, layer, comp_type_str(layer->compositionType), layer->hints, layer->flags, layer->handle,
                layer->transform, blending_type_str(layer->blending),
                layer->sourceCrop.left, layer->sourceCrop.top, layer->sourceCrop.right, layer->sourceCrop.bottom,
                layer->displayFrame.left, layer->displayFrame.top, layer->displayFrame.right, layer->displayFrame.bottom,
                layer->visibleRegionScreen.numRects, layer->acquireFenceFd, layer->releaseFenceFd);
    }
}

/* For vsync thread synchronization */
static pthread_mutex_t vsync_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t vsync_cond = PTHREAD_COND_INITIALIZER;

void
hwcv11_proc_invalidate(const struct hwc_procs* procs)
{
}

void
hwcv11_proc_vsync(const struct hwc_procs* procs, int disp, int64_t timestamp)
{
    //fprintf(stderr, "%s: procs=%x, disp=%d, timestamp=%.0f\n", __func__, procs, disp, (float)timestamp);
    pthread_mutex_lock(&vsync_mutex);
    pthread_cond_signal(&vsync_cond);
    pthread_mutex_unlock(&vsync_mutex);
}

void
hwcv11_proc_hotplug(const struct hwc_procs* procs, int disp, int connected)
{
}

static hwc_procs_t global_procs = {
    hwcv11_proc_invalidate,
    hwcv11_proc_vsync,
    hwcv11_proc_hotplug,
};

HwComposerBackend_v11::HwComposerBackend_v11(hw_module_t *hwc_module, hw_device_t *hw_device)
    : HwComposerBackend(hwc_module)
    , hwc_device((hwc_composer_device_1_t *)hw_device)
    , hwc_win(NULL)
    , hwc_primaryDisplay(NULL)
    , hwc_displayList(NULL)
    , lastDisplayFence(-1)
{
    qDebug() << __PRETTY_FUNCTION__;
    hwc_device->registerProcs(hwc_device, &global_procs);
    sleepDisplay(false);
}

HwComposerBackend_v11::~HwComposerBackend_v11()
{
    // Destroy the window if it hasn't yet been destroyed
    if (hwc_win != NULL) {
        delete hwc_win;
    }

    // Close the hwcomposer handle
    HWC_PLUGIN_EXPECT_ZERO(hwc_close_1(hwc_device));

    if (hwc_displayList != NULL) {
        free(hwc_displayList);
    }

    if (hwc_primaryDisplay != NULL) {
        free(hwc_primaryDisplay);
    }
}

EGLNativeDisplayType
HwComposerBackend_v11::display()
{
    return EGL_DEFAULT_DISPLAY;
}

EGLNativeWindowType
HwComposerBackend_v11::createWindow(int width, int height)
{
    // We expect that we haven't created a window already, if we had, we
    // would leak stuff, and we want to avoid that for obvious reasons.
    HWC_PLUGIN_EXPECT_NULL(hwc_win);
    HWC_PLUGIN_EXPECT_NULL(hwc_primaryDisplay);
    HWC_PLUGIN_EXPECT_NULL(hwc_displayList);

    hwc_win = new HWComposerNativeWindow(width, height, HAL_PIXEL_FORMAT_RGBA_8888);

    int numLayers = 2;
    size_t neededsize = sizeof(hwc_display_contents_1_t) + (numLayers * sizeof(hwc_layer_1_t));
    hwc_primaryDisplay = (hwc_display_contents_1_t *) malloc(neededsize);
    hwc_displayList = (hwc_display_contents_1_t **) malloc((HWC_NUM_DISPLAY_TYPES) * sizeof(hwc_display_contents_1_t *));

    /* for v1.1 we will have at maximum HWC_NUM_DISPLAY_TYPES displays. However we're
     * using the primary display only but set the second one to NULL as some hwc
     * implementations seem to dereference the second entry in the display array */
    hwc_displayList[0] = hwc_primaryDisplay;
    hwc_displayList[1] = NULL;

    hwc_layer_1_t *layer = NULL;

    qDebug() << "width" << width << "height" << height;

    visible_rect.top = 0;
    visible_rect.left = 0;
    visible_rect.bottom = height;
    visible_rect.right = width;

    layer = &hwc_primaryDisplay->hwLayers[0];
    memset(layer, 0, sizeof(hwc_layer_1_t));
    layer->compositionType = HWC_FRAMEBUFFER;
    layer->hints = 0;
    layer->flags = 0;
    layer->handle = 0;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop = visible_rect;
    layer->displayFrame = visible_rect;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &visible_rect;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;

    layer = &hwc_primaryDisplay->hwLayers[1];
    memset(layer, 0, sizeof(hwc_layer_1_t));
    layer->compositionType = HWC_FRAMEBUFFER_TARGET;
    layer->hints = 0;
    layer->flags = 0;
    layer->handle = 0;
    layer->transform = 0;
    layer->blending = HWC_BLENDING_NONE;
    layer->sourceCrop = visible_rect;
    layer->displayFrame = visible_rect;
    layer->visibleRegionScreen.numRects = 1;
    layer->visibleRegionScreen.rects = &visible_rect;
    layer->acquireFenceFd = -1;
    layer->releaseFenceFd = -1;

    hwc_primaryDisplay->retireFenceFd = -1;
    hwc_primaryDisplay->flags = HWC_GEOMETRY_CHANGED;
    hwc_primaryDisplay->numHwLayers = 2;

    //aosp exynos hwc in particular, checks that these fields are non-null in hwc1.1, although
    //these fields are deprecated in hwc1.1 and later.
    hwc_primaryDisplay->dpy = reinterpret_cast<void*>(0xDECAF);
    hwc_primaryDisplay->sur = reinterpret_cast<void*>(0xC0FFEE);

    return (EGLNativeWindowType) static_cast<ANativeWindow *>(hwc_win);
}

void
HwComposerBackend_v11::destroyWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);

    // FIXME: Implement (delete hwc_win + set it to NULL?)
}

void
HwComposerBackend_v11::swap(EGLNativeDisplayType display, EGLSurface surface)
{
    HWC_PLUGIN_ASSERT_NOT_NULL(hwc_win);

    HWComposerNativeWindowBuffer *buffer;
    int bufferFenceFd;

    hwc_win->lockFrontBuffer(&buffer, &bufferFenceFd);
    if (!buffer)
        return;

    hwc_primaryDisplay->hwLayers[1].handle = buffer->handle;
    hwc_primaryDisplay->hwLayers[1].acquireFenceFd = bufferFenceFd;

    hwc_primaryDisplay->hwLayers[0].handle = NULL;
    hwc_primaryDisplay->hwLayers[0].flags = HWC_SKIP_LAYER;

    HWC_PLUGIN_ASSERT_ZERO(hwc_device->prepare(hwc_device, HWC_NUM_DISPLAY_TYPES, hwc_displayList));

#if 0
    hwc_primaryDisplay->dpy = display;
    hwc_primaryDisplay->sur = surface;
    hwc_primaryDisplay->hwLayers[1].acquireFenceFd = -1;
    hwc_primaryDisplay->hwLayers[1].releaseFenceFd = -1;

    qDebug() << "before prepare";
    dump_display_contents(hwc_primaryDisplay);
    HWC_PLUGIN_ASSERT_ZERO(hwc_device->prepare(hwc_device, 1, hwc_displayList));

    HWC_PLUGIN_ASSERT_NOT_NULL(hwc_win);

    HWComposerNativeWindowBuffer *buffer;
    int bufferFenceFd;

    qDebug() << "before lockFrontBuffer";
    dump_display_contents(hwc_primaryDisplay);
    hwc_win->lockFrontBuffer(&buffer, &bufferFenceFd);

    hwc_primaryDisplay->hwLayers[0].handle = NULL;
    hwc_primaryDisplay->hwLayers[0].flags = HWC_SKIP_LAYER;
    hwc_primaryDisplay->hwLayers[1].compositionType = HWC_FRAMEBUFFER_TARGET;
    hwc_primaryDisplay->hwLayers[1].handle = buffer->handle;
    hwc_primaryDisplay->hwLayers[1].acquireFenceFd = ::dup(bufferFenceFd);

    qDebug() << "before set";
    dump_display_contents(hwc_primaryDisplay);
    HWC_PLUGIN_ASSERT_ZERO(hwc_device->set(hwc_device, 1, hwc_displayList));

    qDebug() << "before unlockFrontBuffer";
    dump_display_contents(hwc_primaryDisplay);

    // TODO make sure the release fence is copied from the framebuffer layer
    int releaseFenceFd = hwc_primaryDisplay->hwLayers[1].releaseFenceFd;
    hwc_win->unlockFrontBuffer(buffer, releaseFenceFd);

    qDebug() << "lastDisplayFence =" << lastDisplayFence;

    if (hwc_primaryDisplay->retireFenceFd != -1) {
        close(hwc_primaryDisplay->retireFenceFd);
        hwc_primaryDisplay->retireFenceFd = -1;
    }

    close(bufferFenceFd);
#endif
}

void
HwComposerBackend_v11::sleepDisplay(bool sleep)
{
    if (sleep) {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 0));
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 1));
    }
    else {
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->blank(hwc_device, 0, 0));
        HWC_PLUGIN_EXPECT_ZERO(hwc_device->eventControl(hwc_device, 0, HWC_EVENT_VSYNC, 1));
    }

    if (!sleep && hwc_primaryDisplay != NULL) {
        hwc_primaryDisplay->flags = HWC_GEOMETRY_CHANGED;
    }
}

float
HwComposerBackend_v11::refreshRate()
{
    // TODO: Implement new hwc 1.1 querying of vsync period per-display
    //
    // from hwcomposer_defs.h:
    // "This query is not used for HWC_DEVICE_API_VERSION_1_1 and later.
    //  Instead, the per-display attribute HWC_DISPLAY_VSYNC_PERIOD is used."
    return 60.0;
}

#endif /* HWC_PLUGIN_HAVE_HWCOMPOSER1_API */
