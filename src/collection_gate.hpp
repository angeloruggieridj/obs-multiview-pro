// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// When the multiview may hold counted references of sources (pure, no OBS).
//
// While OBS switches, creates, imports, duplicates or deletes a scene
// collection it removes every source (OBSBasic::ClearSceneData), waits for them
// to be destroyed and then checks that none is left: one reference still held
// by a plugin makes OBS report a "source leak" and close. The frontend sends
// SCENE_COLLECTION_CHANGING before that, SCENE_COLLECTION_CLEANUP in the middle
// of it (after obs_source_remove, before the check), and nothing else until
// the new collection is loaded (every other frontend event is held back while
// OBS is loading or clearing). Meanwhile OBS still delivers posted Qt events,
// so a queued refresh could try to take sources again.
//
// The gate closes at CHANGING and CLEANUP (the owner releases every reference
// at once, synchronously) and stays closed until the collection is loaded
// (SCENE_COLLECTION_CHANGED, FINISHED_LOADING) or any other frontend event
// shows that OBS is no longer loading. EXIT closes it for good.

enum class CollectionEvent {
	Changing, /* OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING */
	Cleanup,  /* OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP */
	Loaded,   /* OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED, FINISHED_LOADING */
	Activity, /* any other frontend event */
	Exit,     /* OBS_FRONTEND_EVENT_EXIT */
};

class CollectionGate {
public:
	/* Returns true when every reference of a source must be released now. */
	bool on_event(CollectionEvent event);
	/* Sources may be taken (and kept) now. */
	bool sources_allowed() const { return m_state == State::Open; }

private:
	enum class State { Open, Clearing, Closed };
	State m_state = State::Open;
};
