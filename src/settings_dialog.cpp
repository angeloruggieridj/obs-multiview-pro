// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings_dialog.hpp"

#include "i18n.hpp"
#include "settings_store.hpp"
#include "update_logic.hpp"
#include "update_manager.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QByteArray>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFragment>
#include <QUrl>
#include <QVBoxLayout>
#include <QShowEvent>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <string>
#include <utility>
#include <vector>

namespace {

constexpr const char *kRepositoryUrl = "https://github.com/angeloruggieridj/obs-multiview-pro";

QColor to_qcolor(uint32_t rgb)
{
	return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF));
}

uint32_t from_qcolor(const QColor &c)
{
	return (uint32_t(c.red()) << 16) | (uint32_t(c.green()) << 8) | uint32_t(c.blue());
}

QPointer<SettingsDialog> g_dialog; /* UI thread only */

constexpr const char *kReleaseTagUrl = "https://github.com/angeloruggieridj/obs-multiview-pro/releases/tag/";

/* Release notes come from the network: nothing they reference is ever loaded
 * (images, style sheets, other documents), and links are only reported
 * through anchorClicked, never followed by the widget.
 *
 * loadResource answers every request with a NON-null value. Qt falls back to
 * loading the name itself when it gets nothing usable: QTextDocument opens it
 * as a local file after a null QVariant, and the image handler calls
 * QImage::load(name) after an empty or undecodable image. A "//host/x.png" or
 * "file://host/x.png" name then becomes a UNC path: an SMB connection made
 * synchronously on the UI thread (measured: about 10 s with a local probe).
 * Images get a 1x1 transparent QImage, everything else an empty byte array. */
class OfflineTextBrowser : public QTextBrowser {
public:
	using QTextBrowser::QTextBrowser;

protected:
	QVariant loadResource(int type, const QUrl &) override
	{
		if (type == QTextDocument::ImageResource) {
			QImage blank(1, 1, QImage::Format_ARGB32);
			blank.fill(Qt::transparent);
			return QVariant(blank);
		}
		return QVariant(QByteArray());
	}
};

/* Removes every image from `doc` (belt and braces: the notes were already
 * stripped of image syntax, see strip_markdown_images). */
void remove_images(QTextDocument *doc)
{
	std::vector<std::pair<int, int>> spans; /* position, length */
	for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
		for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
			const QTextFragment f = it.fragment();
			if (f.isValid() && f.charFormat().isImageFormat())
				spans.emplace_back(f.position(), f.length());
		}
	for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
		QTextCursor c(doc);
		c.setPosition(it->first);
		c.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
		c.removeSelectedText();
	}
}

/* Opens `url` in the browser only when it is a page of this project. */
void open_release_url(const std::string &url)
{
	if (is_allowed_release_url(url))
		QDesktopServices::openUrl(QUrl(QString::fromStdString(url), QUrl::StrictMode));
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(T("Dialog.Title"));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);

	auto *tabs = new QTabWidget(this);
	m_tabs = tabs;
	tabs->addTab(build_appearance_page(), T("Dialog.Tab.Appearance"));
	m_updates_page = build_updates_page();
	tabs->addTab(m_updates_page, T("Dialog.Tab.Updates"));
	tabs->addTab(build_about_page(), T("Dialog.Tab.About"));

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
	/* The plugin's own texts: Qt would otherwise use its own translations. */
	m_buttons->button(QDialogButtonBox::Ok)->setText(T("Dialog.Button.OK"));
	m_buttons->button(QDialogButtonBox::Cancel)->setText(T("Dialog.Button.Cancel"));
	m_buttons->button(QDialogButtonBox::Apply)->setText(T("Dialog.Button.Apply"));
	/* Enter in the hex field must only finish the edit, never press OK. */
	for (QAbstractButton *b : m_buttons->buttons())
		if (auto *pb = qobject_cast<QPushButton *>(b)) {
			pb->setAutoDefault(false);
			pb->setDefault(false);
		}
	m_preview_timer.setSingleShot(true);
	m_preview_timer.setInterval(50);
	connect(&m_preview_timer, &QTimer::timeout, this, [this] { SettingsStore::instance().preview_appearance(m_current); });
	connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
		apply();
		accept();
	});
	connect(m_buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
	connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { apply(); });

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(tabs);
	layout->addWidget(m_buttons);
	setMinimumWidth(460);

	/* "Always on top" and the tile meters can also be set from a window's
	 * menu: follow them. */
	connect(&SettingsStore::instance(), &SettingsStore::appearance_saved, this,
		[this](const Appearance &a) { follow_saved(a); });
	/* What the windows show (saved, previewed or taken back): stay above them
	 * while they are on top. */
	connect(&SettingsStore::instance(), &SettingsStore::appearance_changed, this,
		[this](const Appearance &a) { set_on_top(a.always_on_top); });
	connect(&UpdateManager::instance(), &UpdateManager::state_changed, this, [this] { show_update_state(); });
	connect(&UpdateManager::instance(), &UpdateManager::marker_changed, this, [this] { show_update_state(); });
	reload();
	show_update_state();
	m_keep_on_top = m_saved.always_on_top;
}

void SettingsDialog::show_page(SettingsPage page)
{
	switch (page) {
	case SettingsPage::Keep:
		break;
	case SettingsPage::Appearance:
		m_tabs->setCurrentIndex(0);
		break;
	case SettingsPage::Updates:
		m_tabs->setCurrentWidget(m_updates_page);
		break;
	case SettingsPage::About:
		m_tabs->setCurrentIndex(m_tabs->count() - 1);
		break;
	}
}

QWidget *SettingsDialog::build_appearance_page()
{
	auto *page = new QWidget;
	auto *form = new QFormLayout;

	/* Frame colour: swatch button (opens the colour dialog) + hex field. */
	m_swatch = new QPushButton;
	m_swatch->setFixedSize(56, 26);
	m_swatch->setToolTip(T("Dialog.Appearance.ChooseBorderColor"));
	m_hex = new QLineEdit;
	m_hex->setMaxLength(7);
	m_hex->setPlaceholderText(QStringLiteral("#999999"));
	m_hex->setValidator(
		new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^#?[0-9A-Fa-f]{0,6}$")), m_hex));
	m_hex->setFixedWidth(96);
	auto *color_row = new QHBoxLayout;
	color_row->addWidget(m_swatch);
	color_row->addWidget(m_hex);
	color_row->addStretch(1);
	form->addRow(T("Dialog.Appearance.BorderColor"), color_row);
	connect(m_swatch, &QPushButton::clicked, this, [this] { pick_color(); });
	connect(m_hex, &QLineEdit::textEdited, this, [this](const QString &text) {
		uint32_t rgb;
		if (m_updating || !parse_color(text.toStdString(), rgb))
			return;
		Appearance a = m_current;
		a.border_rgb = rgb;
		values_edited(a, false);
	});
	/* Done typing: show the canonical form (or the last valid value). */
	connect(m_hex, &QLineEdit::editingFinished, this, [this] { show_values(m_current); });

	auto add_percent = [&](const QString &label, QSlider *&slider, QSpinBox *&spin, int Appearance::*field) {
		slider = new QSlider(Qt::Horizontal);
		slider->setRange(0, 100);
		spin = new QSpinBox;
		spin->setRange(0, 100);
		spin->setSuffix(QStringLiteral(" %"));
		auto *row = new QHBoxLayout;
		row->addWidget(slider, 1);
		row->addWidget(spin);
		form->addRow(label, row);
		auto changed = [this, field](int v) {
			if (m_updating)
				return;
			Appearance a = m_current;
			a.*field = v;
			values_edited(a);
		};
		connect(slider, &QSlider::valueChanged, this, changed);
		connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, changed);
	};
	add_percent(T("Dialog.Appearance.LabelOpacity"), m_label_slider, m_label_spin, &Appearance::label_opacity);
	add_percent(T("Dialog.Appearance.MeterOpacity"), m_meter_slider, m_meter_spin, &Appearance::meter_opacity);

	m_tile_meters = new QCheckBox(T("Dialog.Appearance.TileMeters"));
	form->addRow(m_tile_meters);
	connect(m_tile_meters, &QCheckBox::toggled, this, [this](bool on) {
		if (m_updating)
			return;
		Appearance a = m_current;
		a.tile_meters = on;
		values_edited(a);
	});

	m_on_top = new QCheckBox(T("Dialog.Appearance.AlwaysOnTop"));
	form->addRow(m_on_top);
	connect(m_on_top, &QCheckBox::toggled, this, [this](bool on) {
		if (m_updating)
			return;
		Appearance a = m_current;
		a.always_on_top = on;
		values_edited(a);
	});

	auto *hint = new QLabel(T("Dialog.Appearance.Hint"));
	hint->setWordWrap(true);
	auto *reset = new QPushButton(T("Dialog.Appearance.Reset"));
	reset->setAutoDefault(false);
	connect(reset, &QPushButton::clicked, this, [this] { values_edited(Appearance()); });

	auto *outer = new QVBoxLayout(page);
	outer->addLayout(form);
	outer->addWidget(hint);
	auto *reset_row = new QHBoxLayout;
	reset_row->addWidget(reset);
	reset_row->addStretch(1);
	outer->addLayout(reset_row);
	outer->addStretch(1);
	return page;
}

QWidget *SettingsDialog::build_updates_page()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);

	layout->addWidget(new QLabel(T("Dialog.Updates.InstalledVersion", QString::fromUtf8(PLUGIN_VERSION))));

	m_update_status = new QLabel;
	m_update_status->setWordWrap(true);
	m_update_status->setTextFormat(Qt::PlainText);
	QFont bold = m_update_status->font();
	bold.setBold(true);
	m_update_status->setFont(bold);
	m_check_now = new QPushButton(T("Dialog.Updates.CheckNow"));
	m_check_now->setAutoDefault(false);
	auto *status_row = new QHBoxLayout;
	status_row->addWidget(m_update_status, 1);
	status_row->addWidget(m_check_now);
	layout->addLayout(status_row);
	connect(m_check_now, &QPushButton::clicked, this,
		[this] { UpdateManager::instance().check_now(m_prereleases->isChecked()); });

	m_auto_check = new QCheckBox(T("Dialog.Updates.AutoCheck"));
	m_prereleases = new QCheckBox(T("Dialog.Updates.IncludePrereleases"));
	layout->addWidget(m_auto_check);
	layout->addWidget(m_prereleases);
	connect(m_auto_check, &QCheckBox::toggled, this, [this] { update_buttons(); });
	connect(m_prereleases, &QCheckBox::toggled, this, [this] { update_buttons(); });

	m_changelog = new OfflineTextBrowser;
	m_changelog->setOpenLinks(false);
	m_changelog->setOpenExternalLinks(false);
	m_changelog->setPlaceholderText(T("Dialog.Updates.ChangelogPlaceholder"));
	m_changelog->setMinimumHeight(180);
	connect(m_changelog, &QTextBrowser::anchorClicked, this,
		[](const QUrl &url) { open_release_url(url.toString(QUrl::FullyEncoded).toStdString()); });
	layout->addWidget(m_changelog, 1);

	m_open_page = new QPushButton(T("Dialog.Updates.OpenDownloadPage"));
	m_open_page->setAutoDefault(false);
	m_skip = new QPushButton(T("Dialog.Updates.SkipVersion"));
	m_skip->setAutoDefault(false);
	auto *buttons = new QHBoxLayout;
	buttons->addWidget(m_open_page);
	buttons->addWidget(m_skip);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	connect(m_open_page, &QPushButton::clicked, this, [] {
		const UpdateManager &m = UpdateManager::instance();
		if (m.state().status == UpdateStatus::UpdateAvailable)
			open_release_url(m.state().url);
		else if (!m.marker_version().empty())
			open_release_url(kReleaseTagUrl + m.marker_version());
	});
	connect(m_skip, &QPushButton::clicked, this, [] { UpdateManager::instance().skip_latest(); });

	auto *hint = new QLabel(T("Dialog.Updates.Privacy"));
	hint->setWordWrap(true);
	layout->addWidget(hint);
	return page;
}

void SettingsDialog::show_update_state()
{
	if (!m_update_status)
		return;
	const UpdateManager &m = UpdateManager::instance();
	const UpdateState &st = m.state();
	const std::string skipped = SettingsStore::instance().updates().skipped_version;
	const std::string &marker = m.marker_version();

	const StatusText line = update_status_text(st, skipped);
	QString status = line.arg.empty() ? T(line.key) : T(line.key, QString::fromStdString(line.arg));
	bool can_open = false;
	bool can_skip = false;
	std::string markdown;
	switch (st.status) {
	case UpdateStatus::Idle:
		if (!marker.empty()) {
			status = T("Update.Status.AvailableEarlier", QString::fromStdString(version_for_display(marker)));
			can_open = true;
			can_skip = true;
		}
		break;
	case UpdateStatus::UpdateAvailable:
		can_open = !st.url.empty();
		can_skip = st.latest != skipped;
		markdown = st.changelog;
		break;
	case UpdateStatus::UpToDate:
		markdown = st.installed_notes;
		break;
	case UpdateStatus::Checking:
	case UpdateStatus::NoReleases:
	case UpdateStatus::Error:
		break;
	}

	m_update_status->setText(status);
	m_check_now->setEnabled(!m.busy());
	m_open_page->setEnabled(can_open);
	m_skip->setEnabled(can_skip);
	/* Raw HTML in the notes stays text; image syntax was stripped, any image
	 * left is removed, and OfflineTextBrowser loads no resource anyway. */
	m_changelog->document()->setMarkdown(QString::fromStdString(markdown),
					     QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub |
									     QTextDocument::MarkdownNoHTML));
	remove_images(m_changelog->document());
}

QWidget *SettingsDialog::build_about_page()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);

	auto *title = new QLabel(T("Dialog.About.Title"));
	QFont f = title->font();
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.4);
	title->setFont(f);
	layout->addWidget(title);
	layout->addWidget(new QLabel(T("Dialog.About.Version", QString::fromUtf8(PLUGIN_VERSION))));
	layout->addWidget(new QLabel(T("Dialog.About.License")));
	layout->addWidget(new QLabel(T("Dialog.About.Copyright")));

	auto *link = new QLabel(QStringLiteral("<a href=\"%1\">%1</a>").arg(QString::fromLatin1(kRepositoryUrl)));
	link->setTextFormat(Qt::RichText);
	link->setTextInteractionFlags(Qt::TextBrowserInteraction);
	link->setOpenExternalLinks(false);
	/* Only this exact address is ever opened. */
	connect(link, &QLabel::linkActivated, this, [](const QString &url) {
		if (url == QLatin1String(kRepositoryUrl))
			QDesktopServices::openUrl(QUrl(QString::fromLatin1(kRepositoryUrl)));
	});
	layout->addWidget(link);
	layout->addStretch(1);
	return page;
}

void SettingsDialog::reload()
{
	SettingsStore &store = SettingsStore::instance();
	m_saved = store.appearance();
	const UpdateSettings u = store.updates();
	m_saved_auto_check = u.auto_check;
	m_saved_prereleases = u.include_prereleases;
	m_auto_check->setChecked(u.auto_check);
	m_prereleases->setChecked(u.include_prereleases);
	show_values(m_saved);
}

void SettingsDialog::show_values(const Appearance &a, bool rewrite_hex)
{
	m_current = normalized(a);
	m_updating = true;
	if (rewrite_hex)
		m_hex->setText(QString::fromStdString(format_color(m_current.border_rgb)));
	m_label_slider->setValue(m_current.label_opacity);
	m_label_spin->setValue(m_current.label_opacity);
	m_meter_slider->setValue(m_current.meter_opacity);
	m_meter_spin->setValue(m_current.meter_opacity);
	m_on_top->setChecked(m_current.always_on_top);
	m_tile_meters->setChecked(m_current.tile_meters);

	const qreal dpr = devicePixelRatioF();
	QPixmap pm(QSize(int(40 * dpr), int(14 * dpr)));
	pm.setDevicePixelRatio(dpr);
	pm.fill(to_qcolor(m_current.border_rgb));
	{
		QPainter p(&pm);
		p.setPen(QColor(0x80, 0x80, 0x80));
		p.drawRect(0, 0, 39, 13);
	}
	m_swatch->setIcon(QIcon(pm));
	m_swatch->setIconSize(QSize(40, 14));
	m_updating = false;
	update_buttons();
}

void SettingsDialog::values_edited(const Appearance &a, bool rewrite_hex)
{
	show_values(a, rewrite_hex);
	m_preview_timer.start(); /* restarts: the preview follows the last change */
}

/* The saved toggles (always on top, tile meters) changed here or from a
 * window's menu: the boxes show them, and a preview of other unsaved values
 * stays on screen with them. The result is always sent to the windows: the
 * store emits only appearance_saved when the saved value did not change, and
 * the windows may still show a preview of the other value. */
void SettingsDialog::follow_saved(const Appearance &saved)
{
	m_saved.always_on_top = saved.always_on_top;
	m_saved.tile_meters = saved.tile_meters;
	const Appearance shown = follow_saved_toggles(m_current, saved);
	if (shown != m_current)
		show_values(shown);
	m_preview_timer.stop();
	SettingsStore::instance().preview_appearance(m_current);
	update_buttons();
}

void SettingsDialog::set_on_top(bool on)
{
	if (on == m_keep_on_top)
		return;
	m_keep_on_top = on;
	apply_on_top();
	/* A window that turned topmost in the same signal, after this slot, goes
	 * to the top of the band: come back above it once the event is done. */
	if (on)
		QTimer::singleShot(0, this, [this] { apply_on_top(); });
}

void SettingsDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	apply_on_top();
}

void SettingsDialog::apply_on_top()
{
#if defined(_WIN32)
	/* Only the z-order band of the existing native windows: no hide / show.
	 * The colour chooser is owned by this dialog: it follows it, and is set
	 * explicitly too. */
	auto apply = [this](QWidget *w) {
		if (!w || !w->isVisible())
			return;
		SetWindowPos(reinterpret_cast<HWND>(w->winId()), m_keep_on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0,
			     0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	};
	apply(this);
	apply(m_color_dialog.data());
#else
	/* A flag change re-creates the native window, hidden: show it again. */
	auto apply = [this](QWidget *w) {
		if (!w || w->windowFlags().testFlag(Qt::WindowStaysOnTopHint) == m_keep_on_top)
			return;
		const bool visible = w->isVisible();
		w->setWindowFlag(Qt::WindowStaysOnTopHint, m_keep_on_top);
		if (visible)
			w->show();
	};
	apply(this);
	apply(m_color_dialog.data());
#endif
}

void SettingsDialog::pick_color()
{
	/* No nested event loop (QColorDialog::getColor would keep a stack dialog
	 * alive across the loop, and OBS may delete this dialog meanwhile): the
	 * chooser lives on the heap as a child of this dialog, so it dies with it. */
	if (m_color_dialog) {
		m_color_dialog->raise();
		m_color_dialog->activateWindow();
		return;
	}
	auto *chooser = new QColorDialog(to_qcolor(m_current.border_rgb), this);
	chooser->setAttribute(Qt::WA_DeleteOnClose);
	chooser->setWindowTitle(T("Dialog.Appearance.BorderColor"));
	m_color_dialog = chooser;
	/* Only OK sends colorSelected; Cancel / closing changes nothing. */
	connect(chooser, &QColorDialog::colorSelected, this, [this](const QColor &c) {
		if (!c.isValid())
			return;
		Appearance a = m_current;
		a.border_rgb = from_qcolor(c);
		values_edited(a);
	});
	chooser->open();
	apply_on_top(); /* above topmost windows too */
}

void SettingsDialog::apply()
{
	m_preview_timer.stop();
	SettingsStore &store = SettingsStore::instance();
	store.set_appearance(m_current);
	m_saved = store.appearance();
	/* Windows show exactly what is saved (also when a preview of another
	 * value was still on screen and set_appearance had nothing to change). */
	store.preview_appearance(m_saved);

	UpdateSettings u = store.updates();
	u.auto_check = m_auto_check->isChecked();
	u.include_prereleases = m_prereleases->isChecked();
	store.set_updates(u);
	m_saved_auto_check = u.auto_check;
	m_saved_prereleases = u.include_prereleases;
	update_buttons();
}

void SettingsDialog::update_buttons()
{
	if (QPushButton *apply_button = m_buttons ? m_buttons->button(QDialogButtonBox::Apply) : nullptr)
		apply_button->setEnabled(m_current != m_saved ||
					 (m_auto_check && m_auto_check->isChecked() != m_saved_auto_check) ||
					 (m_prereleases && m_prereleases->isChecked() != m_saved_prereleases));
}

/* Enter in a field only finishes that edit: it must not press OK (the button
 * box would make OK the default button). On a button it works as usual. */
void SettingsDialog::keyPressEvent(QKeyEvent *event)
{
	if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
	    !qobject_cast<QAbstractButton *>(focusWidget())) {
		if (focusWidget() == m_hex)
			show_values(m_current);
		event->accept();
		return;
	}
	QDialog::keyPressEvent(event);
}

void SettingsDialog::reject()
{
	/* Takes the preview back (a pending one is dropped). */
	m_preview_timer.stop();
	SettingsStore &store = SettingsStore::instance();
	store.preview_appearance(store.appearance());
	QDialog::reject();
}

void open_settings_dialog(SettingsPage page)
{
	if (!g_dialog) {
		auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
		g_dialog = new SettingsDialog(parent);
	}
	g_dialog->show_page(page);
	g_dialog->show();
	g_dialog->raise();
	g_dialog->activateWindow();
}

void close_settings_dialog()
{
	if (!g_dialog)
		return;
	SettingsDialog *d = g_dialog.data();
	d->setAttribute(Qt::WA_DeleteOnClose, false);
	d->reject();
	delete d;
}
