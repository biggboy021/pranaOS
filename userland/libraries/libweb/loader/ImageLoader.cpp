/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <base/Debug.h>
#include <libcore/Timer.h>
#include <libgfx/Bitmap.h>
#include <libweb/dom/Document.h>
#include <libweb/dom/Element.h>
#include <libweb/loader/ImageLoader.h>
#include <libweb/loader/ResourceLoader.h>

namespace Web {

ImageLoader::ImageLoader(DOM::Element& owner_element)
    : m_owner_element(owner_element)
    , m_timer(Core::Timer::construct())
{
}

void ImageLoader::load(const URL& url)
{
    m_loading_state = LoadingState::Loading;

    auto request = LoadRequest::create_for_url_on_page(url, m_owner_element.document().page());
    set_resource(ResourceLoader::the().load_resource(Resource::Type::Image, request));
}

void ImageLoader::set_visible_in_viewport(bool visible_in_viewport) const
{
    if (m_visible_in_viewport == visible_in_viewport)
        return;
    m_visible_in_viewport = visible_in_viewport;

    if (resource())
        const_cast<ImageResource*>(resource())->update_volatility();
}

void ImageLoader::resource_did_load()
{
    VERIFY(resource());

    if (!resource()->mime_type().starts_with("image/")) {
        m_loading_state = LoadingState::Failed;
        if (on_fail)
            on_fail();
        return;
    }

    m_loading_state = LoadingState::Loaded;

    if constexpr (IMAGE_LOADER_DEBUG) {
        if (!resource()->has_encoded_data()) {
            dbgln("ImageLoader: Resource did load, no encoded data. URL: {}", resource()->url());
        } else {
            dbgln("ImageLoader: Resource did load, has encoded data. URL: {}", resource()->url());
        }
    }

    if (resource()->is_animated() && resource()->frame_count() > 1) {
        m_timer->set_interval(resource()->frame_duration(0));
        m_timer->on_timeout = [this] { animate(); };
        m_timer->start();
    }

    if (on_load)
        on_load();
}

void ImageLoader::animate()
{
    if (!m_visible_in_viewport)
        return;

    m_current_frame_index = (m_current_frame_index + 1) % resource()->frame_count();
    auto current_frame_duration = resource()->frame_duration(m_current_frame_index);

    if (current_frame_duration != m_timer->interval()) {
        m_timer->restart(current_frame_duration);
    }

    if (m_current_frame_index == resource()->frame_count() - 1) {
        ++m_loops_completed;
        if (m_loops_completed > 0 && m_loops_completed == resource()->loop_count()) {
            m_timer->stop();
        }
    }

    if (on_animate)
        on_animate();
}

void ImageLoader::resource_did_fail()
{
    dbgln("ImageLoader: Resource did fail. URL: {}", resource()->url());
    m_loading_state = LoadingState::Failed;
    if (on_fail)
        on_fail();
}

bool ImageLoader::has_image() const
{
    if (!resource())
        return false;
    return bitmap(0);
}

unsigned ImageLoader::width() const
{
    if (!resource())
        return 0;
    return bitmap(0) ? bitmap(0)->width() : 0;
}

unsigned ImageLoader::height() const
{
    if (!resource())
        return 0;
    return bitmap(0) ? bitmap(0)->height() : 0;
}

const Gfx::Bitmap* ImageLoader::bitmap(size_t frame_index) const
{
    if (!resource())
        return nullptr;
    return resource()->bitmap(frame_index);
}

}