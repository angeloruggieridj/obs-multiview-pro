// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "appearance.hpp"

#include <QDialog>
#include <QPointer>
#include <QTimer>

class QCheckBox;
class QColorDialog;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTextBrowser;
class QPushButton;
class QSlider;
class QSpinBox;
class QDialogButtonBox;

/* Pages of the settings dialog. */
enum class SettingsPage { Keep, Appearance, Updates, About };

/* The plugin settings: an "Appearance" tab (frame colour under the tiles,
 * label opacity, meter opacity, windows always on top), an "Updates" tab (installed version, check
 * now, automatic check and pre-release options, change log of the newer
 * releases, download page, skip) and an "About" tab.
 *
 * Appearance changes preview live in every open multiview window
 * (SettingsStore::preview_appearance, nothing is saved). Apply saves them and
 * the two update options, OK saves and closes, Cancel (or closing the dialog)
 * takes the preview back and drops unsaved option changes. "Check for updates
 * now" and "Skip this version" act at once.
 *
 * The preview is sent 50 ms after the last change (trailing edge).
 *
 * UI thread only. Not modal, so the windows can be watched while editing. */
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(QWidget *parent = nullptr);

	void show_page(SettingsPage page);

	/* Shows the saved values again (the dialog is reused while it is open). */
	void reload();

	void reject() override;

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	QWidget *build_appearance_page();
	QWidget *build_updates_page();
	QWidget *build_about_page();
	/* Shows UpdateManager's state in the Updates tab. */
	void show_update_state();
	/* Puts `a` in the controls (the hex field only when `rewrite_hex`, so a
	 * half-typed value is not replaced); nothing is previewed. */
	void show_values(const Appearance &a, bool rewrite_hex = true);
	/* The user changed a value: show it and preview it in the windows. */
	void values_edited(const Appearance &a, bool rewrite_hex = true);
	void follow_saved_on_top(bool on);
	void pick_color();
	void apply();
	void update_buttons();

	Appearance m_saved;   /* what the store holds */
	Appearance m_current; /* what the controls show */
	bool m_saved_auto_check = true;
	bool m_saved_prereleases = false;
	bool m_updating = false;
	/* Coalesces the preview: every window re-rasterizes its labels for a new
	 * opacity, so a slider drag must not trigger one per step. */
	QTimer m_preview_timer;

	/* The open colour chooser (a child of this dialog, non-modal-loop: deleted
	 * with the dialog, never on the stack). */
	QPointer<QColorDialog> m_color_dialog;
	QPushButton *m_swatch = nullptr;
	QLineEdit *m_hex = nullptr;
	QSlider *m_label_slider = nullptr;
	QSpinBox *m_label_spin = nullptr;
	QSlider *m_meter_slider = nullptr;
	QSpinBox *m_meter_spin = nullptr;
	QCheckBox *m_on_top = nullptr;
	QDialogButtonBox *m_buttons = nullptr;

	QTabWidget *m_tabs = nullptr;
	QWidget *m_updates_page = nullptr;
	QLabel *m_update_status = nullptr;
	QPushButton *m_check_now = nullptr;
	QCheckBox *m_auto_check = nullptr;
	QCheckBox *m_prereleases = nullptr;
	QTextBrowser *m_changelog = nullptr;
	QPushButton *m_open_page = nullptr;
	QPushButton *m_skip = nullptr;
};

/* Tools menu / context menu entry point: opens the dialog, or raises the one
 * that is open (at most one), on `page`. UI thread. */
void open_settings_dialog(SettingsPage page = SettingsPage::Keep);
/* Closes and deletes the dialog if it is open, taking back any preview. Call
 * before the windows and the store go away (OBS exit, plugin unload). */
void close_settings_dialog();
