// SPDX-License-Identifier: GPL-2.0-or-later
#include "multiview_window.hpp"
#include "layout_model.hpp"
#include "multiview_renderer.hpp"
#include "obs_state.hpp"
#include "preset_picker.hpp"
#include "settings_dialog.hpp"
#include "settings_store.hpp"
#include "update_logic.hpp"
#include "update_manager.hpp"
#include "status_monitor.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QHideEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QWindow>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

/* Background of the area outside the canvas (letterbox), as the native
 * multiview projector sets it (OBSProjector: obs_display_set_background_color 0). */
static constexpr uint32_t kDisplayBackground = 0x000000;

static int scene_slot_count(Preset preset)
{
	int n = 0;
	for (const Tile &t : build_layout(preset))
		if (t.kind == TileKind::Scene)
			n = (std::max)(n, t.scene_slot + 1);
	return n;
}

static std::string current_collection_name()
{
	char *name = obs_frontend_get_current_scene_collection();
	std::string out = name ? name : "";
	bfree(name);
	return out;
}

MultiviewWindow::MultiviewWindow(int instance, QWidget *parent) : QWidget(parent, Qt::Window), m_instance(instance)
{
	/* Same attributes as OBS's OBSQTDisplay (all public Qt API). */
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_DeleteOnClose);
	/* Like OBSProjector: closing this window must never quit OBS (e.g. when
	 * the main window is hidden in the tray). */
	setAttribute(Qt::WA_QuitOnClose, false);
	setWindowTitle(QStringLiteral("Multiview"));
	resize(960, 540);

	m_state = std::make_unique<ObsState>();
	/* Connected before the renderer creates its own scenes_changed handler,
	 * so the assignment is up to date when the renderer re-resolves the slots
	 * (slots run in connection order). */
	connect(m_state.get(), &ObsState::scenes_changed, this, &MultiviewWindow::on_scenes_changed);
	connect(m_state.get(), &ObsState::scene_collection_changing, this,
		&MultiviewWindow::on_scene_collection_changing);
	connect(m_state.get(), &ObsState::scene_collection_changed, this,
		&MultiviewWindow::on_scene_collection_changed);
	/* Appearance (global): read now, then followed live. */
	m_renderer = std::make_unique<MultiviewRenderer>(*m_state, SettingsStore::instance().appearance());
	connect(&SettingsStore::instance(), &SettingsStore::appearance_changed, this, [this](const Appearance &a) {
		if (m_renderer)
			m_renderer->set_appearance(a);
	});
	/* Saved options first (the slot count depends on the preset), then the
	 * saved assignment of the current scene collection. Nothing is written
	 * until the user changes something. */
	{
		const WindowSettings saved = SettingsStore::instance().get(m_instance);
		m_renderer->set_audio_meters(saved.meters);
		m_renderer->set_safe_areas(saved.safe_areas);
		m_renderer->set_preset(saved.preset);
		load_assignment();
	}
	/* Called only on the UI thread, from MultiviewRenderer::refresh_scenes. */
	m_renderer->set_scene_for_slot([this](int slot) { return m_assignment.scene_for_slot(slot); });

	/* Status sampling (ON AIR / REC / clock tiles): runs only while the layout
	 * has those tiles (see apply_preset). */
	m_monitor = std::make_unique<StatusMonitor>(*m_state);
	connect(m_monitor.get(), &StatusMonitor::snapshot_changed, this, [this](const StatusSnapshot &status) {
		if (m_renderer)
			m_renderer->set_status(status);
	});
	m_monitor->set_enabled(preset_has_status(m_renderer->preset()));

	/* Labels follow the window size once a resize settles (not on every step
	 * of a live resize); until then the old ones are drawn scaled. */
	m_label_scale_timer.setSingleShot(true);
	m_label_scale_timer.setInterval(150);
	connect(&m_label_scale_timer, &QTimer::timeout, this, &MultiviewWindow::update_label_scale);

	QWindow *wh = windowHandle();
	if (!wh) {
		winId(); /* force creation of the native window */
		wh = windowHandle();
	}
	if (wh) {
		connect(wh, &QWindow::visibleChanged, this, [this](bool visible) {
			if (!visible) {
#if !defined(_WIN32) && !defined(__APPLE__)
				/* The native surface goes away when hidden (as OBSQTDisplay). */
				destroy_display();
#endif
				return;
			}
			if (!m_display)
				create_display();
			else
				resize_display();
		});
		connect(wh, &QWindow::screenChanged, this, [this](QScreen *) {
			create_display();
			resize_display();
			update_color_space();
			update_label_scale(); /* DPI may differ */
		});
		wh->installEventFilter(this);
		/* Same marker as OBSProjector: "Hide OBS windows from screen capture"
		 * leaves projector-like windows capturable (OBSBasic::SetDisplayAffinity). */
		wh->setProperty("isOBSProjectorWindow", true);
	}

	obs_log(LOG_INFO, "MultiviewWindow created");
}

MultiviewWindow::~MultiviewWindow()
{
	/* Final save, before anything is torn down: the store already holds every
	 * change, this writes the pending ones now (the OBS exit path closes the
	 * windows synchronously). */
	SettingsStore::instance().flush();
	m_destroying = true;
	m_label_scale_timer.stop();
	/* 1. Stop drawing: removing the draw callback blocks until a callback that
	 *    is running on the graphics thread has returned. */
	destroy_display();
	/* 2. Only now the renderer can go: nothing can call render() any more. */
	if (m_renderer)
		m_renderer->shutdown();
	m_monitor.reset(); /* before the ObsState it observes */
	m_renderer.reset();
	m_state.reset();
	obs_log(LOG_INFO, "MultiviewWindow destroyed");
}

QPaintEngine *MultiviewWindow::paintEngine() const
{
	return nullptr;
}

QSize MultiviewWindow::pixel_size() const
{
	return size() * devicePixelRatioF();
}

void MultiviewWindow::draw_callback(void *param, uint32_t cx, uint32_t cy)
{
	/* Graphics thread. m_renderer is set before the display exists and reset
	 * only after the callback has been removed, so it is valid here. */
	static_cast<MultiviewWindow *>(param)->m_renderer->render(cx, cy);
}

void MultiviewWindow::create_display()
{
	if (m_display || m_destroying || !m_renderer)
		return;

	QWindow *wh = windowHandle();
	if (!wh || !wh->isExposed())
		return;

	const QSize size = pixel_size();
	if (size.width() <= 0 || size.height() <= 0)
		return;

	gs_init_data info = {};
	info.cx = static_cast<uint32_t>(size.width());
	info.cy = static_cast<uint32_t>(size.height());
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;

	/* The only platform-specific part: gs_window differs per platform
	 * (libobs/graphics/graphics.h). Only Windows is tested. */
	bool ok = true;
#if defined(_WIN32)
	info.window.hwnd = reinterpret_cast<void *>(wh->winId());
#elif defined(__APPLE__)
	info.window.view = (id)wh->winId();
#elif defined(__linux__) || defined(__FreeBSD__)
	switch (obs_get_nix_platform()) {
	case OBS_NIX_PLATFORM_X11_EGL:
		info.window.id = static_cast<uint32_t>(wh->winId());
		info.window.display = obs_get_nix_platform_display();
		break;
	case OBS_NIX_PLATFORM_WAYLAND:
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
		/* From Qt 6.9 winId() of a Wayland QWindow is the wl_surface*. */
		info.window.display = reinterpret_cast<void *>(wh->winId());
		ok = info.window.display != nullptr;
#else
		/* Needs Qt private API before 6.9: not supported. */
		ok = false;
#endif
		break;
	default:
		ok = false;
		break;
	}
#else
	ok = false;
#endif
	if (!ok) {
		if (!m_window_handle_warned) {
			m_window_handle_warned = true;
			obs_log(LOG_WARNING,
				"cannot use this window for an OBS display on this platform "
				"(Wayland needs Qt >= 6.9; try QT_QPA_PLATFORM=xcb); the multiview stays empty");
		}
		return;
	}

	m_display = obs_display_create(&info, kDisplayBackground);
	if (!m_display) {
		if (!m_window_handle_warned) {
			m_window_handle_warned = true;
			obs_log(LOG_WARNING, "obs_display_create failed; the multiview stays empty");
		}
		return;
	}
	obs_display_add_draw_callback(m_display, &MultiviewWindow::draw_callback, this);
	update_label_scale();
}

void MultiviewWindow::destroy_display()
{
	if (!m_display)
		return;
	obs_display_remove_draw_callback(m_display, &MultiviewWindow::draw_callback, this);
	obs_display_destroy(m_display);
	m_display = nullptr;
}

void MultiviewWindow::resize_display()
{
	if (!m_display || !isVisible())
		return;
	const QSize size = pixel_size();
	if (size.width() <= 0 || size.height() <= 0)
		return;
	obs_display_resize(m_display, static_cast<uint32_t>(size.width()), static_cast<uint32_t>(size.height()));
}

void MultiviewWindow::update_color_space()
{
	if (m_display)
		obs_display_update_color_space(m_display);
}

void MultiviewWindow::paintEvent(QPaintEvent *)
{
	create_display();
}

void MultiviewWindow::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	create_display();
	resize_display();
	m_label_scale_timer.start();
}

void MultiviewWindow::update_label_scale()
{
	if (!m_renderer || m_destroying)
		return;
	const QSize size = pixel_size();
	m_renderer->set_display_size(size.width(), size.height());
}

void MultiviewWindow::moveEvent(QMoveEvent *event)
{
	QWidget::moveEvent(event);
	update_color_space();
}

bool MultiviewWindow::nativeEvent(const QByteArray &, void *message, qintptr *)
{
#ifdef _WIN32
	const MSG &msg = *static_cast<MSG *>(message);
	if (msg.message == WM_DISPLAYCHANGE)
		update_color_space();
#else
	(void)message;
#endif
	return false;
}

bool MultiviewWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::PlatformSurface && watched == windowHandle()) {
		auto *surface_event = static_cast<QPlatformSurfaceEvent *>(event);
		if (surface_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
			destroy_display();
	}
	return QWidget::eventFilter(watched, event);
}

int MultiviewWindow::scene_slot_at(const QPoint &pos) const
{
	obs_video_info ovi;
	if (!m_renderer || !obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
		return -1;
	const qreal dpr = devicePixelRatioF();
	const float aspect = float(ovi.base_width) / float(ovi.base_height);
	const RectF fit = fit_rect(aspect, float(width() * dpr), float(height() * dpr));
	if (fit.w <= 0.f || fit.h <= 0.f)
		return -1;
	const float nx = (float(pos.x() * dpr) - fit.x) / fit.w;
	const float ny = (float(pos.y() * dpr) - fit.y) / fit.h;
	if (nx < 0.f || ny < 0.f || nx >= 1.f || ny >= 1.f)
		return -1;
	const std::vector<Tile> tiles = build_layout(m_renderer->preset());
	const int idx = hit_test(tiles, nx, ny);
	if (idx < 0 || tiles[static_cast<size_t>(idx)].kind != TileKind::Scene)
		return -1;
	return tiles[static_cast<size_t>(idx)].scene_slot;
}

int MultiviewWindow::label_slot_at(const QPoint &pos) const
{
	obs_video_info ovi;
	if (!m_renderer || !obs_get_video_info(&ovi) || ovi.base_width == 0 || ovi.base_height == 0)
		return -1;
	/* Widget -> display pixels -> canvas pixels (same mapping as the draw). */
	const qreal dpr = devicePixelRatioF();
	const float aspect = float(ovi.base_width) / float(ovi.base_height);
	const RectF fit = fit_rect(aspect, float(width() * dpr), float(height() * dpr));
	if (fit.w <= 0.f || fit.h <= 0.f)
		return -1;
	const float cx = (float(pos.x() * dpr) - fit.x) / fit.w * float(ovi.base_width);
	const float cy = (float(pos.y() * dpr) - fit.y) / fit.h * float(ovi.base_height);
	return m_renderer->label_slot_at(cx, cy);
}

void MultiviewWindow::assignment_changed_no_save()
{
	if (m_renderer)
		m_renderer->refresh_scenes();
}

/* Every user-driven change of the assignment goes through here. */
void MultiviewWindow::assignment_changed()
{
	assignment_changed_no_save();
	save_assignment();
}

/* Re-syncs the slot names (by source UUID) with the scene list. Scene renames
 * have no frontend event; ObsState turns the libobs "source_rename" signal
 * into a (queued) scenes_changed, so names and labels follow a rename on the
 * next UI event loop turn. User actions still call this first, so they never
 * resolve a stale name even inside that window. */
void MultiviewWindow::sync_scene_names()
{
	if (!m_state || !m_renderer)
		return;
	std::vector<std::string> before;
	const int count = scene_slot_count(m_renderer->preset());
	for (int i = 0; i < count; ++i)
		before.push_back(m_assignment.scene_for_slot(i));
	m_assignment.on_scene_list_changed(m_state->scenes());
	for (int i = 0; i < count; ++i)
		if (before[static_cast<size_t>(i)] != m_assignment.scene_for_slot(i)) {
			assignment_changed();
			return;
		}
}

void MultiviewWindow::on_scenes_changed()
{
	/* The renderer refreshes itself from the same signal. */
	if (!m_state || !m_renderer)
		return;
	const int count = scene_slot_count(m_renderer->preset());
	const std::vector<std::string> before = m_assignment.names(count);
	m_assignment.on_scene_list_changed(m_state->scenes());
	/* A rename follows the new name in the file. A scene that disappeared is
	 * not written here: during a collection switch or at exit the list is
	 * transiently empty, and the saved name is ignored on load anyway if the
	 * scene is really gone. */
	const std::vector<std::string> after = m_assignment.names(count);
	for (size_t i = 0; i < after.size(); ++i)
		if (!before[i].empty() && !after[i].empty() && before[i] != after[i]) {
			save_assignment();
			break;
		}
}

void MultiviewWindow::on_scene_collection_changing()
{
	m_switching = true;
	m_switch_clock.start();
}

/* A collection load runs synchronously on the UI thread, so no user action can
 * be handled during one; seeing "switching" this long afterwards means the
 * CHANGED event never came. Treat the switch as over and pick up whatever
 * collection is current, so saving does not stay disabled for good. */
bool MultiviewWindow::switching_now()
{
	constexpr qint64 kStaleSwitchMs = 30000;
	if (m_switching && m_switch_clock.hasExpired(kStaleSwitchMs)) {
		if (m_state && m_renderer && current_collection_name() != m_collection)
			on_scene_collection_changed();
		else
			m_switching = false;
	}
	return m_switching;
}

void MultiviewWindow::on_scene_collection_changed()
{
	if (!m_state || !m_renderer) {
		m_switching = false;
		return;
	}
	/* The assignment of the new collection (default fill when it has none).
	 * The old one was saved with every change, nothing to write here. */
	load_assignment();
	m_switching = false; /* m_collection and the slots belong to the new collection */
	assignment_changed_no_save();
}

void MultiviewWindow::load_assignment()
{
	const int count = scene_slot_count(m_renderer->preset());
	m_collection = current_collection_name();
	const WindowSettings saved = SettingsStore::instance().get(m_instance);
	const auto it = saved.collections.find(m_collection);
	if (it != saved.collections.end())
		m_assignment.load(m_state->scenes(), it->second, count);
	else
		m_assignment.reset_from(m_state->scenes(), count);
}

void MultiviewWindow::save_options()
{
	if (!m_renderer)
		return;
	SettingsStore::instance().set_options(m_instance, m_renderer->preset(), m_renderer->safe_areas(),
					      m_renderer->audio_meters());
}

void MultiviewWindow::save_assignment()
{
	if (!m_renderer || !may_save_assignment(switching_now(), m_collection, current_collection_name()))
		return;
	SettingsStore::instance().set_assignment(m_instance, m_collection,
						 m_assignment.names(scene_slot_count(m_renderer->preset())));
}

void MultiviewWindow::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		/* On a scene label: no click, no drag; the release opens the scene
		 * menu (native "click on the name"). */
		m_label_press_slot = label_slot_at(event->pos());
		if (m_label_press_slot >= 0) {
			m_press_active = false;
			m_dragging = false;
			m_press_slot = -1;
			event->accept();
			return;
		}
		m_press_active = true;
		m_dragging = false;
		m_press_pos = event->pos();
		m_press_slot = scene_slot_at(event->pos());
		m_press_scene = m_press_slot >= 0 ? m_assignment.scene_for_slot(m_press_slot) : std::string();
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void MultiviewWindow::mouseMoveEvent(QMouseEvent *event)
{
	if (m_press_active && !m_dragging && (event->buttons() & Qt::LeftButton) && m_press_slot >= 0 &&
	    (event->pos() - m_press_pos).manhattanLength() >= QApplication::startDragDistance() &&
	    !m_assignment.scene_for_slot(m_press_slot).empty()) {
		m_dragging = true;
		setCursor(Qt::ClosedHandCursor);
	}
	QWidget::mouseMoveEvent(event);
}

void MultiviewWindow::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && m_label_press_slot >= 0) {
		const int slot = m_label_press_slot;
		m_label_press_slot = -1;
		/* Released on the same label: open the menu. Names first, as for any
		 * user action (a rename may not have produced an event yet). */
		sync_scene_names();
		if (label_slot_at(event->pos()) == slot)
			show_scene_menu(slot, event->globalPosition().toPoint());
		event->accept();
		return;
	}
	if (event->button() != Qt::LeftButton || !m_press_active) {
		QWidget::mouseReleaseEvent(event);
		return;
	}
	/* Names are resolved below: bring them up to date first (a rename may not
	 * have produced a scene-list event). */
	sync_scene_names();
	const bool was_dragging = m_dragging;
	const int press_slot = m_press_slot;
	m_press_active = false;
	m_dragging = false;
	m_press_slot = -1;
	unsetCursor();

	const int target = scene_slot_at(event->pos());
	if (was_dragging) {
		/* Dropped outside any scene tile: cancelled. So is a drop of a tile
		 * whose scene changed since the press (a rename, a removal). */
		if (target >= 0 && target != press_slot && m_assignment.scene_for_slot(press_slot) == m_press_scene) {
			m_assignment.swap(press_slot, target);
			assignment_changed();
		}
	} else if (press_slot >= 0 && target == press_slot && m_state) {
		/* A click: set_preview picks preview (studio mode) or program. An
		 * empty tile is a no-op. */
		const std::string scene = m_assignment.scene_for_slot(target);
		if (!scene.empty())
			m_state->set_preview(scene);
	}
	event->accept();
}

void MultiviewWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton) {
		QWidget::mouseDoubleClickEvent(event);
		return;
	}
	/* The release that follows must not act as another click. */
	m_press_active = false;
	m_dragging = false;
	m_press_slot = -1;
	m_label_press_slot = -1;
	if (!m_state)
		return;
	sync_scene_names();
	const int slot = scene_slot_at(event->pos());
	const std::string scene = slot >= 0 ? m_assignment.scene_for_slot(slot) : std::string();
	if (!scene.empty())
		m_state->transition_to(scene); /* studio mode only */
	event->accept();
}

void MultiviewWindow::cancel_press()
{
	m_press_active = false;
	m_dragging = false;
	m_press_slot = -1;
	m_label_press_slot = -1;
	unsetCursor();
}

void MultiviewWindow::focusOutEvent(QFocusEvent *event)
{
	cancel_press();
	QWidget::focusOutEvent(event);
}

void MultiviewWindow::hideEvent(QHideEvent *event)
{
	cancel_press();
	QWidget::hideEvent(event);
}

void MultiviewWindow::contextMenuEvent(QContextMenuEvent *event)
{
	cancel_press();
	sync_scene_names();
	auto *menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);
	const QPoint global_pos = event->globalPos();

	/* A check found a newer version: say so first, quietly (never a popup). */
	if (const UpdateManager *updates = UpdateManager::existing(); updates && !updates->marker_version().empty()) {
		const QString version = QString::fromStdString(version_for_display(updates->marker_version()));
		QAction *update = menu->addAction(QStringLiteral("Update available: %1...").arg(version));
		connect(update, &QAction::triggered, this, [] { open_settings_dialog(SettingsPage::Updates); });
		menu->addSeparator();
	}

	const int slot = scene_slot_at(event->pos());
	if (slot >= 0 && m_state) {
		const std::string current = m_assignment.scene_for_slot(slot);
		add_scene_choices(menu->addMenu(current.empty() ? tr("Add scene...") : tr("Assign scene")), slot);
		if (!current.empty()) {
			QAction *rm = menu->addAction(tr("Remove scene from tile"));
			connect(rm, &QAction::triggered, this, [this, slot] {
				sync_scene_names();
				m_assignment.remove(slot);
				assignment_changed();
			});
		}
		menu->addSeparator();
	}

	QAction *layout = menu->addAction(tr("Layout..."));
	connect(layout, &QAction::triggered, this, [this, global_pos] { show_layout_picker(global_pos); });

	/* No button bar in this window: the toggle lives in the menu. */
	if (m_renderer) {
		QAction *meters = menu->addAction(tr("Audio meters"));
		meters->setCheckable(true);
		meters->setChecked(m_renderer->audio_meters());
		connect(meters, &QAction::toggled, this, [this](bool on) {
			if (m_renderer) {
				m_renderer->set_audio_meters(on);
				save_options();
			}
		});
		QAction *safe = menu->addAction(tr("Safe areas"));
		safe->setCheckable(true);
		safe->setChecked(m_renderer->safe_areas());
		connect(safe, &QAction::toggled, this, [this](bool on) {
			if (m_renderer) {
				m_renderer->set_safe_areas(on);
				save_options();
			}
		});
	}
	menu->addSeparator();
	QAction *settings = menu->addAction(tr("Settings..."));
	connect(settings, &QAction::triggered, this, [] { open_settings_dialog(); });
	menu->popup(global_pos);
	event->accept();
}

void MultiviewWindow::add_scene_choices(QMenu *menu, int slot)
{
	if (!m_state)
		return;
	const std::string current = m_assignment.scene_for_slot(slot);
	bool any = false;
	for (const SceneRef &s : m_state->scenes()) {
		if (!s.visible_in_multiview)
			continue;
		any = true;
		const std::string name = s.name;
		QAction *a = menu->addAction(QString::fromStdString(name).replace('&', QStringLiteral("&&")));
		a->setCheckable(true);
		a->setChecked(name == current);
		connect(a, &QAction::triggered, this, [this, slot, name] {
			/* The scene may have been deleted while the menu was open. */
			if (!m_state)
				return;
			sync_scene_names();
			for (const SceneRef &sc : m_state->scenes())
				if (sc.name == name) {
					m_assignment.assign(slot, name);
					assignment_changed();
					return;
				}
		});
	}
	if (!any)
		menu->addAction(tr("(no scenes)"))->setEnabled(false);
}

void MultiviewWindow::show_scene_menu(int slot, const QPoint &global_pos)
{
	if (slot < 0 || !m_state)
		return;
	cancel_press();
	auto *menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);
	add_scene_choices(menu, slot);
	menu->popup(global_pos);
}

void MultiviewWindow::apply_preset(Preset preset)
{
	if (!m_renderer || !m_state)
		return;
	/* Assignment first: set_preset refreshes the slots once, by name, so the
	 * names must be current. Status tiles are not scene slots: the count is
	 * that of the Scene tiles only. */
	sync_scene_names();
	m_assignment.set_slot_count(m_state->scenes(), scene_slot_count(preset));
	if (m_monitor)
		m_monitor->set_enabled(preset_has_status(preset));
	m_renderer->set_preset(preset);
	save_options();
	save_assignment();
}

void MultiviewWindow::show_layout_picker(const QPoint &global_pos)
{
	if (!m_renderer)
		return;
	auto *picker = new PresetPicker(m_renderer->preset(), this);
	connect(picker, &PresetPicker::preset_chosen, this, [this](Preset preset) { apply_preset(preset); });
	/* Keep the popup on the screen it opens on. */
	QPoint pos = global_pos;
	if (const QScreen *screen = this->screen()) {
		const QRect avail = screen->availableGeometry();
		pos.setX(qBound(avail.left(), pos.x(), qMax(avail.left(), avail.right() - picker->width())));
		pos.setY(qBound(avail.top(), pos.y(), qMax(avail.top(), avail.bottom() - picker->height())));
	}
	picker->move(pos);
	picker->show();
}
