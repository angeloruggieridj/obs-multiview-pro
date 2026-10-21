// SPDX-License-Identifier: GPL-2.0-or-later
#include "obs_state.hpp"
#include "click_target.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QTimer>

#include <utility>

ObsState::ObsState(QObject *parent) : QObject(parent)
{
	obs_frontend_add_event_callback(&ObsState::on_event, this);
	/* Renames have no frontend event; tile names and labels must follow. */
	if (signal_handler_t *sh = obs_get_signal_handler()) {
		signal_handler_connect(sh, "source_rename", &ObsState::on_source_rename, this);
		m_rename_connected = true;
	}
}

ObsState::~ObsState()
{
	disconnect_rename();
	obs_frontend_remove_event_callback(&ObsState::on_event, this);
}

void ObsState::disconnect_rename()
{
	if (!m_rename_connected)
		return;
	m_rename_connected = false;
	/* Waits for a running callback (same mutex as the emission). */
	if (signal_handler_t *sh = obs_get_signal_handler())
		signal_handler_disconnect(sh, "source_rename", &ObsState::on_source_rename, this);
}

void ObsState::on_source_rename(void *data, calldata_t *cd)
{
	obs_source_t *src = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!src || obs_source_get_type(src) != OBS_SOURCE_TYPE_SCENE)
		return;
	auto *self = static_cast<ObsState *>(data);
	/* Possibly not the UI thread: hop over. Dropped if self is gone first. */
	QMetaObject::invokeMethod(
		self,
		[self] {
			if (self->m_active)
				emit self->scenes_changed();
		},
		Qt::QueuedConnection);
}

void ObsState::on_event(enum obs_frontend_event event, void *data)
{
	static_cast<ObsState *>(data)->handle_event(event);
}

namespace {

CollectionEvent collection_event(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		return CollectionEvent::Changing;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		return CollectionEvent::Cleanup;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		return CollectionEvent::Loaded;
	case OBS_FRONTEND_EVENT_EXIT:
		return CollectionEvent::Exit;
	default:
		/* The frontend holds back every other event while it loads or
		 * clears a collection (OBSStudioAPI::on_event, disableSaving). */
		return CollectionEvent::Activity;
	}
}

} // namespace

void ObsState::handle_event(enum obs_frontend_event event)
{
	if (!m_active)
		return;

	/* First, before anything reacts to the event: drop every reference
	 * synchronously when OBS is about to clear the scene collection. */
	if (m_gate.on_event(collection_event(event)))
		emit release_sources();

	switch (event) {
	case OBS_FRONTEND_EVENT_EXIT:
		/* Stop touching OBS; the owner may still be alive for a moment. */
		m_active = false;
		disconnect_rename();
		emit scenes_changed();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		emit program_changed();
		break;
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		emit preview_changed();
		fire_pending_transition();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING)
			emit scene_collection_changing();
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		emit scenes_changed();
		if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED || event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
			emit scene_collection_changed();
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
		emit studio_mode_changed(true);
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		emit studio_mode_changed(false);
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		emit streaming_started();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		emit streaming_stopped();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
		emit recording_started();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		emit recording_stopped();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_PAUSED:
		emit recording_paused_changed(true);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_UNPAUSED:
		emit recording_paused_changed(false);
		break;
	default:
		break;
	}
}

bool ObsState::streaming_active() const
{
	return m_active && obs_frontend_streaming_active();
}

bool ObsState::recording_active() const
{
	return m_active && obs_frontend_recording_active();
}

bool ObsState::recording_paused() const
{
	return m_active && obs_frontend_recording_active() && obs_frontend_recording_paused();
}

ObsState::StreamStats ObsState::stream_stats() const
{
	StreamStats st;
	if (!m_active)
		return st;
	/* Counted reference (may be null): always released. */
	obs_output_t *out = obs_frontend_get_streaming_output();
	if (!out)
		return st;
	st.have = true;
	st.total_bytes = obs_output_get_total_bytes(out);
	st.dropped_frames = obs_output_get_frames_dropped(out);
	st.total_frames = obs_output_get_total_frames(out);
	st.congestion = double(obs_output_get_congestion(out));
	obs_output_release(out);
	return st;
}

ObsState::RecordStats ObsState::record_stats() const
{
	RecordStats st;
	if (!m_active)
		return st;
	/* Counted reference (may be null): always released. */
	obs_output_t *out = obs_frontend_get_recording_output();
	if (!out)
		return st;
	st.have = true;
	st.total_bytes = obs_output_get_total_bytes(out);
	st.total_frames = obs_output_get_total_frames(out);
	obs_output_release(out);
	return st;
}

ObsState::ServiceInfo ObsState::streaming_service() const
{
	ServiceInfo info;
	if (!m_active)
		return info;
	/* NOT a counted reference: the frontend returns the pointer it owns
	 * (OBSBasic::GetService, OBS 32.1), so it is not released here. It is only
	 * used on this (UI) thread, right now, and the strings are copied. */
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service)
		return info;
	/* Counted reference (may be null): always released. */
	obs_data_t *settings = obs_service_get_settings(service);
	if (settings) {
		/* rtmp_common stores the chosen service ("YouTube - RTMPS") here; the
		 * stream key is never read. */
		const char *name = obs_data_get_string(settings, "service");
		if (name)
			info.name = name;
		obs_data_release(settings);
	}
	const char *url = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
	if (url)
		info.server_url = url;
	return info;
}

std::string ObsState::last_recording_path() const
{
	if (!m_active)
		return std::string();
	/* bstrdup'ed by the frontend: freed with bfree. */
	char *path = obs_frontend_get_last_recording();
	if (!path)
		return std::string();
	std::string out(path);
	bfree(path);
	return out;
}

std::vector<SceneRef> ObsState::scenes() const
{
	std::vector<SceneRef> out;
	if (!m_active)
		return out;

	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; ++i) {
		obs_source_t *src = list.sources.array[i];
		const char *name = obs_source_get_name(src);
		if (!name || obs_source_removed(src))
			continue;
		SceneRef ref;
		ref.name = name;
		if (const char *uuid = obs_source_get_uuid(src))
			ref.id = uuid;
		/* As Multiview::Update: default true when the key is unset. */
		obs_data_t *priv = obs_source_get_private_settings(src);
		if (priv) {
			obs_data_set_default_bool(priv, "show_in_multiview", true);
			ref.visible_in_multiview = obs_data_get_bool(priv, "show_in_multiview");
			obs_data_release(priv);
		}
		out.push_back(std::move(ref));
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

obs_source_t *ObsState::program_scene_source() const
{
	return m_active ? obs_frontend_get_current_scene() : nullptr;
}

obs_source_t *ObsState::preview_scene_source() const
{
	if (!m_active || !studio_mode())
		return nullptr;
	return obs_frontend_get_current_preview_scene();
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

void ObsState::transition_to(const std::string &scene)
{
	if (!m_active || !studio_mode() || !scene_exists(scene))
		return;
	m_pending_transition = scene;
	set_preview(scene);
	fire_pending_transition(); /* already applied? */
	if (!m_pending_transition.empty()) {
		QTimer::singleShot(1000, this, [this, scene] {
			if (m_pending_transition == scene)
				m_pending_transition.clear();
		});
	}
}

void ObsState::fire_pending_transition()
{
	if (m_pending_transition.empty() || !m_active || !studio_mode() || preview_scene() != m_pending_transition)
		return;
	m_pending_transition.clear();
	/* Not from inside the frontend event callback. */
	QTimer::singleShot(0, this, [] { obs_frontend_preview_program_trigger_transition(); });
}
