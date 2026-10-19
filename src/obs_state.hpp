// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <obs-frontend-api.h>

#include <QObject>

#include <string>
#include <vector>

/* A scene is referenced by name only; it is resolved when needed. */
struct SceneRef {
	std::string name;
};

/* Adapter over obs-frontend-api state. Must be created, used and destroyed on
 * the Qt UI thread (frontend events are delivered there, so the signals are
 * emitted there too). The frontend callback is registered in the constructor
 * and removed in the destructor. */
class ObsState : public QObject {
	Q_OBJECT

public:
	explicit ObsState(QObject *parent = nullptr);
	~ObsState() override;

	std::vector<SceneRef> scenes() const;
	bool studio_mode() const;
	std::string program_scene() const;
	std::string preview_scene() const;

	/* In studio mode set_preview targets the preview, otherwise the program;
	 * set_program always targets the program. Unknown scene names are a no-op. */
	void set_program(const std::string &scene);
	void set_preview(const std::string &scene);

signals:
	void scenes_changed();
	void program_changed();
	void preview_changed();
	void studio_mode_changed(bool enabled);

private:
	static void on_event(enum obs_frontend_event event, void *data);
	void handle_event(enum obs_frontend_event event);

	bool scene_exists(const std::string &name) const;

	bool m_active = true; /* false after OBS_FRONTEND_EVENT_EXIT */
};
