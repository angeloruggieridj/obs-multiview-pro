// SPDX-License-Identifier: GPL-2.0-or-later
#include "doctest.h"

#include "collection_gate.hpp"

TEST_CASE("state_collection_gate_starts_open")
{
	CollectionGate g;
	CHECK(g.sources_allowed());
	/* activity while open changes nothing and asks for no release */
	CHECK_FALSE(g.on_event(CollectionEvent::Activity));
	CHECK_FALSE(g.on_event(CollectionEvent::Loaded));
	CHECK(g.sources_allowed());
}

TEST_CASE("state_collection_gate_switch_releases_and_stays_idle")
{
	CollectionGate g;
	/* OBS: CHANGING, (save), CLEANUP while clearing, then CHANGED */
	CHECK(g.on_event(CollectionEvent::Changing));
	CHECK_FALSE(g.sources_allowed());
	CHECK(g.on_event(CollectionEvent::Cleanup)); /* released again, synchronously */
	CHECK_FALSE(g.sources_allowed());
	CHECK(g.on_event(CollectionEvent::Loaded) == false);
	CHECK(g.sources_allowed());
}

TEST_CASE("state_collection_gate_cleanup_without_changing")
{
	/* ClearSceneData also runs at shutdown and when a load fails */
	CollectionGate g;
	CHECK(g.on_event(CollectionEvent::Cleanup));
	CHECK_FALSE(g.sources_allowed());
	CHECK(g.on_event(CollectionEvent::Cleanup)); /* a second clearing releases again */
	CHECK_FALSE(g.sources_allowed());
}

TEST_CASE("state_collection_gate_reopens_on_frontend_activity")
{
	/* Frontend events other than CLEANUP / EXIT are not delivered while OBS
	 * is loading or clearing: one arriving means the collection is usable. */
	CollectionGate g;
	g.on_event(CollectionEvent::Changing);
	CHECK_FALSE(g.on_event(CollectionEvent::Activity));
	CHECK(g.sources_allowed());
	g.on_event(CollectionEvent::Cleanup);
	g.on_event(CollectionEvent::Loaded);
	CHECK(g.sources_allowed());
}

TEST_CASE("state_collection_gate_exit_is_final")
{
	CollectionGate g;
	CHECK(g.on_event(CollectionEvent::Exit));
	CHECK_FALSE(g.sources_allowed());
	CHECK_FALSE(g.on_event(CollectionEvent::Loaded));
	CHECK_FALSE(g.on_event(CollectionEvent::Activity));
	CHECK_FALSE(g.sources_allowed());
	/* nothing to release twice once closed */
	CHECK_FALSE(g.on_event(CollectionEvent::Cleanup));
	CHECK_FALSE(g.on_event(CollectionEvent::Exit));
}

TEST_CASE("state_collection_gate_many_switches")
{
	CollectionGate g;
	for (int i = 0; i < 10; ++i) {
		CHECK(g.on_event(CollectionEvent::Changing));
		CHECK(g.on_event(CollectionEvent::Cleanup));
		CHECK_FALSE(g.sources_allowed());
		g.on_event(CollectionEvent::Loaded);
		CHECK(g.sources_allowed());
	}
}
