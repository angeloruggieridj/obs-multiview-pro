// SPDX-License-Identifier: GPL-2.0-or-later
#include "collection_gate.hpp"

bool CollectionGate::on_event(CollectionEvent event)
{
	if (m_state == State::Closed)
		return false;
	switch (event) {
	case CollectionEvent::Changing:
	case CollectionEvent::Cleanup:
		m_state = State::Clearing;
		return true;
	case CollectionEvent::Exit:
		m_state = State::Closed;
		return true;
	case CollectionEvent::Loaded:
	case CollectionEvent::Activity:
		m_state = State::Open;
		return false;
	}
	return false;
}
