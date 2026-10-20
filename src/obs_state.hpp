// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "scene_ref.hpp"

#include <obs-frontend-api.h>

#include <QObject>

#include <string>
#include <vector>

/* Adapter over obs-frontend-api state. Must be created, used and destroyed on
 * the Qt UI thread (frontend events are delivered there, so the signals are
 * emitted there too). The frontend callback is registered in the constructor
 * and removed in the destructor. */
class ObsState : public QObject {
	Q_OBJECT

public:
	explicit ObsState(QObject *parent = nullptr);
	~ObsState() override;

	/* All scenes, in frontend order, with their UUID and whether they are
	 * visible in the multiview (private setting "show_in_multiview"). */
	std::vector<SceneRef> scenes() const;
	bool studio_mode() const;
	std::string program_scene() const;
	std::string preview_scene() const;

	/* In studio mode set_preview targets the preview, otherwise the program;
	 * set_program always targets the program. Unknown scene names are a no-op. */
	void set_program(const std::string &scene);
	void set_preview(const std::string &scene);

	/* Studio mode only: sets the preview to `scene`, then triggers the
	 * transition to program (as a double click does natively). Else a no-op.
	 * The frontend applies a preview change asynchronously, so the transition
	 * is triggered once the preview really is `scene` (gives up after 1 s). */
	void transition_to(const std::string &scene);

signals:
	void scenes_changed();
	/* The whole scene set was replaced (collection switch / finished loading).
	 * Emitted after scenes_changed. */
	void scene_collection_changed();
	void program_changed();
	void preview_changed();
	void studio_mode_changed(bool enabled);

private:
	static void on_event(enum obs_frontend_event event, void *data);
	/* libobs "source_rename" (any thread): queues scenes_changed for scenes. */
	static void on_source_rename(void *data, calldata_t *cd);
	void handle_event(enum obs_frontend_event event);
	void disconnect_rename();

	bool scene_exists(const std::string &name) const;
	void fire_pending_transition();

	std::string m_pending_transition; /* scene waiting for its preview to apply */
	bool m_active = true; /* false after OBS_FRONTEND_EVENT_EXIT */
	bool m_rename_connected = false;
};
