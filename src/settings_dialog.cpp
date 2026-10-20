// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings_dialog.hpp"

#include "settings_store.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QAbstractButton>
#include <QColor>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
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
#include <QUrl>
#include <QVBoxLayout>

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

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(tr("Multiview Settings"));
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowFlag(Qt::WindowContextHelpButtonHint, false);

	auto *tabs = new QTabWidget(this);
	/* Explicit English: the plugin has no translations, and Qt would translate
	 * these stock texts on its own. */
	tabs->addTab(build_appearance_page(), QStringLiteral("Appearance"));
	tabs->addTab(build_about_page(), QStringLiteral("About"));

	m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
	m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("OK"));
	m_buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancel"));
	m_buttons->button(QDialogButtonBox::Apply)->setText(QStringLiteral("Apply"));
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

	reload();
}

QWidget *SettingsDialog::build_appearance_page()
{
	auto *page = new QWidget;
	auto *form = new QFormLayout;

	/* Frame colour: swatch button (opens the colour dialog) + hex field. */
	m_swatch = new QPushButton;
	m_swatch->setFixedSize(56, 26);
	m_swatch->setToolTip(tr("Choose the border colour"));
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
	form->addRow(tr("Border colour"), color_row);
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
	add_percent(tr("Label opacity"), m_label_slider, m_label_spin, &Appearance::label_opacity);
	add_percent(tr("Meter opacity"), m_meter_slider, m_meter_spin, &Appearance::meter_opacity);

	auto *hint = new QLabel(tr("Border colour: the frame under and around the tiles. Label opacity: scene names, "
				   "Preview and Program (50 % is the OBS look). Meter opacity: the audio meters "
				   "(100 % is the OBS look)."));
	hint->setWordWrap(true);
	auto *reset = new QPushButton(QStringLiteral("Reset to defaults"));
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

QWidget *SettingsDialog::build_about_page()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);

	auto *title = new QLabel(QStringLiteral("Multiview Pro for OBS"));
	QFont f = title->font();
	f.setBold(true);
	f.setPointSizeF(f.pointSizeF() * 1.4);
	title->setFont(f);
	layout->addWidget(title);
	layout->addWidget(new QLabel(tr("Version %1").arg(QString::fromUtf8(PLUGIN_VERSION))));
	layout->addWidget(new QLabel(tr("License: GPL-2.0-or-later")));
	layout->addWidget(new QLabel(QStringLiteral("Copyright (C) 2026 Angelo Ruggieri")));

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
	m_saved = SettingsStore::instance().appearance();
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

void SettingsDialog::pick_color()
{
	/* getColor runs a nested event loop: the dialog may be deleted meanwhile. */
	QPointer<SettingsDialog> self(this);
	const QColor c = QColorDialog::getColor(to_qcolor(m_current.border_rgb), this, tr("Border colour"));
	if (!self || !c.isValid())
		return;
	Appearance a = m_current;
	a.border_rgb = from_qcolor(c);
	values_edited(a);
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
	update_buttons();
}

void SettingsDialog::update_buttons()
{
	if (QPushButton *apply_button = m_buttons ? m_buttons->button(QDialogButtonBox::Apply) : nullptr)
		apply_button->setEnabled(m_current != m_saved);
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

void open_settings_dialog()
{
	if (!g_dialog) {
		auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
		g_dialog = new SettingsDialog(parent);
	}
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
