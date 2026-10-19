// SPDX-License-Identifier: GPL-2.0-or-later
#include "multiview_window.hpp"
#include "multiview_renderer.hpp"
#include "obs_state.hpp"
#include "preset_picker.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMenu>
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

MultiviewWindow::MultiviewWindow(QWidget *parent) : QWidget(parent, Qt::Window)
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
	m_renderer = std::make_unique<MultiviewRenderer>(*m_state);

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
	m_destroying = true;
	/* 1. Stop drawing: removing the draw callback blocks until a callback that
	 *    is running on the graphics thread has returned. */
	destroy_display();
	/* 2. Only now the renderer can go: nothing can call render() any more. */
	if (m_renderer)
		m_renderer->shutdown();
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

void MultiviewWindow::contextMenuEvent(QContextMenuEvent *event)
{
	auto *menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);
	const QPoint global_pos = event->globalPos();
	QAction *layout = menu->addAction(tr("Layout..."));
	connect(layout, &QAction::triggered, this, [this, global_pos] { show_layout_picker(global_pos); });
	menu->popup(global_pos);
	event->accept();
}

void MultiviewWindow::show_layout_picker(const QPoint &global_pos)
{
	if (!m_renderer)
		return;
	auto *picker = new PresetPicker(m_renderer->preset(), this);
	connect(picker, &PresetPicker::preset_chosen, this, [this](Preset preset) {
		if (m_renderer)
			m_renderer->set_preset(preset);
	});
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
