// SPDX-License-Identifier: GPL-2.0-or-later
#include "obs_state.hpp"
#include "click_target.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

ObsState::ObsState(QObject *parent) : QObject(parent)
{
	obs_frontend_add_event_callback(&ObsState::on_event, this);
}

ObsState::~ObsState()
{
	obs_frontend_remove_event_callback(&ObsState::on_event, this);
}

void ObsState::on_event(enum obs_frontend_event event, void *data)
{
	static_cast<ObsState *>(data)->handle_event(event);
}

void ObsState::handle_event(enum obs_frontend_event event)
{
	if (!m_active)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_EXIT:
		/* Stop touching OBS; the owner may still be alive for a moment. */
		m_active = false;
		emit scenes_changed();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		emit program_changed();
		break;
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		emit preview_changed();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		emit scenes_changed();
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
		emit studio_mode_changed(true);
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		emit studio_mode_changed(false);
		break;
	default:
		break;
	}
}

std::vector<SceneRef> ObsState::scenes() const
{
	std::vector<SceneRef> out;
	if (!m_active)
		return out;

	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; ++i) {
		const char *name = obs_source_get_name(list.sources.array[i]);
		if (name)
			out.push_back(SceneRef{name});
	}
	obs_frontend_source_list_free(&list);
	return out;
}

bool ObsState::studio_mode() const
{
	return m_active && obs_frontend_preview_program_mode_active();
}

static std::string name_of(obs_source_t *src)
{
	if (!src)
		return std::string();
	const char *n = obs_source_get_name(src);
	std::string s = n ? n : "";
	obs_source_release(src);
	return s;
}

std::string ObsState::program_scene() const
{
	if (!m_active)
		return std::string();
	return name_of(obs_frontend_get_current_scene());
}

std::string ObsState::preview_scene() const
{
	if (!m_active || !studio_mode())
		return std::string();
	return name_of(obs_frontend_get_current_preview_scene());
}

bool ObsState::scene_exists(const std::string &name) const
{
	if (name.empty())
		return false;
	for (const SceneRef &s : scenes())
		if (s.name == name)
			return true;
	return false;
}

void ObsState::set_program(const std::string &scene)
{
	if (!m_active || !scene_exists(scene))
		return;
	obs_source_t *src = obs_get_source_by_name(scene.c_str());
	if (!src)
		return;
	obs_frontend_set_current_scene(src);
	obs_source_release(src);
}

void ObsState::set_preview(const std::string &scene)
{
	if (!m_active || !scene_exists(scene))
		return;
	obs_source_t *src = obs_get_source_by_name(scene.c_str());
	if (!src)
		return;
	if (target_for_click(studio_mode()) == "preview")
		obs_frontend_set_current_preview_scene(src);
	else
		obs_frontend_set_current_scene(src);
	obs_source_release(src);
}
